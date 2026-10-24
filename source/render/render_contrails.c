/* RENDER_CONTRAILS.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/render/render_contrails.c. */

/* ---------- headers */

#include "cseries.h"
#include "cache/texture_cache.h"
#include "effects/contrail_definitions.h"
#include "effects/contrails.h"
#include "bitmaps/bitmap_group.h"
#include "cseries/errors.h"
#include "interface/hud_draw.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "math/periodic_functions.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render.h"
#include "tag_files/tag_files.h"

/* ---------- constants */

enum
{
	_contrail_render_mode_media_mapped = 1 << 2,
	_contrail_render_mode_ground_mapped = 1 << 3,
	_contrail_render_mode_normal = ~(_contrail_render_mode_media_mapped | _contrail_render_mode_ground_mapped),
};

enum
{
	_contrail_point_transitioning_bit = 1,
};

/* ---------- macros */

/* ---------- structures */

struct contrail_vertex
{
	real_point3d point;
	real_point2d texture;
	pixel32 color;
};

typedef char verify_contrail_vertex_size[
	sizeof(struct contrail_vertex) == 0x18 ? 1 : -1];

/* ---------- prototypes */

static void render_contrails(
	unsigned long render_mode_flags);
static void render_contrail(
	struct contrail_datum *contrail,
	struct contrail_definition *definition,
	short instance_index);

real contrail_fade(
	struct contrail_definition *definition,
	short fade_mode,
	real_point3d const *world_point,
	real_vector3d const *world_normal);

/* ---------- globals */

extern boolean render_contrails_enabled;

/* port: said once */
static boolean reported_bad_change_color = FALSE;

/* ---------- public code */

real contrail_fade(
	struct contrail_definition *definition,
	short fade_mode,
	real_point3d const *world_point,
	real_vector3d const *world_normal)
{
	real result = 1.f;

	if (fade_mode)
	{
		real_vector3d to_camera;

		vector_from_points3d(world_point, &render.camera.position, &to_camera);
		result = fabs(dot_product3d(world_normal, &to_camera) / magnitude3d(&to_camera));

		if (definition->flags & (1 << _contrail_fades_slowly_bit))
			result = transition_function_evaluate(2, result);
		if (fade_mode == 2)
			result = 1.f - result;
	}

	return result;
}

static void render_contrail(
	struct contrail_datum *contrail,
	struct contrail_definition *definition,
	short instance_index)
{
	struct bitmap_data *bitmap;
	short segment_count;
	short triangle_count;
	short vertex_count;
	long triangle_buffer_index;
	long vertex_buffer_index;

	bitmap = bitmap_group_get_bitmap_from_sequence(
		definition->bitmap.index,
		contrail->sequence_index,
		contrail->frame_index);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_contrail;
	if (_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE))
	{
		segment_count = contrail->contrail_point_counts[instance_index] - 1;
		triangle_count = segment_count + segment_count;
		vertex_count = triangle_count + 2;
		triangle_buffer_index = rasterizer_dynamic_triangles_new(triangle_count);
		vertex_buffer_index = rasterizer_dynamic_vertices_new(6, vertex_count);
		if (triangle_buffer_index != NONE && vertex_buffer_index != NONE)
		{
			short *triangles;
			struct contrail_vertex *vertices;
			struct contrail_vertex *first_vertex;
			struct contrail_shader *shader;
			boolean apply_orientation;
			real texture_u_step;
			real texture_offset_v;
			real texture_v_far;
			real_point3d average_position;
			struct contrail_point_datum *previous_point;
			real texture_u;
			long point_index;

			triangles = rasterizer_dynamic_triangles_lock(triangle_buffer_index);
			vertices = rasterizer_dynamic_vertices_lock(vertex_buffer_index);
			average_position = *global_origin3d;
			shader = &definition->shader;
			apply_orientation = shader->framebuffer_fade_mode != 0;
			match_assert(
				"c:\\halo\\SOURCE\\render\\render_contrails.c",
				136,
				triangles && vertices);

			texture_u = contrail->texture_offset_u;
			texture_u_step = definition->texture_repeats_u;
			if (definition->scale_flags & (1 << _contrail_texture_repeats_u_bit))
				texture_u_step *= contrail->density;
			texture_u_step = -texture_u_step;
			texture_offset_v = contrail->texture_offset_v;
			texture_v_far = definition->texture_repeats_v;
			if (definition->scale_flags & (1 << _contrail_texture_repeats_v_bit))
				texture_v_far *= contrail->density;
			texture_v_far += texture_offset_v;
			previous_point = NULL;
			point_index = contrail->first_contrail_point_indices[instance_index];

			while (point_index != NONE)
			{
				struct contrail_point_datum *point;
				struct contrail_point_state *state;
				real color_scale;
				real width;
				real_argb_color color;
				real half_width;
				real_vector3d orientation;
				real_vector3d facing_normal;

				point = datum_get(contrail_point_data, point_index);
				state = TAG_BLOCK_GET_ELEMENT(
					&definition->states,
					point->state_index,
					struct contrail_point_state);
				color_scale = 1.f;
				if (state->scale_flags & (1 << _contrail_state_color_bit))
					color_scale = point->density;
				width = state->width;
				if (state->scale_flags & (1 << _contrail_state_width_bit))
					width *= point->density;
				half_width = width;
				color.alpha = (state->color_upper_bound.alpha - state->color_lower_bound.alpha) * color_scale + state->color_lower_bound.alpha;
				color.red = (state->color_upper_bound.red - state->color_lower_bound.red) * color_scale + state->color_lower_bound.red;
				color.green = (state->color_upper_bound.green - state->color_lower_bound.green) * color_scale + state->color_lower_bound.green;
				color.blue = (state->color_upper_bound.blue - state->color_lower_bound.blue) * color_scale + state->color_lower_bound.blue;

				if (point->flags & (1 << _contrail_point_transitioning_bit))
				{
					struct contrail_point_state *next_state;
					real transition;
					real next_color_scale;
					real next_width;
					real_argb_color next_color;

					next_state = TAG_BLOCK_GET_ELEMENT(
						&definition->states,
						point->state_index + 1,
						struct contrail_point_state);
					transition = point->time;
					next_color_scale = 1.f;
					if (next_state->scale_flags & (1 << _contrail_state_color_bit))
						next_color_scale = point->density;
					next_width = next_state->width;
					if (next_state->scale_flags & (1 << _contrail_state_width_bit))
						next_width *= point->density;
					next_color.alpha = (next_state->color_upper_bound.alpha - next_state->color_lower_bound.alpha) * next_color_scale + next_state->color_lower_bound.alpha;
					next_color.red = (next_state->color_upper_bound.red - next_state->color_lower_bound.red) * next_color_scale + next_state->color_lower_bound.red;
					next_color.green = (next_state->color_upper_bound.green - next_state->color_lower_bound.green) * next_color_scale + next_state->color_lower_bound.green;
					next_color.blue = (next_state->color_upper_bound.blue - next_state->color_lower_bound.blue) * next_color_scale + next_state->color_lower_bound.blue;
					half_width = (next_width - width) * transition + width;
					color.alpha = (next_color.alpha - color.alpha) * transition + color.alpha;
					color.red = (next_color.red - color.red) * transition + color.red;
					color.green = (next_color.green - color.green) * transition + color.green;
					color.blue = (next_color.blue - color.blue) * transition + color.blue;
				}

				if (contrail->object_index != NONE)
				{
					struct object_datum *object = object_get(contrail->object_index);
					struct object_definition *object_definition = object_definition_get(object->definition_index);
					struct object_attachment_definition *attachment = TAG_BLOCK_GET_ELEMENT(
						&object_definition->object.attachments,
						contrail->attachment_index,
						struct object_attachment_definition);
					short change_color_index = attachment->change_color_reference - 1;

					/* port: and one the object has (a map's index); said once */
					if (change_color_index != NONE &&
						VALID_INDEX(change_color_index, NUMBER_OF_OBJECT_CHANGE_COLORS))
					{
						real_rgb_color const *change_color = &object->object.outgoing_change_colors[change_color_index];
						color.red *= change_color->red;
						color.green *= change_color->green;
						color.blue *= change_color->blue;
					}
					else if (change_color_index != NONE && !reported_bad_change_color)
					{
						error(_error_silent, "### ERROR a contrail attachment has a bad change color index; it is left out");
						reported_bad_change_color = TRUE;
					}
				}

				half_width *= 0.5f;
				vertices[0].texture.x = texture_u;
				vertices[0].texture.y = texture_v_far;
				vertices[1].texture.x = texture_u;
				vertices[1].texture.y = texture_offset_v;
				average_position.x += point->position.x;
				average_position.y += point->position.y;
				average_position.z += point->position.z;

				switch (definition->render_type)
				{
				case _contrail_render_type_vertical:
					vertices[0].point.x = point->position.x;
					vertices[0].point.y = point->position.y;
					vertices[0].point.z = point->position.z - half_width;
					vertices[1].point.x = point->position.x;
					vertices[1].point.y = point->position.y;
					vertices[1].point.z = point->position.z + half_width;
					if (apply_orientation)
					{
						struct contrail_point_datum *next_point;
						if (previous_point)
						{
							orientation.i = previous_point->position.y - point->position.y;
							orientation.j = point->position.x - previous_point->position.x;
						}
						else
						{
							next_point = datum_get(contrail_point_data, point->next_contrail_point_index);
							orientation.i = point->position.y - next_point->position.y;
							orientation.j = next_point->position.x - point->position.x;
						}
						orientation.k = 0.f;
						normalize3d(&orientation);
					}
					break;

				case _contrail_render_type_horizontal:
				case _contrail_render_type_media:
				{
					real_vector2d perpendicular;
					struct contrail_point_datum *next_point;
					if (previous_point)
					{
						perpendicular.i = previous_point->position.y - point->position.y;
						perpendicular.j = point->position.x - previous_point->position.x;
					}
					else
					{
						next_point = datum_get(contrail_point_data, point->next_contrail_point_index);
						perpendicular.i = point->position.y - next_point->position.y;
						perpendicular.j = next_point->position.x - point->position.x;
					}
					normalize2d(&perpendicular);
					vertices[0].point.x = point->position.x - perpendicular.i * half_width;
					vertices[0].point.y = point->position.y - perpendicular.j * half_width;
					vertices[0].point.z = point->position.z;
					vertices[1].point.x = point->position.x + perpendicular.i * half_width;
					vertices[1].point.y = point->position.y + perpendicular.j * half_width;
					vertices[1].point.z = point->position.z;
					if (apply_orientation)
						orientation = *global_up3d;
					break;
				}

				case _contrail_render_type_viewer:
				{
					real_vector3d tangent;
					real eye_i;
					real eye_j;
					real eye_k;
					struct contrail_point_datum *next_point;
					if (previous_point)
					{
						eye_i = render.camera.position.x - previous_point->position.x;
						eye_j = render.camera.position.y - previous_point->position.y;
						eye_k = render.camera.position.z - previous_point->position.z;
						tangent.i = point->position.x - previous_point->position.x;
						tangent.j = point->position.y - previous_point->position.y;
						tangent.k = point->position.z - previous_point->position.z;
					}
					else
					{
						next_point = datum_get(contrail_point_data, point->next_contrail_point_index);
						eye_i = render.camera.position.x - point->position.x;
						eye_j = render.camera.position.y - point->position.y;
						eye_k = render.camera.position.z - point->position.z;
						tangent.i = next_point->position.x - point->position.x;
						tangent.j = next_point->position.y - point->position.y;
						tangent.k = next_point->position.z - point->position.z;
					}
					facing_normal.i = tangent.k * eye_j - tangent.j * eye_k;
					facing_normal.j = tangent.i * eye_k - tangent.k * eye_i;
					facing_normal.k = tangent.j * eye_i - tangent.i * eye_j;
					normalize3d(&facing_normal);
					vertices[0].point.x = point->position.x - facing_normal.i * half_width;
					vertices[0].point.y = point->position.y - facing_normal.j * half_width;
					vertices[0].point.z = point->position.z - facing_normal.k * half_width;
					vertices[1].point.x = point->position.x + facing_normal.i * half_width;
					vertices[1].point.y = point->position.y + facing_normal.j * half_width;
					vertices[1].point.z = point->position.z + facing_normal.k * half_width;
					if (apply_orientation)
					{
						orientation.i = facing_normal.k * tangent.j - facing_normal.j * tangent.k;
						orientation.j = facing_normal.i * tangent.k - facing_normal.k * tangent.i;
						orientation.k = facing_normal.j * tangent.i - facing_normal.i * tangent.j;
						normalize3d(&orientation);
					}
					break;
				}

				case _contrail_render_type_ground:
				default:
					error(
						_error_silent,
						"contrail %s uses an unsupported render type.",
						tag_get_name(contrail->definition_index));
					/* port: the Xbox's bug fixed: this return left the buffers
					locked and allocated, and current_lock_operation set */
					rasterizer_dynamic_triangles_unlock(triangle_buffer_index);
					rasterizer_dynamic_vertices_unlock(vertex_buffer_index);
					rasterizer_dynamic_triangles_delete(triangle_buffer_index);
					rasterizer_dynamic_vertices_delete(vertex_buffer_index);
					rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
					return;
				}

				color.alpha *= contrail_fade(
					definition,
					shader->framebuffer_fade_mode,
					&point->position,
					&orientation);
				color.alpha = PIN(color.alpha, 0.f, 1.f);
				vertices[0].color = vertices[1].color = real_argb_color_to_pixel32(&color);
				texture_u += texture_u_step;
				previous_point = point;
				vertices += 2;
				point_index = point->next_contrail_point_index;
			}

			vertices -= vertex_count;
			first_vertex = vertices;
			if (!(definition->flags & (1 << _contrail_first_point_unfaded_bit)))
			{
				first_vertex[0].color &= 0x00FFFFFF;
				first_vertex[1].color &= 0x00FFFFFF;
			}
			if (!(definition->flags & (1 << _contrail_last_point_unfaded_bit)))
			{
				vertices[vertex_count - 1].color &= 0x00FFFFFF;
				vertices[vertex_count - 2].color &= 0x00FFFFFF;
			}
			if (segment_count > 0)
			{
				short segment_index;
				for (segment_index = 0; segment_index < segment_count; segment_index++)
				{
					triangles[0] = 2 * segment_index;
					triangles[1] = 2 * segment_index + 1;
					triangles[2] = 2 * segment_index + 2;
					triangles += 3;
					triangles[0] = 2 * segment_index + 2;
					triangles[1] = 2 * segment_index + 1;
					triangles[2] = 2 * segment_index + 3;
					triangles += 3;
				}
			}
			{
				real one_over_point_count = 1.f / contrail->contrail_point_counts[instance_index];
				average_position.x *= one_over_point_count;
				average_position.y *= one_over_point_count;
				average_position.z *= one_over_point_count;
			}
			rasterizer_dynamic_triangles_unlock(triangle_buffer_index);
			rasterizer_dynamic_vertices_unlock(vertex_buffer_index);
			rasterizer_dynamic_unlit_geometry_draw(
				(struct shader const *)shader,
				bitmap,
				0,
				triangle_buffer_index,
				vertex_buffer_index,
				triangle_count,
				&average_position,
				0);
			rasterizer_dynamic_triangles_delete(triangle_buffer_index);
			rasterizer_dynamic_vertices_delete(vertex_buffer_index);
		}
	}
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;

	return;
}

static void render_contrails(
	unsigned long render_mode_flags)
{
	if (render_contrails_enabled)
	{
		long contrail_index;

		for (contrail_index = data_next_index(contrail_data, NONE);
			contrail_index != NONE;
			contrail_index = data_next_index(contrail_data, contrail_index))
		{
			short instance_index;
			struct contrail_datum *contrail = datum_get(contrail_data, contrail_index);
			struct contrail_definition *definition = contrail_definition_get(
				contrail->definition_index);

			for (instance_index = 0; instance_index < 4; instance_index++)
			{
				if (((1 << definition->render_type) & render_mode_flags) &&
					contrail->contrail_point_counts[instance_index] >= 2)
				{
					render_contrail(contrail, definition, instance_index);
				}
			}
		}
	}

	return;
}

#ifdef HALO_PS3
#include "ps3_spu_game.h" /* port: port/ps3/src/ps3_spu.c */
#include "scenario/scenario.h" /* port: scenario_structure_bsp_generation */

enum
{
	CONTRAILS_SPU_INSTANCES = 512,
	CONTRAILS_SPU_POINTS = 4096,
	/* an instance render_contrail draws nothing of (no bitmap format, no buffers) */
	CONTRAILS_SPU_NOTHING = -2,
};

/* (the SPU reads a state's width, its two colours and its flags in a row) */
typedef char contrails_spu_state_layout_check[
	offsetof(struct contrail_point_state, scale_flags) == offsetof(struct contrail_point_state, width) + 36 ? 1 : -1];

/* port: the window's normal contrails in render_contrails' order: each strip built on the render SPU in buffers locked
at the gather, or by render_contrail (a type or size the SPU does not take); render_contrails_normal draws them */
static struct
{
	spu_game_contrails job;
	spu_game_answer answer;
	spu_game_contrail contrails[CONTRAILS_SPU_INSTANCES];
	spu_game_contrail_point points[CONTRAILS_SPU_POINTS];
	struct
	{
		long contrail_index;
		short instance_index;
		/* its job entry, NONE (render_contrail's) or CONTRAILS_SPU_NOTHING */
		short entry;
		struct bitmap_data *bitmap;
		long triangle_buffer_index;
		long vertex_buffer_index;
		short triangle_count;
		real_point3d average_position;
	} instances[CONTRAILS_SPU_INSTANCES];
	short instance_count;
	short entry_count;
	uint32_t ticket;
	boolean begun;
} contrails_spu __attribute__((aligned(128)));

/* port: an instance's points for the SPU, with their positions' average: FALSE when its list is not its count or a
point's state is not the definition's */
static boolean render_contrails_spu_points(
	struct contrail_datum *contrail,
	struct contrail_definition *definition,
	short instance_index,
	spu_game_contrail_point *points,
	real_point3d *average_position)
{
	short point_count = contrail->contrail_point_counts[instance_index];
	long point_index = contrail->first_contrail_point_indices[instance_index];
	short count = 0;

	*average_position = *global_origin3d;
	while (point_index != NONE)
	{
		struct contrail_point_datum *point = datum_get(contrail_point_data, point_index);
		long last_state = point->state_index + ((point->flags & (1 << _contrail_point_transitioning_bit)) ? 1 : 0);

		if (count >= point_count || point->state_index < 0 || last_state >= definition->states.count)
		{
			return FALSE;
		}
		points[count].position[0] = point->position.x;
		points[count].position[1] = point->position.y;
		points[count].position[2] = point->position.z;
		points[count].density = point->density;
		points[count].time = point->time;
		points[count].state_index = point->state_index;
		points[count].flags = (point->flags & (1 << _contrail_point_transitioning_bit)) ?
			SPU_GAME_CONTRAIL_TRANSITIONING : 0;
		points[count].pad = 0;
		average_position->x += point->position.x;
		average_position->y += point->position.y;
		average_position->z += point->position.z;
		count++;
		point_index = point->next_contrail_point_index;
	}
	if (count != point_count)
	{
		return FALSE;
	}
	{
		real one_over_point_count = 1.f / point_count;

		average_position->x *= one_over_point_count;
		average_position->y *= one_over_point_count;
		average_position->z *= one_over_point_count;
	}

	return TRUE;
}

/* port: an instance's entry of the SPU's job, as render_contrail starts it (its bitmap, buffers and values); the entry
NONE when the SPU does not take it */
static void render_contrails_spu_instance(
	struct contrail_datum *contrail,
	struct contrail_definition *definition,
	short instance_index,
	short *point_total)
{
	short instance = contrails_spu.instance_count++;
	short point_count = contrail->contrail_point_counts[instance_index];
	spu_game_contrail *entry = &contrails_spu.contrails[contrails_spu.entry_count];
	spu_game_contrail_point *points = &contrails_spu.points[*point_total];
	struct bitmap_data *bitmap;

	contrails_spu.instances[instance].entry = NONE;
	if ((definition->render_type != _contrail_render_type_vertical &&
		definition->render_type != _contrail_render_type_horizontal &&
		definition->render_type != _contrail_render_type_viewer) ||
		point_count > SPU_GAME_CONTRAIL_POINTS || *point_total + point_count > CONTRAILS_SPU_POINTS ||
		!render_contrails_spu_points(contrail, definition, instance_index, points,
			&contrails_spu.instances[instance].average_position))
	{
		return;
	}
	contrails_spu.instances[instance].entry = CONTRAILS_SPU_NOTHING;
	bitmap = bitmap_group_get_bitmap_from_sequence(
		definition->bitmap.index,
		contrail->sequence_index,
		contrail->frame_index);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_contrail;
	if (_texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE))
	{
		short segment_count = point_count - 1;
		short triangle_count = segment_count + segment_count;
		long triangle_buffer_index = rasterizer_dynamic_triangles_new(triangle_count);
		long vertex_buffer_index = rasterizer_dynamic_vertices_new(6, triangle_count + 2);

		if (triangle_buffer_index != NONE && vertex_buffer_index != NONE)
		{
			short *triangles = rasterizer_dynamic_triangles_lock(triangle_buffer_index);
			void *vertices = rasterizer_dynamic_vertices_lock(vertex_buffer_index);
			struct contrail_shader *shader = &definition->shader;

			memset(entry, 0, sizeof(*entry));
			entry->points = PS3_SPU_EA(points);
			entry->point_count = point_count;
			entry->vertices = PS3_SPU_EA(vertices);
			entry->triangles = PS3_SPU_EA(triangles);
			entry->states = PS3_SPU_EA(definition->states.address);
			entry->state_count = definition->states.count;
			entry->state_size = sizeof(struct contrail_point_state);
			entry->width_offset = offsetof(struct contrail_point_state, width);
			entry->color_mask = 1 << _contrail_state_color_bit;
			entry->width_mask = 1 << _contrail_state_width_bit;
			entry->render_type = definition->render_type;
			entry->fade_mode = shader->framebuffer_fade_mode;
			entry->flags =
				((definition->flags & (1 << _contrail_fades_slowly_bit)) ? SPU_GAME_CONTRAIL_FADES_SLOWLY : 0) |
				((definition->flags & (1 << _contrail_first_point_unfaded_bit)) ? SPU_GAME_CONTRAIL_FIRST_UNFADED : 0) |
				((definition->flags & (1 << _contrail_last_point_unfaded_bit)) ? SPU_GAME_CONTRAIL_LAST_UNFADED : 0);
			if (contrail->object_index != NONE)
			{
				struct object_datum *object = object_get(contrail->object_index);
				struct object_definition *object_definition = object_definition_get(object->definition_index);
				struct object_attachment_definition *attachment = TAG_BLOCK_GET_ELEMENT(
					&object_definition->object.attachments,
					contrail->attachment_index,
					struct object_attachment_definition);
				short change_color_index = attachment->change_color_reference - 1;

				if (change_color_index != NONE &&
					VALID_INDEX(change_color_index, NUMBER_OF_OBJECT_CHANGE_COLORS))
				{
					real_rgb_color const *change_color = &object->object.outgoing_change_colors[change_color_index];

					entry->tinted = TRUE;
					entry->tint[0] = change_color->red;
					entry->tint[1] = change_color->green;
					entry->tint[2] = change_color->blue;
				}
				else if (change_color_index != NONE && !reported_bad_change_color)
				{
					error(_error_silent, "### ERROR a contrail attachment has a bad change color index; it is left out");
					reported_bad_change_color = TRUE;
				}
			}
			entry->texture_u = contrail->texture_offset_u;
			entry->texture_u_step = definition->texture_repeats_u;
			if (definition->scale_flags & (1 << _contrail_texture_repeats_u_bit))
				entry->texture_u_step *= contrail->density;
			entry->texture_u_step = -entry->texture_u_step;
			entry->texture_offset_v = contrail->texture_offset_v;
			entry->texture_v_far = definition->texture_repeats_v;
			if (definition->scale_flags & (1 << _contrail_texture_repeats_v_bit))
				entry->texture_v_far *= contrail->density;
			entry->texture_v_far += entry->texture_offset_v;
			contrails_spu.instances[instance].entry = contrails_spu.entry_count++;
			contrails_spu.instances[instance].bitmap = bitmap;
			contrails_spu.instances[instance].triangle_buffer_index = triangle_buffer_index;
			contrails_spu.instances[instance].vertex_buffer_index = vertex_buffer_index;
			contrails_spu.instances[instance].triangle_count = triangle_count;
			*point_total += point_count;
		}
	}
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;

	return;
}

void render_contrails_spu_begin(
	void)
{
	short point_total = 0;
	long instance_count = 0;
	long contrail_index;

	contrails_spu.begun = FALSE;
	if (!render_contrails_enabled || !ps3_spu_render_running())
	{
		return;
	}
	/* (all of the window's instances in the list, or none: render_contrails_normal draws them in its order) */
	for (contrail_index = data_next_index(contrail_data, NONE);
		contrail_index != NONE;
		contrail_index = data_next_index(contrail_data, contrail_index))
	{
		struct contrail_datum *contrail = datum_get(contrail_data, contrail_index);
		struct contrail_definition *definition = contrail_definition_get(contrail->definition_index);
		short instance_index;

		for (instance_index = 0; instance_index < 4; instance_index++)
		{
			if (((1 << definition->render_type) & _contrail_render_mode_normal) &&
				contrail->contrail_point_counts[instance_index] >= 2)
			{
				instance_count++;
			}
		}
	}
	if (instance_count > CONTRAILS_SPU_INSTANCES)
	{
		return;
	}
	contrails_spu.instance_count = 0;
	contrails_spu.entry_count = 0;
	for (contrail_index = data_next_index(contrail_data, NONE);
		contrail_index != NONE;
		contrail_index = data_next_index(contrail_data, contrail_index))
	{
		struct contrail_datum *contrail = datum_get(contrail_data, contrail_index);
		struct contrail_definition *definition = contrail_definition_get(contrail->definition_index);
		short instance_index;

		for (instance_index = 0; instance_index < 4; instance_index++)
		{
			if (((1 << definition->render_type) & _contrail_render_mode_normal) &&
				contrail->contrail_point_counts[instance_index] >= 2)
			{
				contrails_spu.instances[contrails_spu.instance_count].contrail_index = contrail_index;
				contrails_spu.instances[contrails_spu.instance_count].instance_index = instance_index;
				render_contrails_spu_instance(contrail, definition, instance_index, &point_total);
			}
		}
	}
	contrails_spu.begun = TRUE;
	contrails_spu.ticket = 0;
	if (contrails_spu.entry_count)
	{
		long table_size;
		byte const *table = transition_function_table(2, &table_size);

		contrails_spu.job.contrails = PS3_SPU_EA(contrails_spu.contrails);
		contrails_spu.job.contrail_count = contrails_spu.entry_count;
		contrails_spu.job.transition_table = table ? PS3_SPU_EA(table) : 0;
		contrails_spu.job.transition_size = table_size;
		contrails_spu.job.camera[0] = render.camera.position.x;
		contrails_spu.job.camera[1] = render.camera.position.y;
		contrails_spu.job.camera[2] = render.camera.position.z;
		contrails_spu.job.answer = PS3_SPU_EA(&contrails_spu.answer);
		contrails_spu.job.generation = (uint32_t)scenario_structure_bsp_generation;
		contrails_spu.answer.status = 0;
		contrails_spu.ticket = ps3_spu_render_submit(SPU_GAME_CONTRAILS, sizeof(contrails_spu.job),
			PS3_SPU_EA(&contrails_spu.job), 0);
	}

	return;
}

/* port: the gathered instances drawn in order, the SPU's strips as render_contrail draws its own; FALSE when no
gather was made for the window */
static boolean render_contrails_spu_end(
	void)
{
	boolean done;
	short instance;

	if (!contrails_spu.begun)
	{
		return FALSE;
	}
	contrails_spu.begun = FALSE;
	done = contrails_spu.entry_count && ps3_spu_render_wait(contrails_spu.ticket) &&
		contrails_spu.answer.status == SPU_GAME_DONE;
	contrails_spu.ticket = 0;
	for (instance = 0; instance < contrails_spu.instance_count; instance++)
	{
		struct contrail_datum *contrail = datum_get(contrail_data, contrails_spu.instances[instance].contrail_index);
		struct contrail_definition *definition = contrail_definition_get(contrail->definition_index);
		short entry = contrails_spu.instances[instance].entry;

		if (entry == CONTRAILS_SPU_NOTHING)
		{
			continue;
		}
		if (entry == NONE || !done || contrails_spu.contrails[entry].status != SPU_GAME_DONE)
		{
			render_contrail(contrail, definition, contrails_spu.instances[instance].instance_index);
			continue;
		}
		rasterizer_globals.current_lock_operation = _rasterizer_lock_contrail;
		rasterizer_dynamic_triangles_unlock(contrails_spu.instances[instance].triangle_buffer_index);
		rasterizer_dynamic_vertices_unlock(contrails_spu.instances[instance].vertex_buffer_index);
		rasterizer_dynamic_unlit_geometry_draw(
			(struct shader const *)&definition->shader,
			contrails_spu.instances[instance].bitmap,
			0,
			contrails_spu.instances[instance].triangle_buffer_index,
			contrails_spu.instances[instance].vertex_buffer_index,
			contrails_spu.instances[instance].triangle_count,
			&contrails_spu.instances[instance].average_position,
			0);
		rasterizer_dynamic_triangles_delete(contrails_spu.instances[instance].triangle_buffer_index);
		rasterizer_dynamic_vertices_delete(contrails_spu.instances[instance].vertex_buffer_index);
		rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
	}

	return TRUE;
}
#endif

void render_contrails_ground_mapped(
	void)
{
	render_contrails(_contrail_render_mode_ground_mapped);
	return;
}

void render_contrails_media_mapped(
	void)
{
	render_contrails(_contrail_render_mode_media_mapped);
	return;
}

void render_contrails_normal(
	void)
{
#ifdef HALO_PS3
	/* port: the window's strips from the render SPU (render_contrails_spu_begin) */
	if (render_contrails_spu_end())
	{
		return;
	}
#endif
	render_contrails(_contrail_render_mode_normal);
	return;
}

/* ---------- private code */
