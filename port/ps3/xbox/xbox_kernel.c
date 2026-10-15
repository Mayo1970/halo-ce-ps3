/* Win32/XAPI kernel services on lv2, after xbox_kernel.c. One lock and one condition variable guard
every waitable handle: few lv2 objects, and the game has few threads. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLATFORM_HANDLE_SIGNATURE 0x686e646cUL /* 'hndl' */
#define THREAD_STATE_COUNT 64
#define THREAD_MINIMUM_STACK 0x80000

/* ---------- logging */

void platform_log(const char *format, ...)
{
	char line[1024];
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	if (length < 0)
		return;
	if (length > (int)sizeof(line) - 1)
		length = sizeof(line) - 1;
	ps3_host_log("[halo] %.*s\n", length, line);
}

void platform_unimplemented(const char *name)
{
	static const char *reported[256];
	static long reported_count = 0;
	long index;

	for (index = 0; index < reported_count; index++)
	{
		if (reported[index] == name)
			return;
	}
	if (reported_count < (long)(sizeof(reported) / sizeof(reported[0])))
		reported[reported_count++] = name;
	platform_log("%s is not implemented on the PS3", name);
}

/* ---------- the state of each thread: no TLS in PSL1GHT's runtime */

struct platform_apc
{
	struct platform_apc *next;
	platform_apc_routine routine;
	void *context[3];
};

struct thread_state
{
	volatile __uint64 id;
	DWORD last_error;
	struct platform_apc *apc_head;
	struct platform_apc *apc_tail;
	/* the last place the thread waited or yielded, for platform_heartbeat */
	void *volatile wait_site;
	volatile __uint64 wait_since;
	volatile unsigned long wait_count;
	char name[16];
};

static struct thread_state thread_states[THREAD_STATE_COUNT];
/* shared by the threads past THREAD_STATE_COUNT */
static struct thread_state overflow_state;

static struct thread_state *thread_state_get(void)
{
	__uint64 id = ps3_host_thread_id();
	long index;

	for (index = 0; index < THREAD_STATE_COUNT; index++)
	{
		if (thread_states[index].id == id)
			return &thread_states[index];
	}
	for (index = 0; index < THREAD_STATE_COUNT; index++)
	{
		if (__sync_bool_compare_and_swap(&thread_states[index].id, (__uint64)0, id))
		{
			thread_states[index].last_error = 0;
			thread_states[index].apc_head = NULL;
			thread_states[index].apc_tail = NULL;
			thread_states[index].wait_site = NULL;
			thread_states[index].wait_count = 0;
			thread_states[index].name[0] = 0;
			return &thread_states[index];
		}
	}
	return &overflow_state;
}

/* a finished thread frees its slot: lv2 can give its id to a new thread */
static void thread_state_release(void)
{
	struct thread_state *state = thread_state_get();

	while (state->apc_head)
	{
		struct platform_apc *apc = state->apc_head;

		state->apc_head = apc->next;
		free(apc);
	}
	state->apc_tail = NULL;
	if (state != &overflow_state)
	{
		__sync_synchronize();
		state->id = 0;
	}
}

static void note_wait(void *site)
{
	struct thread_state *state = thread_state_get();

	if (state->wait_site != site)
	{
		state->wait_since = ps3_host_microseconds();
		state->wait_count = 0;
		state->wait_site = site;
	}
	state->wait_count++;
}

/* ---------- last error */

DWORD WINAPI GetLastError(void)
{
	return thread_state_get()->last_error;
}

void WINAPI SetLastError(DWORD error)
{
	thread_state_get()->last_error = error;
}

DWORD platform_set_last_error_from_errno(int error_number)
{
	DWORD error;

	switch (error_number)
	{
	case 0: error = ERROR_SUCCESS; break;
	case ENOENT: error = ERROR_FILE_NOT_FOUND; break;
	case ENOTDIR: error = ERROR_PATH_NOT_FOUND; break;
	case EACCES: case EPERM: case EROFS: error = ERROR_ACCESS_DENIED; break;
	case EEXIST: error = ERROR_ALREADY_EXISTS; break;
	case ENOTEMPTY: error = ERROR_DIR_NOT_EMPTY; break;
	case ENOSPC: error = ERROR_DISK_FULL; break;
	case ENOMEM: error = ERROR_NOT_ENOUGH_MEMORY; break;
	case EBADF: error = ERROR_INVALID_HANDLE; break;
	case EINVAL: error = ERROR_INVALID_PARAMETER; break;
	case EMFILE: case ENFILE: error = ERROR_TOO_MANY_OPEN_FILES; break;
	case EBUSY: error = ERROR_BUSY; break;
	default: error = ERROR_GEN_FAILURE; break;
	}
	SetLastError(error);
	return error;
}

/* ---------- the kernel lock */

static struct ps3_host_lock *kernel_lock;
static struct ps3_host_condition *kernel_condition;

/* before the game's first thread (ps3_main.c) */
void platform_kernel_initialize(void)
{
	kernel_lock = ps3_host_lock_create(FALSE);
	kernel_condition = kernel_lock ? ps3_host_condition_create(kernel_lock) : NULL;
	if (!kernel_condition)
		platform_log("cannot create the kernel lock: waits will fail");
}

static void kernel_enter(void)
{
	ps3_host_lock_enter(kernel_lock);
}

static void kernel_leave(void)
{
	ps3_host_lock_leave(kernel_lock);
}

/* ---------- handles */

struct platform_handle *platform_handle_new(long type, void *data,
	void (*destroy)(struct platform_handle *handle))
{
	struct platform_handle *handle = calloc(1, sizeof(*handle));

	if (!handle)
		return NULL;
	handle->signature = PLATFORM_HANDLE_SIGNATURE;
	handle->type = type;
	handle->data = data;
	handle->destroy = destroy;
	return handle;
}

struct platform_handle *platform_handle_get(HANDLE handle, long type)
{
	struct platform_handle *result = (struct platform_handle *)handle;

	/* GetCurrentThread() and the like are small negative pseudo handles */
	if (!result || (unsigned long)(__uint64)handle >= 0xfffff000UL || (__uint64)handle >> 32 ||
		result->signature != PLATFORM_HANDLE_SIGNATURE || (type && result->type != type))
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return NULL;
	}
	return result;
}

void platform_handle_signal(struct platform_handle *handle)
{
	kernel_enter();
	handle->signaled = TRUE;
	ps3_host_condition_broadcast(kernel_condition);
	kernel_leave();
}

static void handle_free(struct platform_handle *handle)
{
	handle->signature = 0;
	free(handle);
}

BOOL WINAPI CloseHandle(HANDLE object)
{
	struct platform_handle *handle = platform_handle_get(object, 0);

	if (!handle)
		return FALSE;
	if (handle->type == _platform_handle_thread)
	{
		/* freed once both the handle is closed and the thread has exited */
		handle->destroy(handle);
		return TRUE;
	}
	if (handle->destroy)
		handle->destroy(handle);
	handle_free(handle);
	return TRUE;
}

/* ---------- asynchronous procedure calls */

void platform_queue_apc(platform_apc_routine routine, void *context0, void *context1, void *context2)
{
	struct thread_state *state = thread_state_get();
	struct platform_apc *apc = calloc(1, sizeof(*apc));

	if (!apc)
		return;
	apc->routine = routine;
	apc->context[0] = context0;
	apc->context[1] = context1;
	apc->context[2] = context2;
	if (state->apc_tail)
		state->apc_tail->next = apc;
	else
		state->apc_head = apc;
	state->apc_tail = apc;
}

long platform_run_apcs(void)
{
	struct thread_state *state = thread_state_get();
	long count = 0;

	while (state->apc_head)
	{
		struct platform_apc *apc = state->apc_head;

		state->apc_head = apc->next;
		if (!state->apc_head)
			state->apc_tail = NULL;
		apc->routine(apc->context[0], apc->context[1], apc->context[2]);
		free(apc);
		count++;
	}
	return count;
}

/* ---------- waiting */

static BOOL handle_try_acquire(struct platform_handle *handle)
{
	if (handle->type == _platform_handle_mutex)
	{
		__uint64 self = ps3_host_thread_id();

		if (handle->recursion == 0 || handle->owner == self)
		{
			handle->owner = self;
			handle->recursion++;
			return TRUE;
		}
		return FALSE;
	}
	if (!handle->signaled)
		return FALSE;
	if (handle->type == _platform_handle_event && !handle->manual_reset)
		handle->signaled = FALSE;
	return TRUE;
}

static DWORD wait_for_single_object(HANDLE object, DWORD milliseconds, BOOL alertable)
{
	struct platform_handle *handle;
	__uint64 deadline = 0;
	DWORD result = WAIT_OBJECT_0;

	if (alertable && platform_run_apcs())
		return WAIT_IO_COMPLETION;

	handle = platform_handle_get(object, 0);
	if (!handle)
		return WAIT_FAILED;
	if (handle->type != _platform_handle_event &&
		handle->type != _platform_handle_mutex &&
		handle->type != _platform_handle_thread)
	{
		/* files and the like are always signaled */
		return WAIT_OBJECT_0;
	}

	if (milliseconds != INFINITE)
		deadline = ps3_host_microseconds() + (__uint64)milliseconds * 1000;
	kernel_enter();
	while (!handle_try_acquire(handle))
	{
		if (milliseconds == 0)
		{
			result = WAIT_TIMEOUT;
			break;
		}
		if (milliseconds == INFINITE)
		{
			ps3_host_condition_wait(kernel_condition, 0);
		}
		else
		{
			__uint64 now = ps3_host_microseconds();

			if (now >= deadline)
			{
				result = WAIT_TIMEOUT;
				break;
			}
			ps3_host_condition_wait(kernel_condition, deadline - now);
		}
	}
	kernel_leave();
	return result;
}

DWORD WINAPI WaitForSingleObjectEx(HANDLE object, DWORD milliseconds, BOOL alertable)
{
	note_wait(__builtin_return_address(0));
	return wait_for_single_object(object, milliseconds, alertable);
}

DWORD WINAPI WaitForSingleObject(HANDLE object, DWORD milliseconds)
{
	note_wait(__builtin_return_address(0));
	return wait_for_single_object(object, milliseconds, FALSE);
}

/* ---------- events */

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manual_reset,
	BOOL initial_state, LPCSTR name)
{
	struct platform_handle *handle = platform_handle_new(_platform_handle_event, NULL, NULL);

	(void)attributes;
	(void)name;
	if (!handle)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	handle->manual_reset = manual_reset;
	handle->signaled = initial_state;
	return handle;
}

BOOL WINAPI SetEvent(HANDLE event)
{
	struct platform_handle *handle = platform_handle_get(event, _platform_handle_event);

	if (!handle)
		return FALSE;
	platform_handle_signal(handle);
	return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE event)
{
	struct platform_handle *handle = platform_handle_get(event, _platform_handle_event);

	if (!handle)
		return FALSE;
	kernel_enter();
	handle->signaled = FALSE;
	kernel_leave();
	return TRUE;
}

/* ---------- mutexes */

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES attributes, BOOL initial_owner, LPCSTR name)
{
	struct platform_handle *handle = platform_handle_new(_platform_handle_mutex, NULL, NULL);

	(void)attributes;
	(void)name;
	if (!handle)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	if (initial_owner)
	{
		handle->owner = ps3_host_thread_id();
		handle->recursion = 1;
	}
	return handle;
}

BOOL WINAPI ReleaseMutex(HANDLE mutex)
{
	struct platform_handle *handle = platform_handle_get(mutex, _platform_handle_mutex);
	BOOL result = FALSE;

	if (!handle)
		return FALSE;
	kernel_enter();
	if (handle->recursion > 0 && handle->owner == ps3_host_thread_id())
	{
		if (--handle->recursion == 0)
			ps3_host_condition_broadcast(kernel_condition);
		result = TRUE;
	}
	kernel_leave();
	if (!result)
		SetLastError(ERROR_NOT_OWNER);
	return result;
}

/* ---------- threads */

struct platform_thread
{
	struct platform_handle *handle;
	LPTHREAD_START_ROUTINE start;
	LPVOID parameter;
	DWORD exit_code;
	BOOL suspended;
	BOOL closed;
	BOOL finished;
};

static void thread_release(struct platform_handle *handle)
{
	struct platform_thread *thread = handle->data;
	BOOL free_now;

	kernel_enter();
	free_now = thread->finished;
	thread->closed = TRUE;
	kernel_leave();
	if (free_now)
	{
		free(thread);
		handle_free(handle);
	}
}

static void thread_main(void *context)
{
	struct platform_thread *thread = context;
	struct platform_handle *handle = thread->handle;
	DWORD exit_code;
	BOOL free_now;

	kernel_enter();
	while (thread->suspended)
		ps3_host_condition_wait(kernel_condition, 0);
	kernel_leave();
	snprintf(thread_state_get()->name, sizeof(thread_state_get()->name), "%p", (void *)thread->start);

	exit_code = thread->start(thread->parameter);

	kernel_enter();
	thread->exit_code = exit_code;
	thread->finished = TRUE;
	handle->signaled = TRUE;
	ps3_host_condition_broadcast(kernel_condition);
	free_now = thread->closed;
	kernel_leave();
	thread_state_release();
	if (free_now)
	{
		free(thread);
		handle_free(handle);
	}
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES attributes, DWORD stack_size,
	LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags, LPDWORD thread_id)
{
	static LONG next_thread_id;
	struct platform_thread *thread = calloc(1, sizeof(*thread));
	struct platform_handle *handle;
	LONG id;
	char name[16];

	(void)attributes;
	if (!thread)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	handle = platform_handle_new(_platform_handle_thread, thread, thread_release);
	if (!handle)
	{
		free(thread);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	thread->handle = handle;
	thread->start = start;
	thread->parameter = parameter;
	thread->exit_code = STILL_ACTIVE;
	thread->suspended = (flags & CREATE_SUSPENDED) != 0;

	id = __sync_add_and_fetch(&next_thread_id, 1);
	snprintf(name, sizeof(name), "halo%ld", (long)id);
	/* Xbox stacks are small; the PS3 ABI's frames are larger */
	if (ps3_host_thread_create(thread_main, thread, stack_size > THREAD_MINIMUM_STACK ? stack_size : THREAD_MINIMUM_STACK,
		PS3_HOST_PRIORITY_NORMAL, name) != 0)
	{
		free(thread);
		handle_free(handle);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	platform_log("thread %s: entry %p, stack %lu", name, (void *)start, (unsigned long)stack_size);
	if (thread_id)
		*thread_id = (DWORD)id;
	return handle;
}

DWORD WINAPI ResumeThread(HANDLE object)
{
	struct platform_handle *handle = platform_handle_get(object, _platform_handle_thread);
	struct platform_thread *thread;
	DWORD previous;

	if (!handle)
		return (DWORD)-1;
	thread = handle->data;
	kernel_enter();
	previous = thread->suspended ? 1 : 0;
	thread->suspended = FALSE;
	ps3_host_condition_broadcast(kernel_condition);
	kernel_leave();
	return previous;
}

BOOL WINAPI SetThreadPriority(HANDLE object, int priority)
{
	/* the game's threads all run at the main thread's lv2 priority, as on Linux */
	(void)priority;
	return object == GetCurrentThread() || platform_handle_get(object, _platform_handle_thread) != NULL;
}

BOOL WINAPI GetExitCodeThread(HANDLE object, LPDWORD exit_code)
{
	struct platform_handle *handle = platform_handle_get(object, _platform_handle_thread);
	struct platform_thread *thread;

	if (!handle)
		return FALSE;
	thread = handle->data;
	kernel_enter();
	*exit_code = thread->exit_code;
	kernel_leave();
	return TRUE;
}

BOOL WINAPI SwitchToThread(void)
{
	note_wait(__builtin_return_address(0));
	ps3_host_thread_yield();
	return TRUE;
}

static DWORD sleep_ex(DWORD milliseconds, BOOL alertable)
{
	if (alertable && platform_run_apcs())
		return WAIT_IO_COMPLETION;
	if (milliseconds == 0)
	{
		ps3_host_thread_yield();
		return 0;
	}
	if (milliseconds == INFINITE)
	{
		for (;;)
			ps3_host_sleep_microseconds(1000000);
	}
	ps3_host_sleep_microseconds((__uint64)milliseconds * 1000);
	if (alertable && platform_run_apcs())
		return WAIT_IO_COMPLETION;
	return 0;
}

DWORD WINAPI SleepEx(DWORD milliseconds, BOOL alertable)
{
	note_wait(__builtin_return_address(0));
	return sleep_ex(milliseconds, alertable);
}

VOID WINAPI Sleep(DWORD milliseconds)
{
	note_wait(__builtin_return_address(0));
	sleep_ex(milliseconds, FALSE);
}

/* the display thread's heartbeat (port/ps3/src/ps3_main.c): the frames, the main loop's place, and where
each thread last waited, which ./build.sh symbols names; 1 if the main loop has not moved since the last */
int platform_heartbeat(void)
{
	extern volatile long ps3_main_loop_step;
	extern volatile unsigned long ps3_main_loop_count;
	unsigned long platform_d3d_frame_count(void);
	static unsigned long last_frames = (unsigned long)-1, last_count = (unsigned long)-1;
	static long last_step = -1;
	unsigned long frames = platform_d3d_frame_count(), count = ps3_main_loop_count;
	long step = ps3_main_loop_step;
	int stalled = frames == last_frames && count == last_count && step == last_step && count;
	__uint64 now = ps3_host_microseconds();
	long index;

	last_frames = frames;
	last_count = count;
	last_step = step;
	platform_log("heartbeat: %lu frames, main loop pass %lu at step %ld", frames, count, step);
	for (index = 0; index < THREAD_STATE_COUNT; index++)
	{
		struct thread_state *state = &thread_states[index];

		if (!state->id || !state->wait_site)
			continue;
		platform_log("heartbeat: thread %s waits at %p, %lu times in %lu s", state->name[0] ? state->name : "main",
			state->wait_site, state->wait_count, (unsigned long)((now - state->wait_since) / 1000000));
	}
	return stalled;
}

/* an MSVC intrinsic that scenario.c declares; GCC has no builtin of the name */
void _ReadWriteBarrier(void)
{
	__asm__ volatile("" ::: "memory");
}

/* ---------- time */

DWORD WINAPI GetTickCount(void)
{
	return (DWORD)(ps3_host_microseconds() / 1000);
}

/* a microsecond counter, as on Linux: 32-bit intermediate arithmetic in the game stays in range */
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *count)
{
	count->QuadPart = (LONGLONG)ps3_host_microseconds();
	return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *frequency)
{
	frequency->QuadPart = 1000000;
	return TRUE;
}

/* seconds between 1601-01-01 and 1970-01-01 */
#define FILETIME_UNIX_EPOCH_SECONDS 11644473600ULL

void platform_unix_time_to_filetime(unsigned long seconds, unsigned long nanoseconds, FILETIME *file_time)
{
	__uint64 value = ((__uint64)seconds + FILETIME_UNIX_EPOCH_SECONDS) * 10000000ULL + nanoseconds / 100;

	file_time->dwLowDateTime = (DWORD)value;
	file_time->dwHighDateTime = (DWORD)(value >> 32);
}

void platform_filetime_to_unix_time(const FILETIME *file_time, unsigned long *seconds, unsigned long *nanoseconds)
{
	__uint64 value = ((__uint64)file_time->dwHighDateTime << 32) | file_time->dwLowDateTime;
	__uint64 total_seconds = value / 10000000ULL;

	*seconds = total_seconds > FILETIME_UNIX_EPOCH_SECONDS ? (unsigned long)(total_seconds - FILETIME_UNIX_EPOCH_SECONDS) : 0;
	*nanoseconds = (unsigned long)(value % 10000000ULL) * 100;
}

LONG WINAPI CompareFileTime(CONST FILETIME *time1, CONST FILETIME *time2)
{
	__uint64 value1 = ((__uint64)time1->dwHighDateTime << 32) | time1->dwLowDateTime;
	__uint64 value2 = ((__uint64)time2->dwHighDateTime << 32) | time2->dwLowDateTime;

	return value1 < value2 ? -1 : value1 > value2 ? 1 : 0;
}

static const int days_before_month[2][13] =
{
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 },
};

static int is_leap_year(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

BOOL WINAPI SystemTimeToFileTime(CONST SYSTEMTIME *system_time, LPFILETIME file_time)
{
	__uint64 days = 0;
	__uint64 value;
	int year;

	if (system_time->wYear < 1601 || system_time->wMonth < 1 || system_time->wMonth > 12 ||
		system_time->wDay < 1 || system_time->wDay > 31)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	for (year = 1601; year < system_time->wYear; year++)
		days += is_leap_year(year) ? 366 : 365;
	days += days_before_month[is_leap_year(system_time->wYear)][system_time->wMonth - 1];
	days += system_time->wDay - 1;
	value = ((days * 24 + system_time->wHour) * 60 + system_time->wMinute) * 60 + system_time->wSecond;
	value = value * 10000000ULL + (__uint64)system_time->wMilliseconds * 10000ULL;
	file_time->dwLowDateTime = (DWORD)value;
	file_time->dwHighDateTime = (DWORD)(value >> 32);
	return TRUE;
}

VOID WINAPI GetSystemTime(LPSYSTEMTIME system_time)
{
	__uint64 seconds;
	uint32_t nanoseconds;
	unsigned long days;
	int year = 1970;
	int month = 0;
	int leap;

	ps3_host_time_of_day(&seconds, &nanoseconds);
	days = (unsigned long)(seconds / 86400);
	system_time->wDayOfWeek = (WORD)((days + 4) % 7); /* 1970-01-01 was a Thursday */
	for (;;)
	{
		unsigned long year_days = is_leap_year(year) ? 366 : 365;

		if (days < year_days)
			break;
		days -= year_days;
		year++;
	}
	leap = is_leap_year(year);
	while (month < 11 && days >= (unsigned long)days_before_month[leap][month + 1])
		month++;
	system_time->wYear = (WORD)year;
	system_time->wMonth = (WORD)(month + 1);
	system_time->wDay = (WORD)(days - days_before_month[leap][month] + 1);
	system_time->wHour = (WORD)((seconds / 3600) % 24);
	system_time->wMinute = (WORD)((seconds / 60) % 60);
	system_time->wSecond = (WORD)(seconds % 60);
	system_time->wMilliseconds = (WORD)(nanoseconds / 1000000);
}

/* ---------- heap memory (GlobalAlloc / LocalAlloc family) */

struct global_block
{
	SIZE_T size;
	SIZE_T reserved;
	/* with the 8 bytes after it, the payload stays 16-byte aligned */
};

HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size)
{
	struct global_block *block = (flags & GMEM_ZEROINIT) ?
		calloc(1, sizeof(*block) + 8 + size) :
		malloc(sizeof(*block) + 8 + size);

	if (!block)
	{
		platform_log("GlobalAlloc: no memory for %lu bytes", (unsigned long)size);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	block->size = size;
	return (char *)block + sizeof(*block) + 8;
}

static struct global_block *global_block_from_pointer(HGLOBAL memory)
{
	return (struct global_block *)((char *)memory - sizeof(struct global_block) - 8);
}

HGLOBAL WINAPI GlobalReAlloc(HGLOBAL memory, SIZE_T size, UINT flags)
{
	struct global_block *block;
	SIZE_T old_size;

	if (!memory)
		return GlobalAlloc(flags, size);
	block = global_block_from_pointer(memory);
	old_size = block->size;
	block = realloc(block, sizeof(*block) + 8 + size);
	if (!block)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	if ((flags & GMEM_ZEROINIT) && size > old_size)
		memset((char *)block + sizeof(*block) + 8 + old_size, 0, size - old_size);
	block->size = size;
	return (char *)block + sizeof(*block) + 8;
}

HLOCAL WINAPI LocalFree(HLOCAL memory)
{
	if (memory)
		free(global_block_from_pointer(memory));
	return NULL;
}

SIZE_T WINAPI LocalSize(HLOCAL memory)
{
	return memory ? global_block_from_pointer(memory)->size : 0;
}

/* fixed: probing lv2 while other threads allocate can stall them; 64 MB as on the Xbox */
VOID WINAPI GlobalMemoryStatus(LPMEMORYSTATUS status)
{
	memset(status, 0, sizeof(*status));
	status->dwLength = sizeof(*status);
	status->dwTotalPhys = 64 * 1024 * 1024;
	status->dwAvailPhys = 32 * 1024 * 1024;
	status->dwTotalVirtual = 0x7ffe0000;
	status->dwAvailVirtual = 0x7ffe0000;
	status->dwMemoryLoad = 50;
}

/* ---------- debug output */

VOID WINAPI OutputDebugStringA(LPCSTR string)
{
	if (string)
		ps3_host_log_text(string, (uint32_t)strlen(string));
}
