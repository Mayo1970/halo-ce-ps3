/* The game list's games as the ONLINE GAMES screen words them (game/ps3_browser_screen.c): maps, types, states
and the list's order. The original Xbox's (warthog-xbox xbox_game_list_text.c), as the 360's and Wii U's. */

#include <string.h>

#include "ps3_game_list.h"

/* the multiplayer maps, as the menus name them */
static const char *const map_names[][2] =
{
	{ "beavercreek", "Battle Creek" }, { "bloodgulch", "Blood Gulch" }, { "boardingaction", "Boarding Action" },
	{ "carousel", "Derelict" }, { "chillout", "Chill Out" }, { "damnation", "Damnation" },
	{ "hangemhigh", "Hang 'Em High" }, { "longest", "Longest" }, { "prisoner", "Prisoner" },
	{ "putput", "Chiron TL-34" }, { "ratrace", "Rat Race" }, { "sidewinder", "Sidewinder" }, { "wizard", "Wizard" },
};

/* the engines (the list's 1 to 5), as the menus name them */
static const char *const engine_names[] =
{
	"", "CTF", "Slayer", "Oddball", "King of the Hill", "Race",
};

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static void copy(char *text, unsigned long size, const char *from, unsigned long length)
{
	if (!size)
		return;
	if (length > size - 1)
		length = size - 1;
	memcpy(text, from, length);
	text[length] = 0;
}

int game_list_map_name(const char *map, char *text, unsigned long size)
{
	unsigned long start = 0, end, index, at;
	const char *family = NULL;

	if (!size)
		return 0;
	text[0] = 0;
	if (!map)
		return 0;
	/* the scenario's name: after its path's last separator, before any @ */
	end = (unsigned long)strlen(map);
	for (at = 0; at < end && map[at] != '@'; at++)
		;
	if (at < end)
		family = !strcmp(map + at, "@ce") ? "PC" : !strcmp(map + at, "@md") ? "MD" : "?";
	for (index = 0; index < at; index++)
	{
		if (map[index] == '\\' || map[index] == '/')
			start = index + 1;
	}
	if (!family)
	{
		for (index = 0; index < sizeof(map_names) / sizeof(map_names[0]); index++)
		{
			const char *name = map_names[index][0];
			unsigned long length = (unsigned long)strlen(name), character;

			if (length != at - start)
				continue;
			for (character = 0; character < length && lower((unsigned char)map[start + character]) == name[character];
				character++)
				;
			if (character == length)
			{
				copy(text, size, map_names[index][1], (unsigned long)strlen(map_names[index][1]));
				return 1;
			}
		}
		copy(text, size, map + start, at - start);
		return 0;
	}
	/* a Halo PC or HaloMD map: its file, and its family's mark */
	copy(text, size, map + start, at - start);
	index = (unsigned long)strlen(text);
	if (size > index + 5)
	{
		text[index] = ' ';
		text[index + 1] = '(';
		text[index + 2] = family[0];
		text[index + 3] = family[1] ? family[1] : ')';
		text[index + 4] = family[1] ? ')' : 0;
		text[index + 5] = 0;
	}
	return 0;
}

int game_list_game_map_name(const struct game_list_game *game, char *text, unsigned long size)
{
	unsigned long length;

	if (!game->map_title[0] || !game->map_tag[0] || !strchr(game->map, '@'))
		return game_list_map_name(game->map, text, size);
	if (!size)
		return 0;
	copy(text, size, game->map_title, (unsigned long)strlen(game->map_title));
	length = (unsigned long)strlen(text);
	if (size > length + strlen(game->map_tag) + 3)
	{
		text[length] = ' ';
		text[length + 1] = '(';
		copy(text + length + 2, size - length - 2, game->map_tag, (unsigned long)strlen(game->map_tag));
		length = (unsigned long)strlen(text);
		text[length] = ')';
		text[length + 1] = 0;
	}
	return 0;
}

const char *game_list_type_name(const struct game_list_game *game)
{
	const char *gametype = game->gametype;

	/* (a name only of "?", from characters the list can't send, is none) */
	while (*gametype == '?' || *gametype == ' ')
		gametype++;
	if (*gametype)
		return game->gametype;
	if (game->engine > 0 && game->engine < (short)(sizeof(engine_names) / sizeof(engine_names[0])))
		return engine_names[game->engine];
	return "Unknown";
}

const char *game_list_state_name(const char *state)
{
	if (!strcmp(state, "open"))
		return "Open";
	if (!strcmp(state, "playing"))
		return "In Progress";
	if (!strcmp(state, "full"))
		return "Full";
	if (!strcmp(state, "closed"))
		return "Closed";
	return "Unknown";
}

/* a before b in the screen: more players first, then by name (ASCII, case aside), then as the list had them */
static int before(const struct game_list *list, int a, int b)
{
	const struct game_list_game *first = &list->games[a];
	const struct game_list_game *second = &list->games[b];
	int index;

	if (first->players != second->players)
		return first->players > second->players;
	for (index = 0; first->name[index] || second->name[index]; index++)
	{
		int x = lower((unsigned char)first->name[index]), y = lower((unsigned char)second->name[index]);

		if (x != y)
			return x < y;
	}
	return a < b;
}

/* the Xbox's campaign levels: a game on one is network co-op */
static const char *const campaign_maps[] =
{
	"a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
};

int game_list_is_coop(const struct game_list_game *game)
{
	const char *gametype = game->gametype;
	const char *base = game->map;
	const char *cursor;
	unsigned int index;

	/* (the host's co-op variant is named "Co-op": network_server_manager.c) */
	if (lower((unsigned char)gametype[0]) == 'c' && lower((unsigned char)gametype[1]) == 'o' &&
		(gametype[2] == '-' ? lower((unsigned char)gametype[3]) == 'o' && lower((unsigned char)gametype[4]) == 'p' &&
			!gametype[5] : lower((unsigned char)gametype[2]) == 'o' && lower((unsigned char)gametype[3]) == 'p' &&
			!gametype[4]))
	{
		return 1;
	}
	if (game->engine > 0)
		return 0;
	for (cursor = game->map; *cursor; cursor++)
	{
		if (*cursor == '\\' || *cursor == '/')
			base = cursor + 1;
	}
	for (index = 0; index < sizeof(campaign_maps) / sizeof(campaign_maps[0]); index++)
	{
		if (!strcmp(base, campaign_maps[index]))
			return 1;
	}
	return 0;
}

int game_list_order(const struct game_list *list, unsigned char *order, int filter)
{
	int count = list->count, index, place, shown = 0;

	if (count < 0)
		count = 0;
	if (count > GAME_LIST_MAXIMUM_GAMES)
		count = GAME_LIST_MAXIMUM_GAMES;
	/* (an insertion sort: 64 games at most; the games the filter leaves out skipped) */
	for (index = 0; index < count; index++)
	{
		if (filter && game_list_is_coop(&list->games[index]) != (filter == 2))
			continue;
		for (place = shown; place > 0 && before(list, index, order[place - 1]); place--)
			order[place] = order[place - 1];
		order[place] = (unsigned char)index;
		shown++;
	}
	return shown;
}
