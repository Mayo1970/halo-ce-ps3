/* The G0 bring-up test of the platform layer (port/ps3/README.md): video, pads, audio, files and
the log, without the game. `./build.sh bringup` builds it. */

#include "ps3_audio.h"
#include "ps3_input.h"
#include "ps3_system.h"
#include "ps3_video.h"

#include <dirent.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/process.h>

/* 1 MB main thread stack, as in the other PS3 ports */
SYS_PROCESS_PARAM(1001, 0x100000);

#define TONE_HZ 440.0
#define TONE_LEVEL 0.25f
#define TONE_CHUNK 512

static const char *const button_names[PS3_PAD_BUTTON_COUNT] =
{
	"SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT",
	"L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"
};

/* lists maps/ of the data root, whose name can have any case */
static void log_maps(void)
{
	char folder[128], path[256];
	const char *root = ps3_data_root();
	DIR *directory = opendir(root);
	struct dirent *entry;
	unsigned count = 0;
	unsigned long long total = 0;

	folder[0] = '\0';
	if (directory)
	{
		while ((entry = readdir(directory)) != NULL)
		{
			if (strcasecmp(entry->d_name, "maps") == 0)
			{
				if (snprintf(folder, sizeof(folder), "%s/%s", root, entry->d_name) >= (int)sizeof(folder))
					folder[0] = '\0';
				break;
			}
		}
		closedir(directory);
	}
	if (!folder[0] || (directory = opendir(folder)) == NULL)
	{
		ps3_log_printf("[fs] no maps folder: copy the maps folder of the Xbox disc to %s/maps\n", root);
		return;
	}
	while ((entry = readdir(directory)) != NULL)
	{
		struct stat status;

		if (entry->d_name[0] == '.')
			continue;
		if (snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name) >= (int)sizeof(path))
			continue;
		if (stat(path, &status) != 0)
		{
			ps3_log_printf("[fs] %s: stat failed\n", path);
			continue;
		}
		ps3_log_printf("[fs] %-24s %10llu bytes\n", entry->d_name, (unsigned long long)status.st_size);
		count++;
		total += (unsigned long long)status.st_size;
	}
	closedir(directory);
	ps3_log_printf("[fs] %s: %u files, %llu MB\n", folder, count, total >> 20);
}

static void log_button_changes(int port, unsigned previous, unsigned current)
{
	unsigned changed = previous ^ current;
	int bit;

	for (bit = 0; bit < PS3_PAD_BUTTON_COUNT; bit++)
	{
		if (changed & (1u << bit))
			ps3_log_printf("[pad] port %d %s %s\n", port, button_names[bit], (current & (1u << bit)) ? "down" : "up");
	}
}

/* keeps about 100 ms of a 440 Hz tone in the ring */
static void feed_tone(void)
{
	static float chunk[TONE_CHUNK * 2];
	static double phase;
	int frame;

	while (ps3_audio_queued_frames() < PS3_AUDIO_RATE / 10)
	{
		for (frame = 0; frame < TONE_CHUNK; frame++)
		{
			float sample = TONE_LEVEL * (float)sin(phase);

			chunk[frame * 2] = sample;
			chunk[frame * 2 + 1] = sample;
			phase += 2.0 * M_PI * TONE_HZ / PS3_AUDIO_RATE;
			if (phase > 2.0 * M_PI)
				phase -= 2.0 * M_PI;
		}
		if (ps3_audio_write(chunk, TONE_CHUNK) < TONE_CHUNK)
			break;
	}
}

/* a slow blue pulse that shows the flips run; buttons tint the screen */
static unsigned frame_color(unsigned frame, unsigned buttons)
{
	unsigned level = frame & 0xff;

	if (buttons & PS3_PAD_CIRCLE)
		return 0xffc02020;
	if (buttons & PS3_PAD_TRIANGLE)
		return 0xff20c020;
	if (buttons & PS3_PAD_SQUARE)
		return 0xffc020c0;
	if (buttons & PS3_PAD_CROSS)
		return 0xff2040ff;
	if (frame & 0x100)
		level = 0xff - level;
	return 0xff000000u | (level / 8) << 16 | (level / 4) << 8 | (level / 2 + 0x20);
}

static void log_heartbeat(unsigned seconds, unsigned frames)
{
	unsigned queued, completed, timeouts, silent, partial;
	char pads[PS3_PAD_PORTS * 48];
	size_t used = 0;
	int port;

	ps3_video_stats(&queued, &completed, &timeouts);
	ps3_audio_stats(&silent, &partial);
	pads[0] = '\0';
	for (port = 0; port < PS3_PAD_PORTS && used < sizeof(pads); port++)
	{
		const struct ps3_pad *pad = ps3_input_pad(port);

		if (!pad->connected)
			continue;
		used += (size_t)snprintf(pads + used, sizeof(pads) - used, " pad%d=%04x L%u,%u R%u,%u L2=%u R2=%u", port,
			pad->buttons, pad->stick[PS3_PAD_STICK_LEFT_X], pad->stick[PS3_PAD_STICK_LEFT_Y],
			pad->stick[PS3_PAD_STICK_RIGHT_X], pad->stick[PS3_PAD_STICK_RIGHT_Y],
			pad->pressure[PS3_PAD_PRESSURE_L2], pad->pressure[PS3_PAD_PRESSURE_R2]);
	}
	ps3_log_printf("[hb] t=%us fps=%u flips=%u/%u waits=%u audio silent=%u partial=%u%s\n", seconds, frames,
		completed, queued, timeouts, silent, partial, pads[0] ? pads : " no pads");
}

int main(int argc, char *argv[])
{
	static const union { unsigned short value; unsigned char bytes[2]; } byte_order = { 0x0102 };
	unsigned previous_buttons[PS3_PAD_PORTS];
	unsigned frame = 0, frames_this_second = 0, seconds = 0;
	double next_heartbeat;
	int port, video, audio;

	(void)argc;
	(void)argv;
	ps3_system_init();
	ps3_log_printf("[ps3] Halo CE PS3 bring-up, built " __DATE__ " " __TIME__ "\n");
	/* the ABI facts the game's port depends on: LP64, big-endian, 4-byte wchar_t */
	ps3_log_printf("[ps3] sizeof void* %u, long %u, wchar_t %u; %s-endian\n", (unsigned)sizeof(void *),
		(unsigned)sizeof(long), (unsigned)sizeof(wchar_t), byte_order.bytes[0] == 1 ? "big" : "little");
	ps3_memory_report("boot");
	ps3_log_printf("[fs] data root %s\n", ps3_data_root());
	log_maps();

	video = ps3_video_init();
	ps3_memory_report("after the RSX init");
	ps3_input_init();
	audio = ps3_audio_init();
	ps3_memory_report("after the audio init");
	ps3_log_printf("[ps3] video %s, audio %s; hold CROSS for a 440 Hz tone, PS button > Quit Game to exit\n",
		video ? "on" : "FAILED", audio ? "on" : "FAILED");

	memset(previous_buttons, 0, sizeof(previous_buttons));
	next_heartbeat = ps3_time_seconds() + 1.0;
	while (!ps3_exit_requested())
	{
		unsigned all_buttons = 0;

		ps3_input_poll();
		for (port = 0; port < PS3_PAD_PORTS; port++)
		{
			const struct ps3_pad *pad = ps3_input_pad(port);

			if (pad->buttons != previous_buttons[port])
				log_button_changes(port, previous_buttons[port], pad->buttons);
			previous_buttons[port] = pad->buttons;
			all_buttons |= pad->buttons;
		}
		if (audio && (all_buttons & PS3_PAD_CROSS))
			feed_tone();

		ps3_video_begin_frame();
		ps3_video_clear(frame_color(frame, all_buttons));
		ps3_video_end_frame();
		frame++;
		frames_this_second++;

		if (ps3_time_seconds() >= next_heartbeat)
		{
			log_heartbeat(++seconds, frames_this_second);
			frames_this_second = 0;
			next_heartbeat += 1.0;
		}
		/* without video nothing paces the loop */
		if (!video)
			usleep(16 * 1000);
	}

	ps3_log_printf("[ps3] XMB exit after %u frames\n", frame);
	ps3_audio_shutdown();
	ps3_input_shutdown();
	ps3_video_shutdown();
	ps3_system_shutdown();
	return 0;
}
