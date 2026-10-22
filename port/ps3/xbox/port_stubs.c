/* The Linux build's services the PS3 has not yet (menu_files.c, hud_hires.c, text_hires.c, sdl_platform.c,
voice_audio.c, xbox_textures.c's Custom Edition texels, memory_watch.c): each reports "none", which the game handles. */

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

/* display.interpolation (EXTRA OPTIONS' FRAME RATE): 60 frames a second between the 30 Hz ticks, else 30 */
int halo_interpolation_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled;
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
