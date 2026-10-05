/* RSX bring-up as in xashPS3 vid_ps3.c and ioQuake3-PS3 ps3_glimp.c; the
comments give the hardware results behind each rule. */

#include "ps3_video.h"
#include "ps3_system.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

/* the 64 KB rsx.h default command buffer failed on hardware */
#define COMMAND_BUFFER_SIZE (1 * 1024 * 1024)
/* RSX IO mapping is in 1 MB pages; this buffer holds only the command buffer */
#define HOST_BUFFER_SIZE (8 * 1024 * 1024)
/* with two buffers, a late frame waits a whole extra vsync */
#define BUFFER_COUNT 3
#define WAIT_LIMIT 20000

static gcmContextData *context;
static void *host_buffer;
static u32 width = 1280, height = 720, pitch;
static void *color_buffer[BUFFER_COUNT];
static u32 color_offset[BUFFER_COUNT];
static void *depth_buffer;
static u32 depth_offset;
static int current_buffer;
static int ready;
static u32 finish_reference;
static unsigned wait_timeouts;

/* a flip handler, not gcmGetFlipStatus polling: polling read "done" at once on hardware */
static volatile u32 flips_queued;
static volatile u32 flips_completed;

static void flip_handler(const u32 head)
{
	(void)head;
	flips_completed++;
}

static void set_render_target(int index)
{
	gcmSurface surface;
	int slot;

	memset(&surface, 0, sizeof(surface));
	surface.colorFormat = GCM_SURFACE_A8R8G8B8;
	surface.colorTarget = GCM_SURFACE_TARGET_0;
	surface.colorLocation[0] = GCM_LOCATION_RSX;
	surface.colorOffset[0] = color_offset[index];
	surface.colorPitch[0] = pitch;
	/* MRT slots 1-3 must be filled even when unused: a black screen on hardware otherwise */
	for (slot = 1; slot < 4; slot++)
	{
		surface.colorLocation[slot] = GCM_LOCATION_RSX;
		surface.colorOffset[slot] = color_offset[index];
		surface.colorPitch[slot] = 64;
	}
	surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
	surface.depthLocation = GCM_LOCATION_RSX;
	surface.depthOffset = depth_offset;
	surface.depthPitch = width * 4;
	surface.type = GCM_SURFACE_TYPE_LINEAR;
	surface.antiAlias = GCM_SURFACE_CENTER_1;
	surface.width = width;
	surface.height = height;
	surface.x = 0;
	surface.y = 0;
	rsxSetSurface(context, &surface);
}

int ps3_video_init(void)
{
	videoResolution resolution;
	videoConfiguration configuration;
	s32 resolution_id = VIDEO_RESOLUTION_720;
	u8 aspect = VIDEO_ASPECT_16_9;
	s32 result;
	int index;

	if (ready)
		return 1;

	host_buffer = memalign(1024 * 1024, HOST_BUFFER_SIZE);
	if (!host_buffer)
	{
		ps3_log_printf("[vid] memalign of the RSX host buffer failed\n");
		return 0;
	}
	result = rsxInit(&context, COMMAND_BUFFER_SIZE, HOST_BUFFER_SIZE, host_buffer);
	ps3_log_printf("[vid] rsxInit=%d context=%p\n", (int)result, (void *)context);
	if (result != 0 || !context)
		return 0;

	/* 720p for fill rate (1080p was fill-bound in xashPS3); else the TV's own mode */
	if (!videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_16_9, 0))
	{
		videoState state;

		if (videoGetState(0, 0, &state) != 0)
		{
			ps3_log_printf("[vid] videoGetState failed\n");
			return 0;
		}
		resolution_id = state.displayMode.resolution;
		aspect = state.displayMode.aspect;
	}
	if (videoGetResolution(resolution_id, &resolution) != 0)
	{
		ps3_log_printf("[vid] videoGetResolution(%d) failed\n", (int)resolution_id);
		return 0;
	}
	width = resolution.width;
	height = resolution.height;
	pitch = width * 4;

	memset(&configuration, 0, sizeof(configuration));
	configuration.resolution = resolution_id;
	configuration.format = VIDEO_BUFFER_FORMAT_XRGB;
	configuration.pitch = pitch;
	configuration.aspect = aspect;
	result = videoConfigure(0, &configuration, NULL, 0);
	ps3_log_printf("[vid] %ux%u resolution id %d aspect %u videoConfigure=%d\n",
		(unsigned)width, (unsigned)height, (int)resolution_id, (unsigned)aspect, (int)result);

	for (index = 0; index < BUFFER_COUNT; index++)
	{
		color_buffer[index] = rsxMemalign(64, pitch * height);
		if (!color_buffer[index])
		{
			ps3_log_printf("[vid] rsxMemalign of color buffer %d failed\n", index);
			return 0;
		}
		rsxAddressToOffset(color_buffer[index], &color_offset[index]);
		result = gcmSetDisplayBuffer(index, color_offset[index], pitch, width, height);
		ps3_log_printf("[vid] display buffer %d offset 0x%x gcmSetDisplayBuffer=%d\n",
			index, (unsigned)color_offset[index], (int)result);
	}
	depth_buffer = rsxMemalign(64, width * height * 4);
	if (!depth_buffer)
	{
		ps3_log_printf("[vid] rsxMemalign of the depth buffer failed\n");
		return 0;
	}
	rsxAddressToOffset(depth_buffer, &depth_offset);

	gcmSetFlipMode(GCM_FLIP_VSYNC);
	flips_queued = 0;
	flips_completed = 0;
	gcmSetFlipHandler(flip_handler);
	current_buffer = 0;
	ready = 1;
	ps3_log_printf("[vid] ready\n");
	return 1;
}

void ps3_video_shutdown(void)
{
	int index;

	if (ready)
	{
		/* a new reference each time: a repeated value does not wait again */
		rsxFinish(context, ++finish_reference);
		/* a flip interrupt after the teardown would run on freed state */
		gcmSetFlipHandler(NULL);
		for (index = 0; index < BUFFER_COUNT; index++)
		{
			if (color_buffer[index])
				rsxFree(color_buffer[index]);
			color_buffer[index] = NULL;
		}
		if (depth_buffer)
			rsxFree(depth_buffer);
		depth_buffer = NULL;
		ready = 0;
		ps3_log_printf("[vid] shut down\n");
	}
	if (host_buffer)
	{
		free(host_buffer);
		host_buffer = NULL;
	}
}

gcmContextData *ps3_video_context(void)
{
	return ready ? context : NULL;
}

void ps3_video_size(unsigned *out_width, unsigned *out_height)
{
	*out_width = width;
	*out_height = height;
}

void ps3_video_begin_frame(void)
{
	unsigned waited = 0;

	if (!ready)
		return;
	while ((int)(flips_queued - flips_completed) > BUFFER_COUNT - 2)
	{
		usleep(100);
		/* resync, else every later frame waits for a flip that never comes */
		if (++waited > WAIT_LIMIT)
		{
			flips_completed = flips_queued;
			wait_timeouts++;
			break;
		}
	}
	set_render_target(current_buffer);
}

void ps3_video_clear(unsigned argb)
{
	if (!ready)
		return;
	rsxSetColorMask(context, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
	rsxSetDepthWriteEnable(context, GCM_TRUE);
	rsxSetScissor(context, 0, 0, (u16)width, (u16)height);
	rsxSetClearColor(context, argb);
	rsxSetClearDepthStencil(context, 0xffffff00);
	rsxClearSurface(context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_Z | GCM_CLEAR_S);
}

void ps3_video_end_frame(void)
{
	if (!ready)
		return;
	/* WaitFlip before SetFlip: the order of the rsx.h comment gave a black screen on hardware */
	gcmSetWaitFlip(context);
	gcmSetFlip(context, (u8)current_buffer);
	rsxFlushBuffer(context);
	flips_queued++;
	current_buffer = (current_buffer + 1) % BUFFER_COUNT;
}

void ps3_video_stats(unsigned *out_queued, unsigned *out_completed, unsigned *out_timeouts)
{
	*out_queued = flips_queued;
	*out_completed = flips_completed;
	*out_timeouts = wait_timeouts;
}
