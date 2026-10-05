/* The audio port setup and feeder thread of xashPS3 s_ps3.c and ioQuake3-PS3
ps3_snd.c. The feeder only copies: mixing there misses the 5.3 ms block deadline. */

#include "ps3_audio.h"
#include "ps3_system.h"

#include <stdint.h>
#include <string.h>

#include <audio/audio.h>
#include <sys/event_queue.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>

#define CHANNELS 2
#define BLOCK_FRAMES 256
/* a power of two; 16384 frames are 341 ms */
#define RING_FRAMES 16384

static audioPortConfig port_config;
static u32 port_number;
static sys_event_queue_t event_queue;
static sys_ipc_key_t queue_key;
static sys_ppu_thread_t feeder_thread;
static volatile int feeder_quit;
static int running;

/* one writer (ps3_audio_write) and one reader (the feeder): free-running counters */
static float ring[RING_FRAMES * CHANNELS] __attribute__((aligned(16)));
static volatile unsigned ring_write;
static volatile unsigned ring_read;

static volatile unsigned silent_blocks;
static volatile unsigned partial_blocks;

static void feeder_main(void *argument)
{
	/* audioDataStart is not always 16-byte aligned: fill an aligned block, then copy */
	static float staging[BLOCK_FRAMES * CHANNELS] __attribute__((aligned(16)));
	volatile u64 *read_index = (volatile u64 *)(uintptr_t)port_config.readIndex;
	float *port_data = (float *)(uintptr_t)port_config.audioDataStart;
	u64 block_count = port_config.numBlocks;
	sys_event_t event;

	(void)argument;
	while (!feeder_quit)
	{
		unsigned available, frames, frame;
		u64 block;

		if (sysEventQueueReceive(event_queue, &event, 20 * 1000) != 0)
			continue;
		if (feeder_quit)
			break;
		block = (*read_index + 1) % block_count;
		available = ring_write - ring_read;
		/* read the samples only after the index that covers them */
		__sync_synchronize();
		frames = available < BLOCK_FRAMES ? available : BLOCK_FRAMES;
		for (frame = 0; frame < frames; frame++)
		{
			unsigned slot = (ring_read + frame) & (RING_FRAMES - 1);

			staging[frame * CHANNELS] = ring[slot * CHANNELS];
			staging[frame * CHANNELS + 1] = ring[slot * CHANNELS + 1];
		}
		if (frames < BLOCK_FRAMES)
		{
			memset(&staging[frames * CHANNELS], 0, (BLOCK_FRAMES - frames) * CHANNELS * sizeof(float));
			if (frames)
				partial_blocks++;
			else
				silent_blocks++;
		}
		memcpy(port_data + block * BLOCK_FRAMES * CHANNELS, staging, sizeof(staging));
		__sync_synchronize();
		ring_read += frames;
	}
	sysThreadExit(0);
}

unsigned ps3_audio_queued_frames(void)
{
	return ring_write - ring_read;
}

unsigned ps3_audio_write(const float *frames, unsigned count)
{
	unsigned space = RING_FRAMES - (ring_write - ring_read);
	unsigned frame;

	if (!running)
		return 0;
	if (count > space)
		count = space;
	for (frame = 0; frame < count; frame++)
	{
		unsigned slot = (ring_write + frame) & (RING_FRAMES - 1);

		ring[slot * CHANNELS] = frames[frame * CHANNELS];
		ring[slot * CHANNELS + 1] = frames[frame * CHANNELS + 1];
	}
	/* the samples must be visible before the feeder sees the new index */
	__sync_synchronize();
	ring_write += count;
	return count;
}

void ps3_audio_stats(unsigned *out_silent, unsigned *out_partial)
{
	*out_silent = silent_blocks;
	*out_partial = partial_blocks;
}

static void close_port(int stage)
{
	if (stage >= 3)
		audioRemoveNotifyEventQueue(queue_key);
	if (stage >= 2)
		sysEventQueueDestroy(event_queue, 0);
	if (stage >= 1)
		audioPortClose(port_number);
	audioQuit();
}

int ps3_audio_init(void)
{
	static char feeder_name[] = "halo_audio";
	audioPortParam parameters;
	s32 result;

	result = sysModuleLoad(SYSMODULE_AUDIO);
	if (result != 0 && result != (s32)SYSMODULE_ERR_DUPLICATE)
	{
		ps3_log_printf("[aud] sysModuleLoad(AUDIO)=0x%08x\n", (unsigned)result);
		return 0;
	}
	result = audioInit();
	if (result != 0)
	{
		ps3_log_printf("[aud] audioInit=0x%08x\n", (unsigned)result);
		return 0;
	}
	memset(&parameters, 0, sizeof(parameters));
	parameters.numChannels = AUDIO_PORT_2CH;
	parameters.numBlocks = AUDIO_BLOCK_8;
	parameters.attrib = AUDIO_PORT_INITLEVEL;
	parameters.level = 1.0f;
	result = audioPortOpen(&parameters, &port_number);
	if (result != 0)
	{
		ps3_log_printf("[aud] audioPortOpen=0x%08x\n", (unsigned)result);
		close_port(0);
		return 0;
	}
	result = audioGetPortConfig(port_number, &port_config);
	if (result != 0)
	{
		ps3_log_printf("[aud] audioGetPortConfig=0x%08x\n", (unsigned)result);
		close_port(1);
		return 0;
	}
	result = audioCreateNotifyEventQueue(&event_queue, &queue_key);
	if (result != 0)
	{
		ps3_log_printf("[aud] audioCreateNotifyEventQueue=0x%08x\n", (unsigned)result);
		close_port(1);
		return 0;
	}
	result = audioSetNotifyEventQueue(queue_key);
	if (result != 0)
	{
		ps3_log_printf("[aud] audioSetNotifyEventQueue=0x%08x\n", (unsigned)result);
		close_port(2);
		return 0;
	}
	sysEventQueueDrain(event_queue);
	result = audioPortStart(port_number);
	if (result != 0)
	{
		ps3_log_printf("[aud] audioPortStart=0x%08x\n", (unsigned)result);
		close_port(3);
		return 0;
	}

	ring_write = ring_read = 0;
	feeder_quit = 0;
	running = 1;
	/* priority 100, above the main thread: a late block clicks */
	result = sysThreadCreate(&feeder_thread, feeder_main, NULL, 100, 0x4000, THREAD_JOINABLE, feeder_name);
	if (result != 0)
	{
		ps3_log_printf("[aud] sysThreadCreate=0x%08x\n", (unsigned)result);
		running = 0;
		audioPortStop(port_number);
		close_port(3);
		return 0;
	}
	ps3_log_printf("[aud] port %u: %u blocks of %d frames at %d Hz, data at 0x%08x\n",
		(unsigned)port_number, (unsigned)port_config.numBlocks, BLOCK_FRAMES, PS3_AUDIO_RATE,
		(unsigned)port_config.audioDataStart);
	return 1;
}

void ps3_audio_shutdown(void)
{
	u64 result;

	if (!running)
		return;
	running = 0;
	feeder_quit = 1;
	sysThreadJoin(feeder_thread, &result);
	audioPortStop(port_number);
	close_port(3);
	ps3_log_printf("[aud] shut down\n");
}
