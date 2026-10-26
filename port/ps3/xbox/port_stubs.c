/* The Linux build's services the PS3 has not yet (menu_files.c, hud_hires.c, text_hires.c, sdl_platform.c, voice_audio.c,
xbox_textures.c's Custom Edition texels, memory_watch.c, port_mods.c, d3d8_gl.c's extras): each reports "none". */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "halo_menus.h"
#include "port_config.h"

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

	/* ASCII only: the Xbox menus' INTERNET PLAY strings (menu_tags.c) */
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

/* ---------- the high-res HUD and text: none, so the maps' own bitmaps and fonts draw (the button icons:
hud_hires_rsx.c) */

/* no Custom Edition redraws (no Custom Edition maps) */
int hud_hires_asset_custom_edition(long asset)
{
	(void)asset;
	return 0;
}

int hud_hires_asset_ce_only(long asset)
{
	(void)asset;
	return 0;
}

long hud_hires_ce_stock_count_get(void)
{
	return 0;
}

long hud_hires_ce_stock_find(char const *tag, long bitmap)
{
	(void)tag;
	(void)bitmap;
	return -1;
}

char const *hud_hires_ce_stock_tag(long index)
{
	(void)index;
	return "";
}

long hud_hires_ce_stock_bitmap(long index)
{
	(void)index;
	return -1;
}

int hud_hires_ce_stock_matches(long index, unsigned long address, long width, long height, unsigned long size)
{
	(void)index; (void)address; (void)width; (void)height; (void)size;
	return 0;
}

/* (the button icons are made as they are first drawn: hud_hires_rsx.c) */
void hud_hires_map_loaded(const long *assets, long count)
{
	(void)assets;
	(void)count;
}

void hud_hires_map_unloaded(void)
{
}

void halo_custom_edition_hud_meter(int active)
{
	(void)active;
}

int text_hires_enabled(void)
{
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

short players_port_local_player_count(void);

/* display.interpolation (EXTRA OPTIONS' FRAME RATE): 60 frames a second between the 30 Hz ticks, else 30; split screen
always 30 */
int halo_interpolation_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled && players_port_local_player_count() <= 1;
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

/* ---------- the renderer's extras (d3d8_gl.c): the plasma's height fade, model detail and tone mapping as stock */

void d3d8_gl_set_height_fade(float inverse_softness, float height, float offset, float scale)
{
	(void)inverse_softness; (void)height; (void)offset; (void)scale;
}

int halo_model_detail_stock(void)
{
	return 1;
}

float halo_model_detail_scale(int level)
{
	(void)level;
	return 1.0f;
}

void halo_screen_tone_map(short x0, short y0, short x1, short y1)
{
	(void)x0; (void)y0; (void)x1; (void)y1;
}

/* ---------- input's extras (xinput_sdl.c): no text chat keyboard, and bindings named as written */

unsigned long halo_chat_pad_buttons(void)
{
	return 0;
}

int halo_chat_taps(float *x, float *y)
{
	(void)x;
	(void)y;
	return 0;
}

int halo_chat_touch_layout(void)
{
	return 0;
}

void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	snprintf(shown, size, "%s", binding);
}

void test_input_press(unsigned long buttons)
{
	(void)buttons;
}

/* ---------- mods (port_mods.c): no mods folder */

long port_mods_scan(void)
{
	return 0;
}

unsigned long port_mods_scans(void)
{
	return 0;
}

long port_mods_played(void)
{
	return 0;
}

long port_mods_file_find(const char *tag_file, const char *map_name)
{
	(void)tag_file;
	(void)map_name;
	return -1;
}

int port_mods_file_stamp(long file, unsigned long *size, unsigned long stamp[2])
{
	(void)file; (void)size; (void)stamp;
	return 0;
}

int port_mods_file_read(long file, unsigned long offset, void *buffer, unsigned long size)
{
	(void)file; (void)offset; (void)buffer; (void)size;
	return 0;
}

long port_mods_file_order(long file)
{
	(void)file;
	return -1;
}

const char *port_mods_file_mod(long file)
{
	(void)file;
	return "";
}

const char *port_mods_file_path(long file)
{
	(void)file;
	return "";
}

long port_mods_count(void)
{
	return 0;
}

const char *port_mods_folder(long index)
{
	(void)index;
	return "";
}

int port_mods_describe(long index, char *text, unsigned long size, const char *map_name)
{
	(void)index; (void)map_name;
	if (size > 0)
		text[0] = '\0';
	return 0;
}

long port_mods_at(long position)
{
	(void)position;
	return -1;
}

const char *port_mods_name(long index)
{
	(void)index;
	return "";
}

int port_mods_on(long index)
{
	(void)index;
	return 0;
}

int port_mods_status(long index, char *text, unsigned long size)
{
	(void)index;
	if (size > 0)
		text[0] = '\0';
	return 0;
}

int port_mods_details(long index, char *text, unsigned long size)
{
	(void)index;
	if (size > 0)
		text[0] = '\0';
	return 0;
}

long port_mods_move(long position, long places)
{
	(void)places;
	return position;
}

void port_mods_turn(long index, int on)
{
	(void)index;
	(void)on;
}

int port_mods_save(void)
{
	return 1;
}

void port_mods_revert(void)
{
}
