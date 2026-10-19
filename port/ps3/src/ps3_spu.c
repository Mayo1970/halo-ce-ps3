/* The SPU programs (port/ps3/spu), linked in with bin2s, on lv2 SPU threads: the DirectSound mixer (spu_mixer.c), a
job a chunk with an event at its end; the Direct3D thread's conversions (spu_vertices.c), a ring of tasks. */

#include "ps3_host.h"
#include "ps3_spu.h"
#include "ps3_system.h"
#include "ps3_timers.h"

#include <malloc.h>
#include <string.h>

#include <ppu-types.h>
#include <sys/event_queue.h>
#include <sys/spu.h>
#include <sys/systime.h>

/* bin2s's names for the SPU images (the Makefile's SPU rules) */
extern const u8 spu_mixer_elf[];
extern const u8 spu_vertices_elf[];

/* a job longer than this has a dead SPU; a wait on the queue ends after 1 ms to look at the echo, in case the event
was lost */
#define JOB_TIMEOUT_MICROSECONDS 100000
#define EVENT_WAIT_MICROSECONDS 1000
#define EVENT_QUEUE_SIZE 8
#define LV2_ETIMEDOUT 0x8001000B

static spu_mixer_bell bell;
static spu_mixer_echo echo;
static spu_mixer_job *job;
static sysSpuImage image;
static sys_spu_group_t group;
static sys_spu_thread_t thread;
static sys_event_queue_t queue;
static int spus_initialized, queue_created;
/* 1 while the group runs: the mixer thread (a dead SPU) or the exit stops it, once */
static int group_running;
static volatile int mixer_up, stopping;
static uint32_t sequence;
static uint64_t timebase_frequency;

/* the mixer thread's waits; the statistics read them and start them again */
static uint64_t wait_ticks;
static uint32_t wait_events, wait_timeouts, wait_errors;
static uint32_t last_jobs, last_busy_ticks, last_decode_ticks, last_packets;

static uint64_t ticks_from_microseconds(uint64_t microseconds)
{
	return timebase_frequency / 1000 * microseconds / 1000;
}

static uint32_t microseconds_from_ticks(uint64_t ticks)
{
	return (uint32_t)(ticks * 1000 / (timebase_frequency / 1000));
}

/* lv2 takes it once a process: 6 SPUs for SPU threads, none raw */
static void spus_initialize(void)
{
	s32 result;

	timebase_frequency = sysGetTimebaseFrequency();
	if (spus_initialized)
		return;
	result = sysSpuInitialize(6, 0);
	if (result != 0)
		ps3_log_printf("[spu] sysSpuInitialize(6, 0) = 0x%08x\n", (unsigned)result);
	spus_initialized = 1;
}

static void group_stop(int terminate)
{
	u32 cause, status;

	if (__sync_val_compare_and_swap(&group_running, 1, 0) != 1)
		return;
	if (terminate)
		sysSpuThreadGroupTerminate(group, 0);
	sysSpuThreadGroupJoin(group, &cause, &status);
	sysSpuThreadDisconnectEvent(thread, SPU_THREAD_EVENT_USER, SPU_MIXER_EVENT_PORT);
	sysSpuThreadGroupDestroy(group);
	sysSpuImageClose(&image);
}

/* the SPU's own echo from its LS (the copy in main memory is stale while it hangs), then the SPU stops */
static void mixer_fail(const char *reason)
{
	spu_mixer_echo copy;
	u64 *words = (u64 *)&copy;
	uint32_t index;
	s32 result = 0;

	mixer_up = 0;
	memset(&copy, 0, sizeof(copy));
	for (index = 0; index < sizeof(copy) / 8 && result == 0; index++)
		result = sysSpuThreadReadLocalStorage(thread, echo.ls_address + index * 8, &words[index], 8);
	ps3_log_printf("[spu] mixer: %s (job %u; the SPU's own echo: job %u, %u jobs, stage %u, read 0x%08x); SPU off, "
		"the PPE mixes\n", reason, (unsigned)sequence, (unsigned)copy.sequence, (unsigned)copy.jobs,
		(unsigned)copy.stage, (unsigned)result);
	group_stop(1);
}

int ps3_spu_mixer_start(void)
{
	sys_event_queue_attr_t queue_attributes;
	sysSpuThreadGroupAttribute group_attributes;
	sysSpuThreadAttribute thread_attributes;
	sysSpuThreadArgument arguments;
	uint64_t start;
	s32 result;

	if (mixer_up)
		return 1;
	spus_initialize();
	if (!job)
		job = memalign(128, sizeof(*job));
	if (!job)
	{
		ps3_log_printf("[spu] mixer: no memory for its job (%u bytes)\n", (unsigned)sizeof(*job));
		return 0;
	}
	memset(job, 0, sizeof(*job));
	memset(&bell, 0, sizeof(bell));
	memset(&echo, 0, sizeof(echo));
	sequence = 0;
	if (!queue_created)
	{
		memset(&queue_attributes, 0, sizeof(queue_attributes));
		queue_attributes.attr_protocol = SYS_EVENT_QUEUE_FIFO;
		queue_attributes.type = SYS_EVENT_QUEUE_PPU;
		memcpy(queue_attributes.name, "spumix", 7);
		result = sysEventQueueCreate(&queue, &queue_attributes, SYS_EVENT_QUEUE_KEY_LOCAL, EVENT_QUEUE_SIZE);
		if (result != 0)
		{
			ps3_log_printf("[spu] mixer: sysEventQueueCreate = 0x%08x\n", (unsigned)result);
			return 0;
		}
		queue_created = 1;
	}
	result = sysSpuImageImport(&image, spu_mixer_elf, SPU_IMAGE_PROTECT);
	if (result != 0)
	{
		ps3_log_printf("[spu] mixer: sysSpuImageImport = 0x%08x\n", (unsigned)result);
		return 0;
	}
	sysSpuThreadGroupAttributeInitialize(group_attributes);
	sysSpuThreadGroupAttributeName(group_attributes, "halo_mixer");
	result = sysSpuThreadGroupCreate(&group, 1, 100, &group_attributes);
	if (result != 0)
	{
		ps3_log_printf("[spu] mixer: sysSpuThreadGroupCreate = 0x%08x\n", (unsigned)result);
		sysSpuImageClose(&image);
		return 0;
	}
	sysSpuThreadAttributeInitialize(thread_attributes);
	sysSpuThreadAttributeName(thread_attributes, "spu_mixer");
	/* the SPU's main gets 3 arguments (a 4th arrives as 0) */
	sysSpuThreadArgumentInitialize(arguments);
	arguments.arg0 = (u64)(uintptr_t)&bell;
	arguments.arg1 = (u64)(uintptr_t)&echo;
	arguments.arg2 = (u64)(uintptr_t)job;
	result = sysSpuThreadInitialize(&thread, group, 0, &image, &thread_attributes, &arguments);
	if (result == 0)
		result = sysSpuThreadConnectEvent(thread, queue, SPU_THREAD_EVENT_USER, SPU_MIXER_EVENT_PORT);
	if (result == 0)
		result = sysSpuThreadGroupStart(group);
	if (result != 0)
	{
		ps3_log_printf("[spu] mixer: the SPU thread's start = 0x%08x\n", (unsigned)result);
		sysSpuThreadGroupDestroy(group);
		sysSpuImageClose(&image);
		return 0;
	}
	group_running = 1;
	start = ps3_timer_now();
	while (echo.state != SPU_MIXER_RUNNING && ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(100);
	if (echo.state != SPU_MIXER_RUNNING)
	{
		ps3_log_printf("[spu] mixer: the SPU thread did not report in 100 ms\n");
		group_stop(1);
		return 0;
	}
	stopping = 0;
	mixer_up = 1;
	ps3_log_printf("[spu] mixer up: its job at %08x (%u bytes), the echo at %08x in its LS\n", (unsigned)(uintptr_t)job,
		(unsigned)sizeof(*job), (unsigned)echo.ls_address);
	return 1;
}

spu_mixer_job *ps3_spu_mixer_job(void)
{
	return mixer_up ? job : NULL;
}

int ps3_spu_mixer_run(uint32_t voice_count, uint32_t frames)
{
	uint64_t start, timeout = ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS);
	sys_event_t event;

	if (!mixer_up || stopping)
		return 0;
	sequence++;
	job->header.sequence = sequence;
	job->header.voice_count = voice_count;
	job->header.frames = frames;
	/* earlier jobs' events that came after their echo */
	sysEventQueueDrain(queue);
	/* the job before the bell: the SPU reads it when the bell's store wakes it */
	__asm__ volatile("lwsync" ::: "memory");
	bell.sequence = sequence;
	start = ps3_timer_now();
	while (echo.sequence != sequence)
	{
		s32 result = sysEventQueueReceive(queue, &event, EVENT_WAIT_MICROSECONDS);

		if (result == 0)
		{
			wait_events++;
		}
		else if ((u32)result == LV2_ETIMEDOUT)
		{
			wait_timeouts++;
		}
		else
		{
			wait_errors++;
			sysUsleep(100);
		}
		if (stopping)
			return 0;
		if (echo.sequence != sequence && ps3_timer_now() - start > timeout)
		{
			mixer_fail("a job not done in 100 ms");
			return 0;
		}
	}
	/* the results after the sequence that covers them */
	__asm__ volatile("lwsync" ::: "memory");
	wait_ticks += ps3_timer_now() - start;
	return 1;
}

void ps3_spu_mixer_statistics(struct ps3_spu_mixer_statistics *statistics)
{
	uint32_t jobs = echo.jobs, busy_ticks = echo.busy_ticks, decode_ticks = echo.decode_ticks;
	uint32_t packets = echo.packets_decoded;

	memset(statistics, 0, sizeof(*statistics));
	if (!timebase_frequency)
		return;
	statistics->up = mixer_up;
	statistics->jobs = jobs - last_jobs;
	statistics->busy_microseconds = microseconds_from_ticks(busy_ticks - last_busy_ticks);
	statistics->decode_microseconds = microseconds_from_ticks(decode_ticks - last_decode_ticks);
	statistics->packets_decoded = packets - last_packets;
	statistics->wait_microseconds = microseconds_from_ticks(wait_ticks);
	statistics->events = wait_events;
	statistics->timeouts = wait_timeouts;
	statistics->receive_errors = wait_errors;
	statistics->event_errors = echo.event_errors;
	last_jobs = jobs;
	last_busy_ticks = busy_ticks;
	last_decode_ticks = decode_ticks;
	last_packets = packets;
	wait_ticks = 0;
	wait_events = wait_timeouts = wait_errors = 0;
}

void *ps3_spu_buffer_allocate(uint32_t size)
{
	return memalign(128, (size + 127) & ~127u);
}

static void mixer_stop(void)
{
	uint64_t start;

	if (!group_running)
		return;
	stopping = 1;
	mixer_up = 0;
	__asm__ volatile("lwsync" ::: "memory");
	bell.quit = 1;
	start = ps3_timer_now();
	while (echo.state != SPU_MIXER_EXITED && ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(1000);
	if (echo.state != SPU_MIXER_EXITED)
		ps3_log_printf("[spu] mixer: the SPU thread did not end in 100 ms; its group is terminated\n");
	group_stop(echo.state != SPU_MIXER_EXITED);
	ps3_log_printf("[spu] mixer down\n");
}

/* ---------- the Direct3D thread's conversions (spu_vertices.c) */

/* the spins of a wait before it sleeps: about 60 us */
#define VERTICES_SPINS 2000

static spu_vertices_bell vertices_bell;
static spu_vertices_echo vertices_echo;
static spu_vertices_setup vertices_setup;
static spu_vertices_task *vertices_ring;
static sysSpuImage vertices_image;
static sys_spu_group_t vertices_group;
static sys_spu_thread_t vertices_thread;
static int vertices_group_running;
static volatile int vertices_up;
static uint32_t vertices_head;
/* after the SPU stopped answering: the tail the fallback reached */
static volatile int vertices_failed;
static volatile uint32_t vertices_failed_tail;
static void (*vertices_fallback)(const spu_vertices_task *task);
static struct ps3_host_lock *vertices_lock;
/* the waits; the statistics read them and start them again */
static uint64_t vertices_wait_ticks;
static uint32_t vertices_waits;
static uint32_t vertices_last_tail, vertices_last_busy, vertices_last_elements, vertices_last_indices;
static uint32_t vertices_last_input_wait, vertices_last_output_wait;

static uint32_t vertices_tail(void)
{
	return vertices_failed ? vertices_failed_tail : vertices_echo.tail;
}

static void vertices_group_stop(int terminate)
{
	u32 cause, status;

	if (__sync_val_compare_and_swap(&vertices_group_running, 1, 0) != 1)
		return;
	if (terminate)
		sysSpuThreadGroupTerminate(vertices_group, 0);
	sysSpuThreadGroupJoin(vertices_group, &cause, &status);
	sysSpuThreadGroupDestroy(vertices_group);
	sysSpuImageClose(&vertices_image);
}

/* (with the lock) the SPU's own echo from its LS, the SPU stopped, and the tasks it left run here */
static void vertices_fail(const char *reason)
{
	spu_vertices_echo copy;
	u64 *words = (u64 *)&copy;
	uint32_t index, tail;
	s32 result = 0;

	vertices_up = 0;
	memset(&copy, 0, sizeof(copy));
	for (index = 0; index < sizeof(copy) / 8 && result == 0; index++)
		result = sysSpuThreadReadLocalStorage(vertices_thread, vertices_echo.ls_address + index * 8, &words[index], 8);
	vertices_group_stop(1);
	tail = vertices_echo.tail;
	ps3_log_printf("[spu] vertices: %s (%u of %u tasks done; the SPU's own echo: %u, stage %u, read 0x%08x); SPU off, "
		"the PPE converts\n", reason, (unsigned)tail, (unsigned)vertices_head, (unsigned)copy.tail, (unsigned)copy.stage,
		(unsigned)result);
	for (index = tail; index != vertices_head; index++)
		vertices_fallback(&vertices_ring[index & (SPU_VERTICES_RING - 1)]);
	vertices_failed_tail = vertices_head;
	__sync_synchronize();
	vertices_failed = 1;
}

/* until the tasks before target are done: a short spin first (a task takes microseconds), then sleeps that leave the
PPE to the other threads */
static void vertices_wait_until(uint32_t target)
{
	uint64_t start;
	uint32_t spins = 0;

	if ((int32_t)(vertices_tail() - target) >= 0)
		return;
	ps3_host_lock_enter(vertices_lock);
	start = ps3_timer_now();
	while ((int32_t)(vertices_tail() - target) < 0)
	{
		if (ps3_timer_now() - start > ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		{
			vertices_fail("a task not done in 100 ms");
			break;
		}
		/* (db16cyc: the other hardware thread takes these dispatch cycles) */
		if (++spins < VERTICES_SPINS)
			__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
		else
			sysUsleep(20);
	}
	/* the tasks' output after the tail that covers them */
	__asm__ volatile("lwsync" ::: "memory");
	vertices_waits++;
	vertices_wait_ticks += ps3_timer_now() - start;
	ps3_timer_add(_ps3_timer_spu_wait, start);
	ps3_host_lock_leave(vertices_lock);
}

int ps3_spu_vertices_start(void (*fallback)(const spu_vertices_task *task))
{
	sysSpuThreadGroupAttribute group_attributes;
	sysSpuThreadAttribute thread_attributes;
	sysSpuThreadArgument arguments;
	uint64_t start;
	s32 result;

	if (vertices_up || vertices_failed)
		return vertices_up;
	spus_initialize();
	if (!vertices_lock)
		vertices_lock = ps3_host_lock_create(0);
	if (!vertices_ring)
		vertices_ring = memalign(128, SPU_VERTICES_RING * sizeof(*vertices_ring));
	if (!vertices_lock || !vertices_ring)
	{
		ps3_log_printf("[spu] vertices: no memory for the task ring\n");
		return 0;
	}
	memset(&vertices_bell, 0, sizeof(vertices_bell));
	memset(&vertices_echo, 0, sizeof(vertices_echo));
	memset(&vertices_setup, 0, sizeof(vertices_setup));
	vertices_head = 0;
	vertices_setup.ring = (uint32_t)(uintptr_t)vertices_ring;
	vertices_fallback = fallback;
	result = sysSpuImageImport(&vertices_image, spu_vertices_elf, SPU_IMAGE_PROTECT);
	if (result != 0)
	{
		ps3_log_printf("[spu] vertices: sysSpuImageImport = 0x%08x\n", (unsigned)result);
		return 0;
	}
	sysSpuThreadGroupAttributeInitialize(group_attributes);
	sysSpuThreadGroupAttributeName(group_attributes, "halo_vertices");
	result = sysSpuThreadGroupCreate(&vertices_group, 1, 100, &group_attributes);
	if (result != 0)
	{
		ps3_log_printf("[spu] vertices: sysSpuThreadGroupCreate = 0x%08x\n", (unsigned)result);
		sysSpuImageClose(&vertices_image);
		return 0;
	}
	sysSpuThreadAttributeInitialize(thread_attributes);
	sysSpuThreadAttributeName(thread_attributes, "spu_vertices");
	sysSpuThreadArgumentInitialize(arguments);
	arguments.arg0 = (u64)(uintptr_t)&vertices_bell;
	arguments.arg1 = (u64)(uintptr_t)&vertices_echo;
	arguments.arg2 = (u64)(uintptr_t)&vertices_setup;
	result = sysSpuThreadInitialize(&vertices_thread, vertices_group, 0, &vertices_image, &thread_attributes, &arguments);
	if (result == 0)
		result = sysSpuThreadGroupStart(vertices_group);
	if (result != 0)
	{
		ps3_log_printf("[spu] vertices: the SPU thread's start = 0x%08x\n", (unsigned)result);
		sysSpuThreadGroupDestroy(vertices_group);
		sysSpuImageClose(&vertices_image);
		return 0;
	}
	vertices_group_running = 1;
	start = ps3_timer_now();
	while (vertices_echo.state != SPU_VERTICES_RUNNING &&
		ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(100);
	if (vertices_echo.state != SPU_VERTICES_RUNNING)
	{
		ps3_log_printf("[spu] vertices: the SPU thread did not report in 100 ms\n");
		vertices_group_stop(1);
		return 0;
	}
	vertices_up = 1;
	ps3_log_printf("[spu] vertices up: a ring of %u tasks at %08x, the echo at %08x in its LS\n",
		(unsigned)SPU_VERTICES_RING, (unsigned)(uintptr_t)vertices_ring, (unsigned)vertices_echo.ls_address);
	return 1;
}

spu_vertices_task *ps3_spu_vertices_task(void)
{
	if (!vertices_up)
		return NULL;
	/* a slot of the ring is free once the SPU is past the task in it */
	if (vertices_head - vertices_echo.tail >= SPU_VERTICES_RING)
	{
		vertices_wait_until(vertices_head - SPU_VERTICES_RING + 1);
		if (!vertices_up)
			return NULL;
	}
	return &vertices_ring[vertices_head & (SPU_VERTICES_RING - 1)];
}

void ps3_spu_vertices_commit(void)
{
	/* the task before the bell: the SPU reads it when the bell's store wakes it */
	__asm__ volatile("lwsync" ::: "memory");
	vertices_bell.head = ++vertices_head;
}

void ps3_spu_vertices_wait(void)
{
	vertices_wait_until(vertices_head);
}

uint32_t ps3_spu_vertices_head(void)
{
	return vertices_head;
}

int ps3_spu_vertices_done(uint32_t head)
{
	int done = (int32_t)(vertices_tail() - head) >= 0;

	/* (the tasks' output after the tail that covers them) */
	if (done)
		__asm__ volatile("lwsync" ::: "memory");
	return done;
}

int ps3_spu_vertices_running(void)
{
	return vertices_up;
}

void ps3_spu_vertices_statistics(struct ps3_spu_vertices_statistics *statistics)
{
	uint32_t tail = vertices_echo.tail, busy = vertices_echo.busy_ticks, elements = vertices_echo.elements;
	uint32_t indices = vertices_echo.indices, input_wait = vertices_echo.input_wait_ticks;
	uint32_t output_wait = vertices_echo.output_wait_ticks;

	memset(statistics, 0, sizeof(*statistics));
	if (!timebase_frequency)
		return;
	statistics->up = vertices_up;
	statistics->tasks = tail - vertices_last_tail;
	statistics->busy_microseconds = microseconds_from_ticks(busy - vertices_last_busy);
	statistics->elements = elements - vertices_last_elements;
	statistics->indices = indices - vertices_last_indices;
	statistics->waits = vertices_waits;
	statistics->wait_microseconds = microseconds_from_ticks(vertices_wait_ticks);
	statistics->input_wait_microseconds = microseconds_from_ticks(input_wait - vertices_last_input_wait);
	statistics->output_wait_microseconds = microseconds_from_ticks(output_wait - vertices_last_output_wait);
	vertices_last_tail = tail;
	vertices_last_busy = busy;
	vertices_last_elements = elements;
	vertices_last_indices = indices;
	vertices_last_input_wait = input_wait;
	vertices_last_output_wait = output_wait;
	vertices_waits = 0;
	vertices_wait_ticks = 0;
}

static void vertices_stop(void)
{
	uint64_t start;

	if (!vertices_group_running)
		return;
	vertices_up = 0;
	__asm__ volatile("lwsync" ::: "memory");
	vertices_bell.quit = 1;
	start = ps3_timer_now();
	while (vertices_echo.state != SPU_VERTICES_EXITED &&
		ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(1000);
	if (vertices_echo.state != SPU_VERTICES_EXITED)
		ps3_log_printf("[spu] vertices: the SPU thread did not end in 100 ms; its group is terminated\n");
	vertices_group_stop(vertices_echo.state != SPU_VERTICES_EXITED);
	ps3_log_printf("[spu] vertices down\n");
}

void ps3_spu_stop(void)
{
	mixer_stop();
	vertices_stop();
}
