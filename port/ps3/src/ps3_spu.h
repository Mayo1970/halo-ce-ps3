/* PS3_SPU.H: the SPU programs (port/ps3/spu) on lv2 SPU threads (ps3_spu.c): the DirectSound mixer's, the Direct3D
thread's conversions. The Xbox layer calls it, so fixed-width types only. */

#ifndef __HALO_PS3_SPU_H
#define __HALO_PS3_SPU_H

#include <stdint.h>

#include "spu_mixer.h"
#include "spu_vertices.h"

struct ps3_spu_mixer_statistics
{
	uint32_t up;
	/* since the last call: the jobs, the SPU's time in them and in their decodes, the packets it decoded */
	uint32_t jobs;
	uint32_t busy_microseconds;
	uint32_t decode_microseconds;
	uint32_t packets_decoded;
	/* the mixer thread's waits for the jobs, and how its waits on the event queue ended */
	uint32_t wait_microseconds;
	uint32_t events;
	uint32_t timeouts;
	uint32_t receive_errors;
	/* the SPU's events that lv2 refused, since the start */
	uint32_t event_errors;
};

/* starts the mixer's SPU thread; 1 when it runs */
int ps3_spu_mixer_start(void);
/* the job the mixer thread fills; NULL while the SPU is off */
spu_mixer_job *ps3_spu_mixer_job(void);
/* runs the job's first voice_count voices and waits for them: 1 with the results in the job. 0 when the SPU did not
answer in 100 ms: it is off from then on, and the PPE mixes */
int ps3_spu_mixer_run(uint32_t voice_count, uint32_t frames);
void ps3_spu_mixer_statistics(struct ps3_spu_mixer_statistics *statistics);
/* memory the SPU's DMA can write: 128-aligned, the size rounded up to 128 bytes; free() frees it */
void *ps3_spu_buffer_allocate(uint32_t size);

struct ps3_spu_vertices_statistics
{
	uint32_t up;
	/* since the last call: the tasks, the SPU's time in them, the elements and the indices it did */
	uint32_t tasks;
	uint32_t busy_microseconds;
	uint32_t elements;
	uint32_t indices;
	/* the waits that found tasks not done, and their time */
	uint32_t waits;
	uint32_t wait_microseconds;
	/* of the SPU's time: its waits for the DMA of the tasks and inputs, and for its output buffers */
	uint32_t input_wait_microseconds;
	uint32_t output_wait_microseconds;
};

/* starts the conversions' SPU thread; fallback runs a task on the PPE, for the tasks left when the SPU stops answering.
1 when it runs */
int ps3_spu_vertices_start(void (*fallback)(const spu_vertices_task *task));
/* a task to fill, then commit; NULL while the SPU is off. The thread that runs the Direct3D records only */
spu_vertices_task *ps3_spu_vertices_task(void);
void ps3_spu_vertices_commit(void);
/* until every committed task is done: before the RSX can read their output, or their input is written again. A task
not done in 100 ms stops the SPU: the fallback does the tasks left, and the PPE converts from then on */
void ps3_spu_vertices_wait(void);
/* the tasks committed so far; 1 when the SPU has done the tasks before head (no wait) */
uint32_t ps3_spu_vertices_head(void);
int ps3_spu_vertices_done(uint32_t head);
/* 1 while the SPU converts */
int ps3_spu_vertices_running(void);
void ps3_spu_vertices_statistics(struct ps3_spu_vertices_statistics *statistics);

/* at the exit */
void ps3_spu_stop(void);

#endif
