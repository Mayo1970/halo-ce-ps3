/* The timer build (ps3_timers.h): every 300 frames, the game thread's time by phase, in ms a frame, and the
RSX's time by part from its timestamps. */

#include "ps3_timers.h"
#include "ps3_spu.h"
#include "ps3_system.h"

#include <stdio.h>
#include <string.h>

#include <sys/systime.h>

#define TIMER_FRAMES 300

uint64_t ps3_timers[_ps3_timer_count];
uint32_t ps3_timer_counts[_ps3_timer_count];
uint32_t ps3_timer_game_ticks;
uint32_t ps3_timer_quads;
uint32_t ps3_timer_ground_query;
uint64_t ps3_render_d3d[_ps3_timer_count];
uint64_t ps3_collision_timers[PS3_COLLISION_USERS];
uint32_t ps3_collision_tests[PS3_COLLISION_USERS];
uint64_t ps3_collision_kind_timers[_ps3_collision_kinds];
uint64_t ps3_collision_function_timers[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_collision_function_calls[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_collision_nodes[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_collision_objects;
uint64_t ps3_los_function_timers[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_los_function_calls[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_los_nodes[PS3_COLLISION_FUNCTIONS];
uint32_t ps3_los_objects;
uint32_t ps3_los_leaves;
uint32_t ps3_los_clusters;
uint64_t ps3_los_loop_time;
uint64_t ps3_los_test_time;
uint32_t ps3_los_candidates;
uint32_t ps3_los_walked;
uint32_t ps3_bounds_walked;
uint32_t ps3_bounds_read;
uint32_t ps3_los_read;
uint32_t ps3_obstruction_calls;
uint32_t ps3_obstruction_computed;
uint64_t ps3_rsx_timers[_ps3_timer_count];
uint64_t ps3_rsx_frame_nanoseconds;
uint32_t ps3_rsx_frames;
uint32_t ps3_rsx_draws[_ps3_timer_count];
uint32_t ps3_rsx_scale_frames[9];
uint64_t ps3_rsx_scale_nanoseconds[9];
uint32_t ps3_rsx_scale_changes;
uint64_t ps3_draw_stamp_nanoseconds[2][PS3_DRAW_STAMP_KEYS];
uint32_t ps3_draw_stamp_draws[2][PS3_DRAW_STAMP_KEYS];
uint64_t ps3_draw_stamp_part_nanoseconds[_ps3_stamp_count];
uint32_t ps3_draw_stamp_part_draws[_ps3_stamp_count];
uint32_t ps3_draw_stamp_frames;
uint32_t ps3_draw_stamp_dropped;
int ps3_draw_stamps_part = -1;
int ps3_transparent_origin;
void (*ps3_timers_shaders_log)(void);
void (*ps3_timers_direct3d_collect)(void);
volatile uint32_t ps3_spike_counts[_ps3_spike_count];

/* a frame this long on the game thread is a spike: one line of its parts, a few a window */
#define SPIKE_MILLISECONDS 40.0
#define SPIKE_LINES 10
/* a part in the line from half a millisecond */
#define SPIKE_PART_MILLISECONDS 0.5

/* the timers and counts at the frame's start */
static struct
{
	uint64_t start;
	uint64_t timers[_ps3_timer_count];
	uint32_t texture_waits;
	uint32_t ticks;
	uint32_t counts[_ps3_spike_count];
	uint32_t lines;
	uint32_t skipped;
} spike;

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
/* the game's actions (actors.h), as the timers from _ps3_timer_action_none */
static const char *const action_names[] =
{
	"none", "sleep", "alert", "fight", "flee", "uncover", "guard", "search", "wait", "vehicle", "charge", "obey",
	"converse", "avoid",
};
/* the transparent groups' shader types, as the timers from _ps3_timer_transparent_effect */
static const char *const transparent_names[] =
{
	"effect", "model", "generic", "chicago", "water", "glass", "meter", "plasma", "other",
};
/* the specular part's splits, as the timers from _ps3_timer_specular_lights */
static const char *const specular_names[] =
{
	"lights", "specular lightmaps", "reflection masks", "mirrors", "reflections", "transparent", "fog",
};
/* the effects pass's splits on the RSX, as the timers from _ps3_timer_effects_weather */
static const char *const effects_names[] =
{
	"weather", "particles", "contrails", "transparent", "decals", "detail objects",
};
/* the effects pass's stamp parts, from _ps3_stamp_effects_rest */
static const char *const stamp_names[] =
{
	"the rest", "weather", "particles", "particle systems", "contrails", "other effect shaders", "model", "generic",
	"chicago", "water", "glass", "meter", "plasma", "other shaders", "decals", "detail objects",
};

typedef char names_check[sizeof(stamp_names) / sizeof(stamp_names[0]) == _ps3_stamp_count - _ps3_stamp_effects_rest &&
	sizeof(effects_names) / sizeof(effects_names[0]) == _ps3_timer_effects_detail - _ps3_timer_effects_weather + 1 ?
	1 : -1];
/* the objects' widgets by type, as the timers from _ps3_timer_widget_flag */
static const char *const widget_names[] =
{
	"flag", "antenna", "glow", "light volume", "lightning",
};
/* the Direct3D thread's records by type, as the timers from _ps3_timer_record_wrap */
static const char *const record_names[] =
{
	"wrap", "states", "attributes", "texture", "vertex shader", "constants", "constant mode", "viewport",
	"render target", "ui offset", "clear", "draw", "indexed draw", "immediate", "elements", "timestamp", "draw stamps",
	"present",
};

/* the game thread's jobs on the game and render SPUs (ps3_spu.c), by kind: jobs a frame, the SPU's microseconds a job
(its DMA waits), the game thread's waits in ms a frame; the copies are the checkpoint's three kinds */
static void spu_game_log(uint32_t frames, const double *t)
{
	static const char *const names[] =
	{
		"copies", "shadows", "weather", "particles", "lights", "visibility", "sprites", "contrails", "interpolation",
	};
	static const int kinds[][3] =
	{
		{ SPU_GAME_COPY, SPU_GAME_ZEROES, SPU_GAME_ZERO },
		{ SPU_GAME_SHADOW, 0, 0 },
		{ SPU_GAME_WEATHER, 0, 0 },
		{ SPU_GAME_PARTICLES, 0, 0 },
		{ SPU_GAME_LIGHTS, 0, 0 },
		{ SPU_GAME_VISIBILITY, 0, 0 },
		{ SPU_GAME_SPRITES, 0, 0 },
		{ SPU_GAME_CONTRAILS, 0, 0 },
		{ SPU_GAME_INTERPOLATION, 0, 0 },
	};
	static const int waits[] =
	{
		_ps3_timer_spu_game_copy_wait, _ps3_timer_spu_game_shadow_wait, _ps3_timer_spu_game_weather_wait,
		_ps3_timer_spu_game_particles_wait, _ps3_timer_spu_game_lights_wait, _ps3_timer_spu_game_visibility_wait,
		_ps3_timer_spu_game_sprites_wait, _ps3_timer_spu_game_contrails_wait, _ps3_timer_spu_game_interpolation_wait,
	};
	struct ps3_spu_game_statistics s, r;
	char line[1536];
	unsigned long length;
	unsigned index, part;

	ps3_spu_game_statistics(0, &s);
	ps3_spu_game_statistics(1, &r);
	length = (unsigned long)snprintf(line, sizeof(line), "[time] game SPU%s, render SPU%s (jobs a frame; us a job, DMA; "
		"the waits, ms a frame):", s.up ? "" : " off", r.up ? "" : " off");
	for (index = 0; index < sizeof(names) / sizeof(names[0]) && length < sizeof(line); index++)
	{
		uint32_t jobs = 0, busy = 0, dma = 0;

		/* (each kind runs on one of the two) */
		for (part = 0; part < 3 && kinds[index][part]; part++)
		{
			jobs += s.jobs[kinds[index][part]] + r.jobs[kinds[index][part]];
			busy += s.busy_microseconds[kinds[index][part]] + r.busy_microseconds[kinds[index][part]];
			dma += s.dma_microseconds[kinds[index][part]] + r.dma_microseconds[kinds[index][part]];
		}
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f, %.1f; %.3f)",
			index ? "," : "", names[index], (double)jobs / frames, jobs ? (double)busy / jobs : 0.0,
			jobs ? (double)dma / jobs : 0.0, t[waits[index]]);
	}
	if (length < sizeof(line))
	{
		snprintf(line + length, sizeof(line) - length, "; cache %u found, %u fetched; refused %u; shadows checked %u "
			"(%.3f ms a frame), apart %u\n", (unsigned)(s.cache_hits + r.cache_hits),
			(unsigned)(s.cache_misses + r.cache_misses), (unsigned)(s.refused + r.refused),
			(unsigned)ps3_timer_counts[_ps3_timer_spu_shadow_check], t[_ps3_timer_spu_shadow_check],
			(unsigned)ps3_timer_counts[_ps3_timer_spu_shadow_apart]);
	}
	ps3_log_printf("%s", line);
}

/* the AI's lines: line of sight (ai.c, with its rays' collision parts), action control by action, the move */
static void ai_log(double tick_scale, const double *k)
{
	char line[1024];
	unsigned long length;
	double ticks = ps3_timer_game_ticks ? (double)ps3_timer_game_ticks : 1.0;
	uint32_t first_rays = ps3_timer_counts[_ps3_timer_los_first_ray];
	uint32_t avoidance_calls = ps3_timer_counts[_ps3_timer_move_avoidance];
	int action;

	/* the collision functions: 0 vector-structure, 1 vector-objects, 3 model-vector, 4 and 5 the bsp vectors */
	ps3_log_printf("[time] ai line of sight, ms a tick: total %.2f, calls %.1f, with a ray %.1f; first ray %.2f, "
		"fog %.2f, mode rays %.2f, rest %.2f; rays %.1f, first ray length %.1f; structure %.2f (bsp %.2f, bsp nodes "
		"%.0f), objects %.2f (models %.2f, object bsps %.2f, object bsp nodes %.0f); leaves %.1f, clusters %.1f, "
		"objects tested %.1f\n", k[_ps3_timer_los_total], ps3_timer_counts[_ps3_timer_los_total] / ticks,
		first_rays / ticks, k[_ps3_timer_los_first_ray], k[_ps3_timer_los_fog], k[_ps3_timer_los_mode_rays],
		k[_ps3_timer_los_total] - k[_ps3_timer_los_first_ray] - k[_ps3_timer_los_fog] - k[_ps3_timer_los_mode_rays],
		ps3_collision_tests[2] / ticks, first_rays ? ps3_timer_counts[_ps3_timer_los_length] / 100.0 / first_rays : 0.0,
		ps3_los_function_timers[0] * tick_scale, ps3_los_function_timers[5] * tick_scale, ps3_los_nodes[5] / ticks,
		ps3_los_function_timers[1] * tick_scale, ps3_los_function_timers[3] * tick_scale,
		ps3_los_function_timers[4] * tick_scale, ps3_los_nodes[4] / ticks, ps3_los_leaves / ticks,
		ps3_los_clusters / ticks, ps3_los_objects / ticks);
	/* the loop holds the iterator (the copies' walk or kept block, and their test) and the tests */
	ps3_log_printf("[time] ai line of sight objects, ms a tick: loop %.2f, tests %.2f, iterator %.2f; walked %.1f, "
		"read from kept blocks %.1f, passed %.1f, tested %.1f\n", ps3_los_loop_time * tick_scale,
		ps3_los_test_time * tick_scale, (ps3_los_loop_time - ps3_los_test_time) * tick_scale, ps3_los_walked / ticks,
		ps3_los_read / ticks, ps3_los_candidates / ticks, ps3_los_objects / ticks);
	length = (unsigned long)snprintf(line, sizeof(line), "[time] action control, ms a tick (calls a tick):");
	for (action = 0; action <= _ps3_timer_action_avoid - _ps3_timer_action_none && length < sizeof(line); action++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)", action ? "," : "",
			action_names[action], k[_ps3_timer_action_none + action],
			ps3_timer_counts[_ps3_timer_action_none + action] / ticks);
	}
	ps3_log_printf("%s\n", line);
	ps3_log_printf("[time] ai move, ms a tick (calls a tick): vector avoidance %.2f (%.1f), movement %.2f (%.1f; "
		"pathfinding location %.2f, line test %.2f), firing positions %.2f, rest %.2f; path refreshes %.2f (%.1f), "
		"path builds %.2f (%.1f)\n", k[_ps3_timer_move_avoidance], ps3_timer_counts[_ps3_timer_move_avoidance] / ticks,
		k[_ps3_timer_move_movement], ps3_timer_counts[_ps3_timer_move_movement] / ticks, k[_ps3_timer_move_location],
		k[_ps3_timer_move_line], k[_ps3_timer_move_firing_positions], k[_ps3_timer_actor_move] -
		k[_ps3_timer_move_avoidance] - k[_ps3_timer_move_movement] - k[_ps3_timer_move_firing_positions],
		k[_ps3_timer_path_refresh], ps3_timer_counts[_ps3_timer_path_refresh] / ticks, k[_ps3_timer_path_build],
		ps3_timer_counts[_ps3_timer_path_build] / ticks);
	ps3_log_printf("[time] ai paths, ms a tick (calls a tick): setup %.2f (%.1f), search %.2f (%.1f), nodes a search "
		"%.1f, path made %.2f (%.1f), flying %.2f (%.1f), cached %.2f (%.1f)\n", k[_ps3_timer_path_setup],
		ps3_timer_counts[_ps3_timer_path_setup] / ticks, k[_ps3_timer_path_search],
		ps3_timer_counts[_ps3_timer_path_search] / ticks, ps3_timer_counts[_ps3_timer_path_search] ?
		(double)ps3_timer_counts[_ps3_timer_path_nodes] / ps3_timer_counts[_ps3_timer_path_search] : 0.0,
		k[_ps3_timer_path_make], ps3_timer_counts[_ps3_timer_path_make] / ticks, k[_ps3_timer_path_flying],
		ps3_timer_counts[_ps3_timer_path_flying] / ticks, k[_ps3_timer_path_cached],
		ps3_timer_counts[_ps3_timer_path_cached] / ticks);
	ps3_log_printf("[time] ai avoidance, ms a tick (a call): setup %.2f (objects %.1f), structure tests %.2f (%.1f), "
		"object tests %.2f (%.1f), rest %.2f; calls %.1f\n", k[_ps3_timer_avoid_setup],
		avoidance_calls ? (double)ps3_timer_counts[_ps3_timer_avoid_setup] / avoidance_calls : 0.0,
		k[_ps3_timer_avoid_structure],
		avoidance_calls ? (double)ps3_timer_counts[_ps3_timer_avoid_structure] / avoidance_calls : 0.0,
		k[_ps3_timer_avoid_objects],
		avoidance_calls ? (double)ps3_timer_counts[_ps3_timer_avoid_objects] / avoidance_calls : 0.0,
		k[_ps3_timer_move_avoidance] - k[_ps3_timer_avoid_setup] - k[_ps3_timer_avoid_structure] -
		k[_ps3_timer_avoid_objects], avoidance_calls / ticks);
	ps3_log_printf("[time] ai decisions, ms a tick (calls a tick): situation %.2f, emotion %.2f, orders %.2f, decision "
		"loop %.2f (decide %.2f (%.1f), perform %.2f (%.1f))\n", k[_ps3_timer_actor_situation],
		k[_ps3_timer_actor_emotion], k[_ps3_timer_actor_orders], k[_ps3_timer_actor_decision_loop],
		k[_ps3_timer_decision_decide], ps3_timer_counts[_ps3_timer_decision_decide] / ticks,
		k[_ps3_timer_decision_perform], ps3_timer_counts[_ps3_timer_decision_perform] / ticks);
	/* the traverse holds the pops, the edges and their loops; the loops hold the closest points */
	ps3_log_printf("[time] ai path search, ms a tick: reset %.2f, begin %.2f, traverse %.2f (pops %.2f, edges %.2f, "
		"edge loops %.2f, closest points %.2f; surfaces %.1f), debug copy %.2f; path made: smoothing %.2f, "
		"obstacles %.2f\n", k[_ps3_timer_path_reset], k[_ps3_timer_path_begin], k[_ps3_timer_path_traverse],
		k[_ps3_timer_path_pop], k[_ps3_timer_path_edges], k[_ps3_timer_path_edge_loop], k[_ps3_timer_path_closest],
		ps3_timer_counts[_ps3_timer_path_edges] / ticks, k[_ps3_timer_path_debug_copy], k[_ps3_timer_path_smooth],
		k[_ps3_timer_path_obstacles]);
	ps3_log_printf("[time] ai path smoothing, ms a tick (calls a tick): pill tests %.2f (%.1f), turning points %.2f "
		"(%.1f), surfaces %.2f (%.1f)\n", k[_ps3_timer_smooth_pills], ps3_timer_counts[_ps3_timer_smooth_pills] / ticks,
		k[_ps3_timer_smooth_turning], ps3_timer_counts[_ps3_timer_smooth_turning] / ticks, k[_ps3_timer_smooth_surface],
		ps3_timer_counts[_ps3_timer_smooth_surface] / ticks);
}

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

/* inside the heavy ticks' largest parts: the type's preprocess, the bipeds' moving and its pill test (every caller's),
the AI props' refreshes; the rests: a part's time outside the parts it lists */
static void tick_parts_log(const double *k)
{
	double ticks = ps3_timer_game_ticks ? (double)ps3_timer_game_ticks : 1.0;
	const uint32_t *c = ps3_timer_counts;

	ps3_log_printf("[time] the type's preprocess by part, ms a tick (calls a tick): unit %.2f (%.1f), weapon %.2f (%.1f), "
		"device %.2f (%.1f), other %.2f (%.1f); in the unit's (applies a tick): legs %.2f, actions %.2f (%.1f), seat "
		"%.2f (%.1f), aiming %.2f (%.1f), looking %.2f (%.1f), rest %.2f; the screens from kept frames %.1f a tick "
		"(checked %u, apart %u)\n", k[_ps3_timer_preprocess_unit],
		c[_ps3_timer_preprocess_unit] / ticks, k[_ps3_timer_preprocess_weapon], c[_ps3_timer_preprocess_weapon] / ticks,
		k[_ps3_timer_preprocess_device], c[_ps3_timer_preprocess_device] / ticks, k[_ps3_timer_preprocess_other],
		c[_ps3_timer_preprocess_other] / ticks, k[_ps3_timer_unit_legs], k[_ps3_timer_unit_actions],
		c[_ps3_timer_unit_actions] / ticks, k[_ps3_timer_unit_seat], c[_ps3_timer_unit_seat] / ticks,
		k[_ps3_timer_unit_aiming], c[_ps3_timer_unit_aiming] / ticks, k[_ps3_timer_unit_looking],
		c[_ps3_timer_unit_looking] / ticks, k[_ps3_timer_preprocess_unit] - k[_ps3_timer_unit_legs] -
		k[_ps3_timer_unit_actions] - k[_ps3_timer_unit_seat] - k[_ps3_timer_unit_aiming] - k[_ps3_timer_unit_looking],
		c[_ps3_timer_aiming_kept] / ticks, (unsigned)c[_ps3_timer_aiming_checked], (unsigned)c[_ps3_timer_aiming_apart]);
	ps3_log_printf("[time] the bipeds' moving, ms a tick (calls a tick): setup %.2f (%.1f), physics %.2f "
		"(collision_move_pill %.2f (%.1f), the support surface's edges %.2f (%.1f), rest %.2f), after %.2f, rest %.2f\n",
		k[_ps3_timer_moving_setup], c[_ps3_timer_moving_setup] / ticks, k[_ps3_timer_moving_physics],
		k[_ps3_timer_moving_pill], c[_ps3_timer_moving_pill] / ticks, k[_ps3_timer_moving_support],
		c[_ps3_timer_moving_support] / ticks, k[_ps3_timer_moving_physics] - k[_ps3_timer_moving_pill] -
		k[_ps3_timer_moving_support], k[_ps3_timer_moving_after], k[_ps3_timer_biped_moving] -
		k[_ps3_timer_moving_setup] - k[_ps3_timer_moving_physics] - k[_ps3_timer_moving_after]);
	ps3_log_printf("[time] collision_move_pill (every caller), ms a tick (calls a tick): features in the sphere %.2f "
		"(%.1f; %.1f features a call; bsp leaves %.2f (%.1f leaves a call), structure %.2f, objects %.2f (%.1f clusters, "
		"%.1f objects walked, %.1f read from kept blocks), in them the objects' tests %.2f (%.1f), rest %.2f), the move "
		"%.2f (%.1f)\n", k[_ps3_timer_pill_features], c[_ps3_timer_pill_features] / ticks,
		c[_ps3_timer_pill_features] ? (double)c[_ps3_timer_pill_feature_count] / c[_ps3_timer_pill_features] : 0.0,
		k[_ps3_timer_pill_leaves], c[_ps3_timer_pill_leaves] ?
		(double)c[_ps3_timer_pill_leaf_count] / c[_ps3_timer_pill_leaves] : 0.0, k[_ps3_timer_pill_structure],
		k[_ps3_timer_pill_objects], c[_ps3_timer_pill_objects] / ticks, c[_ps3_timer_pill_objects_walked] / ticks,
		c[_ps3_timer_pill_objects_read] / ticks, k[_ps3_timer_pill_object_tests], c[_ps3_timer_pill_object_tests] / ticks,
		k[_ps3_timer_pill_features] - k[_ps3_timer_pill_leaves] - k[_ps3_timer_pill_structure] -
		k[_ps3_timer_pill_objects], k[_ps3_timer_pill_move], c[_ps3_timer_pill_move] / ticks);
	ps3_log_printf("[time] the AI props' refreshes, ms a tick (a tick): position %.2f (%.1f), status %.2f (%.1f; of it "
		"the line of sight %.2f), rest %.2f\n", k[_ps3_timer_prop_position], c[_ps3_timer_prop_position] / ticks,
		k[_ps3_timer_prop_status], c[_ps3_timer_prop_status] / ticks, k[_ps3_timer_prop_status_los],
		k[_ps3_timer_perception_prop_refresh] - k[_ps3_timer_prop_position] - k[_ps3_timer_prop_status]);
}

static void timers_log(uint32_t frames, uint64_t elapsed)
{
	double frequency = (double)sysGetTimebaseFrequency(), scale = 1000.0 / frequency / frames;
	double tick_scale = ps3_timer_game_ticks ? 1000.0 / frequency / ps3_timer_game_ticks : 0.0;
	double rsx_scale = ps3_rsx_frames ? 1.0e-6 / ps3_rsx_frames : 0.0;
	double t[_ps3_timer_count], k[_ps3_timer_count], r[_ps3_timer_count], frame = elapsed * scale, effects_rest;
	char line[1024];
	unsigned long length;
	int index;

	for (index = 0; index < _ps3_timer_count; index++)
	{
		t[index] = ps3_timers[index] * scale;
		k[index] = ps3_timers[index] * tick_scale;
		r[index] = ps3_rsx_timers[index] * rsx_scale;
	}
	/* the specular part is its splits'; the effects part's RSX time too (the render line adds its CPU parts) */
	for (index = _ps3_timer_specular_lights; index <= _ps3_timer_specular_fog; index++)
	{
		t[_ps3_timer_window_specular] += t[index];
		r[_ps3_timer_window_specular] += r[index];
	}
	effects_rest = r[_ps3_timer_window_effects];
	for (index = _ps3_timer_effects_weather; index <= _ps3_timer_effects_detail; index++)
		r[_ps3_timer_window_effects] += r[index];
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
	/* the rest: objects_update's time outside its objects' parts */
	ps3_log_printf("[time] objects_update, ms a tick: units %.2f, items %.2f, projectiles %.2f, others %.2f, damage %.2f, "
		"node matrices %.2f, lights %.2f, values %.2f, activation %.2f, cleanup %.2f, rest %.2f\n",
		k[_ps3_timer_objects_units], k[_ps3_timer_objects_items], k[_ps3_timer_objects_projectiles],
		k[_ps3_timer_objects_others], k[_ps3_timer_objects_damage], k[_ps3_timer_objects_nodes],
		k[_ps3_timer_objects_lights], k[_ps3_timer_objects_values], k[_ps3_timer_objects_activation],
		k[_ps3_timer_objects_cleanup], k[_ps3_timer_tick_objects] - k[_ps3_timer_objects_units] -
		k[_ps3_timer_objects_items] - k[_ps3_timer_objects_projectiles] - k[_ps3_timer_objects_others] -
		k[_ps3_timer_objects_damage] - k[_ps3_timer_objects_nodes] - k[_ps3_timer_objects_lights] -
		k[_ps3_timer_objects_values] - k[_ps3_timer_objects_activation] - k[_ps3_timer_objects_cleanup]);
	ps3_log_printf("[time] node matrices, ms a tick: orientations %.2f, overlays and blends %.2f (overlays %.2f (%.1f), "
		"the type's preprocess %.2f (%.1f), interpolations %.2f (%.1f), rest %.2f), matrices %.2f (first node %.2f, "
		"checks %.2f, other nodes %.2f, bounds %.2f); postprocess %.2f; %.1f calls and %.1f nodes a tick\n",
		k[_ps3_timer_nodes_orientations], k[_ps3_timer_nodes_overlays] + k[_ps3_timer_nodes_preprocess] +
		k[_ps3_timer_nodes_interpolate] + k[_ps3_timer_nodes_blends], k[_ps3_timer_nodes_overlays],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_nodes_overlays] / ps3_timer_game_ticks : 0.0,
		k[_ps3_timer_nodes_preprocess],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_nodes_preprocess] / ps3_timer_game_ticks : 0.0,
		k[_ps3_timer_nodes_interpolate],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_nodes_interpolate] / ps3_timer_game_ticks : 0.0,
		k[_ps3_timer_nodes_blends], k[_ps3_timer_nodes_root] + k[_ps3_timer_nodes_checks] +
		k[_ps3_timer_nodes_children] + k[_ps3_timer_nodes_bounds], k[_ps3_timer_nodes_root], k[_ps3_timer_nodes_checks],
		k[_ps3_timer_nodes_children], k[_ps3_timer_nodes_bounds], k[_ps3_timer_nodes_postprocess],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_nodes_orientations] / ps3_timer_game_ticks : 0.0,
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_nodes_children] / ps3_timer_game_ticks : 0.0);
	/* a tick's walk over the objects is what its objects kept leave of the tick's interpolation */
	ps3_log_printf("[time] render interpolation: a tick, ms a tick: objects kept %.2f (%.1f, %.1f nodes), the walk and "
		"the rest %.2f; a frame, ms a frame: snap tests %.2f (%.1f objects), rotations %.2f (%.1f nodes), blends %.2f "
		"(%.1f nodes)\n", k[_ps3_timer_interpolation_objects],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_interpolation_objects] / ps3_timer_game_ticks : 0.0,
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_interpolation_nodes] / ps3_timer_game_ticks : 0.0,
		k[_ps3_timer_tick_interpolation] - k[_ps3_timer_interpolation_objects], t[_ps3_timer_interpolation_snap],
		(double)ps3_timer_counts[_ps3_timer_interpolation_snap] / frames, t[_ps3_timer_interpolation_rotations],
		(double)ps3_timer_counts[_ps3_timer_interpolation_rotations] / frames, t[_ps3_timer_interpolation_blend],
		(double)ps3_timer_counts[_ps3_timer_interpolation_blend] / frames);
	ps3_log_printf("[time] particles, ms a frame: particles_update %.2f (their physics %.2f; %.1f particles, %.1f with "
		"physics), the SPU's taken %.2f (%.1f events), contrails %.2f, particle systems %.2f\n",
		t[_ps3_timer_particles_update], t[_ps3_timer_particles_physics],
		(double)ps3_timer_counts[_ps3_timer_particles_update] / frames,
		(double)ps3_timer_counts[_ps3_timer_particles_physics] / frames, t[_ps3_timer_particles_finish],
		(double)ps3_timer_counts[_ps3_timer_particles_finish] / frames, t[_ps3_timer_contrails_update],
		t[_ps3_timer_particle_systems_update]);
	ps3_log_printf("[time] point physics, ms a frame: %.2f (wind %.2f, rays %.2f; of them without surfaces %.2f); %.1f "
		"calls and %.1f rays a frame (%.1f without surfaces)\n", t[_ps3_timer_point_total], t[_ps3_timer_point_wind],
		t[_ps3_timer_point_rays], t[_ps3_timer_point_free_rays], (double)ps3_timer_counts[_ps3_timer_point_total] / frames,
		(double)ps3_timer_counts[_ps3_timer_point_rays] / frames,
		(double)ps3_timer_counts[_ps3_timer_point_free_rays] / frames);
	ps3_log_printf("[time] ai, ms a tick: conversations %.2f, encounters %.2f, actors %.2f (general %.2f, perception %.2f, "
		"decisions %.2f, actions %.2f, control %.2f, movement %.2f, look and combat %.2f, unit control %.2f), other %.2f; "
		"%.1f actors a tick\n", k[_ps3_timer_ai_conversations], k[_ps3_timer_ai_encounters], k[_ps3_timer_ai_actors],
		k[_ps3_timer_actor_general], k[_ps3_timer_actor_perception], k[_ps3_timer_actor_situation] +
		k[_ps3_timer_actor_emotion] + k[_ps3_timer_actor_orders] + k[_ps3_timer_actor_decision_loop],
		k[_ps3_timer_actor_actions], k[_ps3_timer_actor_clear_output] + k[_ps3_timer_actor_action_control] +
		k[_ps3_timer_actor_communication] + k[_ps3_timer_actor_conversation], k[_ps3_timer_actor_destination] +
		k[_ps3_timer_actor_look_movement] + k[_ps3_timer_actor_move], k[_ps3_timer_actor_look_combat],
		k[_ps3_timer_actor_unit], k[_ps3_timer_ai_other],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_actor_general] / ps3_timer_game_ticks : 0.0);
	ps3_log_printf("[time] ai parts, ms a tick: control: clear output %.2f, action control %.2f, communication %.2f, "
		"conversation %.2f; movement: destination %.2f, look %.2f, move %.2f; perception: refresh %.2f, danger zone "
		"%.2f, props %.2f (refreshes %.2f); %.1f props and %.1f refreshes a tick\n", k[_ps3_timer_actor_clear_output],
		k[_ps3_timer_actor_action_control], k[_ps3_timer_actor_communication], k[_ps3_timer_actor_conversation],
		k[_ps3_timer_actor_destination], k[_ps3_timer_actor_look_movement], k[_ps3_timer_actor_move],
		k[_ps3_timer_perception_refresh], k[_ps3_timer_perception_danger], k[_ps3_timer_perception_props],
		k[_ps3_timer_perception_prop_refresh],
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_perception_props] / ps3_timer_game_ticks : 0.0,
		ps3_timer_game_ticks ? (double)ps3_timer_counts[_ps3_timer_perception_prop_refresh] / ps3_timer_game_ticks : 0.0);
	ai_log(tick_scale, k);
	/* a pass's parts; the objects pass's render_objects holds find, first person, lighting and models; the rest: the
	render's time outside the sound and the windows' parts */
	{
		double objects_pass = t[_ps3_timer_window_objects] + t[_ps3_timer_render_sky] +
			t[_ps3_timer_render_first_person] + t[_ps3_timer_render_lights];
		double lights_pass = t[_ps3_timer_window_lights] + t[_ps3_timer_lights_flares] + t[_ps3_timer_lights_shadows] +
			t[_ps3_timer_lights_diffuse] + t[_ps3_timer_lights_decals];
		double diffuse_pass = t[_ps3_timer_window_diffuse] + t[_ps3_timer_diffuse_structure] +
			t[_ps3_timer_diffuse_decals];
		double effects_pass = t[_ps3_timer_window_effects] + t[_ps3_timer_effects_weather] +
			t[_ps3_timer_effects_particles] + t[_ps3_timer_effects_contrails] + t[_ps3_timer_effects_transparent] +
			t[_ps3_timer_effects_decals] + t[_ps3_timer_effects_detail];
		double interface_pass = t[_ps3_timer_window_interface] + t[_ps3_timer_hud_fog_flares] +
			t[_ps3_timer_hud_screen] + t[_ps3_timer_hud_flash] + t[_ps3_timer_hud_widgets];

		ps3_log_printf("[time] render: sound %.1f; windows: visibility %.1f, objects %.1f, lightmaps %.1f, lights %.1f, "
			"diffuse %.1f, specular %.1f, effects %.1f, interface %.1f, end %.1f; rest %.1f\n", t[_ps3_timer_sound],
			t[_ps3_timer_window_visibility], objects_pass, t[_ps3_timer_window_lightmaps], lights_pass, diffuse_pass,
			t[_ps3_timer_window_specular], effects_pass, interface_pass, t[_ps3_timer_window_end],
			t[_ps3_timer_render] - t[_ps3_timer_sound] - t[_ps3_timer_window_visibility] - objects_pass -
			t[_ps3_timer_window_lightmaps] - lights_pass - diffuse_pass - t[_ps3_timer_window_specular] - effects_pass -
			interface_pass - t[_ps3_timer_window_end]);
	}
	ps3_log_printf("[time] unit parts, ms a tick: unit %.2f, biped %.2f (turning %.2f, moving %.2f, dead air landing "
		"slipping %.2f, animation %.2f, rest %.2f), vehicle %.2f, item %.2f, projectile %.2f, other %.2f\n",
		k[_ps3_timer_part_unit], k[_ps3_timer_part_biped], k[_ps3_timer_biped_turning], k[_ps3_timer_biped_moving],
		k[_ps3_timer_biped_state], k[_ps3_timer_biped_animation], k[_ps3_timer_biped_rest], k[_ps3_timer_part_vehicle],
		k[_ps3_timer_part_item], k[_ps3_timer_part_projectile], k[_ps3_timer_part_other]);
	tick_parts_log(k);
	ps3_log_printf("[time] vehicles, ms a tick (calls a tick): physics %.2f (%.1f), effects %.2f, stopping %.2f, "
		"animation %.2f (%.1f)\n", k[_ps3_timer_vehicle_physics], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_vehicle_physics] / ps3_timer_game_ticks : 0.0, k[_ps3_timer_vehicle_effects],
		k[_ps3_timer_vehicle_stopping], k[_ps3_timer_vehicle_animation], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_vehicle_animation] / ps3_timer_game_ticks : 0.0);
	ps3_log_printf("[time] physics, ms a tick (calls a tick): forces %.2f (%.1f; %.1f mass points), movement %.2f, "
		"units' collisions %.2f, old physics %.2f (%.1f)\n", k[_ps3_timer_physics_compute], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_physics_compute] / ps3_timer_game_ticks : 0.0, tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_physics_mass_points] / ps3_timer_game_ticks : 0.0, k[_ps3_timer_physics_move],
		k[_ps3_timer_physics_unit_collisions], k[_ps3_timer_physics_old], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_physics_old] / ps3_timer_game_ticks : 0.0);
	ps3_log_printf("[time] old physics, ms a tick (a tick): location %.2f (%.1f mass points), ground %.2f, water %.2f, "
		"antigravity probes %.2f (%.1f), move %.2f, rest %.2f\n", k[_ps3_timer_old_location], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_old_location] / ps3_timer_game_ticks : 0.0, k[_ps3_timer_old_ground],
		k[_ps3_timer_old_water], k[_ps3_timer_old_antigrav], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_old_antigrav] / ps3_timer_game_ticks : 0.0, k[_ps3_timer_old_translate],
		k[_ps3_timer_physics_old] - k[_ps3_timer_old_location] - k[_ps3_timer_old_ground] - k[_ps3_timer_old_water] -
		k[_ps3_timer_old_antigrav] - k[_ps3_timer_old_translate]);
	ps3_log_printf("[time] ground planes (new and old physics), ms a tick (a tick): features in the sphere %.2f (%.1f; "
		"%.1f features a call), point tests %.2f (%.1f)\n", k[_ps3_timer_ground_features], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_features] / ps3_timer_game_ticks : 0.0,
		ps3_timer_counts[_ps3_timer_ground_features] ? (double)ps3_timer_counts[_ps3_timer_ground_feature_count] /
		ps3_timer_counts[_ps3_timer_ground_features] : 0.0, k[_ps3_timer_ground_point_test], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_point_test] / ps3_timer_game_ticks : 0.0);
	ps3_log_printf("[time] ground planes' features in the sphere, ms a tick (a tick): bsp leaves %.2f (%.1f; %.1f leaves "
		"a call), structure %.2f (%.1f), objects %.2f (%.1f clusters, %.1f objects walked, %.1f read from kept blocks), "
		"in them the objects' tests %.2f (%.1f), rest %.2f\n", k[_ps3_timer_ground_leaves], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_leaves] / ps3_timer_game_ticks : 0.0,
		ps3_timer_counts[_ps3_timer_ground_leaves] ? (double)ps3_timer_counts[_ps3_timer_ground_leaf_count] /
		ps3_timer_counts[_ps3_timer_ground_leaves] : 0.0, k[_ps3_timer_ground_structure], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_structure] / ps3_timer_game_ticks : 0.0, k[_ps3_timer_ground_objects],
		tick_scale ? (double)ps3_timer_counts[_ps3_timer_ground_objects] / ps3_timer_game_ticks : 0.0, tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_objects_walked] / ps3_timer_game_ticks : 0.0, tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_objects_read] / ps3_timer_game_ticks : 0.0,
		k[_ps3_timer_ground_object_tests], tick_scale ?
		(double)ps3_timer_counts[_ps3_timer_ground_object_tests] / ps3_timer_game_ticks : 0.0,
		k[_ps3_timer_ground_features] - k[_ps3_timer_ground_leaves] - k[_ps3_timer_ground_structure] -
		k[_ps3_timer_ground_objects]);
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
	/* render_model's calls are the objects', the first person's and the sky's; (Direct3D): the game thread's draws and
	constants in a part */
	ps3_log_printf("[time] render_model but the shadows, ms a frame (Direct3D): node matrices %.2f (%.1f nodes a model), "
		"begin %.2f (%.2f), parts %.2f (%.2f), end %.2f (%.2f); %.1f models a frame; the objects' widgets %.2f; "
		"render_objects' Direct3D %.2f\n", t[_ps3_timer_model_nodes], ps3_timer_counts[_ps3_timer_model_parts] ?
		(double)ps3_timer_counts[_ps3_timer_model_nodes] / ps3_timer_counts[_ps3_timer_model_parts] : 0.0,
		t[_ps3_timer_model_begin], ps3_render_d3d[_ps3_timer_model_begin] * scale, t[_ps3_timer_model_parts],
		ps3_render_d3d[_ps3_timer_model_parts] * scale, t[_ps3_timer_model_end],
		ps3_render_d3d[_ps3_timer_model_end] * scale, (double)ps3_timer_counts[_ps3_timer_model_parts] / frames,
		t[_ps3_timer_render_widgets], ps3_render_d3d[_ps3_timer_window_objects] * scale);
	ps3_log_printf("[time] render_model's parts' draws, ms a frame (Direct3D; a frame): model shaders %.2f (%.2f; %.1f), "
		"environment shaders %.2f (%.2f; %.1f), decals %.2f (%.2f; %.1f), transparent submits %.2f (%.2f; %.1f), the loops "
		"%.2f (%.2f)\n", t[_ps3_timer_parts_model], ps3_render_d3d[_ps3_timer_parts_model] * scale,
		(double)ps3_timer_counts[_ps3_timer_parts_model] / frames, t[_ps3_timer_parts_environment],
		ps3_render_d3d[_ps3_timer_parts_environment] * scale,
		(double)ps3_timer_counts[_ps3_timer_parts_environment] / frames, t[_ps3_timer_parts_decals],
		ps3_render_d3d[_ps3_timer_parts_decals] * scale, (double)ps3_timer_counts[_ps3_timer_parts_decals] / frames,
		t[_ps3_timer_parts_transparent], ps3_render_d3d[_ps3_timer_parts_transparent] * scale,
		(double)ps3_timer_counts[_ps3_timer_parts_transparent] / frames, t[_ps3_timer_parts_rest],
		ps3_render_d3d[_ps3_timer_parts_rest] * scale);
	/* every model shader's draw, the first person's too; (Direct3D): the game thread's draws and constants in a part */
	ps3_log_printf("[time] model shaders' draws, ms a frame (Direct3D): render states %.2f, textures %.2f, colours %.2f, "
		"vertex shader %.2f, constants %.2f (%.2f), fog %.2f, pixel shader %.2f, draws %.2f (%.2f; %.1f a frame), fog "
		"screen %.2f\n", t[_ps3_timer_model_draw_states], t[_ps3_timer_model_draw_textures],
		t[_ps3_timer_model_draw_colours], t[_ps3_timer_model_draw_vertex_shader], t[_ps3_timer_model_draw_constants],
		ps3_render_d3d[_ps3_timer_model_draw_constants] * scale, t[_ps3_timer_model_draw_fog],
		t[_ps3_timer_model_draw_pixel_shader], t[_ps3_timer_model_draw_draws],
		ps3_render_d3d[_ps3_timer_model_draw_draws] * scale,
		(double)ps3_timer_counts[_ps3_timer_model_draw_draws] / frames, t[_ps3_timer_model_draw_fog_screen]);
	length = (unsigned long)snprintf(line, sizeof(line), "[time] the objects' widgets by type, ms a frame (a frame):");
	for (index = 0; index <= _ps3_timer_widget_lightning - _ps3_timer_widget_flag && length < sizeof(line); index++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)", index ? "," : "",
			widget_names[index], t[_ps3_timer_widget_flag + index],
			(double)ps3_timer_counts[_ps3_timer_widget_flag + index] / frames);
	}
	ps3_log_printf("%s\n", line);
	ps3_log_printf("[time] lights and diffuse passes, ms a frame (Direct3D): lights: lens flares %.2f (%.2f), shadows "
		"%.2f (%.2f), diffuse lights %.2f (%.2f), decals %.2f (%.2f); diffuse: structure %.2f (%.2f), decals %.2f (%.2f); "
		"the decals' batches a frame %.1f, their vertices %.0f\n",
		t[_ps3_timer_lights_flares], ps3_render_d3d[_ps3_timer_lights_flares] * scale, t[_ps3_timer_lights_shadows],
		ps3_render_d3d[_ps3_timer_lights_shadows] * scale, t[_ps3_timer_lights_diffuse],
		ps3_render_d3d[_ps3_timer_lights_diffuse] * scale, t[_ps3_timer_lights_decals],
		ps3_render_d3d[_ps3_timer_lights_decals] * scale, t[_ps3_timer_diffuse_structure],
		ps3_render_d3d[_ps3_timer_diffuse_structure] * scale, t[_ps3_timer_diffuse_decals],
		ps3_render_d3d[_ps3_timer_diffuse_decals] * scale, (double)ps3_timer_counts[_ps3_timer_decal_draws] / frames,
		(double)ps3_timer_counts[_ps3_timer_decal_vertices] / frames);
	/* the counts a frame, but the surfaces: a shadow's */
	ps3_log_printf("[time] objects' shadows, ms a frame (Direct3D; a frame): the pass's begin and end %.2f (%.2f), checks "
		"and lighting %.2f (%.2f; %.1f objects), begins %.2f (%.2f; %.1f), models %.2f (%.2f; %.1f shadows), volumes %.2f, "
		"structure %.2f (%.2f; surfaces found %.2f, %.1f a shadow; triangles built %.2f; draws %.2f (%.2f)), ends %.2f "
		"(%.2f); a frame: models %.1f (node matrices kept %.1f)\n",
		t[_ps3_timer_shadow_pass], ps3_render_d3d[_ps3_timer_shadow_pass] * scale,
		t[_ps3_timer_shadow_objects], ps3_render_d3d[_ps3_timer_shadow_objects] * scale,
		(double)ps3_timer_counts[_ps3_timer_shadow_objects] / frames, t[_ps3_timer_shadow_begin],
		ps3_render_d3d[_ps3_timer_shadow_begin] * scale, (double)ps3_timer_counts[_ps3_timer_shadow_begin] / frames,
		t[_ps3_timer_shadow_model], ps3_render_d3d[_ps3_timer_shadow_model] * scale,
		(double)ps3_timer_counts[_ps3_timer_shadow_model] / frames, t[_ps3_timer_shadow_volume],
		t[_ps3_timer_shadow_structure], ps3_render_d3d[_ps3_timer_shadow_structure] * scale,
		t[_ps3_timer_shadow_surfaces], ps3_timer_counts[_ps3_timer_shadow_model] ?
		(double)ps3_timer_counts[_ps3_timer_shadow_surfaces] / ps3_timer_counts[_ps3_timer_shadow_model] : 0.0,
		t[_ps3_timer_shadow_triangles], t[_ps3_timer_shadow_draws], ps3_render_d3d[_ps3_timer_shadow_draws] * scale,
		t[_ps3_timer_shadow_end], ps3_render_d3d[_ps3_timer_shadow_end] * scale,
		(double)ps3_timer_counts[_ps3_timer_shadow_models] / frames,
		(double)ps3_timer_counts[_ps3_timer_shadow_models_kept] / frames);
	length = (unsigned long)snprintf(line, sizeof(line), "[time] specular part's Direct3D draws and constants, ms a "
		"frame:");
	for (index = 0; index <= _ps3_timer_specular_fog - _ps3_timer_specular_lights && length < sizeof(line); index++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f", index ? "," : "",
			specular_names[index], ps3_render_d3d[_ps3_timer_specular_lights + index] * scale);
	}
	ps3_log_printf("%s\n", line);
	/* the weather's visibility tests are what its update, sprites and draw leave */
	ps3_log_printf("[time] weather, ms a frame (a frame): update %.2f (%.1f particles; acceleration %.2f, point physics "
		"%.2f, rest %.2f), visibility %.2f, sprites %.2f (%.1f), draw %.2f\n", t[_ps3_timer_weather_update],
		(double)ps3_timer_counts[_ps3_timer_weather_update] / frames, t[_ps3_timer_weather_acceleration],
		t[_ps3_timer_weather_point], t[_ps3_timer_weather_update] - t[_ps3_timer_weather_acceleration] -
		t[_ps3_timer_weather_point], t[_ps3_timer_effects_weather] - t[_ps3_timer_weather_update] -
		t[_ps3_timer_weather_sprites] - t[_ps3_timer_weather_draw], t[_ps3_timer_weather_sprites],
		(double)ps3_timer_counts[_ps3_timer_weather_sprites] / frames, t[_ps3_timer_weather_draw]);
	length = (unsigned long)snprintf(line, sizeof(line), "[time] transparent geometry, ms a frame (groups a frame): "
		"sort %.2f (%.1f; qsort %.2f, the models' order %.2f (glass scan %.2f, roots %.2f, bounds %.2f (%.1f), planes "
		"%.2f, written %.2f)), begin and end %.2f; by shader:", t[_ps3_timer_transparent_sort],
		(double)ps3_timer_counts[_ps3_timer_transparent_sort] / frames, t[_ps3_timer_transparent_qsort],
		t[_ps3_timer_transparent_order_models], t[_ps3_timer_order_scan], t[_ps3_timer_order_roots],
		t[_ps3_timer_order_bounds], (double)ps3_timer_counts[_ps3_timer_order_bounds] / frames, t[_ps3_timer_order_planes],
		t[_ps3_timer_order_write], t[_ps3_timer_transparent_begin_end]);
	for (index = 0; index <= _ps3_timer_transparent_other - _ps3_timer_transparent_effect && length < sizeof(line);
		index++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)", index ? "," : "",
			transparent_names[index], t[_ps3_timer_transparent_effect + index],
			(double)ps3_timer_counts[_ps3_timer_transparent_effect + index] / frames);
	}
	ps3_log_printf("%s; generic definitions a frame: kept %.1f, made %.1f; the models' bounds a frame: VMX %.1f, checked "
		"%.2f, apart %u\n", line, (double)ps3_timer_counts[_ps3_timer_generic_kept] / frames,
		(double)ps3_timer_counts[_ps3_timer_generic_made] / frames,
		(double)ps3_timer_counts[_ps3_timer_bounds_vmx] / frames,
		(double)ps3_timer_counts[_ps3_timer_bounds_checked] / frames, (unsigned)ps3_timer_counts[_ps3_timer_bounds_apart]);
	/* the draws, the flips, the RSX and the SPU are the Direct3D thread's; the vertical blank is the game thread's */
	ps3_log_printf("[time] Direct3D thread: busy %.1f, the game thread's waits %.1f; draws: state %.1f (textures %.1f: "
		"hash %.1f, uploads %.1f; colours %.1f), vertices %.1f, indices %.1f; waits: vblank %.1f, flip %.1f, RSX %.1f, "
		"SPU %.1f\n",
		t[_ps3_timer_thread_busy], t[_ps3_timer_thread_wait], t[_ps3_timer_draw_state], t[_ps3_timer_draw_textures],
		t[_ps3_timer_texture_hash], t[_ps3_timer_texture_upload], t[_ps3_timer_draw_colours],
		t[_ps3_timer_draw_vertices], t[_ps3_timer_draw_indices], t[_ps3_timer_vblank_wait], t[_ps3_timer_flip_wait],
		t[_ps3_timer_rsx_wait], t[_ps3_timer_spu_wait]);
	/* the colours: the uniforms built, then the fragment program set with them; the stages bound again (the others
	kept) and the fragment programs looked up again (the others the draw before's), a frame */
	ps3_log_printf("[time] Direct3D thread's draw state, ms a frame: vertex program found %.2f, targets %.2f, raster "
		"state %.2f, screen constants %.2f, fragment key %.2f, textures %.2f (%.1f stages bound), fragment program "
		"found %.2f (%.1f looked up), colours %.2f (uniforms %.2f, the program set %.2f), vertex program set %.2f, "
		"constants %.2f\n", t[_ps3_timer_draw_program], t[_ps3_timer_draw_targets], t[_ps3_timer_draw_raster],
		t[_ps3_timer_draw_screen], t[_ps3_timer_draw_key], t[_ps3_timer_draw_textures],
		(double)ps3_timer_counts[_ps3_timer_draw_textures] / frames, t[_ps3_timer_draw_fragment],
		(double)ps3_timer_counts[_ps3_timer_draw_fragment] / frames, t[_ps3_timer_draw_colours],
		t[_ps3_timer_draw_uniforms], t[_ps3_timer_draw_colours] - t[_ps3_timer_draw_uniforms],
		t[_ps3_timer_draw_vertex_program], t[_ps3_timer_draw_constants]);
	/* the loop: the thread's busy time outside its records (the room given back, the SPU's pending tasks) */
	{
		double records = 0.0;

		length = (unsigned long)snprintf(line, sizeof(line), "[time] Direct3D thread's records, ms a frame (a frame):");
		for (index = 0; index <= _ps3_timer_record_present - _ps3_timer_record_wrap && length < sizeof(line); index++)
		{
			records += t[_ps3_timer_record_wrap + index];
			length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)", index ? "," : "",
				record_names[index], t[_ps3_timer_record_wrap + index],
				(double)ps3_timer_counts[_ps3_timer_record_wrap + index] / frames);
		}
		ps3_log_printf("%s; the loop %.2f\n", line, t[_ps3_timer_thread_busy] - records);
	}
	/* the draws' record: what the prologue's parts leave of the game thread's draws; its copies of game memory */
	ps3_log_printf("[time] the game thread's Direct3D draws, ms a frame: %.2f; the prologue: render states %.2f, "
		"attributes %.2f, targets %.2f, textures %.2f; the draws' record %.2f (%.1f KB copied); constants %.2f\n",
		t[_ps3_timer_front_draws], t[_ps3_timer_front_states], t[_ps3_timer_front_attributes],
		t[_ps3_timer_front_targets], t[_ps3_timer_front_textures], t[_ps3_timer_front_draws] -
		t[_ps3_timer_front_states] - t[_ps3_timer_front_attributes] - t[_ps3_timer_front_targets] -
		t[_ps3_timer_front_textures], (double)ps3_timer_counts[_ps3_timer_front_draws] / frames / 1024.0,
		t[_ps3_timer_front_constants]);
	spu_game_log(frames, t);
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
	if (ps3_rsx_scale_frames[0] < ps3_rsx_frames || ps3_rsx_scale_changes)
	{
		length = (unsigned long)snprintf(line, sizeof(line), "[time] dynamic resolution, frames (the windows' RSX ms "
			"a frame) by the view's scale:");
		for (index = 8; index >= 5 && length < sizeof(line); index--)
		{
			length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %u/8 %u (%.1f)",
				index < 8 ? "," : "", (unsigned)index, (unsigned)ps3_rsx_scale_frames[index],
				ps3_rsx_scale_frames[index] ? ps3_rsx_scale_nanoseconds[index] * 1.0e-6 / ps3_rsx_scale_frames[index] :
				0.0);
		}
		ps3_log_printf("%s; %u changes\n", line, (unsigned)ps3_rsx_scale_changes);
	}
	length = (unsigned long)snprintf(line, sizeof(line), "[time] specular part, ms a frame: game thread:");
	for (index = 0; index <= _ps3_timer_specular_fog - _ps3_timer_specular_lights && length < sizeof(line); index++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f", index ? "," : "",
			specular_names[index], t[_ps3_timer_specular_lights + index]);
	}
	for (index = 0; index <= _ps3_timer_specular_fog - _ps3_timer_specular_lights && length < sizeof(line); index++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f draws)",
			index ? "," : "; RSX:", specular_names[index], r[_ps3_timer_specular_lights + index],
			(double)ps3_rsx_draws[_ps3_timer_specular_lights + index] / ps3_rsx_frames);
	}
	ps3_log_printf("%s\n", line);
	/* the rest: from the last split to the window's end */
	length = (unsigned long)snprintf(line, sizeof(line), "[time] effects part on the RSX, ms a frame (draws a frame):");
	for (index = 0; index <= _ps3_timer_effects_detail - _ps3_timer_effects_weather && length < sizeof(line); index++)
	{
		length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)", index ? "," : "",
			effects_names[index], r[_ps3_timer_effects_weather + index],
			(double)ps3_rsx_draws[_ps3_timer_effects_weather + index] / ps3_rsx_frames);
	}
	ps3_log_printf("%s, the rest %.2f (%.1f)\n", line, effects_rest,
		(double)ps3_rsx_draws[_ps3_timer_window_effects] / ps3_rsx_frames);
	if (ps3_draw_stamp_frames)
	{
		length = (unsigned long)snprintf(line, sizeof(line), "[time] effects part's stamped draws by kind, ms a "
			"stamped frame (draws):");
		for (index = _ps3_stamp_effects_rest; index < _ps3_stamp_count && length < sizeof(line); index++)
		{
			length += (unsigned long)snprintf(line + length, sizeof(line) - length, "%s %s %.2f (%.1f)",
				index > _ps3_stamp_effects_rest ? "," : "", stamp_names[index - _ps3_stamp_effects_rest],
				ps3_draw_stamp_part_nanoseconds[index] * 1.0e-6 / ps3_draw_stamp_frames,
				(double)ps3_draw_stamp_part_draws[index] / ps3_draw_stamp_frames);
		}
		ps3_log_printf("%s\n", line);
	}
	if (ps3_timers_shaders_log)
		ps3_timers_shaders_log();
}

/* " name ms" for each part of SPIKE_PART_MILLISECONDS or more */
static unsigned long spike_parts(char *line, unsigned long length, unsigned long size, const double *d,
	const int *timers, const char *const *names, int count)
{
	int index;

	for (index = 0; index < count && length < size; index++)
	{
		if (d[timers[index]] >= SPIKE_PART_MILLISECONDS)
			length += (unsigned long)snprintf(line + length, size - length, " %s %.1f", names[index], d[timers[index]]);
	}
	return length < size ? length : size - 1;
}

/* the frame that ends now, from the timers since its start: its parts when it was long */
static void spike_check(uint64_t now)
{
	static const int tick_timers[] =
	{
		_ps3_timer_tick_units, _ps3_timer_tick_ai, _ps3_timer_tick_players, _ps3_timer_tick_effects,
		_ps3_timer_tick_first_person, _ps3_timer_tick_scripts, _ps3_timer_tick_objects, _ps3_timer_tick_hud,
		_ps3_timer_tick_other, _ps3_timer_tick_interpolation, _ps3_timer_tick_network, _ps3_timer_frame_particles,
		_ps3_timer_frame_widgets, _ps3_timer_frame_sound, _ps3_timer_frame_other,
	};
	static const char *const tick_names[] =
	{
		"units", "ai", "players", "effects", "first person", "scripts", "objects", "hud", "other", "interpolation",
		"network", "particles", "widgets", "sound", "frame other",
	};
	static const int detail_timers[] =
	{
		_ps3_timer_ai_conversations, _ps3_timer_ai_encounters, _ps3_timer_ai_actors, _ps3_timer_ai_other,
		_ps3_timer_objects_units, _ps3_timer_objects_items, _ps3_timer_objects_projectiles, _ps3_timer_objects_others,
		_ps3_timer_objects_damage, _ps3_timer_objects_nodes, _ps3_timer_objects_lights, _ps3_timer_objects_activation,
	};
	static const char *const detail_names[] =
	{
		"ai conversations", "ai encounters", "ai actors", "ai other", "objects' units", "objects' items",
		"objects' projectiles", "objects' others", "objects' damage", "node matrices", "objects' lights",
		"objects' activation",
	};
	static const int render_timers[] =
	{
		_ps3_timer_sound, _ps3_timer_sound_cache, _ps3_timer_window_visibility, _ps3_timer_window_objects,
		_ps3_timer_window_lightmaps, _ps3_timer_window_lights, _ps3_timer_window_diffuse, _ps3_timer_window_specular,
		_ps3_timer_window_effects, _ps3_timer_window_interface, _ps3_timer_window_end,
	};
	static const char *const render_names[] =
	{
		"sound", "sound cache", "visibility", "objects", "lightmaps", "lights", "diffuse", "specular", "effects",
		"interface", "end",
	};
	static const int wait_timers[] =
	{
		_ps3_timer_thread_wait, _ps3_timer_texture_cache_wait, _ps3_timer_vblank_wait, _ps3_timer_flip_wait,
		_ps3_timer_rsx_wait, _ps3_timer_spu_wait,
	};
	static const char *const wait_names[] =
	{
		"Direct3D", "texture cache", "vertical blank", "flip", "RSX", "SPU",
	};
	double scale = 1000.0 / (double)sysGetTimebaseFrequency(), frame = (double)(now - spike.start) * scale, rest;
	double d[_ps3_timer_count];
	uint32_t counts[_ps3_spike_count];
	char line[1536];
	unsigned long length;
	int index;

	if (!spike.start || frame < SPIKE_MILLISECONDS)
		return;
	if (spike.lines >= SPIKE_LINES)
	{
		spike.skipped++;
		return;
	}
	spike.lines++;
	for (index = 0; index < _ps3_timer_count; index++)
		d[index] = (double)(ps3_timers[index] - spike.timers[index]) * scale;
	for (index = _ps3_timer_specular_lights; index <= _ps3_timer_specular_fog; index++)
		d[_ps3_timer_window_specular] += d[index];
	for (index = 0; index < _ps3_spike_count; index++)
		counts[index] = ps3_spike_counts[index] - spike.counts[index];
	rest = frame - d[_ps3_timer_ticks] - d[_ps3_timer_render] - d[_ps3_timer_throttle] - d[_ps3_timer_present];
	length = (unsigned long)snprintf(line, sizeof(line), "[spike] t=%.1fs: a frame of %.1f ms: game ticks %.1f (%u;",
		ps3_time_seconds(), frame, d[_ps3_timer_ticks], (unsigned)(ps3_timer_game_ticks - spike.ticks));
	length = spike_parts(line, length, sizeof(line), d, tick_timers, tick_names,
		(int)(sizeof(tick_timers) / sizeof(tick_timers[0])));
	length = spike_parts(line, length, sizeof(line), d, detail_timers, detail_names,
		(int)(sizeof(detail_timers) / sizeof(detail_timers[0])));
	length += (unsigned long)snprintf(line + length, sizeof(line) - length, "), render %.1f (", d[_ps3_timer_render]);
	length = spike_parts(line, length, sizeof(line), d, render_timers, render_names,
		(int)(sizeof(render_timers) / sizeof(render_timers[0])));
	length += (unsigned long)snprintf(line + length, sizeof(line) - length,
		" ), throttle %.1f, present %.1f, rest %.1f; waits (", d[_ps3_timer_throttle], d[_ps3_timer_present], rest);
	length = spike_parts(line, length, sizeof(line), d, wait_timers, wait_names,
		(int)(sizeof(wait_timers) / sizeof(wait_timers[0])));
	if (length < sizeof(line))
		snprintf(line + length, sizeof(line) - length, " ), texture cache waits %u; textures uploaded %u (%u KB), "
			"shaders translated %u pixel, %u vertex; files moved %u KB\n",
			(unsigned)(ps3_timer_counts[_ps3_timer_texture_cache_wait] - spike.texture_waits),
			(unsigned)counts[_ps3_spike_texture_uploads], (unsigned)(counts[_ps3_spike_texture_bytes] >> 10),
			(unsigned)counts[_ps3_spike_pixel_shaders], (unsigned)counts[_ps3_spike_vertex_shaders],
			(unsigned)(counts[_ps3_spike_disk_bytes] >> 10));
	ps3_log_printf("%s", line);
}

/* the timers at a frame's start, the next spike_check's base */
static void spike_snapshot(uint64_t start)
{
	int index;

	spike.start = start;
	memcpy(spike.timers, ps3_timers, sizeof(spike.timers));
	spike.texture_waits = ps3_timer_counts[_ps3_timer_texture_cache_wait];
	spike.ticks = ps3_timer_game_ticks;
	for (index = 0; index < _ps3_spike_count; index++)
		spike.counts[index] = ps3_spike_counts[index];
}

void ps3_timers_frame(void)
{
	static uint64_t window_start;
	static uint32_t frames;
	uint64_t now = ps3_timer_now();

	spike_check(now);
	if (window_start && ++frames < TIMER_FRAMES)
	{
		spike_snapshot(now);
		return;
	}
	if (spike.skipped)
		ps3_log_printf("[spike] %u more frames of %.0f ms or more in the window, not logged\n", (unsigned)spike.skipped,
			SPIKE_MILLISECONDS);
	spike.lines = spike.skipped = 0;
	/* (the Direct3D SPU's timers since the window's start) */
	if (ps3_timers_direct3d_collect)
		ps3_timers_direct3d_collect();
	if (window_start)
		timers_log(frames, now - window_start);
	memset(ps3_timers, 0, sizeof(ps3_timers));
	memset(ps3_timer_counts, 0, sizeof(ps3_timer_counts));
	memset(ps3_render_d3d, 0, sizeof(ps3_render_d3d));
	memset(ps3_collision_timers, 0, sizeof(ps3_collision_timers));
	memset(ps3_collision_tests, 0, sizeof(ps3_collision_tests));
	memset(ps3_collision_kind_timers, 0, sizeof(ps3_collision_kind_timers));
	memset(ps3_collision_function_timers, 0, sizeof(ps3_collision_function_timers));
	memset(ps3_collision_function_calls, 0, sizeof(ps3_collision_function_calls));
	memset(ps3_collision_nodes, 0, sizeof(ps3_collision_nodes));
	ps3_collision_objects = 0;
	memset(ps3_los_function_timers, 0, sizeof(ps3_los_function_timers));
	memset(ps3_los_function_calls, 0, sizeof(ps3_los_function_calls));
	memset(ps3_los_nodes, 0, sizeof(ps3_los_nodes));
	ps3_los_objects = 0;
	ps3_los_leaves = 0;
	ps3_los_clusters = 0;
	ps3_los_loop_time = 0;
	ps3_los_test_time = 0;
	ps3_los_candidates = 0;
	ps3_los_walked = 0;
	ps3_los_read = 0;
	ps3_obstruction_calls = 0;
	ps3_obstruction_computed = 0;
	ps3_timer_quads = 0;
	memset(ps3_rsx_timers, 0, sizeof(ps3_rsx_timers));
	ps3_rsx_frame_nanoseconds = 0;
	ps3_rsx_frames = 0;
	memset(ps3_rsx_draws, 0, sizeof(ps3_rsx_draws));
	memset(ps3_rsx_scale_frames, 0, sizeof(ps3_rsx_scale_frames));
	memset(ps3_rsx_scale_nanoseconds, 0, sizeof(ps3_rsx_scale_nanoseconds));
	ps3_rsx_scale_changes = 0;
	memset(ps3_draw_stamp_nanoseconds, 0, sizeof(ps3_draw_stamp_nanoseconds));
	memset(ps3_draw_stamp_draws, 0, sizeof(ps3_draw_stamp_draws));
	memset(ps3_draw_stamp_part_nanoseconds, 0, sizeof(ps3_draw_stamp_part_nanoseconds));
	memset(ps3_draw_stamp_part_draws, 0, sizeof(ps3_draw_stamp_part_draws));
	ps3_draw_stamp_frames = 0;
	ps3_draw_stamp_dropped = 0;
	ps3_timer_game_ticks = 0;
	frames = 0;
	/* after the log, which is not a phase */
	window_start = ps3_timer_now();
	/* (the frame's time from its start: the log's in it) */
	spike_snapshot(now);
}
