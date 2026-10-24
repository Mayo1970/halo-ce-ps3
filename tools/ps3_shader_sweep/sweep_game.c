/* The shader sweep's game side (tools/ps3_shader_sweep.py): once the map runs, each BSP drawn whole
(sweep_structure.c); then every object, effect and decal of the map made in front of the camera, a batch at a time,
and every weapon in the player's hands, while each kind of transparent group is drawn again with every shader that
fits it (sweep_transparent.c). The draws go through sweep_d3d.c, which makes their PS3 keys. */

#include "cseries/cseries.h"
#include "cache/cache_files.h"
#include "camera/observer.h"
#include "effects/decal_definitions.h"
#include "effects/decals.h"
#include "effects/effect_definitions.h"
#include "effects/effects.h"
#include "game/cheats.h"
#include "game/game.h"
#include "game/players.h"
#include "items/weapon_definitions.h"
#include "main/main.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "scenario/scenario_definitions.h"

#include <stdlib.h>

/* ticks played before the sweep, so that the map's own first frames (and their keys) come first; ticks each BSP is
drawn whole; things made at once and the ticks they are drawn */
#define SETTLE_TICKS 300
#define BSP_TICKS 30
#define BATCH_COUNT 8
#define BATCH_TICKS 45

#define OBJECT_DEFINITION_TAG 'obje'

enum
{
	_phase_settle,
	_phase_bsps,
	_phase_objects,
	_phase_camouflage,
	_phase_effects,
	_phase_decals,
	_phase_weapons,
	_phase_done,
};

void platform_log(char const *format, ...);
void shader_sweep_write(const char *map_name);
unsigned long shader_sweep_key_count(void);

extern boolean shader_sweep_all_surfaces;
/* render_objects.c: off while the sweep's objects are out, as one placed in the geometry can get a shadow with no
size (structures.c's radius assertion, c40) */
extern boolean render_shadows;
extern boolean shader_sweep_transparent;
extern long shader_sweep_transparent_draws;

const char *shader_sweep_site;

static struct
{
	short phase;
	long presents;
	/* the ticks played, which a revert to a checkpoint does not take back; the phase's start in them */
	long ticks;
	long last_time;
	long phase_start;
	long step;
	short start_bsp_index;
	struct tag_iterator iterator;
	boolean iterator_done;
	boolean camouflage;
	long objects[BATCH_COUNT];
	long effects[BATCH_COUNT];
	unsigned long phase_keys;
	long made;
} sweep;

static void phase_begin(
	short phase,
	char const *site)
{
	unsigned long keys = shader_sweep_key_count();

	platform_log("shader sweep: %s: %lu keys (+%lu); now %s", main_get_map_name(), keys, keys - sweep.phase_keys,
		site ? site : "done");
	sweep.phase = phase;
	sweep.phase_start = sweep.ticks;
	sweep.phase_keys = keys;
	sweep.step = 0;
	sweep.iterator_done = FALSE;
	sweep.made = 0;
	shader_sweep_site = site;
}

/* a point of the batch's row in front of the camera */
static void batch_point(
	long slot,
	real_point3d *point,
	real_vector3d *forward)
{
	struct observer_result const *camera = observer_get_camera(0);
	real_vector3d left;
	real across = ((real)slot - (BATCH_COUNT - 1) * 0.5f) * 1.5f;

	real length;

	/* (level, so that it and the world's up are the object's axes) */
	forward->i = camera->forward.i;
	forward->j = camera->forward.j;
	forward->k = 0.0f;
	length = (real)sqrt(forward->i * forward->i + forward->j * forward->j);
	if (length < 0.001f)
	{
		forward->i = 1.0f;
		forward->j = 0.0f;
	}
	else
	{
		forward->i /= length;
		forward->j /= length;
	}
	cross_product3d(global_up3d, forward, &left);
	point->x = camera->position.x + camera->forward.i * 4.0f + left.i * across;
	point->y = camera->position.y + camera->forward.j * 4.0f + left.j * across;
	point->z = camera->position.z + camera->forward.k * 4.0f + left.k * across;
}

static void objects_batch_delete(
	void)
{
	long slot;

	for (slot = 0; slot < BATCH_COUNT; slot++)
	{
		/* (a projectile or some garbage may be gone already) */
		if (sweep.objects[slot] != NONE && object_header_try_and_get(sweep.objects[slot]))
			object_delete(sweep.objects[slot]);
		sweep.objects[slot] = NONE;
	}
}

/* the next batch of object definitions made; FALSE once there are none */
static boolean objects_batch_new(
	void)
{
	long slot;

	for (slot = 0; slot < BATCH_COUNT && !sweep.iterator_done; slot++)
	{
		long tag_index = tag_iterator_next(&sweep.iterator);
		struct object_placement_data data;

		sweep.objects[slot] = NONE;
		if (tag_index == NONE)
		{
			sweep.iterator_done = TRUE;
			break;
		}
		object_placement_data_new(&data, tag_index, NONE);
		batch_point(slot, &data.position, &data.forward);
		data.up = *global_up3d;
		sweep.objects[slot] = object_new(&data);
		sweep.made += sweep.objects[slot] != NONE;
		/* a unit in camouflage: its model's camouflage passes, fully and partly cloaked */
		if (sweep.camouflage && sweep.objects[slot] != NONE)
		{
			struct unit_datum *unit = unit_try_and_get(sweep.objects[slot]);

			if (unit)
			{
				SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, TRUE);
				unit->unit.active_camouflage = slot & 1 ? 1.0f : 0.5f;
			}
		}
	}
	return slot > 0;
}

static void effects_batch_stop(
	void)
{
	long slot;

	for (slot = 0; slot < BATCH_COUNT; slot++)
	{
		if (sweep.effects[slot] != NONE)
			effect_stop(sweep.effects[slot], TRUE);
		sweep.effects[slot] = NONE;
	}
}

static boolean effects_batch_new(
	void)
{
	long slot;

	for (slot = 0; slot < BATCH_COUNT && !sweep.iterator_done; slot++)
	{
		long tag_index = tag_iterator_next(&sweep.iterator);
		char const *marker_name = "";
		real_point3d point;
		real_vector3d forward;

		sweep.effects[slot] = NONE;
		if (tag_index == NONE)
		{
			sweep.iterator_done = TRUE;
			break;
		}
		batch_point(slot, &point, &forward);
		sweep.effects[slot] = effect_new_unattached_from_markers(tag_index, NONE, NULL, 1, &marker_name, &point,
			&forward, 1.0f, 1.0f, NULL, NULL, FALSE);
		sweep.made += sweep.effects[slot] != NONE;
	}
	return slot > 0;
}

/* decals laid on what is below and ahead of the camera */
static boolean decals_batch_new(
	void)
{
	long slot;

	for (slot = 0; slot < BATCH_COUNT && !sweep.iterator_done; slot++)
	{
		long tag_index = tag_iterator_next(&sweep.iterator);
		real_vector3d down = { 0.0f, 0.0f, -4.0f };
		real_point3d point;
		real_vector3d forward;

		if (tag_index == NONE)
		{
			sweep.iterator_done = TRUE;
			break;
		}
		batch_point(slot, &point, &forward);
		decal_new(tag_index, &point, &down, 1.0f, FALSE, NONE, NULL);
		scale_vector3d(&forward, 4.0f, &forward);
		decal_new(tag_index, &point, &forward, 1.0f, FALSE, NONE, NULL);
		sweep.made++;
	}
	return slot > 0;
}

static void batch_nothing(
	void)
{
}

/* the player's unit, NONE without one */
static long player_unit(
	void)
{
	long player_index = local_player_get_player_index(0);

	return player_index == NONE ? NONE : player_get(player_index)->unit_index;
}

/* the next weapon in the player's hands, the last one gone; its first person model and its HUD are drawn */
static boolean weapon_new(
	void)
{
	long unit_index = player_unit();
	long tag_index;
	struct object_placement_data data;
	long weapon_index;

	if (unit_index == NONE || !object_header_try_and_get(unit_index))
		return FALSE;
	tag_index = tag_iterator_next(&sweep.iterator);
	if (tag_index == NONE)
		return FALSE;
	unit_delete_all_weapons(unit_index);
	object_placement_data_new(&data, tag_index, NONE);
	data.position = observer_get_camera(0)->position;
	weapon_index = object_new(&data);
	if (weapon_index != NONE)
	{
		/* (2: in its hands, as game_engine.c gives a starting weapon) */
		if (unit_add_weapon_to_inventory(unit_index, weapon_index, 2))
			sweep.made++;
		else
			object_delete(weapon_index);
	}
	return TRUE;
}

/* a batch made at the step's start, kept BATCH_TICKS, then gone; FALSE once the definitions are done */
static boolean batches_step(
	boolean (*batch_new)(void),
	void (*batch_end)(void))
{
	if (sweep.ticks - sweep.phase_start < sweep.step * BATCH_TICKS)
		return TRUE;
	batch_end();
	if (!batch_new())
		return FALSE;
	sweep.step++;
	return TRUE;
}

void shader_sweep_frame(
	void)
{
	long slot;

	/* (the game's globals do not exist at the start-up's first frames) */
	if (++sweep.presents < 60 || !game_map_loaded() || !game_in_progress())
		return;
	if (sweep.last_time && game_time_get() > sweep.last_time)
		sweep.ticks += game_time_get() - sweep.last_time;
	sweep.last_time = game_time_get();
	/* (the sweep's units fight the player; a death would revert the map) */
	cheat.deathless_player = TRUE;
	switch (sweep.phase)
	{
		case _phase_settle:
			if (sweep.ticks < SETTLE_TICKS)
				break;
			sweep.start_bsp_index = global_structure_bsp_index_get();
			for (slot = 0; slot < BATCH_COUNT; slot++)
			{
				sweep.objects[slot] = NONE;
				sweep.effects[slot] = NONE;
			}
			shader_sweep_transparent = TRUE;
			phase_begin(_phase_bsps, "bsp");
			break;

		case _phase_bsps:
			if (sweep.ticks - sweep.phase_start < sweep.step * BSP_TICKS)
				break;
			if (sweep.step < global_scenario_get()->structure_bsp_references.count)
			{
				if (global_structure_bsp_index_get() != sweep.step)
					scenario_switch_structure_bsp((short)sweep.step);
				shader_sweep_all_surfaces = TRUE;
				sweep.phase_start = sweep.ticks - sweep.step * BSP_TICKS;
				sweep.step++;
				break;
			}
			shader_sweep_all_surfaces = FALSE;
			if (global_structure_bsp_index_get() != sweep.start_bsp_index)
				scenario_switch_structure_bsp(sweep.start_bsp_index);
			tag_iterator_new(&sweep.iterator, OBJECT_DEFINITION_TAG);
			render_shadows = FALSE;
			phase_begin(_phase_objects, "objects");
			break;

		case _phase_objects:
			if (batches_step(objects_batch_new, objects_batch_delete))
				break;
			platform_log("shader sweep: %ld objects made", sweep.made);
			tag_iterator_new(&sweep.iterator, UNIT_DEFINITION_TAG);
			phase_begin(_phase_camouflage, "camouflage");
			sweep.camouflage = TRUE;
			break;

		case _phase_camouflage:
			if (batches_step(objects_batch_new, objects_batch_delete))
				break;
			platform_log("shader sweep: %ld units made in camouflage", sweep.made);
			sweep.camouflage = FALSE;
			render_shadows = TRUE;
			tag_iterator_new(&sweep.iterator, EFFECT_DEFINITION_TAG);
			phase_begin(_phase_effects, "effects");
			break;

		case _phase_effects:
			if (batches_step(effects_batch_new, effects_batch_stop))
				break;
			platform_log("shader sweep: %ld effects made", sweep.made);
			tag_iterator_new(&sweep.iterator, DECAL_GROUP_TAG);
			phase_begin(_phase_decals, "decals");
			break;

		case _phase_decals:
			if (batches_step(decals_batch_new, batch_nothing))
				break;
			platform_log("shader sweep: %ld decals made", sweep.made);
			tag_iterator_new(&sweep.iterator, WEAPON_DEFINITION_TAG);
			phase_begin(_phase_weapons, "weapons");
			break;

		case _phase_weapons:
			if (batches_step(weapon_new, batch_nothing))
				break;
			platform_log("shader sweep: %ld weapons held", sweep.made);
			shader_sweep_transparent = FALSE;
			phase_begin(_phase_done, NULL);
			platform_log("shader sweep: %ld transparent groups drawn again", shader_sweep_transparent_draws);
			shader_sweep_write(main_get_map_name());
			exit(0);
			break;
	}
}
