/* HS.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/hs/hs.c. */

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "cseries/profile.h"
#include "camera/director.h"
#include "cache/sound_cache.h"
#include "hs.h"
#include "hs_library_external.h"
#include "hs_library_internal.h"
#include "object_lists.h"
#include "hs_scenario_definitions.h"
#include "ai/ai.h"
#include "ai/ai_debug.h"
#include "cache/cache_files.h"
#include "cache/texture_cache.h"
#include "camera/camera_scripting.h"
#include "cutscene/cinematics.h"
#include "effects/player_effects.h"
#include "game/cheats.h"
#include "game/players.h"
#include "interface/attract_mode.h"
#include "interface/hud.h"
#include "interface/terminal.h"
#include "interface/ui_widget.h"
#include "main/console.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "objects/damage.h"
#include "objects/object_lights.h"
#include "objects/scenery.h"
#include "physics/breakable_surfaces.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "saved games/game_state.h"
#include "saved games/saved_game_files.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/ai_profile.h"
#include "ai/ai_script.h"
#include "cutscene/recorded_animation_definitions.h"
#include "cutscene/recorded_animations.h"
#include "devices/devices.h"
#include "game/game.h"
#include "interface/hud_definitions.h"
#include "interface/hud_messaging.h"
#include "interface/hud_unit.h"
#include "interface/hud_weapon.h"
#include "interface/interface.h"
#include "interface/player_ui.h"
#include "rasterizer/rasterizer_cinematics.h"
#include "shaders/shaders.h"
#include "sound/game_sound.h"
#include "sound/sound_classes.h"
#include "sound/sound_manager.h"
#include "structures/structure_lens_flares.h"
#include "structures/structure_visibility.h"
#include "tag_files/files.h"
#include "main/main.h"
#include "units/units.h"
#include "units/vehicles.h"

/* ---------- constants */

enum
{
	scenario_starting_profile_size = 0x68,
	scenario_conversation_definition_size = 0x74,
	scenario_cutscene_flag_size = 0x5C,
	scenario_cutscene_chapter_title_size = 0x60,
	hud_globals_group_tag = 'hudg',
	hud_message_text_group_tag = 'hmt ',
};

/* ---------- macros */

#define HS_EVALUATE_NO_ARGUMENTS(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	function(); \
	hs_return(thread_index, 0); \
	return; \
}

#define HS_EVALUATE_NO_OP(evaluator) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	hs_return(thread_index, 0); \
	return; \
}

#define HS_EVALUATE_RETURN_LONG(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	hs_return(thread_index, function()); \
	return; \
}

#define HS_EVALUATE_RETURN_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_short_result result; \
	result.value = 0; \
	result.short_value = function(); \
	hs_return(thread_index, result.value); \
	return; \
}

#define HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_boolean_result result; \
	result.value = 0; \
	result.boolean = function(); \
	hs_return(thread_index, result.value); \
	return; \
}

#define HS_EVALUATE_SHORT_FROM_LONG(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	long *arguments; \
	union hs_short_result result; \
	result.value = 0; \
	arguments = hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		result.short_value = function(arguments[0]); \
		hs_return(thread_index, result.value); \
	} \
	return; \
}

#define HS_EVALUATE_SHORT_FROM_UNSIGNED_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments; \
	union hs_short_result result; \
	result.value = 0; \
	arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		result.short_value = function(arguments[0].unsigned_short_value); \
		hs_return(thread_index, result.value); \
	} \
	return; \
}

#define HS_EVALUATE_RETURN_SHORT_FROM_ARGUMENTS(evaluator, arguments_type, expression) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	arguments_type const *arguments; \
	union hs_short_result result; \
	result.value = 0; \
	arguments = (arguments_type const *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		result.short_value = expression; \
		hs_return(thread_index, result.value); \
	} \
	return; \
}

#define HS_EVALUATE_LONG_FROM_LONG(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	long *arguments = hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
		hs_return(thread_index, function(arguments[0])); \
	return; \
}

#define HS_EVALUATE_REAL_FROM_LONG(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	long *arguments = hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		union hs_real_value result; \
		result.real_value = function(arguments[0]); \
		hs_return(thread_index, result.long_value); \
	} \
	return; \
}

#define HS_EVALUATE_REAL_FROM_UNSIGNED_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	unsigned short *arguments = (unsigned short *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		union hs_real_value result; \
		result.real_value = function(arguments[0]); \
		hs_return(thread_index, result.long_value); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_LONG(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	long *arguments = hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0]); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_BOOLEAN(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].boolean_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_UNSIGNED_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].unsigned_short_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_LONG_BOOLEAN(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].long_value, arguments[1].boolean_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_LONG_LONG(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	long *arguments = hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0], arguments[1]); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_SHORT_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].short_value, arguments[1].unsigned_short_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].long_value, arguments[1].unsigned_short_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_UNSIGNED_SHORT(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].unsigned_short_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_BOOLEAN(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].boolean_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_STRING(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	struct hs_arguments_string *arguments = (struct hs_arguments_string *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments->value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_LONG_STRING(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	struct hs_arguments_long_string *arguments = (struct hs_arguments_long_string *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments->value0, arguments->value1); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_LONG_LONG_STRING(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	struct hs_arguments_long_long_string *arguments = (struct hs_arguments_long_long_string *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments->value0, arguments->value1, arguments->value2); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_SHORT_BOOLEAN(evaluator, function) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		function(arguments[0].short_value, arguments[1].boolean_value); \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_VOID_FROM_ARGUMENTS(evaluator, arguments_type, expression) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	arguments_type const *arguments; \
	arguments = (arguments_type const *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		expression; \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define hud_globals_definition_get(index) \
	((struct hud_globals_definition *)tag_get(hud_globals_group_tag, (index)))
#define hud_message_text_definition_get(index) \
	((struct hud_message_text_definition *)tag_get(hud_message_text_group_tag, (index)))

#define HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(evaluator, arguments_type, real_index, expression) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	arguments_type const *arguments; \
	arguments = (arguments_type const *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		real real_argument = arguments[real_index].real_value; \
		expression; \
		hs_return(thread_index, 0); \
	} \
	return; \
}

#define HS_EVALUATE_RETURN_BOOLEAN(evaluator, arguments_type, expression) \
static void evaluator( \
	short function_index, \
	long thread_index, \
	boolean initialize) \
{ \
	arguments_type const *arguments; \
	union hs_boolean_result result; \
	result.value = 0; \
	arguments = (arguments_type const *)hs_macro_function_evaluate(function_index, thread_index, initialize); \
	if (arguments) \
	{ \
		result.boolean = expression; \
		hs_return(thread_index, result.value); \
	} \
	return; \
}

/* ---------- structures */

struct hs_object_list_get_element_arguments
{
	long object_list_index;
	unsigned short element_index;
};

union hs_real_value
{
	real real_value;
	long long_value;
};

union hs_short_result
{
	short short_value;
	long value;
};

union hs_evaluation_argument
{
	long long_value;
	real real_value;
	short short_value;
	unsigned short unsigned_short_value;
	boolean boolean_value;
	char const *string_value;
};

struct hs_arguments_boolean
{
	boolean value;
};

struct hs_arguments_long
{
	long value;
};

struct hs_arguments_word
{
	word value;
};

struct hs_arguments_short_long
{
	short value0;
	word pad0;
	long value1;
};

struct hs_arguments_long_word
{
	long value0;
	word value1;
};

struct hs_arguments_string
{
	char const *value;
};

struct hs_arguments_long_string
{
	long value0;
	char const *value1;
};

struct hs_arguments_long_long
{
	long value0;
	long value1;
};

struct hs_arguments_long_long_string
{
	long value0;
	long value1;
	char const *value2;
};

struct hs_arguments_short_word
{
	short value0;
	word pad0;
	word value1;
};

struct hs_arguments_long_long_long
{
	long value0;
	char const *value1;
	long value2;
};

struct hud_message_text_definition
{
	byte reserved_000[0x20];
	struct tag_block messages;
};

struct hud_message_definition
{
	byte reserved[0x40];
};

struct hud_waypoint_arrow_definition
{
	byte reserved[0x68];
};

typedef void (*hs_token_enumerator)(
	void);

struct hs_function_table_storage
{
	struct hs_function_definition const *functions[418];
	struct profile_section profile;
	hs_token_enumerator token_enumerators[18];
};

struct hs_arguments_long_string_string
{
	long value0;
	char const *value1;
	char const *value2;
};

struct hs_arguments_long_string_long_string
{
	long value0;
	char const *value1;
	long value2;
	char const *value3;
};

struct hs_arguments_long_long_string_word
{
	long value0;
	long value1;
	char const *value2;
	word value3;
};

struct hs_arguments_long_long_long_boolean
{
	long value0;
	long value1;
	char const *value2;
	boolean value3;
};

struct hs_arguments_long_long_long_boolean_word
{
	long value0;
	long value1;
	char const *value2;
	boolean value3;
	byte pad3[3];
	word value4;
};

struct hs_arguments_real
{
	real value;
};

struct hs_arguments_real_real
{
	real value0;
	real value1;
};

struct hs_arguments_real_real_real
{
	real value0;
	real value1;
	real value2;
};

struct hs_arguments_real_real_real_real
{
	real value0;
	real value1;
	real value2;
	real value3;
};

struct hs_arguments_word_word_word_real
{
	word value0;
	word pad0;
	word value1;
	word pad1;
	word value2;
	word pad2;
	real value3;
};

struct hs_arguments_word_word_long_real
{
	word value0;
	word pad0;
	word value1;
	word pad1;
	long value2;
	real value3;
};

struct hs_arguments_real_real_real_real_boolean_real
{
	real value0;
	real value1;
	real value2;
	real value3;
	boolean value4;
	byte pad4[3];
	real value5;
};

struct hs_arguments_long_real_word
{
	long value0;
	real value1;
	word value2;
};

struct hs_arguments_long_real_real
{
	long value0;
	real value1;
	real value2;
};

struct hs_arguments_real_real_real_word
{
	real value0;
	real value1;
	real value2;
	word value3;
};

struct hs_arguments_short_word_real_real_real
{
	short value0;
	word pad0;
	word value1;
	word pad1;
	real value2;
	real value3;
	real value4;
};

struct hs_arguments_word_word_word
{
	word value0;
	word pad0;
	word value1;
	word pad1;
	word value2;
};

struct hs_arguments_word_word_long
{
	word value0;
	word pad0;
	word value1;
	word pad1;
	long value2;
};

struct hs_arguments_long_word_boolean
{
	long value0;
	word value1;
	word pad1;
	boolean value2;
};

union hs_boolean_result
{
	boolean boolean;
	long value;
};

/* ---------- prototypes */

static long alphabetize(
	char const **left,
	char const **right);
void unit_scripting_set_maximum_vitality(
	long unit_index,
	real body_vitality,
	real shield_vitality);
void units_scripting_set_maximum_vitality(
	long object_list_index,
	real body_vitality,
	real shield_vitality);
void unit_scripting_set_current_vitality(
	long unit_index,
	real body_vitality,
	real shield_vitality);
void units_scripting_set_current_vitality(
	long object_list_index,
	real body_vitality,
	real shield_vitality);
void rasterizer_model_ambient_reflection_tint(
	real alpha,
	real red,
	real green,
	real blue);
short unit_scripting_get_grenade_count(
	long unit_index);
void hs_help(
	char const *function_name);
boolean unit_scripting_start_user_animation_list(
	long object_list_index,
	long animation_graph_index,
	char const *animation_name,
	boolean interpolate);
boolean unit_scripting_has_weapon(
	long unit_index,
	long weapon_definition_index);
boolean unit_scripting_has_weapon_readied(
	long unit_index,
	long weapon_definition_index);
void unit_scripting_impervious(
	long object_list_index,
	boolean impervious);
real unit_scripting_get_health(
	long unit_index);
real unit_scripting_get_shield(
	long unit_index);
void hs_doc(
	void);
void hs_dispose_from_old_map(
	void);
static long alphabetize_file_references(
	struct file_reference const *left,
	struct file_reference const *right);
int isspace(
	int character);
boolean hs_scenario_merge(
	struct scenario *scenario,
	struct scenario *source_scenario);
static void hs_allocate(
	void);
static boolean hs_rebuild_source(
	void);
static boolean hs_compile_source(
	void);
boolean hs_scenario_postprocess(
	boolean restore_syntax_data);

/* ---------- constants */

#define MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO 19001

/* ---------- globals */

static short hs_enumeration_result_count = 0;
static short hs_enumeration_maximum_count = 0;
static char const **enumeration_results = NULL;
static char const *hs_enumeration_substring = NULL;
static boolean hs_recompile_pending = FALSE;
static boolean hs_syntax_data_allocated = FALSE;
#define hs_token_enumerators hs_function_table.token_enumerators
struct data_array *hs_syntax_data;
extern long global_scenario_index;
extern struct hs_function_table_storage hs_function_table;
extern short const hs_external_global_count;
extern struct hs_external_global_definition *hs_external_globals[];
extern char const *hs_script_type_names[];
extern char const *hs_type_names[];

/* ---------- structures */

struct hs_function_definition_with_1_parameter
{
	struct hs_function_definition definition;
	short parameter_types[1];
};

struct hs_function_definition_with_2_parameters
{
	struct hs_function_definition definition;
	short parameter_types[2];
};

struct hs_function_definition_with_3_parameters
{
	struct hs_function_definition definition;
	short parameter_types[3];
};

struct hs_function_definition_with_4_parameters
{
	struct hs_function_definition definition;
	short parameter_types[4];
};

struct hs_function_definition_with_5_parameters
{
	struct hs_function_definition definition;
	short parameter_types[5];
};

struct hs_function_definition_with_6_parameters
{
	struct hs_function_definition definition;
	short parameter_types[6];
};

/* ---------- prototypes */

static void ai_debug_sound_point_set_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_debug_speak_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_debug_speak_list_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_debug_teleport_to_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_debug_vocalize_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_globals_ai_active_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_globals_dialogue_triggers_enabled_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_globals_grenades_enabled_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_profile_change_render_spray_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_allegiance_broken_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_allegiance_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_allegiance_remove_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_allow_charge_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_allow_dormant_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_attach_free_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_attach_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_attach_units_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_attack_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_automatic_migration_target_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_berserk_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_braindead_by_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_braindead_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_command_list_advance_by_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_command_list_advance_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_command_list_by_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_command_list_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_command_list_status_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_conversation_advance_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_conversation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_conversation_line_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_conversation_status_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_conversation_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_defend_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_deselect_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_detach_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_detach_units_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_erase_all_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_erase_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_exit_vehicle_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_follow_distance_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_follow_target_ai_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_follow_target_disable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_follow_target_players_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_follow_target_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_force_active_by_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_force_active_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_free_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_free_units_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_go_to_vehicle_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_go_to_vehicle_override_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_going_to_vehicle_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_ignore_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_is_attacking_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_kill_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_kill_silent_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_link_activation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_living_count_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_living_fraction_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_look_at_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_magically_see_encounter_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_magically_see_players_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_magically_see_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_magically_see_units_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_maneuver_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_maneuver_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_migrate_and_speak_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_migrate_by_unit_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_migrate_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_nonswarm_count_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_place_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_playfight_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_prefer_target_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_reconnect_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_renew_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_retreat_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_select_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_set_blind_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_set_current_state_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_set_deaf_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_set_respawn_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_set_return_state_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_set_team_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_spawn_actor_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_status_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_stop_looking_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_strength_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_swarm_count_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_teleport_starting_location_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_teleport_starting_location_if_unsupported_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_timer_expire_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_timer_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_try_to_fight_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_try_to_fight_nothing_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_try_to_fight_player_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_vehicle_encounter_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_vehicle_enterable_actor_type_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_vehicle_enterable_actors_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_vehicle_enterable_disable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_vehicle_enterable_distance_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ai_scripting_vehicle_enterable_team_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void attract_mode_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void breakable_surfaces_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void breakable_surfaces_reset_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheat_active_camouflage_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheat_active_camouflage_local_player_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheat_all_powerups_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheat_all_vehicles_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheat_all_weapons_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheat_teleport_to_camera_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cheats_load_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_set_title_delayed_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_set_title_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_show_letterbox_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_skip_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_skip_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void cinematic_suppress_bsp_object_creation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_dump_memory_by_file_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_dump_memory_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_dump_memory_for_file_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_player_teleport_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_pvs_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_sound_classes_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_sound_classes_set_distances_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void debug_sound_classes_set_wet_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_get_position_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_get_power_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_group_change_only_once_more_set_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_group_get_value_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_group_set_actual_value_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_group_set_desired_value_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_one_sided_set_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_operates_automatically_set_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_set_actual_position_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_set_desired_position_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_set_never_appears_locked_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void device_set_power_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void director_load_camera_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void director_save_camera_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void director_script_camera_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void display_scenario_help_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void enumerate_memory_units_test_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void errors_overflow_suppression_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_all_quiet_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_difficulty_level_get_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_difficulty_level_get_ignore_easy_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_is_cooperative_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_safe_to_save_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_safe_to_speak_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_set_game_variant_from_name_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_state_reverted_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_time_get_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void game_time_set_speed_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void garbage_collect_now_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void global_structure_bsp_index_get_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_damage_new_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_damage_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_doc_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_effect_new_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_effect_new_from_object_marker_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_help_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_not_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_create_anew_containing_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_create_anew_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_create_containing_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_create_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_destroy_all_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_destroy_containing_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_destroy_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_list_get_element_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_set_facing_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_set_permutation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_set_shield_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_object_teleport_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_objects_can_see_flag_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_objects_can_see_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_objects_delete_by_definition_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_objects_predict_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_players_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_print_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_recompile_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_sound_get_gain_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_sound_set_gain_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_teleport_players_not_in_trigger_volume_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_trigger_volume_test_objects_all_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_trigger_volume_test_objects_any_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_activate_team_nav_point_with_flag_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_activate_team_nav_point_with_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_deactivate_team_nav_point_with_flag_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_deactivate_team_nav_point_with_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_unit_activate_nav_point_with_flag_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_unit_activate_nav_point_with_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_unit_deactivate_nav_point_with_flag_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hud_unit_deactivate_nav_point_with_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void lights_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_crash_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_load_core_at_startup_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_load_core_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_load_core_name_at_startup_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_load_core_name_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_lost_map_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_print_version_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_reset_map_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_revert_map_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_save_cancel_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_save_core_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_save_core_name_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_save_map_no_timeout_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_save_map_nonsafe_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_save_map_safe_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_saving_map_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_set_difficulty_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_set_game_connection_to_film_playback_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_set_map_name_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_set_multiplayer_map_name_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_skip_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void main_won_map_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void network_game_client_request_immediate_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void numeric_countdown_timer_get_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void numeric_countdown_timer_restart_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void numeric_countdown_timer_set_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void numeric_countdown_timer_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_beautify_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_can_take_damage_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_cannot_take_damage_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_definition_predict_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_list_count_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_list_from_ai_reference_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_pvs_activate_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_pvs_clear_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_pvs_set_camera_point_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_pvs_set_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_scripting_set_collideable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_set_melee_attack_inhibited_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void object_set_ranged_attack_inhibited_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void objects_dump_memory_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void objects_scripting_attach_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void objects_scripting_detach_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void objects_scripting_set_scale_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player0_joystick_set_is_normal_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player0_look_invert_pitch_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player0_look_pitch_is_inverted_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_add_equipment_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_accept_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_action_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_back_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_grenade_trigger_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_jump_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_look_relative_all_directions_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_look_relative_down_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_look_relative_left_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_look_relative_right_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_look_relative_up_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_move_relative_all_directions_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_primary_trigger_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_reset_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_control_action_test_zoom_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_effect_screen_fade_in_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_effect_screen_fade_out_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_input_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_ui_activate_all_solo_levels_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void player_ui_fast_setup_network_server_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void players_unzoom_all_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void profile_dump_to_file_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void profile_graph_toggle_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void profile_initialize_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void profile_sections_activate_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void profile_sections_deactivate_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void radiosity_hack_find_point_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void radiosity_hack_save_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void radiosity_hack_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void random_range_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_decals_flush_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_fps_accumulate_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_lights_reset_for_new_map_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_model_ambient_reflection_tint_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_screen_effect_set_convolution_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_screen_effect_set_filter_desaturation_tint_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_screen_effect_set_filter_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_screen_effect_set_video_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_screen_effect_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_screen_effect_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_script_screen_effect_set_value_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void rasterizer_set_near_clip_distance_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void real_random_range_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void recorded_animation_get_time_left_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void recorded_animation_kill_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void recorded_animation_play_and_delete_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void recorded_animation_play_and_hover_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void recorded_animation_play_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void render_effects_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void saved_game_files_delete_all_custom_profiles_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scenario_switch_structure_bsp_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scenario_trigger_volume_test_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scenery_animation_start_at_frame_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scenery_animation_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scenery_get_animation_time_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_camera_set_absolute_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_camera_set_animation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_camera_set_dead_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_camera_set_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_camera_set_first_person_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_camera_time_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_foley_predict_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_blink_health_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_blink_motion_sensor_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_blink_shield_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_get_timer_ticks_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_messages_clear_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_pause_timer_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_restart_flashing_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_set_flashing_state_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_set_objective_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_set_state_message_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_set_timer_position_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_set_timer_time_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_set_timer_warning_cutoff_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_show_crosshair_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_show_health_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_show_motion_sensor_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_show_shield_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_show_timer_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_time_code_reset_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_time_code_show_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_hud_time_code_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_looping_sound_set_alternate_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_looping_sound_set_scale_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_looping_sound_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_looping_sound_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_player_control_set_camera_control_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_player_effect_set_rotation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_player_effect_set_rumble_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_player_effect_set_translation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_player_effect_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_player_effect_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_show_hud_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_show_hud_help_text_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_sound_new_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_sound_stop_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripted_sound_time_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripting_magic_melee_attack_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void scripting_set_magic_base_seat_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void sound_cache_flush_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void sound_class_set_gain_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void sound_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void structure_lens_flares_place_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void tag_groups_dump_memory_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void terminal_clear_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void texture_cache_flush_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void ui_widget_debug_show_path_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_aim_without_turning_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_close_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_custom_animation_at_frame_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_get_current_flashlight_state_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_get_custom_animation_time_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_is_playing_custom_animation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_kill_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_kill_silent_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_open_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_can_blink_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_doesnt_drop_items_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_enter_vehicle_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_exit_vehicle_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_get_grenade_count_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_get_health_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_get_shield_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_has_weapon_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_has_weapon_readied_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_impervious_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_set_current_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_set_emotion_animation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_set_maximum_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_set_seat_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_start_user_animation_list_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_suspended_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_unit_driver_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_unit_gunner_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_unit_riders_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_vehicle_test_seat_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_scripting_vehicle_test_seat_list_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_set_desired_flashlight_state_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_set_emotion_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_set_enterable_by_player_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_solo_player_integrated_night_vision_is_active_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_start_user_animation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void unit_stop_custom_animation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void units_scripting_set_current_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void units_scripting_set_maximum_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void units_set_desired_flashlight_state_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void vehicle_hover_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void vehicle_scripting_load_magic_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void vehicle_scripting_unload_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void xbox_set_machine_name_evaluate(
	short function_index,
	long thread_index,
	boolean initialize);
static void hs_enumerate_special_form_names(
	void);
static void hs_enumerate_script_type_names(
	void);
static void hs_enumerate_type_names(
	void);
static void hs_enumerate_function_names(
	void);
static void hs_enumerate_script_names(
	void);
static void hs_enumerate_variable_names(
	void);
static void hs_enumerate_ai_names(
	void);
static void hs_enumerate_ai_command_list_names(
	void);
static void hs_enumerate_starting_profile_names(
	void);
static void hs_enumerate_conversation_names(
	void);
static void hs_enumerate_object_names(
	void);
static void hs_enumerate_trigger_volume_names(
	void);
static void hs_enumerate_cutscene_flag_names(
	void);
static void hs_enumerate_cutscene_camera_point_names(
	void);
static void hs_enumerate_cutscene_title_names(
	void);
static void hs_enumerate_cutscene_recording_names(
	void);
static void hs_enumerate_navpoints(
	void);
static void hs_enumerate_hud_messages(
	void);

extern char const *global_game_difficulty_level_names[];
extern char const *global_game_team_names[];
extern char const *global_actor_type_names[];
extern char const *global_hud_anchor_names[];

/* ---------- globals */

char const *hs_type_names[NUMBER_OF_HS_TYPES]=
{
	"unparsed",
	"special form",
	"function name",
	"passthrough",
	"void",
	"boolean",
	"real",
	"short",
	"long",
	"string",
	"script",
	"trigger_volume",
	"cutscene_flag",
	"cutscene_camera_point",
	"cutscene_title",
	"cutscene_recording",
	"device_group",
	"ai",
	"ai_command_list",
	"starting_profile",
	"conversation",
	"navpoint",
	"hud_message",
	"object_list",
	"sound",
	"effect",
	"damage",
	"looping_sound",
	"animation_graph",
	"actor_variant",
	"damage_effect",
	"object_definition",
	"game_difficulty",
	"team",
	"ai_default_state",
	"actor_type",
	"hud_corner",
	"object",
	"unit",
	"vehicle",
	"weapon",
	"device",
	"scenery",
	"object_name",
	"unit_name",
	"vehicle_name",
	"weapon_name",
	"device_name",
	"scenery_name",
};

char const *hs_script_type_names[]=
{
	"startup",
	"dormant",
	"continuous",
	"static",
	"stub",
};

word const hs_object_type_masks[NUMBER_OF_HS_OBJECT_TYPES]=
{
	0xFFFF,
	0x0003,
	0x0002,
	0x0004,
	0x0380,
	0x0040,
};

tag const hs_tag_reference_type_group_tags[]=
{
	'snd!',
	'effe',
	'jpt!',
	'lsnd',
	'antr',
	'actv',
	'jpt!',
	'obje',
};

short const hs_type_sizes[NUMBER_OF_HS_TYPES]=
{
	0,
	0,
	0,
	0,
	0,
	1,
	4,
	2,
	4,
	4,
	4,
	2,
	2,
	2,
	2,
	2,
	2,
	4,
	2,
	2,
	2,
	2,
	2,
	4,
	4,
	4,
	4,
	4,
	4,
	4,
	4,
	4,
	2,
	2,
	2,
	2,
	2,
	4,
	4,
	4,
	4,
	4,
	4,
	2,
	2,
	2,
	2,
	2,
	2,
};

boolean const _hs_type_boolean_default= FALSE;
real const _hs_type_real_default= 0.0f;
short const _hs_type_short_integer_default= 0;
long const _hs_type_long_integer_default= 0;
char const *_hs_type_string_default= "";
short const _hs_type_script_default= NONE;
short const _hs_type_trigger_volume_default= NONE;
short const _hs_type_cutscene_flag_default= NONE;
short const _hs_type_cutscene_camera_point_default= NONE;
short const _hs_type_cutscene_title_default= NONE;
short const _hs_type_cutscene_recording_default= NONE;
short const _hs_type_device_group_default= NONE;
long const _hs_type_ai_default= NONE;
short const _hs_type_ai_command_list_default= NONE;
short const _hs_type_starting_profile_default= NONE;
short const _hs_type_conversation_default= NONE;
short const _hs_type_navpoint_default= NONE;
short const _hs_type_hud_message_default= NONE;
long const _hs_type_object_list_default= NONE;
long const _hs_type_sound_default= NONE;
long const _hs_type_looping_sound_default= NONE;
long const _hs_type_effect_default= NONE;
long const _hs_type_damage_default= NONE;
long const _hs_type_animation_graph_default= NONE;
long const _hs_type_actor_variant_default= NONE;
long const _hs_type_damage_effect_default= NONE;
long const _hs_type_object_definition_default= NONE;
short const _hs_type_enum_game_difficulty_default= NONE;
short const _hs_type_enum_team_default= NONE;
short const _hs_type_enum_ai_default_state_default= NONE;
short const _hs_type_enum_actor_type_default= NONE;
short const _hs_type_enum_hud_corner_default= NONE;
short const _hs_type_object_name_default= NONE;
long const _hs_type_object_default= NONE;
long const _hs_type_unit_default= NONE;
long const _hs_type_vehicle_default= NONE;
long const _hs_type_weapon_default= NONE;
long const _hs_type_device_default= NONE;
long const _hs_type_scenery_default= NONE;

/* the <script>_definition names: the PC demo and HCEX PDBs (hs.obj statics), in table order; four
 * stems differ, xbox_set_machine_name is HCEX-only, eight hs_*_definition names neither PDB has */
static struct hs_function_definition const begin_definition=
{
	_hs_passthrough,
	0,
	"begin",
	hs_parse_begin,
	hs_evaluate_begin,
	"returns the last expression in a sequence after evaluating the sequence in order.",
	"<expression(s)>",
	0,
};

static struct hs_function_definition const begin_random_definition=
{
	_hs_passthrough,
	0,
	"begin_random",
	hs_parse_begin,
	hs_evaluate_begin_random,
	"evaluates the sequence of expressions in random order and returns the last value evaluated.",
	"<expression(s)>",
	0,
};

static struct hs_function_definition const if_definition=
{
	_hs_passthrough,
	0,
	"if",
	hs_parse_if,
	hs_evaluate_if,
	"returns one of two values based on the value of a condition.",
	"<boolean> <then> [<else>]",
	0,
};

static struct hs_function_definition const cond_definition=
{
	_hs_passthrough,
	0,
	"cond",
	hs_parse_cond,
	NULL,
	"returns the value associated with the first true condition.",
	"(<boolean1> <result1>) [(<boolean2> <result2>) [...]]",
	0,
};

static struct hs_function_definition const set_definition=
{
	_hs_passthrough,
	0,
	"set",
	hs_parse_set,
	hs_evaluate_set,
	"set the value of a global variable.",
	"<variable name> <expression>",
	0,
};

static struct hs_function_definition const and_definition=
{
	_hs_type_boolean,
	0,
	"and",
	hs_parse_logical,
	hs_evaluate_logical,
	"returns true if all specified expressions are true.",
	"<boolean(s)>",
	0,
};

static struct hs_function_definition const or_definition=
{
	_hs_type_boolean,
	0,
	"or",
	hs_parse_logical,
	hs_evaluate_logical,
	"returns true if any specified expressions are true.",
	"<boolean(s)>",
	0,
};

static struct hs_function_definition const add_definition=
{
	_hs_type_real,
	0,
	"+",
	hs_parse_arithmetic,
	hs_evaluate_arithmetic,
	"returns the sum of all specified expressions.",
	"<number(s)>",
	0,
};

static struct hs_function_definition const subtract_definition=
{
	_hs_type_real,
	0,
	"-",
	hs_parse_arithmetic,
	hs_evaluate_arithmetic,
	"returns the difference of two expressions.",
	"<number> <number>",
	0,
};

static struct hs_function_definition const multiply_definition=
{
	_hs_type_real,
	0,
	"*",
	hs_parse_arithmetic,
	hs_evaluate_arithmetic,
	"returns the product of all specified expressions.",
	"<number(s)>",
	0,
};

static struct hs_function_definition const divide_definition=
{
	_hs_type_real,
	0,
	"/",
	hs_parse_arithmetic,
	hs_evaluate_arithmetic,
	"returns the quotient of two expressions.",
	"<number> <number>",
	0,
};

static struct hs_function_definition const min_definition=
{
	_hs_type_real,
	0,
	"min",
	hs_parse_arithmetic,
	hs_evaluate_arithmetic,
	"returns the minimum of all specified expressions.",
	"<number(s)>",
	0,
};

static struct hs_function_definition const max_definition=
{
	_hs_type_real,
	0,
	"max",
	hs_parse_arithmetic,
	hs_evaluate_arithmetic,
	"returns the maximum of all specified expressions.",
	"<number(s)>",
	0,
};

static struct hs_function_definition const equal_definition=
{
	_hs_type_boolean,
	0,
	"=",
	hs_parse_equality,
	hs_evaluate_equality,
	"returns true if two expressions are equal",
	"<expression> <expression>",
	0,
};

static struct hs_function_definition const not_equal_definition=
{
	_hs_type_boolean,
	0,
	"!=",
	hs_parse_equality,
	hs_evaluate_equality,
	"returns true if two expressions are not equal",
	"<expression> <expression>",
	0,
};

static struct hs_function_definition const gt_definition=
{
	_hs_type_boolean,
	0,
	">",
	hs_parse_inequality,
	hs_evaluate_inequality,
	"returns true if the first number is larger than the second.",
	"<number> <number>",
	0,
};

static struct hs_function_definition const lt_definition=
{
	_hs_type_boolean,
	0,
	"<",
	hs_parse_inequality,
	hs_evaluate_inequality,
	"returns true if the first number is smaller than the second.",
	"<number> <number>",
	0,
};

static struct hs_function_definition const gte_definition=
{
	_hs_type_boolean,
	0,
	">=",
	hs_parse_inequality,
	hs_evaluate_inequality,
	"returns true if the first number is larger than or equal to the second.",
	"<number> <number>",
	0,
};

static struct hs_function_definition const lte_definition=
{
	_hs_type_boolean,
	0,
	"<=",
	hs_parse_inequality,
	hs_evaluate_inequality,
	"returns true if the first number is smaller than or equal to the second.",
	"<number> <number>",
	0,
};

static struct hs_function_definition const sleep_definition=
{
	_hs_type_void,
	0,
	"sleep",
	hs_parse_sleep,
	hs_evaluate_sleep,
	"pauses execution of this script (or, optionally, another script) for the specified number of ticks.",
	"<short> [<script>]",
	0,
};

static struct hs_function_definition const sleep_until_definition=
{
	_hs_type_void,
	0,
	"sleep_until",
	hs_parse_sleep_until,
	hs_evaluate_sleep_until,
	"pauses execution of this script until the specified condition is true, checking once per second unless a different number of ticks is specified.",
	"<boolean> [<short>]",
	0,
};

static struct hs_function_definition const wake_definition=
{
	_hs_type_void,
	0,
	"wake",
	hs_parse_wake,
	hs_evaluate_wake,
	"wakes a sleeping script in the next update.",
	"<script name>",
	0,
};

static struct hs_function_definition const inspect_definition=
{
	_hs_type_void,
	0,
	"inspect",
	hs_parse_inspect,
	hs_evaluate_inspect,
	"prints the value of an expression to the screen for debugging purposes.",
	"<expression>",
	0,
};

static struct hs_function_definition const object_to_unit_definition=
{
	_hs_type_unit,
	0,
	"unit",
	hs_parse_object_cast_up,
	hs_evaluate_object_cast_up,
	"converts an object to a unit.",
	"<object>",
	0,
};

static struct hs_function_definition const ai_debug_communication_suppress_definition=
{
	_hs_type_void,
	0,
	"ai_debug_communication_suppress",
	hs_parse_debug_string,
	hs_evaluate_debug_string,
	"suppresses (or stops suppressing) a set of AI communication types.",
	"<string(s)>",
	0,
};

static struct hs_function_definition const ai_debug_communication_ignore_definition=
{
	_hs_type_void,
	0,
	"ai_debug_communication_ignore",
	hs_parse_debug_string,
	hs_evaluate_debug_string,
	"ignores (or stops ignoring) a set of AI communication types when printing out communications.",
	"<string(s)>",
	0,
};

static struct hs_function_definition const ai_debug_communication_focus_definition=
{
	_hs_type_void,
	0,
	"ai_debug_communication_focus",
	hs_parse_debug_string,
	hs_evaluate_debug_string,
	"focuses (or stops focusing) a set of unit vocalization types.",
	"<string(s)>",
	0,
};

static struct hs_function_definition_with_1_parameter const not_definition=
{
	{
		_hs_type_boolean,
		0,
		"not",
		hs_macro_function_parse,
		hs_not_evaluate,
		"returns the opposite of the expression.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const print_definition=
{
	{
		_hs_type_void,
		0,
		"print",
		hs_macro_function_parse,
		hs_print_evaluate,
		"prints a string to the console.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition const players_definition=
{
	_hs_type_object_list,
	0,
	"players",
	hs_macro_function_parse,
	hs_players_evaluate,
	"returns a list of the players",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const volume_teleport_players_not_inside_definition=
{
	{
		_hs_type_void,
		0,
		"volume_teleport_players_not_inside",
		hs_macro_function_parse,
		hs_teleport_players_not_in_trigger_volume_evaluate,
		"moves all players outside a specified trigger volume to a specified flag.",
		NULL,
		2,
		{ _hs_type_trigger_volume },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_2_parameters const volume_test_object_definition=
{
	{
		_hs_type_boolean,
		0,
		"volume_test_object",
		hs_macro_function_parse,
		scenario_trigger_volume_test_object_evaluate,
		"returns true if the specified object is within the specified volume.",
		NULL,
		2,
		{ _hs_type_trigger_volume },
	},
	{ _hs_type_object },
};

static struct hs_function_definition_with_2_parameters const volume_test_objects_definition=
{
	{
		_hs_type_boolean,
		0,
		"volume_test_objects",
		hs_macro_function_parse,
		hs_trigger_volume_test_objects_any_evaluate,
		"returns true if any of the specified objects are within the specified volume.",
		NULL,
		2,
		{ _hs_type_trigger_volume },
	},
	{ _hs_type_object_list },
};

static struct hs_function_definition_with_2_parameters const volume_test_objects_all_definition=
{
	{
		_hs_type_boolean,
		0,
		"volume_test_objects_all",
		hs_macro_function_parse,
		hs_trigger_volume_test_objects_all_evaluate,
		"returns true if any of the specified objects are within the specified volume.",
		NULL,
		2,
		{ _hs_type_trigger_volume },
	},
	{ _hs_type_object_list },
};

static struct hs_function_definition_with_1_parameter const object_create_definition=
{
	{
		_hs_type_void,
		0,
		"object_create",
		hs_macro_function_parse,
		hs_object_create_evaluate,
		"creates an object from the scenario.",
		NULL,
		1,
		{ _hs_type_object_name },
	},
};

static struct hs_function_definition_with_1_parameter const object_destroy_definition=
{
	{
		_hs_type_void,
		0,
		"object_destroy",
		hs_macro_function_parse,
		hs_object_destroy_evaluate,
		"destroys an object.",
		NULL,
		1,
		{ _hs_type_object },
	},
};

static struct hs_function_definition_with_1_parameter const object_create_anew_definition=
{
	{
		_hs_type_void,
		0,
		"object_create_anew",
		hs_macro_function_parse,
		hs_object_create_anew_evaluate,
		"creates an object, destroying it first if it already exists.",
		NULL,
		1,
		{ _hs_type_object_name },
	},
};

static struct hs_function_definition_with_1_parameter const object_create_containing_definition=
{
	{
		_hs_type_void,
		0,
		"object_create_containing",
		hs_macro_function_parse,
		hs_object_create_containing_evaluate,
		"creates all objects from the scenario whose names contain the given substring.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const object_create_anew_containing_definition=
{
	{
		_hs_type_void,
		0,
		"object_create_anew_containing",
		hs_macro_function_parse,
		hs_object_create_anew_containing_evaluate,
		"creates anew all objects from the scenario whose names contain the given substring.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const object_destroy_containing_definition=
{
	{
		_hs_type_void,
		0,
		"object_destroy_containing",
		hs_macro_function_parse,
		hs_object_destroy_containing_evaluate,
		"destroys all objects from the scenario whose names contain the given substring.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition const object_destroy_all_definition=
{
	_hs_type_void,
	0,
	"object_destroy_all",
	hs_macro_function_parse,
	hs_object_destroy_all_evaluate,
	"destroys all non player objects.",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const object_teleport_definition=
{
	{
		_hs_type_void,
		0,
		"object_teleport",
		hs_macro_function_parse,
		hs_object_teleport_evaluate,
		"moves the specified object to the specified flag.",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_2_parameters const object_set_facing_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_facing",
		hs_macro_function_parse,
		hs_object_set_facing_evaluate,
		"turns the specified object in the direction of the specified flag.",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_2_parameters const object_set_shield_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_shield",
		hs_macro_function_parse,
		hs_object_set_shield_evaluate,
		"sets the shield vitality of the specified object (between 0 and 1).",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const object_set_permutation_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_permutation",
		hs_macro_function_parse,
		hs_object_set_permutation_evaluate,
		"sets the desired region (use \"\" for all regions) to the permutation with the given name, e.g. (object_set_permutation flood \"right arm\" ~damaged)",
		NULL,
		3,
		{ _hs_type_object },
	},
	{ _hs_type_string, _hs_type_string },
};

static struct hs_function_definition_with_2_parameters const list_get_definition=
{
	{
		_hs_type_object,
		0,
		"list_get",
		hs_macro_function_parse,
		hs_object_list_get_element_evaluate,
		"returns an item in an object list.",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition_with_1_parameter const list_count_definition=
{
	{
		_hs_type_short_integer,
		0,
		"list_count",
		hs_macro_function_parse,
		object_list_count_evaluate,
		"returns the number of objects in a list",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_2_parameters const effect_new_definition=
{
	{
		_hs_type_void,
		0,
		"effect_new",
		hs_macro_function_parse,
		hs_effect_new_evaluate,
		"starts the specified effect at the specified flag.",
		NULL,
		2,
		{ _hs_type_effect },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_3_parameters const effect_new_on_object_marker_definition=
{
	{
		_hs_type_void,
		0,
		"effect_new_on_object_marker",
		hs_macro_function_parse,
		hs_effect_new_from_object_marker_evaluate,
		"starts the specified effect on the specified object at the specified marker.",
		NULL,
		3,
		{ _hs_type_effect },
	},
	{ _hs_type_object, _hs_type_string },
};

static struct hs_function_definition_with_2_parameters const damage_new_definition=
{
	{
		_hs_type_void,
		0,
		"damage_new",
		hs_macro_function_parse,
		hs_damage_new_evaluate,
		"causes the specified damage at the specified flag.",
		NULL,
		2,
		{ _hs_type_damage },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_2_parameters const damage_object_definition=
{
	{
		_hs_type_void,
		0,
		"damage_object",
		hs_macro_function_parse,
		hs_damage_object_evaluate,
		"causes the specified damage at the specified object.",
		NULL,
		2,
		{ _hs_type_damage },
	},
	{ _hs_type_object },
};

static struct hs_function_definition_with_3_parameters const objects_can_see_object_definition=
{
	{
		_hs_type_boolean,
		0,
		"objects_can_see_object",
		hs_macro_function_parse,
		hs_objects_can_see_object_evaluate,
		"returns true if any of the specified units are looking within the specified number of degrees of the object.",
		NULL,
		3,
		{ _hs_type_object_list },
	},
	{ _hs_type_object, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const objects_can_see_flag_definition=
{
	{
		_hs_type_boolean,
		0,
		"objects_can_see_flag",
		hs_macro_function_parse,
		hs_objects_can_see_flag_evaluate,
		"returns true if any of the specified units are looking within the specified number of degrees of the flag.",
		NULL,
		3,
		{ _hs_type_object_list },
	},
	{ _hs_type_cutscene_flag, _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const objects_delete_by_definition_definition=
{
	{
		_hs_type_void,
		0,
		"objects_delete_by_definition",
		hs_macro_function_parse,
		hs_objects_delete_by_definition_evaluate,
		"deletes all objects of type <definition>",
		NULL,
		1,
		{ _hs_type_object_definition },
	},
};

static struct hs_function_definition_with_2_parameters const sound_set_gain_definition=
{
	{
		_hs_type_void,
		0,
		"sound_set_gain",
		hs_macro_function_parse,
		hs_sound_set_gain_evaluate,
		"absolutely do not use this",
		NULL,
		2,
		{ _hs_type_string },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const sound_get_gain_definition=
{
	{
		_hs_type_real,
		0,
		"sound_get_gain",
		hs_macro_function_parse,
		hs_sound_get_gain_evaluate,
		"absolutely do not use this either",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition const script_recompile_definition=
{
	_hs_type_void,
	0,
	"script_recompile",
	hs_macro_function_parse,
	hs_recompile_evaluate,
	"recompiles scripts.",
	NULL,
	0,
};

static struct hs_function_definition const script_doc_definition=
{
	_hs_type_void,
	0,
	"script_doc",
	hs_macro_function_parse,
	hs_doc_evaluate,
	"saves a file called hs_doc.txt with parameters for all script commands.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const help_definition=
{
	{
		_hs_type_void,
		0,
		"help",
		hs_macro_function_parse,
		hs_help_evaluate,
		"prints a description of the named function.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_2_parameters const random_range_definition=
{
	{
		_hs_type_short_integer,
		0,
		"random_range",
		hs_macro_function_parse,
		random_range_evaluate,
		"returns a random value in the range [lower bound, upper bound)",
		NULL,
		2,
		{ _hs_type_short_integer },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition_with_2_parameters const real_random_range_definition=
{
	{
		_hs_type_real,
		0,
		"real_random_range",
		hs_macro_function_parse,
		real_random_range_evaluate,
		"returns a random value in the range [lower bound, upper bound)",
		NULL,
		2,
		{ _hs_type_real },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const numeric_countdown_timer_set_definition=
{
	{
		_hs_type_void,
		0,
		"numeric_countdown_timer_set",
		hs_macro_function_parse,
		numeric_countdown_timer_set_evaluate,
		"<milliseconds>, <auto_start>",
		NULL,
		2,
		{ _hs_type_long_integer },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const numeric_countdown_timer_get_definition=
{
	{
		_hs_type_short_integer,
		0,
		"numeric_countdown_timer_get",
		hs_macro_function_parse,
		numeric_countdown_timer_get_evaluate,
		"<digit_index>",
		NULL,
		1,
		{ _hs_type_short_integer },
	},
};

static struct hs_function_definition const numeric_countdown_timer_stop_definition=
{
	_hs_type_void,
	0,
	"numeric_countdown_timer_stop",
	hs_macro_function_parse,
	numeric_countdown_timer_stop_evaluate,
	"",
	NULL,
	0,
};

static struct hs_function_definition const numeric_countdown_timer_restart_definition=
{
	_hs_type_void,
	0,
	"numeric_countdown_timer_restart",
	hs_macro_function_parse,
	numeric_countdown_timer_restart_evaluate,
	"",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const breakable_surfaces_enable_definition=
{
	{
		_hs_type_void,
		0,
		"breakable_surfaces_enable",
		hs_macro_function_parse,
		breakable_surfaces_enable_evaluate,
		"enables or disables breakability of all breakable surfaces on level",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_2_parameters const recording_play_definition=
{
	{
		_hs_type_boolean,
		0,
		"recording_play",
		hs_macro_function_parse,
		recorded_animation_play_evaluate,
		"make the specified unit run the specified cutscene recording.",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_cutscene_recording },
};

static struct hs_function_definition_with_2_parameters const recording_play_and_delete_definition=
{
	{
		_hs_type_boolean,
		0,
		"recording_play_and_delete",
		hs_macro_function_parse,
		recorded_animation_play_and_delete_evaluate,
		"make the specified unit run the specified cutscene recording, deletes the unit when the animation finishes.",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_cutscene_recording },
};

static struct hs_function_definition_with_2_parameters const recording_play_and_hover_definition=
{
	{
		_hs_type_boolean,
		0,
		"recording_play_and_hover",
		hs_macro_function_parse,
		recorded_animation_play_and_hover_evaluate,
		"make the specified vehicle run the specified cutscene recording, hovers the vehicle when the animation finishes.",
		NULL,
		2,
		{ _hs_type_vehicle },
	},
	{ _hs_type_cutscene_recording },
};

static struct hs_function_definition_with_1_parameter const recording_kill_definition=
{
	{
		_hs_type_void,
		0,
		"recording_kill",
		hs_macro_function_parse,
		recorded_animation_kill_evaluate,
		"kill the specified unit's cutscene recording.",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const recording_time_definition=
{
	{
		_hs_type_short_integer,
		0,
		"recording_time",
		hs_macro_function_parse,
		recorded_animation_get_time_left_evaluate,
		"return the time remaining in the specified unit's cutscene recording.",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const object_set_ranged_attack_inhibited_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_ranged_attack_inhibited",
		hs_macro_function_parse,
		object_set_ranged_attack_inhibited_evaluate,
		"FALSE prevents object from using ranged attack",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const object_set_melee_attack_inhibited_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_melee_attack_inhibited",
		hs_macro_function_parse,
		object_set_melee_attack_inhibited_evaluate,
		"FALSE prevents object from using melee attack",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition const objects_dump_memory_definition=
{
	_hs_type_void,
	0,
	"objects_dump_memory",
	hs_macro_function_parse,
	objects_dump_memory_evaluate,
	"debugs object memory usage",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const object_set_collideable_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_collideable",
		hs_macro_function_parse,
		object_scripting_set_collideable_evaluate,
		"FALSE prevents any object from colliding with the given object",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_3_parameters const object_set_scale_definition=
{
	{
		_hs_type_void,
		0,
		"object_set_scale",
		hs_macro_function_parse,
		objects_scripting_set_scale_evaluate,
		"sets the scale for a given object and interpolates over the given number of frames to achieve that scale",
		NULL,
		3,
		{ _hs_type_object },
	},
	{ _hs_type_real, _hs_type_short_integer },
};

static struct hs_function_definition_with_4_parameters const objects_attach_definition=
{
	{
		_hs_type_void,
		0,
		"objects_attach",
		hs_macro_function_parse,
		objects_scripting_attach_evaluate,
		"attaches the second object to the first; both strings can be empty",
		NULL,
		4,
		{ _hs_type_object },
	},
	{ _hs_type_string, _hs_type_object, _hs_type_string },
};

static struct hs_function_definition_with_2_parameters const objects_detach_definition=
{
	{
		_hs_type_void,
		0,
		"objects_detach",
		hs_macro_function_parse,
		objects_scripting_detach_evaluate,
		"detaches from the given parent object the given child object",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_object },
};

static struct hs_function_definition const garbage_collect_now_definition=
{
	_hs_type_void,
	0,
	"garbage_collect_now",
	hs_macro_function_parse,
	garbage_collect_now_evaluate,
	"causes all garbage objects except those visible to a player to be collected immediately",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const object_cannot_take_damage_definition=
{
	{
		_hs_type_void,
		0,
		"object_cannot_take_damage",
		hs_macro_function_parse,
		object_cannot_take_damage_evaluate,
		"prevents an object from taking damage",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_1_parameter const object_can_take_damage_definition=
{
	{
		_hs_type_void,
		0,
		"object_can_take_damage",
		hs_macro_function_parse,
		object_can_take_damage_evaluate,
		"allows an object to take damage again",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_2_parameters const object_beautify_definition=
{
	{
		_hs_type_void,
		0,
		"object_beautify",
		hs_macro_function_parse,
		object_beautify_evaluate,
		"makes an object pretty for the remainder of the levels' cutscenes.",
		NULL,
		2,
		{ _hs_type_object },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const objects_predict_definition=
{
	{
		_hs_type_void,
		0,
		"objects_predict",
		hs_macro_function_parse,
		hs_objects_predict_evaluate,
		"loads textures necessary to draw a objects that are about to come on-screen.",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_1_parameter const object_type_predict_definition=
{
	{
		_hs_type_void,
		0,
		"object_type_predict",
		hs_macro_function_parse,
		object_definition_predict_evaluate,
		"loads textures necessary to draw an object that's about to come on-screen.",
		NULL,
		1,
		{ _hs_type_object_definition },
	},
};

static struct hs_function_definition_with_1_parameter const object_pvs_set_object_definition=
{
	{
		_hs_type_void,
		0,
		"object_pvs_set_object",
		hs_macro_function_parse,
		object_pvs_set_object_evaluate,
		"sets the specified object as the special place that activates everything it sees.",
		NULL,
		1,
		{ _hs_type_object },
	},
};

static struct hs_function_definition_with_1_parameter const object_pvs_set_camera_definition=
{
	{
		_hs_type_void,
		0,
		"object_pvs_set_camera",
		hs_macro_function_parse,
		object_pvs_set_camera_point_evaluate,
		"sets the specified cutscene camera point as the special place that activates everything it sees.",
		NULL,
		1,
		{ _hs_type_cutscene_camera_point },
	},
};

static struct hs_function_definition const object_pvs_clear_definition=
{
	_hs_type_void,
	0,
	"object_pvs_clear",
	hs_macro_function_parse,
	object_pvs_clear_evaluate,
	"removes the special place that activates everything it sees.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const object_pvs_activate_definition=
{
	{
		_hs_type_void,
		0,
		"object_pvs_activate",
		hs_macro_function_parse,
		object_pvs_activate_evaluate,
		"just another (old) name for object_pvs_set_object.",
		NULL,
		1,
		{ _hs_type_object },
	},
};

static struct hs_function_definition_with_1_parameter const render_lights_definition=
{
	{
		_hs_type_boolean,
		0,
		"render_lights",
		hs_macro_function_parse,
		lights_enable_evaluate,
		"enables/disables dynamic lights",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const scenery_get_animation_time_definition=
{
	{
		_hs_type_short_integer,
		0,
		"scenery_get_animation_time",
		hs_macro_function_parse,
		scenery_get_animation_time_evaluate,
		"returns the number of ticks remaining in a custom animation (or zero, if the animation is over).",
		NULL,
		1,
		{ _hs_type_scenery },
	},
};

static struct hs_function_definition_with_3_parameters const scenery_animation_start_definition=
{
	{
		_hs_type_void,
		0,
		"scenery_animation_start",
		hs_macro_function_parse,
		scenery_animation_start_evaluate,
		"starts a custom animation playing on a piece of scenery",
		NULL,
		3,
		{ _hs_type_scenery },
	},
	{ _hs_type_animation_graph, _hs_type_string },
};

static struct hs_function_definition_with_4_parameters const scenery_animation_start_at_frame_definition=
{
	{
		_hs_type_void,
		0,
		"scenery_animation_start_at_frame",
		hs_macro_function_parse,
		scenery_animation_start_at_frame_evaluate,
		"starts a custom animation playing on a piece of scenery at a specific frame",
		NULL,
		4,
		{ _hs_type_scenery },
	},
	{ _hs_type_animation_graph, _hs_type_string, _hs_type_short_integer },
};

static struct hs_function_definition_with_1_parameter const render_effects_definition=
{
	{
		_hs_type_void,
		0,
		"render_effects",
		hs_macro_function_parse,
		render_effects_evaluate,
		"",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_2_parameters const unit_can_blink_definition=
{
	{
		_hs_type_void,
		0,
		"unit_can_blink",
		hs_macro_function_parse,
		unit_scripting_can_blink_evaluate,
		"allows a unit to blink or not (units never blink when they are dead)",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const unit_open_definition=
{
	{
		_hs_type_void,
		0,
		"unit_open",
		hs_macro_function_parse,
		unit_open_evaluate,
		"opens the hatches on the given unit",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_close_definition=
{
	{
		_hs_type_void,
		0,
		"unit_close",
		hs_macro_function_parse,
		unit_close_evaluate,
		"closes the hatches on a given unit",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_kill_definition=
{
	{
		_hs_type_void,
		0,
		"unit_kill",
		hs_macro_function_parse,
		unit_kill_evaluate,
		"kills a given unit, no saving throw",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_kill_silent_definition=
{
	{
		_hs_type_void,
		0,
		"unit_kill_silent",
		hs_macro_function_parse,
		unit_kill_silent_evaluate,
		"kills a given unit silently (doesn't make them play their normal death animation or sound)",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_get_custom_animation_time_definition=
{
	{
		_hs_type_short_integer,
		0,
		"unit_get_custom_animation_time",
		hs_macro_function_parse,
		unit_get_custom_animation_time_evaluate,
		"returns the number of ticks remaining in a unit's custom animation (or zero, if the animation is over).",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_stop_custom_animation_definition=
{
	{
		_hs_type_void,
		0,
		"unit_stop_custom_animation",
		hs_macro_function_parse,
		unit_stop_custom_animation_evaluate,
		"stops the custom animation running on the given unit.",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_4_parameters const custom_animation_definition=
{
	{
		_hs_type_boolean,
		0,
		"custom_animation",
		hs_macro_function_parse,
		unit_start_user_animation_evaluate,
		"starts a custom animation playing on a unit (interpolates into animation if last parameter is TRUE)",
		NULL,
		4,
		{ _hs_type_unit },
	},
	{ _hs_type_animation_graph, _hs_type_string, _hs_type_boolean },
};

static struct hs_function_definition_with_4_parameters const custom_animation_list_definition=
{
	{
		_hs_type_boolean,
		0,
		"custom_animation_list",
		hs_macro_function_parse,
		unit_scripting_start_user_animation_list_evaluate,
		"starts a custom animation playing on a unit list (interpolates into animation if last parameter is TRUE)",
		NULL,
		4,
		{ _hs_type_object_list },
	},
	{ _hs_type_animation_graph, _hs_type_string, _hs_type_boolean },
};

static struct hs_function_definition_with_5_parameters const unit_custom_animation_at_frame_definition=
{
	{
		_hs_type_boolean,
		0,
		"unit_custom_animation_at_frame",
		hs_macro_function_parse,
		unit_custom_animation_at_frame_evaluate,
		"starts a custom animation playing on a unit at a specific frame index(interpolates into animation if next to last parameter is TRUE)",
		NULL,
		5,
		{ _hs_type_unit },
	},
	{ _hs_type_animation_graph, _hs_type_string, _hs_type_boolean, _hs_type_short_integer },
};

static struct hs_function_definition_with_1_parameter const unit_is_playing_custom_animation_definition=
{
	{
		_hs_type_boolean,
		0,
		"unit_is_playing_custom_animation",
		hs_macro_function_parse,
		unit_is_playing_custom_animation_evaluate,
		"returns TRUE if the given unit is still playing a custom animation",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const unit_aim_without_turning_definition=
{
	{
		_hs_type_void,
		0,
		"unit_aim_without_turning",
		hs_macro_function_parse,
		unit_aim_without_turning_evaluate,
		"allows a unit to aim in place without turning",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const unit_set_emotion_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_emotion",
		hs_macro_function_parse,
		unit_set_emotion_evaluate,
		"sets a unit's facial expression (-1 is none, other values depend on unit)",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition_with_2_parameters const unit_set_enterable_by_player_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_enterable_by_player",
		hs_macro_function_parse,
		unit_set_enterable_by_player_evaluate,
		"can be used to prevent the player from entering a vehicle",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_3_parameters const unit_enter_vehicle_definition=
{
	{
		_hs_type_void,
		0,
		"unit_enter_vehicle",
		hs_macro_function_parse,
		unit_scripting_enter_vehicle_evaluate,
		"puts the specified unit in the specified vehicle (in the named seat)",
		NULL,
		3,
		{ _hs_type_unit },
	},
	{ _hs_type_vehicle, _hs_type_string },
};

static struct hs_function_definition_with_3_parameters const vehicle_test_seat_list_definition=
{
	{
		_hs_type_boolean,
		0,
		"vehicle_test_seat_list",
		hs_macro_function_parse,
		unit_scripting_vehicle_test_seat_list_evaluate,
		"tests whether the named seat has an object in the object list",
		NULL,
		3,
		{ _hs_type_vehicle },
	},
	{ _hs_type_string, _hs_type_object_list },
};

static struct hs_function_definition_with_3_parameters const vehicle_test_seat_definition=
{
	{
		_hs_type_boolean,
		0,
		"vehicle_test_seat",
		hs_macro_function_parse,
		unit_scripting_vehicle_test_seat_evaluate,
		"tests whether the named seat has a specified unit in it",
		NULL,
		3,
		{ _hs_type_vehicle },
	},
	{ _hs_type_string, _hs_type_unit },
};

static struct hs_function_definition_with_2_parameters const unit_set_emotion_animation_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_emotion_animation",
		hs_macro_function_parse,
		unit_scripting_set_emotion_animation_evaluate,
		"sets the emotion animation to be used for the given unit",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_string },
};

static struct hs_function_definition_with_1_parameter const unit_exit_vehicle_definition=
{
	{
		_hs_type_void,
		0,
		"unit_exit_vehicle",
		hs_macro_function_parse,
		unit_scripting_exit_vehicle_evaluate,
		"makes a unit exit its vehicle",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_3_parameters const unit_set_maximum_vitality_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_maximum_vitality",
		hs_macro_function_parse,
		unit_scripting_set_maximum_vitality_evaluate,
		"sets a unit's maximum body and shield vitality",
		NULL,
		3,
		{ _hs_type_unit },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const units_set_maximum_vitality_definition=
{
	{
		_hs_type_void,
		0,
		"units_set_maximum_vitality",
		hs_macro_function_parse,
		units_scripting_set_maximum_vitality_evaluate,
		"sets a group of units' maximum body and shield vitality",
		NULL,
		3,
		{ _hs_type_object_list },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const unit_set_current_vitality_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_current_vitality",
		hs_macro_function_parse,
		unit_scripting_set_current_vitality_evaluate,
		"sets a unit's current body and shield vitality",
		NULL,
		3,
		{ _hs_type_unit },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const units_set_current_vitality_definition=
{
	{
		_hs_type_void,
		0,
		"units_set_current_vitality",
		hs_macro_function_parse,
		units_scripting_set_current_vitality_evaluate,
		"sets a group of units' current body and shield vitality",
		NULL,
		3,
		{ _hs_type_object_list },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const vehicle_load_magic_definition=
{
	{
		_hs_type_short_integer,
		0,
		"vehicle_load_magic",
		hs_macro_function_parse,
		vehicle_scripting_load_magic_evaluate,
		"makes a list of units (named or by encounter) magically get into a vehicle, in the substring-specified seats (e.g. CD-passenger... empty string matches all seats)",
		NULL,
		3,
		{ _hs_type_unit },
	},
	{ _hs_type_string, _hs_type_object_list },
};

static struct hs_function_definition_with_2_parameters const vehicle_unload_definition=
{
	{
		_hs_type_short_integer,
		0,
		"vehicle_unload",
		hs_macro_function_parse,
		vehicle_scripting_unload_evaluate,
		"makes units get out of a vehicle from the substring-specified seats (e.g. CD-passenger... empty string matches all seats)",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_string },
};

static struct hs_function_definition_with_1_parameter const magic_seat_name_definition=
{
	{
		_hs_type_void,
		0,
		"magic_seat_name",
		hs_macro_function_parse,
		scripting_set_magic_base_seat_evaluate,
		"all units controlled by the player will assume the given seat name (valid values are 'asleep', 'alert', 'stand', 'crouch' and 'flee')",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_2_parameters const unit_set_seat_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_seat",
		hs_macro_function_parse,
		unit_scripting_set_seat_evaluate,
		"this unit will assume the named seat",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_string },
};

static struct hs_function_definition const magic_melee_attack_definition=
{
	_hs_type_void,
	0,
	"magic_melee_attack",
	hs_macro_function_parse,
	scripting_magic_melee_attack_evaluate,
	"causes player's unit to start a melee attack",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const vehicle_riders_definition=
{
	{
		_hs_type_object_list,
		0,
		"vehicle_riders",
		hs_macro_function_parse,
		unit_scripting_unit_riders_evaluate,
		"returns a list of all riders in a vehicle",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const vehicle_driver_definition=
{
	{
		_hs_type_unit,
		0,
		"vehicle_driver",
		hs_macro_function_parse,
		unit_scripting_unit_driver_evaluate,
		"returns the driver of a vehicle",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const vehicle_gunner_definition=
{
	{
		_hs_type_unit,
		0,
		"vehicle_gunner",
		hs_macro_function_parse,
		unit_scripting_unit_gunner_evaluate,
		"returns the gunner of a vehicle",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_get_health_definition=
{
	{
		_hs_type_real,
		0,
		"unit_get_health",
		hs_macro_function_parse,
		unit_scripting_get_health_evaluate,
		"returns the health [0,1] of the unit, returns -1 if the unit does not exists",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_get_shield_definition=
{
	{
		_hs_type_real,
		0,
		"unit_get_shield",
		hs_macro_function_parse,
		unit_scripting_get_shield_evaluate,
		"returns the shield [0,1] of the unit, returns -1 if the unit does not exists",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const unit_get_total_grenade_count_definition=
{
	{
		_hs_type_short_integer,
		0,
		"unit_get_total_grenade_count",
		hs_macro_function_parse,
		unit_scripting_get_grenade_count_evaluate,
		"returns the total number of grenades for the given unit, 0 if it does not exist",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const unit_has_weapon_definition=
{
	{
		_hs_type_boolean,
		0,
		"unit_has_weapon",
		hs_macro_function_parse,
		unit_scripting_has_weapon_evaluate,
		"returns TRUE if the <unit> has <object> as a weapon, FALSE otherwise",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_object_definition },
};

static struct hs_function_definition_with_2_parameters const unit_has_weapon_readied_definition=
{
	{
		_hs_type_boolean,
		0,
		"unit_has_weapon_readied",
		hs_macro_function_parse,
		unit_scripting_has_weapon_readied_evaluate,
		"returns TRUE if the <unit> has <object> as the primary weapon, FALSE otherwise",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_object_definition },
};

static struct hs_function_definition_with_1_parameter const unit_doesnt_drop_items_definition=
{
	{
		_hs_type_void,
		0,
		"unit_doesnt_drop_items",
		hs_macro_function_parse,
		unit_scripting_doesnt_drop_items_evaluate,
		"prevents any of the given units from dropping weapons or grenades when they die",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_2_parameters const unit_impervious_definition=
{
	{
		_hs_type_void,
		0,
		"unit_impervious",
		hs_macro_function_parse,
		unit_scripting_impervious_evaluate,
		"prevents any of the given units from being knocked around or playing ping animations",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const unit_suspended_definition=
{
	{
		_hs_type_void,
		0,
		"unit_suspended",
		hs_macro_function_parse,
		unit_scripting_suspended_evaluate,
		"stops gravity from working on the given unit",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition const unit_solo_player_integrated_night_vision_is_active_definition=
{
	_hs_type_boolean,
	0,
	"unit_solo_player_integrated_night_vision_is_active",
	hs_macro_function_parse,
	unit_solo_player_integrated_night_vision_is_active_evaluate,
	"returns whether the night-vision mode could be activated via the flashlight button",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const units_set_desired_flashlight_state_definition=
{
	{
		_hs_type_void,
		0,
		"units_set_desired_flashlight_state",
		hs_macro_function_parse,
		units_set_desired_flashlight_state_evaluate,
		"sets the units' desired flashlight state",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const unit_set_desired_flashlight_state_definition=
{
	{
		_hs_type_void,
		0,
		"unit_set_desired_flashlight_state",
		hs_macro_function_parse,
		unit_set_desired_flashlight_state_evaluate,
		"sets the unit's desired flashlight state",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const unit_get_current_flashlight_state_definition=
{
	{
		_hs_type_boolean,
		0,
		"unit_get_current_flashlight_state",
		hs_macro_function_parse,
		unit_get_current_flashlight_state_evaluate,
		"gets the unit's current flashlight state",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const device_set_never_appears_locked_definition=
{
	{
		_hs_type_void,
		0,
		"device_set_never_appears_locked",
		hs_macro_function_parse,
		device_set_never_appears_locked_evaluate,
		"changes a machine's never_appears_locked flag, but only if paul is a bastard",
		NULL,
		2,
		{ _hs_type_device },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const device_set_power_definition=
{
	{
		_hs_type_void,
		0,
		"device_set_power",
		hs_macro_function_parse,
		device_set_power_evaluate,
		"immediately sets the power of a named device to the given value",
		NULL,
		2,
		{ _hs_type_device },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const device_get_power_definition=
{
	{
		_hs_type_real,
		0,
		"device_get_power",
		hs_macro_function_parse,
		device_get_power_evaluate,
		"gets the current power of a named device",
		NULL,
		1,
		{ _hs_type_device },
	},
};

static struct hs_function_definition_with_2_parameters const device_set_position_definition=
{
	{
		_hs_type_boolean,
		0,
		"device_set_position",
		hs_macro_function_parse,
		device_set_desired_position_evaluate,
		"set the desired position of the given device (used for devices without explicit device groups)",
		NULL,
		2,
		{ _hs_type_device },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const device_get_position_definition=
{
	{
		_hs_type_real,
		0,
		"device_get_position",
		hs_macro_function_parse,
		device_get_position_evaluate,
		"gets the current position of the given device (used for devices without explicit device groups)",
		NULL,
		1,
		{ _hs_type_device },
	},
};

static struct hs_function_definition_with_2_parameters const device_set_position_immediate_definition=
{
	{
		_hs_type_void,
		0,
		"device_set_position_immediate",
		hs_macro_function_parse,
		device_set_actual_position_evaluate,
		"instantaneously changes the position of the given device (used for devices without explicit device groups",
		NULL,
		2,
		{ _hs_type_device },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const device_group_get_definition=
{
	{
		_hs_type_real,
		0,
		"device_group_get",
		hs_macro_function_parse,
		device_group_get_value_evaluate,
		"returns the desired value of the specified device group.",
		NULL,
		1,
		{ _hs_type_device_group },
	},
};

static struct hs_function_definition_with_2_parameters const device_group_set_definition=
{
	{
		_hs_type_boolean,
		0,
		"device_group_set",
		hs_macro_function_parse,
		device_group_set_desired_value_evaluate,
		"changes the desired value of the specified device group.",
		NULL,
		2,
		{ _hs_type_device_group },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const device_group_set_immediate_definition=
{
	{
		_hs_type_void,
		0,
		"device_group_set_immediate",
		hs_macro_function_parse,
		device_group_set_actual_value_evaluate,
		"instantaneously changes the value of the specified device group.",
		NULL,
		2,
		{ _hs_type_device_group },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const device_one_sided_set_definition=
{
	{
		_hs_type_void,
		0,
		"device_one_sided_set",
		hs_macro_function_parse,
		device_one_sided_set_evaluate,
		"TRUE makes the given device one-sided (only able to be opened from one direction), FALSE makes it two-sided",
		NULL,
		2,
		{ _hs_type_device },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const device_operates_automatically_set_definition=
{
	{
		_hs_type_void,
		0,
		"device_operates_automatically_set",
		hs_macro_function_parse,
		device_operates_automatically_set_evaluate,
		"TRUE makes the given device open automatically when any biped is nearby, FALSE makes it not",
		NULL,
		2,
		{ _hs_type_device },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const device_group_change_only_once_more_set_definition=
{
	{
		_hs_type_void,
		0,
		"device_group_change_only_once_more_set",
		hs_macro_function_parse,
		device_group_change_only_once_more_set_evaluate,
		"TRUE allows a device to change states only once",
		NULL,
		2,
		{ _hs_type_device_group },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition const breakable_surfaces_reset_definition=
{
	_hs_type_void,
	0,
	"breakable_surfaces_reset",
	hs_macro_function_parse,
	breakable_surfaces_reset_evaluate,
	"restores all breakable surfaces",
	NULL,
	0,
};

static struct hs_function_definition const cheat_all_powerups_definition=
{
	_hs_type_void,
	0,
	"cheat_all_powerups",
	hs_macro_function_parse,
	cheat_all_powerups_evaluate,
	"drops all powerups near player",
	NULL,
	0,
};

static struct hs_function_definition const cheat_all_weapons_definition=
{
	_hs_type_void,
	0,
	"cheat_all_weapons",
	hs_macro_function_parse,
	cheat_all_weapons_evaluate,
	"drops all weapons near player",
	NULL,
	0,
};

static struct hs_function_definition const cheat_all_vehicles_definition=
{
	_hs_type_void,
	0,
	"cheat_all_vehicles",
	hs_macro_function_parse,
	cheat_all_vehicles_evaluate,
	"drops all vehicles on player",
	NULL,
	0,
};

static struct hs_function_definition const cheat_teleport_to_camera_definition=
{
	_hs_type_void,
	0,
	"cheat_teleport_to_camera",
	hs_macro_function_parse,
	cheat_teleport_to_camera_evaluate,
	"teleports player to camera location",
	NULL,
	0,
};

static struct hs_function_definition const cheat_active_camouflage_definition=
{
	_hs_type_void,
	0,
	"cheat_active_camouflage",
	hs_macro_function_parse,
	cheat_active_camouflage_evaluate,
	"gives the player active camouflage",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const cheat_active_camouflage_local_player_definition=
{
	{
		_hs_type_void,
		0,
		"cheat_active_camouflage_local_player",
		hs_macro_function_parse,
		cheat_active_camouflage_local_player_evaluate,
		"gives the player active camouflage",
		NULL,
		1,
		{ _hs_type_short_integer },
	},
};

static struct hs_function_definition const cheats_load_definition=
{
	_hs_type_void,
	0,
	"cheats_load",
	hs_macro_function_parse,
	cheats_load_evaluate,
	"reloads the cheats.txt file",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const ai_definition=
{
	{
		_hs_type_void,
		0,
		"ai",
		hs_macro_function_parse,
		ai_globals_ai_active_evaluate,
		"turns all AI on or off.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const ai_dialogue_triggers_definition=
{
	{
		_hs_type_void,
		0,
		"ai_dialogue_triggers",
		hs_macro_function_parse,
		ai_globals_dialogue_triggers_enabled_evaluate,
		"turns impromptu dialogue on or off.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const ai_grenades_definition=
{
	{
		_hs_type_void,
		0,
		"ai_grenades",
		hs_macro_function_parse,
		ai_globals_grenades_enabled_evaluate,
		"turns grenade inventory on or off.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const ai_free_definition=
{
	{
		_hs_type_void,
		0,
		"ai_free",
		hs_macro_function_parse,
		ai_scripting_free_evaluate,
		"removes a group of actors from their encounter and sets them free",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_free_units_definition=
{
	{
		_hs_type_void,
		0,
		"ai_free_units",
		hs_macro_function_parse,
		ai_scripting_free_units_evaluate,
		"removes a set of units from their encounter (if any) and sets them free",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_2_parameters const ai_attach_definition=
{
	{
		_hs_type_void,
		0,
		"ai_attach",
		hs_macro_function_parse,
		ai_scripting_attach_unit_evaluate,
		"attaches the specified unit to the specified encounter.",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_2_parameters const ai_attach_units_definition=
{
	{
		_hs_type_void,
		0,
		"ai_attach_units",
		hs_macro_function_parse,
		ai_scripting_attach_units_evaluate,
		"attaches the specified list of units to the specified encounter.",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_2_parameters const ai_attach_free_definition=
{
	{
		_hs_type_void,
		0,
		"ai_attach_free",
		hs_macro_function_parse,
		ai_scripting_attach_free_evaluate,
		"attaches a unit to a newly created free actor of the specified type",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_actor_variant },
};

static struct hs_function_definition_with_1_parameter const ai_detach_definition=
{
	{
		_hs_type_void,
		0,
		"ai_detach",
		hs_macro_function_parse,
		ai_scripting_detach_unit_evaluate,
		"detaches the specified unit from all AI.",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const ai_detach_units_definition=
{
	{
		_hs_type_void,
		0,
		"ai_detach_units",
		hs_macro_function_parse,
		ai_scripting_detach_units_evaluate,
		"detaches the specified list of units from all AI.",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_1_parameter const ai_place_definition=
{
	{
		_hs_type_void,
		0,
		"ai_place",
		hs_macro_function_parse,
		ai_scripting_place_evaluate,
		"places the specified encounter on the map.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_kill_definition=
{
	{
		_hs_type_void,
		0,
		"ai_kill",
		hs_macro_function_parse,
		ai_scripting_kill_evaluate,
		"instantly kills the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_kill_silent_definition=
{
	{
		_hs_type_void,
		0,
		"ai_kill_silent",
		hs_macro_function_parse,
		ai_scripting_kill_silent_evaluate,
		"instantly and silently (no animation or sound played) kills the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_erase_definition=
{
	{
		_hs_type_void,
		0,
		"ai_erase",
		hs_macro_function_parse,
		ai_scripting_erase_evaluate,
		"erases the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition const ai_erase_all_definition=
{
	_hs_type_void,
	0,
	"ai_erase_all",
	hs_macro_function_parse,
	ai_scripting_erase_all_evaluate,
	"erases all AI.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const ai_select_definition=
{
	{
		_hs_type_void,
		0,
		"ai_select",
		hs_macro_function_parse,
		ai_scripting_select_evaluate,
		"selects the specified encounter.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition const ai_deselect_definition=
{
	_hs_type_void,
	0,
	"ai_deselect",
	hs_macro_function_parse,
	ai_scripting_deselect_evaluate,
	"clears the selected encounter.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const ai_spawn_actor_definition=
{
	{
		_hs_type_void,
		0,
		"ai_spawn_actor",
		hs_macro_function_parse,
		ai_scripting_spawn_actor_evaluate,
		"spawns a single actor in the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_set_respawn_definition=
{
	{
		_hs_type_void,
		0,
		"ai_set_respawn",
		hs_macro_function_parse,
		ai_scripting_set_respawn_evaluate,
		"enables or disables respawning in the specified encounter.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_set_deaf_definition=
{
	{
		_hs_type_void,
		0,
		"ai_set_deaf",
		hs_macro_function_parse,
		ai_scripting_set_deaf_evaluate,
		"enables or disables hearing for actors in the specified encounter.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_set_blind_definition=
{
	{
		_hs_type_void,
		0,
		"ai_set_blind",
		hs_macro_function_parse,
		ai_scripting_set_blind_evaluate,
		"enables or disables sight for actors in the specified encounter.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_magically_see_encounter_definition=
{
	{
		_hs_type_void,
		0,
		"ai_magically_see_encounter",
		hs_macro_function_parse,
		ai_scripting_magically_see_encounter_evaluate,
		"makes one encounter magically aware of another.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_1_parameter const ai_magically_see_players_definition=
{
	{
		_hs_type_void,
		0,
		"ai_magically_see_players",
		hs_macro_function_parse,
		ai_scripting_magically_see_players_evaluate,
		"makes an encounter magically aware of nearby players.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_magically_see_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_magically_see_unit",
		hs_macro_function_parse,
		ai_scripting_magically_see_unit_evaluate,
		"makes an encounter magically aware of the specified unit.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_unit },
};

static struct hs_function_definition_with_2_parameters const ai_magically_see_units_definition=
{
	{
		_hs_type_void,
		0,
		"ai_magically_see_units",
		hs_macro_function_parse,
		ai_scripting_magically_see_units_evaluate,
		"makes an encounter magically aware of the specified set of units.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_object_list },
};

static struct hs_function_definition_with_1_parameter const ai_timer_start_definition=
{
	{
		_hs_type_void,
		0,
		"ai_timer_start",
		hs_macro_function_parse,
		ai_scripting_timer_start_evaluate,
		"makes a squad's delay timer start counting.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_timer_expire_definition=
{
	{
		_hs_type_void,
		0,
		"ai_timer_expire",
		hs_macro_function_parse,
		ai_scripting_timer_expire_evaluate,
		"makes a squad's delay timer expire and releases them to enter combat.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_attack_definition=
{
	{
		_hs_type_void,
		0,
		"ai_attack",
		hs_macro_function_parse,
		ai_scripting_attack_evaluate,
		"makes the specified platoon(s) go into the attacking state.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_defend_definition=
{
	{
		_hs_type_void,
		0,
		"ai_defend",
		hs_macro_function_parse,
		ai_scripting_defend_evaluate,
		"makes the specified platoon(s) go into the defending state.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_retreat_definition=
{
	{
		_hs_type_void,
		0,
		"ai_retreat",
		hs_macro_function_parse,
		ai_scripting_retreat_evaluate,
		"makes all squads in the specified platoon(s) maneuver to their designated maneuver squads.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_maneuver_definition=
{
	{
		_hs_type_void,
		0,
		"ai_maneuver",
		hs_macro_function_parse,
		ai_scripting_maneuver_evaluate,
		"makes all squads in the specified platoon(s) maneuver to their designated maneuver squads.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_maneuver_enable_definition=
{
	{
		_hs_type_void,
		0,
		"ai_maneuver_enable",
		hs_macro_function_parse,
		ai_scripting_maneuver_enable_evaluate,
		"enables or disables the maneuver/retreat rule for an encounter or platoon. the rule will still trigger, but none of the actors will be given the order to change squads.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_migrate_definition=
{
	{
		_hs_type_void,
		0,
		"ai_migrate",
		hs_macro_function_parse,
		ai_scripting_migrate_evaluate,
		"makes all or part of an encounter move to another encounter.",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_3_parameters const ai_migrate_and_speak_definition=
{
	{
		_hs_type_void,
		0,
		"ai_migrate_and_speak",
		hs_macro_function_parse,
		ai_scripting_migrate_and_speak_evaluate,
		"makes all or part of an encounter move to another encounter, and say their 'advance' or 'retreat' speech lines.",
		NULL,
		3,
		{ _hs_type_ai },
	},
	{ _hs_type_ai, _hs_type_string },
};

static struct hs_function_definition_with_2_parameters const ai_migrate_by_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_migrate_by_unit",
		hs_macro_function_parse,
		ai_scripting_migrate_by_unit_evaluate,
		"makes a named vehicle or group of units move to another encounter.",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_2_parameters const ai_allegiance_definition=
{
	{
		_hs_type_void,
		0,
		"ai_allegiance",
		hs_macro_function_parse,
		ai_scripting_allegiance_evaluate,
		"creates an allegiance between two teams.",
		NULL,
		2,
		{ _hs_type_enum_team },
	},
	{ _hs_type_enum_team },
};

static struct hs_function_definition_with_2_parameters const ai_allegiance_remove_definition=
{
	{
		_hs_type_void,
		0,
		"ai_allegiance_remove",
		hs_macro_function_parse,
		ai_scripting_allegiance_remove_evaluate,
		"destroys an allegiance between two teams.",
		NULL,
		2,
		{ _hs_type_enum_team },
	},
	{ _hs_type_enum_team },
};

static struct hs_function_definition_with_3_parameters const ai_go_to_vehicle_definition=
{
	{
		_hs_type_void,
		0,
		"ai_go_to_vehicle",
		hs_macro_function_parse,
		ai_scripting_go_to_vehicle_evaluate,
		"tells a group of actors to get into a vehicle, in the substring-specified seats (e.g. passenger for pelican)... does not interrupt any actors who are already going to vehicles",
		NULL,
		3,
		{ _hs_type_ai },
	},
	{ _hs_type_unit, _hs_type_string },
};

static struct hs_function_definition_with_3_parameters const ai_go_to_vehicle_override_definition=
{
	{
		_hs_type_void,
		0,
		"ai_go_to_vehicle_override",
		hs_macro_function_parse,
		ai_scripting_go_to_vehicle_override_evaluate,
		"tells a group of actors to get into a vehicle, in the substring-specified seats (e.g. passenger for pelican)... NB: any actors who are already going to vehicles will stop and go to this one instead!",
		NULL,
		3,
		{ _hs_type_ai },
	},
	{ _hs_type_unit, _hs_type_string },
};

static struct hs_function_definition_with_1_parameter const ai_exit_vehicle_definition=
{
	{
		_hs_type_void,
		0,
		"ai_exit_vehicle",
		hs_macro_function_parse,
		ai_scripting_exit_vehicle_evaluate,
		"tells a group of actors to get out of any vehicles that they are in",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_braindead_definition=
{
	{
		_hs_type_void,
		0,
		"ai_braindead",
		hs_macro_function_parse,
		ai_scripting_braindead_evaluate,
		"makes a group of actors braindead, or restores them to life (in their initial state)",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_braindead_by_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_braindead_by_unit",
		hs_macro_function_parse,
		ai_scripting_braindead_by_unit_evaluate,
		"makes a list of objects braindead, or restores them to life. if you pass in a vehicle index, it makes all actors in that vehicle braindead (including any built-in guns)",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_disregard_definition=
{
	{
		_hs_type_void,
		0,
		"ai_disregard",
		hs_macro_function_parse,
		ai_scripting_ignore_evaluate,
		"if TRUE, forces all actors to completely disregard the specified units, otherwise lets them acknowledge the units again",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_prefer_target_definition=
{
	{
		_hs_type_void,
		0,
		"ai_prefer_target",
		hs_macro_function_parse,
		ai_scripting_prefer_target_evaluate,
		"if TRUE, *ALL* enemies will prefer to attack the specified units. if FALSE, removes the preference.",
		NULL,
		2,
		{ _hs_type_object_list },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const ai_teleport_to_starting_location_definition=
{
	{
		_hs_type_void,
		0,
		"ai_teleport_to_starting_location",
		hs_macro_function_parse,
		ai_scripting_teleport_starting_location_evaluate,
		"teleports a group of actors to the starting locations of their current squad(s)",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_teleport_to_starting_location_if_unsupported_definition=
{
	{
		_hs_type_void,
		0,
		"ai_teleport_to_starting_location_if_unsupported",
		hs_macro_function_parse,
		ai_scripting_teleport_starting_location_if_unsupported_evaluate,
		"teleports a group of actors to the starting locations of their current squad(s), only if they are not supported by solid ground (i.e. if they are falling after switching BSPs)",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_renew_definition=
{
	{
		_hs_type_void,
		0,
		"ai_renew",
		hs_macro_function_parse,
		ai_scripting_renew_evaluate,
		"refreshes the health and grenade count of a group of actors, so they are as good as new",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_try_to_fight_nothing_definition=
{
	{
		_hs_type_void,
		0,
		"ai_try_to_fight_nothing",
		hs_macro_function_parse,
		ai_scripting_try_to_fight_nothing_evaluate,
		"removes the preferential target setting from a group of actors",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_try_to_fight_definition=
{
	{
		_hs_type_void,
		0,
		"ai_try_to_fight",
		hs_macro_function_parse,
		ai_scripting_try_to_fight_evaluate,
		"causes a group of actors to preferentially target another group of actors",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_1_parameter const ai_try_to_fight_player_definition=
{
	{
		_hs_type_void,
		0,
		"ai_try_to_fight_player",
		hs_macro_function_parse,
		ai_scripting_try_to_fight_player_evaluate,
		"causes a group of actors to preferentially target the player",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_command_list_definition=
{
	{
		_hs_type_void,
		0,
		"ai_command_list",
		hs_macro_function_parse,
		ai_scripting_command_list_evaluate,
		"tells a group of actors to begin executing the specified command list",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_ai_command_list },
};

static struct hs_function_definition_with_2_parameters const ai_command_list_by_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_command_list_by_unit",
		hs_macro_function_parse,
		ai_scripting_command_list_by_unit_evaluate,
		"tells a named unit to begin executing the specified command list",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_ai_command_list },
};

static struct hs_function_definition_with_1_parameter const ai_command_list_advance_definition=
{
	{
		_hs_type_void,
		0,
		"ai_command_list_advance",
		hs_macro_function_parse,
		ai_scripting_command_list_advance_evaluate,
		"tells a group of actors that are running a command list that they may advance further along the list (if they are waiting for a stimulus)",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_command_list_advance_by_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_command_list_advance_by_unit",
		hs_macro_function_parse,
		ai_scripting_command_list_advance_by_unit_evaluate,
		"just like ai_command_list_advance but operates upon a unit instead",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const ai_force_active_definition=
{
	{
		_hs_type_void,
		0,
		"ai_force_active",
		hs_macro_function_parse,
		ai_scripting_force_active_evaluate,
		"forces an encounter to remain active (i.e. not freeze in place) even if there are no players nearby",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_force_active_by_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_force_active_by_unit",
		hs_macro_function_parse,
		ai_scripting_force_active_by_unit_evaluate,
		"forces a named actor that is NOT in an encounter to remain active (i.e. not freeze in place) even if there are no players nearby",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_set_return_state_definition=
{
	{
		_hs_type_void,
		0,
		"ai_set_return_state",
		hs_macro_function_parse,
		ai_scripting_set_return_state_evaluate,
		"sets the state that a group of actors will return to when they have nothing to do",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_enum_ai_default_state },
};

static struct hs_function_definition_with_2_parameters const ai_set_current_state_definition=
{
	{
		_hs_type_void,
		0,
		"ai_set_current_state",
		hs_macro_function_parse,
		ai_scripting_set_current_state_evaluate,
		"sets the current state of a group of actors. WARNING: may have unpredictable results on actors that are in combat",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_enum_ai_default_state },
};

static struct hs_function_definition_with_2_parameters const ai_playfight_definition=
{
	{
		_hs_type_void,
		0,
		"ai_playfight",
		hs_macro_function_parse,
		ai_scripting_playfight_evaluate,
		"sets an encounter to be playfighting or not",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition const ai_reconnect_definition=
{
	_hs_type_void,
	0,
	"ai_reconnect",
	hs_macro_function_parse,
	ai_scripting_reconnect_evaluate,
	"reconnects all AI information to the current structure bsp (use this after you create encounters or command lists in sapien, or place new firing points or command list points)",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const ai_vehicle_encounter_definition=
{
	{
		_hs_type_void,
		0,
		"ai_vehicle_encounter",
		hs_macro_function_parse,
		ai_scripting_vehicle_encounter_evaluate,
		"sets a vehicle to 'belong' to a particular encounter/squad. any actors who get into the vehicle will be placed in this squad. NB: vehicles potentially drivable by multiple teams need their own encounter!",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_2_parameters const ai_vehicle_enterable_distance_definition=
{
	{
		_hs_type_void,
		0,
		"ai_vehicle_enterable_distance",
		hs_macro_function_parse,
		ai_scripting_vehicle_enterable_distance_evaluate,
		"sets a vehicle as being impulsively enterable for actors within a certain distance",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const ai_vehicle_enterable_team_definition=
{
	{
		_hs_type_void,
		0,
		"ai_vehicle_enterable_team",
		hs_macro_function_parse,
		ai_scripting_vehicle_enterable_team_evaluate,
		"sets a vehicle as being impulsively enterable for actors on a certain team",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_enum_team },
};

static struct hs_function_definition_with_2_parameters const ai_vehicle_enterable_actor_type_definition=
{
	{
		_hs_type_void,
		0,
		"ai_vehicle_enterable_actor_type",
		hs_macro_function_parse,
		ai_scripting_vehicle_enterable_actor_type_evaluate,
		"sets a vehicle as being impulsively enterable for actors of a certain type (grunt, elite, marine etc)",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_enum_actor_type },
};

static struct hs_function_definition_with_2_parameters const ai_vehicle_enterable_actors_definition=
{
	{
		_hs_type_void,
		0,
		"ai_vehicle_enterable_actors",
		hs_macro_function_parse,
		ai_scripting_vehicle_enterable_actors_evaluate,
		"sets a vehicle as being impulsively enterable for a certain encounter/squad of actors",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_1_parameter const ai_vehicle_enterable_disable_definition=
{
	{
		_hs_type_void,
		0,
		"ai_vehicle_enterable_disable",
		hs_macro_function_parse,
		ai_scripting_vehicle_enterable_disable_evaluate,
		"disables actors from impulsively getting into a vehicle (this is the default state for newly placed vehicles)",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const ai_look_at_object_definition=
{
	{
		_hs_type_void,
		0,
		"ai_look_at_object",
		hs_macro_function_parse,
		ai_scripting_look_at_object_evaluate,
		"tells an actor to look at an object until further notice",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_object },
};

static struct hs_function_definition_with_1_parameter const ai_stop_looking_definition=
{
	{
		_hs_type_void,
		0,
		"ai_stop_looking",
		hs_macro_function_parse,
		ai_scripting_stop_looking_evaluate,
		"tells an actor to stop looking at whatever it's looking at",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_2_parameters const ai_automatic_migration_target_definition=
{
	{
		_hs_type_void,
		0,
		"ai_automatic_migration_target",
		hs_macro_function_parse,
		ai_scripting_automatic_migration_target_evaluate,
		"enables or disables a squad as being an automatic migration target",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const ai_follow_target_disable_definition=
{
	{
		_hs_type_void,
		0,
		"ai_follow_target_disable",
		hs_macro_function_parse,
		ai_scripting_follow_target_disable_evaluate,
		"turns off following for an encounter",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_follow_target_players_definition=
{
	{
		_hs_type_void,
		0,
		"ai_follow_target_players",
		hs_macro_function_parse,
		ai_scripting_follow_target_players_evaluate,
		"sets the follow target for an encounter to be the closest player",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_2_parameters const ai_follow_target_unit_definition=
{
	{
		_hs_type_void,
		0,
		"ai_follow_target_unit",
		hs_macro_function_parse,
		ai_scripting_follow_target_unit_evaluate,
		"sets the follow target for an encounter to be a specific unit",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_unit },
};

static struct hs_function_definition_with_2_parameters const ai_follow_target_ai_definition=
{
	{
		_hs_type_void,
		0,
		"ai_follow_target_ai",
		hs_macro_function_parse,
		ai_scripting_follow_target_ai_evaluate,
		"sets the follow target for an encounter to be a group of AI (encounter, squad or platoon)",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_2_parameters const ai_follow_distance_definition=
{
	{
		_hs_type_void,
		0,
		"ai_follow_distance",
		hs_macro_function_parse,
		ai_scripting_follow_distance_evaluate,
		"sets the distance threshold which will cause squads to migrate when following someone",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const ai_conversation_stop_definition=
{
	{
		_hs_type_void,
		0,
		"ai_conversation_stop",
		hs_macro_function_parse,
		ai_scripting_conversation_stop_evaluate,
		"stops a conversation from playing or trying to play",
		NULL,
		1,
		{ _hs_type_conversation },
	},
};

static struct hs_function_definition_with_1_parameter const ai_conversation_advance_definition=
{
	{
		_hs_type_void,
		0,
		"ai_conversation_advance",
		hs_macro_function_parse,
		ai_scripting_conversation_advance_evaluate,
		"tells a conversation that it may advance",
		NULL,
		1,
		{ _hs_type_conversation },
	},
};

static struct hs_function_definition_with_2_parameters const ai_link_activation_definition=
{
	{
		_hs_type_void,
		0,
		"ai_link_activation",
		hs_macro_function_parse,
		ai_scripting_link_activation_evaluate,
		"links the first encounter so that it will be made active whenever it detects that the second encounter is active",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_ai },
};

static struct hs_function_definition_with_2_parameters const ai_berserk_definition=
{
	{
		_hs_type_void,
		0,
		"ai_berserk",
		hs_macro_function_parse,
		ai_scripting_berserk_evaluate,
		"forces a group of actors to start or stop berserking",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_set_team_definition=
{
	{
		_hs_type_void,
		0,
		"ai_set_team",
		hs_macro_function_parse,
		ai_scripting_set_team_evaluate,
		"makes an encounter change to a new team",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_enum_team },
};

static struct hs_function_definition_with_2_parameters const ai_allow_charge_definition=
{
	{
		_hs_type_void,
		0,
		"ai_allow_charge",
		hs_macro_function_parse,
		ai_scripting_allow_charge_evaluate,
		"either enables or disables charging behavior for a group of actors",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const ai_allow_dormant_definition=
{
	{
		_hs_type_void,
		0,
		"ai_allow_dormant",
		hs_macro_function_parse,
		ai_scripting_allow_dormant_evaluate,
		"either enables or disables automatic dormancy for a group of actors",
		NULL,
		2,
		{ _hs_type_ai },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const ai_is_attacking_definition=
{
	{
		_hs_type_boolean,
		0,
		"ai_is_attacking",
		hs_macro_function_parse,
		ai_scripting_is_attacking_evaluate,
		"returns whether a platoon is in the attacking mode (or if an encounter is specified, returns whether any platoon in that encounter is attacking)",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_command_list_status_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_command_list_status",
		hs_macro_function_parse,
		ai_scripting_command_list_status_evaluate,
		"gets the status of a number of units running command lists: 0 = none, 1 = finished command list, 2 = waiting for stimulus, 3 = running command list",
		NULL,
		1,
		{ _hs_type_object_list },
	},
};

static struct hs_function_definition_with_1_parameter const ai_going_to_vehicle_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_going_to_vehicle",
		hs_macro_function_parse,
		ai_scripting_going_to_vehicle_evaluate,
		"return the number of actors that are still trying to get into the specified vehicle",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const ai_living_count_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_living_count",
		hs_macro_function_parse,
		ai_scripting_living_count_evaluate,
		"return the number of living actors in the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_living_fraction_definition=
{
	{
		_hs_type_real,
		0,
		"ai_living_fraction",
		hs_macro_function_parse,
		ai_scripting_living_fraction_evaluate,
		"return the fraction [0-1] of living actors in the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_strength_definition=
{
	{
		_hs_type_real,
		0,
		"ai_strength",
		hs_macro_function_parse,
		ai_scripting_strength_evaluate,
		"return the current strength (average body vitality from 0-1) of the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_swarm_count_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_swarm_count",
		hs_macro_function_parse,
		ai_scripting_swarm_count_evaluate,
		"return the number of swarm actors in the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_nonswarm_count_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_nonswarm_count",
		hs_macro_function_parse,
		ai_scripting_nonswarm_count_evaluate,
		"return the number of non-swarm actors in the specified encounter and/or squad.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_actors_definition=
{
	{
		_hs_type_object_list,
		0,
		"ai_actors",
		hs_macro_function_parse,
		object_list_from_ai_reference_evaluate,
		"converts an ai reference to an object list.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_status_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_status",
		hs_macro_function_parse,
		ai_scripting_status_evaluate,
		"returns the most severe combat status of a group of actors (0=inactive, 1=noncombat, 2=guarding, 3=search/suspicious, 4=definite enemy(heard or magic awareness), 5=visible enemy, 6=engaging in combat.",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_conversation_definition=
{
	{
		_hs_type_boolean,
		0,
		"ai_conversation",
		hs_macro_function_parse,
		ai_scripting_conversation_evaluate,
		"tries to add an entry to the list of conversations waiting to play. returns FALSE if the required units could not be found to play the conversation, or if the player is too far away and the 'delay' flag is not set.",
		NULL,
		1,
		{ _hs_type_conversation },
	},
};

static struct hs_function_definition_with_1_parameter const ai_conversation_line_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_conversation_line",
		hs_macro_function_parse,
		ai_scripting_conversation_line_evaluate,
		"returns which line the conversation is currently playing, or 999 if the conversation is not currently playing",
		NULL,
		1,
		{ _hs_type_conversation },
	},
};

static struct hs_function_definition_with_1_parameter const ai_conversation_status_definition=
{
	{
		_hs_type_short_integer,
		0,
		"ai_conversation_status",
		hs_macro_function_parse,
		ai_scripting_conversation_status_evaluate,
		"returns the status of a conversation (0=none, 1=trying to begin, 2=waiting for guys to get in position, 3=playing, 4=waiting to advance, 5=could not begin, 6=finished successfully, 7=aborted midway",
		NULL,
		1,
		{ _hs_type_conversation },
	},
};

static struct hs_function_definition_with_2_parameters const ai_allegiance_broken_definition=
{
	{
		_hs_type_boolean,
		0,
		"ai_allegiance_broken",
		hs_macro_function_parse,
		ai_scripting_allegiance_broken_evaluate,
		"returns whether two teams have an allegiance that is currently broken by traitorous behavior",
		NULL,
		2,
		{ _hs_type_enum_team },
	},
	{ _hs_type_enum_team },
};

static struct hs_function_definition_with_1_parameter const camera_control_definition=
{
	{
		_hs_type_void,
		0,
		"camera_control",
		hs_macro_function_parse,
		director_script_camera_evaluate,
		"toggles script control of the camera.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_2_parameters const camera_set_definition=
{
	{
		_hs_type_void,
		0,
		"camera_set",
		hs_macro_function_parse,
		scripted_camera_set_absolute_evaluate,
		"moves the camera to the specified camera point over the specified number of ticks.",
		NULL,
		2,
		{ _hs_type_cutscene_camera_point },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition_with_3_parameters const camera_set_relative_definition=
{
	{
		_hs_type_void,
		0,
		"camera_set_relative",
		hs_macro_function_parse,
		scripted_camera_set_evaluate,
		"moves the camera to the specified camera point over the specified number of ticks (position is relative to the specified object).",
		NULL,
		3,
		{ _hs_type_cutscene_camera_point },
	},
	{ _hs_type_short_integer, _hs_type_object },
};

static struct hs_function_definition_with_2_parameters const camera_set_animation_definition=
{
	{
		_hs_type_void,
		0,
		"camera_set_animation",
		hs_macro_function_parse,
		scripted_camera_set_animation_evaluate,
		"begins a prerecorded camera animation.",
		NULL,
		2,
		{ _hs_type_animation_graph },
	},
	{ _hs_type_string },
};

static struct hs_function_definition_with_1_parameter const camera_set_first_person_definition=
{
	{
		_hs_type_void,
		0,
		"camera_set_first_person",
		hs_macro_function_parse,
		scripted_camera_set_first_person_evaluate,
		"makes the scripted camera follow a unit.",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition_with_1_parameter const camera_set_dead_definition=
{
	{
		_hs_type_void,
		0,
		"camera_set_dead",
		hs_macro_function_parse,
		scripted_camera_set_dead_evaluate,
		"makes the scripted camera zoom out around a unit as if it were dead.",
		NULL,
		1,
		{ _hs_type_unit },
	},
};

static struct hs_function_definition const camera_time_definition=
{
	_hs_type_short_integer,
	0,
	"camera_time",
	hs_macro_function_parse,
	scripted_camera_time_evaluate,
	"returns the number of ticks remaining in the current camera interpolation.",
	NULL,
	0,
};

static struct hs_function_definition const debug_camera_save_definition=
{
	_hs_type_void,
	0,
	"debug_camera_save",
	hs_macro_function_parse,
	director_save_camera_evaluate,
	"saves the camera position and facing.",
	NULL,
	0,
};

static struct hs_function_definition const debug_camera_load_definition=
{
	_hs_type_void,
	0,
	"debug_camera_load",
	hs_macro_function_parse,
	director_load_camera_evaluate,
	"loads the saved camera position and facing.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const game_speed_definition=
{
	{
		_hs_type_void,
		0,
		"game_speed",
		hs_macro_function_parse,
		game_time_set_speed_evaluate,
		"changes the game speed.",
		NULL,
		1,
		{ _hs_type_real },
	},
};

static struct hs_function_definition_with_1_parameter const game_variant_definition=
{
	{
		_hs_type_void,
		0,
		"game_variant",
		hs_macro_function_parse,
		game_set_game_variant_from_name_evaluate,
		"set the game engine",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition const game_time_definition=
{
	_hs_type_long_integer,
	0,
	"game_time",
	hs_macro_function_parse,
	game_time_get_evaluate,
	"gets ticks elapsed since the start of the game.",
	NULL,
	0,
};

static struct hs_function_definition const game_difficulty_get_definition=
{
	_hs_type_enum_game_difficulty,
	0,
	"game_difficulty_get",
	hs_macro_function_parse,
	game_difficulty_level_get_ignore_easy_evaluate,
	"returns the current difficulty setting, but lies to you and will never return easy, instead returning normal",
	NULL,
	0,
};

static struct hs_function_definition const game_difficulty_get_real_definition=
{
	_hs_type_enum_game_difficulty,
	0,
	"game_difficulty_get_real",
	hs_macro_function_parse,
	game_difficulty_level_get_evaluate,
	"returns the actual current difficulty setting without lying",
	NULL,
	0,
};

static struct hs_function_definition const players_unzoom_all_definition=
{
	_hs_type_void,
	0,
	"players_unzoom_all",
	hs_macro_function_parse,
	players_unzoom_all_evaluate,
	"resets zoom levels on all players",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const player_enable_input_definition=
{
	{
		_hs_type_void,
		0,
		"player_enable_input",
		hs_macro_function_parse,
		player_input_enable_evaluate,
		"toggle player input. the player can still free-look, but nothing else.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const player_camera_control_definition=
{
	{
		_hs_type_boolean,
		0,
		"player_camera_control",
		hs_macro_function_parse,
		scripted_player_control_set_camera_control_evaluate,
		"enables/disables camera control globally",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const player_action_test_reset_definition=
{
	_hs_type_void,
	0,
	"player_action_test_reset",
	hs_macro_function_parse,
	player_control_action_test_reset_evaluate,
	"resets the player action test state so that all tests will return false.",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_jump_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_jump",
	hs_macro_function_parse,
	player_control_action_test_jump_evaluate,
	"returns true if any player has jumped since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_primary_trigger_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_primary_trigger",
	hs_macro_function_parse,
	player_control_action_test_primary_trigger_evaluate,
	"returns true if any player has used primary trigger since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_grenade_trigger_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_grenade_trigger",
	hs_macro_function_parse,
	player_control_action_test_grenade_trigger_evaluate,
	"returns true if any player has used grenade trigger since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_zoom_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_zoom",
	hs_macro_function_parse,
	player_control_action_test_zoom_evaluate,
	"returns true if any player has hit the zoom button since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_action_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_action",
	hs_macro_function_parse,
	player_control_action_test_action_evaluate,
	"returns true if any player has hit the action key since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_accept_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_accept",
	hs_macro_function_parse,
	player_control_action_test_accept_evaluate,
	"returns true if any player has hit accept since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_back_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_back",
	hs_macro_function_parse,
	player_control_action_test_back_evaluate,
	"returns true if any player has hit the back key since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_look_relative_up_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_look_relative_up",
	hs_macro_function_parse,
	player_control_action_test_look_relative_up_evaluate,
	"returns true if any player has looked up since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_look_relative_down_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_look_relative_down",
	hs_macro_function_parse,
	player_control_action_test_look_relative_down_evaluate,
	"returns true if any player has looked down since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_look_relative_left_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_look_relative_left",
	hs_macro_function_parse,
	player_control_action_test_look_relative_left_evaluate,
	"returns true if any player has looked left since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_look_relative_right_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_look_relative_right",
	hs_macro_function_parse,
	player_control_action_test_look_relative_right_evaluate,
	"returns true if any player has looked right since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_look_relative_all_directions_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_look_relative_all_directions",
	hs_macro_function_parse,
	player_control_action_test_look_relative_all_directions_evaluate,
	"returns true if any player has looked up, down, left, and right since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition const player_action_test_move_relative_all_directions_definition=
{
	_hs_type_boolean,
	0,
	"player_action_test_move_relative_all_directions",
	hs_macro_function_parse,
	player_control_action_test_move_relative_all_directions_evaluate,
	"returns true if any player has moved forward, backward, left, and right since the last call to (player_action_test_reset).",
	NULL,
	0,
};

static struct hs_function_definition_with_3_parameters const player_add_equipment_definition=
{
	{
		_hs_type_void,
		0,
		"player_add_equipment",
		hs_macro_function_parse,
		player_add_equipment_evaluate,
		"adds/resets the player's health, shield, and inventory (weapons and grenades) to the named profile. resets if third parameter is true, adds if false.",
		NULL,
		3,
		{ _hs_type_unit },
	},
	{ _hs_type_starting_profile, _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const hs_debug_teleport_player_definition=
{
	{
		_hs_type_void,
		0,
		"debug_teleport_player",
		hs_macro_function_parse,
		debug_player_teleport_evaluate,
		"",
		NULL,
		2,
		{ _hs_type_short_integer },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition const map_reset_definition=
{
	_hs_type_void,
	0,
	"map_reset",
	hs_macro_function_parse,
	main_reset_map_evaluate,
	"starts the map from the beginning.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const map_name_definition=
{
	{
		_hs_type_void,
		0,
		"map_name",
		hs_macro_function_parse,
		main_set_map_name_evaluate,
		"changes the name of the solo player map.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const multiplayer_map_name_definition=
{
	{
		_hs_type_void,
		0,
		"multiplayer_map_name",
		hs_macro_function_parse,
		main_set_multiplayer_map_name_evaluate,
		"changes the name of the multiplayer map",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const game_difficulty_set_definition=
{
	{
		_hs_type_void,
		0,
		"game_difficulty_set",
		hs_macro_function_parse,
		main_set_difficulty_evaluate,
		"changes the difficulty setting for the next map to be loaded.",
		NULL,
		1,
		{ _hs_type_enum_game_difficulty },
	},
};

static struct hs_function_definition_with_1_parameter const crash_definition=
{
	{
		_hs_type_void,
		0,
		"crash",
		hs_macro_function_parse,
		main_crash_evaluate,
		"crashes (for debugging).",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const switch_bsp_definition=
{
	{
		_hs_type_void,
		0,
		"switch_bsp",
		hs_macro_function_parse,
		scenario_switch_structure_bsp_evaluate,
		"takes off your condom and changes to a different structure bsp",
		NULL,
		1,
		{ _hs_type_short_integer },
	},
};

static struct hs_function_definition const structure_bsp_index_definition=
{
	_hs_type_short_integer,
	0,
	"structure_bsp_index",
	hs_macro_function_parse,
	global_structure_bsp_index_get_evaluate,
	"returns the current structure bsp index",
	NULL,
	0,
};

static struct hs_function_definition const version_definition=
{
	_hs_type_void,
	0,
	"version",
	hs_macro_function_parse,
	main_print_version_evaluate,
	"prints the build version.",
	NULL,
	0,
};

static struct hs_function_definition const playback_definition=
{
	_hs_type_void,
	0,
	"playback",
	hs_macro_function_parse,
	main_set_game_connection_to_film_playback_evaluate,
	"starts game in film playback mode",
	NULL,
	0,
};

static struct hs_function_definition const texture_cache_flush_definition=
{
	_hs_type_void,
	0,
	"texture_cache_flush",
	hs_macro_function_parse,
	texture_cache_flush_evaluate,
	"don't make me kick your ass",
	NULL,
	0,
};

static struct hs_function_definition const sound_cache_flush_definition=
{
	_hs_type_void,
	0,
	"sound_cache_flush",
	hs_macro_function_parse,
	sound_cache_flush_evaluate,
	"i'm a rebel!",
	NULL,
	0,
};

static struct hs_function_definition const debug_memory_definition=
{
	_hs_type_void,
	0,
	"debug_memory",
	hs_macro_function_parse,
	debug_dump_memory_evaluate,
	"dumps memory leaks.",
	NULL,
	0,
};

static struct hs_function_definition const debug_memory_by_file_definition=
{
	_hs_type_void,
	0,
	"debug_memory_by_file",
	hs_macro_function_parse,
	debug_dump_memory_by_file_evaluate,
	"dumps memory leaks by source file.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const debug_memory_for_file_definition=
{
	{
		_hs_type_void,
		0,
		"debug_memory_for_file",
		hs_macro_function_parse,
		debug_dump_memory_for_file_evaluate,
		"dumps memory leaks from the specified source file.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition const debug_tags_definition=
{
	_hs_type_void,
	0,
	"debug_tags",
	hs_macro_function_parse,
	tag_groups_dump_memory_evaluate,
	"writes all memory being used by tag files into tag_dump.txt",
	NULL,
	0,
};

static struct hs_function_definition const profile_reset_definition=
{
	_hs_type_void,
	0,
	"profile_reset",
	hs_macro_function_parse,
	profile_initialize_evaluate,
	"resets profiling data.",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const profile_dump_definition=
{
	{
		_hs_type_void,
		0,
		"profile_dump",
		hs_macro_function_parse,
		profile_dump_to_file_evaluate,
		"dumps profile based on a substring.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const profile_activate_definition=
{
	{
		_hs_type_void,
		0,
		"profile_activate",
		hs_macro_function_parse,
		profile_sections_activate_evaluate,
		"activates profile sections based on a substring.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const profile_deactivate_definition=
{
	{
		_hs_type_void,
		0,
		"profile_deactivate",
		hs_macro_function_parse,
		profile_sections_deactivate_evaluate,
		"deactivates profile sections based on a substring.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const profile_graph_toggle_definition=
{
	{
		_hs_type_void,
		0,
		"profile_graph_toggle",
		hs_macro_function_parse,
		profile_graph_toggle_evaluate,
		"enables or disables profile graph display of a particular value.",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const hs_debug_pvs_definition=
{
	{
		_hs_type_void,
		0,
		"debug_pvs",
		hs_macro_function_parse,
		debug_pvs_evaluate,
		"displays the current pvs.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const hs_radiosity_start_definition=
{
	_hs_type_void,
	0,
	"radiosity_start",
	hs_macro_function_parse,
	radiosity_hack_start_evaluate,
	"starts radiosity computation.",
	NULL,
	0,
};

static struct hs_function_definition const hs_radiosity_save_definition=
{
	_hs_type_void,
	0,
	"radiosity_save",
	hs_macro_function_parse,
	radiosity_hack_save_evaluate,
	"saves radiosity solution.",
	NULL,
	0,
};

static struct hs_function_definition const hs_radiosity_debug_point_definition=
{
	_hs_type_void,
	0,
	"radiosity_debug_point",
	hs_macro_function_parse,
	radiosity_hack_find_point_evaluate,
	"tests sun occlusion at a point.",
	NULL,
	0,
};

static struct hs_function_definition const ai_lines_definition=
{
	_hs_type_void,
	0,
	"ai_lines",
	hs_macro_function_parse,
	ai_profile_change_render_spray_evaluate,
	"cycles through AI line-spray modes",
	NULL,
	0,
};

static struct hs_function_definition const ai_debug_sound_point_set_definition=
{
	_hs_type_void,
	0,
	"ai_debug_sound_point_set",
	hs_macro_function_parse,
	ai_debug_sound_point_set_evaluate,
	"drops the AI debugging sound point at the camera location",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const ai_debug_vocalize_definition=
{
	{
		_hs_type_void,
		0,
		"ai_debug_vocalize",
		hs_macro_function_parse,
		ai_debug_vocalize_evaluate,
		"makes the selected AI vocalize",
		NULL,
		2,
		{ _hs_type_string },
	},
	{ _hs_type_string },
};

static struct hs_function_definition_with_1_parameter const ai_debug_teleport_to_definition=
{
	{
		_hs_type_void,
		0,
		"ai_debug_teleport_to",
		hs_macro_function_parse,
		ai_debug_teleport_to_evaluate,
		"teleports all players to the specified encounter",
		NULL,
		1,
		{ _hs_type_ai },
	},
};

static struct hs_function_definition_with_1_parameter const ai_debug_speak_definition=
{
	{
		_hs_type_void,
		0,
		"ai_debug_speak",
		hs_macro_function_parse,
		ai_debug_speak_evaluate,
		"makes the currently selected AI speak a vocalization (e.g. ai_speak \"pain minor\")",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const ai_debug_speak_list_definition=
{
	{
		_hs_type_void,
		0,
		"ai_debug_speak_list",
		hs_macro_function_parse,
		ai_debug_speak_list_evaluate,
		"makes the currently selected AI speak a list of vocalizations (e.g. ai_speak_list \"involuntary\")",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_4_parameters const fade_in_definition=
{
	{
		_hs_type_void,
		0,
		"fade_in",
		hs_macro_function_parse,
		player_effect_screen_fade_in_evaluate,
		"does a screen fade in from a particular color",
		NULL,
		4,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real, _hs_type_short_integer },
};

static struct hs_function_definition_with_4_parameters const fade_out_definition=
{
	{
		_hs_type_void,
		0,
		"fade_out",
		hs_macro_function_parse,
		player_effect_screen_fade_out_evaluate,
		"does a screen fade out to a particular color",
		NULL,
		4,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real, _hs_type_short_integer },
};

static struct hs_function_definition const cinematic_start_definition=
{
	_hs_type_void,
	0,
	"cinematic_start",
	hs_macro_function_parse,
	cinematic_start_evaluate,
	"initializes game to start a cinematic (interruptive) cutscene",
	NULL,
	0,
};

static struct hs_function_definition const cinematic_stop_definition=
{
	_hs_type_void,
	0,
	"cinematic_stop",
	hs_macro_function_parse,
	cinematic_stop_evaluate,
	"initializes the game to end a cinematic (interruptive) cutscene",
	NULL,
	0,
};

static struct hs_function_definition const cinematic_skip_start_internal_definition=
{
	_hs_type_void,
	0,
	"cinematic_skip_start_internal",
	hs_macro_function_parse,
	cinematic_skip_start_evaluate,
	"",
	NULL,
	0,
};

static struct hs_function_definition const cinematic_skip_stop_internal_definition=
{
	_hs_type_void,
	0,
	"cinematic_skip_stop_internal",
	hs_macro_function_parse,
	cinematic_skip_stop_evaluate,
	"",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const cinematic_show_letterbox_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_show_letterbox",
		hs_macro_function_parse,
		cinematic_show_letterbox_evaluate,
		"sets or removes the letterbox bars",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const cinematic_set_title_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_set_title",
		hs_macro_function_parse,
		cinematic_set_title_evaluate,
		"activates the chapter title",
		NULL,
		1,
		{ _hs_type_cutscene_title },
	},
};

static struct hs_function_definition_with_2_parameters const cinematic_set_title_delayed_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_set_title_delayed",
		hs_macro_function_parse,
		cinematic_set_title_delayed_evaluate,
		"activates the chapter title, delayed by <real> seconds",
		NULL,
		2,
		{ _hs_type_cutscene_title },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const cinematic_suppress_bsp_object_creation_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_suppress_bsp_object_creation",
		hs_macro_function_parse,
		cinematic_suppress_bsp_object_creation_evaluate,
		"suppresses or enables the automatic creation of objects during cutscenes due to a bsp switch",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const attract_mode_start_definition=
{
	_hs_type_void,
	0,
	"attract_mode_start",
	hs_macro_function_parse,
	attract_mode_start_evaluate,
	"",
	NULL,
	0,
};

static struct hs_function_definition const game_won_definition=
{
	_hs_type_void,
	0,
	"game_won",
	hs_macro_function_parse,
	main_won_map_evaluate,
	"causes the player to successfully finish the current level and move to the next",
	NULL,
	0,
};

static struct hs_function_definition const game_lost_definition=
{
	_hs_type_void,
	0,
	"game_lost",
	hs_macro_function_parse,
	main_lost_map_evaluate,
	"causes the player to revert to his previous saved game",
	NULL,
	0,
};

static struct hs_function_definition const game_safe_to_save_definition=
{
	_hs_type_boolean,
	0,
	"game_safe_to_save",
	hs_macro_function_parse,
	game_safe_to_save_evaluate,
	"returns FALSE if it would be a bad idea to save the player's game right now",
	NULL,
	0,
};

static struct hs_function_definition const game_all_quiet_definition=
{
	_hs_type_boolean,
	0,
	"game_all_quiet",
	hs_macro_function_parse,
	game_all_quiet_evaluate,
	"returns FALSE if there are bad guys around, projectiles in the air, etc.",
	NULL,
	0,
};

static struct hs_function_definition const game_safe_to_speak_definition=
{
	_hs_type_boolean,
	0,
	"game_safe_to_speak",
	hs_macro_function_parse,
	game_safe_to_speak_evaluate,
	"returns FALSE if it would be a bad idea to save the player's game right now",
	NULL,
	0,
};

static struct hs_function_definition const game_is_cooperative_definition=
{
	_hs_type_boolean,
	0,
	"game_is_cooperative",
	hs_macro_function_parse,
	game_is_cooperative_evaluate,
	"returns TRUE if the game is cooperative",
	NULL,
	0,
};

static struct hs_function_definition const game_save_definition=
{
	_hs_type_void,
	0,
	"game_save",
	hs_macro_function_parse,
	main_save_map_safe_evaluate,
	"checks to see if it is safe to save game, then saves (gives up after 8 seconds)",
	NULL,
	0,
};

static struct hs_function_definition const game_save_cancel_definition=
{
	_hs_type_void,
	0,
	"game_save_cancel",
	hs_macro_function_parse,
	main_save_cancel_evaluate,
	"cancels any pending game_save, timeout or not",
	NULL,
	0,
};

static struct hs_function_definition const game_save_no_timeout_definition=
{
	_hs_type_void,
	0,
	"game_save_no_timeout",
	hs_macro_function_parse,
	main_save_map_no_timeout_evaluate,
	"checks to see if it is safe to save game, then saves (this version never gives up)",
	NULL,
	0,
};

static struct hs_function_definition const game_save_totally_unsafe_definition=
{
	_hs_type_void,
	0,
	"game_save_totally_unsafe",
	hs_macro_function_parse,
	main_save_map_nonsafe_evaluate,
	"disregards player's current situation",
	NULL,
	0,
};

static struct hs_function_definition const game_saving_definition=
{
	_hs_type_boolean,
	0,
	"game_saving",
	hs_macro_function_parse,
	main_saving_map_evaluate,
	"checks to see if the game is trying to save the map.",
	NULL,
	0,
};

static struct hs_function_definition const game_revert_definition=
{
	_hs_type_void,
	0,
	"game_revert",
	hs_macro_function_parse,
	main_revert_map_evaluate,
	"reverts to last saved game, if any (for testing, the first bastard that does this to me gets it in the head)",
	NULL,
	0,
};

static struct hs_function_definition const core_load_definition=
{
	_hs_type_void,
	0,
	"core_load",
	hs_macro_function_parse,
	main_load_core_evaluate,
	"loads debug game state from core\\core.bin",
	NULL,
	0,
};

static struct hs_function_definition const core_load_at_startup_definition=
{
	_hs_type_void,
	0,
	"core_load_at_startup",
	hs_macro_function_parse,
	main_load_core_at_startup_evaluate,
	"loads debug game state from core\\core.bin as soon as the map is initialized",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const core_load_name_definition=
{
	{
		_hs_type_void,
		0,
		"core_load_name",
		hs_macro_function_parse,
		main_load_core_name_evaluate,
		"loads debug game state from core\\<path>",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const core_load_name_at_startup_definition=
{
	{
		_hs_type_void,
		0,
		"core_load_name_at_startup",
		hs_macro_function_parse,
		main_load_core_name_at_startup_evaluate,
		"loads debug game state from core\\<path> as soon as the map is initialized",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition const core_save_definition=
{
	_hs_type_void,
	0,
	"core_save",
	hs_macro_function_parse,
	main_save_core_evaluate,
	"saves debug game state to core\\core.bin",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const core_save_name_definition=
{
	{
		_hs_type_void,
		0,
		"core_save_name",
		hs_macro_function_parse,
		main_save_core_name_evaluate,
		"saves debug game state to core\\<path>",
		NULL,
		1,
		{ _hs_type_string },
	},
};

static struct hs_function_definition_with_1_parameter const game_skip_ticks_definition=
{
	{
		_hs_type_void,
		0,
		"game_skip_ticks",
		hs_macro_function_parse,
		main_skip_evaluate,
		"skips <short> amount of game ticks. ONLY USE IN CUTSCENES!!!",
		NULL,
		1,
		{ _hs_type_short_integer },
	},
};

static struct hs_function_definition const game_reverted_definition=
{
	_hs_type_boolean,
	0,
	"game_reverted",
	hs_macro_function_parse,
	game_state_reverted_evaluate,
	"don't use this for anything, you black-hearted bastards.",
	NULL,
	0,
};

static struct hs_function_definition_with_3_parameters const sound_impulse_start_definition=
{
	{
		_hs_type_void,
		0,
		"sound_impulse_start",
		hs_macro_function_parse,
		scripted_sound_new_evaluate,
		"plays an impulse sound from the specified source object (or \"none\"), with the specified scale.",
		NULL,
		3,
		{ _hs_type_sound },
	},
	{ _hs_type_object, _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const sound_impulse_time_definition=
{
	{
		_hs_type_long_integer,
		0,
		"sound_impulse_time",
		hs_macro_function_parse,
		scripted_sound_time_evaluate,
		"returns the time remaining for the specified impulse sound.",
		NULL,
		1,
		{ _hs_type_sound },
	},
};

static struct hs_function_definition_with_1_parameter const sound_impulse_stop_definition=
{
	{
		_hs_type_void,
		0,
		"sound_impulse_stop",
		hs_macro_function_parse,
		scripted_sound_stop_evaluate,
		"stops the specified impulse sound.",
		NULL,
		1,
		{ _hs_type_sound },
	},
};

static struct hs_function_definition_with_1_parameter const sound_looping_predict_definition=
{
	{
		_hs_type_void,
		0,
		"sound_looping_predict",
		hs_macro_function_parse,
		scripted_foley_predict_evaluate,
		"your mom.",
		NULL,
		1,
		{ _hs_type_looping_sound },
	},
};

static struct hs_function_definition_with_3_parameters const sound_looping_start_definition=
{
	{
		_hs_type_void,
		0,
		"sound_looping_start",
		hs_macro_function_parse,
		scripted_looping_sound_start_evaluate,
		"plays a looping sound from the specified source object (or \"none\"), with the specified scale.",
		NULL,
		3,
		{ _hs_type_looping_sound },
	},
	{ _hs_type_object, _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const sound_looping_stop_definition=
{
	{
		_hs_type_void,
		0,
		"sound_looping_stop",
		hs_macro_function_parse,
		scripted_looping_sound_stop_evaluate,
		"stops the specified looping sound.",
		NULL,
		1,
		{ _hs_type_looping_sound },
	},
};

static struct hs_function_definition_with_2_parameters const sound_looping_set_scale_definition=
{
	{
		_hs_type_void,
		0,
		"sound_looping_set_scale",
		hs_macro_function_parse,
		scripted_looping_sound_set_scale_evaluate,
		"changes the scale of the sound (which should affect the volume) within the range 0 to 1.",
		NULL,
		2,
		{ _hs_type_looping_sound },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const sound_looping_set_alternate_definition=
{
	{
		_hs_type_void,
		0,
		"sound_looping_set_alternate",
		hs_macro_function_parse,
		scripted_looping_sound_set_alternate_evaluate,
		"enables or disables the alternate loop/alternate end for a looping sound.",
		NULL,
		2,
		{ _hs_type_looping_sound },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_2_parameters const debug_sounds_enable_definition=
{
	{
		_hs_type_void,
		0,
		"debug_sounds_enable",
		hs_macro_function_parse,
		debug_sound_classes_enable_evaluate,
		"enables or disabled all sound classes matching the substring.",
		NULL,
		2,
		{ _hs_type_string },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_3_parameters const debug_sounds_distances_definition=
{
	{
		_hs_type_void,
		0,
		"debug_sounds_distances",
		hs_macro_function_parse,
		debug_sound_classes_set_distances_evaluate,
		"changes the minimum and maximum distances for all sound classes matching the substring.",
		NULL,
		3,
		{ _hs_type_string },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const debug_sounds_wet_definition=
{
	{
		_hs_type_void,
		0,
		"debug_sounds_wet",
		hs_macro_function_parse,
		debug_sound_classes_set_wet_evaluate,
		"changes the reverb level for all sound classes matching the substring.",
		NULL,
		2,
		{ _hs_type_string },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const sound_class_set_gain_definition=
{
	{
		_hs_type_void,
		0,
		"sound_class_set_gain",
		hs_macro_function_parse,
		sound_class_set_gain_evaluate,
		"changes the gain on the specified sound class(es) to the specified game over the specified number of ticks.",
		NULL,
		3,
		{ _hs_type_string },
	},
	{ _hs_type_real, _hs_type_short_integer },
};

static struct hs_function_definition_with_1_parameter const sound_enable_definition=
{
	{
		_hs_type_void,
		0,
		"sound_enable",
		hs_macro_function_parse,
		sound_enable_evaluate,
		"enables or disables all sound.",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_2_parameters const vehicle_hover_definition=
{
	{
		_hs_type_void,
		0,
		"vehicle_hover",
		hs_macro_function_parse,
		vehicle_hover_evaluate,
		"stops the vehicle from running real physics and runs fake hovering physics instead.",
		NULL,
		2,
		{ _hs_type_vehicle },
	},
	{ _hs_type_boolean },
};

static struct hs_function_definition_with_1_parameter const show_hud_definition=
{
	{
		_hs_type_boolean,
		0,
		"show_hud",
		hs_macro_function_parse,
		scripted_show_hud_evaluate,
		"shows or hides the hud",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const show_hud_help_text_definition=
{
	{
		_hs_type_boolean,
		0,
		"show_hud_help_text",
		hs_macro_function_parse,
		scripted_show_hud_help_text_evaluate,
		"shows or hides the hud help text",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const enable_hud_help_flash_definition=
{
	{
		_hs_type_void,
		0,
		"enable_hud_help_flash",
		hs_macro_function_parse,
		scripted_hud_set_flashing_state_evaluate,
		"starts/stops the help text flashing",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const hud_help_flash_restart_definition=
{
	_hs_type_void,
	0,
	"hud_help_flash_restart",
	hs_macro_function_parse,
	scripted_hud_restart_flashing_evaluate,
	"resets the timer for the help text flashing",
	NULL,
	0,
};

static struct hs_function_definition_with_4_parameters const activate_nav_point_flag_definition=
{
	{
		_hs_type_void,
		0,
		"activate_nav_point_flag",
		hs_macro_function_parse,
		hud_unit_activate_nav_point_with_flag_evaluate,
		"activates a nav point type <string> attached to (local) player <unit> anchored to a flag with a vertical offset <real>. If the player is not local to the machine, this will fail",
		NULL,
		4,
		{ _hs_type_navpoint },
	},
	{ _hs_type_unit, _hs_type_cutscene_flag, _hs_type_real },
};

static struct hs_function_definition_with_4_parameters const activate_nav_point_object_definition=
{
	{
		_hs_type_void,
		0,
		"activate_nav_point_object",
		hs_macro_function_parse,
		hud_unit_activate_nav_point_with_object_evaluate,
		"activates a nav point type <string> attached to (local) player <unit> anchored to an object with a vertical offset <real>. If the player is not local to the machine, this will fail",
		NULL,
		4,
		{ _hs_type_navpoint },
	},
	{ _hs_type_unit, _hs_type_object, _hs_type_real },
};

static struct hs_function_definition_with_4_parameters const activate_team_nav_point_flag_definition=
{
	{
		_hs_type_void,
		0,
		"activate_team_nav_point_flag",
		hs_macro_function_parse,
		hud_activate_team_nav_point_with_flag_evaluate,
		"activates a nav point type <string> attached to a team anchored to a flag with a vertical offset <real>. If the player is not local to the machine, this will fail",
		NULL,
		4,
		{ _hs_type_navpoint },
	},
	{ _hs_type_enum_team, _hs_type_cutscene_flag, _hs_type_real },
};

static struct hs_function_definition_with_4_parameters const activate_team_nav_point_object_definition=
{
	{
		_hs_type_void,
		0,
		"activate_team_nav_point_object",
		hs_macro_function_parse,
		hud_activate_team_nav_point_with_object_evaluate,
		"activates a nav point type <string> attached to a team anchored to an object with a vertical offset <real>. If the player is not local to the machine, this will fail",
		NULL,
		4,
		{ _hs_type_navpoint },
	},
	{ _hs_type_enum_team, _hs_type_object, _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const deactivate_nav_point_flag_definition=
{
	{
		_hs_type_void,
		0,
		"deactivate_nav_point_flag",
		hs_macro_function_parse,
		hud_unit_deactivate_nav_point_with_flag_evaluate,
		"deactivates a nav point type attached to a player <unit> anchored to a flag",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_2_parameters const deactivate_nav_point_object_definition=
{
	{
		_hs_type_void,
		0,
		"deactivate_nav_point_object",
		hs_macro_function_parse,
		hud_unit_deactivate_nav_point_with_object_evaluate,
		"deactivates a nav point type attached to a player <unit> anchored to an object",
		NULL,
		2,
		{ _hs_type_unit },
	},
	{ _hs_type_object },
};

static struct hs_function_definition_with_2_parameters const deactivate_team_nav_point_flag_definition=
{
	{
		_hs_type_void,
		0,
		"deactivate_team_nav_point_flag",
		hs_macro_function_parse,
		hud_deactivate_team_nav_point_with_flag_evaluate,
		"deactivates a nav point type attached to a team anchored to a flag",
		NULL,
		2,
		{ _hs_type_enum_team },
	},
	{ _hs_type_cutscene_flag },
};

static struct hs_function_definition_with_2_parameters const deactivate_team_nav_point_object_definition=
{
	{
		_hs_type_void,
		0,
		"deactivate_team_nav_point_object",
		hs_macro_function_parse,
		hud_deactivate_team_nav_point_with_object_evaluate,
		"deactivates a nav point type attached to a team anchored to an object",
		NULL,
		2,
		{ _hs_type_enum_team },
	},
	{ _hs_type_object },
};

static struct hs_function_definition const cls_definition=
{
	_hs_type_void,
	0,
	"cls",
	hs_macro_function_parse,
	terminal_clear_evaluate,
	"clears console text from the screen",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const error_overflow_suppression_definition=
{
	{
		_hs_type_void,
		0,
		"error_overflow_suppression",
		hs_macro_function_parse,
		errors_overflow_suppression_enable_evaluate,
		"enables or disables the suppression of error spamming",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const structure_lens_flares_place_definition=
{
	_hs_type_void,
	0,
	"structure_lens_flares_place",
	hs_macro_function_parse,
	structure_lens_flares_place_evaluate,
	"places lens flares in the structure bsp",
	NULL,
	0,
};

static struct hs_function_definition_with_3_parameters const player_effect_set_max_translation_definition=
{
	{
		_hs_type_void,
		0,
		"player_effect_set_max_translation",
		hs_macro_function_parse,
		scripted_player_effect_set_translation_evaluate,
		"<x> <y> <z>",
		NULL,
		3,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const player_effect_set_max_rotation_definition=
{
	{
		_hs_type_void,
		0,
		"player_effect_set_max_rotation",
		hs_macro_function_parse,
		scripted_player_effect_set_rotation_evaluate,
		"<yaw> <pitch> <roll>",
		NULL,
		3,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const hs_player_effect_set_max_rumble_definition=
{
	{
		_hs_type_void,
		0,
		"player_effect_set_max_rumble",
		hs_macro_function_parse,
		scripted_player_effect_set_rumble_evaluate,
		"<left> <right>",
		NULL,
		2,
		{ _hs_type_real },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const player_effect_start_definition=
{
	{
		_hs_type_void,
		0,
		"player_effect_start",
		hs_macro_function_parse,
		scripted_player_effect_start_evaluate,
		"<max_intensity> <attack time>",
		NULL,
		2,
		{ _hs_type_real },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const player_effect_stop_definition=
{
	{
		_hs_type_void,
		0,
		"player_effect_stop",
		hs_macro_function_parse,
		scripted_player_effect_stop_evaluate,
		"<decay>",
		NULL,
		1,
		{ _hs_type_real },
	},
};

static struct hs_function_definition_with_1_parameter const hud_show_health_definition=
{
	{
		_hs_type_void,
		0,
		"hud_show_health",
		hs_macro_function_parse,
		scripted_hud_show_health_evaluate,
		"hides/shows the health panel",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const hud_blink_health_definition=
{
	{
		_hs_type_void,
		0,
		"hud_blink_health",
		hs_macro_function_parse,
		scripted_hud_blink_health_evaluate,
		"starts/stops manual blinking of the health panel",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const hud_show_shield_definition=
{
	{
		_hs_type_void,
		0,
		"hud_show_shield",
		hs_macro_function_parse,
		scripted_hud_show_shield_evaluate,
		"hides/shows the shield panel",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const hud_blink_shield_definition=
{
	{
		_hs_type_void,
		0,
		"hud_blink_shield",
		hs_macro_function_parse,
		scripted_hud_blink_shield_evaluate,
		"starts/stops manual blinking of the shield panel",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const hud_show_motion_sensor_definition=
{
	{
		_hs_type_void,
		0,
		"hud_show_motion_sensor",
		hs_macro_function_parse,
		scripted_hud_show_motion_sensor_evaluate,
		"hides/shows the motion sensor panel",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const hud_blink_motion_sensor_definition=
{
	{
		_hs_type_void,
		0,
		"hud_blink_motion_sensor",
		hs_macro_function_parse,
		scripted_hud_blink_motion_sensor_evaluate,
		"starts/stops manual blinking of the motion sensor panel",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const hud_show_crosshair_definition=
{
	{
		_hs_type_void,
		0,
		"hud_show_crosshair",
		hs_macro_function_parse,
		scripted_hud_show_crosshair_evaluate,
		"hides/shows the weapon crosshair",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const hud_clear_messages_definition=
{
	_hs_type_void,
	0,
	"hud_clear_messages",
	hs_macro_function_parse,
	scripted_hud_messages_clear_evaluate,
	"clears all non-state messages on the hud",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const hud_set_help_text_definition=
{
	{
		_hs_type_void,
		0,
		"hud_set_help_text",
		hs_macro_function_parse,
		scripted_hud_set_state_message_evaluate,
		"displays <message> as the help text",
		NULL,
		1,
		{ _hs_type_hud_message },
	},
};

static struct hs_function_definition_with_1_parameter const hud_set_objective_text_definition=
{
	{
		_hs_type_void,
		0,
		"hud_set_objective_text",
		hs_macro_function_parse,
		scripted_hud_set_objective_evaluate,
		"sets <message> as the current objective",
		NULL,
		1,
		{ _hs_type_hud_message },
	},
};

static struct hs_function_definition_with_2_parameters const hud_set_timer_time_definition=
{
	{
		_hs_type_void,
		0,
		"hud_set_timer_time",
		hs_macro_function_parse,
		scripted_hud_set_timer_time_evaluate,
		"sets the time for the timer to <short> minutes and <short> seconds, and starts and displays timer",
		NULL,
		2,
		{ _hs_type_short_integer },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition_with_2_parameters const hud_set_timer_warning_time_definition=
{
	{
		_hs_type_void,
		0,
		"hud_set_timer_warning_time",
		hs_macro_function_parse,
		scripted_hud_set_timer_warning_cutoff_evaluate,
		"sets the warning time for the timer to <short> minutes and <short> seconds",
		NULL,
		2,
		{ _hs_type_short_integer },
	},
	{ _hs_type_short_integer },
};

static struct hs_function_definition_with_3_parameters const hud_set_timer_position_definition=
{
	{
		_hs_type_void,
		0,
		"hud_set_timer_position",
		hs_macro_function_parse,
		scripted_hud_set_timer_position_evaluate,
		"sets the timer upper left position to (x, y)=>(<short>, <short>)",
		NULL,
		3,
		{ _hs_type_short_integer },
	},
	{ _hs_type_short_integer, _hs_type_enum_hud_corner },
};

static struct hs_function_definition_with_1_parameter const show_hud_timer_definition=
{
	{
		_hs_type_void,
		0,
		"show_hud_timer",
		hs_macro_function_parse,
		scripted_hud_show_timer_evaluate,
		"displays the hud timer",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const pause_hud_timer_definition=
{
	{
		_hs_type_void,
		0,
		"pause_hud_timer",
		hs_macro_function_parse,
		scripted_hud_pause_timer_evaluate,
		"pauses or unpauses the hud timer",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const hud_get_timer_ticks_definition=
{
	_hs_type_short_integer,
	0,
	"hud_get_timer_ticks",
	hs_macro_function_parse,
	scripted_hud_get_timer_ticks_evaluate,
	"returns the ticks left on the hud timer",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const time_code_show_definition=
{
	{
		_hs_type_void,
		0,
		"time_code_show",
		hs_macro_function_parse,
		scripted_hud_time_code_show_evaluate,
		"shows the time code timer",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const time_code_start_definition=
{
	{
		_hs_type_void,
		0,
		"time_code_start",
		hs_macro_function_parse,
		scripted_hud_time_code_start_evaluate,
		"starts/stops the time code timer",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const time_code_reset_definition=
{
	_hs_type_void,
	0,
	"time_code_reset",
	hs_macro_function_parse,
	scripted_hud_time_code_reset_evaluate,
	"resets the time code timer",
	NULL,
	0,
};

static struct hs_function_definition const rasterizer_decals_flush_definition=
{
	_hs_type_void,
	0,
	"rasterizer_decals_flush",
	hs_macro_function_parse,
	rasterizer_decals_flush_evaluate,
	"flush all decals",
	NULL,
	0,
};

static struct hs_function_definition const rasterizer_fps_accumulate_definition=
{
	_hs_type_void,
	0,
	"rasterizer_fps_accumulate",
	hs_macro_function_parse,
	rasterizer_fps_accumulate_evaluate,
	"average fps",
	NULL,
	0,
};

static struct hs_function_definition_with_4_parameters const rasterizer_model_ambient_reflection_tint_definition=
{
	{
		_hs_type_void,
		0,
		"rasterizer_model_ambient_reflection_tint",
		hs_macro_function_parse,
		rasterizer_model_ambient_reflection_tint_evaluate,
		"",
		NULL,
		4,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real, _hs_type_real },
};

static struct hs_function_definition const rasterizer_lights_reset_for_new_map_definition=
{
	_hs_type_void,
	0,
	"rasterizer_lights_reset_for_new_map",
	hs_macro_function_parse,
	rasterizer_lights_reset_for_new_map_evaluate,
	"",
	NULL,
	0,
};

static struct hs_function_definition_with_2_parameters const script_screen_effect_set_value_definition=
{
	{
		_hs_type_void,
		0,
		"script_screen_effect_set_value",
		hs_macro_function_parse,
		rasterizer_script_screen_effect_set_value_evaluate,
		"sets a screen effect script value",
		NULL,
		2,
		{ _hs_type_short_integer },
	},
	{ _hs_type_real },
};

static struct hs_function_definition_with_1_parameter const cinematic_screen_effect_start_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_screen_effect_start",
		hs_macro_function_parse,
		rasterizer_screen_effect_start_evaluate,
		"starts screen effect; pass TRUE to clear",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_5_parameters const cinematic_screen_effect_set_convolution_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_screen_effect_set_convolution",
		hs_macro_function_parse,
		rasterizer_screen_effect_set_convolution_evaluate,
		"sets the convolution effect",
		NULL,
		5,
		{ _hs_type_short_integer },
	},
	{ _hs_type_short_integer, _hs_type_real, _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_6_parameters const cinematic_screen_effect_set_filter_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_screen_effect_set_filter",
		hs_macro_function_parse,
		rasterizer_screen_effect_set_filter_evaluate,
		"sets the filter effect",
		NULL,
		6,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real, _hs_type_real, _hs_type_boolean, _hs_type_real },
};

static struct hs_function_definition_with_3_parameters const cinematic_screen_effect_set_filter_desaturation_tint_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_screen_effect_set_filter_desaturation_tint",
		hs_macro_function_parse,
		rasterizer_screen_effect_set_filter_desaturation_tint_evaluate,
		"sets the desaturation filter tint color",
		NULL,
		3,
		{ _hs_type_real },
	},
	{ _hs_type_real, _hs_type_real },
};

static struct hs_function_definition_with_2_parameters const cinematic_screen_effect_set_video_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_screen_effect_set_video",
		hs_macro_function_parse,
		rasterizer_screen_effect_set_video_evaluate,
		"sets the video effect: <noise intensity[0,1]>, <overbright: 0=none, 1=2x, 2=4x>",
		NULL,
		2,
		{ _hs_type_short_integer },
	},
	{ _hs_type_real },
};

static struct hs_function_definition const cinematic_screen_effect_stop_definition=
{
	_hs_type_void,
	0,
	"cinematic_screen_effect_stop",
	hs_macro_function_parse,
	rasterizer_screen_effect_stop_evaluate,
	"returns control of the screen effects to the rest of the game",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const cinematic_set_near_clip_distance_definition=
{
	{
		_hs_type_void,
		0,
		"cinematic_set_near_clip_distance",
		hs_macro_function_parse,
		rasterizer_set_near_clip_distance_evaluate,
		"",
		NULL,
		1,
		{ _hs_type_real },
	},
};

static struct hs_function_definition const hs_enumerate_memory_units_definition=
{
	_hs_type_void,
	0,
	"enumerate_memory_units",
	hs_macro_function_parse,
	enumerate_memory_units_test_evaluate,
	"enumerate memory units",
	NULL,
	0,
};

static struct hs_function_definition const delete_save_game_files_definition=
{
	_hs_type_void,
	0,
	"delete_save_game_files",
	hs_macro_function_parse,
	saved_game_files_delete_all_custom_profiles_evaluate,
	"delete all custom profile files",
	NULL,
	0,
};

static struct hs_function_definition const fast_setup_network_server_definition=
{
	_hs_type_void,
	0,
	"fast_setup_network_server",
	hs_macro_function_parse,
	player_ui_fast_setup_network_server_evaluate,
	"for zach's multiplayer testing",
	NULL,
	0,
};

static struct hs_function_definition const profile_unlock_solo_levels_definition=
{
	_hs_type_void,
	0,
	"profile_unlock_solo_levels",
	hs_macro_function_parse,
	player_ui_activate_all_solo_levels_evaluate,
	"unlocks all the solo player levels for player 1's profile",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const player0_look_invert_pitch_definition=
{
	{
		_hs_type_void,
		0,
		"player0_look_invert_pitch",
		hs_macro_function_parse,
		player0_look_invert_pitch_evaluate,
		"invert player0's look",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition const player0_look_pitch_is_inverted_definition=
{
	_hs_type_boolean,
	0,
	"player0_look_pitch_is_inverted",
	hs_macro_function_parse,
	player0_look_pitch_is_inverted_evaluate,
	"returns TRUE if player0's look pitch is inverted",
	NULL,
	0,
};

static struct hs_function_definition const player0_joystick_set_is_normal_definition=
{
	_hs_type_boolean,
	0,
	"player0_joystick_set_is_normal",
	hs_macro_function_parse,
	player0_joystick_set_is_normal_evaluate,
	"returns TRUE if player0 is using the normal joystick set",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const ui_widget_show_path_definition=
{
	{
		_hs_type_void,
		0,
		"ui_widget_show_path",
		hs_macro_function_parse,
		ui_widget_debug_show_path_evaluate,
		"blah blah",
		NULL,
		1,
		{ _hs_type_boolean },
	},
};

static struct hs_function_definition_with_1_parameter const display_scenario_help_definition=
{
	{
		_hs_type_void,
		0,
		"display_scenario_help",
		hs_macro_function_parse,
		display_scenario_help_evaluate,
		"display in-game help dialog",
		NULL,
		1,
		{ _hs_type_short_integer },
	},
};

static struct hs_function_definition const hs_network_game_start_now_definition=
{
	_hs_type_void,
	0,
	"network_game_start_now",
	hs_macro_function_parse,
	network_game_client_request_immediate_start_evaluate,
	"another one for zach",
	NULL,
	0,
};

static struct hs_function_definition_with_1_parameter const xbox_set_machine_name_definition=
{
	{
		_hs_type_void,
		0,
		"xbox_set_machine_name",
		hs_macro_function_parse,
		xbox_set_machine_name_evaluate,
		"YAHSFFZ",
		NULL,
		1,
		{ _hs_type_string },
	},
};

long const hs_function_table_count= 418;

struct hs_enum_definition const hs_enum_table[]=
{
	{ 4, 0, global_game_difficulty_level_names },
	{ 10, 0, global_game_team_names },
	{ 12, 0, global_ai_default_state_names },
	{ 16, 0, global_actor_type_names },
	{ 5, 0, global_hud_anchor_names },
};

struct hs_function_table_storage hs_function_table=
{
	{
		&begin_definition,
		&begin_random_definition,
		&if_definition,
		&cond_definition,
		&set_definition,
		&and_definition,
		&or_definition,
		&add_definition,
		&subtract_definition,
		&multiply_definition,
		&divide_definition,
		&min_definition,
		&max_definition,
		&equal_definition,
		&not_equal_definition,
		&gt_definition,
		&lt_definition,
		&gte_definition,
		&lte_definition,
		&sleep_definition,
		&sleep_until_definition,
		&wake_definition,
		&inspect_definition,
		&object_to_unit_definition,
		&ai_debug_communication_suppress_definition,
		&ai_debug_communication_ignore_definition,
		&ai_debug_communication_focus_definition,
		&not_definition.definition,
		&print_definition.definition,
		&players_definition,
		&volume_teleport_players_not_inside_definition.definition,
		&volume_test_object_definition.definition,
		&volume_test_objects_definition.definition,
		&volume_test_objects_all_definition.definition,
		&object_teleport_definition.definition,
		&object_set_facing_definition.definition,
		&object_set_shield_definition.definition,
		&object_set_permutation_definition.definition,
		&object_create_definition.definition,
		&object_destroy_definition.definition,
		&object_create_anew_definition.definition,
		&object_create_containing_definition.definition,
		&object_create_anew_containing_definition.definition,
		&object_destroy_containing_definition.definition,
		&object_destroy_all_definition,
		&list_get_definition.definition,
		&list_count_definition.definition,
		&effect_new_definition.definition,
		&effect_new_on_object_marker_definition.definition,
		&damage_new_definition.definition,
		&damage_object_definition.definition,
		&objects_can_see_object_definition.definition,
		&objects_can_see_flag_definition.definition,
		&objects_delete_by_definition_definition.definition,
		&sound_set_gain_definition.definition,
		&sound_get_gain_definition.definition,
		&script_recompile_definition,
		&script_doc_definition,
		&help_definition.definition,
		&random_range_definition.definition,
		&real_random_range_definition.definition,
		&numeric_countdown_timer_set_definition.definition,
		&numeric_countdown_timer_get_definition.definition,
		&numeric_countdown_timer_stop_definition,
		&numeric_countdown_timer_restart_definition,
		&breakable_surfaces_enable_definition.definition,
		&recording_play_definition.definition,
		&recording_play_and_delete_definition.definition,
		&recording_play_and_hover_definition.definition,
		&recording_kill_definition.definition,
		&recording_time_definition.definition,
		&object_set_ranged_attack_inhibited_definition.definition,
		&object_set_melee_attack_inhibited_definition.definition,
		&objects_dump_memory_definition,
		&object_set_collideable_definition.definition,
		&object_set_scale_definition.definition,
		&objects_attach_definition.definition,
		&objects_detach_definition.definition,
		&garbage_collect_now_definition,
		&object_cannot_take_damage_definition.definition,
		&object_can_take_damage_definition.definition,
		&object_beautify_definition.definition,
		&objects_predict_definition.definition,
		&object_type_predict_definition.definition,
		&object_pvs_activate_definition.definition,
		&object_pvs_set_object_definition.definition,
		&object_pvs_set_camera_definition.definition,
		&object_pvs_clear_definition,
		&render_lights_definition.definition,
		&scenery_get_animation_time_definition.definition,
		&scenery_animation_start_definition.definition,
		&scenery_animation_start_at_frame_definition.definition,
		&render_effects_definition.definition,
		&unit_can_blink_definition.definition,
		&unit_open_definition.definition,
		&unit_close_definition.definition,
		&unit_kill_definition.definition,
		&unit_kill_silent_definition.definition,
		&unit_get_custom_animation_time_definition.definition,
		&unit_stop_custom_animation_definition.definition,
		&unit_custom_animation_at_frame_definition.definition,
		&custom_animation_definition.definition,
		&custom_animation_list_definition.definition,
		&unit_is_playing_custom_animation_definition.definition,
		&unit_aim_without_turning_definition.definition,
		&unit_set_emotion_definition.definition,
		&unit_set_enterable_by_player_definition.definition,
		&unit_enter_vehicle_definition.definition,
		&vehicle_test_seat_list_definition.definition,
		&vehicle_test_seat_definition.definition,
		&unit_set_emotion_animation_definition.definition,
		&unit_exit_vehicle_definition.definition,
		&unit_set_maximum_vitality_definition.definition,
		&units_set_maximum_vitality_definition.definition,
		&unit_set_current_vitality_definition.definition,
		&units_set_current_vitality_definition.definition,
		&vehicle_load_magic_definition.definition,
		&vehicle_unload_definition.definition,
		&magic_seat_name_definition.definition,
		&unit_set_seat_definition.definition,
		&magic_melee_attack_definition,
		&vehicle_riders_definition.definition,
		&vehicle_driver_definition.definition,
		&vehicle_gunner_definition.definition,
		&unit_get_health_definition.definition,
		&unit_get_shield_definition.definition,
		&unit_get_total_grenade_count_definition.definition,
		&unit_has_weapon_definition.definition,
		&unit_has_weapon_readied_definition.definition,
		&unit_doesnt_drop_items_definition.definition,
		&unit_impervious_definition.definition,
		&unit_suspended_definition.definition,
		&unit_solo_player_integrated_night_vision_is_active_definition,
		&units_set_desired_flashlight_state_definition.definition,
		&unit_set_desired_flashlight_state_definition.definition,
		&unit_get_current_flashlight_state_definition.definition,
		&device_set_never_appears_locked_definition.definition,
		&device_get_power_definition.definition,
		&device_set_power_definition.definition,
		&device_set_position_definition.definition,
		&device_get_position_definition.definition,
		&device_set_position_immediate_definition.definition,
		&device_group_get_definition.definition,
		&device_group_set_definition.definition,
		&device_group_set_immediate_definition.definition,
		&device_one_sided_set_definition.definition,
		&device_operates_automatically_set_definition.definition,
		&device_group_change_only_once_more_set_definition.definition,
		&breakable_surfaces_reset_definition,
		&cheat_all_powerups_definition,
		&cheat_all_weapons_definition,
		&cheat_all_vehicles_definition,
		&cheat_teleport_to_camera_definition,
		&cheat_active_camouflage_definition,
		&cheat_active_camouflage_local_player_definition.definition,
		&cheats_load_definition,
		&ai_free_definition.definition,
		&ai_free_units_definition.definition,
		&ai_attach_definition.definition,
		&ai_attach_free_definition.definition,
		&ai_detach_definition.definition,
		&ai_place_definition.definition,
		&ai_kill_definition.definition,
		&ai_kill_silent_definition.definition,
		&ai_erase_definition.definition,
		&ai_erase_all_definition,
		&ai_select_definition.definition,
		&ai_deselect_definition,
		&ai_spawn_actor_definition.definition,
		&ai_set_respawn_definition.definition,
		&ai_set_deaf_definition.definition,
		&ai_set_blind_definition.definition,
		&ai_magically_see_encounter_definition.definition,
		&ai_magically_see_players_definition.definition,
		&ai_magically_see_unit_definition.definition,
		&ai_timer_start_definition.definition,
		&ai_timer_expire_definition.definition,
		&ai_attack_definition.definition,
		&ai_defend_definition.definition,
		&ai_retreat_definition.definition,
		&ai_maneuver_definition.definition,
		&ai_maneuver_enable_definition.definition,
		&ai_migrate_definition.definition,
		&ai_migrate_and_speak_definition.definition,
		&ai_migrate_by_unit_definition.definition,
		&ai_allegiance_definition.definition,
		&ai_allegiance_remove_definition.definition,
		&ai_living_count_definition.definition,
		&ai_living_fraction_definition.definition,
		&ai_strength_definition.definition,
		&ai_swarm_count_definition.definition,
		&ai_nonswarm_count_definition.definition,
		&ai_actors_definition.definition,
		&ai_go_to_vehicle_definition.definition,
		&ai_go_to_vehicle_override_definition.definition,
		&ai_going_to_vehicle_definition.definition,
		&ai_exit_vehicle_definition.definition,
		&ai_braindead_definition.definition,
		&ai_braindead_by_unit_definition.definition,
		&ai_disregard_definition.definition,
		&ai_prefer_target_definition.definition,
		&ai_teleport_to_starting_location_definition.definition,
		&ai_teleport_to_starting_location_if_unsupported_definition.definition,
		&ai_renew_definition.definition,
		&ai_try_to_fight_nothing_definition.definition,
		&ai_try_to_fight_definition.definition,
		&ai_try_to_fight_player_definition.definition,
		&ai_command_list_definition.definition,
		&ai_command_list_by_unit_definition.definition,
		&ai_command_list_advance_definition.definition,
		&ai_command_list_advance_by_unit_definition.definition,
		&ai_command_list_status_definition.definition,
		&ai_is_attacking_definition.definition,
		&ai_force_active_definition.definition,
		&ai_force_active_by_unit_definition.definition,
		&ai_set_return_state_definition.definition,
		&ai_set_current_state_definition.definition,
		&ai_playfight_definition.definition,
		&ai_status_definition.definition,
		&ai_reconnect_definition,
		&ai_vehicle_encounter_definition.definition,
		&ai_vehicle_enterable_distance_definition.definition,
		&ai_vehicle_enterable_team_definition.definition,
		&ai_vehicle_enterable_actor_type_definition.definition,
		&ai_vehicle_enterable_actors_definition.definition,
		&ai_vehicle_enterable_disable_definition.definition,
		&ai_look_at_object_definition.definition,
		&ai_stop_looking_definition.definition,
		&ai_automatic_migration_target_definition.definition,
		&ai_follow_target_disable_definition.definition,
		&ai_follow_target_players_definition.definition,
		&ai_follow_target_unit_definition.definition,
		&ai_follow_target_ai_definition.definition,
		&ai_follow_distance_definition.definition,
		&ai_conversation_definition.definition,
		&ai_conversation_stop_definition.definition,
		&ai_conversation_advance_definition.definition,
		&ai_conversation_line_definition.definition,
		&ai_conversation_status_definition.definition,
		&ai_link_activation_definition.definition,
		&ai_berserk_definition.definition,
		&ai_set_team_definition.definition,
		&ai_allow_charge_definition.definition,
		&ai_allow_dormant_definition.definition,
		&ai_allegiance_broken_definition.definition,
		&camera_control_definition.definition,
		&camera_set_definition.definition,
		&camera_set_relative_definition.definition,
		&camera_set_animation_definition.definition,
		&camera_set_first_person_definition.definition,
		&camera_set_dead_definition.definition,
		&camera_time_definition,
		&debug_camera_load_definition,
		&debug_camera_save_definition,
		&game_speed_definition.definition,
		&game_time_definition,
		&game_variant_definition.definition,
		&game_difficulty_get_definition,
		&game_difficulty_get_real_definition,
		&map_reset_definition,
		&map_name_definition.definition,
		&multiplayer_map_name_definition.definition,
		&game_difficulty_set_definition.definition,
		&crash_definition.definition,
		&switch_bsp_definition.definition,
		&structure_bsp_index_definition,
		&version_definition,
		&playback_definition,
		&texture_cache_flush_definition,
		&sound_cache_flush_definition,
		&debug_memory_definition,
		&debug_memory_by_file_definition,
		&debug_memory_for_file_definition.definition,
		&debug_tags_definition,
		&profile_reset_definition,
		&profile_dump_definition.definition,
		&profile_activate_definition.definition,
		&profile_deactivate_definition.definition,
		&profile_graph_toggle_definition.definition,
		&hs_debug_pvs_definition.definition,
		&hs_radiosity_start_definition,
		&hs_radiosity_save_definition,
		&hs_radiosity_debug_point_definition,
		&ai_definition.definition,
		&ai_dialogue_triggers_definition.definition,
		&ai_grenades_definition.definition,
		&ai_lines_definition,
		&ai_debug_sound_point_set_definition,
		&ai_debug_vocalize_definition.definition,
		&ai_debug_teleport_to_definition.definition,
		&ai_debug_speak_definition.definition,
		&ai_debug_speak_list_definition.definition,
		&fade_in_definition.definition,
		&fade_out_definition.definition,
		&cinematic_start_definition,
		&cinematic_stop_definition,
		&cinematic_skip_start_internal_definition,
		&cinematic_skip_stop_internal_definition,
		&cinematic_show_letterbox_definition.definition,
		&cinematic_set_title_definition.definition,
		&cinematic_set_title_delayed_definition.definition,
		&cinematic_suppress_bsp_object_creation_definition.definition,
		&attract_mode_start_definition,
		&game_won_definition,
		&game_lost_definition,
		&game_safe_to_save_definition,
		&game_all_quiet_definition,
		&game_safe_to_speak_definition,
		&game_is_cooperative_definition,
		&game_save_definition,
		&game_save_cancel_definition,
		&game_save_no_timeout_definition,
		&game_save_totally_unsafe_definition,
		&game_saving_definition,
		&game_revert_definition,
		&game_reverted_definition,
		&core_save_definition,
		&core_save_name_definition.definition,
		&core_load_definition,
		&core_load_at_startup_definition,
		&core_load_name_definition.definition,
		&core_load_name_at_startup_definition.definition,
		&game_skip_ticks_definition.definition,
		&sound_impulse_start_definition.definition,
		&sound_impulse_time_definition.definition,
		&sound_impulse_stop_definition.definition,
		&sound_looping_predict_definition.definition,
		&sound_looping_start_definition.definition,
		&sound_looping_stop_definition.definition,
		&sound_looping_set_scale_definition.definition,
		&sound_looping_set_alternate_definition.definition,
		&debug_sounds_enable_definition.definition,
		&debug_sounds_distances_definition.definition,
		&debug_sounds_wet_definition.definition,
		&sound_enable_definition.definition,
		&sound_class_set_gain_definition.definition,
		&vehicle_hover_definition.definition,
		&players_unzoom_all_definition,
		&player_enable_input_definition.definition,
		&player_camera_control_definition.definition,
		&player_action_test_reset_definition,
		&player_action_test_jump_definition,
		&player_action_test_primary_trigger_definition,
		&player_action_test_grenade_trigger_definition,
		&player_action_test_zoom_definition,
		&player_action_test_action_definition,
		&player_action_test_accept_definition,
		&player_action_test_back_definition,
		&player_action_test_look_relative_up_definition,
		&player_action_test_look_relative_down_definition,
		&player_action_test_look_relative_left_definition,
		&player_action_test_look_relative_right_definition,
		&player_action_test_look_relative_all_directions_definition,
		&player_action_test_move_relative_all_directions_definition,
		&player_add_equipment_definition.definition,
		&hs_debug_teleport_player_definition.definition,
		&show_hud_definition.definition,
		&show_hud_help_text_definition.definition,
		&enable_hud_help_flash_definition.definition,
		&hud_help_flash_restart_definition,
		&activate_nav_point_flag_definition.definition,
		&activate_nav_point_object_definition.definition,
		&activate_team_nav_point_flag_definition.definition,
		&activate_team_nav_point_object_definition.definition,
		&deactivate_nav_point_flag_definition.definition,
		&deactivate_nav_point_object_definition.definition,
		&deactivate_team_nav_point_flag_definition.definition,
		&deactivate_team_nav_point_object_definition.definition,
		&cls_definition,
		&error_overflow_suppression_definition.definition,
		&structure_lens_flares_place_definition,
		&player_effect_set_max_translation_definition.definition,
		&player_effect_set_max_rotation_definition.definition,
		&hs_player_effect_set_max_rumble_definition.definition,
		&player_effect_start_definition.definition,
		&player_effect_stop_definition.definition,
		&hud_show_health_definition.definition,
		&hud_blink_health_definition.definition,
		&hud_show_shield_definition.definition,
		&hud_blink_shield_definition.definition,
		&hud_show_motion_sensor_definition.definition,
		&hud_blink_motion_sensor_definition.definition,
		&hud_show_crosshair_definition.definition,
		&hud_clear_messages_definition,
		&hud_set_help_text_definition.definition,
		&hud_set_objective_text_definition.definition,
		&hud_set_timer_time_definition.definition,
		&hud_set_timer_warning_time_definition.definition,
		&hud_set_timer_position_definition.definition,
		&show_hud_timer_definition.definition,
		&pause_hud_timer_definition.definition,
		&hud_get_timer_ticks_definition,
		&time_code_show_definition.definition,
		&time_code_start_definition.definition,
		&time_code_reset_definition,
		&rasterizer_decals_flush_definition,
		&rasterizer_fps_accumulate_definition,
		&rasterizer_model_ambient_reflection_tint_definition.definition,
		&rasterizer_lights_reset_for_new_map_definition,
		&script_screen_effect_set_value_definition.definition,
		&cinematic_screen_effect_start_definition.definition,
		&cinematic_screen_effect_set_convolution_definition.definition,
		&cinematic_screen_effect_set_filter_definition.definition,
		&cinematic_screen_effect_set_filter_desaturation_tint_definition.definition,
		&cinematic_screen_effect_set_video_definition.definition,
		&cinematic_screen_effect_stop_definition,
		&cinematic_set_near_clip_distance_definition.definition,
		&hs_enumerate_memory_units_definition,
		&delete_save_game_files_definition,
		&fast_setup_network_server_definition,
		&profile_unlock_solo_levels_definition,
		&player0_look_invert_pitch_definition.definition,
		&player0_look_pitch_is_inverted_definition,
		&player0_joystick_set_is_normal_definition,
		&ui_widget_show_path_definition.definition,
		&display_scenario_help_definition.definition,
		&hs_network_game_start_now_definition,
		&xbox_set_machine_name_definition.definition,
	},
	{
		"hs_update",
		NONE,
		TRUE,
	},
	{
		hs_enumerate_special_form_names,
		hs_enumerate_script_type_names,
		hs_enumerate_type_names,
		hs_enumerate_function_names,
		hs_enumerate_script_names,
		hs_enumerate_variable_names,
		hs_enumerate_ai_names,
		hs_enumerate_ai_command_list_names,
		hs_enumerate_starting_profile_names,
		hs_enumerate_conversation_names,
		hs_enumerate_object_names,
		hs_enumerate_trigger_volume_names,
		hs_enumerate_cutscene_flag_names,
		hs_enumerate_cutscene_camera_point_names,
		hs_enumerate_cutscene_title_names,
		hs_enumerate_cutscene_recording_names,
		hs_enumerate_navpoints,
		hs_enumerate_hud_messages,
	},
};

/* ---------- public code */

boolean hs_scenario_merge(
	struct scenario *scenario,
	struct scenario *source_scenario)
{
	boolean success = TRUE;
	struct tag_block *source_files;
	short source_file_index;

	source_file_index = 0;
	source_files = &source_scenario->hs_source_files;

	for (; source_file_index<source_files->count; source_file_index++)
	{
		struct hs_source_file *source_file;
		struct tag_block *files;
		short file_index;

		source_file = TAG_BLOCK_GET_ELEMENT(source_files, source_file_index, struct hs_source_file);
		files = &scenario->hs_source_files;
		for (file_index = 0; file_index<files->count; file_index++)
		{
			struct hs_source_file *file;

			file = TAG_BLOCK_GET_ELEMENT(files, file_index, struct hs_source_file);
			if (_stricmp(source_file->name, file->name) == 0)
				break;
		}
		if (file_index == files->count)
		{
			short new_file_index;

			new_file_index = tag_block_add_element(files);
			if (new_file_index != NONE)
			{
				struct hs_source_file *file;

				file = TAG_BLOCK_GET_ELEMENT(files, new_file_index, struct hs_source_file);
				csstrcpy(file->name, source_file->name);
				if (tag_data_resize(&file->source, source_file->source.size))
				{
					csmemcpy(file->source.address, source_file->source.address, source_file->source.size);
				}
				else
				{
					success = FALSE;
				}
			}
			else
			{
				success = FALSE;
			}
		}
	}
	tag_block_resize(&scenario->hs_scripts, 0);

	return success;
}

static void hs_allocate(
	void)
{
	struct scenario *scenario;

	scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	if (scenario &&
		scenario->hs_syntax_data.size ==
			sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node))
	{
		return;
	}

	hs_syntax_data = data_new(
		"script node",
		MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO,
		sizeof(struct hs_syntax_node));
	if (hs_syntax_data)
	{
		data_make_valid(hs_syntax_data);
		if (scenario)
		{
			match_free("c:\\halo\\SOURCE\\hs\\hs.c", 336, scenario->hs_syntax_data.address);
			scenario->hs_syntax_data.address = hs_syntax_data;
			scenario->hs_syntax_data.size =
				sizeof(struct data_array)+MAXIMUM_HS_SYNTAX_NODES_PER_SCENARIO*sizeof(struct hs_syntax_node);
			tag_data_resize(&scenario->hs_string_constants, 0x400);
			tag_block_resize(&scenario->hs_scripts, 0);
		}
		else
		{
			hs_syntax_data_allocated = TRUE;
		}
	}
	else
	{
		error(0, "couldn't allocate script syntax data");
	}
	return;
}

void hs_dispose(
	void)
{
	hs_runtime_dispose_from_old_map();
	object_lists_dispose();
	return;
}

void hs_initialize(
	void)
{
	struct scenario *scenario;

	match_vassert(
		"c:\\halo\\SOURCE\\hs\\hs.c",
		245,
		hs_type_names[48],
		"you can't add an hs type without defining its name.");
	object_lists_initialize();
	hs_runtime_initialize();
	scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	hs_allocate();
	if (scenario && scenario->hs_syntax_data.size)
		hs_scenario_postprocess(FALSE);
	object_lists_initialize_for_new_map();
	hs_runtime_initialize_for_new_map();
	return;
}

void hs_hack(
	void)
{
	struct scenario *scenario;

	if (hs_rebuild_source())
	{
		hs_compile_source();
		hs_dispose_from_old_map();
		scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
		hs_allocate();
		if (scenario && scenario->hs_syntax_data.size)
			hs_scenario_postprocess(FALSE);
		object_lists_initialize_for_new_map();
		hs_runtime_initialize_for_new_map();
	}
	return;
}

/* January inlines this helper in hs_compile_and_evaluate (a dead loop's alignment pad stays) and
 * keeps an out-of-line private-ABI copy for hs_compile_source */
static void hs_compile_source_error(
	char const *error_source,
	char *error_message,
	struct hs_source_file const *source_file,
	char const *source)
{
	char *newline = NULL;

	if (error_message)
	{
		newline = strchr(error_message, '\n');
		if (newline)
			*newline = 0;
	}

	if (source_file && newline)
	{
		short line = 1;

		while (newline > source)
		{
			if (*newline == '\n')
				line++;
			newline--;
		}
		error(2, "[%s line %d] %s: %s", source_file->name, line, error_source, error_message);
	}
	else
	{
		error(2, "%s: %s", error_source, error_message);
	}
	return;
}

static boolean hs_compile_source(
	void)
{
	struct scenario *scenario;
	struct tag_block const *source_files;
	boolean success;
	char const *error_source;
	char const *error_message;
	short source_file_index;

	scenario = global_scenario_get();
	success = TRUE;
	hs_compile_initialize(TRUE);
	source_file_index = 0;
	source_files = &scenario->hs_source_files;
	while (source_file_index<source_files->count)
	{
		struct hs_source_file const *source_file;
		char const *source;

		source_file = TAG_BLOCK_GET_ELEMENT(
			source_files,
			source_file_index,
			struct hs_source_file);
		/* BUG (original): January and HCEA pass these outputs in reverse; a corrected build
		 * passes &error_message, &error_source */
		hs_compile(
			source_file->source.size,
			tag_data_get_pointer(
				&source_file->source,
				0,
				source_file->source.size),
			&error_source,
			&error_message);
		if (error_source)
		{
			source = tag_data_get_pointer(
				&source_file->source,
				0,
				source_file->source.size);
			hs_compile_source_error(
				error_source,
				(char *)error_message,
				source_file,
				source);
			success = FALSE;
		}
		source_file_index++;
	}

	if (success)
		console_printf(FALSE, "scripts successfully compiled.");
	hs_compile_dispose();
	return success;
}

static boolean hs_rebuild_source_file(
	struct file_reference *file)
{
	struct scenario *scenario;
	struct tag_block *source_files;
	struct hs_source_file *source_file;
	void *source;
	unsigned long source_size;
	short source_file_index;
	char name[MAXIMUM_FILENAME_LENGTH+1];

	scenario = global_scenario_get();
	if (file_exists(file))
	{
		source_files = &scenario->hs_source_files;
		source_file_index = tag_block_add_element(source_files);
		if (source_file_index != NONE)
		{
			source_file = TAG_BLOCK_GET_ELEMENT(
				source_files,
				source_file_index,
				struct hs_source_file);
			source = file_read_into_memory(file, &source_size);
			if (source)
			{
				if (tag_data_resize(&source_file->source, source_size))
				{
					file_reference_get_name(file, FLAG(_name_filename_bit), name);
					csstrncpy(source_file->name, name, NUMBEROF(source_file->name)-1);
					source_file->name[NUMBEROF(source_file->name)-1] = 0;
					csmemcpy(
						tag_data_get_pointer(&source_file->source, 0, source_size),
						source,
						source_size);
					return TRUE;
				}
				error(2, "maximum source file size exceeded.");
				return FALSE;
			}
			error(2, "couldn't read source file into memory.");
			return FALSE;
		}
		error(2, "maximum source files per scenario exceeded.");
	}
	return FALSE;
}

static boolean hs_rebuild_source(
	void)
{
	boolean success = TRUE;
	char scenario_path[MAXIMUM_FILENAME_LENGTH+1];
	struct file_reference global_scripts;
	char extension[MAXIMUM_FILENAME_LENGTH+1];
	struct file_reference scripts_directory;
	struct file_reference source_files[8];
	short source_file_count;
	short source_file_index;

	tag_block_resize(&global_scenario_get()->hs_source_files, 0);
	sprintf(scenario_path, "data\\%s", tag_get_name(global_scenario_index));
	sprintf(strrchr(scenario_path, '\\') + 1, "scripts");
	file_reference_create_from_path(
		&global_scripts,
		"data\\global_scripts.hsc",
		FALSE);
	if (file_exists(&global_scripts))
		success = hs_rebuild_source_file(&global_scripts);

	file_reference_create_from_path(&scripts_directory, scenario_path, TRUE);
	source_file_count = (short)find_files(
		0,
		&scripts_directory,
		NUMBEROF(source_files),
		source_files);
	qsort(
		source_files,
		source_file_count,
		sizeof(struct file_reference),
		(int (__cdecl *)(void const *, void const *))alphabetize_file_references);
	for (source_file_index = 0; source_file_index<source_file_count; source_file_index++)
	{
		file_reference_get_name(
			&source_files[source_file_index],
			FLAG(_name_extension_bit),
			extension);
		if (csstrcmp(extension, "hsc") == 0 &&
			!hs_rebuild_source_file(&source_files[source_file_index]))
		{
			success = FALSE;
		}
	}
	return success;
}

void hs_update(
	void)
{
	profile_enter(hs_function_table.profile);
	hs_runtime_update();
	profile_exit(hs_function_table.profile);
	return;
}

void hs_node_gc(
	void)
{
	long syntax_node_index;

	for (syntax_node_index = data_next_index(hs_syntax_data, NONE);
		syntax_node_index != NONE;
		syntax_node_index = data_next_index(hs_syntax_data, syntax_node_index))
	{
		struct hs_syntax_node *syntax_node;

		syntax_node = (struct hs_syntax_node *)datum_get(hs_syntax_data, syntax_node_index);
		if (!(((byte)syntax_node->flags) & 8))
			datum_delete(hs_syntax_data, syntax_node_index);
	}
	return;
}

void hs_initialize_for_new_map(
	void)
{
	struct scenario *scenario;

	scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
	hs_allocate();
	if (scenario && scenario->hs_syntax_data.size)
		hs_scenario_postprocess(FALSE);
	object_lists_initialize_for_new_map();
	hs_runtime_initialize_for_new_map();
	return;
}

void hs_dispose_from_old_map(
	void)
{
	if (hs_syntax_data)
	{
		hs_node_gc();
		if (hs_syntax_data_allocated)
		{
			data_make_invalid(hs_syntax_data);
			data_dispose(hs_syntax_data);
			hs_syntax_data_allocated = FALSE;
		}
		hs_syntax_data = NULL;
	}
	hs_runtime_dispose_from_old_map();
	object_lists_dispose_from_old_map();
	return;
}

void hs_recompile(
	void)
{
	hs_recompile_pending = TRUE;
	return;
}

struct hs_function_definition *hs_function_get(
	short function_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\hs\\hs.c",
		522,
		function_index>=0 && function_index<hs_function_table_count);

	return hs_function_table.functions[function_index];
}

short hs_find_script_by_name(
	char const *name)
{
	short script_index;

	if (global_scenario_index != NONE)
	{
		struct scenario *scenario;

		scenario = global_scenario_get();
		for (script_index = 0; script_index<scenario->hs_scripts.count; script_index++)
		{
			struct hs_script const *script;

			script = TAG_BLOCK_GET_ELEMENT(&scenario->hs_scripts, script_index, struct hs_script);
			if (csstrcmp(name, script->name) == 0)
				return script_index;
		}
	}

	return NONE;
}

short hs_find_global_by_name(
	char const *name)
{
	short global_index;

	for (global_index = 0; global_index<hs_external_global_count; global_index++)
	{
		if (_stricmp(name, hs_global_external_get(global_index)->name) == 0)
			return global_index | 0x8000;
	}

	if (global_scenario_index != NONE)
	{
		struct scenario *scenario;

		scenario = global_scenario_get();
		for (global_index = 0; global_index<scenario->hs_globals.count; global_index++)
		{
			struct hs_global const *global;

			global = TAG_BLOCK_GET_ELEMENT(
				&global_scenario_get()->hs_globals,
				global_index,
				struct hs_global);
			if (_stricmp(name, global->name) == 0)
				return global_index & ~0x8000;
		}
	}

	return NONE;
}

short hs_find_tag_reference_by_index(
	long tag_index)
{
	short reference_index;

	if (global_scenario_index != NONE)
	{
		struct scenario *scenario;

		scenario = global_scenario_get();
		for (reference_index = 0; reference_index<scenario->hs_references.count; reference_index++)
		{
			struct hs_reference const *reference;

			reference = TAG_BLOCK_GET_ELEMENT(&scenario->hs_references, reference_index, struct hs_reference);
			if (reference->reference.index == tag_index)
				return reference_index;
		}
	}

	return NONE;
}

struct hs_external_global_definition *hs_global_external_get(
	short global_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\hs\\hs.c",
		576,
		global_index>=0 && global_index<hs_external_global_count);

	return hs_external_globals[global_index];
}

short hs_global_get_type(
	short global_index)
{
	if (global_index & 0x8000)
		return hs_global_external_get(global_index & 0x7FFF)->type;

	return TAG_BLOCK_GET_ELEMENT(
		&global_scenario_get()->hs_globals,
		global_index & 0x7FFF,
		struct hs_global)->type;
}

char const *hs_global_get_name(
	short global_index)
{
	if (global_index & 0x8000)
		return hs_global_external_get(global_index & 0x7FFF)->name;

	return TAG_BLOCK_GET_ELEMENT(
		&global_scenario_get()->hs_globals,
		global_index & 0x7FFF,
		struct hs_global)->name;
}

short hs_find_function_by_name(
	char const *name)
{
	short function_index;

	for (function_index = 0; function_index<hs_function_table_count; function_index++)
	{
		if (_stricmp(hs_function_table.functions[function_index]->name, name) == 0)
			return function_index;
	}

	return NONE;
}

boolean hs_evaluate_by_name(
	char const *name)
{
	short script_index;

	script_index = hs_find_script_by_name(name);
	if (script_index != NONE)
	{
		struct hs_script const *script;

		script = TAG_BLOCK_GET_ELEMENT(
			&global_scenario_get()->hs_scripts,
			script_index,
			struct hs_script);
		hs_runtime_evaluate(script->root_expression_index);
		return TRUE;
	}

	return FALSE;
}

static void hs_get_function_parameters_string(
	short function_index,
	char *result)
{
	struct hs_function_definition const *function;
	short parameter_index;

	function = hs_function_get(function_index);
	sprintf(result, "(%s", function->name);
	if (function->usage)
	{
		sprintf(result + csstrlen(result), " %s", function->usage);
	}
	else
	{
		for (parameter_index = 0; parameter_index<function->parameter_count; parameter_index++)
		{
			csstrcat(result, " <");
			csstrcat(result, hs_type_names[function->parameter_types[parameter_index]]);
			csstrcat(result, ">");
		}
	}
	csstrcat(result, ")");
	return;
}

static void hs_get_function_documentation_string(
	short function_index,
	char *result)
{
	csstrcpy(result, hs_function_get(function_index)->help);

	return;
}

void hs_help(
	char const *function_name)
{
	char result[2048];
	short function_index;

	function_index = hs_find_function_by_name(function_name);
	if (function_index != NONE)
	{
		hs_get_function_parameters_string(function_index, result);
		console_printf(FALSE, result);
		hs_get_function_documentation_string(function_index, result);
		console_printf(FALSE, result);
	}
	return;
}

void hs_doc(
	void)
{
	char result[2048];
	FILE *file;
	short function_index;

	file = fopen("hs_doc.txt", "w");
	for (function_index = 0; function_index<hs_function_table_count; function_index++)
	{
		hs_function_get(function_index);
		hs_get_function_parameters_string(function_index, result);
		fprintf(file, "%s\r\n", result);
		csstrcpy(result, hs_function_get(function_index)->help);
		fprintf(file, "%s\r\n\r\n", result);
	}
	fclose(file);
	return;
}

short hs_tokens_enumerate(
	char const *substring,
	long type_flags,
	char const **results,
	short maximum_count)
{
	short type_index;

	match_assert(
		"c:\\halo\\SOURCE\\hs\\hs.c",
		920,
		!enumeration_results);
	hs_enumeration_maximum_count = maximum_count;
	hs_enumeration_result_count = 0;
	enumeration_results = results;
	hs_enumeration_substring = substring;
	if (!substring)
		hs_enumeration_substring = "";

	for (type_index = 0; type_index<18; type_index++)
	{
		match_assert(
			"c:\\halo\\SOURCE\\hs\\hs.c",
			929,
			hs_token_enumerators[type_index]);
		if (type_flags & (1 << type_index))
			hs_token_enumerators[type_index]();
	}

	qsort(
		(void *)results,
		hs_enumeration_result_count,
		sizeof(*results),
		(int (__cdecl *)(void const *, void const *))alphabetize);
	enumeration_results = NULL;
	return hs_enumeration_result_count;
}

static void hs_tokens_enumerate_add_string(
	char const *token)
{
	match_assert(
		"c:\\halo\\SOURCE\\hs\\hs.c",
		666,
		enumeration_results);
	if (hs_enumeration_result_count<hs_enumeration_maximum_count &&
		_strnicmp(token, hs_enumeration_substring, csstrlen(hs_enumeration_substring)) == 0)
	{
		short result_index;
		short new_result_count;

		result_index = hs_enumeration_result_count;
		new_result_count = (short)(result_index + 1);
		enumeration_results[result_index] = token;
		hs_enumeration_result_count = new_result_count;
	}
	return;
}

static void hs_enumerate_block_data(
	struct tag_block const *block,
	short name_offset,
	long element_size)
{
	short element_index;

	for (element_index = 0; element_index<block->count; element_index++)
	{
		byte const *element;

		element = (byte const *)tag_block_get_element_with_size(
			block,
			element_index,
			element_size);
		hs_tokens_enumerate_add_string((char const *)(element + name_offset));
	}
	return;
}

static void hs_enumerate_scenario_data(
	short block_offset,
	short name_offset,
	long element_size)
{
	if (global_scenario_index != NONE)
	{
		struct tag_block const *block;

		block = (struct tag_block const *)((byte const *)global_scenario_get() + block_offset);
		hs_enumerate_block_data(block, name_offset, element_size);
	}
	return;
}

static void hs_enumerate_from_string_list(
	char const **names,
	short first,
	short last)
{
	short index;

	for (index = first; index<last; index++)
		hs_tokens_enumerate_add_string(names[index]);
	return;
}

static void hs_enumerate_special_form_names(
	void)
{
	hs_tokens_enumerate_add_string("script");
	hs_tokens_enumerate_add_string("global");
	return;
}

static void hs_enumerate_script_type_names(
	void)
{
	hs_enumerate_from_string_list(hs_script_type_names, 0, 5);
	return;
}

static void hs_enumerate_type_names(
	void)
{
	hs_enumerate_from_string_list(hs_type_names, 4, 49);
	return;
}

static void hs_enumerate_function_names(
	void)
{
	short function_index;

	for (function_index = 0; function_index<hs_function_table_count; function_index++)
		hs_tokens_enumerate_add_string(hs_function_get(function_index)->name);
	return;
}

static void hs_enumerate_script_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, hs_scripts), 0, sizeof(struct hs_script));
	return;
}

static void hs_enumerate_variable_names(
	void)
{
	short global_index;

	for (global_index = 0; global_index<hs_external_global_count; global_index++)
		hs_tokens_enumerate_add_string(hs_global_external_get(global_index)->name);

	if (global_scenario_index != NONE)
	{
		struct scenario *scenario;
		struct tag_block const *globals;

		scenario = global_scenario_get();
		globals = &scenario->hs_globals;
		for (global_index = 0; global_index<globals->count; global_index++)
		{
			struct hs_global const *global;

			global = TAG_BLOCK_GET_ELEMENT(globals, global_index, struct hs_global);
			hs_tokens_enumerate_add_string(global->name);
		}
	}
	return;
}

static void hs_enumerate_ai_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, ai_encounters), 0, sizeof(struct encounter_definition));
	return;
}

static void hs_enumerate_ai_command_list_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, ai_command_lists), 0, sizeof(struct ai_command_list_definition));
	return;
}

static void hs_enumerate_starting_profile_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, starting_profiles), 0, scenario_starting_profile_size);
	return;
}

static void hs_enumerate_conversation_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, ai_conversations), 0, scenario_conversation_definition_size);
	return;
}

static void hs_enumerate_object_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, object_names), 0, sizeof(struct scenario_object_name));
	return;
}

static void hs_enumerate_trigger_volume_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, trigger_volumes), 4, sizeof(struct scenario_trigger_volume));
	return;
}

static void hs_enumerate_cutscene_flag_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, cutscene_flags), 4, scenario_cutscene_flag_size);
	return;
}

static void hs_enumerate_cutscene_camera_point_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, cutscene_camera_points), 4, sizeof(struct scenario_cutscene_camera_point));
	return;
}

static void hs_enumerate_cutscene_title_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, cutscene_chapter_titles), 4, scenario_cutscene_chapter_title_size);
	return;
}

static void hs_enumerate_cutscene_recording_names(
	void)
{
	hs_enumerate_scenario_data(offsetof(struct scenario, recorded_animations), 0, sizeof(struct recorded_animation_definition));
	return;
}

static void hs_enumerate_navpoints(
	void)
{
	long hud_globals_index;

	hud_globals_index = interface_get_tag_index(_interface_hud_globals);
	if (hud_globals_index != NONE)
	{
		struct hud_globals_definition const *hud_globals;

		hud_globals = hud_globals_definition_get(
			interface_get_tag_index(_interface_hud_globals));
		hs_enumerate_block_data(
			&hud_globals->waypoint.arrows,
			0,
			sizeof(struct hud_waypoint_arrow_definition));
	}
	return;
}

static void hs_enumerate_hud_messages(
	void)
{
	if (global_scenario_get()->hud_messages.index != NONE)
	{
		struct hud_message_text_definition const *hud_messages;

		hud_messages = hud_message_text_definition_get(
			global_scenario_get()->hud_messages.index);
		hs_enumerate_block_data(&hud_messages->messages, 0, sizeof(struct hud_message_definition));
	}
	return;
}

static long alphabetize_file_references(
	struct file_reference const *left,
	struct file_reference const *right)
{
	char left_name[MAXIMUM_FILENAME_LENGTH+1];
	char right_name[MAXIMUM_FILENAME_LENGTH+1];

	file_reference_get_name(left, FLAG(_name_filename_bit), left_name);
	file_reference_get_name(right, FLAG(_name_filename_bit), right_name);
	return _stricmp(left_name, right_name);
}

static long alphabetize(
	char const **left,
	char const **right)
{
	return _stricmp(*left, *right);
}

HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(game_safe_to_save_evaluate, game_safe_to_save)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(game_all_quiet_evaluate, game_all_quiet)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(game_safe_to_speak_evaluate, game_safe_to_speak)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(game_is_cooperative_evaluate, game_is_cooperative)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(main_saving_map_evaluate, main_saving_map)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(game_state_reverted_evaluate, game_state_reverted)

HS_EVALUATE_NO_ARGUMENTS(hs_object_destroy_all_evaluate, hs_object_destroy_all)
HS_EVALUATE_NO_ARGUMENTS(numeric_countdown_timer_stop_evaluate, numeric_countdown_timer_stop)
HS_EVALUATE_NO_ARGUMENTS(numeric_countdown_timer_restart_evaluate, numeric_countdown_timer_restart)
HS_EVALUATE_NO_ARGUMENTS(objects_dump_memory_evaluate, objects_dump_memory)
HS_EVALUATE_NO_ARGUMENTS(garbage_collect_now_evaluate, garbage_collect_now)
HS_EVALUATE_NO_ARGUMENTS(object_pvs_clear_evaluate, object_pvs_clear)
HS_EVALUATE_NO_ARGUMENTS(breakable_surfaces_reset_evaluate, breakable_surfaces_reset)
HS_EVALUATE_NO_ARGUMENTS(cheat_all_powerups_evaluate, cheat_all_powerups)
HS_EVALUATE_NO_ARGUMENTS(cheat_all_weapons_evaluate, cheat_all_weapons)
HS_EVALUATE_NO_ARGUMENTS(cheat_all_vehicles_evaluate, cheat_all_vehicles)
HS_EVALUATE_NO_ARGUMENTS(cheat_teleport_to_camera_evaluate, cheat_teleport_to_camera)
HS_EVALUATE_NO_ARGUMENTS(cheat_active_camouflage_evaluate, cheat_active_camouflage)
HS_EVALUATE_NO_ARGUMENTS(scripting_magic_melee_attack_evaluate, scripting_magic_melee_attack)
HS_EVALUATE_NO_ARGUMENTS(cheats_load_evaluate, cheats_load)
HS_EVALUATE_NO_ARGUMENTS(ai_scripting_erase_all_evaluate, ai_scripting_erase_all)
HS_EVALUATE_NO_ARGUMENTS(ai_scripting_deselect_evaluate, ai_scripting_deselect)
HS_EVALUATE_NO_ARGUMENTS(ai_scripting_reconnect_evaluate, ai_scripting_reconnect)
HS_EVALUATE_NO_ARGUMENTS(director_save_camera_evaluate, director_save_camera)
HS_EVALUATE_NO_ARGUMENTS(director_load_camera_evaluate, director_load_camera)
HS_EVALUATE_NO_ARGUMENTS(players_unzoom_all_evaluate, players_unzoom_all)
HS_EVALUATE_NO_ARGUMENTS(player_control_action_test_reset_evaluate, player_control_action_test_reset)
HS_EVALUATE_NO_ARGUMENTS(main_reset_map_evaluate, main_reset_map)
HS_EVALUATE_NO_ARGUMENTS(main_print_version_evaluate, main_print_version)
HS_EVALUATE_NO_ARGUMENTS(main_set_game_connection_to_film_playback_evaluate, main_set_game_connection_to_film_playback)
HS_EVALUATE_NO_ARGUMENTS(texture_cache_flush_evaluate, texture_cache_flush)
HS_EVALUATE_NO_ARGUMENTS(sound_cache_flush_evaluate, sound_cache_flush)
HS_EVALUATE_NO_ARGUMENTS(debug_dump_memory_evaluate, debug_dump_memory)
HS_EVALUATE_NO_ARGUMENTS(debug_dump_memory_by_file_evaluate, debug_dump_memory_by_file)
HS_EVALUATE_NO_ARGUMENTS(profile_initialize_evaluate, profile_initialize)
HS_EVALUATE_NO_ARGUMENTS(ai_profile_change_render_spray_evaluate, ai_profile_change_render_spray)
HS_EVALUATE_NO_ARGUMENTS(ai_debug_sound_point_set_evaluate, ai_debug_sound_point_set)
HS_EVALUATE_NO_ARGUMENTS(cinematic_start_evaluate, cinematic_start)
HS_EVALUATE_NO_ARGUMENTS(cinematic_stop_evaluate, cinematic_stop)
HS_EVALUATE_NO_ARGUMENTS(cinematic_skip_start_evaluate, cinematic_skip_start)
HS_EVALUATE_NO_ARGUMENTS(cinematic_skip_stop_evaluate, cinematic_skip_stop)
HS_EVALUATE_NO_ARGUMENTS(attract_mode_start_evaluate, attract_mode_start)
HS_EVALUATE_NO_ARGUMENTS(main_won_map_evaluate, main_won_map)
HS_EVALUATE_NO_ARGUMENTS(main_lost_map_evaluate, main_lost_map)
HS_EVALUATE_NO_ARGUMENTS(main_save_map_safe_evaluate, main_save_map_safe)
HS_EVALUATE_NO_ARGUMENTS(main_save_cancel_evaluate, main_save_cancel)
HS_EVALUATE_NO_ARGUMENTS(main_save_map_no_timeout_evaluate, main_save_map_no_timeout)
HS_EVALUATE_NO_ARGUMENTS(main_save_map_nonsafe_evaluate, main_save_map_nonsafe)
HS_EVALUATE_NO_ARGUMENTS(main_revert_map_evaluate, main_revert_map)
HS_EVALUATE_NO_ARGUMENTS(main_load_core_evaluate, main_load_core)
HS_EVALUATE_NO_ARGUMENTS(main_load_core_at_startup_evaluate, main_load_core_at_startup)
HS_EVALUATE_NO_ARGUMENTS(main_save_core_evaluate, main_save_core)
HS_EVALUATE_NO_ARGUMENTS(scripted_hud_restart_flashing_evaluate, scripted_hud_restart_flashing)
HS_EVALUATE_NO_ARGUMENTS(terminal_clear_evaluate, terminal_clear)
HS_EVALUATE_NO_ARGUMENTS(structure_lens_flares_place_evaluate, structure_lens_flares_place)
HS_EVALUATE_NO_ARGUMENTS(scripted_hud_messages_clear_evaluate, scripted_hud_messages_clear)
HS_EVALUATE_NO_ARGUMENTS(scripted_hud_time_code_reset_evaluate, scripted_hud_time_code_reset)
HS_EVALUATE_NO_ARGUMENTS(rasterizer_decals_flush_evaluate, rasterizer_decals_flush)
HS_EVALUATE_NO_ARGUMENTS(rasterizer_fps_accumulate_evaluate, rasterizer_fps_accumulate)
HS_EVALUATE_NO_ARGUMENTS(rasterizer_lights_reset_for_new_map_evaluate, rasterizer_lights_reset_for_new_map)
HS_EVALUATE_NO_ARGUMENTS(rasterizer_screen_effect_stop_evaluate, rasterizer_screen_effect_stop)
HS_EVALUATE_NO_ARGUMENTS(enumerate_memory_units_test_evaluate, enumerate_memory_units_test)
HS_EVALUATE_NO_ARGUMENTS(saved_game_files_delete_all_custom_profiles_evaluate, saved_game_files_delete_all_custom_profiles)
HS_EVALUATE_NO_ARGUMENTS(player_ui_fast_setup_network_server_evaluate, player_ui_fast_setup_network_server)
HS_EVALUATE_NO_ARGUMENTS(player_ui_activate_all_solo_levels_evaluate, player_ui_activate_all_solo_levels)
HS_EVALUATE_NO_ARGUMENTS(network_game_client_request_immediate_start_evaluate, network_game_client_request_immediate_start)
HS_EVALUATE_NO_ARGUMENTS(hs_doc_evaluate, hs_doc)
HS_EVALUATE_NO_OP(tag_groups_dump_memory_evaluate)
HS_EVALUATE_NO_OP(radiosity_hack_start_evaluate)
HS_EVALUATE_NO_OP(radiosity_hack_save_evaluate)
HS_EVALUATE_NO_OP(radiosity_hack_find_point_evaluate)
HS_EVALUATE_RETURN_LONG(hs_players_evaluate, hs_players)
HS_EVALUATE_RETURN_LONG(game_time_get_evaluate, game_time_get)
HS_EVALUATE_RETURN_SHORT(scripted_camera_time_evaluate, scripted_camera_time)
HS_EVALUATE_RETURN_SHORT(game_difficulty_level_get_ignore_easy_evaluate, game_difficulty_level_get_ignore_easy)
HS_EVALUATE_RETURN_SHORT(game_difficulty_level_get_evaluate, game_difficulty_level_get)
HS_EVALUATE_RETURN_SHORT(global_structure_bsp_index_get_evaluate, global_structure_bsp_index_get)
HS_EVALUATE_RETURN_SHORT(scripted_hud_get_timer_ticks_evaluate, scripted_hud_get_timer_ticks)
HS_EVALUATE_SHORT_FROM_LONG(object_list_count_evaluate, object_list_count)
HS_EVALUATE_SHORT_FROM_UNSIGNED_SHORT(numeric_countdown_timer_get_evaluate, numeric_countdown_timer_get)
HS_EVALUATE_SHORT_FROM_LONG(recorded_animation_get_time_left_evaluate, recorded_animation_get_time_left)
HS_EVALUATE_SHORT_FROM_LONG(scenery_get_animation_time_evaluate, scenery_get_animation_time)
HS_EVALUATE_SHORT_FROM_LONG(unit_get_custom_animation_time_evaluate, unit_get_custom_animation_time)
HS_EVALUATE_SHORT_FROM_LONG(unit_scripting_get_grenade_count_evaluate, unit_scripting_get_grenade_count)
HS_EVALUATE_SHORT_FROM_LONG(ai_scripting_command_list_status_evaluate, ai_scripting_command_list_status)
HS_EVALUATE_SHORT_FROM_LONG(ai_scripting_going_to_vehicle_evaluate, ai_scripting_going_to_vehicle)
HS_EVALUATE_SHORT_FROM_LONG(ai_scripting_living_count_evaluate, ai_scripting_living_count)
HS_EVALUATE_SHORT_FROM_LONG(ai_scripting_swarm_count_evaluate, ai_scripting_swarm_count)
HS_EVALUATE_SHORT_FROM_LONG(ai_scripting_nonswarm_count_evaluate, ai_scripting_nonswarm_count)
HS_EVALUATE_SHORT_FROM_LONG(ai_scripting_status_evaluate, ai_scripting_status)
HS_EVALUATE_SHORT_FROM_UNSIGNED_SHORT(ai_scripting_conversation_line_evaluate, ai_scripting_conversation_line)
HS_EVALUATE_SHORT_FROM_UNSIGNED_SHORT(ai_scripting_conversation_status_evaluate, ai_scripting_conversation_status)
HS_EVALUATE_RETURN_SHORT_FROM_ARGUMENTS(vehicle_scripting_load_magic_evaluate, struct hs_arguments_long_long_long, (vehicle_scripting_load_magic(arguments->value0, arguments->value1, arguments->value2)))
HS_EVALUATE_RETURN_SHORT_FROM_ARGUMENTS(vehicle_scripting_unload_evaluate, struct hs_arguments_long_long, (vehicle_scripting_unload(arguments->value0, (char const *)(unsigned long)arguments->value1)))
HS_EVALUATE_LONG_FROM_LONG(unit_scripting_unit_riders_evaluate, unit_scripting_unit_riders)
HS_EVALUATE_LONG_FROM_LONG(unit_scripting_unit_driver_evaluate, unit_scripting_unit_driver)
HS_EVALUATE_LONG_FROM_LONG(unit_scripting_unit_gunner_evaluate, unit_scripting_unit_gunner)
HS_EVALUATE_LONG_FROM_LONG(object_list_from_ai_reference_evaluate, object_list_from_ai_reference)
HS_EVALUATE_LONG_FROM_LONG(scripted_sound_time_evaluate, scripted_sound_time)
HS_EVALUATE_VOID_LONG(hs_object_destroy_evaluate, hs_object_destroy)
HS_EVALUATE_VOID_LONG(recorded_animation_kill_evaluate, recorded_animation_kill)
HS_EVALUATE_VOID_LONG(object_cannot_take_damage_evaluate, object_cannot_take_damage)
HS_EVALUATE_VOID_LONG(object_can_take_damage_evaluate, object_can_take_damage)
HS_EVALUATE_VOID_LONG(hs_objects_predict_evaluate, hs_objects_predict)
HS_EVALUATE_VOID_LONG(object_definition_predict_evaluate, object_definition_predict)
HS_EVALUATE_VOID_LONG(object_pvs_set_object_evaluate, object_pvs_set_object)
HS_EVALUATE_VOID_LONG(object_pvs_activate_evaluate, object_pvs_activate)
HS_EVALUATE_VOID_LONG(unit_open_evaluate, unit_open)
HS_EVALUATE_VOID_LONG(unit_close_evaluate, unit_close)
HS_EVALUATE_VOID_LONG(unit_kill_evaluate, unit_kill)
HS_EVALUATE_VOID_LONG(unit_kill_silent_evaluate, unit_kill_silent)
HS_EVALUATE_VOID_LONG(unit_stop_custom_animation_evaluate, unit_stop_custom_animation)
HS_EVALUATE_VOID_LONG(unit_scripting_exit_vehicle_evaluate, unit_scripting_exit_vehicle)
HS_EVALUATE_VOID_LONG(unit_scripting_doesnt_drop_items_evaluate, unit_scripting_doesnt_drop_items)
HS_EVALUATE_VOID_LONG(ai_scripting_free_evaluate, ai_scripting_free)
HS_EVALUATE_VOID_LONG(ai_scripting_free_units_evaluate, ai_scripting_free_units)
HS_EVALUATE_VOID_LONG(ai_scripting_detach_unit_evaluate, ai_scripting_detach_unit)
HS_EVALUATE_VOID_LONG(ai_scripting_detach_units_evaluate, ai_scripting_detach_units)
HS_EVALUATE_VOID_LONG(ai_scripting_place_evaluate, ai_scripting_place)
HS_EVALUATE_VOID_LONG(ai_scripting_kill_evaluate, ai_scripting_kill)
HS_EVALUATE_VOID_LONG(ai_scripting_kill_silent_evaluate, ai_scripting_kill_silent)
HS_EVALUATE_VOID_LONG(ai_scripting_erase_evaluate, ai_scripting_erase)
HS_EVALUATE_VOID_LONG(ai_scripting_select_evaluate, ai_scripting_select)
HS_EVALUATE_VOID_LONG(ai_scripting_spawn_actor_evaluate, ai_scripting_spawn_actor)
HS_EVALUATE_VOID_LONG(ai_scripting_magically_see_players_evaluate, ai_scripting_magically_see_players)
HS_EVALUATE_VOID_LONG(ai_scripting_timer_start_evaluate, ai_scripting_timer_start)
HS_EVALUATE_VOID_LONG(ai_scripting_timer_expire_evaluate, ai_scripting_timer_expire)
HS_EVALUATE_VOID_LONG(ai_scripting_attack_evaluate, ai_scripting_attack)
HS_EVALUATE_VOID_LONG(ai_scripting_defend_evaluate, ai_scripting_defend)
HS_EVALUATE_VOID_LONG(ai_scripting_retreat_evaluate, ai_scripting_retreat)
HS_EVALUATE_VOID_UNSIGNED_SHORT(hs_object_create_evaluate, hs_object_create)
HS_EVALUATE_VOID_UNSIGNED_SHORT(hs_object_create_anew_evaluate, hs_object_create_anew)
HS_EVALUATE_VOID_UNSIGNED_SHORT(object_pvs_set_camera_point_evaluate, object_pvs_set_camera_point)
HS_EVALUATE_VOID_UNSIGNED_SHORT(cheat_active_camouflage_local_player_evaluate, cheat_active_camouflage_local_player)
HS_EVALUATE_VOID_BOOLEAN(breakable_surfaces_enable_evaluate, breakable_surfaces_enable)
HS_EVALUATE_VOID_BOOLEAN(render_effects_evaluate, render_effects)
HS_EVALUATE_VOID_BOOLEAN(ai_globals_ai_active_evaluate, ai_globals_ai_active)
HS_EVALUATE_VOID_BOOLEAN(ai_globals_dialogue_triggers_enabled_evaluate, ai_globals_dialogue_triggers_enabled)
HS_EVALUATE_VOID_BOOLEAN(ai_globals_grenades_enabled_evaluate, ai_globals_grenades_enabled)
HS_EVALUATE_VOID_STRING(hs_print_evaluate, hs_print)
HS_EVALUATE_VOID_STRING(hs_object_create_containing_evaluate, hs_object_create_containing)
HS_EVALUATE_VOID_STRING(hs_object_create_anew_containing_evaluate, hs_object_create_anew_containing)
HS_EVALUATE_VOID_STRING(hs_object_destroy_containing_evaluate, hs_object_destroy_containing)
HS_EVALUATE_VOID_LONG(hs_objects_delete_by_definition_evaluate, hs_objects_delete_by_definition)
HS_EVALUATE_VOID_STRING(scripting_set_magic_base_seat_evaluate, scripting_set_magic_base_seat)
HS_EVALUATE_VOID_LONG_BOOLEAN(object_set_ranged_attack_inhibited_evaluate, object_set_ranged_attack_inhibited)
HS_EVALUATE_VOID_LONG_BOOLEAN(object_set_melee_attack_inhibited_evaluate, object_set_melee_attack_inhibited)
HS_EVALUATE_VOID_LONG_BOOLEAN(object_scripting_set_collideable_evaluate, object_scripting_set_collideable)
HS_EVALUATE_VOID_LONG_BOOLEAN(unit_scripting_can_blink_evaluate, unit_scripting_can_blink)
HS_EVALUATE_VOID_LONG_BOOLEAN(unit_aim_without_turning_evaluate, unit_aim_without_turning)
HS_EVALUATE_VOID_LONG_BOOLEAN(unit_set_enterable_by_player_evaluate, unit_set_enterable_by_player)
HS_EVALUATE_VOID_LONG_BOOLEAN(unit_scripting_impervious_evaluate, unit_scripting_impervious)
HS_EVALUATE_VOID_LONG_BOOLEAN(unit_scripting_suspended_evaluate, unit_scripting_suspended)
HS_EVALUATE_VOID_LONG_BOOLEAN(units_set_desired_flashlight_state_evaluate, units_set_desired_flashlight_state)
HS_EVALUATE_VOID_LONG_BOOLEAN(unit_set_desired_flashlight_state_evaluate, unit_set_desired_flashlight_state)
HS_EVALUATE_VOID_LONG_BOOLEAN(device_set_never_appears_locked_evaluate, device_set_never_appears_locked)
HS_EVALUATE_VOID_LONG_BOOLEAN(device_one_sided_set_evaluate, device_one_sided_set)
HS_EVALUATE_VOID_LONG_BOOLEAN(device_operates_automatically_set_evaluate, device_operates_automatically_set)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_set_respawn_evaluate, ai_scripting_set_respawn)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_set_deaf_evaluate, ai_scripting_set_deaf)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_set_blind_evaluate, ai_scripting_set_blind)
HS_EVALUATE_VOID_LONG_LONG(hs_damage_object_evaluate, hs_damage_object)
HS_EVALUATE_VOID_LONG_LONG(objects_scripting_detach_evaluate, objects_scripting_detach)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_attach_unit_evaluate, ai_scripting_attach_unit)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_attach_units_evaluate, ai_scripting_attach_units)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_attach_free_evaluate, ai_scripting_attach_free)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_magically_see_encounter_evaluate, ai_scripting_magically_see_encounter)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_magically_see_unit_evaluate, ai_scripting_magically_see_unit)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_magically_see_units_evaluate, ai_scripting_magically_see_units)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(hs_object_teleport_evaluate, hs_object_teleport)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(hs_object_set_facing_evaluate, hs_object_set_facing)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(hs_effect_new_evaluate, hs_effect_new)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(hs_damage_new_evaluate, hs_damage_new)
HS_EVALUATE_VOID_LONG_BOOLEAN(numeric_countdown_timer_set_evaluate, numeric_countdown_timer_set)
HS_EVALUATE_VOID_LONG_STRING(unit_scripting_set_emotion_animation_evaluate, unit_scripting_set_emotion_animation)
HS_EVALUATE_VOID_LONG_STRING(unit_scripting_set_seat_evaluate, unit_scripting_set_seat)
HS_EVALUATE_VOID_SHORT_BOOLEAN(device_group_change_only_once_more_set_evaluate, device_group_change_only_once_more_set)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(unit_set_emotion_evaluate, unit_set_emotion)
HS_EVALUATE_VOID_LONG_LONG_STRING(unit_scripting_enter_vehicle_evaluate, unit_scripting_enter_vehicle)

HS_EVALUATE_VOID_FROM_ARGUMENTS(
	hs_teleport_players_not_in_trigger_volume_evaluate,
	struct hs_arguments_short_word,
	hs_teleport_players_not_in_trigger_volume(arguments->value0, arguments->value1))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	hs_object_set_shield_evaluate,
	union hs_evaluation_argument,
	1,
	hs_object_set_shield(arguments[0].long_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS(
	hs_object_set_permutation_evaluate,
	struct hs_arguments_long_string_string,
	hs_object_set_permutation(arguments->value0, arguments->value1, arguments->value2))
HS_EVALUATE_VOID_FROM_ARGUMENTS(
	hs_effect_new_from_object_marker_evaluate,
	struct hs_arguments_long_long_string,
	hs_effect_new_from_object_marker(arguments->value0, arguments->value1, arguments->value2))
static void hs_objects_can_see_object_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument const *arguments;
	union hs_boolean_result result;

	result.value = 0;
	arguments = (union hs_evaluation_argument const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double degrees = arguments[2].real_value;

		result.boolean = hs_objects_can_see_object(arguments[0].long_value, arguments[1].long_value, degrees);
		hs_return(thread_index, result.value);
	}

	return;
}
static void hs_objects_can_see_flag_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument const *arguments;
	union hs_boolean_result result;

	result.value = 0;
	arguments = (union hs_evaluation_argument const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double degrees = arguments[2].real_value;

		result.boolean = hs_objects_can_see_flag(arguments[0].long_value, arguments[1].unsigned_short_value, degrees);
		hs_return(thread_index, result.value);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	hs_sound_set_gain_evaluate,
	union hs_evaluation_argument,
	1,
	hs_sound_set_gain(arguments[0].string_value, real_argument))
static void objects_scripting_set_scale_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_word const *arguments;

	arguments = (struct hs_arguments_long_real_word const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;

		objects_scripting_set_scale(arguments->value0, value1, arguments->value2);
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS(
	objects_scripting_attach_evaluate,
	struct hs_arguments_long_string_long_string,
	objects_scripting_attach(arguments->value0, arguments->value1, arguments->value2, arguments->value3))
HS_EVALUATE_VOID_LONG_BOOLEAN(object_beautify_evaluate, object_beautify)
HS_EVALUATE_VOID_FROM_ARGUMENTS(
	scenery_animation_start_evaluate,
	struct hs_arguments_long_long_string,
	scenery_animation_start(arguments->value0, arguments->value1, arguments->value2))
HS_EVALUATE_VOID_FROM_ARGUMENTS(
	scenery_animation_start_at_frame_evaluate,
	struct hs_arguments_long_long_string_word,
	scenery_animation_start_at_frame(arguments->value0, arguments->value1, arguments->value2, arguments->value3))
static void unit_scripting_set_maximum_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_real const *arguments;

	arguments = (struct hs_arguments_long_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		unit_scripting_set_maximum_vitality(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
static void units_scripting_set_maximum_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_real const *arguments;

	arguments = (struct hs_arguments_long_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		units_scripting_set_maximum_vitality(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
static void unit_scripting_set_current_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_real const *arguments;

	arguments = (struct hs_arguments_long_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		unit_scripting_set_current_vitality(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
static void units_scripting_set_current_vitality_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_real const *arguments;

	arguments = (struct hs_arguments_long_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		units_scripting_set_current_vitality(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	device_set_power_evaluate,
	union hs_evaluation_argument,
	1,
	device_set_power(arguments[0].long_value, real_argument))
static void device_set_desired_position_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument const *arguments;
	union hs_boolean_result result;

	result.value = 0;
	arguments = (union hs_evaluation_argument const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double position = arguments[1].real_value;

		result.boolean = device_set_desired_position(arguments[0].long_value, position);
		hs_return(thread_index, result.value);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	device_set_actual_position_evaluate,
	union hs_evaluation_argument,
	1,
	device_set_actual_position(arguments[0].long_value, real_argument))
static void device_group_set_desired_value_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument const *arguments;
	union hs_boolean_result result;

	result.value = 0;
	arguments = (union hs_evaluation_argument const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double desired_value = arguments[1].real_value;

		result.boolean = device_group_set_desired_value(arguments[0].short_value, desired_value);
		hs_return(thread_index, result.value);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	device_group_set_actual_value_evaluate,
	union hs_evaluation_argument,
	1,
	device_group_set_actual_value(arguments[0].short_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	ai_scripting_vehicle_enterable_distance_evaluate,
	union hs_evaluation_argument,
	1,
	ai_scripting_vehicle_enterable_distance(arguments[0].long_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	ai_scripting_follow_distance_evaluate,
	union hs_evaluation_argument,
	1,
	ai_scripting_follow_distance(arguments[0].long_value, real_argument))
static void player_effect_screen_fade_in_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real_word const *arguments;

	arguments = (struct hs_arguments_real_real_real_word const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

#ifdef HALO_ANDROID
		player_effect_screen_fade_in(*(real const *)&arguments->value0, (real)value1, (real)value2, arguments->value3);
#else
		player_effect_screen_fade_in(arguments->value0, value1, value2, arguments->value3);
#endif
		hs_return(thread_index, 0);
	}

	return;
}
static void player_effect_screen_fade_out_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real_word const *arguments;

	arguments = (struct hs_arguments_real_real_real_word const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

#ifdef HALO_ANDROID
		player_effect_screen_fade_out(*(real const *)&arguments->value0, (real)value1, (real)value2, arguments->value3);
#else
		player_effect_screen_fade_out(arguments->value0, value1, value2, arguments->value3);
#endif
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	cinematic_set_title_delayed_evaluate,
	union hs_evaluation_argument,
	1,
	cinematic_set_title_delayed(arguments[0].short_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	scripted_sound_new_evaluate,
	union hs_evaluation_argument,
	2,
	scripted_sound_new(arguments[0].long_value, arguments[1].long_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	scripted_looping_sound_start_evaluate,
	union hs_evaluation_argument,
	2,
	scripted_looping_sound_start(arguments[0].long_value, arguments[1].long_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	scripted_looping_sound_set_scale_evaluate,
	union hs_evaluation_argument,
	1,
	scripted_looping_sound_set_scale(arguments[0].long_value, real_argument))
static void debug_sound_classes_set_distances_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_real const *arguments;

	arguments = (struct hs_arguments_long_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		debug_sound_classes_set_distances((char const *)(unsigned long)arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	debug_sound_classes_set_wet_evaluate,
	union hs_evaluation_argument,
	1,
	debug_sound_classes_set_wet((char const *)(unsigned long)arguments[0].long_value, real_argument))
static void sound_class_set_gain_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_long_real_word const *arguments;

	arguments = (struct hs_arguments_long_real_word const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;

		sound_class_set_gain((char const *)(unsigned long)arguments->value0, value1, arguments->value2);
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	hud_unit_activate_nav_point_with_flag_evaluate,
	union hs_evaluation_argument,
	3,
	hud_unit_activate_nav_point_with_flag(arguments[0].unsigned_short_value, arguments[1].long_value, arguments[2].unsigned_short_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	hud_unit_activate_nav_point_with_object_evaluate,
	union hs_evaluation_argument,
	3,
	hud_unit_activate_nav_point_with_object(arguments[0].unsigned_short_value, arguments[1].long_value, arguments[2].long_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	hud_activate_team_nav_point_with_flag_evaluate,
	union hs_evaluation_argument,
	3,
	hud_activate_team_nav_point_with_flag(arguments[0].unsigned_short_value, arguments[1].unsigned_short_value, arguments[2].unsigned_short_value, real_argument))
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	hud_activate_team_nav_point_with_object_evaluate,
	union hs_evaluation_argument,
	3,
	hud_activate_team_nav_point_with_object(arguments[0].unsigned_short_value, arguments[1].unsigned_short_value, arguments[2].long_value, real_argument))
static void scripted_player_effect_set_translation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real const *arguments;

	arguments = (struct hs_arguments_real_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		scripted_player_effect_set_translation(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
static void scripted_player_effect_set_rotation_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real const *arguments;

	arguments = (struct hs_arguments_real_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		scripted_player_effect_set_rotation(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
static void scripted_player_effect_set_rumble_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real const *arguments;

	arguments = (struct hs_arguments_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;

		scripted_player_effect_set_rumble(arguments->value0, value1);
		hs_return(thread_index, 0);
	}

	return;
}
static void scripted_player_effect_start_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real const *arguments;

	arguments = (struct hs_arguments_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;

		scripted_player_effect_start(arguments->value0, value1);
		hs_return(thread_index, 0);
	}

	return;
}
static void rasterizer_model_ambient_reflection_tint_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real_real const *arguments;

	arguments = (struct hs_arguments_real_real_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;
		double value3 = arguments->value3;

		rasterizer_model_ambient_reflection_tint(arguments->value0, value1, value2, value3);
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	rasterizer_script_screen_effect_set_value_evaluate,
	union hs_evaluation_argument,
	1,
	rasterizer_script_screen_effect_set_value(arguments[0].unsigned_short_value, real_argument))
static void rasterizer_screen_effect_set_convolution_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_short_word_real_real_real const *arguments;

	arguments = (struct hs_arguments_short_word_real_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		real value2 = arguments->value2;
		real value3 = arguments->value3;
		real value4 = arguments->value4;

		rasterizer_screen_effect_set_convolution(arguments->value0, arguments->value1, value2, value3, value4);
		hs_return(thread_index, 0);
	}

	return;
}
static void rasterizer_screen_effect_set_filter_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real_real_boolean_real const *arguments;

	arguments = (struct hs_arguments_real_real_real_real_boolean_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;
		double value3 = arguments->value3;
		double value5 = arguments->value5;

		rasterizer_screen_effect_set_filter(arguments->value0, value1, value2, value3, arguments->value4, value5);
		hs_return(thread_index, 0);
	}

	return;
}
static void rasterizer_screen_effect_set_filter_desaturation_tint_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_arguments_real_real_real const *arguments;

	arguments = (struct hs_arguments_real_real_real const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double value1 = arguments->value1;
		double value2 = arguments->value2;

		rasterizer_screen_effect_set_filter_desaturation_tint(arguments->value0, value1, value2);
		hs_return(thread_index, 0);
	}

	return;
}
HS_EVALUATE_VOID_FROM_ARGUMENTS_WITH_REAL(
	rasterizer_screen_effect_set_video_evaluate,
	union hs_evaluation_argument,
	1,
	rasterizer_screen_effect_set_video(arguments[0].unsigned_short_value, real_argument))

static void hs_object_list_get_element_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	struct hs_object_list_get_element_arguments *arguments;

	arguments = (struct hs_object_list_get_element_arguments *)hs_macro_function_evaluate(
		function_index, thread_index, initialize);
	if (arguments)
	{
		hs_return(
			thread_index,
			hs_object_list_get_element(arguments->object_list_index, arguments->element_index));
	}

	return;
}

static void hs_sound_get_gain_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		union hs_real_value result;
		result.real_value = hs_sound_get_gain(arguments[0].string_value);
		hs_return(thread_index, result.long_value);
	}
	return;
}
HS_EVALUATE_REAL_FROM_LONG(unit_scripting_get_health_evaluate, unit_scripting_get_health)
HS_EVALUATE_REAL_FROM_LONG(unit_scripting_get_shield_evaluate, unit_scripting_get_shield)
HS_EVALUATE_REAL_FROM_LONG(device_get_power_evaluate, device_get_power)
HS_EVALUATE_REAL_FROM_LONG(device_get_position_evaluate, device_get_position)
HS_EVALUATE_REAL_FROM_UNSIGNED_SHORT(device_group_get_value_evaluate, device_group_get_value)
HS_EVALUATE_REAL_FROM_LONG(ai_scripting_living_fraction_evaluate, ai_scripting_living_fraction)
HS_EVALUATE_REAL_FROM_LONG(ai_scripting_strength_evaluate, ai_scripting_strength)
HS_EVALUATE_VOID_LONG(ai_scripting_maneuver_evaluate, ai_scripting_maneuver)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_maneuver_enable_evaluate, ai_scripting_maneuver_enable)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_migrate_evaluate, ai_scripting_migrate)
HS_EVALUATE_VOID_LONG_LONG_STRING(ai_scripting_migrate_and_speak_evaluate, ai_scripting_migrate_and_speak)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_migrate_by_unit_evaluate, ai_scripting_migrate_by_unit)
HS_EVALUATE_VOID_SHORT_SHORT(ai_scripting_allegiance_evaluate, ai_scripting_allegiance)
HS_EVALUATE_VOID_SHORT_SHORT(ai_scripting_allegiance_remove_evaluate, ai_scripting_allegiance_remove)
HS_EVALUATE_VOID_LONG_LONG_STRING(ai_scripting_go_to_vehicle_evaluate, ai_scripting_go_to_vehicle)
HS_EVALUATE_VOID_LONG_LONG_STRING(ai_scripting_go_to_vehicle_override_evaluate, ai_scripting_go_to_vehicle_override)
HS_EVALUATE_VOID_LONG(ai_scripting_exit_vehicle_evaluate, ai_scripting_exit_vehicle)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_braindead_evaluate, ai_scripting_braindead)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_braindead_by_unit_evaluate, ai_scripting_braindead_by_unit)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_ignore_evaluate, ai_scripting_ignore)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_prefer_target_evaluate, ai_scripting_prefer_target)
HS_EVALUATE_VOID_LONG(ai_scripting_teleport_starting_location_evaluate, ai_scripting_teleport_starting_location)
HS_EVALUATE_VOID_LONG(ai_scripting_teleport_starting_location_if_unsupported_evaluate, ai_scripting_teleport_starting_location_if_unsupported)
HS_EVALUATE_VOID_LONG(ai_scripting_renew_evaluate, ai_scripting_renew)
HS_EVALUATE_VOID_LONG(ai_scripting_try_to_fight_nothing_evaluate, ai_scripting_try_to_fight_nothing)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_try_to_fight_evaluate, ai_scripting_try_to_fight)
HS_EVALUATE_VOID_LONG(ai_scripting_try_to_fight_player_evaluate, ai_scripting_try_to_fight_player)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_command_list_evaluate, ai_scripting_command_list)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_command_list_by_unit_evaluate, ai_scripting_command_list_by_unit)
HS_EVALUATE_VOID_LONG(ai_scripting_command_list_advance_evaluate, ai_scripting_command_list_advance)
HS_EVALUATE_VOID_LONG(ai_scripting_command_list_advance_by_unit_evaluate, ai_scripting_command_list_advance_by_unit)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_force_active_evaluate, ai_scripting_force_active)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_force_active_by_unit_evaluate, ai_scripting_force_active_by_unit)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_set_return_state_evaluate, ai_scripting_set_return_state)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_set_current_state_evaluate, ai_scripting_set_current_state)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_playfight_evaluate, ai_scripting_playfight)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_vehicle_encounter_evaluate, ai_scripting_vehicle_encounter)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_vehicle_enterable_team_evaluate, ai_scripting_vehicle_enterable_team)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_vehicle_enterable_actor_type_evaluate, ai_scripting_vehicle_enterable_actor_type)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_vehicle_enterable_actors_evaluate, ai_scripting_vehicle_enterable_actors)
HS_EVALUATE_VOID_LONG(ai_scripting_vehicle_enterable_disable_evaluate, ai_scripting_vehicle_enterable_disable)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_look_at_object_evaluate, ai_scripting_look_at_object)
HS_EVALUATE_VOID_LONG(ai_scripting_stop_looking_evaluate, ai_scripting_stop_looking)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_automatic_migration_target_evaluate, ai_scripting_automatic_migration_target)
HS_EVALUATE_VOID_LONG(ai_scripting_follow_target_disable_evaluate, ai_scripting_follow_target_disable)
HS_EVALUATE_VOID_LONG(ai_scripting_follow_target_players_evaluate, ai_scripting_follow_target_players)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_follow_target_unit_evaluate, ai_scripting_follow_target_unit)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_follow_target_ai_evaluate, ai_scripting_follow_target_ai)
HS_EVALUATE_VOID_UNSIGNED_SHORT(ai_scripting_conversation_stop_evaluate, ai_scripting_conversation_stop)
HS_EVALUATE_VOID_UNSIGNED_SHORT(ai_scripting_conversation_advance_evaluate, ai_scripting_conversation_advance)
HS_EVALUATE_VOID_LONG_LONG(ai_scripting_link_activation_evaluate, ai_scripting_link_activation)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_berserk_evaluate, ai_scripting_berserk)
HS_EVALUATE_VOID_LONG_UNSIGNED_SHORT(ai_scripting_set_team_evaluate, ai_scripting_set_team)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_allow_charge_evaluate, ai_scripting_allow_charge)
HS_EVALUATE_VOID_LONG_BOOLEAN(ai_scripting_allow_dormant_evaluate, ai_scripting_allow_dormant)
HS_EVALUATE_VOID_FROM_ARGUMENTS(director_script_camera_evaluate, struct hs_arguments_boolean, (director_script_camera(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_camera_set_absolute_evaluate, struct hs_arguments_short_word, (scripted_camera_set_absolute(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_camera_set_evaluate, struct hs_arguments_word_word_long, (scripted_camera_set(arguments->value0, arguments->value1, arguments->value2)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_camera_set_animation_evaluate, struct hs_arguments_long_string, (scripted_camera_set_animation(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_LONG(scripted_camera_set_first_person_evaluate, scripted_camera_set_first_person)
HS_EVALUATE_VOID_LONG(scripted_camera_set_dead_evaluate, scripted_camera_set_dead)
HS_EVALUATE_VOID_FROM_ARGUMENTS(game_time_set_speed_evaluate, struct hs_arguments_real, (game_time_set_speed(arguments->value)))
HS_EVALUATE_VOID_STRING(game_set_game_variant_from_name_evaluate, game_set_game_variant_from_name)
HS_EVALUATE_VOID_BOOLEAN(player_input_enable_evaluate, player_input_enable)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_jump_evaluate, player_control_action_test_jump)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_primary_trigger_evaluate, player_control_action_test_primary_trigger)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_grenade_trigger_evaluate, player_control_action_test_grenade_trigger)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_zoom_evaluate, player_control_action_test_zoom)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_action_evaluate, player_control_action_test_action)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_accept_evaluate, player_control_action_test_accept)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_back_evaluate, player_control_action_test_back)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_look_relative_up_evaluate, player_control_action_test_look_relative_up)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_look_relative_down_evaluate, player_control_action_test_look_relative_down)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_look_relative_left_evaluate, player_control_action_test_look_relative_left)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_look_relative_right_evaluate, player_control_action_test_look_relative_right)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_look_relative_all_directions_evaluate, player_control_action_test_look_relative_all_directions)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player_control_action_test_move_relative_all_directions_evaluate, player_control_action_test_move_relative_all_directions)
HS_EVALUATE_VOID_FROM_ARGUMENTS(player_add_equipment_evaluate, struct hs_arguments_long_word_boolean, (player_add_equipment(arguments->value0, arguments->value1, arguments->value2)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(debug_player_teleport_evaluate, struct hs_arguments_short_word, (debug_player_teleport(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_STRING(main_set_map_name_evaluate, main_set_map_name)
HS_EVALUATE_VOID_STRING(main_set_multiplayer_map_name_evaluate, main_set_multiplayer_map_name)
HS_EVALUATE_VOID_UNSIGNED_SHORT(main_set_difficulty_evaluate, main_set_difficulty)
HS_EVALUATE_VOID_FROM_ARGUMENTS(scenario_switch_structure_bsp_evaluate, struct hs_arguments_word, (scenario_switch_structure_bsp(arguments->value)))
HS_EVALUATE_VOID_STRING(main_crash_evaluate, main_crash)
HS_EVALUATE_VOID_STRING(debug_dump_memory_for_file_evaluate, debug_dump_memory_for_file)
HS_EVALUATE_VOID_STRING(profile_dump_to_file_evaluate, profile_dump_to_file)
HS_EVALUATE_VOID_STRING(profile_sections_activate_evaluate, profile_sections_activate)
HS_EVALUATE_VOID_STRING(profile_sections_deactivate_evaluate, profile_sections_deactivate)
HS_EVALUATE_VOID_STRING(profile_graph_toggle_evaluate, profile_graph_toggle)
HS_EVALUATE_VOID_BOOLEAN(debug_pvs_evaluate, debug_pvs)
static void ai_debug_vocalize_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		ai_debug_vocalize(arguments[0].string_value, arguments[1].string_value);
		hs_return(thread_index, 0);
	}
	return;
}
HS_EVALUATE_VOID_LONG(ai_debug_teleport_to_evaluate, ai_debug_teleport_to)
HS_EVALUATE_VOID_STRING(ai_debug_speak_evaluate, ai_debug_speak)
HS_EVALUATE_VOID_STRING(ai_debug_speak_list_evaluate, ai_debug_speak_list)
HS_EVALUATE_VOID_BOOLEAN(cinematic_show_letterbox_evaluate, cinematic_show_letterbox)
HS_EVALUATE_VOID_UNSIGNED_SHORT(cinematic_set_title_evaluate, cinematic_set_title)
HS_EVALUATE_VOID_BOOLEAN(cinematic_suppress_bsp_object_creation_evaluate, cinematic_suppress_bsp_object_creation)
HS_EVALUATE_VOID_STRING(main_load_core_name_evaluate, main_load_core_name)
HS_EVALUATE_VOID_STRING(main_load_core_name_at_startup_evaluate, main_load_core_name_at_startup)
HS_EVALUATE_VOID_STRING(main_save_core_name_evaluate, main_save_core_name)
HS_EVALUATE_VOID_UNSIGNED_SHORT(main_skip_evaluate, main_skip)
HS_EVALUATE_VOID_LONG(scripted_sound_stop_evaluate, scripted_sound_stop)
HS_EVALUATE_VOID_LONG(scripted_foley_predict_evaluate, scripted_foley_predict)
HS_EVALUATE_VOID_LONG(scripted_looping_sound_stop_evaluate, scripted_looping_sound_stop)
HS_EVALUATE_VOID_LONG_BOOLEAN(scripted_looping_sound_set_alternate_evaluate, scripted_looping_sound_set_alternate)
static void debug_sound_classes_enable_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_evaluation_argument *arguments = (union hs_evaluation_argument *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		debug_sound_classes_enable(arguments[0].string_value, arguments[1].boolean_value);
		hs_return(thread_index, 0);
	}
	return;
}
HS_EVALUATE_VOID_BOOLEAN(sound_enable_evaluate, sound_enable)
HS_EVALUATE_VOID_LONG_BOOLEAN(vehicle_hover_evaluate, vehicle_hover)
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_set_flashing_state_evaluate, struct hs_arguments_boolean, (scripted_hud_set_flashing_state(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(hud_unit_deactivate_nav_point_with_flag_evaluate, struct hs_arguments_long_word, (hud_unit_deactivate_nav_point_with_flag(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(hud_unit_deactivate_nav_point_with_object_evaluate, struct hs_arguments_long_long, (hud_unit_deactivate_nav_point_with_object(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(hud_deactivate_team_nav_point_with_flag_evaluate, struct hs_arguments_short_word, (hud_deactivate_team_nav_point_with_flag(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(hud_deactivate_team_nav_point_with_object_evaluate, struct hs_arguments_short_long, (hud_deactivate_team_nav_point_with_object(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(errors_overflow_suppression_enable_evaluate, struct hs_arguments_boolean, (errors_overflow_suppression_enable(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_player_effect_stop_evaluate, struct hs_arguments_real, (scripted_player_effect_stop(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_show_health_evaluate, struct hs_arguments_boolean, (scripted_hud_show_health(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_blink_health_evaluate, struct hs_arguments_boolean, (scripted_hud_blink_health(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_show_shield_evaluate, struct hs_arguments_boolean, (scripted_hud_show_shield(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_blink_shield_evaluate, struct hs_arguments_boolean, (scripted_hud_blink_shield(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_show_motion_sensor_evaluate, struct hs_arguments_boolean, (scripted_hud_show_motion_sensor(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_blink_motion_sensor_evaluate, struct hs_arguments_boolean, (scripted_hud_blink_motion_sensor(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_show_crosshair_evaluate, struct hs_arguments_boolean, (scripted_hud_show_crosshair(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_set_state_message_evaluate, struct hs_arguments_word, (scripted_hud_set_state_message(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_set_objective_evaluate, struct hs_arguments_word, (scripted_hud_set_objective(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_set_timer_time_evaluate, struct hs_arguments_short_word, (scripted_hud_set_timer_time(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_set_timer_warning_cutoff_evaluate, struct hs_arguments_short_word, (scripted_hud_set_timer_warning_cutoff(arguments->value0, arguments->value1)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_set_timer_position_evaluate, struct hs_arguments_word_word_word, (scripted_hud_set_timer_position(arguments->value0, arguments->value1, arguments->value2)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_show_timer_evaluate, struct hs_arguments_boolean, (scripted_hud_show_timer(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_pause_timer_evaluate, struct hs_arguments_boolean, (scripted_hud_pause_timer(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_time_code_show_evaluate, struct hs_arguments_boolean, (scripted_hud_time_code_show(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(scripted_hud_time_code_start_evaluate, struct hs_arguments_boolean, (scripted_hud_time_code_start(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(rasterizer_screen_effect_start_evaluate, struct hs_arguments_boolean, (rasterizer_screen_effect_start(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(rasterizer_set_near_clip_distance_evaluate, struct hs_arguments_real, (rasterizer_set_near_clip_distance(arguments->value)))
HS_EVALUATE_VOID_FROM_ARGUMENTS(player0_look_invert_pitch_evaluate, struct hs_arguments_boolean, (player0_look_invert_pitch(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player0_look_pitch_is_inverted_evaluate, player0_look_pitch_is_inverted)
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(player0_joystick_set_is_normal_evaluate, player0_joystick_set_is_normal)
HS_EVALUATE_VOID_BOOLEAN(ui_widget_debug_show_path_evaluate, ui_widget_debug_show_path)
HS_EVALUATE_VOID_UNSIGNED_SHORT(display_scenario_help_evaluate, display_scenario_help)
HS_EVALUATE_VOID_STRING(xbox_set_machine_name_evaluate, xbox_set_machine_name)
HS_EVALUATE_VOID_STRING(hs_help_evaluate, hs_help)
HS_EVALUATE_RETURN_BOOLEAN(hs_not_evaluate, struct hs_arguments_boolean, (hs_not(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(scenario_trigger_volume_test_object_evaluate, struct hs_arguments_short_long, (scenario_trigger_volume_test_object(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(hs_trigger_volume_test_objects_any_evaluate, struct hs_arguments_short_long, (hs_trigger_volume_test_objects_any(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(hs_trigger_volume_test_objects_all_evaluate, struct hs_arguments_short_long, (hs_trigger_volume_test_objects_all(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(recorded_animation_play_evaluate, struct hs_arguments_long_word, (recorded_animation_play(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(recorded_animation_play_and_delete_evaluate, struct hs_arguments_long_word, (recorded_animation_play_and_delete(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(recorded_animation_play_and_hover_evaluate, struct hs_arguments_long_word, (recorded_animation_play_and_hover(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(lights_enable_evaluate, struct hs_arguments_boolean, (lights_enable(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(unit_start_user_animation_evaluate, struct hs_arguments_long_long_long_boolean, (unit_start_user_animation(arguments->value0, arguments->value1, arguments->value2, arguments->value3)))
HS_EVALUATE_RETURN_BOOLEAN(unit_scripting_start_user_animation_list_evaluate, struct hs_arguments_long_long_long_boolean, (unit_scripting_start_user_animation_list(arguments->value0, arguments->value1, arguments->value2, arguments->value3)))
HS_EVALUATE_RETURN_BOOLEAN(unit_custom_animation_at_frame_evaluate, struct hs_arguments_long_long_long_boolean_word, (unit_custom_animation_at_frame(arguments->value0, arguments->value1, arguments->value2, arguments->value3, arguments->value4)))
HS_EVALUATE_RETURN_BOOLEAN(unit_is_playing_custom_animation_evaluate, struct hs_arguments_long, (unit_is_playing_custom_animation(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(unit_scripting_vehicle_test_seat_list_evaluate, struct hs_arguments_long_long_long, (unit_scripting_vehicle_test_seat_list(arguments->value0, arguments->value1, arguments->value2)))
HS_EVALUATE_RETURN_BOOLEAN(unit_scripting_vehicle_test_seat_evaluate, struct hs_arguments_long_long_long, (unit_scripting_vehicle_test_seat(arguments->value0, arguments->value1, arguments->value2)))
HS_EVALUATE_RETURN_BOOLEAN(unit_scripting_has_weapon_evaluate, struct hs_arguments_long_long, (unit_scripting_has_weapon(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(unit_scripting_has_weapon_readied_evaluate, struct hs_arguments_long_long, (unit_scripting_has_weapon_readied(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN_NO_ARGUMENTS(unit_solo_player_integrated_night_vision_is_active_evaluate, unit_solo_player_integrated_night_vision_is_active)
HS_EVALUATE_RETURN_BOOLEAN(unit_get_current_flashlight_state_evaluate, struct hs_arguments_long, (unit_get_current_flashlight_state(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(ai_scripting_is_attacking_evaluate, struct hs_arguments_long, (ai_scripting_is_attacking(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(ai_scripting_conversation_evaluate, struct hs_arguments_word, (ai_scripting_conversation(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(ai_scripting_allegiance_broken_evaluate, struct hs_arguments_short_word, (ai_scripting_allegiance_broken(arguments->value0, arguments->value1)))
HS_EVALUATE_RETURN_BOOLEAN(scripted_player_control_set_camera_control_evaluate, struct hs_arguments_boolean, (scripted_player_control_set_camera_control(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(scripted_show_hud_evaluate, struct hs_arguments_boolean, (scripted_show_hud(arguments->value)))
HS_EVALUATE_RETURN_BOOLEAN(scripted_show_hud_help_text_evaluate, struct hs_arguments_boolean, (scripted_show_hud_help_text(arguments->value)))

static void hs_recompile_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	hs_recompile_pending = TRUE;
	hs_return(thread_index, 0);
	return;
}

static void random_range_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_short_result result;
	union hs_evaluation_argument const *arguments;
	word upper_bound;
	short lower_bound;
	result.value = 0;
	arguments = (union hs_evaluation_argument const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		upper_bound = arguments[1].unsigned_short_value;
		lower_bound = arguments[0].short_value;
		result.short_value = seed_random_range(get_global_random_seed_address(), lower_bound, upper_bound);
		hs_return(thread_index, result.value);
	}
	return;
}

static void real_random_range_evaluate(
	short function_index,
	long thread_index,
	boolean initialize)
{
	union hs_real_value result;
	union hs_evaluation_argument const *arguments;
	real upper_bound;
	real lower_bound;

	arguments = (union hs_evaluation_argument const *)hs_macro_function_evaluate(function_index, thread_index, initialize);
	if (arguments)
	{
		double upper = arguments[1].real_value;

		upper_bound = (real)upper;
		lower_bound = arguments[0].real_value;
		result.real_value = real_seed_random_range(get_global_random_seed_address(), lower_bound, upper_bound);
		hs_return(thread_index, result.long_value);
	}
	return;
}

boolean hs_scenario_postprocess(
	boolean restore_syntax_data)
{
	boolean success = TRUE;
	char const *error_source;
	char const *error_message;
	struct data_array *saved_syntax_data;
	struct scenario *scenario;
	boolean recompile;

	scenario = global_scenario_get();
	saved_syntax_data = hs_syntax_data;
	hs_allocate();
	recompile = scenario->hs_scripts.count == 0 && scenario->hs_source_files.count>0;
	hs_syntax_data = (struct data_array *)scenario->hs_syntax_data.address;
	hs_syntax_data->data = (char *)hs_syntax_data+sizeof(struct data_array);
	if (!recompile && hs_compile_postprocess(&error_message, &error_source))
	{
		if (scenario->hs_string_constants.size<0x400)
		{
			success = tag_data_resize(
				&scenario->hs_string_constants,
				scenario->hs_string_constants.size + 0x400);
		}
	}
	else
	{
		if (recompile)
			error(0, "recompiling scripts after scenarios were merged.");
		else if (!error_message)
			error(0, "an unspecified error occurred loading scripts");
		else if (!error_source)
			error(0, "%s", error_message);
		else
			error(0, "%s: %s", error_source, error_message);

		if (hs_compile_source() && hs_compile_postprocess(&error_message, &error_source))
		{
			success = TRUE;
		}
		else
		{
			data_delete_all(hs_syntax_data);
			if (!tag_block_resize(&scenario->hs_globals, 0) ||
				!tag_block_resize(&scenario->hs_scripts, 0) ||
				!tag_data_resize(&global_scenario_get()->hs_string_constants, 0x400))
			{
				error(0, "couldn't reset scripts.");
			}
			success = FALSE;
		}
	}
	if (restore_syntax_data)
		hs_syntax_data = saved_syntax_data;

	return success;
}

/* port: whether this machine plays in another's game (joined to its lobby
or in its game), whose host decides the game */
static boolean hs_playing_in_anothers_game(
	void)
{
	return network_game_distributed_client() ||
		(global_network_game_client_get() && !global_network_game_server_get());
}

/* port: whether a typed expression (console, telnet, cheat button) changes nothing of the game:
its names all these (display and controls), the rest numbers, strings and booleans */
static boolean hs_expression_changes_no_game(
	char const *expression)
{
	static char const *const allowed[] = {
		"set", "cls", "help", "print", "script_doc",
		"display_framerate", "framerate_throttle", "framerate_lock", "rasterizer_fps_accumulate",
		"console_dump_to_file", "terminal_render", "screenshot_size", "screenshot_count",
		"show_hud", "show_hud_help_text", "show_hud_timer", "hud_show_crosshair", "hud_show_health",
		"hud_show_motion_sensor", "hud_show_shield", "sound_enable", "sound_set_gain",
		"controls_swapped", "controls_enable_crouch", "controls_enable_doubled_spin",
		"controls_swap_doubled_spin_state",
		"player0_look_yaw_rate", "player1_look_yaw_rate", "player2_look_yaw_rate", "player3_look_yaw_rate",
		"player0_look_pitch_rate", "player1_look_pitch_rate", "player2_look_pitch_rate",
		"player3_look_pitch_rate",
		"true", "false", "on", "off",
	};
	char const *character = expression;

	while (*character)
	{
		char token[64];
		size_t length = 0;
		boolean number = TRUE;
		short index;

		if (isspace((unsigned char)*character) || *character == '(' || *character == ')')
		{
			character++;
			continue;
		}
		/* (a string, as print takes) */
		if (*character == '"')
		{
			character = strchr(character + 1, '"');
			if (!character)
				return FALSE;
			character++;
			continue;
		}
		while (*character && !isspace((unsigned char)*character) && *character != '(' && *character != ')' &&
			*character != '"')
		{
			if (length + 1 >= sizeof(token))
				return FALSE;
			if (!(*character >= '0' && *character <= '9') && *character != '.' && *character != '-' &&
				*character != '+')
			{
				number = FALSE;
			}
			token[length++] = (char)(*character >= 'A' && *character <= 'Z' ? *character - 'A' + 'a' : *character);
			character++;
		}
		token[length] = 0;
		if (number)
			continue;
		for (index = 0; index < (short)NUMBEROF(allowed); index++)
		{
			if (!csstrcmp(token, allowed[index]))
				break;
		}
		if (index >= (short)NUMBEROF(allowed))
			return FALSE;
	}
	return TRUE;
}

static boolean hs_compile_and_evaluate_command(
	char const *expression);

/* port: a typed command (console, telnet, cheat button, init.txt): what it logs is its answer,
shown whatever game.console_log is (terminal_command_running) */
boolean hs_compile_and_evaluate(
	char const *expression)
{
	boolean was_running = terminal_command_running;
	boolean result;

	terminal_command_running = TRUE;
	result = hs_compile_and_evaluate_command(expression);
	terminal_command_running = was_running;
	return result;
}

static boolean hs_compile_and_evaluate_command(
	char const *expression)
{
	boolean success = FALSE;
	char const *error_message;
	char const *error_source;
	char *character;
	char buffer[1024];
	char expanded[1024];

	/* port: in another's game the host decides: no cheats, game speed or other change of the game
	(each tick puts back what changed before joining, cheats_network_client_enforce) */
	if (hs_playing_in_anothers_game() && !hs_expression_changes_no_game(expression))
	{
		console_warning("not while playing in another's game: the host decides the game");
		return FALSE;
	}
	/* port: the host's ban command ("ban <player name>", or its start: Tab
	completes it), which is no script's */
	{
		char const *text = expression;

		while (*text == ' ' || *text == '\t' || *text == '(')
			text++;
		if ((text[0] == 'b' || text[0] == 'B') && (text[1] == 'a' || text[1] == 'A') &&
			(text[2] == 'n' || text[2] == 'N') && (text[3] == ' ' || text[3] == '\t' || text[3] == 0))
		{
			char name[64];
			long length = 0;

			text += 3;
			while (*text == ' ' || *text == '\t' || *text == '"')
				text++;
			while (*text && *text != '"' && *text != ')' && length < (long)sizeof(name) - 1)
				name[length++] = *text++;
			while (length > 0 && (name[length - 1] == ' ' || name[length - 1] == '\t'))
				length--;
			name[length] = 0;
			return network_game_server_ban_player(name);
		}
	}
	csstrncpy(buffer, expression, sizeof(buffer));
	buffer[sizeof(buffer)-1] = 0;
	if (strchr(buffer, ';'))
		buffer[0] = 0;
	character = buffer;
	if (buffer[0] != 0)
	{
		do
		{
			if (!isspace(*character))
			{
				short type;
				long expression_index;

				type = 0;
				hs_compile_initialize(FALSE);
				if (buffer[0] != '(')
				{
					char *space;

					space = strchr(buffer, ' ');
					if (space)
						*space = 0;
					if (hs_find_global_by_name(buffer) != NONE)
					{
						if (space)
							type = 2;
					}
					else
					{
						type = 1;
					}
					if (space)
						*space = ' ';
				}
				switch (type)
				{
				case 0:
					break;
				case 1:
					snprintf(expanded, sizeof(expanded), "(%s)", buffer);
					expression = expanded;
					break;
				case 2:
					snprintf(expanded, sizeof(expanded), "(set %s)", buffer);
					expression = expanded;
					break;
				default:
					display_assert(NULL, "c:\\halo\\SOURCE\\hs\\hs.c", 1287, TRUE);
					system_exit(-1);
					break;
				}
				/* BUG (original): January and HCEA pass the message/source outputs
				 * in reverse. Keep the authentic target behavior explicit. */
				expression_index = hs_compile_expression(csstrlen(expression), expression, &error_source, &error_message);
				if (expression_index != NONE)
				{
					success = TRUE;
					hs_runtime_evaluate(expression_index);
				}
				else if (error_source)
				{
					hs_compile_source_error(
						error_source,
						(char *)error_message,
						NULL,
						expression);
				}
				hs_compile_dispose();
				break;
			}
			character++;
		} while (*character != 0);
	}
	if (hs_recompile_pending)
	{
		if (hs_rebuild_source())
		{
			struct scenario *scenario;

			hs_compile_source();
			if (hs_syntax_data)
			{
				hs_node_gc();
				if (hs_syntax_data_allocated)
				{
					data_make_invalid(hs_syntax_data);
					data_dispose(hs_syntax_data);
					hs_syntax_data_allocated = FALSE;
				}
				hs_syntax_data = NULL;
			}
			hs_runtime_dispose_from_old_map();
			object_lists_dispose_from_old_map();
			scenario = global_scenario_index != NONE ? global_scenario_get() : NULL;
			hs_allocate();
			if (scenario && scenario->hs_syntax_data.size)
				hs_scenario_postprocess(FALSE);
			object_lists_initialize_for_new_map();
			hs_runtime_initialize_for_new_map();
		}
		hs_recompile_pending = FALSE;
	}

	return success;
}

/* ---------- private code */
