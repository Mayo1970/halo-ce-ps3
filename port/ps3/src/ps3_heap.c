/* The heap log (ps3_system.h): each growth of newlib's heap and each allocation of 64 KB or more, with the request and
its callers, logged at the [mem] heartbeat (ps3_main.c). The Makefile's --wrap sends the calls here. */

#include "ps3_system.h"

#include <reent.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HEAP_LARGE (64 * 1024)
#define HEAP_EVENTS 128
/* the game's allocations come through debug_malloc, system_malloc and GlobalAlloc: five callers reach its caller */
#define HEAP_CALLERS 5
/* the events a heartbeat logs; the rest are counted */
#define HEAP_EVENTS_LOGGED 24

/* a growth (the bytes sbrk took) or a large allocation (growth 0): the request, its callers innermost first */
struct heap_event
{
	double time;
	uint32_t size;
	uint32_t growth;
	uintptr_t callers[HEAP_CALLERS];
};

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__real_memalign(size_t alignment, size_t size);
void *__real__sbrk_r(struct _reent *reent, ptrdiff_t increment);
/* newlib's (mlock.c): recursive, so a request holds it around malloc and sbrk takes it again */
void __malloc_lock(struct _reent *reent);
void __malloc_unlock(struct _reent *reent);

/* under the malloc lock: the events; the request in progress, whose growths sbrk reports */
static struct heap_event events[HEAP_EVENTS];
static uint32_t event_count;
static uint32_t events_lost;
static uint32_t request_size;
static uintptr_t request_callers[HEAP_CALLERS];
static int request_depth;

/* the return addresses from the frame of the function that calls this one: each frame's back chain points to its
caller's, which holds the address the frame's function returns to (16 bytes in: the PPC64 ELFv1 ABI) */
static void callers_read(uintptr_t frame, uintptr_t *callers)
{
	int depth;

	memset(callers, 0, HEAP_CALLERS * sizeof(*callers));
	for (depth = 0; depth < HEAP_CALLERS; depth++)
	{
		uintptr_t up = *(const uintptr_t *)frame;

		if (!up || up <= frame || up - frame > 0x100000 || (up & 15))
			break;
		callers[depth] = *(const uintptr_t *)(up + 16);
		frame = up;
	}
}

static void event_add(uint32_t size, uint32_t growth, const uintptr_t *callers)
{
	__malloc_lock(_REENT);
	if (event_count < HEAP_EVENTS)
	{
		struct heap_event *event = &events[event_count++];

		event->time = ps3_time_seconds();
		event->size = size;
		event->growth = growth;
		memcpy(event->callers, callers, sizeof(event->callers));
	}
	else
	{
		events_lost++;
	}
	__malloc_unlock(_REENT);
}

/* the lock from here to request_end: no other thread's request between */
static void request_begin(uint32_t size, uintptr_t frame)
{
	__malloc_lock(_REENT);
	if (request_depth++)
		return;
	request_size = size;
	callers_read(frame, request_callers);
}

static void request_end(uint32_t size, const void *result)
{
	if (!--request_depth)
	{
		if (result && size >= HEAP_LARGE)
			event_add(size, 0, request_callers);
		request_size = 0;
	}
	__malloc_unlock(_REENT);
}

void *__wrap_malloc(size_t size)
{
	void *result;

	request_begin((uint32_t)size, (uintptr_t)__builtin_frame_address(0));
	result = __real_malloc(size);
	request_end((uint32_t)size, result);
	return result;
}

void *__wrap_calloc(size_t count, size_t size)
{
	void *result;

	request_begin((uint32_t)(count * size), (uintptr_t)__builtin_frame_address(0));
	result = __real_calloc(count, size);
	request_end((uint32_t)(count * size), result);
	return result;
}

void *__wrap_realloc(void *pointer, size_t size)
{
	void *result;

	request_begin((uint32_t)size, (uintptr_t)__builtin_frame_address(0));
	result = __real_realloc(pointer, size);
	request_end((uint32_t)size, result);
	return result;
}

void *__wrap_memalign(size_t alignment, size_t size)
{
	void *result;

	request_begin((uint32_t)size, (uintptr_t)__builtin_frame_address(0));
	result = __real_memalign(alignment, size);
	request_end((uint32_t)size, result);
	return result;
}

/* newlib's malloc grows its heap here, with the malloc lock held */
void *__wrap__sbrk_r(struct _reent *reent, ptrdiff_t increment)
{
	void *result = __real__sbrk_r(reent, increment);

	if (increment > 0 && result != (void *)-1)
		event_add(request_size, (uint32_t)increment, request_callers);
	return result;
}

void ps3_heap_log(void)
{
	static struct heap_event copy[HEAP_EVENTS];
	uint32_t count, lost, index, grown = 0, large = 0;

	__malloc_lock(_REENT);
	count = event_count;
	lost = events_lost;
	memcpy(copy, events, count * sizeof(*copy));
	event_count = 0;
	events_lost = 0;
	__malloc_unlock(_REENT);
	for (index = 0; index < count; index++)
	{
		const struct heap_event *event = &copy[index];

		if (event->growth)
			grown += event->growth;
		else
			large += event->size;
		if (index >= HEAP_EVENTS_LOGGED)
			continue;
		ps3_log_printf("[heap] t=%.1fs %s %u KB, a request of %u bytes from %lx < %lx < %lx < %lx < %lx\n",
			event->time, event->growth ? "grew" : "allocated",
			(unsigned)((event->growth ? event->growth : event->size) >> 10), (unsigned)event->size,
			(unsigned long)event->callers[0], (unsigned long)event->callers[1], (unsigned long)event->callers[2],
			(unsigned long)event->callers[3], (unsigned long)event->callers[4]);
	}
	if (count > HEAP_EVENTS_LOGGED || lost)
	{
		ps3_log_printf("[heap] %u more events (%u not kept); in all %u KB grown, %u KB in large allocations\n",
			(unsigned)(count > HEAP_EVENTS_LOGGED ? count - HEAP_EVENTS_LOGGED : 0) + (unsigned)lost, (unsigned)lost,
			(unsigned)(grown >> 10), (unsigned)(large >> 10));
	}
}
