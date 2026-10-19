/* PS3_TIMERS.H: the timer build: the game thread's time by phase, logged every 300 frames (ps3_timers.c).
Every layer includes it, so fixed-width types only; the game's thread and the Direct3D thread add to the timers. */

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
	/* the game ticks (game.c, game_time.c): each part, logged in ms a tick */
	_ps3_timer_tick_units,
	_ps3_timer_tick_ai,
	_ps3_timer_tick_players,
	_ps3_timer_tick_effects,
	_ps3_timer_tick_first_person,
	_ps3_timer_tick_scripts,
	_ps3_timer_tick_objects,
	_ps3_timer_tick_hud,
	_ps3_timer_tick_other,
	_ps3_timer_tick_interpolation,
	_ps3_timer_tick_network,
	/* the game's part of every frame, in the ticks' time (game_frame) */
	_ps3_timer_frame_particles,
	_ps3_timer_frame_widgets,
	_ps3_timer_frame_sound,
	_ps3_timer_frame_other,
	/* objects_update (objects.c), in ms a tick: the type updates by kind, then the other steps of an object */
	_ps3_timer_objects_units,
	_ps3_timer_objects_items,
	_ps3_timer_objects_projectiles,
	_ps3_timer_objects_others,
	_ps3_timer_objects_damage,
	_ps3_timer_objects_nodes,
	_ps3_timer_objects_lights,
	_ps3_timer_objects_values,
	_ps3_timer_objects_activation,
	_ps3_timer_objects_cleanup,
	/* the render's objects pass before render_objects (render.c), and in it (render_objects.c) */
	_ps3_timer_render_sky,
	_ps3_timer_render_first_person,
	_ps3_timer_render_lights,
	_ps3_timer_render_find,
	_ps3_timer_render_first_person_draw,
	_ps3_timer_render_lighting,
	_ps3_timer_render_models,
	/* the render's effects pass (render.c) */
	_ps3_timer_effects_weather,
	_ps3_timer_effects_particles,
	_ps3_timer_effects_contrails,
	_ps3_timer_effects_transparent,
	_ps3_timer_effects_decals,
	_ps3_timer_effects_detail,
	/* sound_render's steps (sound_manager.c), game_sound_update's (game_sound.c), the waits for the mixer's lock */
	_ps3_timer_sound_listener,
	_ps3_timer_sound_looping,
	_ps3_timer_sound_refresh,
	_ps3_timer_sound_prioritize,
	_ps3_timer_sound_channels,
	_ps3_timer_sound_cache,
	_ps3_timer_game_sound_environment,
	_ps3_timer_game_sound_looping,
	_ps3_timer_sound_lock_wait,
	/* a type update's parts (object_types.c), in ms a tick; biped_update's steps (bipeds.c) */
	_ps3_timer_part_unit,
	_ps3_timer_part_biped,
	_ps3_timer_part_vehicle,
	_ps3_timer_part_item,
	_ps3_timer_part_projectile,
	_ps3_timer_part_other,
	_ps3_timer_biped_turning,
	_ps3_timer_biped_moving,
	_ps3_timer_biped_state,
	_ps3_timer_biped_animation,
	_ps3_timer_biped_rest,
	/* the render's interface pass (render.c), interface_draw_screen in it (interface.c, hud.c) */
	_ps3_timer_hud_fog_flares,
	_ps3_timer_hud_screen,
	_ps3_timer_hud_screen_effect,
	_ps3_timer_hud_players,
	_ps3_timer_hud_motion_sensor,
	_ps3_timer_hud_weapon,
	_ps3_timer_hud_unit,
	_ps3_timer_hud_navigation,
	_ps3_timer_hud_messages,
	_ps3_timer_hud_game_engine,
	_ps3_timer_hud_flash,
	_ps3_timer_hud_widgets,
	/* the screen quads (rasterizer_xbox_dynavobgeom.c): render states, constants, textures, shaders, the draw;
	the game thread's Direct3D draws and constants in the interface pass (render.c) */
	_ps3_timer_quad_states,
	_ps3_timer_quad_constants,
	_ps3_timer_quad_textures,
	_ps3_timer_quad_shaders,
	_ps3_timer_quad_draw,
	_ps3_timer_interface_draws,
	_ps3_timer_interface_constants,
	/* the weapon HUD's parts (hud_weapon.c) and the unit HUD's (hud_unit.c; the motion sensor's update and render
	in its part: motion_sensor.c), each with the screen quads it draws (ps3_timer_counts) */
	_ps3_timer_hud_weapon_state,
	_ps3_timer_hud_crosshairs,
	_ps3_timer_hud_weapon_panel,
	_ps3_timer_hud_weapon_sounds,
	_ps3_timer_hud_grenades,
	_ps3_timer_hud_unit_setup,
	_ps3_timer_hud_unit_background,
	_ps3_timer_hud_shield,
	_ps3_timer_hud_health,
	_ps3_timer_hud_sensor_elements,
	_ps3_timer_hud_sensor,
	_ps3_timer_hud_sensor_update,
	_ps3_timer_hud_sensor_render,
	_ps3_timer_hud_auxiliary,
	/* the game thread's Direct3D calls (d3d8_rsx.c): the draws with their prologue, the vertex constants; no ring waits */
	_ps3_timer_front_draws,
	_ps3_timer_front_constants,
	/* the Direct3D thread's time running records, and the game thread's waits for it (d3d8_rsx.c) */
	_ps3_timer_thread_busy,
	_ps3_timer_thread_wait,
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

/* the outermost collision test's kind (collisions.c, collision_models.c, collision_usage.c) */
enum
{
	_ps3_collision_vector,
	_ps3_collision_sphere,
	_ps3_collision_pill,
	_ps3_collision_point,
	_ps3_collision_move,
	_ps3_collision_kinds
};

/* the game's collision users and logged collision functions (collision_usage.h) */
#define PS3_COLLISION_USERS 22
#define PS3_COLLISION_FUNCTIONS 8

/* timebase ticks */
extern uint64_t ps3_timers[_ps3_timer_count];
/* a count a timer's part makes (the HUD's parts: screen quads) */
extern uint32_t ps3_timer_counts[_ps3_timer_count];
/* the game ticks run, for the time of one */
extern uint32_t ps3_timer_game_ticks;
/* the screen quads drawn */
extern uint32_t ps3_timer_quads;
/* the collision tests' time and count by the user on top of the game's stack, and their time by kind */
extern uint64_t ps3_collision_timers[PS3_COLLISION_USERS];
extern uint32_t ps3_collision_tests[PS3_COLLISION_USERS];
extern uint64_t ps3_collision_kind_timers[_ps3_collision_kinds];
/* each logged collision function's time and calls at any depth, the bsp nodes its tests entered (collision_bsp.c);
the objects the rays tested (collisions.c) */
extern uint64_t ps3_collision_function_timers[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_collision_function_calls[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_collision_nodes[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_collision_objects;
/* the sounds' obstruction (game_sound.c): the calls, and those computed (not from the cache) */
extern uint32_t ps3_obstruction_calls;
extern uint32_t ps3_obstruction_computed;
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

/* adds the time and the screen quads since a part's start to the timer, and starts the next part there */
static inline void ps3_timer_quad_part(int timer, uint64_t *span, uint32_t *quads)
{
	*span = ps3_timer_add(timer, *span);
	ps3_timer_counts[timer] += ps3_timer_quads - *quads;
	*quads = ps3_timer_quads;
}

/* the main loop's top: one call a frame; every 300 frames it logs the times and starts again */
void ps3_timers_frame(void);
/* a split of the RSX's frame: a timestamp report in the Direct3D thread's order (d3d8_rsx.c), which ps3_gpu.c
reads two frames on (no stall); returns the time now, the start of a next span */
uint64_t ps3_timers_rsx_wait(int part);

#endif
