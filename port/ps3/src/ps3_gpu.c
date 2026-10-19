/* The RSX behind the Direct3D device, with the hardware-proven patterns of ioQuake3-PS3 and xashPS3's
ps3gl. One thread calls it at a time: the Direct3D thread, or the game's while that one is idle (d3d8_rsx.c). */

#include "ps3_gpu.h"
#include "ps3_system.h"
#include "ps3_timers.h"
#include "ps3_video.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

/* one half per frame in flight; a half is reused when the RSX has finished its last frame */
#define SCRATCH_HALF_SIZE (16 * 1024 * 1024)
/* labels below 64 are the system's; 66 as ioQuake3-PS3's second ring label */
#define FRAME_LABEL 64
#define IDLE_LABEL 66
#define FENCE_TIMEOUT_SECONDS 2.0
#define DEFERRED_FREE_COUNT 4096
#define TEXTURE_UNITS 4
#define ATTRIBUTE_COUNT 16
#define REMAP_IDENTITY 0xaae4
#define STATISTICS_FRAMES 600

/* the screen's targets in tiled regions, the depth Z-compressed and ZCull-ed (PSL1GHT's rsxtest_tile sample);
0 keeps every target linear */
#define RSX_TILED_TARGETS 1
/* regions 0 to 2 are the display buffers' (ps3_video.c) */
#define FIRST_TARGET_TILE 3
#define TILE_COUNT 15
#define ZCULL_INDEX 0
/* each region's compression tags, apart: a region of size bytes takes size / 64 KB of them */
#define TILE_TAG_SPACING 0x80

/* vertex programs kept in the RSX's 512 slots, switched without an upload; 0 loads each at slot 0 */
#define RSX_RESIDENT_PROGRAMS 1
#define PROGRAM_SLOTS 512
#define RESIDENT_PROGRAM_COUNT 32

/* the RSX's time at each split of the frame (ps3_gpu_timestamp): report slots, three frames' worth (the
reports of the frame before last are read) */
#define TIMESTAMP_REPORT_BASE 256
#define TIMESTAMP_PARTS 32
/* each report holds the RSX's time; the Z-pass type as nouveau's NV40 timestamps (rsxSetTimeStamp's type 0x10 and
misplaced word stopped the RSX on hardware) */
#define REPORT_TIMESTAMP GCM_ZPASS_PIXEL_CNT

/* the G8 diagnostic: a command label and a back-end label after each kind of command, so a hung RSX shows the last
one its front end took and its back end finished */
#define RSX_CHECKPOINTS 0
#define CHECKPOINT_LABEL 67
#define CHECKPOINT_BACKEND_LABEL 68
#define CHECKPOINT_RING 65536

enum
{
	_checkpoint_surface,
	_checkpoint_surface_zcull,
	_checkpoint_clear,
	_checkpoint_zcull_clear,
	_checkpoint_zcull_invalidate,
	_checkpoint_zcull_control,
	_checkpoint_state,
	_checkpoint_vertex_upload,
	_checkpoint_vertex_switch,
	_checkpoint_vertex_constants,
	_checkpoint_fragment,
	_checkpoint_texture,
	_checkpoint_texture_remap,
	_checkpoint_texture_off,
	_checkpoint_attribute,
	_checkpoint_draw,
	_checkpoint_draw_indexed,
	_checkpoint_timestamp,
	_checkpoint_blit,
	_checkpoint_frame_label,
	_checkpoint_flip,
	_checkpoint_start,
	_checkpoint_count
};

#if RSX_CHECKPOINTS
static const char *const checkpoint_names[_checkpoint_count] =
{
	"surface", "surface with ZCull", "clear", "ZCull clear", "ZCull invalidate", "ZCull control", "state",
	"vertex program upload", "vertex program switch", "vertex constants", "fragment program", "texture",
	"texture with a remap", "texture off", "vertex attribute", "draw", "indexed draw", "timestamp report",
	"present blit", "frame label", "flip", "game's first command",
};

static uint32_t checkpoint_serial;
static uint8_t checkpoint_kinds[CHECKPOINT_RING];
static volatile uint32_t *checkpoint_label;
static volatile uint32_t *checkpoint_backend_label;
#endif

static gcmContextData *context;
static int ready;

static struct
{
	uint8_t *base;
	uint32_t offset;
	uint32_t head;
	uint32_t peak;
	uint32_t drops;
} scratch;

/* the last frame the RSX has finished: written by the RSX after each frame's commands */
static volatile uint32_t *frame_label;
static uint32_t frame_index = 1;
static uint32_t finish_reference = 0x80000000u;
static int fence_timeouts;
/* written by the RSX at ps3_gpu_wait_idle's mark */
static volatile uint32_t *idle_label;
static uint32_t idle_reference;

static struct
{
	void *memory;
	uint32_t frame;
	uint32_t size;
	int kind;
} deferred_frees[DEFERRED_FREE_COUNT];
static uint32_t deferred_free_count;

/* the blocks in use by kind (ps3_gpu_memory_usage); the display thread reads them */
static struct
{
	uint64_t local_size;
	uint64_t used;
	uint64_t peak;
	uint64_t bytes[_ps3_gpu_memory_count];
	uint32_t counts[_ps3_gpu_memory_count];
	uint32_t refused;
} rsx_memory;

/* what the RSX holds; reset at each frame, which the system's overlays can change */
static struct ps3_gpu_state state;
static int state_valid;
static struct ps3_gpu_surface surface;
static int surface_valid;
static uint32_t viewport_size[2];
static float viewport_depth[2];
static int viewport_valid;
static const struct rsx_vertex_program *vertex_program;
static struct
{
	const struct rsx_fragment_program *program;
	float values[RSX_FP_MAXIMUM_UNIFORMS][4];
} fragment;
static struct ps3_gpu_texture textures[TEXTURE_UNITS];
/* 0 unknown, 1 off, 2 on */
static uint8_t texture_states[TEXTURE_UNITS];
static int texture_cache_dirty;
static int vertex_cache_dirty;
static uint8_t attribute_enabled[ATTRIBUTE_COUNT];

static uint32_t fragment_staging[RSX_FP_MAXIMUM_INSTRUCTIONS * 8];

/* the tiled regions of the targets (ps3_gpu_target_allocate) */
static struct
{
	void *memory;
	uint32_t bytes;
	int zcull;
} tiles[TILE_COUNT];
/* the depth surface ZCull covers and its size, while bound; and whether its data is good until the next full clear */
static uint32_t zcull_offset;
static uint32_t zcull_width;
static uint32_t zcull_height;
static int zcull_bound;
static int zcull_valid;

static struct
{
	const struct rsx_vertex_program *program;
	uint32_t start;
	uint32_t count;
	uint32_t used;
} resident_programs[RESIDENT_PROGRAM_COUNT];
static uint32_t program_clock;
static unsigned overlay_events;

/* the parts stamped in each of three frames, in order */
static struct
{
	uint8_t parts[TIMESTAMP_PARTS];
	uint32_t count;
} stamps[3];

static void timestamps_read(void);

/* (the G8 diagnostic) the RSX writes the checkpoint's number once it has taken the commands before it (the command
label) and once it has finished them (the back-end label) */
static void checkpoint(int kind)
{
#if RSX_CHECKPOINTS
	if (!checkpoint_label)
		return;
	checkpoint_serial++;
	checkpoint_kinds[checkpoint_serial % CHECKPOINT_RING] = (uint8_t)kind;
	rsxSetWriteCommandLabel(context, CHECKPOINT_LABEL, checkpoint_serial);
	rsxSetWriteBackendLabel(context, CHECKPOINT_BACKEND_LABEL, checkpoint_serial);
#else
	(void)kind;
#endif
}

#if RSX_CHECKPOINTS
static const char *checkpoint_name(uint32_t serial)
{
	if (!serial)
		return "start";
	if (serial > checkpoint_serial)
		return "none";
	if (checkpoint_serial - serial >= CHECKPOINT_RING)
		return "overwritten";
	return checkpoint_names[checkpoint_kinds[serial % CHECKPOINT_RING]];
}
#endif

/* the last checkpoints the RSX took and finished, the ones around them, its FIFO's pointers and the flips */
static void checkpoint_report(void)
{
#if RSX_CHECKPOINTS
	gcmControlRegister *control = gcmGetControlRegister();
	uint32_t front = checkpoint_label ? *checkpoint_label : 0;
	uint32_t back = checkpoint_backend_label ? *checkpoint_backend_label : 0;
	uint32_t serial, first = back > 3 ? back - 3 : 1, last = front + 2;
	char line[640];
	size_t used = 0;
	int tile;

	ps3_log_printf("[gpu] checkpoints: %u written; the front end took %u (the %s; next the %s), the back end finished %u "
		"(the %s; next the %s)\n", (unsigned)checkpoint_serial, (unsigned)front, checkpoint_name(front),
		checkpoint_name(front + 1), (unsigned)back, checkpoint_name(back), checkpoint_name(back + 1));
	if (last > checkpoint_serial)
		last = checkpoint_serial;
	if (last > first + 15)
		last = first + 15;
	line[0] = 0;
	for (serial = first; serial <= last && used < sizeof(line) - 80; serial++)
		used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%u %s%s", serial == first ? "" : ", ",
			(unsigned)serial, checkpoint_name(serial), serial == back && serial == front ? " (both ends)" :
			serial == back ? " (back end)" : serial == front ? " (front end)" : "");
	ps3_log_printf("[gpu] checkpoints around them: %s\n", line);
	ps3_log_printf("[gpu] FIFO get 0x%x, put 0x%x, ref 0x%x; frame %u; ZCull %s at 0x%x (%ux%u)\n",
		control ? (unsigned)control->get : 0, control ? (unsigned)control->put : 0, control ? (unsigned)control->ref : 0,
		(unsigned)frame_index, zcull_bound ? "bound" : "not bound", (unsigned)zcull_offset, (unsigned)zcull_width,
		(unsigned)zcull_height);
	for (tile = FIRST_TARGET_TILE; tile < TILE_COUNT; tile++)
	{
		if (tiles[tile].memory)
			ps3_log_printf("[gpu] tiled region %d: %u bytes%s\n", tile, (unsigned)tiles[tile].bytes,
				tiles[tile].zcull ? ", ZCull" : "");
	}
	ps3_video_flip_report();
#endif
}

static struct
{
	struct rsx_vertex_program vertex;
	struct rsx_fragment_program fragment;
	uint32_t vertex_words[8];
	uint32_t fragment_offset;
} blit;

static struct
{
	uint32_t draws;
	uint32_t vertex_uploads;
	uint32_t vertex_switches;
	uint32_t fragment_copies;
	uint32_t texture_loads;
} statistics;

/* ---------- memory */

void *ps3_gpu_memory_allocate(uint32_t size, uint32_t alignment, int kind, uint32_t *offset)
{
	void *memory = rsxMemalign(alignment < 64 ? 64 : alignment, size);

	if (!memory)
	{
		rsx_memory.refused++;
		return NULL;
	}
	rsxAddressToOffset(memory, offset);
	rsx_memory.bytes[kind] += size;
	rsx_memory.counts[kind]++;
	rsx_memory.used += size;
	if (rsx_memory.used > rsx_memory.peak)
		rsx_memory.peak = rsx_memory.used;
	return memory;
}

static void memory_release(void *memory, uint32_t size, int kind)
{
	rsxFree(memory);
	rsx_memory.bytes[kind] -= size;
	rsx_memory.counts[kind]--;
	rsx_memory.used -= size;
}

void ps3_gpu_memory_usage(struct ps3_gpu_memory_usage *usage)
{
	int kind;

	usage->heap = rsx_memory.local_size;
	usage->display = ps3_video_memory_bytes();
	usage->used = rsx_memory.used;
	usage->peak = rsx_memory.peak;
	for (kind = 0; kind < _ps3_gpu_memory_count; kind++)
	{
		usage->bytes[kind] = rsx_memory.bytes[kind];
		usage->counts[kind] = rsx_memory.counts[kind];
	}
	usage->refused = rsx_memory.refused;
}

static int frame_finished(uint32_t frame)
{
	return (int32_t)(*frame_label - frame) >= 0;
}

/* all the commands so far, done */
static void finish(void)
{
	rsxFinish(context, ++finish_reference);
}

static void deferred_frees_run(int all)
{
	uint32_t index = 0;

	while (index < deferred_free_count)
	{
		if (all || frame_finished(deferred_frees[index].frame))
		{
			memory_release(deferred_frees[index].memory, deferred_frees[index].size, deferred_frees[index].kind);
			deferred_frees[index] = deferred_frees[--deferred_free_count];
		}
		else
		{
			index++;
		}
	}
}

void ps3_gpu_memory_free(void *memory, uint32_t size, int kind)
{
	if (!memory)
		return;
	if (!ready)
	{
		memory_release(memory, size, kind);
		return;
	}
	if (deferred_free_count == DEFERRED_FREE_COUNT)
	{
		finish();
		deferred_frees_run(1);
	}
	deferred_frees[deferred_free_count].memory = memory;
	deferred_frees[deferred_free_count].frame = frame_index;
	deferred_frees[deferred_free_count].size = size;
	deferred_frees[deferred_free_count].kind = kind;
	deferred_free_count++;
}

/* a target: tiled where asked and a region is free (64 KB aligned, a tiled pitch, 32-line rows), the depth with Z
compression and, once, ZCull; else linear */
void *ps3_gpu_target_allocate(uint32_t width, uint32_t height, int depth, int tiled, int zcull, uint32_t *offset,
	uint32_t *pitch, uint32_t *bytes)
{
	void *memory;

#if RSX_TILED_TARGETS
	if (tiled && ready)
	{
		uint32_t tiled_pitch = gcmGetTiledPitchSize(width * 4);
		uint32_t size = (tiled_pitch * ((height + GCM_TILE_LOCAL_ALIGN_HEIGHT - 1) & ~(GCM_TILE_LOCAL_ALIGN_HEIGHT - 1)) +
			GCM_TILE_ALIGN_SIZE - 1) & ~(GCM_TILE_ALIGN_SIZE - 1);
		int tile;

		for (tile = FIRST_TARGET_TILE; tile < TILE_COUNT && tiles[tile].memory; tile++)
			;
		if (tile < TILE_COUNT && tiled_pitch &&
			(memory = ps3_gpu_memory_allocate(size, GCM_TILE_ALIGN_OFFSET, _ps3_gpu_memory_target, offset)) != NULL)
		{
			if (gcmSetTileInfo((u8)tile, GCM_LOCATION_RSX, *offset, size, tiled_pitch,
				depth ? GCM_COMPMODE_Z32_SEPSTENCIL : GCM_COMPMODE_DISABLED,
				(u16)((tile - FIRST_TARGET_TILE) * TILE_TAG_SPACING), depth ? 2 : 0) == 0 && gcmBindTile((u8)tile) == 0)
			{
				tiles[tile].memory = memory;
				tiles[tile].bytes = size;
				tiles[tile].zcull = 0;
				if (zcull && depth && !zcull_bound &&
					gcmBindZcull(ZCULL_INDEX, *offset, (width + GCM_ZCULL_ALIGN_WIDTH - 1) & ~(GCM_ZCULL_ALIGN_WIDTH - 1),
					(height + GCM_ZCULL_ALIGN_HEIGHT - 1) & ~(GCM_ZCULL_ALIGN_HEIGHT - 1), 0, GCM_ZCULL_Z24S8,
					GCM_SURFACE_CENTER_1, GCM_ZCULL_LESS, GCM_ZCULL_LONES, GCM_SCULL_SFUNC_LESS, 1, 0xff) == 0)
				{
					tiles[tile].zcull = 1;
					zcull_bound = 1;
					zcull_offset = *offset;
					zcull_width = width;
					zcull_height = height;
					zcull_valid = 0;
				}
				ps3_log_printf("[gpu] target %ux%u%s tiled (region %d, pitch %u)%s\n", (unsigned)width, (unsigned)height,
					depth ? " depth" : "", tile, (unsigned)tiled_pitch, tiles[tile].zcull ? ", ZCull" : "");
				*pitch = tiled_pitch;
				*bytes = size;
				return memory;
			}
			memory_release(memory, size, _ps3_gpu_memory_target);
		}
	}
#endif
	*pitch = (width * 4 + 63) & ~63u;
	*bytes = *pitch * height;
	return ps3_gpu_memory_allocate(*bytes, 128, _ps3_gpu_memory_target, offset);
}

/* a tiled target's region goes when the RSX is idle (a screen change: rare), others as frees do */
void ps3_gpu_target_free(void *memory, uint32_t bytes)
{
	int tile;

	if (!memory)
		return;
	for (tile = FIRST_TARGET_TILE; tile < TILE_COUNT && tiles[tile].memory != memory; tile++)
		;
	if (tile == TILE_COUNT)
	{
		ps3_gpu_memory_free(memory, bytes, _ps3_gpu_memory_target);
		return;
	}
	ps3_gpu_wait_idle();
	if (tiles[tile].zcull)
	{
		gcmUnbindZcull(ZCULL_INDEX);
		zcull_bound = 0;
		zcull_offset = 0;
	}
	gcmUnbindTile((u8)tile);
	memory_release(memory, tiles[tile].bytes, _ps3_gpu_memory_target);
	memset(&tiles[tile], 0, sizeof(tiles[tile]));
	/* (the surface bound is unknown now) */
	surface_valid = 0;
}

void ps3_gpu_display(uint32_t *width, uint32_t *height, int *widescreen)
{
	unsigned display_width, display_height;

	ps3_video_size(&display_width, &display_height);
	*width = display_width;
	*height = display_height;
	*widescreen = ps3_video_widescreen();
}

void *ps3_gpu_scratch(uint32_t size, uint32_t alignment, uint32_t *offset)
{
	uint32_t half = (frame_index & 1) * SCRATCH_HALF_SIZE;
	uint32_t head = (scratch.head + alignment - 1) & ~(alignment - 1);

	if (!ready || head + size > SCRATCH_HALF_SIZE)
	{
		scratch.drops++;
		return NULL;
	}
	scratch.head = head + size;
	if (scratch.head > scratch.peak)
		scratch.peak = scratch.head;
	*offset = scratch.offset + half + head;
	return scratch.base + half + head;
}

/* ---------- caches */

static void caches_invalidate(void)
{
	state_valid = 0;
	surface_valid = 0;
	viewport_valid = 0;
	vertex_program = NULL;
	fragment.program = NULL;
	memset(texture_states, 0, sizeof(texture_states));
	/* unknown: every attribute is turned off before its first use */
	memset(attribute_enabled, 1, sizeof(attribute_enabled));
	texture_cache_dirty = 1;
}

/* ---------- render targets */

void ps3_gpu_set_surface(const struct ps3_gpu_surface *target)
{
	gcmSurface description;
	int slot;

	if (surface_valid && !memcmp(target, &surface, sizeof(surface)))
		return;
	surface = *target;
	surface_valid = 1;
	memset(&description, 0, sizeof(description));
	description.colorFormat = GCM_SURFACE_A8R8G8B8;
	description.colorTarget = GCM_SURFACE_TARGET_0;
	/* MRT slots 1-3 filled even when unused, as ps3_video.c (a black screen otherwise) */
	for (slot = 0; slot < 4; slot++)
	{
		description.colorLocation[slot] = GCM_LOCATION_RSX;
		description.colorOffset[slot] = target->color_offset;
		description.colorPitch[slot] = slot ? 64 : target->color_pitch;
	}
	description.depthFormat = GCM_SURFACE_ZETA_Z24S8;
	description.depthLocation = GCM_LOCATION_RSX;
	description.depthOffset = target->depth_offset;
	description.depthPitch = target->depth_pitch;
	description.type = GCM_SURFACE_TYPE_LINEAR;
	description.antiAlias = GCM_SURFACE_CENTER_1;
	description.width = target->width;
	description.height = target->height;
	rsxSetSurface(context, &description);
	/* ZCull for the depth it covers only; stencil culling stays off */
	if (zcull_bound)
		rsxSetZCullEnable(context, target->depth_offset == zcull_offset ? GCM_TRUE : GCM_FALSE, GCM_FALSE);
	checkpoint(zcull_bound && target->depth_offset == zcull_offset ? _checkpoint_surface_zcull : _checkpoint_surface);
	viewport_valid = 0;
	/* the targets before can be sampled next */
	texture_cache_dirty = 1;
}

/* the ZCull data is good from a full depth clear until a draw writes depth that is not nearer (its direction) */
static int zcull_surface(void)
{
	return zcull_bound && surface_valid && surface.depth_offset == zcull_offset;
}

static void zcull_invalidate(void)
{
	if (zcull_valid)
	{
		rsxSetZCullInvalidate(context);
		zcull_valid = 0;
		checkpoint(_checkpoint_zcull_invalidate);
	}
}

static void viewport_set(uint32_t width, uint32_t height, float minimum, float maximum)
{
	float scale[4], offset[4];

	if (viewport_valid && viewport_size[0] == width && viewport_size[1] == height &&
		viewport_depth[0] == minimum && viewport_depth[1] == maximum)
		return;
	viewport_valid = 1;
	viewport_size[0] = width;
	viewport_size[1] = height;
	viewport_depth[0] = minimum;
	viewport_depth[1] = maximum;
	/* rows from the top, as ps3gl's (scale y negative) */
	scale[0] = width * 0.5f;
	scale[1] = height * -0.5f;
	scale[2] = (maximum - minimum) * 0.5f;
	scale[3] = 0.0f;
	offset[0] = width * 0.5f;
	offset[1] = height * 0.5f;
	offset[2] = (maximum + minimum) * 0.5f;
	offset[3] = 0.0f;
	rsxSetViewport(context, 0, 0, (u16)width, (u16)height, minimum, maximum, scale, offset);
	rsxSetViewportClip(context, 0, (u16)width, (u16)height);
}

void ps3_gpu_clear(uint32_t flags, uint32_t argb, uint32_t depth_stencil, uint32_t x, uint32_t y, uint32_t width,
	uint32_t height)
{
	uint32_t mask = 0, color_mask = 0;

	if (!ready || !width || !height)
		return;
	if (flags & 0x10) { mask |= GCM_CLEAR_R; color_mask |= GCM_COLOR_MASK_R; }
	if (flags & 0x20) { mask |= GCM_CLEAR_G; color_mask |= GCM_COLOR_MASK_G; }
	if (flags & 0x40) { mask |= GCM_CLEAR_B; color_mask |= GCM_COLOR_MASK_B; }
	if (flags & 0x80) { mask |= GCM_CLEAR_A; color_mask |= GCM_COLOR_MASK_A; }
	if (flags & 0x01)
		mask |= GCM_CLEAR_Z;
	if (flags & 0x02)
		mask |= GCM_CLEAR_S;
	if (!mask)
		return;
	rsxSetScissor(context, (u16)x, (u16)y, (u16)width, (u16)height);
	rsxSetColorMask(context, color_mask);
	rsxSetClearColor(context, argb);
	if (mask & GCM_CLEAR_Z)
		rsxSetDepthWriteEnable(context, GCM_TRUE);
	if (mask & GCM_CLEAR_S)
		rsxSetStencilMask(context, 0xff);
	rsxSetClearDepthStencil(context, depth_stencil);
	rsxClearSurface(context, mask);
	checkpoint(_checkpoint_clear);
	if ((mask & GCM_CLEAR_Z) && zcull_surface())
	{
		/* the whole depth surface: ZCull starts again from it; a part (a split-screen window, a smaller colour
		target's) leaves its data stale */
		if (!x && !y && width >= zcull_width && height >= zcull_height)
		{
			rsxSetClearZCullSurface(context, GCM_TRUE, GCM_FALSE);
			zcull_valid = 1;
			checkpoint(_checkpoint_zcull_clear);
		}
		else
		{
			zcull_invalidate();
		}
	}
	/* the masks and the scissor are the draws' again at the next state */
	state_valid = 0;
}

/* ---------- render state */

void ps3_gpu_set_state(const struct ps3_gpu_state *next)
{
	const struct ps3_gpu_state *old = &state;
	int all = !state_valid;

	if (!ready)
		return;
	if (all || next->color_mask != old->color_mask)
		rsxSetColorMask(context, next->color_mask);
	if (all || next->depth_function != old->depth_function)
	{
		rsxSetDepthTestEnable(context, next->depth_function != 0);
		if (next->depth_function)
			rsxSetDepthFunc(context, next->depth_function);
	}
	if (all || next->depth_write != old->depth_write)
		rsxSetDepthWriteEnable(context, next->depth_write != 0);
	/* a depth write that need not be nearer (GREATER, NOTEQUAL, GEQUAL, ALWAYS) breaks ZCull's farthest depths */
	if (next->depth_write && next->depth_function > GCM_LEQUAL && zcull_surface())
		zcull_invalidate();
	if (all || next->stencil_function != old->stencil_function || next->stencil_reference != old->stencil_reference ||
		next->stencil_mask != old->stencil_mask)
	{
		rsxSetStencilTestEnable(context, next->stencil_function != 0);
		if (next->stencil_function)
			rsxSetStencilFunc(context, next->stencil_function, next->stencil_reference, next->stencil_mask);
	}
	if (all || next->stencil_write_mask != old->stencil_write_mask)
		rsxSetStencilMask(context, next->stencil_write_mask);
	if (all || next->stencil_fail != old->stencil_fail || next->stencil_depth_fail != old->stencil_depth_fail ||
		next->stencil_pass != old->stencil_pass)
		rsxSetStencilOp(context, next->stencil_fail, next->stencil_depth_fail, next->stencil_pass);
	if (all || next->blend != old->blend)
		rsxSetBlendEnable(context, next->blend != 0);
	if (all || next->blend_source != old->blend_source || next->blend_destination != old->blend_destination)
		rsxSetBlendFunc(context, (u16)next->blend_source, (u16)next->blend_destination, (u16)next->blend_source,
			(u16)next->blend_destination);
	if (all || next->blend_equation != old->blend_equation)
		rsxSetBlendEquation(context, (u16)next->blend_equation, (u16)next->blend_equation);
	if (all || next->blend_color != old->blend_color)
		rsxSetBlendColor(context, next->blend_color, 0);
	if (all || next->alpha_function != old->alpha_function || next->alpha_reference != old->alpha_reference)
	{
		rsxSetAlphaTestEnable(context, next->alpha_function != 0);
		if (next->alpha_function)
			rsxSetAlphaFunc(context, next->alpha_function, next->alpha_reference);
	}
	if (all || next->cull_face != old->cull_face)
	{
		rsxSetCullFaceEnable(context, next->cull_face != 0);
		if (next->cull_face)
			rsxSetCullFace(context, next->cull_face);
	}
	if (all || next->front_face != old->front_face)
		rsxSetFrontFace(context, next->front_face);
	if (all || next->polygon_mode != old->polygon_mode)
	{
		rsxSetFrontPolygonMode(context, next->polygon_mode);
		rsxSetBackPolygonMode(context, next->polygon_mode);
	}
	if (all || next->polygon_offset != old->polygon_offset)
	{
		rsxSetPolygonOffsetFillEnable(context, next->polygon_offset != 0);
		rsxSetPolygonOffsetLineEnable(context, next->polygon_offset != 0);
	}
	if (all || next->polygon_offset_factor != old->polygon_offset_factor ||
		next->polygon_offset_units != old->polygon_offset_units)
		rsxSetPolygonOffset(context, next->polygon_offset_factor, next->polygon_offset_units);
	if (all || next->scissor_x != old->scissor_x || next->scissor_y != old->scissor_y ||
		next->scissor_width != old->scissor_width || next->scissor_height != old->scissor_height)
		rsxSetScissor(context, next->scissor_x, next->scissor_y, next->scissor_width, next->scissor_height);
	state = *next;
	state_valid = 1;
	viewport_set(surface.width, surface.height, next->depth_minimum, next->depth_maximum);
	checkpoint(_checkpoint_state);
}

/* ---------- programs */

/* a resident program's slots, or -1; with none free, the programs that ran longest ago go */
static int program_slots_find(const struct rsx_vertex_program *program, int *uploaded)
{
	uint32_t count = program->instruction_count;
	int index, slot = -1;

	*uploaded = 0;
	for (index = 0; index < RESIDENT_PROGRAM_COUNT; index++)
	{
		if (resident_programs[index].program == program)
		{
			resident_programs[index].used = ++program_clock;
			return (int)resident_programs[index].start;
		}
	}
	if (!count || count > PROGRAM_SLOTS)
		return -1;
	for (;;)
	{
		uint32_t start = 0;
		int free_entry = -1, oldest = -1, blocked = 0;

		/* the lowest start past every resident range it would overlap */
		do
		{
			blocked = 0;
			for (index = 0; index < RESIDENT_PROGRAM_COUNT; index++)
			{
				const struct rsx_vertex_program *other = resident_programs[index].program;

				if (other && start < resident_programs[index].start + resident_programs[index].count &&
					resident_programs[index].start < start + count)
				{
					start = resident_programs[index].start + resident_programs[index].count;
					blocked = 1;
				}
			}
		} while (blocked && start + count <= PROGRAM_SLOTS);
		for (index = 0; index < RESIDENT_PROGRAM_COUNT; index++)
		{
			if (!resident_programs[index].program)
			{
				if (free_entry < 0)
					free_entry = index;
			}
			else if (oldest < 0 || resident_programs[index].used < resident_programs[oldest].used)
			{
				oldest = index;
			}
		}
		if (start + count <= PROGRAM_SLOTS && free_entry >= 0)
		{
			resident_programs[free_entry].program = program;
			resident_programs[free_entry].start = start;
			resident_programs[free_entry].count = count;
			resident_programs[free_entry].used = ++program_clock;
			slot = (int)start;
			*uploaded = 1;
			break;
		}
		if (oldest < 0)
			break;
		resident_programs[oldest].program = NULL;
	}
	return slot;
}

/* uploads the program to free slots once, then starts it there; the system's overlays drop them (frame_begin) */
void ps3_gpu_set_vertex_program(const struct rsx_vertex_program *program)
{
	rsxVertexProgram header;
	int uploaded = 1, slot = 0;

	if (!ready || program == vertex_program)
		return;
#if RSX_RESIDENT_PROGRAMS
	slot = program_slots_find(program, &uploaded);
	if (slot < 0)
	{
		/* (larger than the slots: at slot 0, over the others) */
		memset(resident_programs, 0, sizeof(resident_programs));
		slot = 0;
		uploaded = 1;
	}
#endif
	memset(&header, 0, sizeof(header));
	header.num_regs = (u16)program->register_count;
	/* no instructions: the start, the masks and the timeout only */
	header.num_insn = (u16)(uploaded ? program->instruction_count : 0);
	header.input_mask = program->input_mask;
	header.output_mask = program->output_mask;
	header.insn_start = (u16)slot;
	rsxLoadVertexProgramBlock(context, &header, program->microcode);
	checkpoint(uploaded ? _checkpoint_vertex_upload : _checkpoint_vertex_switch);
	vertex_program = program;
	if (uploaded)
		statistics.vertex_uploads++;
	else
		statistics.vertex_switches++;
}

void ps3_gpu_set_vertex_constants(uint32_t first, uint32_t count, const float *values)
{
	if (ready && count)
	{
		rsxLoadVertexProgramParameterBlock(context, first, count, values);
		checkpoint(_checkpoint_vertex_constants);
	}
}

static void fragment_program_load(uint32_t offset, uint32_t control, uint32_t register_count, uint32_t texcoords,
	uint32_t texcoords_2d)
{
	rsxFragmentProgram header;

	memset(&header, 0, sizeof(header));
	header.num_regs = (u16)register_count;
	header.fp_control = control;
	header.texcoords = (u16)texcoords;
	/* 2D reads two components; 3D, as cgcomp gives a float4 coordinate, all four */
	header.texcoord2D = (u16)texcoords_2d;
	header.texcoord3D = (u16)(texcoords & ~texcoords_2d);
	rsxLoadFragmentProgramLocation(context, &header, offset, GCM_LOCATION_RSX);
	checkpoint(_checkpoint_fragment);
}

int ps3_gpu_set_fragment_program(const struct rsx_fragment_program *program, const float uniforms[][4])
{
	uint32_t index, offset, bytes = program->word_count * 4;
	void *memory;

	if (!ready)
		return -1;
	if (fragment.program == program)
	{
		for (index = 0; index < program->uniform_count; index++)
		{
			if (memcmp(fragment.values[index], uniforms[program->uniforms[index].uniform], sizeof(fragment.values[0])))
				break;
		}
		if (index == program->uniform_count)
			return 0;
	}
	memory = ps3_gpu_scratch(bytes, 64, &offset);
	if (!memory)
		return -1;
	memcpy(fragment_staging, program->microcode, bytes);
	for (index = 0; index < program->uniform_count; index++)
	{
		const float *value = uniforms[program->uniforms[index].uniform];

		rsx_fragment_program_set_uniform(fragment_staging, program->uniforms[index].word, value);
		memcpy(fragment.values[index], value, sizeof(fragment.values[0]));
	}
	/* one sequential pass into write-combined memory */
	memcpy(memory, fragment_staging, bytes);
	fragment_program_load(offset, program->control, program->register_count, program->texcoord_mask, 0);
	fragment.program = program;
	statistics.fragment_copies++;
	return 0;
}

/* ---------- textures */

static void texture_unit_off(uint32_t unit)
{
	if (texture_states[unit] != 1)
	{
		rsxTextureControl(context, (u8)unit, GCM_FALSE, 0, 0, 0);
		texture_states[unit] = 1;
		checkpoint(_checkpoint_texture_off);
	}
}

void ps3_gpu_set_texture(uint32_t unit, const struct ps3_gpu_texture *texture)
{
	gcmTexture description;

	if (!ready || unit >= TEXTURE_UNITS)
		return;
	if (!texture)
	{
		texture_unit_off(unit);
		return;
	}
	if (texture_states[unit] == 2 && !memcmp(texture, &textures[unit], sizeof(*texture)))
		return;
	textures[unit] = *texture;
	texture_states[unit] = 2;
	memset(&description, 0, sizeof(description));
	description.format = texture->format;
	/* a level count, not a flag (ioQuake3-PS3) */
	description.mipmap = texture->levels ? texture->levels : 1;
	description.dimension = texture->dimension;
	description.cubemap = texture->cube ? GCM_TRUE : GCM_FALSE;
	/* an 8- or 16-bit format's channels as the Xbox's (xbox_texture_layout.c) */
	description.remap = texture->remap ? texture->remap : REMAP_IDENTITY;
	description.width = texture->width;
	description.height = texture->height;
	description.depth = texture->depth ? texture->depth : 1;
	description.location = GCM_LOCATION_RSX;
	description.pitch = texture->pitch;
	description.offset = texture->offset;
	rsxLoadTexture(context, (u8)unit, &description);
	rsxTextureControl(context, (u8)unit, GCM_TRUE, texture->minimum_level, texture->maximum_level, texture->anisotropy);
	rsxTextureFilter(context, (u8)unit, texture->bias, texture->minify, texture->magnify,
		GCM_TEXTURE_CONVOLUTION_QUINCUNX);
	rsxTextureWrapMode(context, (u8)unit, texture->wrap_s, texture->wrap_t, texture->wrap_r,
		GCM_TEXTURE_UNSIGNED_REMAP_NORMAL, GCM_TEXTURE_ZFUNC_NEVER, 0);
	rsxTextureBorderColor(context, (u8)unit, texture->border);
	checkpoint(texture->remap ? _checkpoint_texture_remap : _checkpoint_texture);
	statistics.texture_loads++;
}

void ps3_gpu_textures_written(void)
{
	texture_cache_dirty = 1;
}

/* ---------- vertices and draws */

void ps3_gpu_set_attribute(uint32_t index, uint32_t offset, uint32_t stride, uint32_t elements)
{
	if (!ready || index >= ATTRIBUTE_COUNT)
		return;
	if (!elements)
	{
		if (attribute_enabled[index])
		{
			rsxBindVertexArrayAttrib(context, (u8)index, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
			attribute_enabled[index] = 0;
		}
		return;
	}
	/* offsets 16-aligned: a misaligned float attribute froze the whole console (ps3gl) */
	rsxBindVertexArrayAttrib(context, (u8)index, 0, offset, (u8)stride, (u8)elements, GCM_VERTEX_DATA_TYPE_F32,
		GCM_LOCATION_RSX);
	attribute_enabled[index] = 1;
	checkpoint(_checkpoint_attribute);
}

void ps3_gpu_vertices_written(void)
{
	vertex_cache_dirty = 1;
}

static void draw_begin(void)
{
	if (texture_cache_dirty)
	{
		rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);
		texture_cache_dirty = 0;
	}
	if (vertex_cache_dirty)
	{
		rsxInvalidateVertexCache(context);
		vertex_cache_dirty = 0;
	}
	statistics.draws++;
}

void ps3_gpu_draw(uint32_t primitive, uint32_t first, uint32_t count)
{
	if (!ready || !count)
		return;
	draw_begin();
	rsxDrawVertexArray(context, primitive, first, count);
	checkpoint(_checkpoint_draw);
}

void ps3_gpu_draw_indexed(uint32_t primitive, uint32_t offset, uint32_t count)
{
	if (!ready || !count)
		return;
	draw_begin();
	rsxDrawIndexArray(context, (u8)primitive, offset, count, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
	checkpoint(_checkpoint_draw_indexed);
}

/* ---------- presentation */

static void blit_instruction_defaults(struct rsx_instruction *instruction)
{
	int position;

	memset(instruction, 0, sizeof(*instruction));
	instruction->condition = _rsx_condition_always;
	instruction->condition_swizzle[1] = 1;
	instruction->condition_swizzle[2] = 2;
	instruction->condition_swizzle[3] = 3;
	instruction->uniform = RSX_NO_UNIFORM;
	for (position = 0; position < 3; position++)
	{
		instruction->source[position].swizzle[1] = 1;
		instruction->source[position].swizzle[2] = 2;
		instruction->source[position].swizzle[3] = 3;
	}
}

/* a one-instruction fragment program into RSX memory */
static int fragment_program_store(const struct rsx_instruction *instruction, uint32_t *offset)
{
	uint32_t words[8], index, count;
	void *memory;

	count = rsx_fragment_encode(instruction, 1, words);
	for (index = 0; index < count; index++)
		words[index] = (words[index] << 16) | (words[index] >> 16);
	memory = ps3_gpu_memory_allocate(sizeof(words) + 64, 64, _ps3_gpu_memory_program, offset);
	if (!memory)
		return -1;
	memcpy(memory, words, count * 4);
	return 0;
}

/* position from v0 and the texture coordinate from v1; the texel of unit 0 */
static int blit_programs_build(void)
{
	struct rsx_instruction instruction;

	blit_instruction_defaults(&instruction);
	instruction.opcode = _rsx_vp_mov;
	instruction.destination_type = _rsx_register_output;
	instruction.destination = _rsx_vp_output_position;
	instruction.mask = _rsx_xyzw;
	instruction.source[0].type = _rsx_register_input;
	instruction.source[0].index = 0;
	rsx_vertex_encode(&instruction, 0, blit.vertex_words);
	instruction.destination = _rsx_vp_output_texture0;
	instruction.source[0].index = 1;
	rsx_vertex_encode(&instruction, 1, blit.vertex_words + 4);
	blit.vertex.microcode = blit.vertex_words;
	blit.vertex.instruction_count = 2;
	blit.vertex.input_mask = 0x3;
	blit.vertex.output_mask = 0x4000;
	blit.vertex.register_count = 1;

	blit_instruction_defaults(&instruction);
	instruction.opcode = _rsx_fp_tex;
	instruction.destination_type = _rsx_register_output;
	instruction.mask = _rsx_xyzw;
	instruction.source[0].type = _rsx_register_input;
	instruction.source[0].index = _rsx_fp_input_texture0;
	return fragment_program_store(&instruction, &blit.fragment_offset);
}

/* a quad of the display (pixels from the top left) with texture unit 0 */
static void blit_quad(unsigned display_width, unsigned display_height, float left, float top, float right, float bottom)
{
	uint32_t vertices_offset, unit;
	float *vertices = ps3_gpu_scratch(4 * 8 * sizeof(float), 16, &vertices_offset);
	float x0 = left / display_width * 2.0f - 1.0f, x1 = right / display_width * 2.0f - 1.0f;
	float y0 = 1.0f - top / display_height * 2.0f, y1 = 1.0f - bottom / display_height * 2.0f;

	if (!vertices)
		return;
	{
		const float quad[4][8] =
		{
			{ x0, y0, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
			{ x1, y0, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f },
			{ x1, y1, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f },
			{ x0, y1, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f },
		};

		memcpy(vertices, quad, sizeof(quad));
	}
	for (unit = 0; unit < ATTRIBUTE_COUNT; unit++)
		ps3_gpu_set_attribute(unit, vertices_offset + (unit ? 16 : 0), 32, unit < 2 ? 4 : 0);
	ps3_gpu_draw(GCM_TYPE_QUADS, 0, 4);
}

static void blit_draw(uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height)
{
	unsigned display_width, display_height, picture_width, picture_height;
	float scale[4], viewport_offset[4], left, top;
	struct ps3_gpu_texture texture;
	uint32_t unit;

	ps3_video_size(&display_width, &display_height);
	/* the back buffer is drawn at the display's lines (d3d8_rsx.c's screen scale): pixel for pixel in the middle,
	else scaled to fit */
	picture_width = width;
	picture_height = height;
	if (picture_width > display_width || picture_height > display_height)
	{
		picture_width = display_height * width <= display_width * height ? display_height * width / height : display_width;
		picture_height = display_height * width <= display_width * height ? display_height : display_width * height / width;
	}
	left = (float)((display_width - picture_width) / 2);
	top = (float)((display_height - picture_height) / 2);

	rsxSetColorMask(context, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
	rsxSetDepthTestEnable(context, GCM_FALSE);
	rsxSetDepthWriteEnable(context, GCM_FALSE);
	rsxSetStencilTestEnable(context, GCM_FALSE);
	rsxSetBlendEnable(context, GCM_FALSE);
	rsxSetAlphaTestEnable(context, GCM_FALSE);
	rsxSetCullFaceEnable(context, GCM_FALSE);
	rsxSetPolygonOffsetFillEnable(context, GCM_FALSE);
	rsxSetFrontPolygonMode(context, GCM_POLYGON_MODE_FILL);
	rsxSetBackPolygonMode(context, GCM_POLYGON_MODE_FILL);
	rsxSetScissor(context, 0, 0, (u16)display_width, (u16)display_height);
	scale[0] = display_width * 0.5f;
	scale[1] = display_height * -0.5f;
	scale[2] = 0.5f;
	scale[3] = 0.0f;
	viewport_offset[0] = display_width * 0.5f;
	viewport_offset[1] = display_height * 0.5f;
	viewport_offset[2] = 0.5f;
	viewport_offset[3] = 0.0f;
	rsxSetViewport(context, 0, 0, (u16)display_width, (u16)display_height, 0.0f, 1.0f, scale, viewport_offset);
	rsxSetViewportClip(context, 0, (u16)display_width, (u16)display_height);
	/* black bars where the picture does not cover the display (4:3) */
	if (picture_width < display_width || picture_height < display_height)
	{
		rsxSetClearColor(context, 0xff000000u);
		rsxClearSurface(context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
	}

	ps3_gpu_set_vertex_program(&blit.vertex);
	fragment_program_load(blit.fragment_offset, 0x40, 2, 1, 1);
	fragment.program = NULL;

	memset(&texture, 0, sizeof(texture));
	texture.offset = offset;
	texture.pitch = pitch;
	texture.width = (uint16_t)width;
	texture.height = (uint16_t)height;
	texture.depth = 1;
	/* normalized coordinates on a linear texture, as ps3gl's */
	texture.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	texture.levels = 1;
	texture.dimension = GCM_TEXTURE_DIMS_2D;
	texture.minify = GCM_TEXTURE_LINEAR;
	texture.magnify = GCM_TEXTURE_LINEAR;
	texture.wrap_s = GCM_TEXTURE_CLAMP_TO_EDGE;
	texture.wrap_t = GCM_TEXTURE_CLAMP_TO_EDGE;
	texture.wrap_r = GCM_TEXTURE_CLAMP_TO_EDGE;
	ps3_gpu_set_texture(0, &texture);
	for (unit = 1; unit < TEXTURE_UNITS; unit++)
		texture_unit_off(unit);
	texture_cache_dirty = 1;
	blit_quad(display_width, display_height, left, top, left + picture_width, top + picture_height);
}

static void frame_begin(void)
{
	double start;

	frame_index++;
	scratch.head = 0;
	/* the half this frame uses: the RSX must have finished the frame before last */
	if (!frame_finished(frame_index - 2))
	{
		uint64_t span = ps3_timer_now();

		start = ps3_time_seconds();
		while (!frame_finished(frame_index - 2))
		{
			if (ps3_time_seconds() - start > FENCE_TIMEOUT_SECONDS)
			{
				if (fence_timeouts++ < 10)
				{
					ps3_log_printf("[gpu] frame %u: the RSX did not finish frame %u (label %u)\n",
						(unsigned)frame_index, (unsigned)(frame_index - 2), (unsigned)*frame_label);
					checkpoint_report();
				}
				break;
			}
			usleep(20);
		}
		ps3_timer_add(_ps3_timer_rsx_wait, span);
	}
	timestamps_read();
	deferred_frees_run(0);
	caches_invalidate();
	/* an overlay of the system's (the XMB in the game) may use the vertex program slots */
	if (overlay_events != ps3_system_overlay_events())
	{
		overlay_events = ps3_system_overlay_events();
		memset(resident_programs, 0, sizeof(resident_programs));
	}
	/* ZCull's state, as rsxtest_tile's, at each frame as the rest of the state */
	if (zcull_bound)
	{
		rsxSetZCullControl(context, GCM_ZCULL_LESS, GCM_ZCULL_LONES);
		rsxSetSCullControl(context, GCM_SCULL_SFUNC_LESS, 1, 0xff);
		checkpoint(_checkpoint_zcull_control);
	}
}

void ps3_gpu_wait_idle(void)
{
	double start;

	if (!ready)
		return;
	rsxSetWriteBackendLabel(context, IDLE_LABEL, ++idle_reference);
	rsxFlushBuffer(context);
	start = ps3_time_seconds();
	while (*idle_label != idle_reference)
	{
		if (ps3_time_seconds() - start > FENCE_TIMEOUT_SECONDS)
		{
			ps3_log_printf("[gpu] the RSX did not drain (label %u of %u)\n", (unsigned)*idle_label,
				(unsigned)idle_reference);
			break;
		}
		usleep(20);
	}
}

void ps3_gpu_timestamp(int part)
{
	uint32_t set = frame_index % 3;

	if (ready && stamps[set].count < TIMESTAMP_PARTS)
	{
		rsxSetReport(context, REPORT_TIMESTAMP, TIMESTAMP_REPORT_BASE + set * TIMESTAMP_PARTS + stamps[set].count);
		stamps[set].parts[stamps[set].count++] = (uint8_t)part;
		checkpoint(_checkpoint_timestamp);
	}
}

static void (*stop_hook)(void);

void ps3_gpu_set_stop_hook(void (*hook)(void))
{
	stop_hook = hook;
}

void ps3_gpu_stop_drawing(void)
{
	if (stop_hook)
		stop_hook();
}

/* the frame before last's timestamps (the RSX has finished it): each part ends at its split, from the split before;
and the frame's time */
static void timestamps_read(void)
{
	uint32_t set = (frame_index + 1) % 3, index;
	uint64_t previous = 0, first = 0;

	for (index = 0; index < stamps[set].count; index++)
	{
		uint64_t now = gcmGetTimeStamp(TIMESTAMP_REPORT_BASE + set * TIMESTAMP_PARTS + index);

		if (index)
			ps3_rsx_timers[stamps[set].parts[index]] += now > previous ? now - previous : 0;
		else
			first = now;
		previous = now;
	}
	if (stamps[set].count > 1)
	{
		ps3_rsx_frame_nanoseconds += previous > first ? previous - first : 0;
		ps3_rsx_frames++;
	}
	stamps[set].count = 0;
}

void ps3_gpu_present(uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height)
{
	uint64_t span;

	if (!ready)
		return;
	span = ps3_timer_now();
	ps3_video_begin_frame();
	ps3_timer_add(_ps3_timer_flip_wait, span);
	if (width && height)
		blit_draw(offset, pitch, width, height);
	checkpoint(_checkpoint_blit);
	/* the frame's last timestamp (_ps3_timer_present: the time after the render's) */
	ps3_gpu_timestamp(_ps3_timer_present);
	/* the RSX writes it once this frame's draws are done (a command label can come early: ioq3) */
	rsxSetWriteBackendLabel(context, FRAME_LABEL, frame_index);
	checkpoint(_checkpoint_frame_label);
	ps3_video_end_frame();
	checkpoint(_checkpoint_flip);
	if (frame_index % STATISTICS_FRAMES == 0)
	{
		ps3_log_printf("[gpu] frame %u: %u draws, %u vertex programs uploaded and %u switched, %u fragment programs, "
			"%u textures a frame; scratch peak %u KB of %u, %u dropped\n", (unsigned)frame_index,
			(unsigned)(statistics.draws / STATISTICS_FRAMES), (unsigned)(statistics.vertex_uploads / STATISTICS_FRAMES),
			(unsigned)(statistics.vertex_switches / STATISTICS_FRAMES),
			(unsigned)(statistics.fragment_copies / STATISTICS_FRAMES), (unsigned)(statistics.texture_loads / STATISTICS_FRAMES),
			(unsigned)(scratch.peak / 1024), (unsigned)(SCRATCH_HALF_SIZE / 1024), (unsigned)scratch.drops);
		memset(&statistics, 0, sizeof(statistics));
	}
	frame_begin();
}

/* ---------- start-up */

int ps3_gpu_initialize(void)
{
	gcmConfiguration configuration;

	if (ready)
		return 0;
	ps3_display_acquire();
	context = ps3_video_context();
	if (!context)
	{
		ps3_log_printf("[gpu] no RSX context: nothing is drawn\n");
		return -1;
	}
	gcmGetConfiguration(&configuration);
	/* PSL1GHT's heap (librsx mm.c) is all of it but 4 KB */
	rsx_memory.local_size = configuration.localSize - 4096;
	ps3_log_printf("[gpu] RSX local memory %u KB, display buffers %u KB\n", (unsigned)(configuration.localSize / 1024),
		ps3_video_memory_bytes() / 1024);
	scratch.base = ps3_gpu_memory_allocate(2 * SCRATCH_HALF_SIZE, 128, _ps3_gpu_memory_scratch, &scratch.offset);
	if (!scratch.base || blit_programs_build() != 0)
	{
		ps3_log_printf("[gpu] out of RSX memory at the start\n");
		return -1;
	}
	frame_label = (volatile uint32_t *)gcmGetLabelAddress(FRAME_LABEL);
	*frame_label = 0;
	frame_index = 1;
	idle_label = (volatile uint32_t *)gcmGetLabelAddress(IDLE_LABEL);
	*idle_label = 0;
	idle_reference = 0;
#if RSX_CHECKPOINTS
	checkpoint_label = (volatile uint32_t *)gcmGetLabelAddress(CHECKPOINT_LABEL);
	*checkpoint_label = 0;
	checkpoint_backend_label = (volatile uint32_t *)gcmGetLabelAddress(CHECKPOINT_BACKEND_LABEL);
	*checkpoint_backend_label = 0;
	checkpoint(_checkpoint_start);
#endif
	caches_invalidate();
	ready = 1;
	ps3_log_printf("[gpu] ready: scratch 2 x %u KB at 0x%08x\n", (unsigned)(SCRATCH_HALF_SIZE / 1024),
		(unsigned)scratch.offset);
	return 0;
}
