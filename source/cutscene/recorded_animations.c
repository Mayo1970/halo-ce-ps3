/* RECORDED_ANIMATIONS.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/cutscene/recorded_animations.c. */

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "cutscene/recorded_animation_definitions.h"
#include "cutscene/recorded_animation_playback.h"
#include "cutscene/recorded_animation_playback_v1.h"
#include "game/players.h"
#include "hs/hs_library_external.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "saved games/game_state.h"
#include "text/draw_string.h"
#include "units/unit_control_data.h"
#include "units/units.h"
#include "units/vehicles.h"

/* ---------- constants */

enum
{
	_recording_thread_finished_bit = 0,
	_recording_thread_killed_bit = 1,
	_recording_thread_unit_was_controlled_bit = 2,
	_recording_thread_delete_unit_on_complete_bit = 3,
	_recording_thread_hover_vehicle_on_complete_bit = 4,
	RECORDED_ANIMATION_VERSION = 4,
};

/* ---------- structures */

struct animation_thread
{
	short identifier;
	short pad2;
	long unit_index;
	word ticks_left;
	word flags;
	long relative_ticks;
	byte *event_stream;
	struct unit_control_data controller;
	byte animation_state[0xC];
	short version;
	short pad62;
};

struct animation_thread_debug
{
	boolean active;
	byte pad1[3];
	byte *HALO_PTR32 event_stream_start;
	long stream_length;
	short animation_index;
	short padE;
};

/* 64 of them fill the 0x400 bytes of animation_threads_debug */
typedef char animation_thread_debug_size_assert[
	sizeof(struct animation_thread_debug) == 0x10 ? 1 : -1];

struct animation_playback
{
	void (*initialize_event_stream)(
		void *animation_state,
		void *controller,
		byte **event_stream,
		byte unit_control_data_version);
	boolean (*apply_event_stream)(
		void *animation_state,
		struct unit_control_data *controller,
		long *relative_ticks,
		byte **event_stream);
};

/* ---------- macros */

#define animation_threads_debug_get(index) \
	(animation_threads_debug + ((index) & 0xFFFF))

/* ---------- prototypes */

static struct animation_thread *get_controlling_thread(
	long unit_index,
	long *thread_index_reference);
static boolean recorded_animation_play_internal(
	long unit_index,
	short animation_index,
	word extra_flags);

/* ---------- globals */

static struct data_array *animation_threads;
boolean debug_recording = FALSE;
static struct animation_thread_debug *animation_threads_debug = NULL;

static struct animation_playback current_playback =
{
	recorded_animation_initialize_event_stream,
	recorded_animation_apply_event_stream
};

static struct animation_playback v1_playback =
{
	recorded_animation_initialize_event_stream_v1,
	recorded_animation_apply_event_stream_v1
};

static struct animation_playback *playback_codec[RECORDED_ANIMATION_VERSION] =
{
	&v1_playback,
	&v1_playback,
	&v1_playback,
	&current_playback
};

short debug_recording_newlines = 10;

/* ---------- public code */

void recorded_animations_initialize(
	void)
{
	animation_threads = game_state_data_new(
		"recorded animations",
		64,
		sizeof(struct animation_thread));
	match_assert(
		"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
		108,
		animation_threads);

	animation_threads_debug = match_malloc(
		"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
		111,
		0x400);
	match_assert(
		"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
		112,
		animation_threads_debug);

	return;
}

void recorded_animations_dispose(
	void)
{
	if (animation_threads_debug)
	{
		match_free(
			"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
			123,
			animation_threads_debug);
		animation_threads_debug = NULL;
	}

	return;
}

void recorded_animations_dispose_from_old_map(
	void)
{
	data_make_invalid(animation_threads);

	return;
}

void recorded_animations_clear_debug_storage(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
		0x99,
		animation_threads_debug);
	memset(animation_threads_debug, 0, 0x400);

	return;
}

void recorded_animations_update(
	void)
{
	struct data_iterator iterator;
	struct animation_thread *thread;

	data_iterator_new(&iterator, animation_threads);
	thread = data_iterator_next(&iterator);
	while (thread)
	{
		if (object_try_and_get_and_verify_type(thread->unit_index, _object_mask_unit))
		{
			if (!TEST_FLAG(thread->flags, _recording_thread_finished_bit))
			{
				struct animation_thread_debug *thread_debug;
				long *relative_ticks;
				boolean finished;

				thread->ticks_left--;
				relative_ticks = &thread->relative_ticks;
				finished = !playback_codec[thread->version]->apply_event_stream(
					thread->animation_state,
					&thread->controller,
					relative_ticks,
					&thread->event_stream);

				match_assert(
					"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
					347,
					thread->relative_ticks>=0);

				thread_debug = animation_threads_debug_get(iterator.datum_index);
				if (thread_debug->active)
				{
					/* port: a full pointer first; in a difference GCC sign-extends a HALO_PTR32 one (ROADMAP trap 36) */
					match_assert(
						"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
						354,
						thread->event_stream-(byte *)thread_debug->event_stream_start<thread_debug->stream_length||(thread->event_stream-(byte *)thread_debug->event_stream_start==thread_debug->stream_length&&finished));
				}

				thread->relative_ticks++;
				unit_control(thread->unit_index, &thread->controller);
				SET_FLAG(
					thread->flags,
					_recording_thread_finished_bit,
					finished);
			}
			else
			{
				struct animation_thread_debug *thread_debug;

				thread_debug = animation_threads_debug_get(iterator.datum_index);
				if (thread_debug->active &&
					!TEST_FLAG(thread->flags, _recording_thread_killed_bit) &&
					thread->ticks_left != 0)
				{
					match_vassert(
						"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
						373,
						FALSE,
						csprintf(
							temporary,
							"animation %s appears corrupt",
							TAG_BLOCK_GET_ELEMENT(
								&global_scenario_get()->recorded_animations,
								thread_debug->animation_index,
								struct recorded_animation_definition)->name));
				}

				thread_debug->active = FALSE;
				unit_set_controllable(
					thread->unit_index,
					TEST_FLAG(thread->flags, _recording_thread_unit_was_controlled_bit));
				unit_set_possessed(thread->unit_index, FALSE);
				unit_set_actively_controlled(thread->unit_index, FALSE);
				object_set_automatic_deactivation(thread->unit_index, TRUE);
				if (TEST_FLAG(
					thread->flags,
					_recording_thread_delete_unit_on_complete_bit))
				{
					hs_object_destroy(thread->unit_index);
				}
				if (TEST_FLAG(
					thread->flags,
					_recording_thread_hover_vehicle_on_complete_bit))
				{
					vehicle_hover(thread->unit_index, TRUE);
				}

				datum_delete(animation_threads, iterator.datum_index);
			}
		}
		else
		{
			datum_delete(animation_threads, iterator.datum_index);
		}

		thread = data_iterator_next(&iterator);
	}

	return;
}

void recorded_animations_initialize_for_new_map(
	void)
{
	data_make_valid(animation_threads);
	recorded_animations_clear_debug_storage();

	return;
}

void render_debug_recording(
	void)
{
	char string[0x2800];
	struct data_iterator iterator;
	struct scenario_object_name *object_name;
	short tab_stops[2];
	short newline_index;
	long string_length;
	struct animation_thread *thread;
	struct object_datum *object;
	struct animation_thread_debug *thread_debug;
	char const *recording_name;

	if (debug_recording)
	{
		string_length = 0;
		newline_index = 0;
		tab_stops[0] = 200;
		tab_stops[1] = 300;
		while (newline_index < debug_recording_newlines)
		{
			string_length += sprintf(string + (short)string_length, "|n");
			newline_index++;
		}

		string_length += sprintf(
			string + (short)string_length,
			"recording name|tticks left|tobject name");

		data_iterator_new(&iterator, animation_threads);
		thread = data_iterator_next(&iterator);
		while (thread)
		{
			object = object_try_and_get_and_verify_type(
				thread->unit_index,
				_object_mask_all);
			thread_debug = animation_threads_debug_get(iterator.datum_index);
			if (!TEST_FLAG(thread->flags, _recording_thread_finished_bit) &&
				object &&
				object->object.name_index != NONE)
			{
				object_name = TAG_BLOCK_GET_ELEMENT(
					&global_scenario_get()->object_names,
					object->object.name_index,
					struct scenario_object_name);

				recording_name = "<unknown>";
				if (thread_debug->active)
				{
					recording_name = TAG_BLOCK_GET_ELEMENT(
						&global_scenario_get()->recorded_animations,
						thread_debug->animation_index,
						struct recorded_animation_definition)->name;
				}

				string_length += sprintf(
					string + (short)string_length,
					"|n%s|t",
					recording_name);
				string_length += sprintf(
					string + (short)string_length,
					"%d|t",
					thread->ticks_left);
				string_length += sprintf(
					string + (short)string_length,
					"%s",
					object_name->name);
			}

			thread = data_iterator_next(&iterator);
		}

		string[0x400] = 0;
		draw_string_set_tab_stops(tab_stops, NUMBEROF(tab_stops));
		render_debug_string(TRUE, string);
		draw_string_set_tab_stops(tab_stops, 0);
	}

	return;
}

void recorded_animation_verify(
	struct recorded_animation_definition const *animation)
{
	struct unit_control_data controller;
	byte animation_state[0xC];
	byte *stream;
	byte *playback_stream;
	long size;
	long relative_ticks;
	long ticks_left;
	boolean finished;

	stream = animation->event_stream.address;
	playback_stream = stream;
	size = animation->event_stream.size;
	ticks_left = (word)animation->length_in_ticks;
	relative_ticks = 0;

	playback_codec[animation->version-1]->initialize_event_stream(
		animation_state,
		&controller,
		&playback_stream,
		animation->unit_control_data_version);

	do
	{
		ticks_left--;
		finished = !playback_codec[animation->version-1]->apply_event_stream(
			animation_state,
			&controller,
			&relative_ticks,
			&playback_stream);

		match_assert(
			"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
			428,
			ticks_left>=0);
		match_assert(
			"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
			429,
			relative_ticks>=0);
		match_assert(
			"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
			430,
			playback_stream-stream<size||(playback_stream-stream==size&&finished));

		relative_ticks++;
	}
	while (!finished);

	return;
}

boolean recorded_animation_controlling_unit(
	long unit_index)
{
	boolean result = FALSE;
	struct data_iterator iterator;
	struct animation_thread *thread;

	data_iterator_new(&iterator, animation_threads);
	thread = data_iterator_next(&iterator);
	if (thread)
	{
		while (thread->unit_index != unit_index ||
			TEST_FLAG(thread->flags, _recording_thread_finished_bit))
		{
			thread = data_iterator_next(&iterator);
			if (!thread)
				break;
		}

		if (thread)
			result = TRUE;
	}

	return result;
}

static struct animation_thread *get_controlling_thread(
	long unit_index,
	long *thread_index_reference)
{
	long thread_index = NONE;
	struct data_iterator iterator;
	struct animation_thread *thread;

	data_iterator_new(&iterator, animation_threads);

	thread = data_iterator_next(&iterator);
	while (thread)
	{
		if (thread->unit_index == unit_index)
		{
			thread_index = iterator.datum_index;
			break;
		}

		thread = data_iterator_next(&iterator);
	}

	if (thread_index_reference)
		*thread_index_reference = thread_index;

	return thread;
}

void recorded_animation_kill(
	long unit_index)
{
	struct animation_thread *thread;

	thread = get_controlling_thread(unit_index, NULL);
	if (thread)
	{
		thread->flags |=
			FLAG(_recording_thread_finished_bit) |
			FLAG(_recording_thread_killed_bit);
	}

	return;
}

long recorded_animation_get_time_left(
	long unit_index)
{
	long result = 0;
	struct animation_thread *thread;

	thread = get_controlling_thread(unit_index, NULL);

	match_assert(
		"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
		312,
		!thread||thread->unit_index==unit_index);

	if (thread && thread->unit_index == unit_index)
		result = thread->ticks_left;

	return result;
}

boolean recorded_animation_play(
	long unit_index,
	short animation_index)
{
	return recorded_animation_play_internal(unit_index, animation_index, 0);
}

boolean recorded_animation_play_and_delete(
	long unit_index,
	short animation_index)
{
	return recorded_animation_play_internal(
		unit_index,
		animation_index,
		FLAG(_recording_thread_delete_unit_on_complete_bit));
}

boolean recorded_animation_play_and_hover(
	long unit_index,
	short animation_index)
{
	return recorded_animation_play_internal(
		unit_index,
		animation_index,
		FLAG(_recording_thread_hover_vehicle_on_complete_bit));
}

/* ---------- private code */

static boolean recorded_animation_play_internal(
	long unit_index,
	short animation_index,
	word extra_flags)
{
	boolean result = FALSE;
	long thread_index;
	struct animation_thread *thread;
	struct recorded_animation_definition const *animation;
	struct animation_thread_debug *debug;
	struct animation_playback *playback;

	if (unit_index != NONE)
	{
		if (animation_index != NONE && animation_index < global_scenario_get()->recorded_animations.count)
		{
			object_get_and_verify_type(unit_index, _object_mask_unit);
			player_index_from_unit_index(unit_index);

			thread = get_controlling_thread(unit_index, &thread_index);
			animation = TAG_BLOCK_GET_ELEMENT(
				&global_scenario_get()->recorded_animations,
				animation_index,
				struct recorded_animation_definition);

			if (!recorded_animation_controlling_unit(unit_index))
			{
				if (!thread)
				{
					thread_index = datum_new(animation_threads);
					if (thread_index != NONE)
						thread = datum_get(animation_threads, thread_index);
				}

				if (thread)
				{
					match_assert(
						"c:\\halo\\SOURCE\\cutscene\\recorded_animations.c",
						233,
						animation->version>0&&animation->version<=RECORDED_ANIMATION_VERSION&&playback_codec[animation->version-1]);

					thread->unit_index = unit_index;
					thread->relative_ticks = 0;
					thread->ticks_left = animation->length_in_ticks;
					thread->event_stream = tag_data_get_pointer(
						&animation->event_stream,
						0,
						animation->event_stream.size);

					debug = animation_threads_debug_get(thread_index);
					debug->active = TRUE;
					debug->event_stream_start = thread->event_stream;
					debug->stream_length = animation->event_stream.size;
					debug->animation_index = animation_index;

					thread->version = animation->version - 1;
					SET_FLAG(thread->flags, _recording_thread_finished_bit, FALSE);

					playback = playback_codec[thread->version];
					playback->initialize_event_stream(
						thread->animation_state,
						&thread->controller,
						&thread->event_stream,
						animation->unit_control_data_version);

					unit_set_actively_controlled(unit_index, TRUE);
					SET_FLAG(
						thread->flags,
						_recording_thread_unit_was_controlled_bit,
						unit_controllable(unit_index));

					unit_set_controllable(unit_index, FALSE);
					unit_set_possessed(unit_index, TRUE);
					object_set_automatic_deactivation(unit_index, FALSE);
					thread->flags |= extra_flags;
					result = TRUE;
				}
				else
				{
					error(_error_silent, "Could not allocate space for a new animation");
				}
			}
			else if (thread)
			{
				char const *playing_name;

				debug = animation_threads_debug_get(thread_index);
				playing_name = "<unknown>";
				if (debug->active)
				{
					playing_name = TAG_BLOCK_GET_ELEMENT(
						&global_scenario_get()->recorded_animations,
						debug->animation_index,
						struct recorded_animation_definition)->name;
				}

				error(
					_error_silent,
					"trying to play %s while %s is playing",
					animation->name,
					playing_name);
			}
			else
			{
				error(_error_silent, "can't play animation on unit");
			}
		}
		else
		{
			error(_error_silent, "this animation doesn't exist");
		}
	}
	else
	{
		error(_error_silent, "unit doesn't exist");
	}

	return result;
}
