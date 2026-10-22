/* COLLISION_USAGE.H: header included in hcex build. */

#ifndef __COLLISION_USAGE_H
#define __COLLISION_USAGE_H
#pragma once

#include "cseries/cseries.h"
#include <xtl.h>

/* ---------- constants */

enum
{
	_collision_user_unknown,
	_collision_user_ai_look,
	_collision_user_ai_lineofsight,
	_collision_user_ai_comms,
	_collision_user_ai_lineoffire,
	_collision_user_ai_melee,
	_collision_user_aim_assist,
	_collision_user_bipeds,
	_collision_user_melee,
	_collision_user_decals,
	_collision_user_area_damage,
	_collision_user_items,
	_collision_user_observer,
	_collision_user_point_physics,
	_collision_user_projectiles,
	_collision_user_structure_lighting,
	_collision_user_sounds,
	_collision_user_vehicles,
	_collision_user_limp_body_physics,
	_collision_user_objects,
	_collision_user_ui,
	_collision_user_debugging,
	NUMBER_OF_COLLISION_USER_TYPES,
};

enum
{
	_collision_function_vector_structure,
	_collision_function_vector_objects,
	_collision_function_vector_bounds_object,
	_collision_function_vector_intersect_model,
	_collision_function_vector_intersect_bsp_object,
	_collision_function_vector_intersect_bsp_structure,
	_collision_function_sphere_intersect_bsp_object,
	_collision_function_sphere_intersect_bsp_structure,
	NUMBER_OF_COLLISION_FUNCTION_TYPES,
};

enum
{
	MAXIMUM_COLLISION_USER_STACK_DEPTH = 32,
};

/* ---------- macros */

#ifdef HALO_PS3
#include "ps3_timers.h"

/* port: the timer build's collision tests (collision_usage.c): by user and kind (the outermost test), by logged
function; the bsp nodes a test entered (collision_bsp.c), the objects a ray tested (collisions.c) */
#define PS3_COLLISION_BEGIN() ps3_collision_begin()
#define PS3_COLLISION_END(kind) ps3_collision_end((kind), NONE)
/* port: and the same for the AI's line-of-sight rays alone (the user on top of the stack), with the leaves their
bsp test gave and the clusters whose objects they walked (collisions.c) */
#define PS3_COLLISION_LOS() (global_current_collision_user_depth > 0 && \
	global_current_collision_user_depth <= MAXIMUM_COLLISION_USER_STACK_DEPTH && \
	global_current_collision_users[global_current_collision_user_depth - 1] == _collision_user_ai_lineofsight)
#define PS3_COLLISION_NODES(function, count) ((void)(ps3_collision_nodes[(function)] += (uint32_t)(count), \
	PS3_COLLISION_LOS() ? (ps3_los_nodes[(function)] += (uint32_t)(count)) : 0))
#define PS3_COLLISION_OBJECT() ((void)(ps3_collision_objects++, PS3_COLLISION_LOS() ? ps3_los_objects++ : 0))
#define PS3_COLLISION_LEAVES(count) ((void)(PS3_COLLISION_LOS() ? (ps3_los_leaves += (uint32_t)(count)) : 0))
#define PS3_COLLISION_CLUSTER() ((void)(PS3_COLLISION_LOS() ? ps3_los_clusters++ : 0))
/* port: their loop over a cluster's objects (collisions.c): a test's start with the object the copies passed, its end;
the loop's end, with the objects the copies' iterator walked and read from kept blocks since walked and read were read */
#define PS3_COLLISION_TEST_BEGIN(los, span) ((void)((los) ? ((span) = ps3_timer_now(), ps3_los_candidates++) : 0))
#define PS3_COLLISION_TEST_END(los, span) ((void)((los) ? (ps3_los_test_time += ps3_timer_now() - (span)) : 0))
#define PS3_COLLISION_LOOP_END(los, span, walked, read) ((void)((los) ? (ps3_los_loop_time += ps3_timer_now() - (span), \
	ps3_los_walked += ps3_bounds_walked - (walked), ps3_los_read += ps3_bounds_read - (read)) : 0))
/* port: the parts of compute_ground_plane's sphere query alone (physics.c sets ps3_timer_ground_query): a span's start,
its time added to a timer (the next span's start), a count */
#define PS3_SPHERE_SPAN(span) ((void)(ps3_timer_ground_query ? ((span) = ps3_timer_now()) : 0))
#define PS3_SPHERE_ADD(timer, span) ((void)(ps3_timer_ground_query ? ((span) = ps3_timer_add((timer), (span))) : 0))
#define PS3_SPHERE_COUNT(timer, count) ((void)(ps3_timer_ground_query ? \
	(ps3_timer_counts[(timer)] += (uint32_t)(count)) : 0))
#define PS3_SPHERE_TIMED(timer, call) ({ uint64_t ps3_sphere_start = ps3_timer_ground_query ? ps3_timer_now() : 0; \
	__typeof__(call) ps3_sphere_result = (call); \
	PS3_SPHERE_ADD((timer), ps3_sphere_start); PS3_SPHERE_COUNT((timer), 1); ps3_sphere_result; })
#else
#define PS3_COLLISION_BEGIN() ((void)0)
#define PS3_COLLISION_END(kind) ((void)0)
#define PS3_COLLISION_NODES(function, count) ((void)0)
#define PS3_COLLISION_OBJECT() ((void)0)
#define PS3_COLLISION_LEAVES(count) ((void)0)
#define PS3_COLLISION_CLUSTER() ((void)0)
#define PS3_COLLISION_TEST_BEGIN(los, span) ((void)0)
#define PS3_COLLISION_TEST_END(los, span) ((void)0)
#define PS3_COLLISION_LOOP_END(los, span, walked, read) ((void)0)
#define PS3_SPHERE_SPAN(span) ((void)0)
#define PS3_SPHERE_ADD(timer, span) ((void)0)
#define PS3_SPHERE_COUNT(timer, count) ((void)0)
#define PS3_SPHERE_TIMED(timer, call) (call)
#endif

/* ---------- structures */

/* ---------- prototypes/COLLISION_USAGE.C */

#ifdef HALO_PS3
void ps3_collision_begin(
	void);
void ps3_collision_end(
	short kind,
	short function);
#endif

void collision_log_initialize(
	void);
void collision_log_enable(
	boolean enable);
void collision_log_render(
	void);
void collision_log_start_time(
	LARGE_INTEGER *start_time);
void collision_log_end_time(
	short collision_function,
	__int64 end_time);
void collision_log_usage(
	short collision_function);
void collision_log_display(
	char *buffer);
void collision_log_begin_period(
	short time_period);
void collision_log_continue_period(
	short time_period);
void collision_log_end_period(
	void);

/* ---------- globals */

extern short global_current_collision_user_depth;
extern short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];

/* ---------- public code */

#endif // __COLLISION_USAGE_H
