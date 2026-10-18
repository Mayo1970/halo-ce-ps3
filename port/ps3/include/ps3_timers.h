/* PS3_TIMERS.H: the timer build: the game thread's time by phase, logged every 300 frames (ps3_timers.c).
Every layer includes it, so fixed-width types only; only the game's thread adds to the timers. */

#ifndef __HALO_PS3_TIMERS_H
#define __HALO_PS3_TIMERS_H

#include <stdint.h>

enum
{
	/* the main loop (main.c) */
	_ps3_timer_ticks,
	_ps3_timer_render,
	_ps3_timer_throttle,
	_ps3_timer_present,
	/* in the render: the sound (main.c), then each window's parts in order (render.c) */
	_ps3_timer_sound,
	_ps3_timer_window_visibility,
	_ps3_timer_window_objects,
	_ps3_timer_window_lightmaps,
	_ps3_timer_window_lights,
	_ps3_timer_window_diffuse,
	_ps3_timer_window_specular,
	_ps3_timer_window_effects,
	_ps3_timer_window_interface,
	_ps3_timer_window_end,
	/* the Direct3D device's draws (d3d8_rsx.c): the state includes the textures and the colours */
	_ps3_timer_draw_state,
	_ps3_timer_draw_textures,
	_ps3_timer_texture_hash,
	_ps3_timer_texture_upload,
	_ps3_timer_draw_colours,
	_ps3_timer_draw_vertices,
	_ps3_timer_draw_indices,
	/* waits in the present: two flips pending (d3d8_rsx.c), the display flips and the RSX (ps3_gpu.c) */
	_ps3_timer_vblank_wait,
	_ps3_timer_flip_wait,
	_ps3_timer_rsx_wait,
	/* RSX splits only (ps3_rsx_timers): the frame's start, before each window, after the render */
	_ps3_timer_rsx_previous,
	_ps3_timer_rsx_before,
	_ps3_timer_rsx_after,
	_ps3_timer_count
};

/* timebase ticks */
extern uint64_t ps3_timers[_ps3_timer_count];
/* the game ticks run, for the time of one */
extern uint32_t ps3_timer_game_ticks;
/* the RSX's nanoseconds from each split to the next, from its timestamps (ps3_gpu.c), and per frame */
extern uint64_t ps3_rsx_timers[_ps3_timer_count];
extern uint64_t ps3_rsx_frame_nanoseconds;
extern uint32_t ps3_rsx_frames;

/* the timebase, read again while its low word is 0 (PSL1GHT's __gettime: a Cell erratum) */
static inline uint64_t ps3_timer_now(void)
{
	uint64_t ticks;

	__asm__ volatile("1: mftb %0; cmpwi 7,%0,0; beq- 7,1b" : "=r"(ticks) : : "cr7");
	return ticks;
}

/* adds the time since start to the timer; returns the time now, the start of a next span */
static inline uint64_t ps3_timer_add(int timer, uint64_t start)
{
	uint64_t now = ps3_timer_now();

	ps3_timers[timer] += now - start;
	return now;
}

/* the main loop's top: one call a frame; every 300 frames it logs the times and starts again */
void ps3_timers_frame(void);
/* a split of the RSX's frame: a timestamp report, which ps3_gpu.c reads two frames on (no stall); returns the
time now, the start of a next span */
uint64_t ps3_timers_rsx_wait(int part);

#endif
