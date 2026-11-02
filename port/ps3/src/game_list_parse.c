/* The game list's HTTP response and text, each read bounded: no terminator needed, nothing copied past its
destination, a response or list cut short an error. The original Xbox's (warthog-xbox xbox_game_list_parse.c). */

#include <string.h>

#include "ps3_game_list.h"

static const char *const error_strings[NUMBER_OF_GAME_LIST_ERRORS] =
{
	"ok",
	"bad status line",
	"bad header",
	"headers cut short",
	"bad Content-Length",
	"body cut short",
	"an encoding the list does not use",
	"bad list header",
	"a list format this build does not read",
	"list cut short",
	"the list's count does not match",
	"the host did not resolve",
	"could not connect",
	"could not send",
	"could not receive",
	"timed out",
	"the response is too large",
	"the server refused",
};

const char *game_list_error_string(int error)
{
	return error >= 0 && error < NUMBER_OF_GAME_LIST_ERRORS ? error_strings[error] : "unknown error";
}

/* ---------- lines */

enum
{
	_line_ok,
	_line_missing,
	_line_long
};

/* the next line from *offset, without its CR LF (or LF), *offset past it. _line_missing: no line break before
size; _line_long: none within limit bytes (*offset then past the line break, for a caller that skips it) */
static int next_line(const char *data, unsigned long size, unsigned long *offset, unsigned long limit,
	const char **line, unsigned long *length)
{
	unsigned long start = *offset;
	unsigned long index;
	unsigned long end;

	if (start >= size)
		return _line_missing;
	for (index = start; index < size && data[index] != '\n'; index++)
		;
	if (index >= size)
		return _line_missing;
	*offset = index + 1;
	end = index;
	if (end > start && data[end - 1] == '\r')
		end--;
	if (end - start > limit)
		return _line_long;
	*line = data + start;
	*length = end - start;
	return _line_ok;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* the two equal, ASCII case aside; text of length bytes, name terminated */
static int equal_ignoring_case(const char *text, unsigned long length, const char *name)
{
	unsigned long index;

	for (index = 0; index < length; index++)
	{
		if (!name[index] || lower((unsigned char)text[index]) != lower((unsigned char)name[index]))
			return 0;
	}
	return !name[length];
}

/* digits only, at most digits of them, as a number no more than maximum: -1 if not */
static long parse_number(const char *text, unsigned long length, unsigned long digits, long maximum)
{
	unsigned long index;
	long value = 0;

	if (!length || length > digits)
		return -1;
	for (index = 0; index < length; index++)
	{
		if (text[index] < '0' || text[index] > '9')
			return -1;
		value = value * 10 + (text[index] - '0');
	}
	return value <= maximum ? value : -1;
}

/* ---------- the HTTP response */

static int token_character(int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

int game_list_parse_response(const char *data, unsigned long size, struct game_list_response *response)
{
	unsigned long offset = 0;
	const char *line;
	unsigned long length;
	int headers = 0;
	int result;
	long status;

	memset(response, 0, sizeof(*response));
	response->content_length = -1;
	if (!data)
		return GAME_LIST_ERROR_STATUS_LINE;

	/* "HTTP/1.x 200 OK" (the reason may be empty, or missing with its space) */
	result = next_line(data, size, &offset, GAME_LIST_HEADER_LINE, &line, &length);
	if (result != _line_ok)
		return result == _line_missing ? GAME_LIST_ERROR_HEADERS_TRUNCATED : GAME_LIST_ERROR_STATUS_LINE;
	if (length < 12 || memcmp(line, "HTTP/1.", 7) || line[7] < '0' || line[7] > '9' || line[8] != ' ' ||
		(length > 12 && line[12] != ' '))
	{
		return GAME_LIST_ERROR_STATUS_LINE;
	}
	status = parse_number(line + 9, 3, 3, 999);
	if (status < 100 || status > 599)
		return GAME_LIST_ERROR_STATUS_LINE;
	response->status = (int)status;

	for (;;)
	{
		unsigned long colon, start, end, index;

		result = next_line(data, size, &offset, GAME_LIST_HEADER_LINE, &line, &length);
		if (result == _line_missing)
			return GAME_LIST_ERROR_HEADERS_TRUNCATED;
		if (result == _line_long)
			return GAME_LIST_ERROR_HEADER;
		if (!length)
			break;
		if (++headers > GAME_LIST_HEADERS)
			return GAME_LIST_ERROR_HEADER;
		/* name ":" value, the name a token (no folded lines, no space before the colon) */
		for (colon = 0; colon < length && token_character((unsigned char)line[colon]); colon++)
			;
		if (!colon || colon >= length || line[colon] != ':')
			return GAME_LIST_ERROR_HEADER;
		for (start = colon + 1; start < length && (line[start] == ' ' || line[start] == '\t'); start++)
			;
		for (end = length; end > start && (line[end - 1] == ' ' || line[end - 1] == '\t'); end--)
			;
		for (index = start; index < end; index++)
		{
			unsigned char c = (unsigned char)line[index];

			if ((c < 0x20 && c != '\t') || c == 0x7f)
				return GAME_LIST_ERROR_HEADER;
		}
		if (equal_ignoring_case(line, colon, "content-length"))
		{
			long value = parse_number(line + start, end - start, 9, GAME_LIST_RESPONSE_SIZE);

			if (value < 0 || (response->content_length >= 0 && response->content_length != value))
				return GAME_LIST_ERROR_CONTENT_LENGTH;
			response->content_length = value;
		}
		else if (equal_ignoring_case(line, colon, "transfer-encoding") ||
			(equal_ignoring_case(line, colon, "content-encoding") &&
				!equal_ignoring_case(line + start, end - start, "identity")))
		{
			/* (an HTTP/1.0 request is never sent chunks, and the request asks for no compression) */
			return GAME_LIST_ERROR_ENCODING;
		}
	}

	response->body = data + offset;
	response->body_size = size - offset;
	if (response->content_length >= 0)
	{
		if (response->body_size < (unsigned long)response->content_length)
			return GAME_LIST_ERROR_BODY_TRUNCATED;
		response->body_size = (unsigned long)response->content_length;
	}
	return GAME_LIST_OK;
}

/* ---------- the list */

/* the Latin letters with marks (U+00C0 to U+017F) as the plain letters the menus' fonts have; a space for a
character with no plain form */
static const char latin_1_letters[] =
	"AAAAAA CEEEEIIIIDNOOOOO OUUUUY  aaaaaa ceeeeiiiidnooooo ouuuuy y";
static const char latin_extended_a_letters[] =
	"AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

/* a character (a Unicode code point) as plain ASCII, into text (room for 2): its length, 0 for one with no
plain form (dropped) */
static int plain_text(unsigned long character, char *text)
{
	if (character >= 0x20 && character <= 0x7e)
	{
		text[0] = (char)character;
		return 1;
	}
	switch (character)
	{
	case 0xa0: text[0] = ' '; return 1;
	case 0xa1: text[0] = '!'; return 1;
	case 0xbf: text[0] = '?'; return 1;
	case 0xc6: text[0] = 'A'; text[1] = 'E'; return 2;
	case 0xe6: text[0] = 'a'; text[1] = 'e'; return 2;
	case 0xde: text[0] = 'T'; text[1] = 'h'; return 2;
	case 0xfe: text[0] = 't'; text[1] = 'h'; return 2;
	case 0xdf: text[0] = 's'; text[1] = 's'; return 2;
	case 0x152: text[0] = 'O'; text[1] = 'E'; return 2;
	case 0x153: text[0] = 'o'; text[1] = 'e'; return 2;
	case 0x2018: case 0x2019: text[0] = '\''; return 1;
	case 0x201c: case 0x201d: text[0] = '"'; return 1;
	case 0x2013: case 0x2014: text[0] = '-'; return 1;
	default: break;
	}
	if (character >= 0xc0 && character <= 0xff && latin_1_letters[character - 0xc0] != ' ')
	{
		text[0] = latin_1_letters[character - 0xc0];
		return 1;
	}
	if (character >= 0x100 && character <= 0x17f)
	{
		text[0] = latin_extended_a_letters[character - 0x100];
		return 1;
	}
	return 0;
}

/* the UTF-8 character at text (length bytes left): its code point and how many bytes it takes; 1 (and a code
point of 0, dropped) for a byte that does not start one */
static unsigned long next_character(const unsigned char *text, unsigned long length, unsigned long *character)
{
	unsigned long size, index;
	unsigned long value;

	*character = 0;
	if (text[0] < 0x80)
	{
		*character = text[0];
		return 1;
	}
	if (text[0] >= 0xc2 && text[0] <= 0xdf)
	{
		size = 2;
		value = text[0] & 0x1f;
	}
	else if (text[0] >= 0xe0 && text[0] <= 0xef)
	{
		size = 3;
		value = text[0] & 0x0f;
	}
	else if (text[0] >= 0xf0 && text[0] <= 0xf4)
	{
		size = 4;
		value = text[0] & 0x07;
	}
	else
		return 1;
	if (size > length)
		return 1;
	for (index = 1; index < size; index++)
	{
		if ((text[index] & 0xc0) != 0x80)
			return 1;
		value = (value << 6) | (text[index] & 0x3f);
	}
	*character = value;
	return size;
}

/* a UTF-8 field into maximum + 1 characters of the plain ASCII the menus' fonts draw (a field left with nothing
is "?"); 0 if it is longer, or shorter than minimum */
static int copy_text(char *destination, unsigned long maximum, const char *text, unsigned long length,
	unsigned long minimum)
{
	unsigned long index = 0, used = 0;

	if (length < minimum)
		return 0;
	while (index < length)
	{
		unsigned long character;
		char plain[2];
		int size;

		index += next_character((const unsigned char *)text + index, length - index, &character);
		size = plain_text(character, plain);
		if (used + (unsigned long)size > maximum)
			return 0;
		memcpy(destination + used, plain, (size_t)size);
		used += (unsigned long)size;
	}
	if (!used && length)
		destination[used++] = '?';
	destination[used] = 0;
	return 1;
}

static int parse_invite(char *destination, const char *text, unsigned long length)
{
	unsigned long index;

	if (length != 64 && length != 44)
		return 0;
	for (index = 0; index < length; index++)
	{
		int c = lower((unsigned char)text[index]);

		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
		destination[index] = (char)c;
	}
	destination[length] = 0;
	return 1;
}

static int parse_state(char *destination, const char *text, unsigned long length)
{
	unsigned long index;

	if (!length || length > GAME_LIST_STATE_LENGTH)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (text[index] < 'a' || text[index] > 'z')
			return 0;
		destination[index] = text[index];
	}
	destination[length] = 0;
	return 1;
}

#define GAME_FIELDS 10
/* the fields read, at most: the ten, the host's platform, hosting and protocol (passed over), and a map pack's
map's name and its pack's tag */
#define GAME_FIELDS_READ 15

/* a game's line: its ten fields, tab separated, and the map pack's two after the host's three (later ones left
alone); 0 if one of the ten is missing or out of bounds (a bad map pack field is left empty) */
static int parse_game(const char *line, unsigned long length, struct game_list_game *game)
{
	const char *fields[GAME_FIELDS_READ];
	unsigned long sizes[GAME_FIELDS_READ];
	unsigned long start = 0, index;
	int count = 0;
	long engine, players, maximum_players, version;

	for (index = 0; index <= length && count < GAME_FIELDS_READ; index++)
	{
		if (index == length || line[index] == '\t')
		{
			fields[count] = line + start;
			sizes[count] = index - start;
			count++;
			start = index + 1;
		}
	}
	if (count < GAME_FIELDS)
		return 0;
	memset(game, 0, sizeof(*game));
	engine = parse_number(fields[3], sizes[3], 2, 15);
	players = parse_number(fields[5], sizes[5], 3, 255);
	maximum_players = parse_number(fields[6], sizes[6], 3, 255);
	version = parse_number(fields[7], sizes[7], 5, 65535);
	if (!parse_invite(game->invite, fields[0], sizes[0]) ||
		!copy_text(game->name, GAME_LIST_NAME_LENGTH, fields[1], sizes[1], 1) ||
		!copy_text(game->map, GAME_LIST_MAP_LENGTH, fields[2], sizes[2], 1) ||
		engine < 0 ||
		!copy_text(game->gametype, GAME_LIST_GAMETYPE_LENGTH, fields[4], sizes[4], 0) ||
		players < 0 || maximum_players < 0 || version < 0 ||
		!copy_text(game->region, GAME_LIST_REGION_LENGTH, fields[8], sizes[8], 0) ||
		!parse_state(game->state, fields[9], sizes[9]))
	{
		return 0;
	}
	/* (a map pack's map's name and its pack's tag, only for a map of a pack, <file>@<pack>) */
	if (count >= 15 && strchr(game->map, '@') && sizes[13] && sizes[14] &&
		(!copy_text(game->map_title, GAME_LIST_MAP_TITLE_LENGTH, fields[13], sizes[13], 1) ||
		!copy_text(game->map_tag, GAME_LIST_MAP_TAG_LENGTH, fields[14], sizes[14], 1)))
	{
		game->map_title[0] = 0;
		game->map_tag[0] = 0;
	}
	game->engine = (short)engine;
	game->players = (short)players;
	game->maximum_players = (short)maximum_players;
	game->version = (unsigned short)version;
	return 1;
}

int game_list_parse(const char *text, unsigned long size, struct game_list *list)
{
	static const char header[] = "warthog-list ";
	unsigned long offset = 0;
	const char *line;
	unsigned long length, index;
	long format, count;
	int lines = 0;
	int result;

	memset(list, 0, sizeof(*list));
	if (!text)
		return GAME_LIST_ERROR_LIST_HEADER;

	/* "warthog-list <format> <count>" */
	result = next_line(text, size, &offset, GAME_LIST_LINE, &line, &length);
	if (result != _line_ok)
		return result == _line_missing ? GAME_LIST_ERROR_LIST_TRUNCATED : GAME_LIST_ERROR_LIST_HEADER;
	if (length <= sizeof(header) - 1 || memcmp(line, header, sizeof(header) - 1))
		return GAME_LIST_ERROR_LIST_HEADER;
	line += sizeof(header) - 1;
	length -= sizeof(header) - 1;
	for (index = 0; index < length && line[index] != ' '; index++)
		;
	if (index >= length)
		return GAME_LIST_ERROR_LIST_HEADER;
	format = parse_number(line, index, 4, 9999);
	count = parse_number(line + index + 1, length - index - 1, 4, 9999);
	if (format < 0 || count < 0)
		return GAME_LIST_ERROR_LIST_HEADER;
	/* (a later format adds fields after the tenth, which this one skips) */
	if (format < GAME_LIST_FORMAT)
		return GAME_LIST_ERROR_FORMAT;
	if (count > GAME_LIST_MAXIMUM_GAMES)
		return GAME_LIST_ERROR_COUNT;
	list->format = (int)format;

	/* the games, then "end" */
	for (;;)
	{
		unsigned long before = offset;

		result = next_line(text, size, &offset, GAME_LIST_LINE, &line, &length);
		if (result == _line_missing)
		{
			/* ("end" as the last bytes, without its line break) */
			if (size - before == 3 && !memcmp(text + before, "end", 3))
			{
				line = text + before;
				length = 3;
			}
			else
			{
				memset(list, 0, sizeof(*list));
				return GAME_LIST_ERROR_LIST_TRUNCATED;
			}
		}
		if (result != _line_long && length == 3 && !memcmp(line, "end", 3))
			break;
		if (++lines > count)
		{
			memset(list, 0, sizeof(*list));
			return GAME_LIST_ERROR_COUNT;
		}
		if (result == _line_long || !parse_game(line, length, &list->games[list->count]))
			list->skipped++;
		else
			list->count++;
	}
	if (lines != count)
	{
		memset(list, 0, sizeof(*list));
		return GAME_LIST_ERROR_COUNT;
	}
	return GAME_LIST_OK;
}
