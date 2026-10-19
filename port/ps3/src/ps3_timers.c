/* The timer build (ps3_timers.h): every 300 frames, the game thread's time by phase, in ms a frame, and the
RSX's time by part from its timestamps. */

#include "ps3_timers.h"
#include "ps3_system.h"

#include <stdio.h>
#include <string.h>

#include <sys/systime.h>

#define TIMER_FRAMES 300

uint64_t ps3_timers[_ps3_timer_count];
uint32_t ps3_timer_counts[_ps3_timer_count];
uint32_t ps3_timer_game_ticks;
uint32_t ps3_timer_quads;
uint64_t ps3_collision_timers[PS3_COLLISION_USERS];
uint32_t ps3_collision_tests[PS3_COLLISION_USERS];
uint64_t ps3_collision_kind_timers[_ps3_collision_kinds];
uint64_t ps3_collision_function_timers[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_collision_function_calls[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_collision_nodes[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_collision_objects;
uint32_t ps3_obstruction_calls;
uint32_t ps3_obstruction_computed;
uint64_t ps3_rsx_timers[_ps3_timer_count];
uint64_t ps3_rsx_frame_nanoseconds;
uint32_t ps3_rsx_frames;

/* the game's global_collision_user_names (its "????" as "unknown") and global_collision_function_names
(collision_usage.c) */
static const char *const collision_user_names[PS3_COLLISION_USERS] =
{
	"unknown", "ai-look", "ai-los", "ai-comm", "ai-fire", "ai-melee", "aim", "biped", "melee", "decal", "areadmg",
	"item", "obsrv", "pt-phys", "proj", "light", "sound", "veh", "limp", "object", "ui", "debug",
};
static const char *const collision_function_names[PS3_COLLISION_FUNCTIONS] =
{
	"vector-structure", "vector-objects", "features-in-sphere", "model-vector", "object-bsp-vector",
	"structure-bsp-vector", "object-bsp-sphere", "structure-bsp-sphere",
};

/* the collision lines: by kind, then by user with its tests a frame */
static void collision_log(uint32_t frames, double scale)
{
	char line[1024];
	unsigned long length;
	uint64_t total = 0, tests = 0;
	int user, function;

	for (user = 0; user < PS3_COLLISION_USERS; user++)
	{
		total += ps3_collision_timers[user];
		tests += ps3_collision_tests[user];
	}
	ps3_log_printf("[time] collision, ms a frame: total %.2f (%.1f tests a frame); by test: vector %.2f, sphere %.2f, "
		"pill %.2f, point %.2f, move %.2f\n", total * scale, (double)tests / frames,
		ps3_collision_kind_timers[_ps3_collision_vector] * scale, ps3_collision_kind_timers[_ps3_collision_sphere] * scale,
		ps3_collision_kind_timers[_ps3_collision_pill] * scale, ps3_collision_kind_timers[_ps3_collision_point] * scale,
		ps3_collision_kind_timers[_ps3_collision_move] * scale);
	length = (unsigned long)snprintf(line, sizeof(line), "[time] collision by user, ms a frame (tests a frame):");
	for (user = 0; user < PS3_COLLISION_USERS && length < sizeof(line); user++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)", user ? "," : "",
			collision_user_names[user], ps3_collision_timers[user] * scale, (double)ps3_collision_tests[user] / frames);
	}
	ps3_log_printf("%s\n", line);
	/* a function's time holds the functions it calls (vector-structure holds structure-bsp-vector) */
	length = (unsigned long)snprintf(line, sizeof(line), "[time] collision by function, ms a frame (calls a frame):");
	for (function = 0; function < PS3_COLLISION_FUNCTIONS && length < sizeof(line); function++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)",
			function ? "," : "", collision_function_names[function], ps3_collision_function_timers[function] * scale,
			(double)ps3_collision_function_calls[function] / frames);
	}
	if (length < sizeof(line))
	{
		snprintf(line + length, sizeof(line) - length, "; bsp nodes a frame: object-bsp-vector %.0f, "
			"structure-bsp-vector %.0f, object-bsp-sphere %.0f, structure-bsp-sphere %.0f; objects tested a frame %.1f",
			(double)ps3_collision_nodes[4] / frames, (double)ps3_collision_nodes[5] / frames,
			(double)ps3_collision_nodes[6] / frames, (double)ps3_collision_nodes[7] / frames,
			(double)ps3_collision_objects / frames);
	}
	ps3_log_printf("%s\n", line);
}

static void timers_log(uint32_t frames, uint64_t elapsed)
{
	double frequency = (double)sysGetTimebaseFrequency(), scale = 1000.0 / frequency / frames;
	double tick_scale = ps3_timer_game_ticks ? 1000.0 / frequency / ps3_timer_game_ticks : 0.0;
	double rsx_scale = ps3_rsx_frames ? 1.0e-6 / ps3_rsx_frames : 0.0;
	double t[_ps3_timer_count], k[_ps3_timer_count], r[_ps3_timer_count], frame = elapsed * scale;
	int index;

	for (index = 0; index < _ps3_timer_count; index++)
	{
		t[index] = ps3_timers[index] * scale;
		k[index] = ps3_timers[index] * tick_scale;
		r[index] = ps3_rsx_timers[index] * rsx_scale;
	}
	ps3_log_printf("[time] %u frames, %.1f fps, %.1f ms a frame: game ticks %.1f (%.2f a frame, %.1f ms each), "
		"render %.1f, throttle wait %.1f, present %.1f, rest %.1f\n", (unsigned)frames, 1000.0 / frame, frame,
		t[_ps3_timer_ticks], (double)ps3_timer_game_ticks / frames,
		ps3_timer_game_ticks ? t[_ps3_timer_ticks] * frames / ps3_timer_game_ticks : 0.0, t[_ps3_timer_render],
		t[_ps3_timer_throttle], t[_ps3_timer_present],
		frame - t[_ps3_timer_ticks] - t[_ps3_timer_render] - t[_ps3_timer_throttle] - t[_ps3_timer_present]);
	/* the game ticks' time holds the game's part of every frame too (game_frame) */
	ps3_log_printf("[time] tick, ms each: units %.2f, ai %.2f, players %.2f, effects %.2f, first person %.2f, "
		"scripts %.2f, objects %.2f, hud %.2f, other %.2f, interpolation %.2f, network %.2f; game frame, ms a frame: "
		"particles %.2f, widgets %.2f, sound %.2f, other %.2f\n", k[_ps3_timer_tick_units], k[_ps3_timer_tick_ai],
		k[_ps3_timer_tick_players], k[_ps3_timer_tick_effects], k[_ps3_timer_tick_first_person],
		k[_ps3_timer_tick_scripts], k[_ps3_timer_tick_objects], k[_ps3_timer_tick_hud], k[_ps3_timer_tick_other],
		k[_ps3_timer_tick_interpolation], k[_ps3_timer_tick_network], t[_ps3_timer_frame_particles],
		t[_ps3_timer_frame_widgets], t[_ps3_timer_frame_sound], t[_ps3_timer_frame_other]);
	ps3_log_printf("[time] objects_update, ms a tick: units %.2f, items %.2f, projectiles %.2f, others %.2f, damage %.2f, "
		"node matrices %.2f, lights %.2f, values %.2f, activation %.2f, cleanup %.2f\n", k[_ps3_timer_objects_units],
		k[_ps3_timer_objects_items], k[_ps3_timer_objects_projectiles], k[_ps3_timer_objects_others],
		k[_ps3_timer_objects_damage], k[_ps3_timer_objects_nodes], k[_ps3_timer_objects_lights],
		k[_ps3_timer_objects_values], k[_ps3_timer_objects_activation], k[_ps3_timer_objects_cleanup]);
	/* a pass's parts; the objects pass's render_objects holds find, first person, lighting and models */
	ps3_log_printf("[time] render: sound %.1f; windows: visibility %.1f, objects %.1f, lightmaps %.1f, lights %.1f, "
		"diffuse %.1f, specular %.1f, effects %.1f, interface %.1f, end %.1f\n", t[_ps3_timer_sound],
		t[_ps3_timer_window_visibility], t[_ps3_timer_window_objects] + t[_ps3_timer_render_sky] +
		t[_ps3_timer_render_first_person] + t[_ps3_timer_render_lights], t[_ps3_timer_window_lightmaps],
		t[_ps3_timer_window_lights], t[_ps3_timer_window_diffuse], t[_ps3_timer_window_specular],
		t[_ps3_timer_window_effects] + t[_ps3_timer_effects_weather] + t[_ps3_timer_effects_particles] +
		t[_ps3_timer_effects_contrails] + t[_ps3_timer_effects_transparent] + t[_ps3_timer_effects_decals] +
		t[_ps3_timer_effects_detail], t[_ps3_timer_window_interface] + t[_ps3_timer_hud_fog_flares] +
		t[_ps3_timer_hud_screen] + t[_ps3_timer_hud_flash] + t[_ps3_timer_hud_widgets], t[_ps3_timer_window_end]);
	ps3_log_printf("[time] unit parts, ms a tick: unit %.2f, biped %.2f (turning %.2f, moving %.2f, dead air landing "
		"slipping %.2f, animation %.2f, rest %.2f), vehicle %.2f, item %.2f, projectile %.2f, other %.2f\n",
		k[_ps3_timer_part_unit], k[_ps3_timer_part_biped], k[_ps3_timer_biped_turning], k[_ps3_timer_biped_moving],
		k[_ps3_timer_biped_state], k[_ps3_timer_biped_animation], k[_ps3_timer_biped_rest], k[_ps3_timer_part_vehicle],
		k[_ps3_timer_part_item], k[_ps3_timer_part_projectile], k[_ps3_timer_part_other]);
	ps3_log_printf("[time] sound, ms a frame: sound_render: listener %.2f, looping %.2f, refresh %.2f, prioritize %.2f, "
		"channels %.2f, cache %.2f; game_sound_update: environment %.2f, looping %.2f; DirectSound's lock waits %.2f; "
		"obstruction a frame: calls %.1f, computed %.1f\n", t[_ps3_timer_sound_listener], t[_ps3_timer_sound_looping],
		t[_ps3_timer_sound_refresh], t[_ps3_timer_sound_prioritize], t[_ps3_timer_sound_channels],
		t[_ps3_timer_sound_cache], t[_ps3_timer_game_sound_environment], t[_ps3_timer_game_sound_looping],
		t[_ps3_timer_sound_lock_wait], (double)ps3_obstruction_calls / frames, (double)ps3_obstruction_computed / frames);
	collision_log(frames, scale);
	ps3_log_printf("[time] interface pass: fog and flares %.2f, screen %.2f (effect %.2f, players %.2f, motion sensor %.2f, "
		"weapon %.2f, unit %.2f, navigation %.2f, messages %.2f, game engine %.2f), flash %.2f, widgets %.2f; the game "
		"thread's Direct3D calls: draws %.2f, constants %.2f\n", t[_ps3_timer_hud_fog_flares], t[_ps3_timer_hud_screen],
		t[_ps3_timer_hud_screen_effect], t[_ps3_timer_hud_players], t[_ps3_timer_hud_motion_sensor],
		t[_ps3_timer_hud_weapon], t[_ps3_timer_hud_unit], t[_ps3_timer_hud_navigation], t[_ps3_timer_hud_messages],
		t[_ps3_timer_hud_game_engine], t[_ps3_timer_hud_flash], t[_ps3_timer_hud_widgets], t[_ps3_timer_front_draws],
		t[_ps3_timer_front_constants]);
	/* a quad's parts hold its Direct3D calls; the interface pass's draws and constants hold its quads' */
	ps3_log_printf("[time] screen quads, ms a frame: %.1f quads; states %.2f, constants %.2f, textures %.2f, "
		"shaders %.2f, draw %.2f; the interface pass's Direct3D calls: draws %.2f, constants %.2f\n",
		(double)ps3_timer_quads / frames, t[_ps3_timer_quad_states], t[_ps3_timer_quad_constants],
		t[_ps3_timer_quad_textures], t[_ps3_timer_quad_shaders], t[_ps3_timer_quad_draw],
		t[_ps3_timer_interface_draws], t[_ps3_timer_interface_constants]);
	ps3_log_printf("[time] HUD parts, ms a frame (quads a frame): weapon: state %.2f, crosshairs %.2f (%.1f), "
		"panel %.2f (%.1f), sounds %.2f, grenades %.2f (%.1f); unit: setup %.2f, background %.2f (%.1f), "
		"shield %.2f (%.1f), health %.2f (%.1f), motion sensor elements %.2f (%.1f), motion sensor %.2f (%.1f) "
		"(update %.2f, render %.2f), auxiliary %.2f (%.1f)\n", t[_ps3_timer_hud_weapon_state],
		t[_ps3_timer_hud_crosshairs], (double)ps3_timer_counts[_ps3_timer_hud_crosshairs] / frames,
		t[_ps3_timer_hud_weapon_panel], (double)ps3_timer_counts[_ps3_timer_hud_weapon_panel] / frames,
		t[_ps3_timer_hud_weapon_sounds], t[_ps3_timer_hud_grenades],
		(double)ps3_timer_counts[_ps3_timer_hud_grenades] / frames, t[_ps3_timer_hud_unit_setup],
		t[_ps3_timer_hud_unit_background], (double)ps3_timer_counts[_ps3_timer_hud_unit_background] / frames,
		t[_ps3_timer_hud_shield], (double)ps3_timer_counts[_ps3_timer_hud_shield] / frames, t[_ps3_timer_hud_health],
		(double)ps3_timer_counts[_ps3_timer_hud_health] / frames, t[_ps3_timer_hud_sensor_elements],
		(double)ps3_timer_counts[_ps3_timer_hud_sensor_elements] / frames, t[_ps3_timer_hud_sensor],
		(double)ps3_timer_counts[_ps3_timer_hud_sensor] / frames, t[_ps3_timer_hud_sensor_update],
		t[_ps3_timer_hud_sensor_render], t[_ps3_timer_hud_auxiliary],
		(double)ps3_timer_counts[_ps3_timer_hud_auxiliary] / frames);
	ps3_log_printf("[time] objects pass: sky %.2f, first person %.2f, lights %.2f, render_objects %.2f (find %.2f, "
		"first person %.2f, lighting %.2f, models %.2f); effects pass: weather %.2f, particles %.2f, contrails %.2f, "
		"transparent %.2f, decals %.2f, detail objects %.2f\n", t[_ps3_timer_render_sky], t[_ps3_timer_render_first_person],
		t[_ps3_timer_render_lights], t[_ps3_timer_window_objects], t[_ps3_timer_render_find],
		t[_ps3_timer_render_first_person_draw], t[_ps3_timer_render_lighting], t[_ps3_timer_render_models],
		t[_ps3_timer_effects_weather], t[_ps3_timer_effects_particles], t[_ps3_timer_effects_contrails],
		t[_ps3_timer_effects_transparent], t[_ps3_timer_effects_decals], t[_ps3_timer_effects_detail]);
	/* the draws, the flips and the RSX are the Direct3D thread's; the vertical blank is the game thread's */
	ps3_log_printf("[time] Direct3D thread: busy %.1f, the game thread's waits %.1f; draws: state %.1f (textures %.1f: "
		"hash %.1f, uploads %.1f; colours %.1f), vertices %.1f, indices %.1f; waits: vblank %.1f, flip %.1f, RSX %.1f\n",
		t[_ps3_timer_thread_busy], t[_ps3_timer_thread_wait], t[_ps3_timer_draw_state], t[_ps3_timer_draw_textures],
		t[_ps3_timer_texture_hash], t[_ps3_timer_texture_upload], t[_ps3_timer_draw_colours],
		t[_ps3_timer_draw_vertices], t[_ps3_timer_draw_indices], t[_ps3_timer_vblank_wait], t[_ps3_timer_flip_wait],
		t[_ps3_timer_rsx_wait]);
	if (!ps3_rsx_frames)
		return;
	/* each part: the RSX's time from the split before to its own; the frame's from its first split to its last */
	ps3_log_printf("[time] RSX in %u frames, ms a frame: %.1f; before the windows %.1f, visibility %.1f, objects %.1f, "
		"lightmaps %.1f, lights %.1f, diffuse %.1f, specular %.1f, effects %.1f, interface %.1f, end %.1f, "
		"after the windows %.1f, present %.1f\n", (unsigned)ps3_rsx_frames, ps3_rsx_frame_nanoseconds * rsx_scale,
		r[_ps3_timer_rsx_before], r[_ps3_timer_window_visibility], r[_ps3_timer_window_objects],
		r[_ps3_timer_window_lightmaps], r[_ps3_timer_window_lights], r[_ps3_timer_window_diffuse],
		r[_ps3_timer_window_specular], r[_ps3_timer_window_effects], r[_ps3_timer_window_interface],
		r[_ps3_timer_window_end], r[_ps3_timer_rsx_after], r[_ps3_timer_present]);
}

void ps3_timers_frame(void)
{
	static uint64_t window_start;
	static uint32_t frames;
	uint64_t now = ps3_timer_now();

	if (window_start && ++frames < TIMER_FRAMES)
		return;
	if (window_start)
		timers_log(frames, now - window_start);
	memset(ps3_timers, 0, sizeof(ps3_timers));
	memset(ps3_timer_counts, 0, sizeof(ps3_timer_counts));
	memset(ps3_collision_timers, 0, sizeof(ps3_collision_timers));
	memset(ps3_collision_tests, 0, sizeof(ps3_collision_tests));
	memset(ps3_collision_kind_timers, 0, sizeof(ps3_collision_kind_timers));
	memset(ps3_collision_function_timers, 0, sizeof(ps3_collision_function_timers));
	memset(ps3_collision_function_calls, 0, sizeof(ps3_collision_function_calls));
	memset(ps3_collision_nodes, 0, sizeof(ps3_collision_nodes));
	ps3_collision_objects = 0;
	ps3_obstruction_calls = 0;
	ps3_obstruction_computed = 0;
	ps3_timer_quads = 0;
	memset(ps3_rsx_timers, 0, sizeof(ps3_rsx_timers));
	ps3_rsx_frame_nanoseconds = 0;
	ps3_rsx_frames = 0;
	ps3_timer_game_ticks = 0;
	frames = 0;
	/* after the log, which is not a phase */
	window_start = ps3_timer_now();
}
