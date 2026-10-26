/* MENU_TAGS.C: the XML menus (halo_menus.h) as tags after ui.map's in a copy of its tag table, which cannot grow (its
tags' names follow it), from scenario_tags_load to scenario_tags_unload (campaign maps too); on a problem the game keeps its own menus */

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "game/game.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"

#include "halo_menus.h"
#include "custom_edition_cache.h"

#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
char const *config_string(char const *name);
int platform_display_resolutions(long *widths, long *heights, int maximum);
int platform_window_sizes(long *widths, long *heights, int maximum);
/* (sdl_platform.c's PLATFORM_AUDIO_DEVICE_NAME_SIZE) */
int platform_audio_devices(int recording, char (*names)[128], int maximum);

/* the game's network server (this machine hosts: network_game_globals.c) */
void *global_network_game_server_get(void);
boolean network_game_is_active(void);

/* cache_files.c's (port) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);

/* the game's (port) */
char *tag_get_name(long tag_index);
char const *ui_widget_event_handler_function_name(long function_index);

void menu_tags_loaded(char const *map_name);
void menu_tags_unloaded(void);
char const *pc_menus_root_name(void);
char const *pc_menu_function_name(long function_index);
char const *pc_menu_game_data_input_name(long function_index);
boolean pc_menu_tag(long tag_index);
#ifdef HALO_PS3
boolean pc_menu_xbox_patch_tag(long tag_index);
short game_modes_gametype_count(void);
#endif
boolean pc_menu_frame_placement(struct bitmap_data const *bitmap, short *x, short *y, short *width, short *height);

/* ---------- constants */

#define PC_MENU_TAG_PREFIX "pc\\"
/* the multiplayer maps' widget collection, whose pause screens pause_patch
adds to */
#define MULTIPLAYER_COLLECTION "ui\\shell\\multiplayer"
/* the campaign's full screen pause screen, which pause_campaign_patch adds to */
#define CAMPAIGN_PAUSE_SCREEN "ui\\shell\\solo_game\\pause_game\\pause_game"
/* the tags pause_patch or pause_campaign_patch adds: two buttons and their
texts (SETTINGS, and END GAME or FRIENDLY FIRE) */
#define PAUSE_PATCH_TAGS 4
/* what a frame of ui.map's is drawn as in a game map, which has not got it */
#define BLANK_PNG "ce/port/blank__0.png"
#define UI_WIDGET_DEFINITION_TAG 'DeLa'
#define UNICODE_STRING_LIST_TAG 'ustr'
#define FONT_TAG 'font'
#define SOUND_TAG 'snd!'
#define MAXIMUM_STRINGS 64
#define MAXIMUM_TEXT 2048
/* the size of a menu bitmap's D3D texture, which only stands for its PNG */
#define PLACEHOLDER_SIZE 4

enum
{
	_bitmap_type_2d = 0,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_has_power_of_two_dimensions_bit = 0,
};

enum
{
	_event_handler_close_current_widget_bit = 0,
	_event_handler_close_other_widget_bit,
	_event_handler_close_all_widgets_bit,
	_event_handler_open_widget_bit,
	_event_handler_reload_self_bit,
	_event_handler_reload_other_widget_bit,
	_event_handler_give_focus_to_widget_bit,
	_event_handler_run_function_bit,
	_event_handler_replace_self_with_widget_bit,
	_event_handler_go_back_to_previous_widget_bit,
	_event_handler_run_scenario_script_bit,
	_event_handler_try_to_branch_on_failure_bit,
};

enum
{
	_widget_type_container,
	_widget_type_text_box,
	_widget_type_spinner_list,
	_widget_type_column_list,
};

enum
{
	_list_items_generated_from_string_list_tag_bit = 1,
};

enum
{
	_child_widget_use_custom_controller_index_bit = 0,
};

/* ---------- structures */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *HALO_PTR32 name;
	void *HALO_PTR32 base_address;
	unsigned long unused[2];
};

typedef char verify_cache_file_tag_instance_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

/* the widget definition, as ui_widget.c has it */
struct ui_widget_event_handler_reference
{
	long flags;
	short event_type;
	short function;
	struct tag_reference widget_tag;
	struct tag_reference sound_effect;
	char script[32];
};

struct ui_widget_child_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	short vertical_offset;
	short horizontal_offset;
	byte unknown03A[0x50 - 0x3A];
};

struct ui_widget_conditional_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	byte unknown036[0x50 - 0x36];
};

struct ui_widget_search_and_replace_reference
{
	char search_string[32];
	short replace_function;
};

struct ui_widget_game_data_input_reference
{
	short function;
	byte unknown002[0x24 - 0x02];
};

struct ui_widget_definition
{
	short type;
	short controller_index;
	char name[32];
	rectangle2d bounds;
	long flags;
	long milliseconds_to_auto_close;
	long auto_close_fade_time;
	struct tag_reference background_bitmap;
	struct tag_block game_data_inputs;
	struct tag_block event_handlers;
	struct tag_block search_and_replace_functions;
	byte unknown06C[0xEC - 0x6C];
	struct tag_reference text_label_string_list;
	struct tag_reference text_font;
	real_argb_color text_color;
	short justification;
	word text_box_flags;
	byte unknown120[0x12E - 0x120];
	short string_list_index;
	short horizontal_offset;
	short vertical_offset;
	byte unknown134[0x150 - 0x134];
	long list_flags;
	struct tag_reference list_header_bitmap;
	struct tag_reference list_footer_bitmap;
	rectangle2d list_header_bounds;
	rectangle2d list_footer_bounds;
	byte unknown184[0x1A4 - 0x184];
	struct tag_reference extended_description_widget;
	byte unknown1B4[0x2D4 - 0x1B4];
	struct tag_block conditional_widgets;
	byte unknown2E0[0x3E0 - 0x2E0];
	struct tag_block child_widgets;
};

typedef char verify_ui_widget_event_handler_reference_size[
	sizeof(struct ui_widget_event_handler_reference) == 0x48 ? 1 : -1];
typedef char verify_ui_widget_child_reference_size[
	sizeof(struct ui_widget_child_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_conditional_reference_size[
	sizeof(struct ui_widget_conditional_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_search_and_replace_reference_size[
	sizeof(struct ui_widget_search_and_replace_reference) == 0x22 ? 1 : -1];
typedef char verify_ui_widget_game_data_input_reference_size[
	sizeof(struct ui_widget_game_data_input_reference) == 0x24 ? 1 : -1];
typedef char verify_ui_widget_definition_text_color_offset[
	offsetof(struct ui_widget_definition, text_color) == 0x10C ? 1 : -1];
typedef char verify_ui_widget_definition_list_flags_offset[
	offsetof(struct ui_widget_definition, list_flags) == 0x150 ? 1 : -1];
typedef char verify_ui_widget_definition_conditional_widgets_offset[
	offsetof(struct ui_widget_definition, conditional_widgets) == 0x2D4 ? 1 : -1];
typedef char verify_ui_widget_definition_size[
	sizeof(struct ui_widget_definition) == 0x3EC ? 1 : -1];

/* a frame of ui.map's that a menu file scales: where in its widget, and at
what size, it is drawn (pc_menu_frame_placement) */
struct menu_frame_placement
{
	struct bitmap_data const *bitmap;
	short x, y;
	short width, height;
};

/* a spinner bound to a setting (menu_functions.c) */
struct pc_menu_setting
{
	long definition_index;
	char const *setting;
	long value_count;
	char const *values[MAXIMUM_STRINGS];
	short loaded_index;
};

/* ---------- globals */

/* the names the files may use, by their values */
static char const *const event_names[] =
{
	"a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
	"up", "down", "left", "right", "start", "back", "left_thumb", "right_thumb",
	"stick_up", "stick_down", "stick_left", "stick_right",
	"right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
	"created", "deleted",
	/* (the PC version's, which this engine never sends) */
	"get_focus", "lose_focus", "left_mouse", "middle_mouse", "right_mouse", "double_click", "custom_activation",
	"post_render",
};

static char const *const widget_flag_names[] =
{
	"pass_unhandled_to_focused_child", "pause_game", "flash_bitmap", "up_down_tabs_children",
	"left_right_tabs_children", "up_down_tabs_items", "left_right_tabs_items", "no_focused_child",
	"pass_unhandled_to_all_children", "render_any_controller", "pass_handled_to_all_children",
	"main_menu_if_no_history", "tag_controller_index", "nifty_fx", "no_history",
	/* (the PC version's, which this engine has no use for) */
	"force_handle_mouse", "no_widescreen_fill",
};

static char const *const widget_type_names[] = { "container", "text", "spinner", "column_list" };
static char const *const list_flag_names[] = { "items_in_code", "items_from_strings", "one_tooltip", "single_preview" };
static char const *const text_flag_names[] = { "editable", "password", "flashing", "no_focus_test" };
static char const *const align_names[] = { "left", "right", "center" };
/* (build_number and pid are the PC version's, which this engine has not) */
static char const *const replace_names[] = { "none", "controller", "build_number", "pid" };

/* the game data functions, as the tags name them
(ui_widget_game_data_input_functions.c) */
static char const *const game_data_input_names[] =
{
	"NULL", "player settings menu update desc", "unused", "playlist settings menu update desc",
	"gametype select menu update desc", "multiplayer type menu update desc", "solo level select update",
	"difficulty menu update desc", "build number textbox only", "server list update",
	"network pregame status update", "splitscreen pregame status update", "net splitscreen prejoin players",
	"mp profile list update", "3wide player profile list update", "plyr prof edit select menu upd8",
	"player profile small menu update", "game settings lists text update", "solo game objective text",
	"color picker update", "game settings lists pic update", "main menu fake animate",
	"mp level select update", "get active plyr profile name", "get edit plyr profile name",
	"get edit game settings name", "get active plyr profile color", "mp set textbox map name",
	"mp set textbox game ruleset", "mp set textbox teams noteams", "mp set textbox score limit",
	"mp set textbox score limit type", "mp set bitmap for map", "mp set bitmap for ruleset",
	"mp set textbox", "mp edit profile set rule text", "system link status check", "mp game directions",
	"teams no teams bitmap update", "warn if diff will nuke saved game", "dim if no net cable",
};

/* the port's event handler functions (menu_functions.c), from PC_MENU_FUNCTION_BASE: its own, the PC version's that the
Xbox's have not, then its own of the Xbox's names, run instead of the Xbox's (those take the Xbox's widgets) */
static char const *const port_function_names[] =
{
	"port quit game", "port setting load", "port setting save", "unwired",
	"exit gracefully to xbox dashboard", "pause game invert pitch", "start new coop game",
	"pause game invert spinner get", "pause game invert spinner set", "main menu quit game",
	"mouse emit accept event", "mouse emit back event", "mouse emit dpad left event",
	"mouse emit dpad right event", "mouse spinner 3wide click", "controls screen init", "video screen init",
	"controls begin binding", "gamespy screen init", "gamespy screen dispose", "gamespy select header",
	"gamespy select item", "gamespy select button", "plr prof init mouse set", "plr prof change mouse set",
	"plr prof init audio set", "plr prof change audio set", "plr prof change video set",
	"controls screen dispose", "controls screen change set", "mouse emit x event", "gamepad screen init",
	"gamepad screen dispose", "gamepad screen change gamepads", "gamepad screen select item",
	"mouse screen defaults", "audio screen defaults", "video screen defaults", "controls screen defaults",
	"profile set edit begin", "profile manager delete", "profile manager select", "gamespy dismiss error",
	"server settings init", "ss edit server name", "ss edit server password", "ss start game",
	"video test dialog init", "video test dialog dispose", "video test dialog accept",
	"gamespy dismiss filters", "gamespy update filter settings", "gamespy back handler",
	"mouse spinner 1wide click", "controls back handler", "controls advanced launch", "controls advanced ok",
	"mp pause menu open", "mp game options open", "mp choose team", "mp prof init vehicle options",
	"mp prof save vehicle options", "single prev cl item activated", "mp prof init teamplay options",
	"mp prof save teamplay options", "mp game options choose", "emit custom activation event",
	"plr prof cancel audio set", "plr prof init network options", "plr prof save network options",
	"difficulty item select",
	"gamespy get patch", "video screen dispose", "campaign menu init", "campaign menu continue",
	"load game menu init", "load game menu dispose", "load game menu activated", "solo menu save checkpoint",
	"mp type set mode", "direct ip connect init",
	"direct ip connect go", "direct ip edit field", "network settings edit a port", "network settings defaults",
	"load game menu delete request", "load game menu delete finish",
	"initialize sp level list solo", "dispose sp level list", "solo level set map", "set difficulty",
	"port settings save", "port settings defaults", "controls binding slot",
	"color picker menu initialize", "color picker menu dispose", "color picker select color",
	"player profile list initialize", "player profile list dispose", "request del player profile",
	"final del player profile",
	"ss copy invite", "join controller to mp game", "mp level list initialize", "mp level list dispose",
	"mp level select", "mp profiles list initialize", "mp profiles list dispose", "mp profile set for game",
	"port lobby preview join",
	"port setup edit",
	"port setup options init", "port setup options save",
	"port map list back",
	"port pause end game", "port coop friendly fire",
	"port coop begin", "port coop player 2 list initialize", "port coop player 2",
	"port lobby open", "port lobby add player", "port lobby join", "port lobby leave",
	"port lobby player list initialize", "port lobby player choose",
	"port lobby preview add", "port lobby preview leave",
	/* (the gametype editor's: the Xbox's walk their rows by place, which the
	PC version's screens changed) */
	"mp profile begin editing", "mp profile save changes", "request del playlist profile", "final del playlist profile",
	"mp profile init game engine", "mp profile set game engine", "mp profile init ctf rules",
	"mp profile init koth rules", "mp profile init slayer rules", "mp profile init oddball rules",
	"mp profile init racing rules", "mp profile init player opts", "mp profile init item options",
	"mp profile init indicator opts", "mp profile set ctf rules", "mp profile set koth rules",
	"mp profile set slayer rules", "mp profile set oddball rules", "mp profile set racing rules",
	"mp profile set player options", "mp profile set item options", "mp profile set indicator opts",
	/* (the gametype editor's OPENCE OPTIONS: OpenCE's gametypes, game_modes.c) */
	"mp profile init opence options", "mp profile set opence options",
	/* (Settings' OK: the Xbox's fails when the profile has no changes, the
	settings' screens having written theirs to config.toml) */
	"player profile save changes",
	/* (the server browser's password screen) */
	"port password init", "port password edit", "port password join", "port password back",
	/* (Gamepads' OK in a single-player campaign: pause_settings_patch) */
	"port profile settings save",
	/* (a new profile's screens: Settings' is then being edited, which the
	Xbox's leaves to its own screens) */
	"new campaign decision",
	/* (the main menu's Mods: menu_functions.c) */
	"port mods init", "port mods close", "port mods toggle", "port mods move up", "port mods move down",
	"port mods save",
#ifdef HALO_PS3
	/* (the Xbox menus' LAN PLAY and INTERNET PLAY: xbox_multiplayer_patch) */
	"xbox internet begin", "xbox internet end", "xbox internet select", "xbox internet code",
	"xbox lan begin", "xbox lan end", "xbox lan select", "xbox lan create", "xbox lan create if none",
	/* (HOST ONLINE: xbox_host_wire) */
	"xbox host coop", "xbox host multiplayer", "xbox host coop level", "xbox host coop start", "xbox host online",
	"xbox host slots",
#endif
};

/* the PC version's game data functions that the Xbox's have not, from
PC_MENU_FUNCTION_BASE (menu_functions.c) */
static char const *const port_game_data_input_names[] =
{
	"unwired",
	"pause game set textbox inverted", "dim unless two controllers", "controls update menu",
	"video menu update", "gamespy screen update", "common button bar update", "gamepad update menu",
	"server settings update", "audio menu update", "mp prof vehicles update", "solo map list update",
	"mp map list update", "gt select list update", "gt edit list update", "load game list update",
	"direct ip connect update", "network settings update",
	"port settings help", "color picker update", "3wide player profile list update", "port lobby update",
	"port lobby preview update",
	/* (the gametype editor's: the Xbox's stops the game on the buttons'
	row; the Xbox's read only player_ui's gametype, not Server Setup's) */
	"game settings lists text update", "get edit game settings name", "mp edit profile set rule text",
	"port password update",
	/* (the profile settings' picture: the Xbox's of the button settings,
	on Gamepad Setup's row) */
	"port gamepad layout preview",
	/* (the main menu's Mods) */
	"port mods update",
	/* (network co-op's pause menu's FRIENDLY FIRE: its text, ON or OFF) */
	"port coop friendly fire text",
	/* (the gametype editor's description, with OPENCE OPTIONS' row) */
	"port playlist edit desc",
#ifdef HALO_PS3
	"xbox internet update", "xbox mp type desc", "xbox lan update", "xbox invite code", "xbox host desc",
	"xbox extra options update", "xbox host header", "xbox slots key",
#endif
};

static struct
{
	/* what is let go when the map unloads */
	void **blocks;
	long block_count;
	struct bitmap_data **bitmaps;
	long bitmap_count;
	struct menu_frame_placement *placements;
	long placement_count;
	struct cache_file_tag_instance *original_instances;
	long original_count;
	struct pc_menu_setting *settings;
	long setting_count;
	boolean loaded;
#ifdef HALO_PS3
	/* the Xbox's menus have LAN PLAY and INTERNET PLAY (xbox_multiplayer_patch); the pregame screen's tag, whose Y the
	port's function has */
	boolean xbox_patched;
	long xbox_pregame;
#endif
	char root[300];
} menu_tags;

/* the build under way */
static struct
{
	struct halo_menus const *menus;
	long first_index;
	long first_salt;
	long next;
	/* each widget's, bitmap's and string list's tag index, and each
	widget's own text's and strings' (NONE: none) */
	long *widget_tags;
	long *bitmap_tags;
	long *strings_tags;
	long *text_tags;
	long *spinner_tags;
	boolean failed;
	/* a game map's build (the pause menu's SETTINGS): what the map has not
	got is none, not a failure */
	boolean game_map;
	/* a single-player campaign's map's, whose screens pause the game */
	boolean campaign;
	/* the strings taken from the Custom Edition's loc.map */
	long map_strings;
	/* a network co-op game's map's: its pause menu gets FRIENDLY FIRE too */
	boolean coop;
} build;

/* ---------- private code */

/* (the game's allocator, which malloc and free are here: cseries.h) */
static void *allocate(long size)
{
	void *block = malloc(size > 0 ? size : 1);

	if (!block)
	{
		build.failed = TRUE;
		return NULL;
	}
	memset(block, 0, size > 0 ? size : 1);
	{
		void **blocks = realloc(menu_tags.blocks, (menu_tags.block_count + 1) * sizeof(*menu_tags.blocks));

		if (!blocks)
		{
			free(block);
			build.failed = TRUE;
			return NULL;
		}
		menu_tags.blocks = blocks;
	}
	menu_tags.blocks[menu_tags.block_count++] = block;
	return block;
}

static void problem(char const *file, long line, char const *message, char const *detail)
{
	if (!build.failed)
		halo_menus_log(file, line, message, detail);
	build.failed = TRUE;
}

static long name_index(char const *name, char const *const *names, long count)
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name, names[index]))
			return index;
	}
	return NONE;
}

/* the bits of the space-separated names */
static long flags_parse(char const *text, char const *const *names, long count, char const *file, long line)
{
	long flags = 0;

	while (text && *text)
	{
		char word[64];
		long length, bit;

		text += strspn(text, " \t\r\n");
		length = (long)strcspn(text, " \t\r\n");
		if (!length)
			break;
		if (length >= (long)sizeof(word))
			length = sizeof(word) - 1;
		memcpy(word, text, length);
		word[length] = 0;
		text += strcspn(text, " \t\r\n");
		bit = name_index(word, names, count);
		if (bit == NONE)
			problem(file, line, "there is no flag", word);
		else
			flags |= 1L << bit;
	}
	return flags;
}

static void reference_clear(struct tag_reference *reference, long group_tag)
{
	reference->group_tag = group_tag;
	reference->name = "";
	reference->name_length = 0;
	reference->index = NONE;
}

static void reference_set(struct tag_reference *reference, long group_tag, long tag_index)
{
	if (tag_index == NONE)
	{
		reference_clear(reference, group_tag);
		return;
	}
	reference->group_tag = group_tag;
	reference->name = (char *)tag_get_name(tag_index);
	reference->name_length = (long)strlen(reference->name);
	reference->index = tag_index;
}

static long find(char const *name, long count, char const *(*name_of)(long))
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name_of(index), name))
			return index;
	}
	return NONE;
}

static char const *widget_name(long index) { return build.menus->widgets[index].name; }
static char const *bitmap_name(long index) { return build.menus->bitmaps[index].name; }
static char const *strings_name(long index) { return build.menus->string_lists[index].name; }

static long widget_named(char const *name)
{
	return find(name, build.menus->widget_count, widget_name);
}

static long map_tag(long group_tag, char const *name, char const *file, long line)
{
	long index = tag_loaded(group_tag, name);

	/* (a game map has few of ui.map's: those are none) */
	if (index == NONE && !build.game_map)
		problem(file, line, "the map has no tag", name);
	return index;
}

/* a tag: the map's by its path (with a backslash), else one of ours */
static long tag_named(long group_tag, char const *name, char const *file, long line)
{
	long index = NONE;

	if (strchr(name, '\\'))
		return map_tag(group_tag, name, file, line);
	switch (group_tag)
	{
	case UI_WIDGET_DEFINITION_TAG:
		index = widget_named(name);
		index = index == NONE ? NONE : build.widget_tags[index];
		break;
	case BITMAP_GROUP_TAG:
		index = find(name, build.menus->bitmap_count, bitmap_name);
		index = index == NONE ? NONE : build.bitmap_tags[index];
		break;
	case UNICODE_STRING_LIST_TAG:
		index = find(name, build.menus->string_list_count, strings_name);
		index = index == NONE ? NONE : build.strings_tags[index];
		break;
	}
	if (index == NONE)
		problem(file, line, "there is nothing named", name);
	return index;
}

/* "unwired <name>": a function the PC version has for these widgets, not yet written for this engine
(port/assets/menus/UNWIRED.md), which does nothing */
static boolean unwired(char const *name)
{
	return !strncmp(name, "unwired ", 8);
}

static long function_index(char const *name, char const *file, long line)
{
	char const *function_name;
	long index;

	if (unwired(name))
		name = "unwired";
	index = name_index(name, port_function_names, NUMBEROF(port_function_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	for (index = 0; (function_name = ui_widget_event_handler_function_name(index)) != NULL; index++)
	{
		if (!strcmp(name, function_name))
			return index;
	}
	problem(file, line, "there is no event handler function", name);
	return NONE;
}

static long game_data_input_index(char const *name, char const *file, long line)
{
	long index;

	if (unwired(name))
		name = "unwired";
	/* (ours first, as for the event handlers) */
	index = name_index(name, port_game_data_input_names, NUMBEROF(port_game_data_input_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	index = name_index(name, game_data_input_names, NUMBEROF(game_data_input_names));
	if (index != NONE)
		return index;
	problem(file, line, "there is no game data input", name);
	return NONE;
}

static long hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return NONE;
}

/* "#RRGGBB" or "#AARRGGBB" */
static boolean parse_color(char const *text, real_argb_color *color)
{
	long length = text ? (long)strlen(text) : 0;
	long components[4] = { 255, 0, 0, 0 };
	long index;

	if ((length != 7 && length != 9) || text[0] != '#')
		return FALSE;
	for (index = 0; index < (length - 1) / 2; index++)
	{
		long high = hex_digit(text[1 + 2 * index]), low = hex_digit(text[2 + 2 * index]);

		if (high == NONE || low == NONE)
			return FALSE;
		components[length == 7 ? index + 1 : index] = high * 16 + low;
	}
	color->alpha = components[0] / 255.0f;
	color->red = components[1] / 255.0f;
	color->green = components[2] / 255.0f;
	color->blue = components[3] / 255.0f;
	return TRUE;
}

/* "top left bottom right" */
static void parse_bounds(char const *text, rectangle2d *bounds, char const *file, long line)
{
	long top, left, bottom, right;
	char extra;

#ifdef HALO_PS3
	/* the game's long is an int here; newlib's %ld would store 8 bytes */
	if (text && sscanf(text, "%d %d %d %d %c", &top, &left, &bottom, &right, &extra) == 4)
#else
	if (text && sscanf(text, "%ld %ld %ld %ld %c", &top, &left, &bottom, &right, &extra) == 4)
#endif
	{
		bounds->y0 = (short)top;
		bounds->x0 = (short)left;
		bounds->y1 = (short)bottom;
		bounds->x1 = (short)right;
	}
	else if (text)
	{
		problem(file, line, "bounds are \"top left bottom right\":", text);
	}
}

/* a 'ustr' of the strings; each with an index in the list `map` (the
Custom Edition's in its language, or NULL) given by maps is its string
there */
static void *string_list_build(char const *const *strings, long const *maps, long count,
	struct custom_edition_string_list const *map)
{
	struct string_list *list = allocate(sizeof(struct string_list));
	struct string_list_entry *entries = allocate(count * sizeof(struct string_list_entry));
	long index;

	if (!list || !entries)
		return list;
	list->strings.count = count;
	list->strings.address = entries;
	for (index = 0; index < count; index++)
	{
		long length = (long)strlen(strings[index]);
		unsigned short *characters;
		long written;

		if (map && maps[index] != HALO_MENU_NONE && maps[index] < map->count)
		{
			length = map->lengths[maps[index]];
			characters = allocate(length * sizeof(unsigned short));
			if (!characters)
				return list;
			memcpy(characters, map->strings[maps[index]], length * sizeof(unsigned short));
			entries[index].string.size = length * (long)sizeof(unsigned short);
			entries[index].string.address = characters;
			build.map_strings++;
			continue;
		}
		characters = allocate((length * 2 + 2) * sizeof(unsigned short));
		if (!characters)
			return list;
		written = halo_menus_utf16(strings[index], characters, length * 2 + 2);
		entries[index].string.size = written * (long)sizeof(unsigned short);
		entries[index].string.address = characters;
	}
	return list;
}

/* the "|"-separated pieces of text, copied (at most MAXIMUM_STRINGS) */
static long split(char const *text, char const **pieces)
{
	long count = 0;

	while (text && count < MAXIMUM_STRINGS)
	{
		long length = (long)strcspn(text, "|");
		char *piece = allocate(length + 1);

		if (!piece)
			break;
		memcpy(piece, text, length);
		pieces[count++] = piece;
		if (!text[length])
			break;
		text += length + 1;
	}
	return count;
}

/* a spinner's strings or values, split; Video Setup's add the display's resolutions after NATIVE (shown "1920 x 1080",
set "1920x1080"), Window Size's those that fit; Audio Setup's add SDL's devices after SYSTEM DEFAULT (in capitals) */
static long spinner_split(struct halo_menu_widget const *widget, boolean values, char const **pieces)
{
	long count = split(values ? widget->values : widget->strings, pieces);
	long widths[MAXIMUM_STRINGS], heights[MAXIMUM_STRINGS];
	long added, index;

	if (widget->setting && (!strcmp(widget->setting, "audio.output_device") ||
		!strcmp(widget->setting, "audio.input_device")))
	{
		static char names[MAXIMUM_STRINGS][128];

		added = platform_audio_devices(!strcmp(widget->setting, "audio.input_device"), names,
			(int)(MAXIMUM_STRINGS - count));
		for (index = 0; index < added; index++)
		{
			char shown[32];
			char const *name = names[index];
			char *piece;
			long length;

			if (!values)
			{
				long at;

				for (at = 0; name[at] && at < (long)sizeof(shown) - 1; at++)
					shown[at] = name[at] >= 'a' && name[at] <= 'z' ? (char)(name[at] - 'a' + 'A') : name[at];
				shown[at] = 0;
				/* (cut short: what the spinner shows) */
				if (strlen(name) > 18)
					strcpy(shown + 15, "...");
				name = shown;
			}
			length = (long)strlen(name);
			piece = allocate(length + 1);
			if (!piece)
				break;
			strcpy(piece, name);
			pieces[count++] = piece;
		}
		return count;
	}
	if (widget->setting && !strcmp(widget->setting, "display.resolution"))
		added = platform_display_resolutions(widths, heights, (int)(MAXIMUM_STRINGS - count));
	else if (widget->setting && !strcmp(widget->setting, "display.window_size"))
	{
		added = platform_window_sizes(widths, heights, MAXIMUM_STRINGS);
		if (added > 0)
			count = 0;
	}
	else
		return count;
	for (index = 0; index < added; index++)
	{
		char text[32];
		char *piece;

		snprintf(text, sizeof(text), values ? "%ldx%ld" : "%ld x %ld", widths[index], heights[index]);
		piece = allocate((long)strlen(text) + 1);
		if (!piece)
			break;
		strcpy(piece, text);
		pieces[count++] = piece;
	}
	return count;
}

static void *bitmap_build(struct halo_menu_bitmap const *source, long tag_index)
{
	struct bitmap_group *group = allocate(sizeof(struct bitmap_group));
	struct bitmap_group_sequence *sequence = allocate(sizeof(struct bitmap_group_sequence));
	struct bitmap_data *bitmaps = allocate(source->frame_count * sizeof(struct bitmap_data));
	long frame;

	if (build.failed)
		return group;
	if (!source->frame_count)
	{
		problem(source->file, source->line, "a bitmap has no frames:", source->name);
		return group;
	}
	sequence->first_bitmap_index = 0;
	sequence->bitmap_count = (short)source->frame_count;
	group->sequences.count = 1;
	group->sequences.address = sequence;
	group->bitmaps.count = source->frame_count;
	group->bitmaps.address = bitmaps;
	for (frame = 0; frame < source->frame_count; frame++)
	{
		struct halo_menu_frame const *data = &build.menus->frames[source->first_frame + frame];
		struct bitmap_data *bitmap = &bitmaps[frame];
		char const *png = data->png;
		long width = data->width, height = data->height;

		/* (a frame of the map's: a copy of its bitmap, which the texture cache loads from the map as its own;
		in a game map that has not got it, nothing) */
		if (data->map && build.game_map && tag_loaded(BITMAP_GROUP_TAG, data->map) == NONE)
		{
			png = BLANK_PNG;
			width = height = PLACEHOLDER_SIZE;
		}
		else if (data->map)
		{
			long group_index = map_tag(BITMAP_GROUP_TAG, data->map, source->file, source->line);
			struct bitmap_group *group_source;

			if (group_index == NONE)
				return group;
			group_source = bitmap_group_get(group_index);
			if (data->index < 0 || data->index >= group_source->bitmaps.count)
			{
				problem(source->file, source->line, "the map's bitmap has no such frame:", data->map);
				return group;
			}
			memcpy(bitmap, (struct bitmap_data *)group_source->bitmaps.address + data->index, sizeof(*bitmap));
			bitmap->cache_block_index = NONE;
			bitmap->base_address = NULL;
			/* (scaled to a size of the file's: the texture stays the map's,
			drawn smaller or larger, ui_widget.c) */
			if (data->width > 0 && data->height > 0 && data->width <= 2048 && data->height <= 2048)
			{
				struct menu_frame_placement *placements = realloc(menu_tags.placements,
					(menu_tags.placement_count + 1) * sizeof(*menu_tags.placements));

				if (placements)
				{
					menu_tags.placements = placements;
					placements[menu_tags.placement_count].bitmap = bitmap;
					placements[menu_tags.placement_count].x = (short)PIN(data->x, -2048, 2048);
					placements[menu_tags.placement_count].y = (short)PIN(data->y, -2048, 2048);
					placements[menu_tags.placement_count].width = (short)data->width;
					placements[menu_tags.placement_count].height = (short)data->height;
					menu_tags.placement_count++;
				}
			}
			continue;
		}

		if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
		{
			problem(source->file, source->line, "a frame is 1 to 2048 wide and high:", png);
			return group;
		}
		/* (its D3D texture is a small one, which its PNG is drawn for: the
		game draws a bitmap in units of its size, to whatever texture it has) */
		bitmap->signature = BITMAP_GROUP_TAG;
		bitmap->width = PLACEHOLDER_SIZE;
		bitmap->height = PLACEHOLDER_SIZE;
		bitmap->depth = 1;
		bitmap->type = _bitmap_type_2d;
		bitmap->format = _bitmap_format_a8r8g8b8;
		bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit);
		bitmap->tag_index = tag_index;
		bitmap->cache_block_index = NONE;
		if (!rasterizer_bitmap_new(bitmap))
		{
			problem(source->file, source->line, "could not make the texture of bitmap", source->name);
			return group;
		}
		bitmap->width = (short)width;
		bitmap->height = (short)height;
		{
			struct bitmap_data **bitmaps = realloc(menu_tags.bitmaps,
				(menu_tags.bitmap_count + 1) * sizeof(*menu_tags.bitmaps));

			if (!bitmaps)
			{
				rasterizer_bitmap_delete(bitmap);
				problem(source->file, source->line, "out of memory for the bitmaps of", source->name);
				return group;
			}
			menu_tags.bitmaps = bitmaps;
		}
		menu_tags.bitmaps[menu_tags.bitmap_count++] = bitmap;
		/* (none without a renderer: debug.null_renderer) */
		if (bitmap->hardware_format)
			halo_menus_art_register(bitmap->hardware_format, png);
	}
	return group;
}

/* adds a conditional widget to the definition, if it has not got it */
static void conditional_add(struct ui_widget_definition *definition, long tag_index, long flags)
{
	struct ui_widget_conditional_reference *conditionals =
		(struct ui_widget_conditional_reference *)definition->conditional_widgets.address;
	struct ui_widget_conditional_reference *grown;
	long count = definition->conditional_widgets.count, index;

	for (index = 0; index < count; index++)
	{
		if (conditionals[index].widget_tag.index == tag_index)
			return;
	}
	grown = allocate((count + 1) * sizeof(*grown));
	if (!grown)
		return;
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (count)
		memcpy(grown, conditionals, count * sizeof(*grown));
	reference_set(&grown[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
	grown[count].flags = flags;
	definition->conditional_widgets.address = grown;
	definition->conditional_widgets.count = count + 1;
}

static void handler_build(struct ui_widget_event_handler_reference *handler, struct halo_menu_handler const *source,
	long event_type, struct ui_widget_definition *definition)
{
	char const *targets[5];
	long target_count = 0;

	handler->event_type = (short)event_type;
	reference_clear(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG);
	reference_clear(&handler->sound_effect, SOUND_TAG);
	if (source->run)
	{
		handler->function = (short)function_index(source->run, source->file, source->line);
		SET_FLAG(handler->flags, _event_handler_run_function_bit, TRUE);
	}
	if (source->script)
	{
		strncpy(handler->script, source->script, sizeof(handler->script) - 1);
		SET_FLAG(handler->flags, _event_handler_run_scenario_script_bit, TRUE);
	}
	else if (source->label)
	{
		strncpy(handler->script, source->label, sizeof(handler->script) - 1);
	}
	if (source->open)
	{
		targets[target_count++] = source->open;
		SET_FLAG(handler->flags, _event_handler_open_widget_bit, TRUE);
	}
	if (source->replace)
	{
		targets[target_count++] = source->replace;
		SET_FLAG(handler->flags, _event_handler_replace_self_with_widget_bit, TRUE);
	}
	if (source->focus)
	{
		targets[target_count++] = source->focus;
		SET_FLAG(handler->flags, _event_handler_give_focus_to_widget_bit, TRUE);
	}
	if (source->back)
		SET_FLAG(handler->flags, _event_handler_go_back_to_previous_widget_bit, TRUE);
	if (source->branch)
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
	if (source->close)
	{
		if (!strcmp(source->close, "current"))
			SET_FLAG(handler->flags, _event_handler_close_current_widget_bit, TRUE);
		else if (!strcmp(source->close, "all"))
			SET_FLAG(handler->flags, _event_handler_close_all_widgets_bit, TRUE);
		else if (!strcmp(source->close, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_close_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "close is \"current\", \"all\" or \"other\" (with widget=)",
				source->close);
	}
	if (source->reload)
	{
		if (!strcmp(source->reload, "self"))
			SET_FLAG(handler->flags, _event_handler_reload_self_bit, TRUE);
		else if (!strcmp(source->reload, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_reload_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "reload is \"self\" or \"other\" (with widget=)", source->reload);
	}
	if (source->widget)
		targets[target_count++] = source->widget;
	if (target_count > 1)
	{
		long index;

		/* (several flags may share the one widget) */
		for (index = 1; index < target_count; index++)
		{
			if (strcmp(targets[index], targets[0]))
				problem(source->file, source->line, "a handler names one widget (open, replace, focus or widget)",
					NULL);
		}
	}
	if (target_count)
	{
		reference_set(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, targets[0], source->file, source->line));
	}
	if (source->sound)
		reference_set(&handler->sound_effect, SOUND_TAG, map_tag(SOUND_TAG, source->sound, source->file, source->line));
	if (source->otherwise)
	{
		if (!source->run)
			problem(source->file, source->line, "otherwise needs run (the function whose failure it is for)", NULL);
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, source->otherwise, source->file, source->line), 1);
	}
}

static void setting_add(struct halo_menu_widget const *source, long definition_index)
{
	struct pc_menu_setting *setting;
	struct pc_menu_setting *settings = realloc(menu_tags.settings,
		(menu_tags.setting_count + 1) * sizeof(*menu_tags.settings));

	if (!settings)
	{
		problem(source->file, source->line, "out of memory for the settings of", source->name);
		return;
	}
	menu_tags.settings = settings;
	setting = &menu_tags.settings[menu_tags.setting_count++];
	memset(setting, 0, sizeof(*setting));
	setting->definition_index = definition_index;
	setting->setting = source->setting;
	setting->loaded_index = NONE;
	setting->value_count = spinner_split(source, TRUE, setting->values);
}

/* a font: large, small, terminal, or the map's by its path */
static long font_tag(char const *font, char const *file, long line)
{
	char const *path = !strcmp(font, "large") ? "ui\\large_ui" :
		!strcmp(font, "small") ? "ui\\small_ui" :
		!strcmp(font, "terminal") ? "ui\\interstate" : font;

	return map_tag(FONT_TAG, path, file, line);
}

static void *widget_build(long widget_index)
{
	struct halo_menus const *menus = build.menus;
	struct halo_menu_widget const *source = &menus->widgets[widget_index];
	struct ui_widget_definition *definition = allocate(sizeof(struct ui_widget_definition));
	char const *leaf = strrchr(source->name, '/') ? strrchr(source->name, '/') + 1 : source->name;
	long child, handler, input, conditional, replace, count;

	if (!definition)
		return NULL;
	strncpy(definition->name, leaf, sizeof(definition->name) - 1);
	definition->type = _widget_type_container;
	if (source->type)
	{
		long type = name_index(source->type, widget_type_names, NUMBEROF(widget_type_names));

		if (type == NONE)
			problem(source->file, source->line, "there is no widget type", source->type);
		else
			definition->type = (short)type;
	}
	definition->controller_index = 4;
	if (source->controller && strcmp(source->controller, "any"))
	{
		if (strlen(source->controller) != 1 || source->controller[0] < '1' || source->controller[0] > '4')
			problem(source->file, source->line, "controller is 1 to 4, or \"any\":", source->controller);
		else
			definition->controller_index = (short)(source->controller[0] - '1');
	}
	definition->bounds.y0 = (short)source->top;
	definition->bounds.x0 = (short)source->left;
	definition->bounds.y1 = (short)(source->top + (source->has_height ? source->height : 480));
	definition->bounds.x1 = (short)(source->left + (source->has_width ? source->width : 640));
	definition->flags = flags_parse(source->flags, widget_flag_names, NUMBEROF(widget_flag_names),
		source->file, source->line);
	/* (in a network game the clock runs on) */
	if (build.game_map && !build.campaign)
		definition->flags &= ~FLAG(1); /* pause_game */
	definition->milliseconds_to_auto_close = source->auto_close;
	definition->auto_close_fade_time = source->auto_close_fade;
	reference_clear(&definition->background_bitmap, BITMAP_GROUP_TAG);
	if (source->bitmap)
	{
		reference_set(&definition->background_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->bitmap, source->file, source->line));
	}
	/* the game data inputs */
	for (count = 0, input = source->first_input; input != HALO_MENU_NONE; input = menus->inputs[input].next)
		count++;
	if (count)
	{
		struct ui_widget_game_data_input_reference *inputs = allocate(count * sizeof(*inputs));

		definition->game_data_inputs.count = count;
		definition->game_data_inputs.address = inputs;
		for (count = 0, input = source->first_input; inputs && input != HALO_MENU_NONE; input = menus->inputs[input].next)
		{
			struct halo_menu_input const *data = &menus->inputs[input];

			inputs[count++].function = (short)game_data_input_index(data->input, data->file, data->line);
		}
	}
	/* search and replace */
	for (count = 0, replace = source->first_replace; replace != HALO_MENU_NONE; replace = menus->replaces[replace].next)
		count++;
	if (count)
	{
		struct ui_widget_search_and_replace_reference *replaces = allocate(count * sizeof(*replaces));

		definition->search_and_replace_functions.count = count;
		definition->search_and_replace_functions.address = replaces;
		for (count = 0, replace = source->first_replace; replaces && replace != HALO_MENU_NONE;
			replace = menus->replaces[replace].next)
		{
			struct halo_menu_replace const *item = &menus->replaces[replace];
			long function = item->function ? name_index(item->function, replace_names, NUMBEROF(replace_names)) : 0;

			if (function == NONE)
				problem(item->file, item->line, "a replace function is none or controller:", item->function);
			strncpy(replaces[count].search_string, item->search, sizeof(replaces[count].search_string) - 1);
			/* (the PC version's own are none here) */
			replaces[count++].replace_function = (short)(function == 1 ? 1 : 0);
		}
	}
	/* the text */
	reference_clear(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG);
	reference_clear(&definition->text_font, FONT_TAG);
	if ((source->text != NULL) + (source->strings != NULL) + (source->string_list != NULL) > 1)
		problem(source->file, source->line, "a widget has one of text, strings and string_list:", source->name);
	if (build.text_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.text_tags[widget_index]);
	}
	else if (build.spinner_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.spinner_tags[widget_index]);
		definition->list_flags |= FLAG(_list_items_generated_from_string_list_tag_bit);
	}
	else if (source->string_list)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG,
			tag_named(UNICODE_STRING_LIST_TAG, source->string_list, source->file, source->line));
	}
	definition->string_list_index = (short)source->string_index;
	/* (our own text is white in the large font, unless given) */
	if (source->text || source->strings)
	{
		definition->text_color.alpha = definition->text_color.red = 1.0f;
		definition->text_color.green = definition->text_color.blue = 1.0f;
		if (!source->font)
			reference_set(&definition->text_font, FONT_TAG, font_tag("large", source->file, source->line));
	}
	if (source->color && !parse_color(source->color, &definition->text_color))
		problem(source->file, source->line, "a color is #RRGGBB or #AARRGGBB:", source->color);
	if (source->font)
		reference_set(&definition->text_font, FONT_TAG, font_tag(source->font, source->file, source->line));
	if (source->align)
	{
		long align = name_index(source->align, align_names, NUMBEROF(align_names));

		if (align == NONE)
			problem(source->file, source->line, "align is \"left\", \"right\" or \"center\":", source->align);
		else
			definition->justification = (short)align;
	}
	definition->text_box_flags = (word)flags_parse(source->text_flags, text_flag_names, NUMBEROF(text_flag_names),
		source->file, source->line);
	definition->horizontal_offset = (short)source->text_x;
	definition->vertical_offset = (short)source->text_y;
	/* the list */
	definition->list_flags |= flags_parse(source->list_flags, list_flag_names, NUMBEROF(list_flag_names),
		source->file, source->line);
	if (source->strings && (definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE))
		problem(source->file, source->line, "strings are for a spinner with no children:", source->name);
	if (TEST_FLAG(definition->list_flags, _list_items_generated_from_string_list_tag_bit) &&
		(definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE ||
			definition->text_label_string_list.index == NONE))
		problem(source->file, source->line, "items_from_strings is for a spinner with strings and no children:",
			source->name);
	if (source->setting)
	{
		if (!source->strings || !source->values)
			problem(source->file, source->line, "a setting needs strings and values:", source->name);
		else
			setting_add(source, build.widget_tags[widget_index]);
	}
	reference_clear(&definition->list_header_bitmap, BITMAP_GROUP_TAG);
	reference_clear(&definition->list_footer_bitmap, BITMAP_GROUP_TAG);
	if (source->header_bitmap)
	{
		reference_set(&definition->list_header_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->header_bitmap, source->file, source->line));
	}
	if (source->footer_bitmap)
	{
		reference_set(&definition->list_footer_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->footer_bitmap, source->file, source->line));
	}
	parse_bounds(source->header_bounds, &definition->list_header_bounds, source->file, source->line);
	parse_bounds(source->footer_bounds, &definition->list_footer_bounds, source->file, source->line);
	reference_clear(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG);
	if (source->description)
	{
		if (definition->type != _widget_type_column_list)
			problem(source->file, source->line, "a description is for a column list:", source->name);
		reference_set(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, source->description, source->file, source->line));
	}
	/* the children */
	for (count = 0, child = source->first_child; child != HALO_MENU_NONE; child = menus->children[child].next)
		count++;
	if (definition->type == _widget_type_spinner_list && count != 0 && count != 1 && count != 3)
		problem(source->file, source->line, "a spinner has 0, 1 or 3 children:", source->name);
	if (count)
	{
		struct ui_widget_child_reference *children = allocate(count * sizeof(*children));

		definition->child_widgets.count = count;
		definition->child_widgets.address = children;
		for (count = 0, child = source->first_child; children && child != HALO_MENU_NONE; child = menus->children[child].next)
		{
			struct halo_menu_child const *entry = &menus->children[child];
			long tag_index = entry->nested != HALO_MENU_NONE ? build.widget_tags[entry->nested] :
				tag_named(UI_WIDGET_DEFINITION_TAG, entry->widget, entry->file, entry->line);
			char const *name = entry->nested != HALO_MENU_NONE ? menus->widgets[entry->nested].name : entry->widget;
			char const *child_leaf = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;

			reference_set(&children[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
			strncpy(children[count].name, child_leaf, sizeof(children[count].name) - 1);
			children[count].horizontal_offset = (short)entry->x;
			children[count].vertical_offset = (short)entry->y;
			if (entry->controller)
			{
				if (strlen(entry->controller) != 1 || entry->controller[0] < '1' || entry->controller[0] > '4')
					problem(entry->file, entry->line, "a child's controller is 1 to 4:", entry->controller);
				SET_FLAG(children[count].flags, _child_widget_use_custom_controller_index_bit, TRUE);
				children[count].custom_controller_index = (short)(entry->controller[0] - '1');
			}
			count++;
		}
	}
	/* the conditional widgets */
	for (conditional = source->first_conditional; conditional != HALO_MENU_NONE;
		conditional = menus->conditionals[conditional].next)
	{
		struct halo_menu_conditional const *item = &menus->conditionals[conditional];

		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, item->widget, item->file, item->line),
			item->if_failed ? 1 : 0);
	}
	/* the event handlers: one for each of each <on>'s events */
	for (count = 0, handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		count++;
	if (count)
	{
		long total = 0;

		/* (count the events) */
		for (handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		{
			char const *text = menus->handlers[handler].event;

			while (text && *text)
			{
				long length;

				text += strspn(text, " \t\r\n");
				length = (long)strcspn(text, " \t\r\n");
				if (!length)
					break;
				total++;
				text += length;
			}
		}
		if (total)
		{
			struct ui_widget_event_handler_reference *handlers = allocate(total * sizeof(*handlers));

			definition->event_handlers.count = total;
			definition->event_handlers.address = handlers;
			for (count = 0, handler = source->first_handler; handlers && handler != HALO_MENU_NONE;
				handler = menus->handlers[handler].next)
			{
				struct halo_menu_handler const *on = &menus->handlers[handler];
				char const *text = on->event;

				while (text && *text)
				{
					char word[64];
					long length, event;

					text += strspn(text, " \t\r\n");
					length = (long)strcspn(text, " \t\r\n");
					if (!length)
						break;
					if (length >= (long)sizeof(word))
						length = sizeof(word) - 1;
					memcpy(word, text, length);
					word[length] = 0;
					text += strcspn(text, " \t\r\n");
					event = name_index(word, event_names, NUMBEROF(event_names));
					if (event == NONE)
						problem(on->file, on->line, "there is no event", word);
					handler_build(&handlers[count++], on, event, definition);
				}
			}
		}
	}
	return definition;
}

/* the tag table with count more entries, the existing ones as they are */
static struct cache_file_tag_instance *instances_grow(long count, long *first_index, long *first_salt)
{
	long existing, index, salt = 0;
	struct cache_file_tag_instance *instances = cache_files_tag_instances(&existing);
	struct cache_file_tag_instance *grown;

	if (!instances)
		return NULL;
	/* (every tag's absolute index fits a tag index's 16 bits, short of NONE's, and the table's size cannot wrap: the
	map's count was checked as it loaded, cache_files.c, and is again) */
	if (existing < 0 || count < 0 || existing > UNSIGNED_SHORT_MAX - count)
	{
		platform_log("menus: %ld tags and the map's %ld are too many for a tag table", count, existing);
		return NULL;
	}
	grown = allocate((existing + count) * sizeof(*grown));
	if (!grown)
		return NULL;
	memcpy(grown, instances, existing * sizeof(*grown));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (unsigned long)instances[index].tag_index >> 16;

		if (instance_salt > salt)
			salt = instance_salt;
	}
	menu_tags.original_instances = instances;
	menu_tags.original_count = existing;
	*first_index = existing;
	*first_salt = salt + 1;
	return grown;
}

static long next_tag(void)
{
	long tag_index = ((build.first_salt + build.next) << 16) | (build.first_index + build.next);

	build.next++;
	return tag_index;
}

static void instance_set(struct cache_file_tag_instance *instances, long group_tag, long tag_index, char const *name,
	char const *suffix, void *definition)
{
	struct cache_file_tag_instance *instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index)];
	char *copy = allocate((long)strlen(PC_MENU_TAG_PREFIX) + (long)strlen(name) + (long)strlen(suffix) + 1);

	if (copy)
	{
		char *character;

		strcpy(copy, PC_MENU_TAG_PREFIX);
		strcat(copy, name);
		strcat(copy, suffix);
		/* (named as the map's tags are) */
		for (character = copy; *character; character++)
		{
			if (*character == '/')
				*character = '\\';
		}
	}
	instance->group_tag = group_tag;
	instance->parent_group_tags[0] = NONE;
	instance->parent_group_tags[1] = NONE;
	instance->tag_index = tag_index;
	/* (the name is read by tag_loaded: of none, when the copy failed, which
	fails the build) */
	instance->name = copy ? copy : "";
	instance->base_address = definition;
}

static void menu_tags_release(void)
{
	long index;

	for (index = 0; index < menu_tags.bitmap_count; index++)
	{
		if (menu_tags.bitmaps[index]->hardware_format)
			rasterizer_bitmap_delete(menu_tags.bitmaps[index]);
	}
	halo_menus_art_forget();
	if (menu_tags.original_instances)
		cache_files_set_tag_instances(menu_tags.original_instances, menu_tags.original_count);
	/* (the game's free takes no NULL: settings is, with no setting spinners) */
	for (index = 0; index < menu_tags.block_count; index++)
		free(menu_tags.blocks[index]);
	if (menu_tags.blocks)
		free(menu_tags.blocks);
	if (menu_tags.bitmaps)
		free(menu_tags.bitmaps);
	if (menu_tags.placements)
		free(menu_tags.placements);
	if (menu_tags.settings)
		free(menu_tags.settings);
	memset(&menu_tags, 0, sizeof(menu_tags));
}

/* ---------- public code */

/* ---------- the multiplayer pause menu: SETTINGS, the host's END GAME (LEAVE GAME copies before it); the Xbox's box
is the port's taller one (pause/pausebox_*: tools/port_settings.py), or on custom maps what is below moves down */

#define PAUSE_SETTINGS_SCREEN "main_menu/settings_select/player_setup/player_profile_edit/player_profile_edit_screen"
#define PAUSE_BUTTON_SPACING 35
#define PAUSE_BOX_FIRST_BUTTONS 3

static long ui_function_named(char const *name)
{
	long index;

	for (index = 0; index < 512; index++)
	{
		char const *function = ui_widget_event_handler_function_name(index);

		if (!function)
			break;
		if (!strcmp(function, name))
			return index;
	}
	return NONE;
}

static boolean tag_name_ends(long tag_index, char const *end)
{
	char const *name = tag_index == NONE ? NULL : tag_get_name(tag_index);
	size_t length = name ? strlen(name) : 0, end_length = strlen(end);

	return name && length >= end_length && !strcmp(name + length - end_length, end);
}

/* the index of the list's button that runs the function (LEAVE GAME's),
else NONE */
static long pause_quit_button(struct ui_widget_definition const *list, long quit_function)
{
	struct ui_widget_child_reference const *children = list->child_widgets.address;
	long child;

	for (child = 0; child < list->child_widgets.count; child++)
	{
		struct ui_widget_definition const *button;
		struct ui_widget_event_handler_reference const *handlers;
		long handler;

		if (children[child].widget_tag.index == NONE)
			continue;
		button = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		handlers = button->event_handlers.address;
		for (handler = 0; handler < button->event_handlers.count; handler++)
		{
			if (handlers[handler].function == quit_function &&
				TEST_FLAG(handlers[handler].flags, _event_handler_run_function_bit))
			{
				return child;
			}
		}
	}
	return NONE;
}

/* a button like the model (its look), its own text, each of the model's events (its A and Start) running the function
and opening the widget (if not NONE); with other_text, a second text, which the game data input (input) chooses */
static void *pause_button(struct cache_file_tag_instance *instances, struct ui_widget_definition const *model,
	char const *name, char const *text, long function, long widget_tag, char const *other_text, long input)
{
	struct ui_widget_definition *button = allocate(sizeof(*button));
	struct ui_widget_event_handler_reference *handlers =
		allocate(model->event_handlers.count * sizeof(struct ui_widget_event_handler_reference));
	long text_tag = next_tag(), handler;
	char const *texts[2];

	if (!button || !handlers)
		return NULL;
	memcpy(button, model, sizeof(*button));
	texts[0] = text;
	texts[1] = other_text;
	instance_set(instances, UNICODE_STRING_LIST_TAG, text_tag, name, " text",
		string_list_build(texts, NULL, other_text ? 2 : 1, NULL));
	reference_set(&button->text_label_string_list, UNICODE_STRING_LIST_TAG, text_tag);
	button->string_list_index = 0;
	button->game_data_inputs.count = 0;
	button->game_data_inputs.address = NULL;
	if (other_text)
	{
		struct ui_widget_game_data_input_reference *inputs = allocate(sizeof(*inputs));

		if (!inputs)
			return NULL;
		inputs->function = (short)input;
		button->game_data_inputs.count = 1;
		button->game_data_inputs.address = inputs;
	}
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (model->event_handlers.count)
		memcpy(handlers, model->event_handlers.address, model->event_handlers.count * sizeof(*handlers));
	for (handler = 0; handler < model->event_handlers.count; handler++)
	{
		handlers[handler].flags = FLAG(_event_handler_run_function_bit);
		handlers[handler].function = (short)function;
		handlers[handler].script[0] = 0;
		reference_set(&handlers[handler].widget_tag, UI_WIDGET_DEFINITION_TAG, widget_tag);
		if (widget_tag != NONE)
		{
			handlers[handler].flags |= FLAG(_event_handler_open_widget_bit) |
				FLAG(_event_handler_try_to_branch_on_failure_bit);
		}
	}
	button->event_handlers.address = handlers;
	return button;
}

/* the list's buttons: SETTINGS, and the host's END GAME or co-op's FRIENDLY FIRE (its text the machine's choice:
menu_functions.c), before LEAVE GAME or REVERT TO SAVED (quit); returns how many were added, and the growth */
static long pause_list_patch(struct cache_file_tag_instance *instances, struct ui_widget_definition *list, long quit,
	boolean host, short *growth)
{
	struct ui_widget_child_reference *children = list->child_widgets.address;
	long count = list->child_widgets.count, added = host || build.coop ? 2 : 1, child;
	struct ui_widget_child_reference *grown = allocate((count + added) * sizeof(struct ui_widget_child_reference));
	struct ui_widget_definition const *model = tag_get(UI_WIDGET_DEFINITION_TAG, children[quit].widget_tag.index);
	long settings_widget = widget_named(PAUSE_SETTINGS_SCREEN);
	short spacing = count >= 2 ? (short)(children[count - 1].vertical_offset - children[count - 2].vertical_offset) :
		PAUSE_BUTTON_SPACING;
	char const *names[2];
	void *buttons[2];
	long tags[2];

	if (!grown || settings_widget == NONE || spacing <= 0)
		return 0;
	names[0] = "pause/settings_button";
	tags[0] = next_tag();
	buttons[0] = pause_button(instances, model, names[0], "SETTINGS",
		function_index("profile set edit begin", "pause", 0), build.widget_tags[settings_widget], NULL, NONE);
	names[1] = build.coop ? "pause/friendly_fire_button" : "pause/end_game_button";
	tags[1] = added > 1 ? next_tag() : NONE;
	buttons[1] = build.coop ?
		pause_button(instances, model, names[1], "FRIENDLY FIRE ON",
			function_index("port coop friendly fire", "pause", 0), NONE, "FRIENDLY FIRE OFF",
			game_data_input_index("port coop friendly fire text", "pause", 0)) :
		host ? pause_button(instances, model, names[1], "END GAME",
			function_index("port pause end game", "pause", 0), NONE, NULL, NONE) : NULL;
	if (!buttons[0] || (added > 1 && !buttons[1]))
		return 0;
	for (child = 0; child < added; child++)
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, tags[child], names[child], "", buttons[child]);
	if (quit)
		memcpy(grown, children, quit * sizeof(*grown));
	for (child = 0; child < added; child++)
	{
		grown[quit + child] = children[quit];
		reference_set(&grown[quit + child].widget_tag, UI_WIDGET_DEFINITION_TAG, tags[child]);
		grown[quit + child].vertical_offset = (short)(children[quit].vertical_offset + child * spacing);
	}
	for (child = quit; child < count; child++)
	{
		grown[child + added] = children[child];
		grown[child + added].vertical_offset = (short)(children[child].vertical_offset + added * spacing);
	}
	list->child_widgets.address = grown;
	list->child_widgets.count = count + added;
	/* (a list with room for them, Halo PC's of five rows that Custom Edition maps keep two of: the buttons centred in
	it; else it grows, as the Xbox's of two rows does, and draws within its bounds) */
	{
		short room = (short)(list->bounds.y1 - list->bounds.y0);
		short column = (short)((count + added - 1) * spacing + model->bounds.y1 - model->bounds.y0);

		*growth = 0;
		if (column <= room)
		{
			for (child = 0; child < count + added; child++)
				grown[child].vertical_offset = (short)((room - column) / 2 + child * spacing);
		}
		else
		{
			*growth = (short)(added * PAUSE_BUTTON_SPACING);
			list->bounds.y1 = (short)(list->bounds.y1 + added * spacing);
		}
	}
	return added;
}

/* the Xbox's own box: a widget of the three pausebox2 pieces */
static boolean pause_box_stock(struct ui_widget_definition const *box)
{
	struct ui_widget_child_reference const *pieces = box->child_widgets.address;

	return box->child_widgets.count == 3 && tag_name_ends(pieces[0].widget_tag.index, "\\pausebox2_left") &&
		tag_name_ends(pieces[1].widget_tag.index, "\\pausebox2_left_center") &&
		tag_name_ends(pieces[2].widget_tag.index, "\\pausebox2_right");
}

/* the stock box's pieces drawn with the port's taller box: its frame for
the number of buttons */
static void pause_box_redraw(struct ui_widget_definition const *box, long buttons)
{
	static char const *const names[] = { "pause/pausebox_left", "pause/pausebox_center", "pause/pausebox_right" };
	struct ui_widget_child_reference const *pieces = box->child_widgets.address;
	long piece;

	for (piece = 0; piece < 3; piece++)
	{
		struct ui_widget_definition *definition = tag_get(UI_WIDGET_DEFINITION_TAG, pieces[piece].widget_tag.index);
		long bitmap = find(names[piece], build.menus->bitmap_count, bitmap_name);
		struct bitmap_group *group;
		struct bitmap_group_sequence *sequence;

		if (bitmap == NONE)
			continue;
		group = bitmap_group_get(build.bitmap_tags[bitmap]);
		sequence = group->sequences.address;
		/* (one frame: the one for the number of buttons) */
		sequence->first_bitmap_index = (short)PIN(buttons - PAUSE_BOX_FIRST_BUTTONS, 0, group->bitmaps.count - 1);
		sequence->bitmap_count = 1;
		reference_set(&definition->background_bitmap, BITMAP_GROUP_TAG, build.bitmap_tags[bitmap]);
		definition->bounds.y1 = (short)(definition->bounds.y0 + 256);
	}
}

static void pause_patch(struct cache_file_tag_instance *instances)
{
	long collection = tag_loaded('Soul', MULTIPLAYER_COLLECTION);
	long quit_function = ui_function_named("mp game player quit");
	boolean host = global_network_game_server_get() != NULL;
	struct tag_block const *screens;
	long patched_list = NONE, added = 0, buttons = 0, screen;
	short grow = 0;
	boolean box_redrawn = FALSE;

	if (collection == NONE || quit_function == NONE)
		return;
	screens = tag_get('Soul', collection);
	for (screen = 0; screen < screens->count; screen++)
	{
		long screen_tag = ((struct tag_reference const *)screens->address)[screen].index;
		struct ui_widget_definition *definition;
		struct ui_widget_child_reference *children;
		long child, list_child = NONE, box_child = NONE;
		short list_top;

		if (screen_tag == NONE)
			continue;
		definition = tag_get(UI_WIDGET_DEFINITION_TAG, screen_tag);
		children = definition->child_widgets.address;
		for (child = 0; child < definition->child_widgets.count && list_child == NONE; child++)
		{
			struct ui_widget_definition *list;
			long quit;

			if (children[child].widget_tag.index == NONE)
				continue;
			list = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
			if (list->type != _widget_type_column_list)
				continue;
			/* (one list, which the screens share: patched once) */
			if (children[child].widget_tag.index == patched_list)
			{
				list_child = child;
				continue;
			}
			quit = pause_quit_button(list, quit_function);
			if (quit == NONE || patched_list != NONE)
				continue;
			added = pause_list_patch(instances, list, quit, host, &grow);
			if (!added)
				return;
			buttons = list->child_widgets.count;
			patched_list = children[child].widget_tag.index;
			list_child = child;
		}
		if (list_child == NONE)
			continue;
		for (child = 0; child < definition->child_widgets.count; child++)
		{
			if (child != list_child && children[child].widget_tag.index != NONE &&
				pause_box_stock(tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index)))
			{
				box_child = child;
			}
		}
		/* the stock box: taller, centred where it was, the list with it, and what is below it moved down; else only
		what is below the list (as far as the list grew) */
		list_top = children[list_child].vertical_offset;
		for (child = 0; child < definition->child_widgets.count; child++)
		{
			if (box_child != NONE && (child == box_child || child == list_child))
				children[child].vertical_offset -= grow / 2;
			else if (children[child].vertical_offset > list_top)
				children[child].vertical_offset += box_child != NONE ? grow - grow / 2 : grow;
		}
		if (box_child != NONE && !box_redrawn)
		{
			pause_box_redraw(tag_get(UI_WIDGET_DEFINITION_TAG, children[box_child].widget_tag.index), buttons);
			box_redrawn = TRUE;
		}
	}
	if (patched_list != NONE)
		platform_log("menus: the pause menu has SETTINGS%s", host ? " and END GAME" : "");
}

/* ---------- the campaign's pause menu: SETTINGS before REVERT TO SAVED (co-op: FRIENDLY FIRE after it), rows at the PC's
pitch, prompts and box moved down with them; Settings has in-game items, Gamepads' OK saves the profile */

#define PAUSE_SETTINGS_LIST "main_menu/settings_select/player_setup/player_profile_edit/profile_edit_select_list"
#define PAUSE_SETTINGS_GAMEPADS_OK "main_menu/settings_select/player_setup/player_profile_edit/gamepad_setup/button_ok"
#define PAUSE_NETWORK_ITEM "main_menu/settings_select/player_setup/player_profile_edit/network_settings_profile_item"
#define PAUSE_NETWORK_SCREEN \
	"main_menu/settings_select/player_setup/player_profile_edit/network_setup/in_game/in_game_network_settings_screen"

/* Change Name's keyboard and Change Color's pictures are ui.map's, Network
Setup is for others' games (shown in co-op: its pages, pause_network_patch),
and About is nothing to it */
static char const *const pause_settings_hidden_items[] =
{
	"main_menu/settings_select/player_setup/player_profile_edit/name_profile_item",
	PAUSE_NETWORK_ITEM,
	"main_menu/settings_select/player_setup/player_profile_edit/color_profile_item",
	"main_menu/settings_select/player_setup/player_profile_edit/about_item",
};

/* the PC version's pause menu's row pitch (30 apart). The pause box's frames from the third and the objectives' box are the
stock 159 grown by 36 for five rows (SETTINGS), then 66 for six (co-op's FRIENDLY FIRE too: tools/port_settings.py) */
#define PAUSE_CAMPAIGN_ROW_PITCH 30
#define PAUSE_CAMPAIGN_BOX_FRAME 2
static short const pause_campaign_box_heights[] = { 195, 225 };

/* the port box piece for one of the map's pause box pieces, else NULL */
static char const *pause_campaign_box_bitmap(long tag_index)
{
	if (tag_name_ends(tag_index, "\\pausebox2_left_center"))
		return "pause/pausebox_center";
	if (tag_name_ends(tag_index, "\\pausebox2_left"))
		return "pause/pausebox_left";
	if (tag_name_ends(tag_index, "\\pausebox2_right"))
		return "pause/pausebox_right";
	/* The objectives' pieces carry a header, not the button-key footer. */
	if (tag_name_ends(tag_index, "\\pausebox_right_center") || tag_name_ends(tag_index, "\\pausebox_center"))
		return "pause/objectives_center";
	if (tag_name_ends(tag_index, "\\pausebox_left"))
		return "pause/objectives_left";
	if (tag_name_ends(tag_index, "\\pausebox_right"))
		return "pause/objectives_right";
	return NULL;
}

/* the screen offset of the prompts under the list (B and A), else NONE */
static short pause_campaign_prompts(struct ui_widget_definition const *screen)
{
	struct ui_widget_child_reference const *children = screen->child_widgets.address;
	long child;

	for (child = 0; child < screen->child_widgets.count; child++)
	{
		if (tag_name_ends(children[child].widget_tag.index, "\\button_key_sm") ||
			tag_name_ends(children[child].widget_tag.index, "\\button_key"))
		{
			return children[child].vertical_offset;
		}
	}
	return NONE;
}

/* the prompts move down with the last row */
static void pause_campaign_shift_prompts(struct ui_widget_definition *widget, short shift)
{
	struct ui_widget_child_reference *children = widget->child_widgets.address;
	long child;

	for (child = 0; child < widget->child_widgets.count; child++)
	{
		if (children[child].widget_tag.index == NONE)
			continue;
		if (tag_name_ends(children[child].widget_tag.index, "\\button_key_sm") ||
			tag_name_ends(children[child].widget_tag.index, "\\button_key"))
		{
			children[child].vertical_offset = (short)(children[child].vertical_offset + shift);
		}
		else if (!pause_campaign_box_bitmap(children[child].widget_tag.index))
		{
			pause_campaign_shift_prompts(tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index), shift);
		}
	}
}

/* the box (the menu's, and the mission objectives' beside it) grows by the
same amount, using the frame drawn for that height */
static void pause_campaign_grow_box(struct ui_widget_definition *widget, short shift)
{
	struct ui_widget_child_reference *children;
	long child;
	boolean piece_here = FALSE;

	if (!widget || shift <= 0 || widget->child_widgets.count <= 0)
		return;
	children = widget->child_widgets.address;
	for (child = 0; child < widget->child_widgets.count; child++)
	{
		struct ui_widget_definition *child_widget;
		char const *piece_bitmap;

		if (children[child].widget_tag.index == NONE)
			continue;
		child_widget = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		piece_bitmap = pause_campaign_box_bitmap(children[child].widget_tag.index);
		if (!piece_bitmap)
		{
			pause_campaign_grow_box(child_widget, shift);
			continue;
		}
		piece_here = TRUE;
		{
			/* (bitmap_name is the lookup, not piece_bitmap: find calls it) */
			long bitmap = find(piece_bitmap, build.menus->bitmap_count, bitmap_name);
			short height = (short)(child_widget->bounds.y1 - child_widget->bounds.y0);
			short frame = NONE, size;

			for (size = 0; size < (short)NUMBEROF(pause_campaign_box_heights); size++)
			{
				if ((short)(height + shift) == pause_campaign_box_heights[size])
					frame = (short)((strstr(piece_bitmap, "objectives_") ? 0 : PAUSE_CAMPAIGN_BOX_FRAME) + size);
			}
			if (bitmap != NONE && frame != NONE)
			{
				struct bitmap_group *group = bitmap_group_get(build.bitmap_tags[bitmap]);
				struct bitmap_group_sequence *sequence = group ? group->sequences.address : NULL;

				if (sequence && frame < group->bitmaps.count)
				{
					sequence->first_bitmap_index = frame;
					sequence->bitmap_count = 1;
					reference_set(&child_widget->background_bitmap, BITMAP_GROUP_TAG, build.bitmap_tags[bitmap]);
					child_widget->bounds.y1 = (short)(child_widget->bounds.y0 + height + shift);
				}
			}
			else
			{
				child_widget->bounds.y1 = (short)(child_widget->bounds.y1 + shift);
			}
		}
	}
	if (piece_here)
		widget->bounds.y1 = (short)(widget->bounds.y1 + shift);
}

static boolean pause_settings_item_hidden(long tag_index)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(pause_settings_hidden_items); index++)
	{
		long widget = widget_named(pause_settings_hidden_items[index]);

		/* (Network Setup stays in a co-op game, which is a network one or
		has players beside this one: build.campaign is a game alone) */
		if (!build.campaign && !strcmp(pause_settings_hidden_items[index], PAUSE_NETWORK_ITEM))
			continue;
		if (widget != NONE && build.widget_tags[widget] == tag_index)
			return TRUE;
	}
	return FALSE;
}

/* in a game (a multiplayer map's pause menu, a campaign's in co-op), Network Setup opens its pages alone
(PAUSE_NETWORK_SCREEN: tools/port_settings.py), not the rows that would drop the game or are for the main menu */
static void pause_network_patch(void)
{
	long item_widget = widget_named(PAUSE_NETWORK_ITEM);
	long screen_widget = widget_named(PAUSE_NETWORK_SCREEN);
	struct ui_widget_definition *item;
	struct ui_widget_event_handler_reference *handlers;
	long handler;

	if (item_widget == NONE || screen_widget == NONE)
		return;
	item = tag_get(UI_WIDGET_DEFINITION_TAG, build.widget_tags[item_widget]);
	handlers = item->event_handlers.address;
	for (handler = 0; handler < item->event_handlers.count; handler++)
	{
		if (TEST_FLAG(handlers[handler].flags, _event_handler_open_widget_bit))
			reference_set(&handlers[handler].widget_tag, UI_WIDGET_DEFINITION_TAG, build.widget_tags[screen_widget]);
	}
}

/* the hidden items stay in the list, empty and taking no events (which it passes over), as an item's description is the
one at its place in the list; the others move up into the first places */
static void pause_settings_patch(void)
{
	long list_widget = widget_named(PAUSE_SETTINGS_LIST);
	long ok_widget = widget_named(PAUSE_SETTINGS_GAMEPADS_OK);
	long settings_save = function_index("port settings save", "pause", 0);
	struct ui_widget_definition *list, *ok;
	struct ui_widget_child_reference *children;
	struct ui_widget_event_handler_reference *handlers;
	short places[16];
	long child, handler, place_count = 0, place = 0;

	if (list_widget == NONE || ok_widget == NONE)
		return;
	list = tag_get(UI_WIDGET_DEFINITION_TAG, build.widget_tags[list_widget]);
	children = list->child_widgets.address;
	for (child = 0; child < list->child_widgets.count && place_count < (long)NUMBEROF(places); child++)
	{
		struct ui_widget_definition *item = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);

		if (item->type == _widget_type_text_box)
			places[place_count++] = (short)(item->bounds.y0 + children[child].vertical_offset);
	}
	for (child = 0; child < list->child_widgets.count && place < place_count; child++)
	{
		struct ui_widget_definition *item = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);

		if (item->type != _widget_type_text_box)
			continue;
		if (pause_settings_item_hidden(children[child].widget_tag.index))
		{
			item->event_handlers.count = 0;
			reference_clear(&item->background_bitmap, BITMAP_GROUP_TAG);
			reference_clear(&item->text_label_string_list, UNICODE_STRING_LIST_TAG);
			/* (nowhere for the mouse to focus it) */
			item->bounds.y1 = item->bounds.y0;
			item->bounds.x1 = item->bounds.x0;
		}
		else
		{
			children[child].vertical_offset = (short)(places[place++] - item->bounds.y0);
		}
	}
	ok = tag_get(UI_WIDGET_DEFINITION_TAG, build.widget_tags[ok_widget]);
	handlers = ok->event_handlers.address;
	for (handler = 0; handler < ok->event_handlers.count; handler++)
	{
		if (handlers[handler].function == settings_save)
			handlers[handler].function = (short)function_index("port profile settings save", "pause", 0);
	}
}

static void pause_campaign_patch(struct cache_file_tag_instance *instances)
{
	long screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, CAMPAIGN_PAUSE_SCREEN);
	struct ui_widget_definition *definition;
	struct ui_widget_child_reference *children;
	long child;

	if (screen == NONE)
		return;
	definition = tag_get(UI_WIDGET_DEFINITION_TAG, screen);
	children = definition->child_widgets.address;
	for (child = 0; child < definition->child_widgets.count; child++)
	{
		struct ui_widget_definition *list;
		struct ui_widget_child_reference const *rows;
		short span, top, grow, bottom;
		long revert;

		if (children[child].widget_tag.index == NONE)
			continue;
		list = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		if (list->type != _widget_type_column_list || list->child_widgets.count < 2)
			continue;
		rows = list->child_widgets.address;
		/* (REVERT TO SAVED, by its tag: it opens its confirmation rather
		than running a function, and a map's list need not have it second) */
		for (revert = 0; revert < list->child_widgets.count; revert++)
		{
			if (tag_name_ends(rows[revert].widget_tag.index, "\\restart_at_save_point_button"))
				break;
		}
		if (revert == list->child_widgets.count)
			return;
		span = (short)(rows[list->child_widgets.count - 1].vertical_offset - rows[0].vertical_offset);
		top = rows[0].vertical_offset;
		bottom = list->bounds.y1;
		if (pause_list_patch(instances, list, revert, FALSE, &grow))
		{
			struct ui_widget_child_reference *placed = list->child_widgets.address;
			struct ui_widget_definition const *button =
				tag_get(UI_WIDGET_DEFINITION_TAG, placed[0].widget_tag.index);
			short height = (short)(button->bounds.y1 - button->bounds.y0);
			short prompts = pause_campaign_prompts(definition);
			short count = (short)list->child_widgets.count;
			short row, new_last, need = 0;

			for (row = 0; row < count; row++)
				placed[row].vertical_offset = (short)(top + row * PAUSE_CAMPAIGN_ROW_PITCH);
			new_last = placed[count - 1].vertical_offset;
			list->bounds.y1 = bottom;
			if (list->bounds.y1 < (short)(new_last + height))
				list->bounds.y1 = (short)(new_last + height);
			/* keep the gap the last row had above the prompts, and grow the
			box by the same amount */
			if (prompts != NONE)
			{
				short old_bottom = (short)(children[child].vertical_offset + top + span + height);
				short new_bottom = (short)(children[child].vertical_offset + new_last + height);
				short clearance = (short)(prompts - old_bottom);

				if (clearance < 0)
					clearance = 0;
				need = (short)(new_bottom + clearance - prompts);
			}
			if (need > 0)
			{
				pause_campaign_shift_prompts(definition, need);
				pause_campaign_grow_box(definition, need);
			}
			pause_settings_patch();
			platform_log("menus: the pause menu has SETTINGS%s", build.coop ? " and FRIENDLY FIRE" : "");
		}
		return;
	}
}

/* The full-screen campaign pause menu also serves network co-op. Local
split-screen keeps its separate layout. */
static boolean full_screen_campaign_map(void)
{
	return tag_loaded('Soul', MULTIPLAYER_COLLECTION) == NONE &&
		tag_loaded(UI_WIDGET_DEFINITION_TAG, CAMPAIGN_PAUSE_SCREEN) != NONE &&
		(game_connection() != _game_connection_local || network_game_is_active() || !game_is_cooperative());
}

/* of those, a network game's: network co-op, whose pause menu gets FRIENDLY
FIRE too */
static boolean network_coop_campaign_map(void)
{
	return full_screen_campaign_map() && game_connection() != _game_connection_local && network_game_is_active();
}

/* whether display.menus asks for the PC version's menus: "pc" in any case
(a value neither "pc" nor "xbox" is said once in the log, and is the Xbox's) */
static boolean menus_pc_chosen(void)
{
	static boolean said;
	char const *value = config_string("display.menus");

	if (!csstrcasecmp(value, "pc"))
		return TRUE;
	if (csstrcasecmp(value, "xbox") && !said)
	{
		platform_log("menus: display.menus \"%s\" is not \"xbox\" or \"pc\": the Xbox's", value);
		said = TRUE;
	}
	return FALSE;
}

#ifdef HALO_PS3
/* ---------- the Xbox menus' LAN PLAY and INTERNET PLAY (the PS3 has no PC menus): in SYSTEM LINK PLAY's place and
after it, copies of System Link's game list for this network's games and for the server browser's (menu_functions.c) */

#define XBOX_MULTIPLAYER "ui\\shell\\main_menu\\multiplayer_type_select\\"
#define XBOX_SERVER_LIST XBOX_MULTIPLAYER "connected\\server_list\\"
/* (under connected\, so that they open only on maps that play multiplayer: ui_widget_launch_widget) */
#define XBOX_LAN XBOX_MULTIPLAYER "connected\\lan\\"
#define XBOX_INTERNET XBOX_MULTIPLAYER "connected\\internet\\"
#define XBOX_HOST XBOX_MULTIPLAYER "connected\\host\\"
#define XBOX_PREGAME XBOX_MULTIPLAYER "connected\\pregame\\"
#define XBOX_SOLO_LEVELS "ui\\shell\\main_menu\\solo_level_select\\"
#define XBOX_DIFFICULTY "ui\\shell\\main_menu\\difficulty_select\\"
#define XBOX_SYSTEM_LINK_ITEM 2
#define XBOX_ITEM_SPACING 33
/* the server list's description's child that says it is looking for games */
#define XBOX_LOOKING_CHILD 3
/* CREATE GAME's key's children: the Y button and its text */
#define XBOX_KEY_TEXT_CHILD 1
/* where the pregame screen shows the invite code: right of "Choose teams using D-pad", on its bar */
#define XBOX_INVITE_CODE_TOP 82
#define XBOX_INVITE_CODE_LEFT 310
#define XBOX_INVITE_CODE_WIDTH 262
/* the host's Y key in the pregame screen (its slots): left of the key bar's X (130), at its height; its text after the
button, which CREATE GAME's centres in 180 units */
#define XBOX_SLOTS_KEY_LEFT 20
/* ui\shell\strings\game_variant_descriptions' strings: 10 of the gametype types, then the Xbox's 26 gametypes' */
#define XBOX_VARIANT_DESCRIPTIONS 36
#define XBOX_SLOTS_TEXT_LEFT 22
#define XBOX_SLOTS_TEXT_WIDTH 90

enum
{
	_xbox_lan_text,
	_xbox_internet_text,
	_xbox_descriptions,
	_xbox_code_text,
	_xbox_host_text,
	_xbox_host_texts,
	_xbox_host_descriptions,
	_xbox_slots_text,
	_xbox_lan_item,
	_xbox_lan_screen,
	_xbox_lan_list,
	_xbox_internet_item,
	_xbox_internet_screen,
	_xbox_internet_list,
	_xbox_internet_description,
	_xbox_internet_status,
	_xbox_code_button,
	_xbox_code_key,
	_xbox_invite_code,
	_xbox_slots_key,
	_xbox_slots_button,
	/* HOST ONLINE's screen of CO-OP CAMPAIGN and MULTIPLAYER, and co-op's level and difficulty (xbox_host_wire) */
	_xbox_host_item,
	_xbox_host_screen,
	_xbox_host_header,
	_xbox_host_list,
	_xbox_host_coop_item,
	_xbox_host_multiplayer_item,
	_xbox_host_description,
	_xbox_host_picture,
	_xbox_host_description_text,
	_xbox_host_level_screen,
	_xbox_host_level_list,
	_xbox_host_difficulty_screen,
	_xbox_host_difficulty_list,
	/* SETTINGS' screen of PLAYER PROFILES and EXTRA OPTIONS, and EXTRA OPTIONS' (xbox_settings_wire) */
	_xbox_settings_texts,
	_xbox_settings_descriptions,
	_xbox_settings_title,
	_xbox_extra_title,
	_xbox_extra_labels,
	_xbox_extra_widescreen_values,
	_xbox_extra_frame_rate_values,
	_xbox_extra_button_values,
	_xbox_extra_dynamic_values,
	_xbox_extra_preset_values,
	_xbox_extra_help,
	_xbox_settings_screen,
	_xbox_settings_header,
	_xbox_settings_list,
	_xbox_settings_profiles_item,
	_xbox_settings_extra_item,
	_xbox_settings_description,
	_xbox_settings_picture,
	_xbox_settings_description_text,
	_xbox_extra_screen,
	_xbox_extra_header,
	_xbox_extra_list,
	_xbox_extra_widescreen_row,
	_xbox_extra_widescreen_label,
	_xbox_extra_widescreen_spinner,
	_xbox_extra_frame_rate_row,
	_xbox_extra_frame_rate_label,
	_xbox_extra_frame_rate_spinner,
	_xbox_extra_button_row,
	_xbox_extra_button_label,
	_xbox_extra_button_spinner,
	_xbox_extra_dynamic_row,
	_xbox_extra_dynamic_label,
	_xbox_extra_dynamic_spinner,
	_xbox_extra_preset_row,
	_xbox_extra_preset_label,
	_xbox_extra_preset_spinner,
	_xbox_extra_help_text,
	NUMBER_OF_XBOX_TAGS,
	/* (each part's string lists come first) */
	FIRST_XBOX_WIDGET = _xbox_lan_item,
	FIRST_XBOX_SETTINGS_TAG = _xbox_settings_texts,
	FIRST_XBOX_SETTINGS_WIDGET = _xbox_settings_screen,
};

#define XBOX_SETTINGS "ui\\shell\\main_menu\\settings_select\\"
#define XBOX_PROFILE_EDIT XBOX_SETTINGS "player_setup\\player_profile_edit\\"
#define XBOX_ADVANCED_CONTROLS XBOX_PROFILE_EDIT "advanced_controls\\"
#define XBOX_EXTRA XBOX_SETTINGS "extra_options\\"

/* named as the map's tags are (the instances keep the pointers) */
static char const *const xbox_tag_names[NUMBER_OF_XBOX_TAGS] =
{
	XBOX_MULTIPLAYER "multiplayer_lan_option",
	XBOX_MULTIPLAYER "multiplayer_internet_option",
	XBOX_MULTIPLAYER "multiplayer_option_descriptions_port",
	XBOX_INTERNET "enter_code_button",
	XBOX_MULTIPLAYER "multiplayer_host_option",
	XBOX_HOST "host_options",
	XBOX_HOST "host_option_descriptions",
	XBOX_PREGAME "slots_button",
	XBOX_MULTIPLAYER "multiplayer_type_lan_item",
	XBOX_LAN "lan_screen",
	XBOX_LAN "lan_list",
	XBOX_MULTIPLAYER "multiplayer_type_internet_item",
	XBOX_INTERNET "internet_screen",
	XBOX_INTERNET "internet_list",
	XBOX_INTERNET "internet_extended_desc",
	XBOX_INTERNET "internet_status_text",
	XBOX_INTERNET "enter_code_button",
	XBOX_INTERNET "enter_code_key",
	XBOX_PREGAME "invite_code_text",
	XBOX_PREGAME "slots_key",
	XBOX_PREGAME "slots_button",
	XBOX_MULTIPLAYER "multiplayer_type_host_item",
	XBOX_HOST "host_select_screen",
	XBOX_HOST "header_host_online",
	XBOX_HOST "host_select_list",
	XBOX_HOST "host_coop_item",
	XBOX_HOST "host_multiplayer_item",
	XBOX_HOST "host_list_ext_desc",
	XBOX_HOST "host_options_pic",
	XBOX_HOST "host_options_txt",
	XBOX_HOST "host_level_select_screen",
	XBOX_HOST "host_level_select_list",
	XBOX_HOST "host_difficulty_select_screen",
	XBOX_HOST "host_difficulty_select_list",
	XBOX_SETTINGS "settings_options",
	XBOX_SETTINGS "settings_option_descriptions",
	XBOX_SETTINGS "settings_title",
	XBOX_EXTRA "extra_title",
	XBOX_EXTRA "extra_labels",
	XBOX_EXTRA "var_widescreen",
	XBOX_EXTRA "var_frame_rate",
	XBOX_EXTRA "var_button_icons",
	XBOX_EXTRA "var_dynamic_resolution",
	XBOX_EXTRA "var_preset",
	XBOX_EXTRA "extra_help_strings",
	XBOX_SETTINGS "settings_select_screen",
	XBOX_SETTINGS "header_settings",
	XBOX_SETTINGS "settings_select_list",
	XBOX_SETTINGS "settings_profiles_item",
	XBOX_SETTINGS "settings_extra_item",
	XBOX_SETTINGS "settings_list_ext_desc",
	XBOX_SETTINGS "settings_options_pic",
	XBOX_SETTINGS "settings_options_txt",
	XBOX_EXTRA "extra_options_screen",
	XBOX_EXTRA "header_extra_options",
	XBOX_EXTRA "extra_options_menu",
	XBOX_EXTRA "op_widescreen",
	XBOX_EXTRA "widescreen_label",
	XBOX_EXTRA "widescreen_spinner",
	XBOX_EXTRA "op_frame_rate",
	XBOX_EXTRA "frame_rate_label",
	XBOX_EXTRA "frame_rate_spinner",
	XBOX_EXTRA "op_button_icons",
	XBOX_EXTRA "button_icons_label",
	XBOX_EXTRA "button_icons_spinner",
	XBOX_EXTRA "op_dynamic_resolution",
	XBOX_EXTRA "dynamic_resolution_label",
	XBOX_EXTRA "dynamic_resolution_spinner",
	XBOX_EXTRA "op_preset",
	XBOX_EXTRA "preset_label",
	XBOX_EXTRA "preset_spinner",
	XBOX_EXTRA "extra_options_help",
};

/* the map's widgets EXTRA OPTIONS copies (xbox_settings_find) */
struct xbox_settings_stock
{
	long main_menu_item;
	long profiles_screen;
	long type_screen;
	long type_list;
	long first_item;
	long second_item;
	long type_description;
	long advanced_screen;
	long advanced_list;
	long first_row;
	long second_row;
	long advanced_help;
	long button_key;
	long options_key;
};

/* a list's handler: its event, the function it runs, and whether it then opens the map select (hosting) */
struct xbox_list_event
{
	char const *event;
	char const *function;
	boolean hosts;
};

/* (as System Link's list: A or START joins the focused game, or hosts when there is none; Y hosts) */
static struct xbox_list_event const xbox_lan_events[] =
{
	{ "created", "xbox lan begin", FALSE },
	{ "deleted", "xbox lan end", FALSE },
	{ "a", "xbox lan select", FALSE },
	{ "start", "xbox lan select", FALSE },
	{ "a", "xbox lan create if none", TRUE },
	{ "start", "xbox lan create if none", TRUE },
	{ "y", "xbox lan create", TRUE },
};

/* (Y types an invite code) */
static struct xbox_list_event const xbox_internet_events[] =
{
	{ "created", "xbox internet begin", FALSE },
	{ "deleted", "xbox internet end", FALSE },
	{ "a", "xbox internet select", FALSE },
	{ "start", "xbox internet select", FALSE },
	{ "y", "xbox internet code", FALSE },
};

/* a copy of the map's widget, under its own name (an instance's, which menu_functions.c finds) */
static struct ui_widget_definition *xbox_widget_copy(long tag_index, char const *name)
{
	struct ui_widget_definition *copy = allocate(sizeof(*copy));

	if (copy)
	{
		memcpy(copy, tag_get(UI_WIDGET_DEFINITION_TAG, tag_index), sizeof(*copy));
		memset(copy->name, 0, sizeof(copy->name));
		strncpy(copy->name, name, sizeof(copy->name) - 1);
	}
	return copy;
}

/* a copy of SYSTEM LINK PLAY's item: its own text, and its own screen opened */
static void xbox_item_set(struct ui_widget_definition *item, long text_tag, long screen_tag)
{
	struct ui_widget_event_handler_reference *handlers = allocate(item->event_handlers.count * sizeof(*handlers));
	long index;

	if (!handlers)
		return;
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (item->event_handlers.count)
		memcpy(handlers, item->event_handlers.address, item->event_handlers.count * sizeof(*handlers));
	for (index = 0; index < item->event_handlers.count; index++)
	{
		if (TEST_FLAG(handlers[index].flags, _event_handler_open_widget_bit))
			reference_set(&handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG, screen_tag);
	}
	item->event_handlers.address = handlers;
	reference_set(&item->text_label_string_list, UNICODE_STRING_LIST_TAG, text_tag);
	item->string_list_index = 0;
}

/* a copy of System Link's screen: its list the copy's, and in CREATE GAME's key's place key_tag (NONE: kept) */
static void xbox_screen_set(struct ui_widget_definition *screen, long server_list, long list_tag, long key_tag)
{
	struct ui_widget_child_reference *children = allocate(screen->child_widgets.count * sizeof(*children));
	long index;

	if (!children)
		return;
	for (index = 0; index < screen->child_widgets.count; index++)
	{
		struct ui_widget_child_reference const *child =
			(struct ui_widget_child_reference const *)screen->child_widgets.address + index;

		children[index] = *child;
		if (child->widget_tag.index == server_list)
			reference_set(&children[index].widget_tag, UI_WIDGET_DEFINITION_TAG, list_tag);
		if (key_tag != NONE && tag_name_ends(child->widget_tag.index, "\\create_game_key"))
			reference_set(&children[index].widget_tag, UI_WIDGET_DEFINITION_TAG, key_tag);
	}
	screen->child_widgets.address = children;
}

/* a copy of System Link's list: its rows, with the port's update and handlers (map_select: hosting's screen) */
static void xbox_list_set(struct ui_widget_definition *list, char const *update, struct xbox_list_event const *events,
	long count, long map_select)
{
	struct ui_widget_game_data_input_reference *inputs = allocate(sizeof(*inputs));
	struct ui_widget_event_handler_reference *handlers = allocate(count * sizeof(*handlers));
	long index;

	if (!inputs || !handlers)
		return;
	inputs->function = (short)game_data_input_index(update, "xbox", 0);
	list->game_data_inputs.count = 1;
	list->game_data_inputs.address = inputs;
	for (index = 0; index < count; index++)
	{
		handlers[index].flags = FLAG(_event_handler_run_function_bit);
		handlers[index].event_type = (short)name_index(events[index].event, event_names, NUMBEROF(event_names));
		handlers[index].function = (short)function_index(events[index].function, "xbox", 0);
		reference_clear(&handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG);
		reference_clear(&handlers[index].sound_effect, SOUND_TAG);
		if (events[index].hosts)
		{
			handlers[index].flags |= FLAG(_event_handler_open_widget_bit);
			reference_set(&handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG, map_select);
		}
	}
	list->event_handlers.count = count;
	list->event_handlers.address = handlers;
}

static void xbox_child_set(struct ui_widget_child_reference *child, long tag_index, char const *name)
{
	reference_set(&child->widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
	memset(child->name, 0, sizeof(child->name));
	strncpy(child->name, name, sizeof(child->name) - 1);
}

/* ---------- SETTINGS (the PS3's): a screen of PLAYER PROFILES and EXTRA OPTIONS, a copy of the multiplayer menu's;
EXTRA OPTIONS' five rows, a copy of the profile's ADVANCED CONTROLS, written to config.toml */

/* the headers' place and colour, those of the maps' header pictures' text (in the menu's large font) */
#define XBOX_HEADER_TOP 46
#define XBOX_HEADER_LEFT 65
#define XBOX_HEADER_HEIGHT 26
#define XBOX_HEADER_RIGHT 600
/* "STYLE A" in the spinners' font (large_ui) is 81 units, "PERFORMANCE" 138: ADVANCED CONTROLS' spinners are 46 wide */
#define XBOX_BUTTON_SPINNER_EXTRA_WIDTH 40
#define XBOX_PRESET_SPINNER_EXTRA_WIDTH 100

static struct ui_widget_definition *xbox_widget(long tag_index)
{
	return tag_index == NONE ? NULL : tag_get(UI_WIDGET_DEFINITION_TAG, tag_index);
}

static long xbox_child_tag(long tag_index, long child)
{
	struct ui_widget_definition *widget = xbox_widget(tag_index);

	if (!widget || child >= widget->child_widgets.count)
		return NONE;
	return ((struct ui_widget_child_reference *)widget->child_widgets.address)[child].widget_tag.index;
}

/* the map's widgets the screens copy, each as the Xbox's ui.map has it; FALSE if one is not */
static boolean xbox_settings_find(struct xbox_settings_stock *stock)
{
	struct ui_widget_definition *item;
	long index;
	boolean opens = FALSE;

	stock->main_menu_item = tag_loaded(UI_WIDGET_DEFINITION_TAG, "ui\\shell\\main_menu\\main_menu_item_settings");
	stock->profiles_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SETTINGS "player_setup\\player_profile_select_screen");
	stock->type_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_select_screen");
	stock->type_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_select_list");
	stock->first_item = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_coop_item");
	stock->second_item = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_split_item");
	stock->type_description = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_list_ext_desc");
	stock->advanced_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_ADVANCED_CONTROLS "advanced_controls_screen");
	stock->advanced_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_ADVANCED_CONTROLS "advanced_controls_menu");
	stock->first_row = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_ADVANCED_CONTROLS "op_invert_joystick");
	stock->second_row = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_ADVANCED_CONTROLS "op_controller_vibration");
	stock->advanced_help = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_ADVANCED_CONTROLS "advanced_controls_help");
	stock->button_key = tag_loaded(UI_WIDGET_DEFINITION_TAG, "ui\\shell\\main_menu\\button_key");
	stock->options_key = tag_loaded(UI_WIDGET_DEFINITION_TAG, "ui\\shell\\main_menu\\button_key_options");
	if (stock->main_menu_item == NONE || stock->profiles_screen == NONE || stock->type_screen == NONE ||
		stock->type_list == NONE || stock->first_item == NONE || stock->second_item == NONE ||
		stock->type_description == NONE || stock->advanced_screen == NONE || stock->advanced_list == NONE ||
		stock->first_row == NONE || stock->second_row == NONE || stock->advanced_help == NONE ||
		stock->button_key == NONE || stock->options_key == NONE)
		return FALSE;
	item = xbox_widget(stock->main_menu_item);
	for (index = 0; index < item->event_handlers.count; index++)
	{
		struct ui_widget_event_handler_reference const *handler =
			(struct ui_widget_event_handler_reference const *)item->event_handlers.address + index;

		opens |= TEST_FLAG(handler->flags, _event_handler_open_widget_bit) &&
			handler->widget_tag.index == stock->profiles_screen;
	}
	/* the screens' children: the multiplayer menu's header, list, key and line; its description's picture and
	text; ADVANCED CONTROLS' list, header and key; each row's label and spinner */
	return opens && xbox_widget(stock->type_screen)->child_widgets.count == 4 &&
		xbox_child_tag(stock->type_screen, 1) == stock->type_list &&
		xbox_child_tag(stock->type_screen, 2) == stock->button_key &&
		xbox_widget(stock->type_description)->child_widgets.count == 2 &&
		xbox_widget(stock->advanced_screen)->child_widgets.count == 3 &&
		xbox_child_tag(stock->advanced_screen, 0) == stock->advanced_list &&
		xbox_child_tag(stock->advanced_screen, 2) == stock->options_key &&
		xbox_widget(stock->advanced_list)->child_widgets.count >= 5 &&
		xbox_child_tag(stock->advanced_list, 0) == stock->first_row &&
		xbox_widget(stock->first_row)->child_widgets.count == 2 &&
		xbox_widget(stock->second_row)->child_widgets.count == 2 &&
		xbox_widget(xbox_child_tag(stock->first_row, 1)) &&
		xbox_widget(xbox_child_tag(stock->first_row, 1))->type == _widget_type_spinner_list &&
		xbox_widget(xbox_child_tag(stock->second_row, 1)) &&
		xbox_widget(xbox_child_tag(stock->second_row, 1))->type == _widget_type_spinner_list;
}

/* the string lists and the copies, whose references xbox_settings_wire sets once the tags exist */
static void xbox_settings_define(struct xbox_settings_stock const *stock, void **definitions)
{
	/* (ASCII: port_stubs.c's halo_menus_utf16) */
	char const *texts[] = { "PLAYER PROFILES", "EXTRA OPTIONS" };
	char const *descriptions[] =
	{
		"Create, edit or delete the\r\nplayer profiles: name,\r\ncontrols and color.",
		"Choose the picture's shape,\r\nthe frame rate, the button\r\nicons, the resolution and a\r\npreset.",
	};
	char const *settings_title = "SETTINGS";
	char const *extra_title = "EXTRA OPTIONS";
	/* (in the rows' order, xbox_settings_wire's; the helps too, each row's values in turn) */
	char const *labels[] = { "WIDESCREEN", "BUTTON ICONS", "PRESET", "FRAME RATE", "DYNAMIC RESOLUTION" };
	char const *widescreen_values[] = { "ON", "OFF" };
	char const *frame_rate_values[] = { "60", "30" };
	char const *button_values[] = { "XBOX", "STYLE A", "STYLE B" };
	char const *dynamic_values[] = { "ON", "OFF" };
	char const *preset_values[] = { "CUSTOM", "QUALITY", "PERFORMANCE" };
	char const *help[] =
	{
		"The picture fills a 16:9 screen, and you see more\r\nof the world at its sides.",
		"The picture keeps the Xbox's 4:3 shape, in the\r\nmiddle of the screen.",
		"The Xbox's buttons, as the maps draw them.",
		"The PS3's buttons: colored circles with white\r\nsymbols.",
		"The PS3's buttons: black circles with colored\r\nsymbols.",
		"FRAME RATE and DYNAMIC RESOLUTION as you set\r\nthem. Split screen always plays at 30.",
		"30 frames a second, the world always drawn at the\r\nfull resolution. Split screen always plays at 30.",
		"60 frames a second, the world drawn smaller in\r\nheavy scenes. Split screen always plays at 30.",
		"The game draws 60 frames a second, blending the\r\nworld between its 30 updates a second.",
		"The game draws 30 frames a second, as on the Xbox.",
		"In heavy scenes the world is drawn smaller, then\r\nscaled up: steadier, a little softer.",
		"The world is always drawn at the full resolution.",
	};

	definitions[_xbox_settings_texts] = string_list_build(texts, NULL, NUMBEROF(texts), NULL);
	definitions[_xbox_settings_descriptions] = string_list_build(descriptions, NULL, NUMBEROF(descriptions), NULL);
	definitions[_xbox_settings_title] = string_list_build(&settings_title, NULL, 1, NULL);
	definitions[_xbox_extra_title] = string_list_build(&extra_title, NULL, 1, NULL);
	definitions[_xbox_extra_labels] = string_list_build(labels, NULL, NUMBEROF(labels), NULL);
	definitions[_xbox_extra_widescreen_values] = string_list_build(widescreen_values, NULL, NUMBEROF(widescreen_values), NULL);
	definitions[_xbox_extra_frame_rate_values] = string_list_build(frame_rate_values, NULL, NUMBEROF(frame_rate_values), NULL);
	definitions[_xbox_extra_button_values] = string_list_build(button_values, NULL, NUMBEROF(button_values), NULL);
	definitions[_xbox_extra_dynamic_values] = string_list_build(dynamic_values, NULL, NUMBEROF(dynamic_values), NULL);
	definitions[_xbox_extra_preset_values] = string_list_build(preset_values, NULL, NUMBEROF(preset_values), NULL);
	definitions[_xbox_extra_help] = string_list_build(help, NULL, NUMBEROF(help), NULL);
	definitions[_xbox_settings_screen] = xbox_widget_copy(stock->type_screen, "settings_select_screen");
	definitions[_xbox_settings_header] = xbox_widget_copy(stock->first_item, "header_settings");
	definitions[_xbox_settings_list] = xbox_widget_copy(stock->type_list, "settings_select_list");
	/* (both as SPLIT SCREEN's item, which only opens its screen; the first in CO-OP's place) */
	definitions[_xbox_settings_profiles_item] = xbox_widget_copy(stock->second_item, "settings_profiles_item");
	definitions[_xbox_settings_extra_item] = xbox_widget_copy(stock->second_item, "settings_extra_item");
	definitions[_xbox_settings_description] = xbox_widget_copy(stock->type_description, "settings_list_ext_desc");
	definitions[_xbox_settings_picture] = xbox_widget_copy(xbox_child_tag(stock->type_description, 0),
		"settings_options_pic");
	definitions[_xbox_settings_description_text] = xbox_widget_copy(xbox_child_tag(stock->type_description, 1),
		"settings_options_txt");
	definitions[_xbox_extra_screen] = xbox_widget_copy(stock->advanced_screen, "extra_options_screen");
	definitions[_xbox_extra_header] = xbox_widget_copy(stock->first_item, "header_extra_options");
	definitions[_xbox_extra_list] = xbox_widget_copy(stock->advanced_list, "extra_options_menu");
	definitions[_xbox_extra_widescreen_row] = xbox_widget_copy(stock->first_row, "op_widescreen");
	definitions[_xbox_extra_widescreen_label] = xbox_widget_copy(xbox_child_tag(stock->first_row, 0), "widescreen_label");
	definitions[_xbox_extra_widescreen_spinner] = xbox_widget_copy(xbox_child_tag(stock->first_row, 1),
		"widescreen_spinner");
	definitions[_xbox_extra_frame_rate_row] = xbox_widget_copy(stock->second_row, "op_frame_rate");
	definitions[_xbox_extra_frame_rate_label] = xbox_widget_copy(xbox_child_tag(stock->second_row, 0), "frame_rate_label");
	definitions[_xbox_extra_frame_rate_spinner] = xbox_widget_copy(xbox_child_tag(stock->second_row, 1),
		"frame_rate_spinner");
	/* (a spinner row, as FRAME RATE's) */
	definitions[_xbox_extra_button_row] = xbox_widget_copy(stock->second_row, "op_button_icons");
	definitions[_xbox_extra_button_label] = xbox_widget_copy(xbox_child_tag(stock->second_row, 0), "button_icons_label");
	definitions[_xbox_extra_button_spinner] = xbox_widget_copy(xbox_child_tag(stock->second_row, 1),
		"button_icons_spinner");
	definitions[_xbox_extra_dynamic_row] = xbox_widget_copy(stock->second_row, "op_dynamic_resolution");
	definitions[_xbox_extra_dynamic_label] = xbox_widget_copy(xbox_child_tag(stock->second_row, 0),
		"dynamic_resolution_label");
	definitions[_xbox_extra_dynamic_spinner] = xbox_widget_copy(xbox_child_tag(stock->second_row, 1),
		"dynamic_resolution_spinner");
	definitions[_xbox_extra_preset_row] = xbox_widget_copy(stock->second_row, "op_preset");
	definitions[_xbox_extra_preset_label] = xbox_widget_copy(xbox_child_tag(stock->second_row, 0), "preset_label");
	definitions[_xbox_extra_preset_spinner] = xbox_widget_copy(xbox_child_tag(stock->second_row, 1), "preset_spinner");
	definitions[_xbox_extra_help_text] = xbox_widget_copy(stock->advanced_help, "extra_options_help");
}

/* a screen's children: the copy's records of its template's, those named here pointing at the port's widgets */
static void xbox_children_set(struct ui_widget_definition *widget, long const *template_children, long count,
	long const *tags, char const *const *names)
{
	struct ui_widget_child_reference *children = allocate(count * sizeof(*children));
	long index;

	if (!children)
		return;
	for (index = 0; index < count; index++)
	{
		children[index] = ((struct ui_widget_child_reference *)widget->child_widgets.address)[template_children[index]];
		if (tags[index] != NONE)
			xbox_child_set(&children[index], tags[index], names[index]);
	}
	widget->child_widgets.count = count;
	widget->child_widgets.address = children;
}

/* a text widget (a list item's copy) as a screen's header: its title in the header pictures' place and blue */
static void xbox_header_set(struct ui_widget_definition *header, long title_tag)
{
	header->bounds.y0 = XBOX_HEADER_TOP;
	header->bounds.x0 = XBOX_HEADER_LEFT;
	header->bounds.y1 = XBOX_HEADER_TOP + XBOX_HEADER_HEIGHT;
	header->bounds.x1 = XBOX_HEADER_RIGHT;
	header->event_handlers.count = 0;
	header->game_data_inputs.count = 0;
	header->justification = (short)name_index("left", align_names, NUMBEROF(align_names));
	header->text_color.alpha = 1.0f;
	header->text_color.red = 41.0f / 255.0f;
	header->text_color.green = 149.0f / 255.0f;
	header->text_color.blue = 1.0f;
	reference_set(&header->text_label_string_list, UNICODE_STRING_LIST_TAG, title_tag);
	header->string_list_index = 0;
}

/* handlers that run a function, and with go_back return to the screen before */
static void xbox_handlers_set(struct ui_widget_definition *widget, char const *const *events, long count,
	char const *function, boolean go_back)
{
	struct ui_widget_event_handler_reference *handlers = allocate(count * sizeof(*handlers));
	long index;

	if (!handlers)
		return;
	for (index = 0; index < count; index++)
	{
		handlers[index].flags = FLAG(_event_handler_run_function_bit) |
			(go_back ? FLAG(_event_handler_go_back_to_previous_widget_bit) : 0);
		handlers[index].event_type = (short)name_index(events[index], event_names, NUMBEROF(event_names));
		handlers[index].function = (short)function_index(function, "xbox", 0);
		reference_clear(&handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG);
		reference_clear(&handlers[index].sound_effect, SOUND_TAG);
	}
	widget->event_handlers.count = count;
	widget->event_handlers.address = handlers;
}

static void xbox_input_set(struct ui_widget_definition *widget, char const *function)
{
	struct ui_widget_game_data_input_reference *inputs = allocate(sizeof(*inputs));

	if (!inputs)
		return;
	inputs->function = (short)game_data_input_index(function, "xbox", 0);
	widget->game_data_inputs.count = 1;
	widget->game_data_inputs.address = inputs;
}

/* a spinner bound to a setting: its values, shown by its strings in order ("port setting load" and "port settings
save", menu_functions.c) */
static void xbox_setting_add(long definition_index, char const *setting, char const *const *values, long count)
{
	struct pc_menu_setting *settings = realloc(menu_tags.settings, (menu_tags.setting_count + 1) * sizeof(*settings));
	struct pc_menu_setting *entry;
	long index;

	if (!settings)
	{
		build.failed = TRUE;
		return;
	}
	menu_tags.settings = settings;
	entry = &menu_tags.settings[menu_tags.setting_count++];
	memset(entry, 0, sizeof(*entry));
	entry->definition_index = definition_index;
	entry->setting = setting;
	entry->loaded_index = NONE;
	for (index = 0; index < count && index < MAXIMUM_STRINGS; index++)
		entry->values[index] = values[index];
	entry->value_count = index;
}

/* the screens' references, children and handlers, and their five settings */
static void xbox_settings_wire(struct xbox_settings_stock const *stock, void **definitions, long const *tags)
{
	/* (display.screen_width: 0 is the display's shape, 640 the Xbox's 4:3) */
	static char const *const widescreen_values[] = { "0", "640" };
	static char const *const frame_rate_values[] = { "true", "false" };
	static char const *const button_values[] = { "xbox", "style_a", "style_b" };
	static char const *const dynamic_values[] = { "true", "false" };
	static char const *const preset_values[] = { "custom", "quality", "performance" };
	/* (in their order on the screen) */
	static long const rows[][4] =
	{
		{ _xbox_extra_widescreen_row, _xbox_extra_widescreen_label, _xbox_extra_widescreen_spinner,
			_xbox_extra_widescreen_values },
		{ _xbox_extra_button_row, _xbox_extra_button_label, _xbox_extra_button_spinner, _xbox_extra_button_values },
		{ _xbox_extra_preset_row, _xbox_extra_preset_label, _xbox_extra_preset_spinner, _xbox_extra_preset_values },
		{ _xbox_extra_frame_rate_row, _xbox_extra_frame_rate_label, _xbox_extra_frame_rate_spinner,
			_xbox_extra_frame_rate_values },
		{ _xbox_extra_dynamic_row, _xbox_extra_dynamic_label, _xbox_extra_dynamic_spinner,
			_xbox_extra_dynamic_values },
	};
	static char const *const accept_events[] = { "a", "start" };
	static char const *const created_event[] = { "created" };
	struct ui_widget_definition *screen = definitions[_xbox_settings_screen];
	struct ui_widget_definition *list = definitions[_xbox_settings_list];
	struct ui_widget_definition *description = definitions[_xbox_settings_description];
	struct ui_widget_definition *picture = definitions[_xbox_settings_picture];
	struct ui_widget_definition *text = definitions[_xbox_settings_description_text];
	struct ui_widget_definition *extra_screen = definitions[_xbox_extra_screen];
	struct ui_widget_definition *extra_list = definitions[_xbox_extra_list];
	long index;

	/* SETTINGS: the header, the list of two, the key (the multiplayer menu's line goes) */
	{
		long template_children[] = { 0, 1, 2 };
		long children[] = { tags[_xbox_settings_header], tags[_xbox_settings_list], NONE };
		char const *names[] = { "header_settings", "settings_select_list", "" };

		xbox_children_set(screen, template_children, NUMBEROF(children), children, names);
	}
	screen->event_handlers.count = 0;
	xbox_header_set(definitions[_xbox_settings_header], tags[_xbox_settings_title]);
	{
		long template_children[] = { 0, 1 };
		long children[] = { tags[_xbox_settings_profiles_item], tags[_xbox_settings_extra_item] };
		char const *names[] = { "settings_profiles_item", "settings_extra_item" };

		xbox_children_set(list, template_children, NUMBEROF(children), children, names);
	}
	xbox_input_set(list, "multiplayer type menu update desc");
	reference_set(&list->extended_description_widget, UI_WIDGET_DEFINITION_TAG, tags[_xbox_settings_description]);
	((struct ui_widget_definition *)definitions[_xbox_settings_profiles_item])->bounds =
		xbox_widget(stock->first_item)->bounds;
	xbox_item_set(definitions[_xbox_settings_profiles_item], tags[_xbox_settings_texts], stock->profiles_screen);
	xbox_item_set(definitions[_xbox_settings_extra_item], tags[_xbox_settings_texts], tags[_xbox_extra_screen]);
	((struct ui_widget_definition *)definitions[_xbox_settings_extra_item])->string_list_index = 1;
	/* the description: no picture (its frames are the multiplayer modes'), the text up where it was */
	{
		long template_children[] = { 0, 1 };
		long children[] = { tags[_xbox_settings_picture], tags[_xbox_settings_description_text] };
		char const *names[] = { "settings_options_pic", "settings_options_txt" };

		xbox_children_set(description, template_children, NUMBEROF(children), children, names);
		if (!build.failed)
		{
			struct ui_widget_child_reference *records = description->child_widgets.address;

			records[1].vertical_offset = (short)(records[0].vertical_offset + picture->bounds.y0 - text->bounds.y0);
		}
	}
	reference_clear(&picture->background_bitmap, BITMAP_GROUP_TAG);
	reference_set(&text->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_settings_descriptions]);
	text->string_list_index = 0;

	/* EXTRA OPTIONS: ADVANCED CONTROLS' list at its five rows' places, its header the port's, its key */
	{
		long template_children[] = { 0, 1, 2 };
		long children[] = { tags[_xbox_extra_list], tags[_xbox_extra_header], NONE };
		char const *names[] = { "extra_options_menu", "header_extra_options", "" };

		xbox_children_set(extra_screen, template_children, NUMBEROF(children), children, names);
	}
	extra_screen->event_handlers.count = 0;
	xbox_header_set(definitions[_xbox_extra_header], tags[_xbox_extra_title]);
	{
		long template_children[] = { 0, 1, 2, 3, 4 };
		long children[] = { tags[_xbox_extra_widescreen_row], tags[_xbox_extra_button_row],
			tags[_xbox_extra_preset_row], tags[_xbox_extra_frame_rate_row], tags[_xbox_extra_dynamic_row] };
		char const *names[] = { "op_widescreen", "op_button_icons", "op_preset", "op_frame_rate",
			"op_dynamic_resolution" };

		xbox_children_set(extra_list, template_children, NUMBEROF(children), children, names);
	}
	/* (PRESET's rule, then the help line of the row and value focused, as the Xbox's: menu_functions.c) */
	xbox_input_set(extra_list, "xbox extra options update");
	reference_set(&extra_list->extended_description_widget, UI_WIDGET_DEFINITION_TAG, tags[_xbox_extra_help_text]);
	/* A or START: the changed values into config.toml (applied from the next frame), and back */
	xbox_handlers_set(extra_list, accept_events, NUMBEROF(accept_events), "port settings save", TRUE);
	for (index = 0; index < NUMBEROF(rows); index++)
	{
		long row = rows[index][0];
		long label = rows[index][1];
		long spinner = rows[index][2];
		long values = rows[index][3];
		long template_children[] = { 0, 1 };
		long children[2];
		char const *names[2];

		children[0] = tags[label];
		children[1] = tags[spinner];
		names[0] = ((struct ui_widget_definition *)definitions[label])->name;
		names[1] = ((struct ui_widget_definition *)definitions[spinner])->name;
		xbox_children_set(definitions[row], template_children, 2, children, names);
		reference_set(&((struct ui_widget_definition *)definitions[label])->text_label_string_list,
			UNICODE_STRING_LIST_TAG, tags[_xbox_extra_labels]);
		((struct ui_widget_definition *)definitions[label])->string_list_index = (short)index;
		reference_set(&((struct ui_widget_definition *)definitions[spinner])->text_label_string_list,
			UNICODE_STRING_LIST_TAG, tags[values]);
		xbox_handlers_set(definitions[spinner], created_event, 1, "port setting load", FALSE);
	}
	reference_set(&((struct ui_widget_definition *)definitions[_xbox_extra_help_text])->text_label_string_list,
		UNICODE_STRING_LIST_TAG, tags[_xbox_extra_help]);
	((struct ui_widget_definition *)definitions[_xbox_extra_help_text])->string_list_index = 0;
	xbox_setting_add(tags[_xbox_extra_widescreen_spinner], "display.screen_width", widescreen_values, 2);
	xbox_setting_add(tags[_xbox_extra_frame_rate_spinner], "display.interpolation", frame_rate_values, 2);
	xbox_setting_add(tags[_xbox_extra_button_spinner], "display.button_icons", button_values, 3);
	xbox_setting_add(tags[_xbox_extra_dynamic_spinner], "display.dynamic_resolution", dynamic_values, 2);
	xbox_setting_add(tags[_xbox_extra_preset_spinner], "display.preset", preset_values, 3);
	/* BUTTON ICONS' and PRESET's values are wider than ON or 60: their spinners and left arrows reach further left */
	{
		struct ui_widget_definition *spinner = definitions[_xbox_extra_button_spinner];

		spinner->bounds.x0 -= XBOX_BUTTON_SPINNER_EXTRA_WIDTH;
		spinner->list_header_bounds.x0 -= XBOX_BUTTON_SPINNER_EXTRA_WIDTH;
		spinner->list_header_bounds.x1 -= XBOX_BUTTON_SPINNER_EXTRA_WIDTH;
		spinner = definitions[_xbox_extra_preset_spinner];
		spinner->bounds.x0 -= XBOX_PRESET_SPINNER_EXTRA_WIDTH;
		spinner->list_header_bounds.x0 -= XBOX_PRESET_SPINNER_EXTRA_WIDTH;
		spinner->list_header_bounds.x1 -= XBOX_PRESET_SPINNER_EXTRA_WIDTH;
	}
}

/* the map's own, last, as it cannot be undone: the main menu's SETTINGS opens the new screen */
static void xbox_settings_open(struct xbox_settings_stock const *stock, long settings_screen)
{
	struct ui_widget_definition *item = xbox_widget(stock->main_menu_item);
	struct ui_widget_event_handler_reference *handlers = allocate(item->event_handlers.count * sizeof(*handlers));
	long index;

	if (!handlers)
		return;
	memcpy(handlers, item->event_handlers.address, item->event_handlers.count * sizeof(*handlers));
	for (index = 0; index < item->event_handlers.count; index++)
	{
		if (TEST_FLAG(handlers[index].flags, _event_handler_open_widget_bit) &&
			handlers[index].widget_tag.index == stock->profiles_screen)
			reference_set(&handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG, settings_screen);
	}
	item->event_handlers.address = handlers;
}

/* ---------- HOST ONLINE (the PS3's), after INTERNET PLAY: a screen of CO-OP CAMPAIGN and MULTIPLAYER, a copy of the
multiplayer menu's; co-op's level and difficulty, copies of the campaign's whose A hosts (menu_functions.c) */

/* the map's widgets HOST ONLINE copies (xbox_host_find) */
struct xbox_host_stock
{
	long type_screen;
	long type_list;
	long first_item;
	long second_item;
	long type_description;
	long level_screen;
	long level_list;
	long difficulty_screen;
	long difficulty_list;
	long difficulty_warning;
};

/* the map's widgets the screens copy, each as the Xbox's ui.map has it; FALSE if one is not */
static boolean xbox_host_find(struct xbox_host_stock *stock)
{
	stock->type_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_select_screen");
	stock->type_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_select_list");
	stock->first_item = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_coop_item");
	stock->second_item = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_split_item");
	stock->type_description = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_list_ext_desc");
	stock->level_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SOLO_LEVELS "solo_level_select_screen");
	stock->level_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SOLO_LEVELS "level_select_list");
	stock->difficulty_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_DIFFICULTY "difficulty_select_list_screen");
	stock->difficulty_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_DIFFICULTY "difficulty_select_list");
	stock->difficulty_warning = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_DIFFICULTY "warning_message");
	if (stock->type_screen == NONE || stock->type_list == NONE || stock->first_item == NONE ||
		stock->second_item == NONE || stock->type_description == NONE || stock->level_screen == NONE ||
		stock->level_list == NONE || stock->difficulty_screen == NONE || stock->difficulty_list == NONE ||
		stock->difficulty_warning == NONE)
		return FALSE;
	/* the screens' children: the multiplayer menu's header, list, key and line; its description's picture and text;
	the level screen's list, header and key; the difficulty screen's list, warning, header and key */
	return xbox_widget(stock->type_screen)->child_widgets.count == 4 &&
		xbox_child_tag(stock->type_screen, 1) == stock->type_list &&
		xbox_widget(stock->type_description)->child_widgets.count == 2 &&
		xbox_widget(stock->level_screen)->child_widgets.count == 3 &&
		xbox_child_tag(stock->level_screen, 0) == stock->level_list &&
		xbox_widget(stock->level_list)->type == _widget_type_spinner_list &&
		xbox_widget(stock->difficulty_screen)->child_widgets.count == 4 &&
		xbox_child_tag(stock->difficulty_screen, 0) == stock->difficulty_list &&
		xbox_child_tag(stock->difficulty_screen, 1) == stock->difficulty_warning &&
		xbox_widget(stock->difficulty_list)->type == _widget_type_column_list &&
		xbox_widget(stock->difficulty_list)->child_widgets.count == 4;
}

static void xbox_host_define(struct xbox_host_stock const *stock, void **definitions)
{
	definitions[_xbox_host_screen] = xbox_widget_copy(stock->type_screen, "host_select_screen");
	definitions[_xbox_host_header] = xbox_widget_copy(stock->first_item, "header_host_online");
	definitions[_xbox_host_list] = xbox_widget_copy(stock->type_list, "host_select_list");
	/* (both as SPLIT SCREEN's item; the first in CO-OP's place) */
	definitions[_xbox_host_coop_item] = xbox_widget_copy(stock->second_item, "host_coop_item");
	definitions[_xbox_host_multiplayer_item] = xbox_widget_copy(stock->second_item, "host_multiplayer_item");
	definitions[_xbox_host_description] = xbox_widget_copy(stock->type_description, "host_list_ext_desc");
	definitions[_xbox_host_picture] = xbox_widget_copy(xbox_child_tag(stock->type_description, 0), "host_options_pic");
	definitions[_xbox_host_description_text] = xbox_widget_copy(xbox_child_tag(stock->type_description, 1),
		"host_options_txt");
	definitions[_xbox_host_level_screen] = xbox_widget_copy(stock->level_screen, "host_level_select_screen");
	definitions[_xbox_host_level_list] = xbox_widget_copy(stock->level_list, "host_level_select_list");
	definitions[_xbox_host_difficulty_screen] = xbox_widget_copy(stock->difficulty_screen,
		"host_difficulty_select_screen");
	definitions[_xbox_host_difficulty_list] = xbox_widget_copy(stock->difficulty_list, "host_difficulty_select_list");
}

/* a copy's handlers: the map's, with A's and START's first running the port's function, then opening the screen;
their later ones (the campaign's start) dropped */
static void xbox_accept_replace(struct ui_widget_definition *widget, char const *function, long screen_tag)
{
	struct ui_widget_event_handler_reference const *stock = widget->event_handlers.address;
	struct ui_widget_event_handler_reference *handlers = allocate(widget->event_handlers.count * sizeof(*handlers));
	short events[2];
	boolean replaced[2] = { FALSE, FALSE };
	long index, count = 0, event;

	if (!handlers)
		return;
	events[0] = (short)name_index("a", event_names, NUMBEROF(event_names));
	events[1] = (short)name_index("start", event_names, NUMBEROF(event_names));
	for (index = 0; index < widget->event_handlers.count; index++)
	{
		for (event = 0; event < 2 && stock[index].event_type != events[event]; event++)
			;
		if (event < 2 && replaced[event])
			continue;
		handlers[count] = stock[index];
		if (event < 2)
		{
			handlers[count].flags = FLAG(_event_handler_run_function_bit) | FLAG(_event_handler_open_widget_bit);
			handlers[count].function = (short)function_index(function, "xbox", 0);
			reference_set(&handlers[count].widget_tag, UI_WIDGET_DEFINITION_TAG, screen_tag);
			replaced[event] = TRUE;
		}
		count++;
	}
	widget->event_handlers.count = count;
	widget->event_handlers.address = handlers;
}

/* an item's A and START: the port's function, then (unless it fails) the screen */
static void xbox_accept_set(struct ui_widget_definition *item, char const *function, long screen_tag)
{
	static char const *const accept_events[] = { "a", "start" };
	long index;

	xbox_handlers_set(item, accept_events, NUMBEROF(accept_events), function, FALSE);
	if (build.failed)
		return;
	for (index = 0; index < item->event_handlers.count; index++)
	{
		struct ui_widget_event_handler_reference *handler =
			(struct ui_widget_event_handler_reference *)item->event_handlers.address + index;

		handler->flags |= FLAG(_event_handler_open_widget_bit);
		reference_set(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG, screen_tag);
	}
}

/* the screens' references, children and handlers: MULTIPLAYER goes on to the map select (map_select) */
static void xbox_host_wire(struct xbox_host_stock const *stock, void **definitions, long const *tags, long map_select)
{
	struct ui_widget_definition *screen = definitions[_xbox_host_screen];
	struct ui_widget_definition *list = definitions[_xbox_host_list];
	struct ui_widget_definition *coop_item = definitions[_xbox_host_coop_item];
	struct ui_widget_definition *multiplayer_item = definitions[_xbox_host_multiplayer_item];
	struct ui_widget_definition *text = definitions[_xbox_host_description_text];

	/* HOST ONLINE: the header, the list of two, the key (the line goes, as SETTINGS'); "created" stays the
	multiplayer menu's, one player */
	{
		long template_children[] = { 0, 1, 2 };
		long children[] = { tags[_xbox_host_header], tags[_xbox_host_list], NONE };
		char const *names[] = { "header_host_online", "host_select_list", "" };

		xbox_children_set(screen, template_children, NUMBEROF(children), children, names);
	}
	xbox_header_set(definitions[_xbox_host_header], tags[_xbox_host_text]);
	xbox_input_set(definitions[_xbox_host_header], "xbox host header");
	{
		long template_children[] = { 0, 1 };
		long children[] = { tags[_xbox_host_coop_item], tags[_xbox_host_multiplayer_item] };
		char const *names[] = { "host_coop_item", "host_multiplayer_item" };

		xbox_children_set(list, template_children, NUMBEROF(children), children, names);
	}
	xbox_input_set(list, "xbox host desc");
	reference_set(&list->extended_description_widget, UI_WIDGET_DEFINITION_TAG, tags[_xbox_host_description]);
	coop_item->bounds = xbox_widget(stock->first_item)->bounds;
	reference_set(&coop_item->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_host_texts]);
	coop_item->string_list_index = 0;
	reference_set(&multiplayer_item->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_host_texts]);
	multiplayer_item->string_list_index = 1;
	xbox_accept_set(coop_item, "xbox host coop", tags[_xbox_host_level_screen]);
	xbox_accept_set(multiplayer_item, "xbox host multiplayer", map_select);
	/* the description: the multiplayer modes' pictures ("xbox host desc" picks them), the port's text */
	{
		long template_children[] = { 0, 1 };
		long children[] = { tags[_xbox_host_picture], tags[_xbox_host_description_text] };
		char const *names[] = { "host_options_pic", "host_options_txt" };

		xbox_children_set(definitions[_xbox_host_description], template_children, NUMBEROF(children), children,
			names);
	}
	reference_set(&text->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_host_descriptions]);
	text->string_list_index = 0;

	/* co-op's level: the campaign's list, whose A checks the level and opens co-op's difficulty */
	{
		long template_children[] = { 0, 1, 2 };
		long children[] = { tags[_xbox_host_level_list], NONE, NONE };
		char const *names[] = { "host_level_select_list", "", "" };

		xbox_children_set(definitions[_xbox_host_level_screen], template_children, NUMBEROF(children), children,
			names);
	}
	xbox_accept_replace(definitions[_xbox_host_level_list], "xbox host coop level", tags[_xbox_host_difficulty_screen]);
	/* co-op's difficulty: without the saved game's warning, its A hosts and opens the pregame screen */
	{
		long template_children[] = { 0, 2, 3 };
		long children[] = { tags[_xbox_host_difficulty_list], NONE, NONE };
		char const *names[] = { "host_difficulty_select_list", "", "" };

		xbox_children_set(definitions[_xbox_host_difficulty_screen], template_children, NUMBEROF(children), children,
			names);
	}
	((struct ui_widget_definition *)definitions[_xbox_host_difficulty_screen])->game_data_inputs.count = 0;
	xbox_accept_replace(definitions[_xbox_host_difficulty_list], "xbox host coop start",
		tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_PREGAME "connected_pregame_screen"));
}

static void xbox_multiplayer_patch(void)
{
	/* (ASCII: port_stubs.c's halo_menus_utf16) */
	char const *lan_text = "LAN PLAY";
	char const *internet_text = "INTERNET PLAY";
	char const *code_text = "= ENTER CODE";
	/* (HOST ONLINE's screen is also LAN PLAY's CREATE GAME, its header and descriptions those of the mode) */
	char const *host_text[] = { "HOST ONLINE", "CREATE GAME" };
	char const *host_texts[] = { "CO-OP CAMPAIGN", "MULTIPLAYER" };
	char const *slots_text = "= SLOTS";
	/* OpenCE's gametypes' descriptions, in game_modes.c's order, as the Xbox's gametypes' */
	char const *gametype_descriptions[] =
	{
		"No shields, and a\r\nhead shot kills.\r\nStart with a Pistol\r\n"
			"and an AR; no\r\nGrenades and no\r\nMotion Tracker.",
		"Zombies with the\r\nSkull hunt the\r\nhumans. A human\r\n"
			"they kill joins\r\nthem. Survive 5\r\nminutes to win.",
		"Each kill gives you\r\nthe next weapon of\r\nthe list. A kill\r\n"
			"with the last wins.\r\nA melee kill sets\r\nthe victim back.",
		"Team Slayer with no\r\nMotion Tracker, no\r\naim assist and\r\n"
			"friendly fire on.\r\nA kill restores your\r\nhealth.",
		"CTF with no Motion\r\nTracker, no aim\r\nassist and friendly\r\n"
			"fire on. A kill\r\nrestores your health.",
		"Team Ball with no\r\nMotion Tracker, no\r\naim assist and\r\n"
			"friendly fire on.\r\nA kill restores your\r\nhealth.",
		"Team King with no\r\nMotion Tracker, no\r\naim assist and\r\n"
			"friendly fire on.\r\nA kill restores your\r\nhealth.",
	};
	long variant_descriptions = tag_loaded(UNICODE_STRING_LIST_TAG, "ui\\shell\\strings\\game_variant_descriptions");
	struct string_list *gametype_strings = NULL, *stock_variant_descriptions = NULL;
	struct string_list_entry *variant_entries = NULL;
	char const *descriptions_text[] =
	{
		"Host or join a game on the\r\nlocal network, with players\r\non PC, Android and PS3.",
		"Join a public game over the\r\ninternet, with players on PC,\r\nAndroid and the web.",
		"Host a co-op campaign or a\r\nmultiplayer game over the\r\ninternet: players join with\r\nits code.",
	};
	char const *host_descriptions[] =
	{
		"Play a campaign level with\r\nfriends over the internet.\r\nThey join with its code.",
		"Host a multiplayer game over\r\nthe internet. Players join\r\nwith its code or from the\r\nserver browser.",
		"Play a campaign level with\r\nfriends on the local\r\nnetwork.",
		"Host a multiplayer game on\r\nthe local network.",
	};
	long type_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_select_screen");
	long type_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_select_list");
	long system_link = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_type_conn_item");
	long options_text = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "multiplayer_options_txt");
	long descriptions = tag_loaded(UNICODE_STRING_LIST_TAG, XBOX_MULTIPLAYER "multiplayer_option_descriptions");
	long map_select = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_MULTIPLAYER "connected\\connected_map_select_wrapper");
	long server_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SERVER_LIST "server_list_screen");
	long server_list = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SERVER_LIST "server_list");
	long server_description = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SERVER_LIST "server_list_extended_desc");
	long looking = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SERVER_LIST "looking_for_servers_text");
	long create_key = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SERVER_LIST "create_game_key");
	long create_button = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_SERVER_LIST "create_game_button");
	long pregame_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_PREGAME "connected_pregame_screen");
	long choose_teams = tag_loaded(UI_WIDGET_DEFINITION_TAG, XBOX_PREGAME "choose_teams_text");
	struct ui_widget_definition *list, *screen, *options, *description, *status, *stock_key, *pregame;
	struct ui_widget_definition *lan_item, *lan_screen, *lan_list, *internet_item, *internet_screen, *internet_list;
	struct ui_widget_definition *code_button, *code_key, *invite_code, *host_item, *slots_key, *slots_button;
	struct ui_widget_child_reference *list_children, *description_children, *key_children, *pregame_children;
	struct ui_widget_child_reference *slots_children;
	struct ui_widget_event_handler_reference *host_handlers, *pregame_handlers;
	struct ui_widget_game_data_input_reference *invite_code_input;
	struct string_list const *stock_descriptions;
	struct string_list *description_strings;
	struct string_list_entry *entries;
	struct cache_file_tag_instance *instances;
	struct xbox_settings_stock settings_stock;
	struct xbox_host_stock host_stock;
	void *definitions[NUMBER_OF_XBOX_TAGS];
	long tags[NUMBER_OF_XBOX_TAGS];
	long index, type_description, blue_line = NONE, teams_child = NONE, key_bar_child = NONE, count;
	boolean settings;

	if (type_screen == NONE || type_list == NONE || system_link == NONE || options_text == NONE ||
		descriptions == NONE || map_select == NONE || server_screen == NONE || server_list == NONE ||
		server_description == NONE || looking == NONE || create_key == NONE || create_button == NONE ||
		pregame_screen == NONE || choose_teams == NONE)
	{
		platform_log("menus: ui.map has not the System Link screens: no LAN PLAY or INTERNET PLAY");
		return;
	}
	list = tag_get(UI_WIDGET_DEFINITION_TAG, type_list);
	screen = tag_get(UI_WIDGET_DEFINITION_TAG, type_screen);
	options = tag_get(UI_WIDGET_DEFINITION_TAG, options_text);
	stock_descriptions = tag_get(UNICODE_STRING_LIST_TAG, descriptions);
	description = tag_get(UI_WIDGET_DEFINITION_TAG, server_description);
	stock_key = tag_get(UI_WIDGET_DEFINITION_TAG, create_key);
	pregame = tag_get(UI_WIDGET_DEFINITION_TAG, pregame_screen);
	for (index = 0; index < screen->child_widgets.count; index++)
	{
		if (tag_name_ends(((struct ui_widget_child_reference *)screen->child_widgets.address)[index].widget_tag.index,
			"\\blueline"))
		{
			blue_line = index;
		}
	}
	for (index = 0; index < pregame->child_widgets.count; index++)
	{
		struct ui_widget_child_reference const *child =
			(struct ui_widget_child_reference const *)pregame->child_widgets.address + index;

		if (child->widget_tag.index == choose_teams)
			teams_child = index;
		if (tag_name_ends(child->widget_tag.index, "\\mp_button_key"))
			key_bar_child = index;
	}
	if (list->child_widgets.count != 4 || list->game_data_inputs.count != 1 || blue_line == NONE ||
		((struct ui_widget_child_reference *)list->child_widgets.address)[XBOX_SYSTEM_LINK_ITEM].widget_tag.index !=
			system_link ||
		options->text_label_string_list.index != descriptions || stock_descriptions->strings.count != 4 ||
		description->child_widgets.count != 5 ||
		((struct ui_widget_child_reference *)description->child_widgets.address)[XBOX_LOOKING_CHILD].widget_tag.index !=
			looking ||
		stock_key->child_widgets.count != 2 ||
		((struct ui_widget_child_reference *)stock_key->child_widgets.address)[XBOX_KEY_TEXT_CHILD].widget_tag.index !=
			create_button || teams_child == NONE || key_bar_child == NONE || !xbox_host_find(&host_stock))
	{
		platform_log("menus: ui.map's System Link screens are not the Xbox's: no LAN PLAY or INTERNET PLAY");
		return;
	}
	/* (before the multiplayer menu changes below: the Xbox's screens as they are) */
	settings = xbox_settings_find(&settings_stock);
	if (!settings)
		platform_log("menus: ui.map's settings screens are not the Xbox's: no EXTRA OPTIONS");
	count = settings ? NUMBER_OF_XBOX_TAGS : FIRST_XBOX_SETTINGS_TAG;
	memset(&build, 0, sizeof(build));
	instances = instances_grow(count, &build.first_index, &build.first_salt);
	if (!instances)
		goto failed;
	for (index = 0; index < count; index++)
		tags[index] = next_tag();
	/* the items, the game lists, INTERNET PLAY's description and message: copies of System Link's */
	definitions[_xbox_lan_text] = string_list_build(&lan_text, NULL, 1, NULL);
	definitions[_xbox_internet_text] = string_list_build(&internet_text, NULL, 1, NULL);
	definitions[_xbox_descriptions] = description_strings =
		string_list_build(descriptions_text, NULL, NUMBEROF(descriptions_text), NULL);
	definitions[_xbox_lan_item] = lan_item = xbox_widget_copy(system_link, "multiplayer_type_lan_item");
	definitions[_xbox_lan_screen] = lan_screen = xbox_widget_copy(server_screen, "lan_screen");
	definitions[_xbox_lan_list] = lan_list = xbox_widget_copy(server_list, "lan_list");
	definitions[_xbox_internet_item] = internet_item = xbox_widget_copy(system_link, "multiplayer_type_internet_item");
	definitions[_xbox_internet_screen] = internet_screen = xbox_widget_copy(server_screen, "internet_screen");
	definitions[_xbox_internet_list] = internet_list = xbox_widget_copy(server_list, "internet_list");
	definitions[_xbox_internet_description] = description =
		xbox_widget_copy(server_description, "internet_extended_desc");
	definitions[_xbox_internet_status] = status = xbox_widget_copy(looking, "internet_status_text");
	/* INTERNET PLAY's Y key (a copy of CREATE GAME's), and the pregame screen's invite code */
	definitions[_xbox_code_text] = string_list_build(&code_text, NULL, 1, NULL);
	definitions[_xbox_code_button] = code_button = xbox_widget_copy(create_button, "enter_code_button");
	definitions[_xbox_code_key] = code_key = xbox_widget_copy(create_key, "enter_code_key");
	definitions[_xbox_invite_code] = invite_code = xbox_widget_copy(choose_teams, "invite_code_text");
	/* HOST ONLINE's item (a copy of SYSTEM LINK PLAY's) and screens; the pregame screen's host key, Y = SLOTS (a copy
	of CREATE GAME's) */
	definitions[_xbox_host_text] = string_list_build(host_text, NULL, NUMBEROF(host_text), NULL);
	definitions[_xbox_host_texts] = string_list_build(host_texts, NULL, NUMBEROF(host_texts), NULL);
	definitions[_xbox_host_descriptions] = string_list_build(host_descriptions, NULL, NUMBEROF(host_descriptions),
		NULL);
	definitions[_xbox_host_item] = host_item = xbox_widget_copy(system_link, "multiplayer_type_host_item");
	definitions[_xbox_slots_text] = string_list_build(&slots_text, NULL, 1, NULL);
	definitions[_xbox_slots_key] = slots_key = xbox_widget_copy(create_key, "slots_key");
	definitions[_xbox_slots_button] = slots_button = xbox_widget_copy(create_button, "slots_button");
	xbox_host_define(&host_stock, definitions);
	if (settings)
		xbox_settings_define(&settings_stock, definitions);
	key_children = allocate(2 * sizeof(*key_children));
	slots_children = allocate(2 * sizeof(*slots_children));
	invite_code_input = allocate(sizeof(*invite_code_input));
	entries = allocate((stock_descriptions->strings.count + 2) * sizeof(*entries));
	list_children = allocate((list->child_widgets.count + 2) * sizeof(*list_children));
	pregame_children = allocate((pregame->child_widgets.count + 2) * sizeof(*pregame_children));
	host_handlers = allocate((xbox_widget(system_link)->event_handlers.count + 2) * sizeof(*host_handlers));
	pregame_handlers = allocate((pregame->event_handlers.count + 1) * sizeof(*pregame_handlers));
	/* (a default gametype's description is its index's + 10: OpenCE's, after the Xbox's 26, past the list's end) */
	if (variant_descriptions != NONE)
		stock_variant_descriptions = tag_get(UNICODE_STRING_LIST_TAG, variant_descriptions);
	if (stock_variant_descriptions && stock_variant_descriptions->strings.count == XBOX_VARIANT_DESCRIPTIONS &&
		game_modes_gametype_count() == (short)NUMBEROF(gametype_descriptions))
	{
		gametype_strings = string_list_build(gametype_descriptions, NULL, NUMBEROF(gametype_descriptions), NULL);
		variant_entries = allocate((XBOX_VARIANT_DESCRIPTIONS + NUMBEROF(gametype_descriptions)) *
			sizeof(*variant_entries));
	}
	if (build.failed)
		goto failed;
	for (index = 0; index < count; index++)
	{
		struct cache_file_tag_instance *instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tags[index])];

		instance->group_tag = index < FIRST_XBOX_WIDGET ||
			(index >= FIRST_XBOX_SETTINGS_TAG && index < FIRST_XBOX_SETTINGS_WIDGET) ?
			UNICODE_STRING_LIST_TAG : UI_WIDGET_DEFINITION_TAG;
		instance->parent_group_tags[0] = NONE;
		instance->parent_group_tags[1] = NONE;
		instance->tag_index = tags[index];
		instance->name = (char *)xbox_tag_names[index];
		instance->base_address = definitions[index];
	}
	cache_files_set_tag_instances(instances, build.first_index + build.next);
	/* the descriptions: the map's, LAN PLAY's in SYSTEM LINK PLAY's place, INTERNET PLAY's and HOST ONLINE's last */
	memcpy(entries, stock_descriptions->strings.address, stock_descriptions->strings.count * sizeof(*entries));
	entries[XBOX_SYSTEM_LINK_ITEM] = ((struct string_list_entry *)description_strings->strings.address)[0];
	entries[stock_descriptions->strings.count] = ((struct string_list_entry *)description_strings->strings.address)[1];
	entries[stock_descriptions->strings.count + 1] =
		((struct string_list_entry *)description_strings->strings.address)[2];
	description_strings->strings.count = stock_descriptions->strings.count + 2;
	description_strings->strings.address = entries;
	xbox_item_set(lan_item, tags[_xbox_lan_text], tags[_xbox_lan_screen]);
	xbox_item_set(internet_item, tags[_xbox_internet_text], tags[_xbox_internet_screen]);
	xbox_item_set(host_item, tags[_xbox_host_text], tags[_xbox_host_screen]);
	/* (HOST ONLINE's A and START first set its screen's mode, the internet; LAN PLAY's create sets the LAN) */
	for (index = 0; index < 2; index++)
	{
		host_handlers[index].flags = FLAG(_event_handler_run_function_bit);
		host_handlers[index].event_type = (short)name_index(index ? "start" : "a", event_names, NUMBEROF(event_names));
		host_handlers[index].function = (short)function_index("xbox host online", "xbox", 0);
		reference_clear(&host_handlers[index].widget_tag, UI_WIDGET_DEFINITION_TAG);
		reference_clear(&host_handlers[index].sound_effect, SOUND_TAG);
	}
	memcpy(host_handlers + 2, host_item->event_handlers.address,
		host_item->event_handlers.count * sizeof(*host_handlers));
	host_item->event_handlers.count += 2;
	host_item->event_handlers.address = host_handlers;
	xbox_host_wire(&host_stock, definitions, tags, map_select);
	xbox_screen_set(lan_screen, server_list, tags[_xbox_lan_list], NONE);
	xbox_screen_set(internet_screen, server_list, tags[_xbox_internet_list], tags[_xbox_code_key]);
	reference_set(&code_button->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_code_text]);
	code_button->string_list_index = 0;
	memcpy(key_children, code_key->child_widgets.address, 2 * sizeof(*key_children));
	reference_set(&key_children[XBOX_KEY_TEXT_CHILD].widget_tag, UI_WIDGET_DEFINITION_TAG, tags[_xbox_code_button]);
	code_key->child_widgets.address = key_children;
	/* (CREATE GAME's key has an X handler of its own) */
	code_key->event_handlers.count = 0;
	/* the invite code: a text box with no strings of its own, written by "xbox invite code" */
	invite_code->bounds.x1 = invite_code->bounds.x0 + XBOX_INVITE_CODE_WIDTH;
	invite_code->justification = (short)name_index("right", align_names, NUMBEROF(align_names));
	reference_clear(&invite_code->text_label_string_list, UNICODE_STRING_LIST_TAG);
	invite_code->event_handlers.count = 0;
	invite_code_input->function = (short)game_data_input_index("xbox invite code", "xbox", 0);
	invite_code->game_data_inputs.count = 1;
	invite_code->game_data_inputs.address = invite_code_input;
	/* Y = SLOTS: the button, then its text, shown to the host only ("xbox slots key") */
	memcpy(slots_children, slots_key->child_widgets.address, 2 * sizeof(*slots_children));
	reference_set(&slots_children[XBOX_KEY_TEXT_CHILD].widget_tag, UI_WIDGET_DEFINITION_TAG, tags[_xbox_slots_button]);
	slots_children[XBOX_KEY_TEXT_CHILD].horizontal_offset = XBOX_SLOTS_TEXT_LEFT;
	slots_key->child_widgets.address = slots_children;
	slots_key->event_handlers.count = 0;
	xbox_input_set(slots_key, "xbox slots key");
	reference_set(&slots_button->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_slots_text]);
	slots_button->string_list_index = 0;
	slots_button->justification = (short)name_index("left", align_names, NUMBEROF(align_names));
	slots_button->bounds.x1 = slots_button->bounds.x0 + XBOX_SLOTS_TEXT_WIDTH;
	/* (LAN PLAY's create opens HOST ONLINE's screen, in the LAN's mode) */
	xbox_list_set(lan_list, "xbox lan update", xbox_lan_events, NUMBEROF(xbox_lan_events), tags[_xbox_host_screen]);
	xbox_list_set(internet_list, "xbox internet update", xbox_internet_events, NUMBEROF(xbox_internet_events),
		map_select);
	reference_set(&internet_list->extended_description_widget, UI_WIDGET_DEFINITION_TAG,
		tags[_xbox_internet_description]);
	/* INTERNET PLAY's description: System Link's, its message a text box the list's update writes */
	description_children = allocate(description->child_widgets.count * sizeof(*description_children));
	if (description_children)
	{
		memcpy(description_children, description->child_widgets.address,
			description->child_widgets.count * sizeof(*description_children));
		reference_set(&description_children[XBOX_LOOKING_CHILD].widget_tag, UI_WIDGET_DEFINITION_TAG,
			tags[_xbox_internet_status]);
		description->child_widgets.address = description_children;
	}
	reference_clear(&status->text_label_string_list, UNICODE_STRING_LIST_TAG);
	type_description = game_data_input_index("xbox mp type desc", "xbox", 0);
	if (settings)
		xbox_settings_wire(&settings_stock, definitions, tags);
	if (build.failed)
		goto failed;
	/* the map's own last, as they cannot be undone: LAN PLAY for SYSTEM LINK PLAY, INTERNET PLAY and HOST ONLINE
	after it */
	memcpy(list_children, list->child_widgets.address, list->child_widgets.count * sizeof(*list_children));
	list_children[XBOX_SYSTEM_LINK_ITEM + 3] = list_children[XBOX_SYSTEM_LINK_ITEM + 1];
	list_children[XBOX_SYSTEM_LINK_ITEM + 3].vertical_offset += 2 * XBOX_ITEM_SPACING;
	list_children[XBOX_SYSTEM_LINK_ITEM + 1] = list_children[XBOX_SYSTEM_LINK_ITEM];
	list_children[XBOX_SYSTEM_LINK_ITEM + 1].vertical_offset += XBOX_ITEM_SPACING;
	list_children[XBOX_SYSTEM_LINK_ITEM + 2] = list_children[XBOX_SYSTEM_LINK_ITEM];
	list_children[XBOX_SYSTEM_LINK_ITEM + 2].vertical_offset += 2 * XBOX_ITEM_SPACING;
	xbox_child_set(&list_children[XBOX_SYSTEM_LINK_ITEM], tags[_xbox_lan_item], lan_item->name);
	xbox_child_set(&list_children[XBOX_SYSTEM_LINK_ITEM + 1], tags[_xbox_internet_item], internet_item->name);
	xbox_child_set(&list_children[XBOX_SYSTEM_LINK_ITEM + 2], tags[_xbox_host_item], host_item->name);
	list->child_widgets.count = list->child_widgets.count + 2;
	list->child_widgets.address = list_children;
	((struct ui_widget_game_data_input_reference *)list->game_data_inputs.address)->function = (short)type_description;
	((struct ui_widget_child_reference *)screen->child_widgets.address)[blue_line].vertical_offset +=
		2 * XBOX_ITEM_SPACING;
	reference_set(&options->text_label_string_list, UNICODE_STRING_LIST_TAG, tags[_xbox_descriptions]);
	/* the pregame screen's invite code: a child as "Choose teams using D-pad" is, on its bar's right */
	memcpy(pregame_children, pregame->child_widgets.address, pregame->child_widgets.count * sizeof(*pregame_children));
	pregame_children[pregame->child_widgets.count] = pregame_children[teams_child];
	xbox_child_set(&pregame_children[pregame->child_widgets.count], tags[_xbox_invite_code], invite_code->name);
	pregame_children[pregame->child_widgets.count].vertical_offset = XBOX_INVITE_CODE_TOP;
	pregame_children[pregame->child_widgets.count].horizontal_offset = XBOX_INVITE_CODE_LEFT;
	/* and the host's Y key, on the key bar's line, left of it; Y runs "xbox host slots" */
	pregame_children[pregame->child_widgets.count + 1] = pregame_children[key_bar_child];
	xbox_child_set(&pregame_children[pregame->child_widgets.count + 1], tags[_xbox_slots_key], slots_key->name);
	pregame_children[pregame->child_widgets.count + 1].horizontal_offset = XBOX_SLOTS_KEY_LEFT;
	pregame->child_widgets.count = pregame->child_widgets.count + 2;
	pregame->child_widgets.address = pregame_children;
	memcpy(pregame_handlers, pregame->event_handlers.address,
		pregame->event_handlers.count * sizeof(*pregame_handlers));
	{
		struct ui_widget_event_handler_reference *slots_handler = &pregame_handlers[pregame->event_handlers.count];

		slots_handler->flags = FLAG(_event_handler_run_function_bit);
		slots_handler->event_type = (short)name_index("y", event_names, NUMBEROF(event_names));
		slots_handler->function = (short)function_index("xbox host slots", "xbox", 0);
		reference_clear(&slots_handler->widget_tag, UI_WIDGET_DEFINITION_TAG);
		reference_clear(&slots_handler->sound_effect, SOUND_TAG);
	}
	pregame->event_handlers.count = pregame->event_handlers.count + 1;
	pregame->event_handlers.address = pregame_handlers;
	menu_tags.xbox_pregame = pregame_screen;
	/* the gametypes' descriptions: the Xbox's, then OpenCE's */
	if (variant_entries)
	{
		memcpy(variant_entries, stock_variant_descriptions->strings.address,
			XBOX_VARIANT_DESCRIPTIONS * sizeof(*variant_entries));
		memcpy(variant_entries + XBOX_VARIANT_DESCRIPTIONS, gametype_strings->strings.address,
			NUMBEROF(gametype_descriptions) * sizeof(*variant_entries));
		stock_variant_descriptions->strings.count = XBOX_VARIANT_DESCRIPTIONS + NUMBEROF(gametype_descriptions);
		stock_variant_descriptions->strings.address = variant_entries;
	}
	if (settings)
		xbox_settings_open(&settings_stock, tags[_xbox_settings_screen]);
	menu_tags.xbox_patched = TRUE;
	platform_log("menus: the multiplayer menu has LAN PLAY, INTERNET PLAY and HOST ONLINE%s",
		settings && !build.failed ? "; SETTINGS has EXTRA OPTIONS" : "");
	memset(&build, 0, sizeof(build));
	return;

failed:
	platform_log("menus: LAN PLAY, INTERNET PLAY and HOST ONLINE not added");
	menu_tags_release();
	memset(&build, 0, sizeof(build));
}
#endif

/* whether the PC menus' text is the language of the maps' Custom Edition
(display.menu_language), else English */
static boolean menus_language_maps(void)
{
	static boolean said;
	char const *value = config_string("display.menu_language");

	if (!csstrcasecmp(value, "maps"))
		return TRUE;
	if (csstrcasecmp(value, "english") && !said)
	{
		platform_log("menus: display.menu_language \"%s\" is not \"maps\" or \"english\": English", value);
		said = TRUE;
	}
	return FALSE;
}

void menu_tags_loaded(
	char const *map_name)
{
	struct halo_menus const *menus;
	struct cache_file_tag_instance *instances;
	long widget_count, own_lists = 0, total, index;
	boolean locale;

	boolean game_map = strcmp(map_name, "ui") != 0;
	boolean campaign = game_map && full_screen_campaign_map();
	boolean coop = game_map && network_coop_campaign_map();

#ifdef HALO_PS3
	/* (the Xbox's menus, which the PS3 keeps: port_stubs.c) */
	if (!game_map && (!menus_pc_chosen() || !halo_menus_load()))
	{
		xbox_multiplayer_patch();
		return;
	}
#endif
	/* (ui.map, and a multiplayer map: its pause menu's SETTINGS; a
	campaign's too, single player's or network co-op's, which also gets
	FRIENDLY FIRE) */
	if ((game_map && tag_loaded('Soul', MULTIPLAYER_COLLECTION) == NONE && !campaign) ||
		!menus_pc_chosen())
	{
		return;
	}
	menus = halo_menus_load();
	if (!menus)
		return;
	memset(&build, 0, sizeof(build));
	build.menus = menus;
	build.game_map = game_map;
	/* Settings must not pause the simulation in a network campaign. */
	build.campaign = campaign && game_connection() == _game_connection_local &&
		!network_game_is_active() && !game_is_cooperative();
	build.coop = coop;
	widget_count = menus->widget_count;
	build.widget_tags = malloc((widget_count + 1) * sizeof(long));
	build.text_tags = malloc((widget_count + 1) * sizeof(long));
	build.spinner_tags = malloc((widget_count + 1) * sizeof(long));
	build.bitmap_tags = malloc((menus->bitmap_count + 1) * sizeof(long));
	build.strings_tags = malloc((menus->string_list_count + 1) * sizeof(long));
	if (!build.widget_tags || !build.text_tags || !build.spinner_tags || !build.bitmap_tags || !build.strings_tags)
		goto failed;
	for (index = 0; index < widget_count; index++)
	{
		own_lists += (menus->widgets[index].text != NULL) + (menus->widgets[index].strings != NULL);
		/* (names are unique) */
		if (widget_named(menus->widgets[index].name) != index)
			problem(menus->widgets[index].file, menus->widgets[index].line, "two widgets are named",
				menus->widgets[index].name);
	}
	total = widget_count + own_lists + menus->string_list_count + menus->bitmap_count +
		(game_map ? PAUSE_PATCH_TAGS : 0);
	instances = build.failed ? NULL : instances_grow(total, &build.first_index, &build.first_salt);
	if (!instances)
		goto failed;
	/* each new tag's index */
	for (index = 0; index < widget_count; index++)
	{
		build.widget_tags[index] = next_tag();
		build.text_tags[index] = menus->widgets[index].text ? next_tag() : NONE;
		build.spinner_tags[index] = menus->widgets[index].strings ? next_tag() : NONE;
	}
	for (index = 0; index < menus->string_list_count; index++)
		build.strings_tags[index] = next_tag();
	for (index = 0; index < menus->bitmap_count; index++)
		build.bitmap_tags[index] = next_tag();
	/* the tags: the bitmaps and strings first, which the widgets name; the
	widgets' names in the table before any is built, which they find there */
	for (index = 0; index < menus->bitmap_count && !build.failed; index++)
	{
		instance_set(instances, BITMAP_GROUP_TAG, build.bitmap_tags[index], menus->bitmaps[index].name, "",
			bitmap_build(&menus->bitmaps[index], build.bitmap_tags[index]));
	}
	/* (the PC version's strings in the language of the Custom Edition the
	maps come from, its loc.map's, unless English is chosen) */
	locale = menus_language_maps() && custom_edition_locale_open();
	for (index = 0; index < menus->string_list_count && !build.failed; index++)
	{
		struct halo_menu_strings const *list = &menus->string_lists[index];
		struct custom_edition_string_list *map = locale && list->map ? custom_edition_locale_strings(list->map) : NULL;

		instance_set(instances, UNICODE_STRING_LIST_TAG, build.strings_tags[index], list->name, "",
			string_list_build(menus->strings + list->first, menus->string_maps + list->first, list->count, map));
		if (map)
			free(map);
	}
	if (locale)
	{
		custom_edition_locale_close();
		if (!game_map)
			platform_log("menus: %ld strings from the Custom Edition's loc.map", build.map_strings);
	}
	for (index = 0; index < widget_count && !build.failed; index++)
	{
		struct halo_menu_widget const *widget = &menus->widgets[index];

		if (widget->text)
		{
			instance_set(instances, UNICODE_STRING_LIST_TAG, build.text_tags[index], widget->name, " text",
				string_list_build(&widget->text, NULL, 1, NULL));
		}
		if (widget->strings)
		{
			char const *pieces[MAXIMUM_STRINGS];
			long count = spinner_split(widget, FALSE, pieces);

			instance_set(instances, UNICODE_STRING_LIST_TAG, build.spinner_tags[index], widget->name, " strings",
				string_list_build(pieces, NULL, count, NULL));
		}
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, build.widget_tags[index], widget->name, "", NULL);
	}
	if (build.failed)
		goto failed;
	/* (pause_patch's, until it makes them: nothing a name finds) */
	for (index = build.first_index + build.next; index < build.first_index + total; index++)
	{
		instances[index].group_tag = NONE;
		instances[index].tag_index = NONE;
		instances[index].name = "";
	}
	cache_files_set_tag_instances(instances, build.first_index + total);
	for (index = 0; index < widget_count && !build.failed; index++)
		instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[index])].base_address = widget_build(index);
	if (build.failed)
		goto failed;
	if (game_map)
	{
		pause_patch(instances);
		if (campaign)
			pause_campaign_patch(instances);
		pause_network_patch();
		if (build.failed)
			goto failed;
		/* (those it made) */
		cache_files_set_tag_instances(instances, build.first_index + build.next);
	}
	else if (widget_named(menus->root) != NONE)
	{
		snprintf(menu_tags.root, sizeof(menu_tags.root), "%s%s", PC_MENU_TAG_PREFIX, menus->root);
		for (index = 0; menu_tags.root[index]; index++)
		{
			if (menu_tags.root[index] == '/')
				menu_tags.root[index] = '\\';
		}
	}
	else
	{
		platform_log("menus: there is no widget named %s, the main menu", menus->root);
	}
	menu_tags.loaded = TRUE;
	platform_log("menus: %ld widgets, %ld string lists and %ld bitmaps added to the map's %ld tags",
		widget_count, own_lists + menus->string_list_count, menus->bitmap_count, build.first_index);
	goto done;

failed:
	platform_log("menus: not added; using the game's own menus");
	menu_tags_release();

done:
	free(build.widget_tags);
	free(build.text_tags);
	free(build.spinner_tags);
	free(build.strings_tags);
	free(build.bitmap_tags);
	memset(&build, 0, sizeof(build));
}

void menu_tags_unloaded(
	void)
{
	if (menu_tags.loaded || menu_tags.block_count)
		menu_tags_release();
}

char const *pc_menus_root_name(
	void)
{
	/* (debug.menu_open: a screen to see, a profile being edited for the
	settings' screens) */
	char const *open = config_string("debug.menu_open");

	if (menu_tags.loaded && *open && build.menus == NULL)
	{
		static char name[300];
		long index;

		snprintf(name, sizeof(name), "%s%s", PC_MENU_TAG_PREFIX, open);
		for (index = 0; name[index]; index++)
		{
			if (name[index] == '/')
				name[index] = '\\';
		}
		if (tag_loaded(UI_WIDGET_DEFINITION_TAG, name) != NONE)
		{
			extern boolean pc_menu_profile_edit_begin(void);

			pc_menu_profile_edit_begin();
			return name;
		}
		platform_log("menus: debug.menu_open: there is no widget named %s", open);
	}
	return menu_tags.loaded && menu_tags.root[0] ? menu_tags.root : "ui\\shell\\main_menu\\main_menu";
}

boolean pc_menu_tag(
	long tag_index)
{
	return menu_tags.loaded && tag_index != NONE &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) >= menu_tags.original_count;
}

#ifdef HALO_PS3
/* a tag of the Xbox menus' LAN PLAY or INTERNET PLAY, or the pregame screen, whose functions are the port's
(ui_widget_function_denied) */
boolean pc_menu_xbox_patch_tag(
	long tag_index)
{
	return menu_tags.xbox_patched && tag_index != NONE &&
		(DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) >= menu_tags.original_count || tag_index == menu_tags.xbox_pregame);
}
#endif

/* where in its widget, and how large, a frame of ui.map's that a menu file scales is drawn (<frame map=... width=
height= x= y=>); FALSE for any other bitmap, drawn as it is (ui_widget.c) */
boolean pc_menu_frame_placement(
	struct bitmap_data const *bitmap,
	short *x,
	short *y,
	short *width,
	short *height)
{
	long index;

	for (index = 0; index < menu_tags.placement_count; index++)
	{
		if (menu_tags.placements[index].bitmap == bitmap)
		{
			*x = menu_tags.placements[index].x;
			*y = menu_tags.placements[index].y;
			*width = menu_tags.placements[index].width;
			*height = menu_tags.placements[index].height;
			return TRUE;
		}
	}
	return FALSE;
}

char const *pc_menu_function_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_function_names) ?
		port_function_names[function_index] : NULL;
}

/* a widget definition's string_index (port settings help: its label's) */
short pc_menu_string_index(
	long definition_index)
{
	struct ui_widget_definition *definition = tag_get(UI_WIDGET_DEFINITION_TAG, definition_index);

	return definition ? definition->string_list_index : NONE;
}

/* the screen the engine opens by name after a network game (the Xbox's), or ours where the PC version's menus are up:
the host's map select, the lobby */
char const *pc_menus_screen(
	char const *name)
{
	static struct
	{
		char const *game_name;
		char const *menu_name;
	} const screens[] =
	{
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_postgame_wrapper",
			"pc\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_wrapper" },
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
			"pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen" },
	};
	short index;

	if (!menu_tags.loaded)
		return name;
	for (index = 0; index < NUMBEROF(screens); index++)
	{
		if (!strcmp(name, screens[index].game_name))
			return screens[index].menu_name;
	}
	return name;
}

char const *pc_menu_game_data_input_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_game_data_input_names) ?
		port_game_data_input_names[function_index] : NULL;
}

struct pc_menu_setting *pc_menu_setting_get(
	long definition_index)
{
	long index;

	for (index = 0; index < menu_tags.setting_count; index++)
	{
		if (menu_tags.settings[index].definition_index == definition_index)
			return &menu_tags.settings[index];
	}
	return NULL;
}
