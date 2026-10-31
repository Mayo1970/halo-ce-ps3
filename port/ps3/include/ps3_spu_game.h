/* PS3_SPU_GAME.H: the game thread's jobs on two SPUs (ps3_spu.c, port/ps3/spu/spu_game.c). The game's units include it,
so fixed-width types only. */

#ifndef __HALO_PS3_SPU_GAME_JOBS_H
#define __HALO_PS3_SPU_GAME_JOBS_H

#include <stdint.h>

#include "../spu/spu_game.h"

/* the address the SPU's DMA takes (all of the process's memory is under 4 GB) */
#define PS3_SPU_EA(pointer) ((uint32_t)(uintptr_t)(const void *)(pointer))

/* starts the SPU thread; 1 when it runs */
int ps3_spu_game_start(void);
int ps3_spu_game_running(void);
/* a job (spu_game.h): its ticket, 0 while the SPU is off (the caller does the job). The game thread only */
uint32_t ps3_spu_game_submit(uint32_t kind, uint32_t size, uint32_t source, uint32_t target);
/* 1 once the ticket's job is done (its answer says how); 0 when the SPU is off: the caller does the job. A job not
done in 100 ms stops the SPU */
int ps3_spu_game_wait(uint32_t ticket);
/* 1 when the ticket's job is done, without a wait */
int ps3_spu_game_done(uint32_t ticket);

/* the same for the render SPU: the window's jobs, in their own ring */
int ps3_spu_render_start(void);
int ps3_spu_render_running(void);
uint32_t ps3_spu_render_submit(uint32_t kind, uint32_t size, uint32_t source, uint32_t target);
int ps3_spu_render_wait(uint32_t ticket);
int ps3_spu_render_done(uint32_t ticket);

#endif
