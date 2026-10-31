/* PS3_GAME_LIST.H: the console game list (GET /v1/console/games), parsed and worded as the original Xbox's
(ChupathingyCE/warthog-xbox xbox_game_list.h), as the 360's and Wii U's. The site's text is untrusted. */
/* Both ABIs read it: the host's (src/, 64-bit long) and the game's LP64 copy; a long only passes by value. */

#ifndef __PS3_GAME_LIST_H
#define __PS3_GAME_LIST_H

#define GAME_LIST_HOST "warthog.milenko.org"
/* coop=1: the co-op games on the campaign levels too; players=32 as the other consoles' 16-player builds; no
packs=1: the PS3 plays the Xbox's own maps alone */
#define GAME_LIST_PATH "/v1/console/games?coop=1&players=32"
#define GAME_LIST_PORT 80
/* the whole response, headers and all, at most (about 130 bytes a line, 41 lines in 5.3 KB) */
#define GAME_LIST_RESPONSE_SIZE 10240
/* the response's header lines, at most this many of this many bytes */
#define GAME_LIST_HEADER_LINE 1024
#define GAME_LIST_HEADERS 64
/* the list: games and a line's bytes (its line break apart), as the server's CONSOLE_GAMES and CONSOLE_LINE */
#define GAME_LIST_MAXIMUM_GAMES 64
#define GAME_LIST_LINE 255
#define GAME_LIST_FORMAT 1
/* the fields' characters, at most (the server's caps) */
#define GAME_LIST_INVITE_LENGTH 64
#define GAME_LIST_NAME_LENGTH 32
#define GAME_LIST_MAP_LENGTH 64
#define GAME_LIST_GAMETYPE_LENGTH 24
#define GAME_LIST_REGION_LENGTH 16
#define GAME_LIST_STATE_LENGTH 8
/* an Xbox map pack's map's name and its pack's tag ("Gothra", "CE+") */
#define GAME_LIST_MAP_TITLE_LENGTH 32
#define GAME_LIST_MAP_TAG_LENGTH 4

enum
{
	GAME_LIST_OK = 0,
	/* the HTTP response */
	GAME_LIST_ERROR_STATUS_LINE,
	GAME_LIST_ERROR_HEADER,
	GAME_LIST_ERROR_HEADERS_TRUNCATED,
	GAME_LIST_ERROR_CONTENT_LENGTH,
	GAME_LIST_ERROR_BODY_TRUNCATED,
	GAME_LIST_ERROR_ENCODING,
	/* the list */
	GAME_LIST_ERROR_LIST_HEADER,
	GAME_LIST_ERROR_FORMAT,
	GAME_LIST_ERROR_LIST_TRUNCATED,
	GAME_LIST_ERROR_COUNT,
	/* the fetch */
	GAME_LIST_ERROR_RESOLVE,
	GAME_LIST_ERROR_CONNECT,
	GAME_LIST_ERROR_SEND,
	GAME_LIST_ERROR_RECEIVE,
	GAME_LIST_ERROR_TIMEOUT,
	GAME_LIST_ERROR_TOO_LARGE,
	GAME_LIST_ERROR_HTTP,
	NUMBER_OF_GAME_LIST_ERRORS
};

/* (the host's alone) */
struct game_list_response
{
	int status;
	/* -1: none given */
	long content_length;
	const char *body;
	unsigned long body_size;
};

struct game_list_game
{
	char invite[GAME_LIST_INVITE_LENGTH + 1];
	char name[GAME_LIST_NAME_LENGTH + 1];
	char map[GAME_LIST_MAP_LENGTH + 1];
	char gametype[GAME_LIST_GAMETYPE_LENGTH + 1];
	char region[GAME_LIST_REGION_LENGTH + 1];
	char state[GAME_LIST_STATE_LENGTH + 1];
	/* an Xbox map pack's map's name and its pack's tag (fields 14 and 15; empty when the list gives none) */
	char map_title[GAME_LIST_MAP_TITLE_LENGTH + 1];
	char map_tag[GAME_LIST_MAP_TAG_LENGTH + 1];
	short engine;
	short players;
	short maximum_players;
	unsigned short version;
};

struct game_list
{
	int format;
	int count;
	/* the lines left out (a field missing or out of bounds) */
	int skipped;
	struct game_list_game games[GAME_LIST_MAXIMUM_GAMES];
};

const char *game_list_error_string(int error);

/* an HTTP/1.x response, size bytes (no terminator needed): its status, and its body (pointing into data) as
Content-Length says, else to the end */
int game_list_parse_response(const char *data, unsigned long size, struct game_list_response *response);

/* the list's text, size bytes (no terminator needed) */
int game_list_parse(const char *text, unsigned long size, struct game_list *list);

/* ---------- the screen's words (src/game_list_text.c) */

/* a map as the menus name it ("Blood Gulch"), else its file's name ("timberland (PC)", "(MD)" for HaloMD's), at
most size - 1 characters: 1 if it is one of the console's own multiplayer maps */
int game_list_map_name(const char *map, char *text, unsigned long size);
/* a game's map in words: a map pack's by the list's name and tag ("Gothra (CE+)"), else as game_list_map_name */
int game_list_game_map_name(const struct game_list_game *game, char *text, unsigned long size);
/* a game's type: its gametype, else its engine's name, else "Unknown" */
const char *game_list_type_name(const struct game_list_game *game);
/* a game's state ("open", ...) in words */
const char *game_list_state_name(const char *state);
/* whether a game is network co-op (its gametype "Co-op", or no engine on a campaign level) */
int game_list_is_coop(const struct game_list_game *game);
/* the games in the screen's order (most players first, then by name) as indices into list->games; their count.
filter: browser.h's BROWSER_LIST_FILTER_* (0 every game, 1 multiplayer, 2 co-op) */
int game_list_order(const struct game_list *list, unsigned char *order, int filter);

/* ---------- the PS3 (src/ps3_online.c) */

enum
{
	PS3_GAME_LIST_NONE,
	PS3_GAME_LIST_FETCHING,
	PS3_GAME_LIST_READY,
	PS3_GAME_LIST_FAILED
};

/* the first list, then the signed legacy table, once Winsock starts (xnet_ps3.c) */
void ps3_online_start(void);
/* the list asked for again, on the fetch's thread (not while one runs) */
void ps3_game_list_request(void);
/* the game thread's: the last list that came (NULL before one; kept while a refresh fails), and how many came */
const struct game_list *ps3_game_list_get(int *lists);
/* the latest request's state (PS3_GAME_LIST_...), and its error's words */
int ps3_game_list_state(void);
const char *ps3_game_list_error(void);

#endif
