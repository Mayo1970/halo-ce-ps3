/* The Linux build's services the PS3 has not yet (p2p.c, menu_files.c, hud_hires.c, text_hires.c,
sdl_platform.c): each reports "none", which the game already handles. */

#include <string.h>

#include "p2p.h"
#include "halo_menus.h"

/* ---------- internet play */

void p2p_discord_identity(char *id, int id_size, char *name, int name_size)
{
	if (id_size > 0)
		id[0] = '\0';
	if (name_size > 0)
		name[0] = '\0';
}

void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	(void)source;
	(void)name;
	if (size > 0)
		destination[0] = '\0';
}

void p2p_hardware_id(char *hex, int size)
{
	if (size > 0)
		hex[0] = '\0';
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	(void)source;
	if (size > 0)
		destination[0] = '\0';
}

int p2p_invite_link(char *link, int size)
{
	if (size > 0)
		link[0] = '\0';
	return 0;
}

int p2p_join_invite(const char *text)
{
	(void)text;
	return 0;
}

void p2p_lobby_browse(int on)
{
	(void)on;
}

int p2p_lobby_games(struct p2p_listing *games, int maximum_count)
{
	(void)games;
	(void)maximum_count;
	return 0;
}

void p2p_lobby_mark_failed(const unsigned char *identifier)
{
	(void)identifier;
}

void p2p_lobby_refresh(void)
{
}

int p2p_peer_address(const unsigned char *identifier, unsigned long *address)
{
	(void)identifier;
	(void)address;
	return 0;
}

unsigned long p2p_peer_endpoint_address(unsigned long virtual_address)
{
	return virtual_address;
}

void p2p_set_game_listing(const char *name, const char *map, const char *gametype, int engine_type, int open,
	int in_progress, int has_teams)
{
	(void)name; (void)map; (void)gametype; (void)engine_type; (void)open; (void)in_progress; (void)has_teams;
}

void p2p_set_game_player_counts(int count, int maximum)
{
	(void)count;
	(void)maximum;
}

void p2p_set_hosting_allowed(int allowed)
{
	(void)allowed;
}

void p2p_set_hosting_public(int public)
{
	(void)public;
}

/* ---------- the PC menus: none, so the game keeps the Xbox's */

struct halo_menus const *halo_menus_load(void)
{
	return NULL;
}

long halo_menus_utf16(char const *utf8, unsigned short *out, long capacity)
{
	long count = 0;

	/* (unreachable without menus) ASCII only */
	while (count + 1 < capacity && utf8[count])
	{
		out[count] = (unsigned char)utf8[count];
		count++;
	}
	if (capacity > 0)
		out[count++] = 0;
	return count;
}

void halo_menus_log(char const *file, long line, char const *message, char const *detail)
{
	platform_log("menus: %s:%ld: %s %s", file ? file : "", (long)line, message ? message : "", detail ? detail : "");
}

void halo_menus_art_register(void const *texture, char const *png)
{
	(void)texture;
	(void)png;
}

void halo_menus_art_forget(void)
{
}

/* ---------- the high-res HUD and text: none, so the maps' own bitmaps and fonts draw */

long hud_hires_asset_count(void)
{
	return 0;
}

char const *hud_hires_asset_tag(long asset)
{
	(void)asset;
	return "";
}

long hud_hires_asset_bitmap(long asset)
{
	(void)asset;
	return 0;
}

long hud_hires_asset_fits(long asset, long width, long height)
{
	(void)asset;
	(void)width;
	(void)height;
	return 0;
}

struct text_hires_glyph;

long text_hires_font(char const *tag_name, float cap_height, float oversample)
{
	(void)tag_name;
	(void)cap_height;
	(void)oversample;
	return -1;
}

int text_hires_covers(long font, unsigned long code)
{
	(void)font;
	(void)code;
	return 0;
}

int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	(void)font;
	(void)code;
	(void)glyph;
	return 0;
}

void text_hires_register_atlas(unsigned long const *texture, unsigned long width, unsigned long height)
{
	(void)texture;
	(void)width;
	(void)height;
}

/* ---------- the desktop: window, clipboard, quit */

int halo_interpolation_enabled(void)
{
	/* 30 frames each second, as on the Xbox, until G4 paces frames at the display */
	return 0;
}

void platform_display_apply(void)
{
}

void platform_binding_capture_begin(void)
{
}

int platform_binding_capture_poll(int *input)
{
	(void)input;
	/* cancelled */
	return 3;
}

int platform_clipboard_get(char *text, int size)
{
	if (size > 0)
		text[0] = '\0';
	return 0;
}

void platform_clipboard_set(char const *text)
{
	(void)text;
}

void platform_request_quit(void)
{
	platform_log("the game asks to quit");
	ps3_host_exit(0);
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	(void)open;
	if (notches)
		*notches = 0;
	if (pages)
		*pages = 0;
}

void platform_show_message(char const *title, char const *message)
{
	platform_log("%s: %s", title ? title : "", message ? message : "");
}
