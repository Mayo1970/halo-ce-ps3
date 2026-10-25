/* RASTERIZER_TRANSPARENT_GEOMETRY.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/rasterizer/rasterizer_transparent_geometry.c. */

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "rasterizer.h"
#include "rasterizer_console_vars.h"
#include "rasterizer_geometry.h"
#include "rasterizer_transparent_geometry.h"
#include "rasterizer/xbox/rasterizer_xbox_internal.h"
#include "rasterizer/xbox/rasterizer_xbox_state.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "view_fov.h" /* port: port/linux/game/view_fov.c */

/* ---------- constants */

enum
{
	_shader_type_transparent_water = 7,
	_rasterizer_geometry_first_person_bit = 7,
	_rasterizer_target_render_primary = 0,
};

/* ---------- macros */

#ifdef HALO_PS3
#include "ps3_timers.h"

/* port: the timer build's parts of the draw (port/ps3/include/ps3_timers.h) */
#define PS3_TRANSPARENT_SPAN(span) ((span) = ps3_timer_now())
#define PS3_TRANSPARENT_ADD(timer, span) ((void)ps3_timer_add((timer), (span)))
#define PS3_TRANSPARENT_COUNT(timer, count) (ps3_timer_counts[(timer)] += (uint32_t)(count))
/* port: (the timer build) a new group's maker; a group's draws' stamp part, and the part before the groups again */
#define PS3_TRANSPARENT_ORIGIN(group_index) (ps3_group_origins[(group_index)] = (unsigned char)ps3_transparent_origin)
#define PS3_TRANSPARENT_STAMPS(group) ps3_transparent_stamps(group)
#define PS3_TRANSPARENT_STAMPS_END() ps3_transparent_stamps_end()
#else
#define PS3_TRANSPARENT_SPAN(span) ((void)0)
#define PS3_TRANSPARENT_ADD(timer, span) ((void)0)
#define PS3_TRANSPARENT_COUNT(timer, count) ((void)0)
#define PS3_TRANSPARENT_ORIGIN(group_index) ((void)0)
#define PS3_TRANSPARENT_STAMPS(group) ((void)0)
#define PS3_TRANSPARENT_STAMPS_END() ((void)0)
#endif

/* ---------- structures */

/* January's assert strings name this type and its sorted_index field, and pin
the stride: the group array is walked with a 0xA0 element size. */
struct transparent_geometry_group
{
	unsigned long geometry_flags;
	long object_index;
	long source_object_index;
	struct shader *HALO_PTR32 shader;
	byte opaque10[0x44];
	long dynamic_vertex_buffer_index;
	struct vertex_buffer const *HALO_PTR32 vertex_buffer;
	byte opaque5c[0x14];
	real z_sort;
	byte opaque74[0x1C];
	long sorted_index;
	byte opaque94[9];
	boolean cortana_hack;
	byte opaque9e[2];
};

typedef char transparent_geometry_group_size_assert[
	sizeof(struct transparent_geometry_group) == 0xA0 ? 1 : -1];
typedef char transparent_geometry_group_geometry_flags_offset_assert[
	offsetof(struct transparent_geometry_group, geometry_flags) == 0x0 ? 1 : -1];
typedef char transparent_geometry_group_object_index_offset_assert[
	offsetof(struct transparent_geometry_group, object_index) == 0x4 ? 1 : -1];
typedef char transparent_geometry_group_source_object_index_offset_assert[
	offsetof(struct transparent_geometry_group, source_object_index) == 0x8 ? 1 : -1];
typedef char transparent_geometry_group_shader_offset_assert[
	offsetof(struct transparent_geometry_group, shader) == 0xC ? 1 : -1];
typedef char transparent_geometry_group_dynamic_vertex_buffer_index_offset_assert[
	offsetof(struct transparent_geometry_group, dynamic_vertex_buffer_index) == 0x54 ? 1 : -1];
typedef char transparent_geometry_group_vertex_buffer_offset_assert[
	offsetof(struct transparent_geometry_group, vertex_buffer) == 0x58 ? 1 : -1];
typedef char transparent_geometry_group_z_sort_offset_assert[
	offsetof(struct transparent_geometry_group, z_sort) == 0x70 ? 1 : -1];
typedef char transparent_geometry_group_sorted_index_offset_assert[
	offsetof(struct transparent_geometry_group, sorted_index) == 0x90 ? 1 : -1];
typedef char transparent_geometry_group_cortana_hack_offset_assert[
	offsetof(struct transparent_geometry_group, cortana_hack) == 0x9D ? 1 : -1];

/* ---------- prototypes */
static void rasterizer_sort_external(
	void);
#ifdef HALO_PS3
static int ps3_transparent_timer(
	struct transparent_geometry_group const *group);
static void ps3_transparent_stamps(
	struct transparent_geometry_group const *group);
static void ps3_transparent_stamps_end(
	void);
static void ps3_particles_half_run(
	short *first,
	short *end);
/* port: d3d8_rsx.c's, port_config.c's */
int d3d8_particles_half(int on);
int config_boolean(const char *name);
#endif

/* ---------- globals */

static short transparent_geometry_group_index = 0;
static unsigned long transparent_geometry_group_pending_flags[
	BIT_VECTOR_SIZE_IN_LONGS(RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS)] = { 0 };
static struct transparent_geometry_group *transparent_geometry_groups = NULL;
static struct transparent_geometry_group *transparent_geometry_groups2 = NULL;
static long transparent_geometry_group_count = 0;
static long transparent_geometry_group_count2 = 0;
static short *transparent_geometry_group_sorted_indices = NULL;
static short transparent_geometry_next_group_sorted_index = 0;
#ifdef HALO_PS3
/* port: (the timer build) each group's maker's stamp part (0: its shader's type); the stamp part before the groups,
-1 while none of them changed it */
static unsigned char ps3_group_origins[RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS];
static int ps3_stamps_part_before = -1;
#endif


/* ---------- public code */

boolean rasterizer_transparent_geometry_initialize(
	void)
{
	boolean success = TRUE;

	transparent_geometry_groups = debug_malloc(
		RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS * sizeof(struct transparent_geometry_group),
		FALSE, "c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x29);
	transparent_geometry_group_sorted_indices = debug_malloc(
		RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS * sizeof(short),
		FALSE, "c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x2B);
	transparent_geometry_groups2 = debug_malloc(
		RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS2 * sizeof(struct transparent_geometry_group),
		FALSE, "c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x2E);

	transparent_geometry_group_count2 = 0;
	transparent_geometry_group_count = 0;

	if (!transparent_geometry_groups ||
		!transparent_geometry_group_sorted_indices ||
		!transparent_geometry_groups2)
	{
		error(_error_silent, "### ERROR failed to allocate transparent geometry buffer");
		success = FALSE;
	}

	success = success && rasterizer_transparent_geometry_initialize_aux_buffer();

	return success;
}

void rasterizer_transparent_geometry_begin(
	void)
{
	transparent_geometry_group_count = 0;
	transparent_geometry_next_group_sorted_index = 0;
	memset(transparent_geometry_group_pending_flags, 0,
		sizeof(transparent_geometry_group_pending_flags));
	transparent_geometry_group_count2 = 0;

	return;
}

void rasterizer_transparent_geometry_end(
	void)
{
	return;
}

short rasterizer_transparent_geometry_get_primary_vertex_type(
	struct transparent_geometry_group const *group)
{
	short vertex_type = NONE;

	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0xF4, group);

	if (group->vertex_buffer)
	{
		vertex_type = group->vertex_buffer->type;
	}
	else if (group->dynamic_vertex_buffer_index!=NONE)
	{
		vertex_type = rasterizer_dynamic_vertices_get_type(group->dynamic_vertex_buffer_index);
	}
	else
	{
		error(_error_silent, "### ERROR transparent geometry group has no vertices");
	}

	return vertex_type;
}

void rasterizer_transparent_geometry_set_group_pending_status(
	struct transparent_geometry_group const *group,
	boolean pending)
{
	short group_presorted_index = rasterizer_transparent_geometry_get_group_presorted_index(group);

	if (group_presorted_index!=NONE)
		BIT_VECTOR_SET_FLAG(transparent_geometry_group_pending_flags, group_presorted_index, !pending);

	return;
}

boolean rasterizer_transparent_geometry_get_group_pending_status(
	struct transparent_geometry_group const *group)
{
	short group_presorted_index = rasterizer_transparent_geometry_get_group_presorted_index(group);
	boolean pending = TRUE;

	if (group_presorted_index!=NONE)
		pending = !BIT_VECTOR_TEST_FLAG(transparent_geometry_group_pending_flags, group_presorted_index);

	return pending;
}

short rasterizer_transparent_geometry_get_group_presorted_index(
	struct transparent_geometry_group const *group)
{
	short group_presorted_index = NONE;

	if (group>=transparent_geometry_groups &&
		group<transparent_geometry_groups+transparent_geometry_group_count)
	{
		group_presorted_index = (short)(group-transparent_geometry_groups);

		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0xCB,
			group_presorted_index>=0 && group_presorted_index<transparent_geometry_group_count);
		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0xCC,
			((unsigned long)group-(unsigned long)transparent_geometry_groups)%sizeof(struct transparent_geometry_group)==0);
	}

	return group_presorted_index;
}

void *rasterizer_transparent_geometry_get_group_from_presorted_index(
	short group_presorted_index)
{
	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0xBC,
		group_presorted_index>=0 && group_presorted_index<transparent_geometry_group_count);

	return (byte *)transparent_geometry_groups + group_presorted_index*0xA0;
}

void *rasterizer_transparent_geometry_get_groups2(
	short *group_count)
{
	if (group_count)
		*group_count = (short)transparent_geometry_group_count2;

	return transparent_geometry_groups2;
}

struct transparent_geometry_group *rasterizer_transparent_geometry_next_group(
	struct transparent_geometry_group const *group)
{
	short next_group_sorted_index;
	short group_sorted_index;
	struct transparent_geometry_group *next_group = (struct transparent_geometry_group *)group;

	if (group)
	{
		group_sorted_index = (short)group->sorted_index;
		next_group_sorted_index = (short)(group_sorted_index + 1);

		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x89,
			group->sorted_index>=0 && group->sorted_index<transparent_geometry_group_count);

		if (next_group_sorted_index<transparent_geometry_group_count)
		{
			match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x8D,
				next_group_sorted_index>=0);

			next_group = transparent_geometry_groups +
				transparent_geometry_group_sorted_indices[next_group_sorted_index];
		}
		else
		{
			next_group = NULL;
		}
	}

	return next_group;
}

void *rasterizer_transparent_geometry_get_groups(
	void)
{
	return transparent_geometry_groups;
}

struct transparent_geometry_group *rasterizer_transparent_geometry_new_group(
	void)
{
	struct transparent_geometry_group *group = NULL;
	long group_index = transparent_geometry_group_count;

	if (group_index<RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS)
	{
		group = transparent_geometry_groups + group_index;
		group->sorted_index = group_index;
		transparent_geometry_group_count = group_index + 1;
		PS3_TRANSPARENT_ORIGIN(group_index); /* port: (the timer build) */
	}

	return group;
}

struct transparent_geometry_group *rasterizer_transparent_geometry_new_group2(
	void)
{
	struct transparent_geometry_group *group = NULL;
	long group_index = transparent_geometry_group_count2;

	if (group_index<RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS2)
	{
		group = transparent_geometry_groups2 + group_index;
		group->sorted_index = group_index;
		transparent_geometry_group_count2 = group_index + 1;
	}

	return group;
}

void rasterizer_transparent_geometry_stop(
	void)
{
	rasterizer_set_stencil_mode(0);

	return;
}

void rasterizer_transparent_geometry_dispose(
	void)
{
	rasterizer_transparent_geometry_dispose_aux_buffer();

	if (transparent_geometry_groups)
		debug_free(transparent_geometry_groups,
			"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x111);
	transparent_geometry_groups = NULL;

	if (transparent_geometry_group_sorted_indices)
		debug_free(transparent_geometry_group_sorted_indices,
			"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x114);
	transparent_geometry_group_sorted_indices = NULL;

	if (transparent_geometry_groups2)
		debug_free(transparent_geometry_groups2,
			"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c", 0x118);
	transparent_geometry_groups2 = NULL;

	transparent_geometry_group_count2 = 0;
	transparent_geometry_group_count = 0;

	return;
}

/* ---------- private code */

static void rasterizer_sort_internal(
	struct transparent_geometry_group *group)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c",
		0x192,
		group);

	return;
}

static int __cdecl group_sorted_indices_cmpfn(
	void const *group_index1_pointer,
	void const *group_index2_pointer)
{
	short const *group_index1 = group_index1_pointer;
	short const *group_index2 = group_index2_pointer;
	struct transparent_geometry_group *group1;
	struct transparent_geometry_group *group2;
	long comparison = 0;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c",
		0x1AA,
		group_index1 && (*group_index1)>=0 && (*group_index1)<transparent_geometry_group_count);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c",
		0x1AB,
		group_index2 && (*group_index2)>=0 && (*group_index2)<transparent_geometry_group_count);

	group1 = transparent_geometry_groups + *group_index1;
	group2 = transparent_geometry_groups + *group_index2;

	if (shader_is_water_decal(group1->shader))
	{
		comparison = -1;
		goto cortana_tiebreak;
	}
	if (shader_is_water_decal(group2->shader))
	{
		comparison = 1;
		goto cortana_tiebreak;
	}

	if (!group1->shader || group1->shader->base.type!=_shader_type_transparent_water)
	{
		if (group2->shader && group2->shader->base.type==_shader_type_transparent_water)
		{
			comparison = 1;
			goto cortana_tiebreak;
		}

		if ((group1->geometry_flags & FLAG(_rasterizer_geometry_first_person_bit)) &&
			!(group2->geometry_flags & FLAG(_rasterizer_geometry_first_person_bit)))
		{
			comparison = 1;
			goto cortana_tiebreak;
		}

		if (!(group2->geometry_flags & FLAG(_rasterizer_geometry_first_person_bit)) ||
			(group1->geometry_flags & FLAG(_rasterizer_geometry_first_person_bit)))
		{
			if (group1->z_sort>group2->z_sort)
			{
				comparison = 1;
				goto cortana_tiebreak;
			}
			if (!(group1->z_sort<group2->z_sort))
			{
				if (group1->source_object_index>group2->source_object_index)
				{
					comparison = 1;
					goto cortana_tiebreak;
				}
				if (group1->source_object_index>=group2->source_object_index)
				{
					goto cortana_tiebreak;
				}
			}
		}
	}
	comparison = -1;

	cortana_tiebreak:
	if (group1->cortana_hack && !group2->cortana_hack)
	{
		return 1;
	}
	if (group2->cortana_hack && !group1->cortana_hack)
	{
		return -1;
	}

	return comparison;
}

static void rasterizer_sort_external(
	void)
{
	short group_index;
#ifdef HALO_PS3
	uint64_t ps3_span;
#endif

	for (group_index = 0; group_index<transparent_geometry_group_count; group_index++)
	{
		rasterizer_sort_internal(transparent_geometry_groups + group_index);
		transparent_geometry_group_sorted_indices[group_index] = group_index;
	}

	PS3_TRANSPARENT_SPAN(ps3_span); /* port: (the timer build) */
	qsort(
		transparent_geometry_group_sorted_indices,
		transparent_geometry_group_count,
		sizeof(*transparent_geometry_group_sorted_indices),
		group_sorted_indices_cmpfn);
	PS3_TRANSPARENT_ADD(_ps3_timer_transparent_qsort, ps3_span); /* port: (the timer build) */

	/* port: move model packets next to their glass surface, leaving other packets on their original side of every
	surface. */
	PS3_TRANSPARENT_SPAN(ps3_span); /* port: (the timer build) */
	rasterizer_transparent_geometry_order_models(
		transparent_geometry_group_sorted_indices, transparent_geometry_group_count);
	PS3_TRANSPARENT_ADD(_ps3_timer_transparent_order_models, ps3_span); /* port: (the timer build) */

	for (group_index = 0; group_index<transparent_geometry_group_count; group_index++)
	{
		transparent_geometry_groups[transparent_geometry_group_sorted_indices[group_index]].sorted_index = group_index;
	}

	return;
}

void rasterizer_transparent_geometry_draw(
	boolean water)
{
	long profile = water
		? _rasterizer_profile_water
		: _rasterizer_profile_queued_transparents;
#ifdef HALO_PS3
	uint64_t ps3_span;
#endif

	rasterizer_profile_begin((short)profile);
	if (transparent_geometry_group_count > 0)
	{
		boolean first_person_flag = FALSE;
#ifdef HALO_PS3
		short ps3_half_first = NONE;
		short ps3_half_end = NONE;
		boolean ps3_half_on = FALSE;
#endif

		if (water)
		{
			PS3_TRANSPARENT_SPAN(ps3_span); /* port: (the timer build) */
			rasterizer_sort_external();
			PS3_TRANSPARENT_ADD(_ps3_timer_transparent_sort, ps3_span); /* port: (the timer build) */
			PS3_TRANSPARENT_COUNT(_ps3_timer_transparent_sort, transparent_geometry_group_count); /* port: (the timer build) */
			transparent_geometry_group_index = 0;
			if (global_window_parameters.window_index != NONE)
			{
				rasterizer_debug_options.transparent_pixel_counter_active = TRUE;
			}
		}

		PS3_TRANSPARENT_SPAN(ps3_span); /* port: (the timer build) */
		rasterizer_transparent_geometry_groups_begin();
		PS3_TRANSPARENT_ADD(_ps3_timer_transparent_begin_end, ps3_span); /* port: (the timer build) */
		rasterizer_debug_options.transparent_pixel_counter_active = FALSE;
#ifdef HALO_PS3
		/* port: display.half_resolution_particles' run of effects (d3d8_rsx.c draws them at half resolution) */
		if (!water)
			ps3_particles_half_run(&ps3_half_first, &ps3_half_end);
#endif
		while (transparent_geometry_group_index < transparent_geometry_group_count)
		{
			struct transparent_geometry_group *group =
				transparent_geometry_groups +
				transparent_geometry_group_sorted_indices[
					transparent_geometry_group_index];

#ifdef HALO_PS3
			/* port: its start, and its end (blended over the frame) before the first-person weapon's groups */
			if (transparent_geometry_group_index == ps3_half_first)
				ps3_half_on = d3d8_particles_half(TRUE) != 0;
			if (ps3_half_on && transparent_geometry_group_index == ps3_half_end)
			{
				d3d8_particles_half(FALSE);
				ps3_half_on = FALSE;
			}
#endif

			if (water)
			{
				struct shader *shader = group->shader;

				if (!shader ||
					(shader->base.type != _shader_type_transparent_water &&
						!shader_is_water_decal(shader)))
				{
					break;
				}
			}

			if (TEST_FLAG(group->geometry_flags, _rasterizer_geometry_first_person_bit))
			{
				match_assert(
					"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c",
					0x154,
					!water);
				match_assert(
					"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c",
					0x155,
					global_window_parameters.rasterizer_target==_rasterizer_target_render_primary);
				if (!first_person_flag)
				{
					/* port: the first-person weapon's projection (view_fov.c) */
					viewmodel_projection_begin();
					rasterizer_set_stencil_mode(0);
					rasterizer_set_frustum_z(
						rasterizer_globals.first_person_weapon_near_clip_distance,
						rasterizer_globals.first_person_weapon_far_clip_distance);
					first_person_flag = TRUE;
				}
			}
			else
			{
				match_assert(
					"c:\\halo\\SOURCE\\rasterizer\\rasterizer_transparent_geometry.c",
					0x163,
					!first_person_flag);
			}

			PS3_TRANSPARENT_STAMPS(group); /* port: (the timer build) */
			PS3_TRANSPARENT_SPAN(ps3_span); /* port: (the timer build) */
			rasterizer_transparent_geometry_group_draw(group, FALSE);
			PS3_TRANSPARENT_ADD(ps3_transparent_timer(group), ps3_span); /* port: (the timer build) */
			PS3_TRANSPARENT_COUNT(ps3_transparent_timer(group), 1); /* port: (the timer build) */
			transparent_geometry_group_index++;
		}
#ifdef HALO_PS3
		if (ps3_half_on)
			d3d8_particles_half(FALSE); /* port: (the run's end) */
#endif
		PS3_TRANSPARENT_STAMPS_END(); /* port: (the timer build) */

		if (!water && global_window_parameters.window_index != NONE)
		{
			rasterizer_debug_options.transparent_pixel_counter_active = TRUE;
		}
		PS3_TRANSPARENT_SPAN(ps3_span); /* port: (the timer build) */
		rasterizer_transparent_geometry_groups_end();
		PS3_TRANSPARENT_ADD(_ps3_timer_transparent_begin_end, ps3_span); /* port: (the timer build) */
		rasterizer_debug_options.transparent_pixel_counter_active = FALSE;
		if (first_person_flag)
		{
			viewmodel_projection_end();	/* port: (view_fov.c) */
			rasterizer_set_frustum_z(0.0f, 0.0f);
		}
	}
	rasterizer_profile_end((short)profile);

	return;
}

#ifdef HALO_PS3
/* port: (the timer build) a group's timer by its shader's type: screen, effect, decal, environment, model, generic,
chicago, water, glass, meter, plasma */
static int ps3_transparent_timer(
	struct transparent_geometry_group const *group)
{
	static const unsigned char timers[] = { 8, 0, 8, 8, 1, 2, 3, 4, 5, 6, 7 };
	short type = group->shader ? group->shader->base.type : NONE;

	return _ps3_timer_transparent_effect + (type >= 0 && type < (short)sizeof(timers) ? timers[type] :
		_ps3_timer_transparent_other - _ps3_timer_transparent_effect);
}

/* port: (the timer build) while the effects pass's draws are stamped, a group's draws in its maker's part, else in its
shader type's */
static void ps3_transparent_stamps(
	struct transparent_geometry_group const *group)
{
	int part = ps3_group_origins[group - transparent_geometry_groups];

	if (ps3_draw_stamps_part < _ps3_stamp_effects_rest)
		return;
	if (!part)
		part = _ps3_stamp_effect + ps3_transparent_timer(group) - _ps3_timer_transparent_effect;
	if (part == ps3_draw_stamps_part)
		return;
	if (ps3_stamps_part_before < 0)
		ps3_stamps_part_before = ps3_draw_stamps_part;
	ps3_timers_draw_stamps(part);
}

static void ps3_transparent_stamps_end(
	void)
{
	if (ps3_stamps_part_before >= 0)
		ps3_timers_draw_stamps(ps3_stamps_part_before);
	ps3_stamps_part_before = -1;
}

/* port: an effect whose blend d3d8_rsx.c's half target takes: alpha blend, add, alpha multiply add */
static boolean ps3_particles_half_group(
	struct transparent_geometry_group const *group)
{
	enum
	{
		_shader_type_effect = 1
	};
	short blend;

	if (!group->shader || group->shader->base.type != _shader_type_effect ||
		TEST_FLAG(group->geometry_flags, _rasterizer_geometry_first_person_bit))
	{
		return FALSE;
	}
	blend = ((struct shader_effect_definition const *)group->shader)->framebuffer_blend_function;

	return blend == 0 || blend == 3 || blend == 7;
}

/* port: the groups left from the first-person weapon's on (*end), and before them the run of effects after every
other group (*first, NONE for none) */
static void ps3_particles_half_run(
	short *first,
	short *end)
{
	short index;

	*first = NONE;
	*end = (short)transparent_geometry_group_count;
	if (!config_boolean("display.half_resolution_particles"))
		return;
	for (index = transparent_geometry_group_index; index < transparent_geometry_group_count; index++)
	{
		if (TEST_FLAG(transparent_geometry_groups[transparent_geometry_group_sorted_indices[index]].geometry_flags,
			_rasterizer_geometry_first_person_bit))
		{
			*end = index;
			break;
		}
	}
	for (index = *end; index > transparent_geometry_group_index &&
		ps3_particles_half_group(transparent_geometry_groups + transparent_geometry_group_sorted_indices[index - 1]);
		index--)
	{
	}
	if (index < *end)
		*first = index;
}
#endif
