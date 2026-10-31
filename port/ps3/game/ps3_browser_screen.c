/* The Multiplayer menu's ONLINE GAMES on the PS3 (HALO_CONSOLE_ONLINE_GAMES): the console game list
(src/ps3_online.c) on a screen of code, the Wii U's wiiu_browser_screen.c (warthog-wiiu) without Link Profile. */
/* Up and down pick, left and right page, A joins through the game's invite (the tunnel, p2p.c), X refreshes, Y
filters, B goes back. */

#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "interface/player_ui.h"
#include "networking/network_game_globals.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "bitmaps/bitmap_group.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"

#include <stdio.h>
#include <string.h>

#include "ps3_game_list.h"

/* (ui_widget.c's, ui_widget_event_handler_functions.c's, network_client_manager.c's) */
long ui_widget_online_games_font(boolean heading);
boolean ui_online_games_start_network(void);
long network_game_client_join_invite_host(char const *invite);
boolean create_global_network_game_client(void);
void game_connection_set(short connection);
/* (port/linux/src: internet play's tunnel to an invite's host, the lists' filter, the settings) */
int p2p_join_invite(char const *text);
int browser_list_filter(void);
void browser_list_filter_cycle(void);
int config_boolean(char const *name);

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	_browser_event_left_stick = 1,
	_browser_event_button = 3,

	/* (ui_widget.c's private enums) */
	_justify_left = 0,
	_justify_right,
	_justify_center,
	_browser_sound_cursor = 1,
	_browser_sound_forward = 2,
	_browser_sound_back = 3,

	/* no A this soon after the screen opens (the menu's A) */
	OPEN_SETTLE = 600,
	STATUS_DURATION = 5000,
	/* how long the tunnel and the host's advertisement may take */
	CONNECT_TIMEOUT = 45000,
	/* a held direction: the first repeat after this long, then this often; a longer gap is a new press */
	STEP_FIRST_REPEAT_DELAY = 350,
	STEP_REPEAT_INTERVAL = 110,
	STEP_RELEASE_GAP = 150,

	/* the 640x480 layout, inside the title-safe area (48 to 592 across, 36 to 444 down) */
	SCREEN_LEFT = 48,
	SCREEN_RIGHT = 592,
	TITLE_TOP = 38,
	RULE_Y = 72,
	HEAD_TOP = 76,
	LIST_TOP = 96,
	LIST_BOTTOM = 284,
	MAXIMUM_ROWS = 9,
	DETAIL_TOP = 308,
	DETAIL_BOTTOM = 412,
	PROMPT_TOP = 420,
	PROMPT_BOTTOM = 444,

	/* a cell's inset, the space between columns, and the Map and Type columns' widths */
	CELL_INSET = 6,
	COLUMN_GAP = 12,
	COLUMN_MAP_WIDTH = 124,
	COLUMN_TYPE_WIDTH = 96,

	PICTURE_LEFT = SCREEN_LEFT + 8,
	PICTURE_RIGHT = PICTURE_LEFT + 120,
	DETAIL_TEXT = PICTURE_RIGHT + 14,
};

/* the screen's colors (pixel32, 0xAARRGGBB); the background is opaque, as a screen the menus open */
#define COLOR_BACKGROUND 0xFF050B18
#define COLOR_RULE 0xFF2A62C8
#define COLOR_PANEL 0xC0081530
#define COLOR_HEADER 0xFF0C1E40
#define COLOR_ROW_SELECTED 0xFF2052B0
#define COLOR_PICTURE 0xFF0A1A33

/* the multiplayer maps' pictures (the menus' mp_map_grafix, in its order) */
static char const *const map_picture_order[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
};

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long opened_time;
	/* the list taken (ps3_game_list_get's count of lists), in the screen's order for the filter */
	int lists;
	int count;
	int filter;
	unsigned char order[GAME_LIST_MAXIMUM_GAMES];
	short selected;
	/* the rows a page, as the menu's font fits them */
	short rows;
	/* the selected game's invite, kept selected when the list comes again */
	char selected_invite[GAME_LIST_INVITE_LENGTH + 1];
	char status[64];
	unsigned long status_time;
	/* a join under way: the invite and when it started */
	boolean connecting;
	char connecting_invite[GAME_LIST_INVITE_LENGTH + 1];
	unsigned long connecting_time;
} browser_screen;

/* ---------- private code */

static void set_status(
	char const *status)
{
	csstrncpy(browser_screen.status, status, sizeof(browser_screen.status) - 1);
	browser_screen.status[sizeof(browser_screen.status) - 1] = 0;
	browser_screen.status_time = system_milliseconds();
}

static struct game_list_game const *selected_game(
	void)
{
	struct game_list const *list = ps3_game_list_get(NULL);

	if (!list || browser_screen.selected < 0 || browser_screen.selected >= browser_screen.count)
		return NULL;
	return &list->games[browser_screen.order[browser_screen.selected]];
}

/* a list that came since the last look, put in the screen's order; the selected game kept if still listed */
static void take_list(
	void)
{
	int lists;
	struct game_list const *list = ps3_game_list_get(&lists);
	short index;

	if (!list || (lists == browser_screen.lists && browser_list_filter() == browser_screen.filter))
		return;
	browser_screen.lists = lists;
	browser_screen.filter = browser_list_filter();
	browser_screen.count = game_list_order(list, browser_screen.order, browser_screen.filter);
	for (index = 0; index < browser_screen.count; index++)
	{
		if (!strcmp(list->games[browser_screen.order[index]].invite, browser_screen.selected_invite))
			break;
	}
	browser_screen.selected = index < browser_screen.count ? index : 0;
}

static void remember_selected(
	void)
{
	struct game_list_game const *game = selected_game();

	csstrncpy(browser_screen.selected_invite, game ? game->invite : "", GAME_LIST_INVITE_LENGTH);
	browser_screen.selected_invite[GAME_LIST_INVITE_LENGTH] = 0;
}

static void refresh(
	void)
{
	if (ps3_game_list_state() == PS3_GAME_LIST_FETCHING)
		return;
	ps3_game_list_request();
}

/* the first player in the game to be joined, with the profile System Link's Start would pick: the last used,
else the first saved */
static void join_first_player(
	void)
{
	long profile_index = player_ui_get_player1_last_used_profile_index();
	struct player_profile profile;

	player_ui_local_player_joined_multiplayer_game(0);
	if (profile_index == NONE || !TEST_FLAG(profile_index, _saved_game_file_index_valid_bit))
	{
		long profile_indices[100];
		word profile_count = NUMBEROF(profile_indices);

		player_profiles_enumerate_available_to_local_player_index(0, &profile_count, profile_indices, FALSE);
		profile_index = profile_count ? profile_indices[0] : NONE;
	}
	if (profile_index != NONE && TEST_FLAG(profile_index, _saved_game_file_index_valid_bit) &&
		player_profile_get(profile_index, &profile))
	{
		player_ui_set_active_player_profile(0, profile_index, &profile);
	}
}

/* an invite joined: the tunnel to its host, then its game once advertised (wait_for_host); the log has only the
invite's first digits */
static void join_invite(
	char const *invite,
	char const *name)
{
	char link[16 + GAME_LIST_INVITE_LENGTH];

	if (!config_boolean("network.online"))
	{
		set_status("Internet play is off (network.online).");
		error(_error_log, "online games: no internet play with network.online off in config.toml");
		return;
	}
	if (!global_network_game_client_get())
	{
		if (!create_global_network_game_client())
		{
			set_status("Could not start the network.");
			return;
		}
		game_connection_set(_game_connection_network_client);
	}
	join_first_player();
	snprintf(link, sizeof(link), "halo://join/%s", invite);
	if (!p2p_join_invite(link))
	{
		set_status("Internet play is off.");
		return;
	}
	browser_screen.connecting = TRUE;
	csstrncpy(browser_screen.connecting_invite, invite, GAME_LIST_INVITE_LENGTH);
	browser_screen.connecting_invite[GAME_LIST_INVITE_LENGTH] = 0;
	browser_screen.connecting_time = system_milliseconds();
	set_status("Connecting...");
	error(_error_log, "online games: joining %s (invite %.8s...)", name, invite);
}

/* the picked game's host: its game joined once advertised, and its lobby opened */
static void wait_for_host(
	void)
{
	long joined = network_game_client_join_invite_host(browser_screen.connecting_invite);

	if (joined > 0)
	{
		error(_error_log, "online games: the host's game was advertised through the tunnel; joining it");
		browser_screen.connecting = FALSE;
		browser_screen.active = FALSE;
		ui_widgets_close_all();
		ui_widget_load_by_name_or_tag(
			"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
			NONE, NULL, NONE, NONE, NONE, NONE);
	}
	else if (joined == -2)
	{
		browser_screen.connecting = FALSE;
		set_status("Could not connect to the host.");
		error(_error_log, "online games: the connection to the host's game failed");
	}
	else if (joined < 0)
	{
		browser_screen.connecting = FALSE;
		set_status("That game can't be joined from this version.");
		error(_error_log, "online games: the host's game is not one this version joins");
	}
	else if (system_milliseconds() - browser_screen.connecting_time > CONNECT_TIMEOUT)
	{
		browser_screen.connecting = FALSE;
		set_status("The host did not answer.");
		error(_error_log, "online games: the host's game was not advertised within %d seconds",
			(int)(CONNECT_TIMEOUT / 1000));
	}
}

/* ---------- public code */

boolean browser_screen_active(
	void)
{
	return browser_screen.active;
}

void browser_screen_open(
	void)
{
	browser_screen.active = TRUE;
	browser_screen.opened_time = system_milliseconds();
	browser_screen.status[0] = 0;
	if (!browser_screen.rows)
		browser_screen.rows = MAXIMUM_ROWS;
	/* (the list as it came last; a fresh one asked for) */
	browser_screen.lists = 0;
	browser_screen.connecting = FALSE;
	take_list();
	refresh();
	/* the network searching, as System Link's list starts it: the joined host's advertisement comes through the
	tunnel */
	if (!ui_online_games_start_network())
		set_status("Could not start the network.");
	error(_error_log, "online games: opened, %d games listed", browser_screen.count);
	/* (the menu's A, still queued, is not a pick) */
	event_manager_flush();
}

/* a step held down: the first press moves at once; held, it waits, then repeats at the menus' pace. The move to
make now, or 0 */
static short held_move(
	short move)
{
	static unsigned long last_event_time, last_step_time;
	static short last_move;
	static boolean repeating;
	unsigned long now = system_milliseconds();
	short result = 0;

	if (!move)
		return 0;
	if (move != last_move || now - last_event_time > STEP_RELEASE_GAP)
	{
		result = move;
		last_step_time = now;
		repeating = FALSE;
	}
	else if (now - last_step_time >= (unsigned long)(repeating ? STEP_REPEAT_INTERVAL : STEP_FIRST_REPEAT_DELAY))
	{
		result = move;
		last_step_time = now;
		repeating = TRUE;
	}
	last_move = move;
	last_event_time = now;
	return result;
}

void browser_screen_process(
	void)
{
	struct event_record event;
	short move = 0;

	take_list();
	while (browser_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == _browser_event_left_stick)
		{
			if (event.data.stick.y == SHORT_MAX)
				move = -1;
			else if (event.data.stick.y == SHORT_MIN)
				move = 1;
			else if (event.data.stick.x == SHORT_MIN)
				move = -browser_screen.rows;
			else if (event.data.stick.x == SHORT_MAX)
				move = browser_screen.rows;
		}
		else if (event.type == _browser_event_button)
		{
			switch (event.data.button.index)
			{
			case _gamepad_binary_button_dpad_up: move = -1; break;
			case _gamepad_binary_button_dpad_down: move = 1; break;
			case _gamepad_binary_button_dpad_left: move = -browser_screen.rows; break;
			case _gamepad_binary_button_dpad_right: move = browser_screen.rows; break;
			case _gamepad_analog_button_a:
				if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE &&
					selected_game())
				{
					struct game_list_game const *game = selected_game();

					ui_play_audio_feedback_sound(_browser_sound_forward);
					if (strcmp(game->state, "open"))
						set_status("That game is not accepting players.");
					else
						join_invite(game->invite, game->name);
				}
				break;
			case _gamepad_analog_button_y:
				/* (the filter: all games, multiplayer or co-op) */
				ui_play_audio_feedback_sound(_browser_sound_forward);
				browser_list_filter_cycle();
				browser_screen.selected = 0;
				browser_screen.selected_invite[0] = 0;
				take_list();
				break;
			case _gamepad_analog_button_x:
				ui_play_audio_feedback_sound(_browser_sound_forward);
				error(_error_log, "online games: refresh");
				refresh();
				break;
			case _gamepad_analog_button_b:
				ui_play_audio_feedback_sound(_browser_sound_back);
				/* (B stops a join under way; again, goes back) */
				if (browser_screen.connecting)
				{
					browser_screen.connecting = FALSE;
					set_status("Stopped.");
				}
				else
					browser_screen.active = FALSE;
				break;
			default: break;
			}
		}
		move = held_move(move);
		if (move)
		{
			short selected = (short)PIN(browser_screen.selected + move, 0, MAX(0, browser_screen.count - 1));

			if (selected != browser_screen.selected)
			{
				ui_play_audio_feedback_sound(_browser_sound_cursor);
				browser_screen.selected = selected;
				browser_screen.status[0] = 0;
			}
			move = 0;
		}
	}
	remember_selected();
	if (browser_screen.active && browser_screen.connecting)
		wait_for_host();
	/* (the widgets behind take nothing while the screen is up) */
	event_manager_flush();
}

/* ---------- drawing */

static void set_color(
	real_argb_color *color,
	real alpha,
	real red,
	real green,
	real blue)
{
	color->alpha = alpha;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

static void quad(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color)
{
	rectangle2d bounds;

	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, color);
}

/* ASCII text in a box (clipped to it), in a font and color */
static void text(
	long font,
	short justification,
	real_argb_color const *color,
	short x0,
	short y0,
	short x1,
	short y1,
	char const *string)
{
	wchar_t wide[96];
	rectangle2d bounds;
	short index;

	if (font == NONE || !string)
		return;
	for (index = 0; index < NUMBEROF(wide) - 1 && string[index]; index++)
		wide[index] = (wchar_t)(unsigned char)string[index];
	wide[index] = 0;
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_string_set_draw_mode(font, NONE, justification, 0, color);
	rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, wide);
}

/* a font's line, in pixels */
static short line_height(
	long font)
{
	struct font_header *header = font != NONE ? font_definition_get(font) : NULL;

	return header ? (short)(header->ascending_height + header->descending_height) : 16;
}

/* text's width in a font, in pixels */
static short text_width(
	long font,
	char const *string)
{
	wchar_t wide[96];
	rectangle2d bounds, text_bounds, cursor_bounds;
	real_argb_color color;
	short index;

	if (font == NONE || !string)
		return 0;
	for (index = 0; index < NUMBEROF(wide) - 1 && string[index]; index++)
		wide[index] = (wchar_t)(unsigned char)string[index];
	wide[index] = 0;
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 640;
	bounds.y1 = 480;
	set_color(&color, 1.0f, 1.0f, 1.0f, 1.0f);
	draw_string_set_draw_mode(font, NONE, _justify_left, 0, &color);
	draw_unicode_string_compute_bounds(&bounds, wide, &text_bounds, &cursor_bounds);
	return (short)(text_bounds.x1 - text_bounds.x0);
}

/* the map's picture (the menus' own), or the box alone for a map with none */
static void draw_map_picture(
	char const *map,
	short y0,
	short y1)
{
	long bitmap_index = tag_loaded('bitm', "ui\\shell\\bitmaps\\mp_map_grafix");
	char file[GAME_LIST_MAP_LENGTH + 1];
	unsigned long start = 0, index;
	short frame = NONE;

	quad(PICTURE_LEFT, y0, PICTURE_RIGHT, y1, COLOR_PICTURE);
	/* (the scenario's name, after the path) */
	for (index = 0; map[index]; index++)
	{
		if (map[index] == '\\' || map[index] == '/')
			start = index + 1;
	}
	csstrncpy(file, map + start, sizeof(file) - 1);
	file[sizeof(file) - 1] = 0;
	for (index = 0; index < NUMBEROF(map_picture_order); index++)
	{
		if (!_stricmp(file, map_picture_order[index]))
			frame = (short)index;
	}
	if (bitmap_index != NONE && frame != NONE)
	{
		struct bitmap_data *bitmap = bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame);

		if (bitmap)
		{
			/* (the picture fills the bitmap's top left, 140 by 116) */
			rectangle2d bounds, art;

			bounds.x0 = PICTURE_LEFT;
			bounds.y0 = y0;
			bounds.x1 = PICTURE_RIGHT;
			bounds.y1 = y1;
			art.x0 = 0;
			art.y0 = 0;
			art.x1 = (short)MIN(140, bitmap->width);
			art.y1 = (short)MIN(116, bitmap->height);
			draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
		}
	}
}

static void draw_details(
	struct game_list_game const *game,
	long font,
	real_argb_color const *label_color,
	real_argb_color const *text_color)
{
	short height = line_height(font) + 2;
	short y = DETAIL_TOP + 6;
	char line[96], value[GAME_LIST_MAP_LENGTH + 8];

	quad(SCREEN_LEFT, DETAIL_TOP, SCREEN_RIGHT, DETAIL_BOTTOM, COLOR_PANEL);
	if (!game)
		return;
	draw_map_picture(game->map, DETAIL_TOP + 6, DETAIL_BOTTOM - 6);
	text(font, _justify_left, text_color, DETAIL_TEXT, y, SCREEN_RIGHT - 8, y + height, game->name);
	y += height;
#define DETAIL_LINE(label, value) \
	text(font, _justify_left, label_color, DETAIL_TEXT, y, DETAIL_TEXT + 80, y + height, label); \
	text(font, _justify_left, text_color, DETAIL_TEXT + 84, y, SCREEN_RIGHT - 8, y + height, value); \
	y += height;
	game_list_game_map_name(game, value, sizeof(value));
	DETAIL_LINE("Map", value);
	DETAIL_LINE("Type", game_list_type_name(game));
	snprintf(line, sizeof(line), "%d of %d", game->players, game->maximum_players);
	DETAIL_LINE("Players", line);
	snprintf(line, sizeof(line), "%s%s%s", game_list_state_name(game->state), game->region[0] ? ", " : "",
		game->region);
	DETAIL_LINE("Status", line);
#undef DETAIL_LINE
}

void browser_screen_render(
	void)
{
	long heading_font = ui_widget_online_games_font(TRUE);
	long font = ui_widget_online_games_font(FALSE);
	struct game_list const *list = ps3_game_list_get(NULL);
	struct game_list_game const *selected = selected_game();
	real_argb_color title_color, head_color, text_color, dim_color, note_color;
	short row_height = MAX(line_height(font) + 4, 18);
	short rows = MIN(MAXIMUM_ROWS, (LIST_BOTTOM - LIST_TOP) / row_height);
	short column_map, column_type, column_players, column_region;
	short page_first, row;
	long players = 0;
	char line[96], value[GAME_LIST_MAP_LENGTH + 8];
	int state = ps3_game_list_state();
	short index;

	set_color(&title_color, 1.0f, 0.24f, 0.55f, 1.0f);
	set_color(&head_color, 1.0f, 0.5f, 0.69f, 1.0f);
	set_color(&text_color, 1.0f, 0.9f, 0.93f, 0.99f);
	set_color(&dim_color, 1.0f, 0.56f, 0.65f, 0.78f);
	set_color(&note_color, 1.0f, 0.94f, 0.54f, 0.29f);
	if (rows < 1)
		rows = 1;
	browser_screen.rows = rows;

	/* the columns from the right: Region and Players as wide as their headings or widest values, Type and Map
	fixed, the game's name the rest */
	column_region = (short)(SCREEN_RIGHT - CELL_INSET - MAX(text_width(font, "REGION"), text_width(font, "Europe")));
	column_players = (short)(column_region - COLUMN_GAP -
		MAX(text_width(font, "PLAYERS"), text_width(font, "16/16")));
	column_type = (short)(column_players - COLUMN_GAP - COLUMN_TYPE_WIDTH);
	column_map = (short)(column_type - COLUMN_GAP - COLUMN_MAP_WIDTH);

	quad(0, 0, 640, 480, COLOR_BACKGROUND);
	text(heading_font != NONE ? heading_font : font, _justify_left, &title_color, SCREEN_LEFT, TITLE_TOP,
		SCREEN_RIGHT, RULE_Y - 2, "ONLINE GAMES");
	for (index = 0; index < browser_screen.count && list; index++)
		players += list->games[browser_screen.order[index]].players;
	snprintf(line, sizeof(line), "%d GAMES   %ld PLAYERS", browser_screen.count, players);
	text(font, _justify_right, &dim_color, SCREEN_LEFT, RULE_Y - 2 - line_height(font), SCREEN_RIGHT,
		RULE_Y - 2, line);
	quad(SCREEN_LEFT, RULE_Y, SCREEN_RIGHT, RULE_Y + 1, COLOR_RULE);

	/* the list's headings, on a band as wide as the panel, at the rows' columns */
	quad(SCREEN_LEFT, HEAD_TOP, SCREEN_RIGHT, LIST_TOP, COLOR_HEADER);
	{
		short ty = (short)(HEAD_TOP + (LIST_TOP - HEAD_TOP - line_height(font)) / 2);

		text(font, _justify_left, &head_color, SCREEN_LEFT + CELL_INSET, ty, column_map - CELL_INSET, LIST_TOP,
			"GAME");
		text(font, _justify_left, &head_color, column_map, ty, column_type - CELL_INSET, LIST_TOP, "MAP");
		text(font, _justify_left, &head_color, column_type, ty, column_players - CELL_INSET, LIST_TOP, "TYPE");
		text(font, _justify_left, &head_color, column_players, ty, column_region - CELL_INSET, LIST_TOP, "PLAYERS");
		text(font, _justify_left, &head_color, column_region, ty, SCREEN_RIGHT, LIST_TOP, "REGION");
	}
	quad(SCREEN_LEFT, LIST_TOP, SCREEN_RIGHT, LIST_TOP + rows * row_height, COLOR_PANEL);
	page_first = (short)(browser_screen.selected - browser_screen.selected % rows);
	if (!list || !browser_screen.count)
	{
		/* (why a fetch failed is in the log: src/ps3_online.c) */
		char const *words =
			state == PS3_GAME_LIST_FAILED ? "Could not reach the game list. Check your network settings." :
			state == PS3_GAME_LIST_FETCHING || !list ? "Getting the game list..." :
			browser_list_filter() == 2 ? "No co-op games right now." :
			browser_list_filter() == 1 ? "No multiplayer games right now." :
			"No one is hosting right now.";

		text(font, _justify_center, &dim_color, SCREEN_LEFT, LIST_TOP + row_height * 2, SCREEN_RIGHT,
			LIST_TOP + row_height * 3, words);
	}
	for (row = 0; list && row < rows && page_first + row < browser_screen.count; row++)
	{
		struct game_list_game const *game = &list->games[browser_screen.order[page_first + row]];
		short y0 = (short)(LIST_TOP + row * row_height);
		short y1 = (short)(y0 + row_height);
		short ty = (short)(y0 + (row_height - line_height(font)) / 2);
		real_argb_color const *color = strcmp(game->state, "open") ? &dim_color : &text_color;

		if (page_first + row == browser_screen.selected)
		{
			quad(SCREEN_LEFT, y0, SCREEN_RIGHT, y1, COLOR_ROW_SELECTED);
			color = &text_color;
		}
		text(font, _justify_left, color, SCREEN_LEFT + CELL_INSET, ty, column_map - CELL_INSET, y1, game->name);
		game_list_game_map_name(game, value, sizeof(value));
		text(font, _justify_left, color, column_map, ty, column_type - CELL_INSET, y1, value);
		text(font, _justify_left, color, column_type, ty, column_players - CELL_INSET, y1, game_list_type_name(game));
		snprintf(line, sizeof(line), "%d/%d", game->players, game->maximum_players);
		text(font, _justify_left, color, column_players, ty, column_region - CELL_INSET, y1, line);
		text(font, _justify_left, color, column_region, ty, SCREEN_RIGHT, y1, game->region[0] ? game->region : "-");
	}
	/* under the list: what A or X last did (a while), else the list's state; and the page */
	{
		short y0 = (short)(LIST_TOP + rows * row_height + 2);
		short y1 = (short)(y0 + line_height(font) + 2);

		if (browser_screen.status[0] && system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
		{
			text(font, _justify_left, &note_color, SCREEN_LEFT + CELL_INSET, y0, SCREEN_RIGHT, y1,
				browser_screen.status);
		}
		else if (state == PS3_GAME_LIST_FETCHING)
			text(font, _justify_left, &dim_color, SCREEN_LEFT + CELL_INSET, y0, SCREEN_RIGHT, y1, "Refreshing...");
		else if (state == PS3_GAME_LIST_FAILED && list && browser_screen.count)
		{
			text(font, _justify_left, &note_color, SCREEN_LEFT + CELL_INSET, y0, SCREEN_RIGHT, y1,
				"Could not refresh the game list.");
		}
		snprintf(line, sizeof(line), "PAGE %d OF %d", page_first / rows + 1,
			MAX(1, (browser_screen.count + rows - 1) / rows));
		text(font, _justify_right, &head_color, SCREEN_LEFT, y0, SCREEN_RIGHT - CELL_INSET, y1, line);
	}

	draw_details(selected, font, &head_color, &text_color);

	/* the buttons, each icon by its word from the left (centered text would part the icons from their words) */
	if (font != NONE)
	{
		/* (Y names the filter it is on) */
		static wchar_t const *const buttons[] =
		{
			L"%a-button Join   %x-button Refresh   %y-button Filter: All   %b-button Back",
			L"%a-button Join   %x-button Refresh   %y-button Filter: Multiplayer   %b-button Back",
			L"%a-button Join   %x-button Refresh   %y-button Filter: Co-op   %b-button Back",
		};
		rectangle2d bounds;

		bounds.x0 = SCREEN_LEFT + CELL_INSET;
		bounds.y0 = PROMPT_TOP;
		bounds.x1 = SCREEN_RIGHT;
		bounds.y1 = PROMPT_BOTTOM;
		draw_string_set_draw_mode(font, NONE, _justify_left, 0, &text_color);
		draw_string_and_hack_in_icons(&bounds, &bounds, NULL, 0,
			browser_screen.connecting ? L"%b-button Stop" :
				buttons[browser_list_filter() == 2 ? 2 : browser_list_filter() == 1 ? 1 : 0], FALSE);
	}
}

/* ---------- the desktop's other screens of code (moderation, the clipboard's invite): never up on the PS3 */

boolean moderation_screen_active(
	void)
{
	return FALSE;
}

void moderation_screen_update(
	boolean menu_open)
{
	(void)menu_open;
}

void moderation_screen_process(
	void)
{
}

void moderation_screen_render(
	void)
{
}

void invite_prompt_render(
	void)
{
}
