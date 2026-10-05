/* The lv2 side of ps3_host.h: mutexes, condition variables, PPU threads, the timebase, the
mmapper calls of the Xbox memory window, and lv2 file system calls (thread-safe, unlike newlib's). */

#include "ps3_host.h"
#include "ps3_system.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ppu-asm.h>
#include <ppu-lv2.h>
#include <ppu-types.h>
#include <sys/cond.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <sys/mutex.h>
#include <sys/process.h>
#include <sys/systime.h>
#include <sys/thread.h>

/* ---------- errors */

/* lv2's CELL_E* codes are 0x800100xx; the low byte numbers them */
static int errno_from_lv2(s32 result)
{
	if (result >= 0)
		return 0;
	switch ((u32)result & 0xff)
	{
	case 0x01: return EAGAIN;
	case 0x02: return EINVAL;
	case 0x04: return ENOMEM;
	case 0x06: return ENOENT;
	case 0x09: return EPERM;
	case 0x0a: return EBUSY;
	case 0x0b: return ETIMEDOUT;
	case 0x12: return EISDIR;
	case 0x14: return EEXIST;
	case 0x22: return ENFILE;
	case 0x23: return ENOSPC;
	case 0x26: return EROFS;
	case 0x29: return EACCES;
	case 0x2a: return EBADF;
	case 0x2b: return EIO;
	case 0x2c: return EMFILE;
	case 0x2e: return ENOTDIR;
	case 0x34: return ENAMETOOLONG;
	case 0x36: return ENOTEMPTY;
	default: return EIO;
	}
}

/* ---------- log and process */

void ps3_host_vlog(const char *format, va_list arguments)
{
	ps3_log_vprintf(format, arguments);
}

void ps3_host_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	ps3_log_vprintf(format, arguments);
	va_end(arguments);
}

void ps3_host_log_text(const char *text, uint32_t length)
{
	ps3_log_printf("%.*s", (int)length, text);
}

int ps3_host_exit_requested(void)
{
	return ps3_exit_requested();
}

uint32_t ps3_host_free_memory_mb(void)
{
	return ps3_memory_free_mb();
}

const char *ps3_host_data_root(void)
{
	return ps3_data_root();
}

/* ---------- locks and condition variables */

struct ps3_host_lock
{
	sys_mutex_t mutex;
};

struct ps3_host_condition
{
	sys_cond_t condition;
};

/* the lock and condition waits in progress, with their callers, for the heartbeat (ps3_host_report_blocked) */
#define BLOCKED_SLOTS 32

static struct
{
	volatile uint64_t thread;
	volatile uint64_t since;
	void *volatile callers[3];
} blocked[BLOCKED_SLOTS];

/* the return addresses of the waiting function's callers, from the back chain of this thread's own stack,
which ends at a zero link */
static int __attribute__((noinline)) blocked_begin(void)
{
	sys_ppu_thread_t thread = 0;
	uint64_t frames[5];
	int slot, level;

	frames[0] = (uint64_t)__builtin_frame_address(0);
	for (level = 1; level < 5; level++)
	{
		uint64_t next = frames[level - 1] ? *(volatile uint64_t *)frames[level - 1] : 0;

		frames[level] = next > frames[level - 1] && next - frames[level - 1] < 0x100000 && !(next & 15) ? next : 0;
	}
	sysThreadGetId(&thread);
	for (slot = 0; slot < BLOCKED_SLOTS; slot++)
	{
		if (__sync_bool_compare_and_swap(&blocked[slot].thread, 0, (uint64_t)thread + 1))
		{
			for (level = 0; level < 3; level++)
				blocked[slot].callers[level] = frames[level + 2] ? *(void *volatile *)(frames[level + 2] + 16) : NULL;
			blocked[slot].since = ps3_host_microseconds();
			return slot;
		}
	}
	return -1;
}

static void blocked_end(int slot)
{
	if (slot >= 0)
		blocked[slot].thread = 0;
}

void ps3_host_report_blocked(void)
{
	uint64_t now = ps3_host_microseconds();
	int slot;

	for (slot = 0; slot < BLOCKED_SLOTS; slot++)
	{
		uint64_t thread = blocked[slot].thread;
		uint64_t since = blocked[slot].since;

		if (thread && now > since && now - since > 2000000)
			ps3_log_printf("[host] thread %llx blocked %llu s, callers %p %p %p\n", (unsigned long long)(thread - 1),
				(unsigned long long)((now - since) / 1000000), blocked[slot].callers[0], blocked[slot].callers[1],
				blocked[slot].callers[2]);
	}
}

struct ps3_host_lock *ps3_host_lock_create(int recursive)
{
	struct ps3_host_lock *lock = malloc(sizeof(*lock));
	sys_mutex_attr_t attributes;

	if (!lock)
		return NULL;
	sysMutexAttrInitialize(attributes);
	if (recursive)
		attributes.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
	if (sysMutexCreate(&lock->mutex, &attributes) != 0)
	{
		ps3_log_printf("[host] sysMutexCreate failed\n");
		free(lock);
		return NULL;
	}
	return lock;
}

void ps3_host_lock_destroy(struct ps3_host_lock *lock)
{
	if (lock)
	{
		sysMutexDestroy(lock->mutex);
		free(lock);
	}
}

void ps3_host_lock_enter(struct ps3_host_lock *lock)
{
	int slot = blocked_begin();

	sysMutexLock(lock->mutex, 0);
	blocked_end(slot);
}

int ps3_host_lock_try_enter(struct ps3_host_lock *lock)
{
	return sysMutexTryLock(lock->mutex) == 0;
}

void ps3_host_lock_leave(struct ps3_host_lock *lock)
{
	sysMutexUnlock(lock->mutex);
}

struct ps3_host_condition *ps3_host_condition_create(struct ps3_host_lock *lock)
{
	struct ps3_host_condition *condition = malloc(sizeof(*condition));
	sys_cond_attr_t attributes;

	if (!condition)
		return NULL;
	sysCondAttrInitialize(attributes);
	if (sysCondCreate(&condition->condition, lock->mutex, &attributes) != 0)
	{
		ps3_log_printf("[host] sysCondCreate failed\n");
		free(condition);
		return NULL;
	}
	return condition;
}

int ps3_host_condition_wait(struct ps3_host_condition *condition, uint64_t timeout_microseconds)
{
	int slot = blocked_begin();
	s32 result = sysCondWait(condition->condition, timeout_microseconds);

	blocked_end(slot);
	return errno_from_lv2(result) == ETIMEDOUT;
}

void ps3_host_condition_broadcast(struct ps3_host_condition *condition)
{
	sysCondBroadcast(condition->condition);
}

/* ---------- threads */

struct thread_start
{
	void (*entry)(void *argument);
	void *argument;
};

/* a PPU thread must end in sysThreadExit, not return */
static void thread_main(void *context)
{
	struct thread_start start = *(struct thread_start *)context;

	free(context);
	start.entry(start.argument);
	sysThreadExit(0);
}

int ps3_host_thread_create(void (*entry)(void *argument), void *argument, uint32_t stack_size, int priority,
	const char *name)
{
	struct thread_start *start = malloc(sizeof(*start));
	sys_ppu_thread_t thread;
	char thread_name[28];
	s32 result;

	if (!start)
		return ENOMEM;
	start->entry = entry;
	start->argument = argument;
	/* (sysThreadCreate takes a writable name) */
	snprintf(thread_name, sizeof(thread_name), "%s", name ? name : "halo");
	result = sysThreadCreate(&thread, thread_main, start, priority, stack_size, 0, thread_name);
	if (result != 0)
	{
		ps3_log_printf("[host] sysThreadCreate %s failed 0x%08x\n", thread_name, (unsigned)result);
		free(start);
		return errno_from_lv2(result);
	}
	return 0;
}

uint64_t ps3_host_thread_id(void)
{
	sys_ppu_thread_t thread = 0;

	sysThreadGetId(&thread);
	return thread;
}

void ps3_host_thread_yield(void)
{
	sysThreadYield();
}

void ps3_host_sleep_microseconds(uint64_t microseconds)
{
	while (microseconds)
	{
		u32 part = microseconds > 1000000000u ? 1000000000u : (u32)microseconds;

		sysUsleep(part);
		microseconds -= part;
	}
}

/* ---------- time */

static u64 timebase_frequency;

uint64_t ps3_host_microseconds(void)
{
	u64 ticks = __gettime();

	if (!timebase_frequency)
		timebase_frequency = sysGetTimebaseFrequency();
	return ticks / timebase_frequency * 1000000u + ticks % timebase_frequency * 1000000u / timebase_frequency;
}

void ps3_host_time_of_day(uint64_t *seconds, uint32_t *nanoseconds)
{
	u64 sec = 0, nsec = 0;

	sysGetCurrentTime(&sec, &nsec);
	*seconds = sec;
	*nanoseconds = (uint32_t)nsec;
}

/* ---------- the Xbox memory window */

/* the flags of PSL1GHT's own heap (librt sbrk.c): 1 MB pages, any access right */
#define WINDOW_AREA_FLAGS (SYS_MEMORY_PAGE_SIZE_1M | SYS_MEMORY_ACCESS_RIGHT_ANY)
#define WINDOW_AREA_SIZE 0x10000000u

#define WINDOW_MAXIMUM_TRIES 8

int ps3_host_window_reserve(uint32_t address)
{
	sys_mem_addr_t areas[WINDOW_MAXIMUM_TRIES];
	sys_mem_addr_t area = 0;
	int count = 0, index, found = 0;
	s32 result;

	/* an alignment of the address itself leaves lv2 one place to put the area */
	result = sysMMapperAllocateAddress(WINDOW_AREA_SIZE, WINDOW_AREA_FLAGS, address, &area);
	ps3_log_printf("[mem] mmapper area aligned to 0x%08x: result 0x%08x at 0x%08x\n", (unsigned)address,
		(unsigned)result, (unsigned)area);
	if (result == 0 && area == address)
		return 0;
	if (result == 0)
		sysMMapperFreeAddress(area);
	/* else 256 MB areas, lowest first, until one lands there; the others are freed */
	while (count < WINDOW_MAXIMUM_TRIES)
	{
		result = sysMMapperAllocateAddress(WINDOW_AREA_SIZE, WINDOW_AREA_FLAGS, WINDOW_AREA_SIZE, &areas[count]);
		ps3_log_printf("[mem] mmapper area: result 0x%08x at 0x%08x\n", (unsigned)result, (unsigned)areas[count]);
		if (result != 0)
			break;
		if (areas[count] == address)
		{
			found = 1;
			break;
		}
		if (areas[count++] > address)
			break;
	}
	for (index = 0; index < count; index++)
		sysMMapperFreeAddress(areas[index]);
	return found ? 0 : EFAULT;
}

int ps3_host_window_map(uint32_t address, uint32_t size, uint32_t *memory)
{
	sys_mem_id_t id;
	s32 result = sysMMapperAllocateMemory(size, SYS_MEMORY_PAGE_SIZE_1M, &id);

	if (result != 0)
	{
		ps3_log_printf("[mem] sysMMapperAllocateMemory %u KB failed 0x%08x\n", (unsigned)(size >> 10), (unsigned)result);
		return errno_from_lv2(result);
	}
	result = sysMMapperMapMemory(address, id, SYS_MEMORY_PROT_READ_WRITE);
	if (result != 0)
	{
		ps3_log_printf("[mem] sysMMapperMapMemory at 0x%08x failed 0x%08x\n", (unsigned)address, (unsigned)result);
		sysMMapperFreeMemory(id);
		return errno_from_lv2(result);
	}
	*memory = id;
	return 0;
}

void ps3_host_window_unmap(uint32_t address, uint32_t memory)
{
	sys_mem_id_t id = memory;

	if (sysMMapperUnmapMemory(address, &id) == 0)
		sysMMapperFreeMemory(id);
}

/* ---------- files */

int ps3_host_file_open(const char *path, int flags, int32_t *descriptor)
{
	s32 fd = -1;
	s32 result = sysLv2FsOpen(path, flags, &fd, 0666, NULL, 0);

	*descriptor = fd;
	return errno_from_lv2(result);
}

int ps3_host_file_close(int32_t descriptor)
{
	return errno_from_lv2(sysLv2FsClose(descriptor));
}

int ps3_host_file_read(int32_t descriptor, void *buffer, uint64_t size, uint64_t *done)
{
	u64 count = 0;
	s32 result = sysLv2FsRead(descriptor, buffer, size, &count);

	*done = result == 0 ? count : 0;
	return errno_from_lv2(result);
}

int ps3_host_file_write(int32_t descriptor, const void *buffer, uint64_t size, uint64_t *done)
{
	u64 count = 0;
	s32 result = sysLv2FsWrite(descriptor, buffer, size, &count);

	*done = result == 0 ? count : 0;
	return errno_from_lv2(result);
}

int ps3_host_file_seek(int32_t descriptor, int64_t offset, int whence, uint64_t *position)
{
	u64 at = 0;
	s32 result = sysLv2FsLSeek64(descriptor, (u64)offset, whence, &at);

	if (position)
		*position = at;
	return errno_from_lv2(result);
}

int ps3_host_file_truncate(int32_t descriptor, uint64_t size)
{
	return errno_from_lv2(sysLv2FsFtruncate(descriptor, size));
}

static void information_from_stat(const sysFSStat *status, struct ps3_host_file_information *information)
{
	memset(information, 0, sizeof(*information));
	/* S_IFDIR: lv2 keeps the BSD mode bits */
	information->directory = (status->st_mode & 0170000) == 0040000;
	information->read_only = !(status->st_mode & 0200);
	information->size = status->st_size;
	information->access_time = status->st_atime;
	information->modification_time = status->st_mtime;
	information->creation_time = status->st_ctime;
}

int ps3_host_file_information(int32_t descriptor, struct ps3_host_file_information *information)
{
	sysFSStat status;
	s32 result = sysLv2FsFStat(descriptor, &status);

	if (result == 0)
		information_from_stat(&status, information);
	return errno_from_lv2(result);
}

int ps3_host_path_information(const char *path, struct ps3_host_file_information *information)
{
	sysFSStat status;
	s32 result = sysLv2FsStat(path, &status);

	if (result == 0)
		information_from_stat(&status, information);
	return errno_from_lv2(result);
}

int ps3_host_path_unlink(const char *path)
{
	return errno_from_lv2(sysLv2FsUnlink(path));
}

int ps3_host_path_rename(const char *from, const char *to)
{
	return errno_from_lv2(sysLv2FsRename(from, to));
}

int ps3_host_path_make_directory(const char *path)
{
	return errno_from_lv2(sysLv2FsMkdir(path, 0777));
}

int ps3_host_path_remove_directory(const char *path)
{
	return errno_from_lv2(sysLv2FsRmdir(path));
}

int ps3_host_path_set_read_only(const char *path, int read_only)
{
	return errno_from_lv2(sysLv2FsChmod(path, read_only ? 0444 : 0666));
}

int ps3_host_path_set_times(const char *path, int64_t access_time, int64_t modification_time)
{
	sysFSUtimbuf times;

	times.actime = (time_t)access_time;
	times.modtime = (time_t)modification_time;
	return errno_from_lv2(sysLv2FsUtime(path, &times));
}

int ps3_host_disk_space(const char *path, uint64_t *free_bytes, uint64_t *total_bytes)
{
	struct ps3_host_file_information information;

	/* 8 GB: the free size call is in a system module (sys_fs) that is not loaded here */
	if (ps3_host_path_information(path, &information) != 0)
		return ENOENT;
	*free_bytes = 8ull << 30;
	*total_bytes = 8ull << 30;
	return 0;
}

int ps3_host_directory_open(const char *path, int32_t *descriptor)
{
	s32 fd = -1;
	s32 result = sysLv2FsOpenDir(path, &fd);

	*descriptor = fd;
	return errno_from_lv2(result);
}

int ps3_host_directory_next(int32_t descriptor, char *name, uint32_t size)
{
	for (;;)
	{
		sysFSDirent entry;
		u64 read = 0;

		if (sysLv2FsReadDir(descriptor, &entry, &read) != 0 || read == 0)
			return 0;
		if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, ".."))
			continue;
		snprintf(name, size, "%s", entry.d_name);
		return 1;
	}
}

void ps3_host_directory_close(int32_t descriptor)
{
	sysLv2FsCloseDir(descriptor);
}
