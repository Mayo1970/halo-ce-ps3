/* PS3_HOST.H: the lv2 services of the Xbox layer (port/ps3/xbox). That layer has the game's
32-bit long, so this boundary uses fixed-width types only. Errors are newlib errno values. */

#ifndef __HALO_PS3_HOST_H
#define __HALO_PS3_HOST_H

#include <stdarg.h>
#include <stdint.h>

/* ---------- log and process */

void ps3_host_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void ps3_host_vlog(const char *format, va_list arguments);
void ps3_host_log_text(const char *text, uint32_t length);

int ps3_host_exit_requested(void);
void ps3_host_exit(int status) __attribute__((noreturn));
uint32_t ps3_host_free_memory_mb(void);
const char *ps3_host_data_root(void);

/* the screen's colour while nothing draws (ps3_main.c) */
enum
{
	_ps3_host_status_running,
	_ps3_host_status_stopped,
	_ps3_host_status_failed,
};
void ps3_host_set_status(int status);

/* ---------- locks and condition variables (lv2 mutexes) */

struct ps3_host_lock;
struct ps3_host_condition;

struct ps3_host_lock *ps3_host_lock_create(int recursive);
void ps3_host_lock_destroy(struct ps3_host_lock *lock);
void ps3_host_lock_enter(struct ps3_host_lock *lock);
int ps3_host_lock_try_enter(struct ps3_host_lock *lock);
void ps3_host_lock_leave(struct ps3_host_lock *lock);

/* the lock must not be recursive (lv2 condition variables) */
struct ps3_host_condition *ps3_host_condition_create(struct ps3_host_lock *lock);
/* with the lock held; 0 waits for ever. 0 when woken, 1 on timeout */
int ps3_host_condition_wait(struct ps3_host_condition *condition, uint64_t timeout_microseconds);
void ps3_host_condition_broadcast(struct ps3_host_condition *condition);

/* ---------- threads (detached lv2 PPU threads) */

#define PS3_HOST_PRIORITY_NORMAL 1001

/* 0 on success */
int ps3_host_thread_create(void (*entry)(void *argument), void *argument, uint32_t stack_size, int priority,
	const char *name);
uint64_t ps3_host_thread_id(void);
void ps3_host_thread_yield(void);
void ps3_host_sleep_microseconds(uint64_t microseconds);

/* ---------- time */

uint64_t ps3_host_microseconds(void);
/* UTC since 1970 */
void ps3_host_time_of_day(uint64_t *seconds, uint32_t *nanoseconds);

/* ---------- the Xbox memory window (lv2 mmapper, 1 MB pages) */

#define PS3_HOST_WINDOW_PAGE 0x100000u

/* reserves the 256 MB area at address; 0 on success */
int ps3_host_window_reserve(uint32_t address);
/* maps fresh memory at a page-aligned address; 0 on success */
int ps3_host_window_map(uint32_t address, uint32_t size, uint32_t *memory);
void ps3_host_window_unmap(uint32_t address, uint32_t memory);

/* ---------- files (lv2 file system calls) */

#define PS3_HOST_O_RDONLY 0000000
#define PS3_HOST_O_WRONLY 0000001
#define PS3_HOST_O_RDWR 0000002
#define PS3_HOST_O_CREAT 0000100
#define PS3_HOST_O_EXCL 0000200
#define PS3_HOST_O_TRUNC 0001000
#define PS3_HOST_O_APPEND 0002000

struct ps3_host_file_information
{
	uint32_t directory;
	uint32_t read_only;
	uint64_t size;
	/* seconds since 1970 */
	int64_t access_time;
	int64_t modification_time;
	int64_t creation_time;
};

int ps3_host_file_open(const char *path, int flags, int32_t *descriptor);
int ps3_host_file_close(int32_t descriptor);
int ps3_host_file_read(int32_t descriptor, void *buffer, uint64_t size, uint64_t *done);
int ps3_host_file_write(int32_t descriptor, const void *buffer, uint64_t size, uint64_t *done);
/* whence: 0 set, 1 current, 2 end */
int ps3_host_file_seek(int32_t descriptor, int64_t offset, int whence, uint64_t *position);
int ps3_host_file_truncate(int32_t descriptor, uint64_t size);
int ps3_host_file_information(int32_t descriptor, struct ps3_host_file_information *information);

int ps3_host_path_information(const char *path, struct ps3_host_file_information *information);
int ps3_host_path_unlink(const char *path);
int ps3_host_path_rename(const char *from, const char *to);
int ps3_host_path_make_directory(const char *path);
int ps3_host_path_remove_directory(const char *path);
int ps3_host_path_set_read_only(const char *path, int read_only);
int ps3_host_path_set_times(const char *path, int64_t access_time, int64_t modification_time);
int ps3_host_disk_space(const char *path, uint64_t *free_bytes, uint64_t *total_bytes);

int ps3_host_directory_open(const char *path, int32_t *descriptor);
/* copies the next name but . and ..; 1 with a name, 0 at the end */
int ps3_host_directory_next(int32_t descriptor, char *name, uint32_t size);
void ps3_host_directory_close(int32_t descriptor);

#endif
