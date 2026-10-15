/* The Linux build's services the PS3 has not yet (p2p.c, menu_files.c, hud_hires.c, text_hires.c, sdl_platform.c,
voice_audio.c, xbox_textures.c's Custom Edition texels, memory_watch.c): each reports "none", which the game handles. */

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

void p2p_set_hosting_password(const char *password)
{
	(void)password;
}

int p2p_listing_unlock(struct p2p_listing *listing, const char *password)
{
	(void)listing;
	(void)password;
	return 0;
}

int p2p_peer_key_hash(unsigned long virtual_address, unsigned char *identifier, unsigned char *key_hash)
{
	(void)virtual_address;
	(void)identifier;
	(void)key_hash;
	return 0;
}

void p2p_set_answering_players(int on)
{
	(void)on;
}

int p2p_reach_player(const unsigned char *key_hash)
{
	(void)key_hash;
	return 0;
}

int p2p_peer_reached(const unsigned char *identifier, unsigned long *address)
{
	(void)identifier;
	(void)address;
	return 0;
}

/* map sharing's piece hashes, as p2p_crypto.c's (the Makefile builds Monocypher) */
void crypto_blake2b(unsigned char *hash, size_t hash_size, const unsigned char *message, size_t message_size);

void p2p_blake2b_256(const void *data, unsigned long size, unsigned char *digest)
{
	crypto_blake2b(digest, 32, data, size);
}

/* ---------- voice chat (voice_audio.c): no microphone, and the others' voices are not played */

int voice_audio_microphone(int open)
{
	(void)open;
	return 0;
}

int voice_audio_read_frame(float *frame)
{
	(void)frame;
	return 0;
}

float voice_audio_level(const float *frame)
{
	(void)frame;
	return 0.0f;
}

int voice_audio_encode(const float *frame, int bitrate, unsigned char *packet, int maximum)
{
	(void)frame;
	(void)bitrate;
	(void)packet;
	(void)maximum;
	return 0;
}

void voice_audio_play(int speaker, unsigned short sequence, const unsigned char *packet, int length, float gain,
	float pan)
{
	(void)speaker; (void)sequence; (void)packet; (void)length; (void)gain; (void)pan;
}

void voice_audio_forget(int speaker)
{
	(void)speaker;
}

void voice_audio_forget_all(void)
{
}

void voice_audio_set_volume(float volume)
{
	(void)volume;
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

void text_hires_set_reset_hook(void (*hook)(void))
{
	(void)hook;
}

unsigned long hud_hires_asset_sprites(long asset)
{
	(void)asset;
	return 0;
}

int hud_hires_sprites_drawable(long asset, unsigned long address, unsigned long level0_size)
{
	(void)asset;
	(void)address;
	(void)level0_size;
	return 0;
}

void hud_hires_register_placeholder(long asset, unsigned long const *texture)
{
	(void)asset;
	(void)texture;
}

/* ---------- Custom Edition maps: none (their tags are not swapped to the PS3's byte order) */

const char *platform_custom_edition_root(void)
{
	return "";
}

void *halo_custom_edition_tag_cache(void)
{
	return NULL;
}

void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order)
{
	(void)texels;
	(void)channel_order;
}

void halo_custom_edition_texels_forget(void)
{
}

/* ---------- the renderer's write tracking (memory_watch.c): none, the RSX copies go as maps load */

void memory_watch_prepare_write(void *address, unsigned long size)
{
	(void)address;
	(void)size;
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

/* (the settings' lists of modes and devices: none to choose) */
int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

int platform_audio_devices(int recording, char (*names)[128], int maximum)
{
	(void)recording;
	(void)names;
	(void)maximum;
	return 0;
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
