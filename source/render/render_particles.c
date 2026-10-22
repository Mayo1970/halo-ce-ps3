/* RENDER_PARTICLES.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/render/render_particles.c. */

/* ---------- headers */

#include <stdlib.h>

#include "camera/director.h"
#include "cseries/cseries.h"
#include "cseries/profile.h"
#include "effects/particles.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "render_sprite.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "view_fov.h" /* port: port/linux/game/view_fov.c */

/* ---------- constants */

enum
{
	/* render_particles lists every visible particle here without a bound check: the size of the native builds'
	particle pool (effects/particles.c, halo_port_capacity.h) */
	MAXIMUM_RENDERED_PARTICLES = HALO_PORT_MAXIMUM_PARTICLES,
	MAXIMUM_RENDERED_PARTICLE_GROUPS = 512,
};

/* ---------- macros */

/* ---------- structures */

struct rendered_particle_datum
{
	short particle_index;
	short definition_index;
	short cluster_index;
	boolean attached_to_first_person_weapon;
	byte pad;
};

typedef char rendered_particle_size_assert[
	sizeof(struct rendered_particle_datum) == 8 ? 1 : -1];

/* ---------- prototypes */

/* ---------- globals */

static struct profile_section render_particles_section =
{
	"render_particles",
	NONE,
	TRUE,
};

/* ---------- public code */

boolean local_player_is_first_person(
	short local_player_index)
{
	boolean result =
		director_get_perspective(local_player_index) ==
		_director_perspective_first_person;

	if (!result)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_get(player_index);

		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);

			if (unit->object.parent_object_index != NONE &&
				unit->unit.parent_seat_index != NONE)
			{
				struct unit_datum *parent_unit =
					unit_get(unit->object.parent_object_index);
				struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
					&unit_definition_get(parent_unit->definition_index)->unit.seats,
					unit->unit.parent_seat_index,
					struct unit_seat);

				if (TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
					result = TRUE;
			}
		}
	}

	return result;
}

static int __cdecl compare_rendered_particles(
	void const *left_pointer,
	void const *right_pointer)
{
	struct rendered_particle_datum const *left = left_pointer;
	struct rendered_particle_datum const *right = right_pointer;
	int result = left->definition_index - right->definition_index;

	if (!result)
	{
		result = left->cluster_index - right->cluster_index;
		if (!result)
		{
			result =
				left->attached_to_first_person_weapon -
				right->attached_to_first_person_weapon;
		}
	}

	return result;
}

/* port: render_particles' gather: the visible particles listed, sorted and grouped by definition, cluster and first
person; the listed particles' count (render_particles_spu_begin gathers the same way) */
static short render_particles_gather(
	struct rendered_particle_datum *rendered_particles,
	short *group_particle_counts,
	short *group_count_result)
{
	short rendered_particle_count = 0;
	short group_count = 0;
	short local_player_index = render.local_player_index;
	long particle_index;

	if (local_player_index == NONE ||
		!local_player_is_first_person(local_player_index))
	{
		local_player_index = MAXIMUM_LOCAL_PLAYERS;
	}

	for (particle_index = data_next_index(particle_data, NONE);
		particle_index != NONE;
		particle_index = data_next_index(particle_data, particle_index))
	{
		struct particle_datum *particle = particle_get(particle_index);
		boolean owned_by_local_player =
			particle->local_player_index == local_player_index;

		/* port: not the first-person weapon's with it hidden (view_fov.c) */
		if (render_location_visible(&particle->location) &&
			viewmodel_draws_geometry(owned_by_local_player &&
				TEST_FLAG(particle->flags, _particle_datum_dont_draw_third_person_bit)) &&
			(!TEST_FLAG(
				particle->flags,
				_particle_datum_dont_draw_first_person_bit) ||
				!owned_by_local_player) &&
			(!TEST_FLAG(
				particle->flags,
				_particle_datum_dont_draw_third_person_bit) ||
				owned_by_local_player))
		{
			struct rendered_particle_datum *rendered_particle =
				&rendered_particles[rendered_particle_count++];

			rendered_particle->particle_index = (short)particle_index;
			rendered_particle->definition_index =
				(short)particle->definition_index;
			rendered_particle->cluster_index =
				particle->location.cluster_index;
			rendered_particle->attached_to_first_person_weapon =
				owned_by_local_player &&
				TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_third_person_bit);
		}
	}

	if (rendered_particle_count > 0)
	{
		short remaining_particle_count = rendered_particle_count;

		qsort(
			rendered_particles,
			rendered_particle_count,
			sizeof(struct rendered_particle_datum),
			compare_rendered_particles);

		{
			struct rendered_particle_datum *rendered_particle =
				rendered_particles;
			short *current_sprite_group = NULL;
			short previous_definition_index = NONE;
			short previous_cluster_index = NONE;
			boolean previous_attached_to_first_person_weapon = FALSE;

			while (remaining_particle_count-- > 0)
			{
				if (rendered_particle->definition_index ==
						previous_definition_index &&
					rendered_particle->cluster_index ==
						previous_cluster_index &&
					rendered_particle->attached_to_first_person_weapon ==
						previous_attached_to_first_person_weapon)
				{
					(*current_sprite_group)++;
				}
				else
				{
					if (group_count >= MAXIMUM_RENDERED_PARTICLE_GROUPS)
						break;

					current_sprite_group =
						&group_particle_counts[group_count++];
					*current_sprite_group = 1;
					previous_definition_index =
						rendered_particle->definition_index;
					previous_cluster_index =
						rendered_particle->cluster_index;
					previous_attached_to_first_person_weapon =
						rendered_particle->attached_to_first_person_weapon;
				}

				rendered_particle++;
			}
		}
	}

	*group_count_result = group_count;
	return rendered_particle_count;
}

/* port: render_particles' particle at its place: its size cut, scale and fades, its sprite */
static void render_particle_sprite(
	struct build_sprite_data *sprite_data,
	struct particle_definition *definition,
	struct particle_datum *particle,
	real_point3d const *position,
	real_vector3d const *direction,
	real radius,
	short *built_particle_count,
	real *total_radius)
{
	real diameter =
		render_frustum_sphere_diameter_in_pixels(
			&render.frustum,
			position,
			radius);

	if (diameter > definition->lod_cutoff)
	{
		real fade;
		real scale;
		real remaining_life;
		unsigned long flags;
		word particle_flags;

		fade = 1.0f;
		(*built_particle_count)++;
		scale =
			(radius + radius) *
			definition->runtime_oo_width;
		remaining_life =
			particle->lifespan - particle->age;
		*total_radius += radius;

		if (diameter < definition->minimum_pixels)
		{
			scale =
				(definition->minimum_pixels / diameter) * scale;
		}

		if (definition->fade_in_time > 0.0f &&
			particle->age < definition->fade_in_time)
		{
			fade = particle->age / definition->fade_in_time;
		}

		if (definition->fade_out_time > 0.0f &&
			remaining_life < definition->fade_out_time)
		{
			fade *=
				remaining_life / definition->fade_out_time;
		}

		flags = 0;
		particle_flags = particle->flags;
		SET_FLAG(
			flags,
			_build_sprite_u_mirror_bit,
			TEST_FLAG(
				particle_flags,
				_particle_datum_u_mirror_bit));
		SET_FLAG(
			flags,
			_build_sprite_v_mirror_bit,
			TEST_FLAG(
				particle_flags,
				_particle_datum_v_mirror_bit));

		build_sprite(
			sprite_data,
			definition->sprite_orientation,
			particle->sequence_index,
			particle->frame_index,
			position,
			direction,
			particle->rotation,
			scale,
			&particle->color,
			fade,
			flags);

		particle->last_rendered_frame_index =
			render.frame_index;
	}

	return;
}

/* port: render_particles' group: its particles placed and their sprites built and drawn; the group after it */
static struct rendered_particle_datum *render_particles_group(
	struct rendered_particle_datum *rendered_particle,
	short particle_count)
{
	struct particle_definition *definition =
		particle_definition_get(
			rendered_particle->definition_index);
	struct build_sprite_data sprite_data;
	real_point3d position;
	real_vector3d direction;
	real total_radius = 0.0f;
	short built_particle_count = 0;

	build_sprites_begin(
		&sprite_data,
		particle_count,
		definition->bitmap.index,
		&definition->shader,
		rendered_particle->attached_to_first_person_weapon
			? FLAG(_build_sprites_first_person_bit)
			: 0);

	while (particle_count-- > 0)
	{
		struct particle_datum *particle =
			particle_get(rendered_particle->particle_index);
		real radius =
			particle_get_radius(rendered_particle->particle_index);

		if (particle->object_index != NONE)
		{
			real_matrix4x3 const *node_matrix;

			if (TEST_FLAG(
				particle->flags,
				_particle_datum_attached_to_local_player_bit))
			{
				node_matrix =
					first_person_weapon_get_node_matrix(
						particle->local_player_index,
						particle->node_index);
			}
			else if (object_try_and_get(particle->object_index))
			{
				node_matrix = object_get_node_matrix(
					particle->object_index,
					particle->node_index);
			}
			else
			{
				particle_delete(rendered_particle->particle_index);
				rendered_particle++;
				continue;
			}

			matrix4x3_transform_point(
				node_matrix,
				&particle->position,
				&position);
			matrix4x3_transform_normal(
				node_matrix,
				&particle->direction,
				&direction);
		}
		else
		{
			position = particle->position;
			direction = particle->direction;
			/* January explicitly reasserts the detached sentinel here. */
			particle->object_index = NONE;
		}

		render_particle_sprite(
			&sprite_data,
			definition,
			particle,
			&position,
			&direction,
			radius,
			&built_particle_count,
			&total_radius);

		rendered_particle++;
	}

	((struct shader_effect_definition *)sprite_data.shader)->
		secondary_map_radius = built_particle_count ?
		total_radius / built_particle_count : 0.0f;
	build_sprites_end(&sprite_data);

	return rendered_particle;
}

#ifdef HALO_PS3
#include "ps3_spu_game.h" /* port: port/ps3/src/ps3_spu.c */

extern boolean debug_sprites; /* port: render_sprite.c */

enum
{
	PARTICLES_SPU_PARTICLES = 2048,
	PARTICLES_SPU_TABLES = 512,
	PARTICLES_SPU_TABLE_BYTES = 192 * 1024,
};

/* port: a bitmap group's sprite table for the SPU (build_sprite_spu_table) and its bitmaps by slot, kept for the map */
struct particles_spu_table
{
	long bitmap_group_index;
	spu_game_weather_sprites const *sprites;
	unsigned long size;
	struct bitmap_data *bitmaps[SPU_GAME_SPRITE_SLOTS];
	short slot_count;
	boolean valid;
};

static struct
{
	boolean ready;
	unsigned long used;
	struct particles_spu_table tables[PARTICLES_SPU_TABLES];
	spu_game_weather_sprites scratch;
	unsigned char bytes[PARTICLES_SPU_TABLE_BYTES] __attribute__((aligned(16)));
} particles_spu_tables;

/* port: the window's particles gathered by render_particles_spu_begin, each group's sprites built on the render SPU
or (the first person's, those past the SPU's tables or room) by the PPE when render_particles draws them */
static struct
{
	spu_game_sprites job;
	spu_game_answer answer;
	spu_game_sprite_group groups[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	spu_game_sprite_particle particles[PARTICLES_SPU_PARTICLES];
	real radii[PARTICLES_SPU_PARTICLES];
	struct build_sprite_data sprite_data[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	struct rendered_particle_datum rendered[MAXIMUM_RENDERED_PARTICLES];
	short counts[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	short first[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	/* a group's job group (NONE: the PPE's), and the build_sprite group of each of its slots (NONE: none) */
	short job_groups[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	short slot_groups[MAXIMUM_RENDERED_PARTICLE_GROUPS][SPU_GAME_SPRITE_SLOTS];
	short group_count;
	short job_group_count;
	int32_t big_sprite_count;
	uint32_t ticket;
	boolean begun;
} particles_spu __attribute__((aligned(128)));

void render_particles_spu_dispose(
	void)
{
	short index;

	for (index = 0; index < PARTICLES_SPU_TABLES; index++)
	{
		particles_spu_tables.tables[index].bitmap_group_index = NONE;
	}
	particles_spu_tables.used = 0;
	particles_spu_tables.ready = TRUE;

	return;
}

/* port: the bitmap group's table, made at its first use; NULL when the SPU cannot take its sprites */
static struct particles_spu_table const *particles_spu_table_get(
	long bitmap_group_index)
{
	long probe;

	if (!particles_spu_tables.ready)
	{
		render_particles_spu_dispose();
	}
	if (bitmap_group_index == NONE)
	{
		return NULL;
	}
	for (probe = 0; probe < PARTICLES_SPU_TABLES; probe++)
	{
		struct particles_spu_table *table =
			&particles_spu_tables.tables[(bitmap_group_index + probe) & (PARTICLES_SPU_TABLES - 1)];

		if (table->bitmap_group_index == bitmap_group_index)
		{
			return table->valid ? table : NULL;
		}
		if (table->bitmap_group_index == NONE)
		{
			unsigned long room;

			table->bitmap_group_index = bitmap_group_index;
			table->valid = build_sprite_spu_table(bitmap_group_index, &particles_spu_tables.scratch, table->bitmaps,
				&table->slot_count, &table->size);
			room = (table->size + 15) & ~15UL;
			if (table->valid && particles_spu_tables.used + room <= PARTICLES_SPU_TABLE_BYTES)
			{
				memcpy(&particles_spu_tables.bytes[particles_spu_tables.used], &particles_spu_tables.scratch, table->size);
				table->sprites = (spu_game_weather_sprites const *)&particles_spu_tables.bytes[particles_spu_tables.used];
				particles_spu_tables.used += room;
			}
			else
			{
				table->valid = FALSE;
			}
			return table->valid ? table : NULL;
		}
	}

	return NULL;
}

/* port: a group's part of the SPU's job: its particles placed (render_particles_group's way), its sprites' groups
made now for each slot its particles draw from; it stays the PPE's when the SPU cannot take it */
static void render_particles_spu_group(
	short group_index,
	short *spu_particle_count)
{
	struct rendered_particle_datum *rendered_particle = &particles_spu.rendered[particles_spu.first[group_index]];
	short particle_count = particles_spu.counts[group_index];
	struct particle_definition *definition = particle_definition_get(rendered_particle->definition_index);
	spu_game_sprite_group *job_group = &particles_spu.groups[particles_spu.job_group_count];
	struct build_sprite_data *sprite_data = &particles_spu.sprite_data[particles_spu.job_group_count];
	spu_game_sprite_particle *entries = &particles_spu.particles[*spu_particle_count];
	struct particles_spu_table const *table;
	boolean slot_used[SPU_GAME_SPRITE_SLOTS];
	short index;

	particles_spu.job_groups[group_index] = NONE;
	if (rendered_particle->attached_to_first_person_weapon ||
		*spu_particle_count + particle_count > PARTICLES_SPU_PARTICLES)
	{
		return;
	}
	table = particles_spu_table_get(definition->bitmap.index);
	if (!table)
	{
		return;
	}
	memset(slot_used, 0, sizeof(slot_used));
	for (index = 0; index < particle_count; index++, rendered_particle++)
	{
		struct particle_datum *particle = particle_get(rendered_particle->particle_index);
		spu_game_sprite_particle *entry = &entries[index];
		real_point3d position;
		real_vector3d direction;
		short sequence_index = particle->sequence_index;
		short sprite_index = particle->frame_index;

		memset(entry, 0, sizeof(*entry));
		particles_spu.radii[*spu_particle_count + index] = particle_get_radius(rendered_particle->particle_index);
		if (particle->object_index != NONE)
		{
			real_matrix4x3 const *node_matrix;

			if (TEST_FLAG(particle->flags, _particle_datum_attached_to_local_player_bit))
			{
				node_matrix = first_person_weapon_get_node_matrix(particle->local_player_index, particle->node_index);
			}
			else if (object_try_and_get(particle->object_index))
			{
				node_matrix = object_get_node_matrix(particle->object_index, particle->node_index);
			}
			else
			{
				/* (deleted as the render deletes it; no sprite of it) */
				particle_delete(rendered_particle->particle_index);
				rendered_particle->particle_index = NONE;
				entry->sequence_index = NONE;
				continue;
			}
			matrix4x3_transform_point(node_matrix, &particle->position, &position);
			matrix4x3_transform_normal(node_matrix, &particle->direction, &direction);
		}
		else
		{
			position = particle->position;
			direction = particle->direction;
			particle->object_index = NONE;
		}
		entry->position[0] = position.x;
		entry->position[1] = position.y;
		entry->position[2] = position.z;
		entry->radius = particles_spu.radii[*spu_particle_count + index];
		entry->direction[0] = direction.i;
		entry->direction[1] = direction.j;
		entry->direction[2] = direction.k;
		entry->rotation = particle->rotation;
		entry->color[0] = particle->color.alpha;
		entry->color[1] = particle->color.red;
		entry->color[2] = particle->color.green;
		entry->color[3] = particle->color.blue;
		entry->age = particle->age;
		entry->lifespan = particle->lifespan;
		entry->sequence_index = sequence_index;
		entry->sprite_index = sprite_index;
		entry->flags =
			(TEST_FLAG(particle->flags, _particle_datum_u_mirror_bit) ? SPU_GAME_SPRITE_U_MIRROR : 0) |
			(TEST_FLAG(particle->flags, _particle_datum_v_mirror_bit) ? SPU_GAME_SPRITE_V_MIRROR : 0);
		if (sequence_index >= 0 && sequence_index < table->sprites->sequence_count &&
			table->sprites->first_sprite[sequence_index] != NONE &&
			sprite_index >= 0 && sprite_index < table->sprites->sprite_count[sequence_index])
		{
			long slot = table->sprites->sprites[table->sprites->first_sprite[sequence_index] + sprite_index].slot;

			if (slot >= 0 && slot < SPU_GAME_SPRITE_SLOTS)
			{
				slot_used[slot] = TRUE;
			}
		}
	}

	build_sprites_begin(sprite_data, particle_count, definition->bitmap.index, &definition->shader, 0);
	for (index = 0; index < SPU_GAME_SPRITE_SLOTS; index++)
	{
		short sprite_group = sprite_data->group_count;

		particles_spu.slot_groups[group_index][index] = NONE;
		job_group->vertices[index] = 0;
		if (index >= table->slot_count || !slot_used[index])
		{
			continue;
		}
		/* (the group of a bitmap without vertices made too, as build_sprite_get_group makes it) */
		if (build_sprite_group_for_bitmap(sprite_data, table->bitmaps[index]) != NONE)
		{
			job_group->vertices[index] = PS3_SPU_EA(sprite_data->groups[sprite_group].vertices);
		}
		if (sprite_data->group_count > sprite_group)
		{
			particles_spu.slot_groups[group_index][index] = sprite_group;
		}
	}
	job_group->lod_cutoff = definition->lod_cutoff;
	job_group->minimum_pixels = definition->minimum_pixels;
	job_group->one_over_width = definition->runtime_oo_width;
	job_group->fade_in_time = definition->fade_in_time;
	job_group->fade_out_time = definition->fade_out_time;
	job_group->mode = definition->sprite_orientation;
	job_group->fade_mode = definition->shader.framebuffer_fade_mode;
	job_group->fade_alpha = definition->shader.framebuffer_blend_function && !TEST_FLAG(definition->shader.flags, 1);
	job_group->sprites = PS3_SPU_EA(table->sprites);
	job_group->sprites_size = (uint32_t)table->size;
	job_group->first_particle = *spu_particle_count;
	job_group->particle_count = particle_count;
	particles_spu.job_groups[group_index] = particles_spu.job_group_count++;
	*spu_particle_count += particle_count;

	return;
}

void render_particles_spu_begin(
	void)
{
	short spu_particle_count = 0;
	short group_index;
	short first = 0;

	particles_spu.begun = FALSE;
	if (!render_particles_enabled || debug_sprites || !ps3_spu_render_running())
	{
		return;
	}
	render_particles_gather(particles_spu.rendered, particles_spu.counts, &particles_spu.group_count);
	particles_spu.job_group_count = 0;
	for (group_index = 0; group_index < particles_spu.group_count; group_index++)
	{
		particles_spu.first[group_index] = first;
		render_particles_spu_group(group_index, &spu_particle_count);
		first += particles_spu.counts[group_index];
	}
	particles_spu.begun = TRUE;
	particles_spu.ticket = 0;
	if (!particles_spu.job_group_count)
	{
		return;
	}

	{
		spu_game_sprites *job = &particles_spu.job;
		short index;

		memset(job, 0, sizeof(*job));
		job->world_to_view[0] = render.frustum.world_to_view.scale;
		for (index = 0; index < 12; index++)
		{
			job->world_to_view[1 + index] = render.frustum.world_to_view.n[index / 3][index % 3];
		}
		job->projection[0] = render.frustum.projection_matrix[0][0];
		job->projection[1] = render.frustum.projection_matrix[2][0];
		job->projection[2] = render.frustum.projection_matrix[1][1];
		job->projection[3] = render.frustum.projection_matrix[2][1];
		job->projection_world_to_screen[0] = render.frustum.projection_world_to_screen.i;
		job->projection_world_to_screen[1] = render.frustum.projection_world_to_screen.j;
		build_sprite_spu_globals(job->viewer_up, job->viewer_forward, &job->big_sprite_count);
		particles_spu.big_sprite_count = job->big_sprite_count;
		job->groups = PS3_SPU_EA(particles_spu.groups);
		job->group_count = particles_spu.job_group_count;
		job->particles = PS3_SPU_EA(particles_spu.particles);
		job->particle_count = spu_particle_count;
		job->answer = PS3_SPU_EA(&particles_spu.answer);
		particles_spu.answer.status = 0;
		particles_spu.ticket = ps3_spu_render_submit(SPU_GAME_SPRITES, sizeof(*job), PS3_SPU_EA(job), 0);
	}

	return;
}

/* port: the SPU's group's build_sprite groups in the order build_sprite made them (its slots' first uses), with their
sprites */
static void render_particles_spu_order_groups(
	short group_index,
	spu_game_sprite_group const *job_group,
	struct build_sprite_data *sprite_data)
{
	struct build_sprite_group groups[SPU_GAME_SPRITE_SLOTS];
	long orders[SPU_GAME_SPRITE_SLOTS];
	short sorted[SPU_GAME_SPRITE_SLOTS];
	short index;

	for (index = 0; index < sprite_data->group_count; index++)
	{
		orders[index] = LONG_MAX;
		sorted[index] = index;
		groups[index] = sprite_data->groups[index];
	}
	for (index = 0; index < SPU_GAME_SPRITE_SLOTS; index++)
	{
		short sprite_group = particles_spu.slot_groups[group_index][index];

		if (sprite_group != NONE)
		{
			groups[sprite_group].sprite_count = (short)job_group->slot_sprites[index];
			if (job_group->slot_order[index] != NONE)
			{
				orders[sprite_group] = job_group->slot_order[index];
			}
		}
	}
	for (index = 1; index < sprite_data->group_count; index++)
	{
		short moved = sorted[index];
		short place = index;

		while (place > 0 && orders[sorted[place - 1]] > orders[moved])
		{
			sorted[place] = sorted[place - 1];
			place--;
		}
		sorted[place] = moved;
	}
	for (index = 0; index < sprite_data->group_count; index++)
	{
		sprite_data->groups[index] = groups[sorted[index]];
	}

	return;
}

/* port: a group of the SPU's job drawn as render_particles_group draws it: the SPU's sprites, or the PPE's from the
places the gather found when the SPU did not build them */
static void render_particles_spu_group_end(
	short group_index,
	boolean done)
{
	short job_group_index = particles_spu.job_groups[group_index];
	spu_game_sprite_group const *job_group = &particles_spu.groups[job_group_index];
	struct build_sprite_data *sprite_data = &particles_spu.sprite_data[job_group_index];
	struct rendered_particle_datum *rendered_particle = &particles_spu.rendered[particles_spu.first[group_index]];
	struct particle_definition *definition = particle_definition_get(rendered_particle->definition_index);
	short particle_count = particles_spu.counts[group_index];
	boolean spu_built = done && job_group->sprite_count != NONE;
	real total_radius = 0.0f;
	short built_particle_count = 0;
	short index;

	if (spu_built)
	{
		sprite_data->sprite_count = (short)job_group->sprite_count;
		sprite_data->centroid.x = job_group->centroid[0];
		sprite_data->centroid.y = job_group->centroid[1];
		sprite_data->centroid.z = job_group->centroid[2];
		render_particles_spu_order_groups(group_index, job_group, sprite_data);
	}
	for (index = 0; index < particle_count; index++, rendered_particle++)
	{
		long entry_index = job_group->first_particle + index;
		spu_game_sprite_particle const *entry = &particles_spu.particles[entry_index];
		struct particle_datum *particle;

		if (rendered_particle->particle_index == NONE)
		{
			continue;
		}
		particle = particle_get(rendered_particle->particle_index);
		if (spu_built)
		{
			if (entry->flags & SPU_GAME_SPRITE_BUILT)
			{
				built_particle_count++;
				total_radius += particles_spu.radii[entry_index];
				particle->last_rendered_frame_index = render.frame_index;
			}
		}
		else
		{
			real_point3d position;
			real_vector3d direction;

			position.x = entry->position[0];
			position.y = entry->position[1];
			position.z = entry->position[2];
			direction.i = entry->direction[0];
			direction.j = entry->direction[1];
			direction.k = entry->direction[2];
			render_particle_sprite(sprite_data, definition, particle, &position, &direction,
				particles_spu.radii[entry_index], &built_particle_count, &total_radius);
		}
	}
	((struct shader_effect_definition *)sprite_data->shader)->secondary_map_radius = built_particle_count ?
		total_radius / built_particle_count : 0.0f;
	build_sprites_end(sprite_data);

	return;
}

/* port: the window's groups drawn in order, the SPU's and the PPE's; FALSE when no gather was made for the window */
static boolean render_particles_spu_end(
	void)
{
	boolean done = FALSE;
	short group_index;

	if (!particles_spu.begun)
	{
		return FALSE;
	}
	particles_spu.begun = FALSE;
	if (particles_spu.job_group_count)
	{
		done = ps3_spu_render_wait(particles_spu.ticket) && particles_spu.answer.status == SPU_GAME_DONE;
		particles_spu.ticket = 0;
		if (done)
		{
			build_sprite_spu_add_more(particles_spu.job.big_sprite_count - particles_spu.big_sprite_count,
				particles_spu.job.coverage);
		}
	}
	for (group_index = 0; group_index < particles_spu.group_count; group_index++)
	{
		if (particles_spu.job_groups[group_index] == NONE)
		{
			render_particles_group(&particles_spu.rendered[particles_spu.first[group_index]],
				particles_spu.counts[group_index]);
		}
		else
		{
			render_particles_spu_group_end(group_index, done);
		}
	}

	return TRUE;
}
#endif

void render_particles(
	void)
{
	struct rendered_particle_datum rendered_particles[MAXIMUM_RENDERED_PARTICLES];
	short group_particle_counts[MAXIMUM_RENDERED_PARTICLE_GROUPS];

	profile_enter(render_particles_section);

#ifdef HALO_PS3
	/* port: the window's gather and the SPU's sprites (render_particles_spu_begin) */
	if (render_particles_spu_end())
	{
		profile_exit(render_particles_section);
		return;
	}
#endif

	if (render_particles_enabled)
	{
		short group_count;

		/* port: (the gather, then each group's sprites: render_particles_gather, render_particles_group) */
		if (render_particles_gather(rendered_particles, group_particle_counts, &group_count) > 0)
		{
			struct rendered_particle_datum *rendered_particle =
				rendered_particles;
			short group_index;

			for (group_index = 0; group_index < group_count; group_index++)
			{
				rendered_particle = render_particles_group(
					rendered_particle,
					group_particle_counts[group_index]);
			}
		}
	}

	profile_exit(render_particles_section);
	return;
}

/* ---------- private code */
