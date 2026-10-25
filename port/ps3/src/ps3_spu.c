/* The SPU programs (port/ps3/spu, bin2s) on lv2 SPU threads: the DirectSound mixer (spu_mixer.c), a job a chunk;
the Direct3D thread's conversions (spu_vertices.c) and the game thread's jobs (spu_game.c, two SPUs), each a ring. */

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
extern const u8 spu_game_elf[];
extern const u8 spu_direct3d_elf[];

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

/* ---------- the game thread's jobs (spu_game.c): two SPUs with one program, each a ring: the game's (the checkpoint's
copies, the particles' update) and the render's (shadows, weather, lights, visibility, sprites, contrails) */

#define GAME_SPUS 2

struct game_spu
{
	spu_game_bell bell;
	spu_game_echo echo;
	spu_game_setup setup;
	spu_game_job *ring;
	sysSpuImage image;
	sys_spu_group_t group;
	sys_spu_thread_t thread;
	const char *name;
	int group_running;
	volatile int up;
	uint32_t head;
	/* each ring slot's kind, for its wait's timer */
	uint8_t kinds[SPU_GAME_RING];
	/* the waits by kind; the statistics read them and start them again */
	uint64_t wait_ticks[SPU_GAME_KINDS];
	uint32_t waits[SPU_GAME_KINDS];
	uint32_t last_jobs[SPU_GAME_KINDS], last_busy[SPU_GAME_KINDS], last_dma[SPU_GAME_KINDS];
	uint32_t last_hits, last_misses, last_refused;
};

static struct game_spu game_spus[GAME_SPUS] __attribute__((aligned(128)));

static const int game_wait_timers[SPU_GAME_KINDS] =
{
	_ps3_timer_spu_game_copy_wait, _ps3_timer_spu_game_copy_wait, _ps3_timer_spu_game_copy_wait,
	_ps3_timer_spu_game_copy_wait, _ps3_timer_spu_game_shadow_wait, _ps3_timer_spu_game_weather_wait,
	_ps3_timer_spu_game_particles_wait, _ps3_timer_spu_game_lights_wait, _ps3_timer_spu_game_visibility_wait,
	_ps3_timer_spu_game_sprites_wait, _ps3_timer_spu_game_contrails_wait, _ps3_timer_spu_game_interpolation_wait,
};

static void game_group_stop(struct game_spu *g, int terminate)
{
	u32 cause, status;

	if (__sync_val_compare_and_swap(&g->group_running, 1, 0) != 1)
		return;
	if (terminate)
		sysSpuThreadGroupTerminate(g->group, 0);
	sysSpuThreadGroupJoin(g->group, &cause, &status);
	sysSpuThreadGroupDestroy(g->group);
	sysSpuImageClose(&g->image);
}

/* the SPU's own echo from its LS, then the SPU stops: the jobs not done are their callers' (their waits give 0) */
static void game_fail(struct game_spu *g, const char *reason)
{
	spu_game_echo copy;
	u64 *words = (u64 *)&copy;
	uint32_t index;
	s32 result = 0;

	g->up = 0;
	memset(&copy, 0, sizeof(copy));
	for (index = 0; index < sizeof(copy) / 8 && result == 0; index++)
		result = sysSpuThreadReadLocalStorage(g->thread, g->echo.ls_address + index * 8, &words[index], 8);
	game_group_stop(g, 1);
	ps3_log_printf("[spu] %s: %s (%u of %u jobs done; the SPU's own echo: %u, stage %u, read 0x%08x); SPU off, the "
		"PPE does the jobs\n", g->name, reason, (unsigned)g->echo.tail, (unsigned)g->head, (unsigned)copy.tail,
		(unsigned)copy.stage, (unsigned)result);
}

static int game_start(struct game_spu *g, const char *name, const char *group_name, const char *thread_name)
{
	sysSpuThreadGroupAttribute group_attributes;
	sysSpuThreadAttribute thread_attributes;
	sysSpuThreadArgument arguments;
	uint64_t start;
	s32 result;

	if (g->up)
		return 1;
	g->name = name;
	spus_initialize();
	if (!g->ring)
		g->ring = memalign(128, SPU_GAME_RING * sizeof(*g->ring));
	if (!g->ring)
	{
		ps3_log_printf("[spu] %s: no memory for the job ring\n", name);
		return 0;
	}
	memset(&g->bell, 0, sizeof(g->bell));
	memset(&g->echo, 0, sizeof(g->echo));
	memset(&g->setup, 0, sizeof(g->setup));
	g->head = 0;
	g->setup.ring = (uint32_t)(uintptr_t)g->ring;
	result = sysSpuImageImport(&g->image, spu_game_elf, SPU_IMAGE_PROTECT);
	if (result != 0)
	{
		ps3_log_printf("[spu] %s: sysSpuImageImport = 0x%08x\n", name, (unsigned)result);
		return 0;
	}
	sysSpuThreadGroupAttributeInitialize(group_attributes);
	sysSpuThreadGroupAttributeName(group_attributes, group_name);
	result = sysSpuThreadGroupCreate(&g->group, 1, 100, &group_attributes);
	if (result != 0)
	{
		ps3_log_printf("[spu] %s: sysSpuThreadGroupCreate = 0x%08x\n", name, (unsigned)result);
		sysSpuImageClose(&g->image);
		return 0;
	}
	sysSpuThreadAttributeInitialize(thread_attributes);
	sysSpuThreadAttributeName(thread_attributes, thread_name);
	sysSpuThreadArgumentInitialize(arguments);
	arguments.arg0 = (u64)(uintptr_t)&g->bell;
	arguments.arg1 = (u64)(uintptr_t)&g->echo;
	arguments.arg2 = (u64)(uintptr_t)&g->setup;
	result = sysSpuThreadInitialize(&g->thread, g->group, 0, &g->image, &thread_attributes, &arguments);
	if (result == 0)
		result = sysSpuThreadGroupStart(g->group);
	if (result != 0)
	{
		ps3_log_printf("[spu] %s: the SPU thread's start = 0x%08x\n", name, (unsigned)result);
		sysSpuThreadGroupDestroy(g->group);
		sysSpuImageClose(&g->image);
		return 0;
	}
	g->group_running = 1;
	start = ps3_timer_now();
	while (g->echo.state != SPU_GAME_RUNNING &&
		ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(100);
	if (g->echo.state != SPU_GAME_RUNNING)
	{
		ps3_log_printf("[spu] %s: the SPU thread did not report in 100 ms\n", name);
		game_group_stop(g, 1);
		return 0;
	}
	g->up = 1;
	ps3_log_printf("[spu] %s up: a ring of %u jobs at %08x, the echo at %08x in its LS\n", name,
		(unsigned)SPU_GAME_RING, (unsigned)(uintptr_t)g->ring, (unsigned)g->echo.ls_address);
	return 1;
}

static int game_done(struct game_spu *g, uint32_t ticket)
{
	int done = ticket && (int32_t)(g->echo.tail - ticket) >= 0;

	/* (the job's output after the tail that covers it) */
	if (done)
		__asm__ volatile("lwsync" ::: "memory");
	return done;
}

static int game_wait(struct game_spu *g, uint32_t ticket)
{
	uint64_t start;
	uint32_t spins = 0, kind;

	if (game_done(g, ticket))
		return 1;
	if (!ticket || !g->up)
		return 0;
	kind = g->kinds[(ticket - 1) & (SPU_GAME_RING - 1)];
	start = ps3_timer_now();
	while ((int32_t)(g->echo.tail - ticket) < 0)
	{
		if (ps3_timer_now() - start > ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		{
			game_fail(g, "a job not done in 100 ms");
			break;
		}
		/* (db16cyc: the other hardware thread takes these dispatch cycles) */
		if (++spins < VERTICES_SPINS)
			__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
		else
			sysUsleep(20);
	}
	__asm__ volatile("lwsync" ::: "memory");
	g->waits[kind]++;
	g->wait_ticks[kind] += ps3_timer_now() - start;
	ps3_timer_add(game_wait_timers[kind], start);
	return (int32_t)(g->echo.tail - ticket) >= 0;
}

static uint32_t game_submit(struct game_spu *g, uint32_t kind, uint32_t size, uint32_t source, uint32_t target)
{
	spu_game_job *job;

	if (!g->up || kind >= SPU_GAME_KINDS)
		return 0;
	/* a slot of the ring is free once the SPU is past the job in it */
	if (g->head - g->echo.tail >= SPU_GAME_RING && !game_wait(g, g->head - SPU_GAME_RING + 1))
		return 0;
	job = &g->ring[g->head & (SPU_GAME_RING - 1)];
	job->kind = kind;
	job->size = size;
	job->source = source;
	job->target = target;
	g->kinds[g->head & (SPU_GAME_RING - 1)] = (uint8_t)kind;
	/* the job and what it reads before the bell: the SPU reads them when the bell's store wakes it */
	__asm__ volatile("sync" ::: "memory");
	g->bell.head = ++g->head;
	return g->head;
}

int ps3_spu_game_start(void)
{
	return game_start(&game_spus[0], "game", "halo_game", "spu_game");
}

int ps3_spu_game_running(void)
{
	return game_spus[0].up;
}

int ps3_spu_game_done(uint32_t ticket)
{
	return game_done(&game_spus[0], ticket);
}

int ps3_spu_game_wait(uint32_t ticket)
{
	return game_wait(&game_spus[0], ticket);
}

uint32_t ps3_spu_game_submit(uint32_t kind, uint32_t size, uint32_t source, uint32_t target)
{
	return game_submit(&game_spus[0], kind, size, source, target);
}

int ps3_spu_render_start(void)
{
	return game_start(&game_spus[1], "render", "halo_render", "spu_render");
}

int ps3_spu_render_running(void)
{
	return game_spus[1].up;
}

int ps3_spu_render_done(uint32_t ticket)
{
	return game_done(&game_spus[1], ticket);
}

int ps3_spu_render_wait(uint32_t ticket)
{
	return game_wait(&game_spus[1], ticket);
}

uint32_t ps3_spu_render_submit(uint32_t kind, uint32_t size, uint32_t source, uint32_t target)
{
	return game_submit(&game_spus[1], kind, size, source, target);
}

void ps3_spu_game_statistics(int render, struct ps3_spu_game_statistics *statistics)
{
	struct game_spu *g = &game_spus[render ? 1 : 0];
	uint32_t kind;

	memset(statistics, 0, sizeof(*statistics));
	if (!timebase_frequency)
		return;
	statistics->up = g->up;
	for (kind = 0; kind < SPU_GAME_KINDS; kind++)
	{
		uint32_t jobs = g->echo.jobs[kind], busy = g->echo.busy_ticks[kind], dma = g->echo.dma_ticks[kind];

		statistics->jobs[kind] = jobs - g->last_jobs[kind];
		statistics->busy_microseconds[kind] = microseconds_from_ticks(busy - g->last_busy[kind]);
		statistics->dma_microseconds[kind] = microseconds_from_ticks(dma - g->last_dma[kind]);
		statistics->waits[kind] = g->waits[kind];
		statistics->wait_microseconds[kind] = microseconds_from_ticks(g->wait_ticks[kind]);
		g->last_jobs[kind] = jobs;
		g->last_busy[kind] = busy;
		g->last_dma[kind] = dma;
		g->waits[kind] = 0;
		g->wait_ticks[kind] = 0;
	}
	statistics->cache_hits = g->echo.cache_hits - g->last_hits;
	statistics->cache_misses = g->echo.cache_misses - g->last_misses;
	statistics->refused = g->echo.refused - g->last_refused;
	g->last_hits = g->echo.cache_hits;
	g->last_misses = g->echo.cache_misses;
	g->last_refused = g->echo.refused;
}

static void game_stop(struct game_spu *g)
{
	uint64_t start;

	if (!g->group_running)
		return;
	g->up = 0;
	__asm__ volatile("lwsync" ::: "memory");
	g->bell.quit = 1;
	start = ps3_timer_now();
	while (g->echo.state != SPU_GAME_EXITED &&
		ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(1000);
	if (g->echo.state != SPU_GAME_EXITED)
		ps3_log_printf("[spu] %s: the SPU thread did not end in 100 ms; its group is terminated\n", g->name);
	game_group_stop(g, g->echo.state != SPU_GAME_EXITED);
	ps3_log_printf("[spu] %s down\n", g->name);
}

/* ---------- the Direct3D thread's work (spu_direct3d.c): the records, the draw state and the RSX commands */

#define DIRECT3D_QUEUE_SIZE 32

static spu_d3d_bell d3d_bell;
static spu_d3d_echo d3d_echo;
static spu_d3d_setup d3d_setup;
static sysSpuImage d3d_image;
static sys_spu_group_t d3d_group;
static sys_spu_thread_t d3d_thread;
/* the service thread's queue and the game thread's */
static sys_event_queue_t d3d_queues[2];
static int d3d_queues_created, d3d_group_running;
static volatile int d3d_up;

static void direct3d_group_stop(int terminate)
{
	u32 cause, status;

	if (__sync_val_compare_and_swap(&d3d_group_running, 1, 0) != 1)
		return;
	if (terminate)
		sysSpuThreadGroupTerminate(d3d_group, 0);
	sysSpuThreadGroupJoin(d3d_group, &cause, &status);
	sysSpuThreadDisconnectEvent(d3d_thread, SPU_THREAD_EVENT_USER, SPU_D3D_SERVICE_PORT);
	sysSpuThreadDisconnectEvent(d3d_thread, SPU_THREAD_EVENT_USER, SPU_D3D_GAME_PORT);
	sysSpuThreadGroupDestroy(d3d_group);
	sysSpuImageClose(&d3d_image);
}

int ps3_spu_direct3d_start(const spu_d3d_setup *setup)
{
	sys_event_queue_attr_t queue_attributes;
	sysSpuThreadGroupAttribute group_attributes;
	sysSpuThreadAttribute thread_attributes;
	sysSpuThreadArgument arguments;
	uint64_t start;
	s32 result;
	int index;

	if (d3d_up)
		return 1;
	spus_initialize();
	memset(&d3d_bell, 0, sizeof(d3d_bell));
	memset(&d3d_echo, 0, sizeof(d3d_echo));
	d3d_setup = *setup;
	for (index = 0; index < 2 && !d3d_queues_created; index++)
	{
		memset(&queue_attributes, 0, sizeof(queue_attributes));
		queue_attributes.attr_protocol = SYS_EVENT_QUEUE_FIFO;
		queue_attributes.type = SYS_EVENT_QUEUE_PPU;
		memcpy(queue_attributes.name, index ? "d3dgame" : "d3dserv", 8);
		result = sysEventQueueCreate(&d3d_queues[index], &queue_attributes, SYS_EVENT_QUEUE_KEY_LOCAL,
			DIRECT3D_QUEUE_SIZE);
		if (result != 0)
		{
			ps3_log_printf("[spu] direct3d: sysEventQueueCreate = 0x%08x\n", (unsigned)result);
			return 0;
		}
		if (index)
			d3d_queues_created = 1;
	}
	result = sysSpuImageImport(&d3d_image, spu_direct3d_elf, SPU_IMAGE_PROTECT);
	if (result != 0)
	{
		ps3_log_printf("[spu] direct3d: sysSpuImageImport = 0x%08x\n", (unsigned)result);
		return 0;
	}
	sysSpuThreadGroupAttributeInitialize(group_attributes);
	sysSpuThreadGroupAttributeName(group_attributes, "halo_direct3d");
	result = sysSpuThreadGroupCreate(&d3d_group, 1, 100, &group_attributes);
	if (result != 0)
	{
		ps3_log_printf("[spu] direct3d: sysSpuThreadGroupCreate = 0x%08x\n", (unsigned)result);
		sysSpuImageClose(&d3d_image);
		return 0;
	}
	sysSpuThreadAttributeInitialize(thread_attributes);
	sysSpuThreadAttributeName(thread_attributes, "spu_direct3d");
	sysSpuThreadArgumentInitialize(arguments);
	arguments.arg0 = (u64)(uintptr_t)&d3d_bell;
	arguments.arg1 = (u64)(uintptr_t)&d3d_echo;
	arguments.arg2 = (u64)(uintptr_t)&d3d_setup;
	result = sysSpuThreadInitialize(&d3d_thread, d3d_group, 0, &d3d_image, &thread_attributes, &arguments);
	if (result == 0)
		result = sysSpuThreadConnectEvent(d3d_thread, d3d_queues[0], SPU_THREAD_EVENT_USER, SPU_D3D_SERVICE_PORT);
	if (result == 0)
		result = sysSpuThreadConnectEvent(d3d_thread, d3d_queues[1], SPU_THREAD_EVENT_USER, SPU_D3D_GAME_PORT);
	if (result == 0)
		result = sysSpuThreadGroupStart(d3d_group);
	if (result != 0)
	{
		ps3_log_printf("[spu] direct3d: the SPU thread's start = 0x%08x\n", (unsigned)result);
		sysSpuThreadGroupDestroy(d3d_group);
		sysSpuImageClose(&d3d_image);
		return 0;
	}
	d3d_group_running = 1;
	start = ps3_timer_now();
	while (d3d_echo.state != SPU_D3D_RUNNING &&
		ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(100);
	if (d3d_echo.state != SPU_D3D_RUNNING)
	{
		ps3_log_printf("[spu] direct3d: the SPU thread did not report in 100 ms\n");
		direct3d_group_stop(1);
		return 0;
	}
	d3d_up = 1;
	ps3_log_printf("[spu] direct3d up: the echo at %08x in its LS\n", (unsigned)d3d_echo.ls_address);
	return 1;
}

spu_d3d_bell *ps3_spu_direct3d_bell(void)
{
	return &d3d_bell;
}

const volatile spu_d3d_echo *ps3_spu_direct3d_echo(void)
{
	return &d3d_echo;
}

int ps3_spu_direct3d_wait(int queue, uint32_t timeout_microseconds)
{
	sys_event_t event;

	return sysEventQueueReceive(d3d_queues[queue ? 1 : 0], &event, timeout_microseconds) == 0;
}

void ps3_spu_direct3d_drain(int queue)
{
	sysEventQueueDrain(d3d_queues[queue ? 1 : 0]);
}

/* the SPU's own echo from its LS (the copy in main memory is stale while it hangs) */
void ps3_spu_direct3d_report(const char *reason)
{
	spu_d3d_echo copy;
	u64 *words = (u64 *)&copy;
	uint32_t index;
	s32 result = 0;

	memset(&copy, 0, sizeof(copy));
	for (index = 0; index < sizeof(copy) / 8 && result == 0 && d3d_group_running; index++)
		result = sysSpuThreadReadLocalStorage(d3d_thread, d3d_echo.ls_address + index * 8, &words[index], 8);
	ps3_log_printf("[spu] direct3d: %s (the SPU's own echo: %llu run, request %u, %u chunks, record %u, stage %u, "
		"read 0x%08x)\n", reason, (unsigned long long)copy.consumed, (unsigned)copy.request, (unsigned)copy.chunks,
		(unsigned)copy.record_type, (unsigned)copy.stage, (unsigned)result);
}

void ps3_spu_direct3d_stop(void)
{
	uint64_t start;

	if (!d3d_group_running)
		return;
	d3d_up = 0;
	__asm__ volatile("lwsync" ::: "memory");
	d3d_bell.quit = 1;
	start = ps3_timer_now();
	while (d3d_echo.state != SPU_D3D_EXITED &&
		ps3_timer_now() - start < ticks_from_microseconds(JOB_TIMEOUT_MICROSECONDS))
		sysUsleep(1000);
	if (d3d_echo.state != SPU_D3D_EXITED)
		ps3_log_printf("[spu] direct3d: the SPU thread did not end in 100 ms; its group is terminated\n");
	direct3d_group_stop(d3d_echo.state != SPU_D3D_EXITED);
	ps3_log_printf("[spu] direct3d down\n");
}

void ps3_spu_stop(void)
{
	mixer_stop();
	vertices_stop();
	game_stop(&game_spus[0]);
	game_stop(&game_spus[1]);
	ps3_spu_direct3d_stop();
}
