/* d3d8_gl.c's Direct3D device on the RSX (ps3_gpu.c), vertex programs and combiners as runtime microcode (ps3_rsx_*.c).
Resources: d3d8_resources.c. The game's thread records calls; the PPE's other thread (Direct3D thread) draws them. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "halo_ui_pointer.h"
#include "port_config.h"
#include "ps3_gpu.h"
#include "ps3_spu.h"
#include "ps3_system.h"
#include "ps3_timers.h"
#include "ps3_vmx.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
/* the widest screen the game draws (16:9 is 852 columns): the back buffer's memory holds it */
#define SCREEN_MAXIMUM_WIDTH 864
/* rasterizer_xbox.c's secondary target (reflections, active camouflage), drawn at the screen's scale */
#define SECONDARY_TARGET_WIDTH 320
#define SECONDARY_TARGET_HEIGHT 240
#define VERTICAL_BLANK_MICROSECONDS (1000000 / 60)
#define VERTEX_SHADER_SIGNATURE 0x76736864UL /* 'vshd' */
#define VERTEX_PROGRAM_SLOTS 136
#define VERTEX_ATTRIBUTE_COUNT 16
#define VERTEX_CONSTANT_COUNT 192
/* D3D constant register -96 is hardware register 0 */
#define VERTEX_CONSTANT_BIAS 96
#define TEXTURE_STAGES 4
#define STATISTICS_FRAMES 600

/* GCM's values (gcm_sys.h cannot be included with the game's 32-bit long) */
#define GCM_NEVER 0x0200
#define GCM_GREATER 0x0204
#define GCM_COLOR_MASK_B 0x00000001
#define GCM_COLOR_MASK_G 0x00000100
#define GCM_COLOR_MASK_R 0x00010000
#define GCM_COLOR_MASK_A 0x01000000
#define GCM_CULL_FRONT 0x0404
#define GCM_CULL_BACK 0x0405
#define GCM_FRONTFACE_CW 0x0900
#define GCM_FRONTFACE_CCW 0x0901
#define GCM_POLYGON_MODE_FILL 0x1b02
#define GCM_FUNC_ADD 0x8006
#define GCM_FUNC_MIN 0x8007
#define GCM_FUNC_MAX 0x8008
#define GCM_FUNC_SUBTRACT 0x800a
#define GCM_FUNC_REVERSE_SUBTRACT 0x800b
#define GCM_TYPE_TRIANGLE_FAN 7
#define GCM_TEXTURE_FORMAT_LIN 0x20
#define GCM_TEXTURE_FORMAT_UNRM 0x40
#define GCM_TEXTURE_FORMAT_A8R8G8B8 5
#define GCM_TEXTURE_FORMAT_DXT1 6
#define GCM_TEXTURE_FORMAT_DXT23 7
#define GCM_TEXTURE_FORMAT_DXT45 8
#define GCM_TEXTURE_DIMS_2D 2
#define GCM_TEXTURE_DIMS_3D 3
#define GCM_TEXTURE_NEAREST 1
#define GCM_TEXTURE_LINEAR 2
#define GCM_TEXTURE_NEAREST_MIPMAP_NEAREST 3
#define GCM_TEXTURE_LINEAR_MIPMAP_NEAREST 4
#define GCM_TEXTURE_NEAREST_MIPMAP_LINEAR 5
#define GCM_TEXTURE_LINEAR_MIPMAP_LINEAR 6
#define GCM_TEXTURE_REPEAT 1
#define GCM_TEXTURE_MIRRORED_REPEAT 2
#define GCM_TEXTURE_CLAMP_TO_EDGE 3
#define GCM_TEXTURE_BORDER 4

void d3d8_surface_initialize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
/* port_stubs.c: display.interpolation */
int halo_interpolation_enabled(void);

/* ---------- state the XDK header's inline functions read and write */

/* (on cache lines: record_states compares them 64 bytes at a time with VMX, their lines fetched first) */
DWORD D3D__RenderState[D3DRS_MAX] __attribute__((aligned(128)));
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX] __attribute__((aligned(128)));
WORD *D3D__IndexData;

/* ---------- the device */

struct vertex_element
{
	unsigned char reg;
	unsigned char stream;
	unsigned char type;
	unsigned char bytes;
	unsigned short offset;
};

struct vertex_shader_object
{
	unsigned long signature;
	unsigned long id;
	DWORD *instructions;
	unsigned long instruction_count;
	struct vertex_element elements[VERTEX_ATTRIBUTE_COUNT];
	unsigned long element_count;
	/* translated at the first draw */
	struct rsx_vertex_program program;
	BOOL translated;
	BOOL untranslatable;
};

/* a surface's words, as the game's thread read them for the Direct3D thread */
struct surface_words
{
	DWORD valid;
	DWORD data;
	DWORD format;
	DWORD size;
};

/* a stage's texture header and palette data; fresh: the header was set up again (Lock 0) since its last draw */
struct stage_texture
{
	DWORD bound;
	DWORD data;
	DWORD format;
	DWORD size;
	DWORD palette;
	DWORD fresh;
};

/* the device as the Direct3D thread draws: the records' state */
static struct
{
	BOOL created;
	BOOL gpu_ready;
	D3DPRESENT_PARAMETERS presentation;
	/* the game's surfaces; they change only between frames, with the Direct3D thread idle (halo_screen_commit) */
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	/* the colour and depth targets */
	struct surface_words targets[2];
	D3DVIEWPORT8 viewport;
	struct stage_texture stages[TEXTURE_STAGES];
	D3DSHADERCONSTANTMODE shader_constant_mode;

	struct vertex_shader_object *vertex_shader;
	struct vertex_shader_object *program_slots[VERTEX_PROGRAM_SLOTS];
	unsigned long program_address;
	/* the D3D constants, then the RSX's own (ps3_rsx_program.h) */
	float constants[_rsx_vp_constant_count][4];
	unsigned long constants_dirty_first;
	unsigned long constants_dirty_last;
	float viewport_scale[4];
	float viewport_offset[4];

	/* a draw's streams: vertex v at base + v * stride, in the tag cache or in the record's copy */
	struct
	{
		const unsigned char *base;
		UINT stride;
	} streams[16];

	/* the current value of each input register (SetVertexData) */
	float attributes[VERTEX_ATTRIBUTE_COUNT][4];

	/* the bound targets' size in the game's units, their pixels a unit and their size in pixels (bind_targets) */
	unsigned long target_width;
	unsigned long target_height;
	float target_scale[2];
	unsigned long target_pixel_width;
	unsigned long target_pixel_height;
	/* a stage's linear render target drawn at the screen's scale: its texel coordinates times this (bind_textures) */
	float texture_scale[TEXTURE_STAGES][2];

	unsigned long frame;
} device;

/* the render and texture stage states the Direct3D thread draws with; the game writes D3D__RenderState */
static DWORD render_state[D3DRS_MAX];
static DWORD texture_state[D3DTSS_MAXSTAGES][D3DTSS_MAX];

/* what the records changed since a draw made those parts of its state (states_apply, the texture records): a stage's
sampler states, the texture modes, the fragment key's states, a stage's texture */
#define CHANGED_SAMPLER(stage) (1u << (stage))
#define CHANGED_TEXTURE_MODES 0x10u
#define CHANGED_FRAGMENT_KEY 0x20u
#define CHANGED_TEXTURE(stage) (0x100u << (stage))
#define CHANGED_TEXTURES (0xfu | CHANGED_TEXTURE_MODES | 0xf00u)
static uint32_t draw_state_changes = ~0u;

/* the device as the game's thread sees it; the records carry it to the Direct3D thread */
static struct
{
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DBaseTexture *textures[TEXTURE_STAGES];
	D3DPalette *palettes[TEXTURE_STAGES];
	struct vertex_shader_object *vertex_shader;
	struct
	{
		DWORD data;
		UINT stride;
	} streams[16];
	/* SetIndices' base vertex, added to every index of an indexed draw */
	UINT base_vertex_index;
	/* the vertex count of the map vertex buffer that the next indexed draw reads (d3d8_draw_vertex_count) */
	unsigned long draw_vertex_count;
	float attributes[VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	/* each vertex's registers of the layout, in order (capacity in floats); the registers written since Begin */
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long immediate_capacity;
	DWORD immediate_written;
	/* the registers the first vertex had written, widened when a later one writes another */
	DWORD immediate_layout;
	unsigned char immediate_regs[VERTEX_ATTRIBUTE_COUNT];
	unsigned long immediate_reg_count;
	unsigned long immediate_floats;
	/* registers out of the layout written after the first vertex, and their values for the vertices before */
	DWORD immediate_changed;
	float immediate_before[VERTEX_ATTRIBUTE_COUNT][4];
	D3DMATRIX transforms[D3DTS_MAX];
	BOOL visibility_test_active;
	unsigned long next_vertex_shader_id;
	unsigned long frame;
	long ui_offset;
	/* what the records have given the Direct3D thread */
	DWORD recorded_render_state[D3DRS_MAX] __attribute__((aligned(128)));
	DWORD recorded_texture_state[D3DTSS_MAXSTAGES][D3DTSS_MAX] __attribute__((aligned(128)));
	float recorded_attributes[VERTEX_ATTRIBUTE_COUNT][4];
	struct stage_texture recorded_stages[TEXTURE_STAGES];
	struct surface_words recorded_targets[2];
	/* the ring's position after the last frame's present record */
	uint64_t previous_present;
} front;

/* ---------- records: a call's arguments, read by the Direct3D thread in the order the game made them */

enum
{
	_record_wrap,
	_record_states,
	_record_attributes,
	_record_texture,
	_record_vertex_shader,
	_record_constants,
	_record_constant_mode,
	_record_viewport,
	_record_render_target,
	_record_ui_offset,
	_record_clear,
	_record_draw,
	_record_draw_indexed,
	_record_immediate,
	_record_elements,
	_record_timestamp,
	_record_draw_stamps,
	_record_present,
	/* the Direct3D SPU's only (spu_direct3d.h) */
	_record_screen,
	_record_tag_cache,
	_record_label,
	/* (both's) */
	_record_particles,
	_record_light_bounds,
	_record_scale
};

/* the timer build's records by type (_ps3_timer_record_wrap on) */
typedef char record_timers_check[_record_present - _record_wrap ==
	_ps3_timer_record_present - _ps3_timer_record_wrap ? 1 : -1];

/* each record starts 16-aligned with this header; bytes: the whole record, a multiple of 16 */
struct record
{
	uint32_t type;
	uint32_t bytes;
	uint32_t a;
	uint32_t b;
};

/* 0: each record runs on the game's thread as it is made */
#define DIRECT3D_THREAD 1
/* a heavy frame's records take about 1 MB: room for the frame drawn and the one recorded */
#define RING_BYTES (4UL * 1024 * 1024)
/* a larger record runs on the game's thread, with the Direct3D thread idle */
#define RECORD_LARGEST (RING_BYTES / 4)
/* the records the Direct3D thread gets at once (the game's thread publishes them) */
#define PUBLISH_BYTES (64UL * 1024)
/* the PPE's cache line: the ring's lines are zeroed in the cache before records are written (ring_lines_zero) */
#define RING_LINE 128
/* a wait sees again after this, whatever wakes it */
#define WAIT_MICROSECONDS 100000

static struct
{
	unsigned char *memory;
	/* byte positions since the start: the game's thread records and publishes, the Direct3D thread consumes */
	uint64_t written;
	volatile uint64_t published;
	volatile uint64_t consumed;
	/* the draws numbered (draw_prologue) and the last one done */
	DWORD recorded_draws;
	volatile DWORD completed_draws;
	/* the waits in progress, under the lock */
	volatile int consumer_sleeping;
	volatile int producers_waiting;
	volatile int stop;
	volatile int stopped;
	BOOL running;
	/* after direct3d_stop, no record runs */
	BOOL dead;
	uint64_t producer;
	uint64_t consumer;
	struct ps3_host_lock *lock;
	struct ps3_host_condition *work;
	struct ps3_host_condition *progress;
	/* the record being made, and whether it runs on the game's thread (record_commit) */
	struct record *current;
	BOOL current_inline;
	unsigned char *large;
	unsigned long large_bytes;
} ring;

/* the Direct3D thread's work on an SPU (spu_direct3d.c, the service thread below): its state */
static spu_d3d_request spu_request_block;
static spu_d3d_response spu_response_block;
static spu_d3d_statistics spu_statistics_block;
static spu_d3d_initial spu_initial;
static spu_d3d_chunk spu_chunk_list[SPU_D3D_CHUNK_LIST] __attribute__((aligned(128)));
static int32_t spu_fragment_buckets[SPU_D3D_FRAGMENT_BUCKETS] __attribute__((aligned(128)));

static struct
{
	BOOL on;
	spu_d3d_bell *bell;
	const volatile spu_d3d_echo *echo;
	spu_d3d_setup setup;
	/* the chunks called, the request answered */
	uint32_t called;
	uint32_t answered;
	spu_d3d_texture *textures;
	spu_d3d_array *arrays;
	spu_d3d_index_array *index_arrays;
	spu_d3d_fragment *fragments;
	uint32_t fragment_count;
	uint32_t fragment_capacity;
	volatile int stop;
	volatile int stopped;
	/* the command buffer's context: the service thread's, the game thread's while the SPU is idle */
	struct ps3_host_lock *gpu_lock;
	/* the targets its requests made, for render_targets_evict */
	struct
	{
		void *memory;
		uint32_t bytes;
	} targets[2 * SPU_D3D_TARGETS];
	uint32_t target_count;
	/* the SPU's timers and counts at the timers' last log */
	uint64_t timers[SPU_D3D_TIMERS];
	uint32_t counts[SPU_D3D_TIMERS];
} spu;

static void spu_wait(uint64_t position);
static void spu_chunks_wait(void);
static BOOL spu_direct3d_start(void);

static void *record_begin(uint32_t type, unsigned long payload_bytes, uint32_t a, uint32_t b);
static void record_commit(void);
static void direct3d_publish(void);
static void direct3d_wait(uint64_t position);
static void direct3d_drain(void);
static void direct3d_start(void);
static void direct3d_stop(void);
static void present_run(void);

static struct
{
	unsigned long draws, skipped_program, skipped_target, skipped_scratch, skipped_scissor;
	unsigned long texture_uploads, texture_bytes;
	/* of those bytes, the SPU's to convert; the bytes of the texture checks the SPU hashed */
	unsigned long texture_spu_bytes, texture_hashed_spu;
	/* vertex elements converted into the vertex cache and at draws; copies the cache had no room for */
	unsigned long cached_elements, converted_elements, cache_refused;
	/* the elements converted, by type (D3DVSDT_*) */
	unsigned long type_elements[128];
} statistics;

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

/* b / 255 for each byte (Direct3D_CreateDevice): an int-to-float conversion goes through memory on the PPE; 16-aligned
for the Direct3D SPU's DMA */
static float byte_floats[256] __attribute__((aligned(16)));

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = byte_floats[(color >> 16) & 0xff];
	out[1] = byte_floats[(color >> 8) & 0xff];
	out[2] = byte_floats[color & 0xff];
	out[3] = byte_floats[(color >> 24) & 0xff];
}

/* ---------- the screen: 480 lines of the game, 640 columns (4:3) or the display's shape (display.screen_width),
drawn at the display's resolution: the targets the size of the screen get screen_scale pixels a unit (d3d8_gl.c's) */

static long screen_width;
static float screen_scale[2] = { 1.0f, 1.0f };
static unsigned long screen_read_at = (unsigned long)-1;
static long ui_offset;

static void render_targets_evict(void);
static BOOL particles_targets(void);
static void particles_targets_free(void);
static BOOL scaled_target(void);
static void scaled_target_free(void);
static void scaled_target_commit(void);

static void screen_mode_choose(long *width, float scale[2])
{
	uint32_t display_width = 0, display_height = 0;
	long requested = config_integer("display.screen_width"), wide;
	int widescreen = 0;
	float pixel_aspect;

	*width = SCREEN_WIDTH;
	scale[0] = scale[1] = 1.0f;
	ps3_gpu_display(&display_width, &display_height, &widescreen);
	if (!display_width || !display_height)
		return;
	/* a pixel's width over its height: 1 at 720p, more on a 16:9 picture of 720x480 */
	pixel_aspect = (widescreen ? 16.0f / 9.0f : 4.0f / 3.0f) * (float)display_height / (float)display_width;
	wide = widescreen ? (SCREEN_HEIGHT * 16 / 9) & ~1L : SCREEN_WIDTH;
	*width = requested <= 0 || requested > wide ? wide : requested < SCREEN_WIDTH ? SCREEN_WIDTH : requested & ~1L;
	scale[1] = (float)display_height / (float)SCREEN_HEIGHT;
	/* the display's shape fills it; narrower keeps square units, in the middle */
	scale[0] = *width == wide && widescreen ? (float)display_width / (float)*width : scale[1] / pixel_aspect;
}

long halo_screen_width(void)
{
	if (!screen_width)
	{
		screen_read_at = config_changes();
		screen_mode_choose(&screen_width, screen_scale);
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", screen_width, SCREEN_HEIGHT, screen_width * screen_scale[0],
			SCREEN_HEIGHT * screen_scale[1]);
	}
	return screen_width;
}

/* the menus and full-screen overlays are laid out for 640 columns: centred while they draw */
void halo_screen_ui_offset(unsigned char centered)
{
	long offset = centered ? (halo_screen_width() - SCREEN_WIDTH) / 2 : 0;

	if (offset != front.ui_offset)
	{
		front.ui_offset = offset;
		record_begin(_record_ui_offset, 0, (uint32_t)offset, 0);
		record_commit();
	}
}

void d3d8_surface_resize(D3DSurface *surface, D3DFORMAT format, unsigned long width, unsigned long height);
void hud_hires_style_commit(void);

/* EXTRA OPTIONS' WIDESCREEN (display.screen_width), between frames: the game's layout and the targets must agree
for a whole frame (rasterizer_xbox.c). Returns the width the game draws. */
long halo_screen_commit(void)
{
	long width;
	float scale[2];

	/* (BUTTON ICONS' style, read on the game thread as the width is) */
	hud_hires_style_commit();
	if (!screen_width)
		return halo_screen_width();
	if (screen_read_at == config_changes())
		return screen_width;
	screen_read_at = config_changes();
	screen_mode_choose(&width, scale);
	if (width != screen_width || scale[0] != screen_scale[0] || scale[1] != screen_scale[1])
	{
		/* the surfaces and the targets change under the Direct3D thread's frames */
		direct3d_drain();
		spu_chunks_wait();
		platform_log("screen: %ldx%d drawn at %.0fx%.0f", width, SCREEN_HEIGHT, width * scale[0],
			SCREEN_HEIGHT * scale[1]);
		screen_width = width;
		screen_scale[0] = scale[0];
		screen_scale[1] = scale[1];
		if (device.created)
		{
			device.presentation.BackBufferWidth = (UINT)width;
			d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, (unsigned long)width, SCREEN_HEIGHT);
			d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, (unsigned long)width, SCREEN_HEIGHT);
		}
		/* (the targets of the old size, which nothing finds now) */
		render_targets_evict();
		if (config_boolean("display.half_resolution_particles"))
			particles_targets();
		if (config_boolean("display.dynamic_resolution"))
			scaled_target();
	}
	else
		scaled_target_commit();
	return screen_width;
}

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)menus_active;
	(void)pointer;
	return 0;
}

/* no pointer, so the scoreboard offers no menu (as on Android) */
int halo_scoreboard_pointer_update(int offered, struct halo_ui_pointer *pointer)
{
	(void)offered;
	(void)pointer;
	return -1;
}

/* the screen's targets' pixels to the Xbox's one, the larger way (the screen effects' convolutions) */
float halo_screen_scale(void)
{
	halo_screen_width();
	return screen_scale[0] > screen_scale[1] ? screen_scale[0] : screen_scale[1];
}

/* the shadow maps at the Xbox's 128x128 */
long halo_shadow_map_scale(void)
{
	return 1;
}

/* no display.anti_aliasing pass */
void halo_screen_anti_alias(short x0, short y0, short x1, short y1)
{
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
}

/* models stay lit for each vertex (no display.per_pixel_lighting) */
void halo_vertex_shader_lighting(unsigned long handle)
{
	(void)handle;
}

/* ---------- vertical blank emulation */

static struct ps3_host_lock *vertical_blank_lock;
static struct ps3_host_condition *vertical_blank_condition;
static D3DCALLBACK vertical_blank_callback;
static unsigned long vertical_blank_count;
static volatile unsigned int flip_count;
static unsigned long pending_flips;
static BOOL vertical_blank_started;

static void vertical_blank_thread(void *unused)
{
	__uint64 next = ps3_host_microseconds();

	(void)unused;
	for (;;)
	{
		D3DCALLBACK callback;
		__uint64 now;

		next += VERTICAL_BLANK_MICROSECONDS;
		now = ps3_host_microseconds();
		if (next > now)
			ps3_host_sleep_microseconds(next - now);
		else
			next = now;

		ps3_host_lock_enter(vertical_blank_lock);
		vertical_blank_count++;
		/* a presented frame shows at the next vertical blank */
		if (pending_flips)
		{
			pending_flips--;
			flip_count++;
		}
		callback = vertical_blank_callback;
		ps3_host_condition_broadcast(vertical_blank_condition);
		ps3_host_lock_leave(vertical_blank_lock);

		if (callback)
			callback(0);
	}
}

static void vertical_blank_start(void)
{
	if (vertical_blank_started)
		return;
	vertical_blank_lock = ps3_host_lock_create(FALSE);
	vertical_blank_condition = vertical_blank_lock ? ps3_host_condition_create(vertical_blank_lock) : NULL;
	/* above the game threads, as the Xbox vblank DPC preempts them; it sleeps between vblanks */
	if (vertical_blank_condition &&
		ps3_host_thread_create(vertical_blank_thread, NULL, 0x40000, PS3_HOST_PRIORITY_NORMAL - 1, "vblank") == 0)
		vertical_blank_started = TRUE;
	else
		platform_log("cannot start the vertical blank thread");
}

/* replaces main/d3d_intimacy.cpp, which reads the Xbox runtime's private flip counter */
volatile unsigned int *d3d_find_flipcount(void)
{
	return &flip_count;
}

void WINAPI D3DDevice_SetVerticalBlankCallback(D3DCALLBACK callback)
{
	vertical_blank_start();
	if (!vertical_blank_started)
		return;
	ps3_host_lock_enter(vertical_blank_lock);
	vertical_blank_callback = callback;
	ps3_host_lock_leave(vertical_blank_lock);
}

void WINAPI D3DDevice_BlockUntilVerticalBlank(void)
{
	unsigned long count;

	vertical_blank_start();
	if (!vertical_blank_started)
		return;
	ps3_host_lock_enter(vertical_blank_lock);
	count = vertical_blank_count;
	while (vertical_blank_count == count)
		ps3_host_condition_wait(vertical_blank_condition, 0);
	ps3_host_lock_leave(vertical_blank_lock);
}

/* ---------- vertex constants */

static void constants_mark(unsigned long first, unsigned long last)
{
	if (device.constants_dirty_first > first)
		device.constants_dirty_first = first;
	if (device.constants_dirty_last < last)
		device.constants_dirty_last = last;
}

static void constants_store(unsigned long first, const void *data, unsigned long count)
{
	const float (*values)[4] = data;
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		if (memcmp(device.constants[first + index], values[index], sizeof(device.constants[0])))
		{
			memcpy(device.constants[first + index], values[index], sizeof(device.constants[0]));
			constants_mark(first + index, first + index);
		}
	}
}

static void constants_upload(void)
{
	unsigned long first = device.constants_dirty_first, last = device.constants_dirty_last;

	if (first > last)
		return;
	ps3_gpu_set_vertex_constants((uint32_t)first, (uint32_t)(last - first + 1), &device.constants[first][0]);
	device.constants_dirty_first = _rsx_vp_constant_count;
	device.constants_dirty_last = 0;
}

/* the depth buffer's range: the screen-space z the game's programs give */
static float depth_scale(void)
{
	if (device.targets[1].valid)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.targets[1].format, device.targets[1].size, &description);
		if (description.format == D3DFMT_D16 || description.format == D3DFMT_LIN_D16 ||
			description.format == D3DFMT_F16 || description.format == D3DFMT_LIN_F16)
			return 65535.0f;
	}
	return 16777215.0f;
}

static void viewport_update_constants(void)
{
	/* Direct3D's reserved constants c[-38] and c[-37] map clip space to the screen */
	float zscale = depth_scale();

	device.viewport_scale[0] = device.viewport.Width * 0.5f;
	device.viewport_scale[1] = -(float)device.viewport.Height * 0.5f;
	device.viewport_scale[2] = zscale * (device.viewport.MaxZ - device.viewport.MinZ);
	device.viewport_scale[3] = 0.0f;
	device.viewport_offset[0] = device.viewport.X + device.viewport.Width * 0.5f;
	device.viewport_offset[1] = device.viewport.Y + device.viewport.Height * 0.5f;
	device.viewport_offset[2] = zscale * device.viewport.MinZ;
	device.viewport_offset[3] = 0.0f;
	if (!(device.shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS))
	{
		constants_store(VERTEX_CONSTANT_BIAS - 38, device.viewport_scale, 1);
		constants_store(VERTEX_CONSTANT_BIAS - 37, device.viewport_offset, 1);
	}
}

/* the NV2A's screen space (the game's units) to the RSX's clip space for the bound target, its pixel centres at
integers (Direct3D's) moved to halves (the RSX's), the menus centred; ps3_rsx_vp.c's epilogue reads them */
static void screen_constants_update(void)
{
	float zscale = depth_scale();
	float range = device.viewport.MaxZ - device.viewport.MinZ;
	float width = (float)device.target_width, height = (float)device.target_height;
	float values[2][4];

	values[0][0] = 2.0f / width;
	values[0][1] = -2.0f / height;
	values[0][2] = range > 1.0e-6f ? 2.0f / (zscale * range) : 0.0f;
	values[0][3] = 0.0f;
	values[1][0] = (2.0f * (float)ui_offset + 1.0f) / width - 1.0f;
	values[1][1] = 1.0f - 1.0f / height;
	values[1][2] = range > 1.0e-6f ? -2.0f * device.viewport.MinZ / range - 1.0f : 0.0f;
	values[1][3] = 0.0f;
	constants_store(_rsx_vp_constant_screen_scale, values, 2);
}

static void constants_initialize(void)
{
	static const float fixed[4][4] =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		/* as nv2a_vsh.c: a computed address can fall just short of its integer */
		{ 0.001f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
	};

	memcpy(device.constants[_rsx_vp_constant_default], fixed, sizeof(fixed));
	constants_mark(0, _rsx_vp_constant_count - 1);
}

/* ---------- render targets: RSX surfaces in place of the memory their Data names */

struct render_target
{
	struct render_target *next;
	DWORD data;
	/* in pixels: the game's units (units_*) times scale */
	unsigned long width;
	unsigned long height;
	unsigned long units_width;
	unsigned long units_height;
	float scale[2];
	BOOL depth;
	void *memory;
	uint32_t offset;
	uint32_t pitch;
	uint32_t bytes;
	unsigned long last_rendered;
};

#define RENDER_TARGET_BUCKET_COUNT 256

static struct render_target *render_target_buckets[RENDER_TARGET_BUCKET_COUNT];
/* moves when targets go (render_targets_evict): bind_targets looks them up again */
static unsigned long render_target_generation;
/* moves when what a texture stage binds can change: the frame, a target made, gone or first drawn in a frame, a
texture's RSX copy or button texture (bind_textures binds its kept stages again) */
static unsigned long texture_epoch;

static struct render_target **render_target_bucket(DWORD data)
{
	return &render_target_buckets[((data >> 12) ^ (data >> 20)) % RENDER_TARGET_BUCKET_COUNT];
}

static BOOL format_is_depth(DWORD format)
{
	return format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
}

/* the pixel edge of a coordinate in the game's units (d3d8_gl.c's scaled_pixel) */
static long scaled_pixel(float coordinate, float scale)
{
	float value = coordinate * scale + 0.5f;
	long pixel = (long)value;

	/* (toward zero: a negative value with a fraction rounds down one more) */
	if ((float)pixel > value)
		pixel--;
	return pixel;
}

/* data 0: the device's own targets, for a draw without a colour or depth surface. The screen's targets and the
secondary target get the screen's scale (d3d8_gl.c's render_target_get); the screen's are tiled, its depth culled. */
static struct render_target *render_target_lookup(DWORD data, unsigned long units_width, unsigned long units_height,
	BOOL depth)
{
	struct render_target *target;
	BOOL screen = units_width == (unsigned long)halo_screen_width() && units_height == SCREEN_HEIGHT;
	float scale[2] = { 1.0f, 1.0f };
	unsigned long width, height;

	if (screen || (!depth && units_width == SECONDARY_TARGET_WIDTH && units_height == SECONDARY_TARGET_HEIGHT))
	{
		scale[0] = screen_scale[0];
		scale[1] = screen_scale[1];
	}
	width = (unsigned long)scaled_pixel((float)units_width, scale[0]);
	height = (unsigned long)scaled_pixel((float)units_height, scale[1]);
	for (target = *render_target_bucket(data); target; target = target->next)
	{
		if (target->data == data && target->width == width && target->height == height && target->depth == depth)
			return target;
	}
	target = calloc(1, sizeof(*target));
	if (!target)
		return NULL;
	target->memory = ps3_gpu_target_allocate((uint32_t)width, (uint32_t)height, depth != FALSE, screen && data,
		screen && depth && data, &target->offset, &target->pitch, &target->bytes);
	if (!target->memory)
	{
		platform_log("Direct3D: no RSX memory for a %lux%lu target", width, height);
		free(target);
		return NULL;
	}
	target->data = data;
	target->width = width;
	target->height = height;
	target->units_width = units_width;
	target->units_height = units_height;
	target->scale[0] = scale[0];
	target->scale[1] = scale[1];
	target->depth = depth;
	target->next = *render_target_bucket(data);
	*render_target_bucket(data) = target;
	texture_epoch++;
	return target;
}

static struct render_target *render_target_get(const struct surface_words *surface, BOOL depth)
{
	struct xgpu_texture_description description;

	if (!surface->valid || !surface->data)
		return NULL;
	xgpu_texture_describe(surface->format, surface->size, &description);
	if (format_is_depth(description.format) != depth || !description.width || !description.height)
		return NULL;
	return render_target_lookup(surface->data, description.width, description.height, depth);
}

static void surface_words_read(const D3DSurface *surface, struct surface_words *words)
{
	words->valid = surface != NULL;
	words->data = surface ? surface->Data : 0;
	words->format = surface ? surface->Format : 0;
	words->size = surface ? surface->Size : 0;
}

/* every target (the screen's size changed), freed once the frames in flight are done with them */
static void render_targets_evict(void)
{
	unsigned long index;

	if (spu.on)
	{
		spu_d3d_record_screen *screen;

		/* (the SPU idle: the game thread has the command buffer) */
		ps3_host_lock_enter(spu.gpu_lock);
		for (index = 0; index < spu.target_count; index++)
			ps3_gpu_target_free(spu.targets[index].memory, spu.targets[index].bytes);
		spu.target_count = 0;
		particles_targets_free();
		scaled_target_free();
		ps3_host_lock_leave(spu.gpu_lock);
		screen = record_begin(_record_screen, sizeof(*screen), 0, 0);
		if (screen)
		{
			screen->width = (uint32_t)screen_width;
			screen->scale[0] = screen_scale[0];
			screen->scale[1] = screen_scale[1];
			screen->pad = 0;
			record_commit();
		}
		return;
	}

	for (index = 0; index < RENDER_TARGET_BUCKET_COUNT; index++)
	{
		while (render_target_buckets[index])
		{
			struct render_target *target = render_target_buckets[index];

			render_target_buckets[index] = target->next;
			ps3_gpu_target_free(target->memory, target->bytes);
			free(target);
		}
	}
	particles_targets_free();
	scaled_target_free();
	render_target_generation++;
	texture_epoch++;
}

/* the colour target last drawn with this data, for a texture over it */
static struct render_target *render_target_find(DWORD data)
{
	struct render_target *target, *best = NULL;

	for (target = *render_target_bucket(data); target; target = target->next)
	{
		if (target->data == data && data && !target->depth && (!best || target->last_rendered > best->last_rendered))
			best = target;
	}
	return best;
}

/* the half resolution particles (display.half_resolution_particles; rasterizer_transparent_geometry.c's): the effects
in front of every other transparent surface drawn into targets of half the frame's size, then blended over it */
static struct
{
	/* the game's thread's: the half targets' memory, made at the first use, freed with the screen's targets */
	void *color_memory, *depth_memory;
	uint32_t color_bytes, depth_bytes;
	spu_d3d_record_particles targets;
	/* the Direct3D thread's: on between the records, with the frame's targets it began on; bound, the last bind's; the
	half targets' part drawn, half of the frame's surface */
	BOOL on, bound;
	spu_d3d_record_particles half;
	struct render_target *main_color, *main_depth;
	unsigned long width, height;
} particles;

/* the dynamic resolution (display.dynamic_resolution; render.c): the screen's 3D view drawn into a target of the
screen's size at a scale the RSX's time chose (ps3_gpu_dynamic_scale), then scaled up over the screen for the HUD */
static struct
{
	/* the game's thread's: the view's target, made with the device, freed with the screen's targets */
	void *memory;
	uint32_t bytes;
	spu_d3d_record_scale target;
	/* the Direct3D thread's: on between the records; the screen's colour and depth its first bind found, the view's
	size and pixels a unit at the scale */
	BOOL on;
	spu_d3d_record_scale view;
	float factor;
	struct render_target *color, *depth;
	unsigned long width, height;
	float scale[2];
} scaled;

/* the targets the last bind_targets found */
static struct
{
	struct render_target *color, *depth;
} targets_bound;

/* the surface a colour target and depth draw into: between the scale records the screen's colour is the view's
target at the scale (TRUE) */
static BOOL target_surface(struct render_target *color, struct render_target *depth, struct ps3_gpu_surface *surface)
{
	BOOL smaller = scaled.on && color->data == scaled.view.data && !color->depth;

	surface->depth_offset = depth->offset;
	surface->depth_pitch = depth->pitch;
	surface->smaller = (uint32_t)smaller;
	if (!smaller)
	{
		surface->color_offset = color->offset;
		surface->color_pitch = color->pitch;
		surface->width = (uint16_t)color->width;
		surface->height = (uint16_t)color->height;
		return FALSE;
	}
	if (!scaled.width)
	{
		scaled.scale[0] = color->scale[0] * scaled.factor;
		scaled.scale[1] = color->scale[1] * scaled.factor;
		scaled.width = (unsigned long)scaled_pixel((float)color->units_width, scaled.scale[0]);
		scaled.height = (unsigned long)scaled_pixel((float)color->units_height, scaled.scale[1]);
		scaled.color = color;
		scaled.depth = depth;
	}
	surface->color_offset = scaled.view.color_offset;
	surface->color_pitch = scaled.view.color_pitch;
	surface->width = (uint16_t)scaled.width;
	surface->height = (uint16_t)scaled.height;
	return TRUE;
}

/* the RSX surface for the current targets; FALSE if there is nothing to draw into. The lookups are kept while the
surfaces' words stay: every draw binds. */
static BOOL bind_targets(BOOL *has_depth)
{
	static struct
	{
		struct surface_words targets[2];
		unsigned long generation;
		struct render_target *color, *depth;
		BOOL has_depth, valid;
	} last;
	struct render_target *color, *depth;
	struct ps3_gpu_surface surface;
	const float *scale;

	if (last.valid && last.generation == render_target_generation &&
		!memcmp(last.targets, device.targets, sizeof(last.targets)))
	{
		color = last.color;
		depth = last.depth;
		*has_depth = last.has_depth;
		goto bind;
	}
	last.valid = FALSE;
	color = render_target_get(&device.targets[0], FALSE);
	depth = render_target_get(&device.targets[1], TRUE);
	if (!color && !depth)
		return FALSE;
	if (!color)
		color = render_target_lookup(0, depth->units_width, depth->units_height, FALSE);
	*has_depth = depth && color && depth->width >= color->width && depth->height >= color->height;
	if (!*has_depth && color)
		depth = render_target_lookup(0, color->units_width, color->units_height, TRUE);
	if (!color || !depth)
		return FALSE;
	memcpy(last.targets, device.targets, sizeof(last.targets));
	last.generation = render_target_generation;
	last.color = color;
	last.depth = depth;
	last.has_depth = *has_depth;
	last.valid = TRUE;

bind:
	/* (render_target_find takes the target with this data last drawn) */
	if (color->last_rendered != device.frame + 1)
	{
		color->last_rendered = device.frame + 1;
		texture_epoch++;
	}
	targets_bound.color = color;
	targets_bound.depth = depth;
	particles.bound = particles.on && color == particles.main_color && depth == particles.main_depth;
	scale = target_surface(color, depth, &surface) ? scaled.scale : color->scale;
	if (particles.bound)
	{
		/* the frame's units over the half target's pixels */
		surface.color_offset = particles.half.color_offset;
		surface.color_pitch = particles.half.color_pitch;
		surface.depth_offset = particles.half.depth_offset;
		surface.depth_pitch = particles.half.depth_pitch;
		surface.width = (uint16_t)particles.width;
		surface.height = (uint16_t)particles.height;
		surface.smaller = 0;
		ps3_gpu_set_surface(&surface);
		device.target_width = color->units_width;
		device.target_height = color->units_height;
		device.target_scale[0] = scale[0] * 0.5f;
		device.target_scale[1] = scale[1] * 0.5f;
		device.target_pixel_width = particles.width;
		device.target_pixel_height = particles.height;
		return TRUE;
	}
	ps3_gpu_set_surface(&surface);
	device.target_width = color->units_width;
	device.target_height = color->units_height;
	device.target_scale[0] = scale[0];
	device.target_scale[1] = scale[1];
	device.target_pixel_width = surface.width;
	device.target_pixel_height = surface.height;
	return TRUE;
}

/* the game's thread: the half targets for the screen's size, made with the device and again with the screen's (before
the screen's targets: their memory the same whichever thread draws); FALSE without them */
static BOOL particles_targets(void)
{
	uint32_t width = ((uint32_t)scaled_pixel((float)halo_screen_width(), screen_scale[0]) + 1) / 2;
	uint32_t height = ((uint32_t)scaled_pixel((float)SCREEN_HEIGHT, screen_scale[1]) + 1) / 2;
	spu_d3d_record_particles *targets = &particles.targets;

	if (particles.color_memory)
		return targets->width == width && targets->height == height;
	memset(targets, 0, sizeof(*targets));
	particles.color_memory = ps3_gpu_target_allocate(width, height, 0, 1, 0, &targets->color_offset,
		&targets->color_pitch, &particles.color_bytes);
	particles.depth_memory = ps3_gpu_target_allocate(width, height, 1, 1, 0, &targets->depth_offset,
		&targets->depth_pitch, &particles.depth_bytes);
	if (!particles.color_memory || !particles.depth_memory)
	{
		platform_log("Direct3D: no RSX memory for the half resolution particles' %ux%u targets", (unsigned)width,
			(unsigned)height);
		particles_targets_free();
		return FALSE;
	}
	targets->width = width;
	targets->height = height;
	platform_log("Direct3D: the half resolution particles' targets: %ux%u", (unsigned)width, (unsigned)height);
	return TRUE;
}

static void particles_targets_free(void)
{
	if (particles.color_memory)
		ps3_gpu_target_free(particles.color_memory, particles.color_bytes);
	if (particles.depth_memory)
		ps3_gpu_target_free(particles.depth_memory, particles.depth_bytes);
	particles.color_memory = particles.depth_memory = NULL;
}

/* rasterizer_transparent_geometry.c: the draws from 1 to 0 into the half targets, then blended over the frame's;
FALSE when they cannot be (the draws go to the frame's) */
int d3d8_particles_half(int on)
{
	spu_d3d_record_particles *payload;

	if (!on)
	{
		record_begin(_record_particles, 0, 0, 0);
		record_commit();
		return TRUE;
	}
	if (!device.gpu_ready || !particles_targets())
		return FALSE;
	payload = record_begin(_record_particles, sizeof(*payload), 1, 0);
	if (!payload)
		return FALSE;
	*payload = particles.targets;
	record_commit();
	return TRUE;
}

/* the Direct3D thread: the half targets bound, their colour cleared to the frame seen whole (transmittance 1), their
depth the frame's; nothing when the frame's targets are not the screen's */
static void particles_begin(const spu_d3d_record_particles *half)
{
	struct ps3_gpu_surface surface, frame;
	BOOL has_depth;

	if (particles.on || !bind_targets(&has_depth) || !has_depth ||
		(targets_bound.color->width + 1) / 2 != half->width || (targets_bound.color->height + 1) / 2 != half->height)
		return;
	particles.on = TRUE;
	particles.half = *half;
	particles.main_color = targets_bound.color;
	particles.main_depth = targets_bound.depth;
	/* (half of the frame's surface: the view's at its scale) */
	target_surface(particles.main_color, particles.main_depth, &frame);
	particles.width = (frame.width + 1UL) / 2;
	particles.height = (frame.height + 1UL) / 2;
	surface.color_offset = half->color_offset;
	surface.color_pitch = half->color_pitch;
	surface.depth_offset = half->depth_offset;
	surface.depth_pitch = half->depth_pitch;
	surface.width = (uint16_t)particles.width;
	surface.height = (uint16_t)particles.height;
	surface.smaller = 0;
	ps3_gpu_set_surface(&surface);
	ps3_gpu_clear(0xf2, 0xff000000u, 0, 0, 0, (uint32_t)particles.width, (uint32_t)particles.height);
	ps3_gpu_particles_depth(particles.main_depth->offset, particles.main_depth->pitch, frame.width, frame.height);
}

/* the half target's colour over the frame's: theirs plus the frame's times the transmittance */
static void particles_end(void)
{
	struct ps3_gpu_surface surface;

	if (!particles.on)
		return;
	particles.on = FALSE;
	particles.bound = FALSE;
	target_surface(particles.main_color, particles.main_depth, &surface);
	ps3_gpu_set_surface(&surface);
	ps3_gpu_particles_composite(particles.half.color_offset, particles.half.color_pitch, (uint32_t)particles.width,
		(uint32_t)particles.height);
}

/* the game's thread: the view's target for the screen's size, made with the device and again with the screen's;
FALSE without it */
static BOOL scaled_target(void)
{
	uint32_t width = (uint32_t)scaled_pixel((float)halo_screen_width(), screen_scale[0]);
	uint32_t height = (uint32_t)scaled_pixel((float)SCREEN_HEIGHT, screen_scale[1]);

	if (scaled.memory)
		return TRUE;
	memset(&scaled.target, 0, sizeof(scaled.target));
	scaled.memory = ps3_gpu_target_allocate(width, height, 0, 1, 0, &scaled.target.color_offset,
		&scaled.target.color_pitch, &scaled.bytes);
	if (!scaled.memory)
	{
		platform_log("Direct3D: no RSX memory for the dynamic resolution's %ux%u target", (unsigned)width,
			(unsigned)height);
		return FALSE;
	}
	platform_log("Direct3D: the dynamic resolution's target: %ux%u", (unsigned)width, (unsigned)height);
	return TRUE;
}

/* (with the Direct3D thread idle: no view is drawn into it) */
static void scaled_target_free(void)
{
	if (scaled.memory)
		ps3_gpu_target_free(scaled.memory, scaled.bytes);
	scaled.memory = NULL;
	scaled.on = FALSE;
}

/* the game's thread, between frames: the target for the setting turned on (EXTRA OPTIONS' DYNAMIC RESOLUTION), made
with the Direct3D thread idle */
static void scaled_target_commit(void)
{
	if (scaled.memory || !device.created || !device.gpu_ready || !config_boolean("display.dynamic_resolution"))
		return;
	direct3d_drain();
	spu_chunks_wait();
	scaled_target();
}

/* render.c: the primary window's 3D view from its clear (1) to before the HUD (0), at the scale the RSX's time chose;
FALSE when it is drawn whole */
int d3d8_dynamic_resolution(int on)
{
	spu_d3d_record_scale *payload;
	uint32_t eighths;

	if (!on)
	{
		record_begin(_record_scale, 0, 0, 0);
		record_commit();
		return TRUE;
	}
	if (!device.gpu_ready || !config_boolean("display.dynamic_resolution"))
		return FALSE;
	/* (a whole view's record, 8 eighths, marks its frame as a measure of the scale; the target is made between
	frames, halo_screen_commit) */
	eighths = ps3_gpu_dynamic_scale();
	if (!scaled.memory)
		eighths = 8;
	payload = record_begin(_record_scale, sizeof(*payload), 1, 0);
	if (!payload)
		return FALSE;
	*payload = scaled.target;
	payload->data = device.back_buffer.Data;
	payload->eighths = eighths;
	record_commit();
	return eighths < 8;
}

/* the Direct3D thread: the frame's scale for ps3_gpu.c's measure; under 8 eighths, the screen's colour drawn into the
view's target at the scale until scale_end */
static void scale_begin(const spu_d3d_record_scale *view)
{
	if (view->eighths >= 1 && view->eighths <= 8)
		ps3_gpu_frame_scale(view->eighths);
	if (scaled.on || view->eighths < 1 || view->eighths > 7)
		return;
	scaled.on = TRUE;
	scaled.view = *view;
	scaled.factor = view->eighths / 8.0f;
	scaled.color = scaled.depth = NULL;
	scaled.width = scaled.height = 0;
	texture_epoch++;
}

/* the view's target over the whole screen's colour; its depth stays the view's, which the HUD does not test */
static void scale_end(void)
{
	struct ps3_gpu_surface surface;

	if (!scaled.on)
		return;
	scaled.on = FALSE;
	texture_epoch++;
	if (!scaled.width)
		return;
	surface.color_offset = scaled.color->offset;
	surface.color_pitch = scaled.color->pitch;
	surface.depth_offset = scaled.depth->offset;
	surface.depth_pitch = scaled.depth->pitch;
	surface.width = (uint16_t)scaled.color->width;
	surface.height = (uint16_t)scaled.color->height;
	surface.smaller = 0;
	ps3_gpu_set_surface(&surface);
	ps3_gpu_scale_up(scaled.view.color_offset, scaled.view.color_pitch, (uint32_t)scaled.width,
		(uint32_t)scaled.height);
}

/* a draw into the half target: the colour blended as into the frame's, the alpha (the transmittance) times the
frame's factor; no stencil, which the half target does not have */
static void particles_state(struct ps3_gpu_state *state)
{
	state->color_mask |= GCM_COLOR_MASK_A;
	state->stencil_function = 0;
	state->stencil_write_mask = 0;
	if (!state->blend)
	{
		state->blend = 1;
		state->blend_source = D3DBLEND_ONE;
		state->blend_destination = D3DBLEND_ZERO;
		state->blend_equation = GCM_FUNC_ADD;
	}
	state->blend_source_alpha = D3DBLEND_ZERO;
	state->blend_destination_alpha = state->blend_destination;
}

/* ---------- the lights' bounds (rasterizer_xbox_environment.c's diffuse lights): a light's draws give 0 outside its
distance attenuation's box, so a scissor and the depth bounds test leave those pixels out */

/* 0: the lights' draws over their whole surfaces */
#define RSX_LIGHT_BOUNDS 1

static struct
{
	/* the game's thread's: rasterizer_set_frustum_z's world to clip columns (c[-96] to c[-93]); a record on */
	double columns[4][4];
	BOOL recorded;
	/* the Direct3D thread's */
	BOOL on;
	spu_d3d_record_light_bounds bounds;
} light_bounds;

void d3d8_light_bounds_transform(const float *columns)
{
	int index;

	for (index = 0; index < 16; index++)
		light_bounds.columns[index / 4][index % 4] = columns[index];
}

/* the box's clip space bounds from its corners, the box grown by 1/64 and 1/1024 of a unit for the texel coordinates'
rounding, the depth by 2^-12 for the vertices'; with a corner not in front, any x and y */
static void light_bounds_find(const float *center, float half_size, spu_d3d_record_light_bounds *bounds)
{
	double half = half_size * (1.0 + 1.0 / 64.0) + 1.0 / 1024.0, clip[8][4];
	double x0 = 1.0, y0 = 1.0, x1 = -1.0, y1 = -1.0, z0 = 1.0, z1 = 0.0;
	int corner, column, behind = 0;

	for (corner = 0; corner < 8; corner++)
	{
		double x = center[0] + ((corner & 1) ? half : -half), y = center[1] + ((corner & 2) ? half : -half);
		double z = center[2] + ((corner & 4) ? half : -half);

		for (column = 0; column < 4; column++)
			clip[corner][column] = x * light_bounds.columns[column][0] + y * light_bounds.columns[column][1] +
				z * light_bounds.columns[column][2] + light_bounds.columns[column][3];
		if (clip[corner][3] <= 0.0)
		{
			behind++;
			continue;
		}
		{
			double px = clip[corner][0] / clip[corner][3], py = clip[corner][1] / clip[corner][3];
			double pz = clip[corner][2] / clip[corner][3];

			x0 = px < x0 ? px : x0;
			x1 = px > x1 ? px : x1;
			y0 = py < y0 ? py : y0;
			y1 = py > y1 ? py : y1;
			z0 = pz < z0 ? pz : z0;
			z1 = pz > z1 ? pz : z1;
		}
	}
	if (behind)
	{
		x0 = y0 = -2.0;
		x1 = y1 = 2.0;
		z0 = -1.0;
		/* the depth's largest is a front corner's when it falls toward the eye's plane: each edge's crossing below 0 */
		for (corner = 0; corner < 8 && behind < 8; corner++)
		{
			int axis;

			for (axis = 1; axis < 8; axis <<= 1)
			{
				const double *a = clip[corner], *b = clip[corner | axis];

				if ((corner & axis) || (a[3] > 0.0) == (b[3] > 0.0))
					continue;
				if (a[2] + (b[2] - a[2]) * (a[3] / (a[3] - b[3])) >= 0.0)
					z1 = 2.0;
			}
		}
		if (behind == 8)
			x0 = 1.0, x1 = -1.0;
	}
	/* (past the viewport's edges, also moved in by ui_offset, -2 and 2 cover them: the scissor cuts the rest) */
	bounds->x0 = (float)(x0 < -2.0 ? -2.0 : x0 > 2.0 ? 2.0 : x0);
	bounds->y0 = (float)(y0 < -2.0 ? -2.0 : y0 > 2.0 ? 2.0 : y0);
	bounds->x1 = (float)(x1 < -2.0 ? -2.0 : x1 > 2.0 ? 2.0 : x1);
	bounds->y1 = (float)(y1 < -2.0 ? -2.0 : y1 > 2.0 ? 2.0 : y1);
	z0 -= 1.0 / 4096.0;
	z1 += 1.0 / 4096.0;
	bounds->z0 = (float)(z0 < 0.0 ? 0.0 : z0 > 1.0 ? 1.0 : z0);
	bounds->z1 = (float)(z1 < 0.0 ? 0.0 : z1 > 1.0 ? 1.0 : z1);
	bounds->pad[0] = bounds->pad[1] = 0;
}

/* the draws after it light only the box center +- half_size; center NULL: their whole surfaces again */
void d3d8_light_bounds(const float *center, float half_size)
{
	spu_d3d_record_light_bounds *payload;

	if (!center || !RSX_LIGHT_BOUNDS)
	{
		if (light_bounds.recorded)
		{
			record_begin(_record_light_bounds, 0, 0, 0);
			record_commit();
			light_bounds.recorded = FALSE;
		}
		return;
	}
	payload = record_begin(_record_light_bounds, sizeof(*payload), 1, 0);
	if (!payload)
		return;
	light_bounds_find(center, half_size, payload);
	record_commit();
	light_bounds.recorded = TRUE;
}

/* the pixel a coordinate falls in, as scaled_pixel's */
static long light_bounds_pixel(float value)
{
	long pixel = (long)value;

	if ((float)pixel > value)
		pixel--;
	return pixel;
}

/* the box's pixels and 2 more each side (rounding) inside the scissor; the screen's y grows down from y1 */
static void light_bounds_scissor(long *left, long *top, long *right, long *bottom)
{
	const spu_d3d_record_light_bounds *b = &light_bounds.bounds;
	float offset_x = device.viewport_offset[0] + (float)ui_offset + 0.5f, offset_y = device.viewport_offset[1] + 0.5f;
	long x0 = light_bounds_pixel((b->x0 * device.viewport_scale[0] + offset_x) * device.target_scale[0]) - 2;
	long x1 = light_bounds_pixel((b->x1 * device.viewport_scale[0] + offset_x) * device.target_scale[0]) + 3;
	long y0 = light_bounds_pixel((b->y1 * device.viewport_scale[1] + offset_y) * device.target_scale[1]) - 2;
	long y1 = light_bounds_pixel((b->y0 * device.viewport_scale[1] + offset_y) * device.target_scale[1]) + 3;

	if (x0 > *left)
		*left = x0;
	if (x1 < *right)
		*right = x1;
	if (y0 > *top)
		*top = y0;
	if (y1 < *bottom)
		*bottom = y1;
}

/* ---------- textures: RSX copies, converted again when their texels change */

struct texture_entry
{
	struct texture_entry *next;
	DWORD data, format_word, size_word, palette_hash;
	__uint64 content_hash;
	unsigned long checked_frame;
	unsigned long last_used_frame;
	/* texels in the texture cache, checked since its header was last set up (texture_get) */
	BOOL checked;
	struct xgpu_texture_description description;
	void *memory;
	unsigned long bytes;
	struct ps3_gpu_texture gpu;
	/* a button icon's texture drawn instead (hud_hires_rsx.c): for its bitmap, for the style it was checked with;
	for a placeholder's sprites, checked each frame */
	int hires_style;
	BOOL hires, placeholder;
	struct ps3_gpu_texture hires_gpu;
};

#define TEXTURE_BUCKET_COUNT 1024
#define TEXTURE_IDLE_FRAMES 1800
/* xbox_texture_cache.c's XBOX_TEXTURE_CACHE_SIZE */
#define TEXTURE_CACHE_BYTES 0x1600000UL

void *physical_memory_get_texture_cache_base_address(void);
int hud_hires_rsx_style(void);
int hud_hires_rsx_override(const unsigned char *texels, unsigned long width, unsigned long height,
	unsigned long level0_size, struct ps3_gpu_texture *gpu);
int hud_hires_rsx_placeholder(DWORD data, struct ps3_gpu_texture *gpu);
int hud_hires_rsx_asset_texture(long asset, int style, struct ps3_gpu_texture *gpu);
void hud_hires_rsx_placeholders(unsigned int words[8][2]);

static struct texture_entry *texture_buckets[TEXTURE_BUCKET_COUNT];
static unsigned long texture_frame = 1;

static unsigned long texture_bucket_index(DWORD data, DWORD format_word, DWORD size_word)
{
	return ((data >> 7) ^ (format_word * 2654435761UL) ^ size_word) % TEXTURE_BUCKET_COUNT;
}

static BOOL spu_address(const void *address);

/* the bytes from which a texture's hash goes to the SPU: smaller ones are quicker here than a wait for its tasks */
#define TEXTURE_HASH_SPU_BYTES (256 * 1024)

/* spu_vertices.h's texture hash: an SPU task, waited for, or here */
static __uint64 texture_content_hash(const unsigned char *source, unsigned long size)
{
	static uint32_t result[4] __attribute__((aligned(16)));
	spu_vertices_task *task = NULL;

	if (size >= TEXTURE_HASH_SPU_BYTES && size < 0x80000000UL && !((uintptr_t)source & 15) && spu_address(source) &&
		spu_address(result) && (task = ps3_spu_vertices_task()) != NULL)
	{
		spu_vertices_texture *hash = (spu_vertices_texture *)task;

		memset(hash, 0, sizeof(*hash));
		hash->kind = SPU_VERTICES_HASH;
		hash->source = (uint32_t)(uintptr_t)source;
		hash->output = (uint32_t)(uintptr_t)result;
		hash->units = (uint32_t)size;
		ps3_spu_vertices_commit();
		ps3_spu_vertices_wait();
		statistics.texture_hashed_spu += size;
		return (__uint64)result[0] << 32 | result[1];
	}
	return xgpu_texture_hash(source, size);
}

static BOOL spu_address(const void *address);

static void texture_upload(struct texture_entry *entry, const unsigned char *source, const DWORD *palette)
{
	const struct xgpu_texture_description *description = &entry->description;
	unsigned long faces = description->cube_map ? 6 : 1;
	unsigned long levels = description->linear ? 1 : description->levels;
	unsigned long source_face = xgpu_texture_face_size(description);
	unsigned long face_bytes = 0, face, level, offset, remap;
	unsigned char *memory;
	uint32_t memory_offset;
	spu_vertices_texture texture_task;

	for (level = 0; level < levels; level++)
		face_bytes += xgpu_texture_rsx_level_bytes(description, level);
	/* the RSX's cube faces start 128-aligned, as the Xbox's */
	if (description->cube_map)
		face_bytes = (face_bytes + 127) & ~127UL;
	memory = ps3_gpu_memory_allocate((uint32_t)(face_bytes * faces), 128, _ps3_gpu_memory_texture, &memory_offset);
	if (!memory)
	{
		platform_log("Direct3D: no RSX memory for a %lux%lu texture (format %02lx)", description->width,
			description->height, (unsigned long)description->format);
		return;
	}
	for (face = 0; face < faces; face++)
	{
		const unsigned char *face_source = source + face * source_face;
		unsigned char *face_memory = memory + face * face_bytes;
		spu_vertices_task *task = NULL;

		/* an SPU task, which the RSX reads after (ps3_gpu.c waits before each flush); or here */
		if (!palette && !((uintptr_t)face_source & 15) && spu_address(face_source) && spu_address(face_memory) &&
			xgpu_texture_rsx_task(description, &texture_task) &&
			(task = ps3_spu_vertices_task()) != NULL)
		{
			*(spu_vertices_texture *)task = texture_task;
			((spu_vertices_texture *)task)->source = (uint32_t)(uintptr_t)face_source;
			((spu_vertices_texture *)task)->output = (uint32_t)(uintptr_t)face_memory;
			ps3_spu_vertices_commit();
			statistics.texture_spu_bytes += face_bytes;
			continue;
		}
		for (level = 0, offset = 0; level < levels; level++)
		{
			xgpu_texture_rsx_convert_level(description, level, face_source + xgpu_texture_level_offset(description, level),
				palette, face_memory + offset);
			offset += xgpu_texture_rsx_level_bytes(description, level);
		}
	}
	ps3_gpu_textures_written();
	ps3_gpu_memory_free(entry->memory, (uint32_t)entry->bytes, _ps3_gpu_memory_texture);
	entry->memory = memory;
	entry->bytes = face_bytes * faces;
	statistics.texture_uploads++;
	statistics.texture_bytes += face_bytes * faces;

	memset(&entry->gpu, 0, sizeof(entry->gpu));
	entry->gpu.offset = memory_offset;
	entry->gpu.width = (uint16_t)description->width;
	entry->gpu.height = (uint16_t)description->height;
	entry->gpu.depth = (uint16_t)description->depth;
	entry->gpu.levels = (uint8_t)levels;
	entry->gpu.dimension = description->depth > 1 ? GCM_TEXTURE_DIMS_3D : GCM_TEXTURE_DIMS_2D;
	entry->gpu.cube = description->cube_map != FALSE;
	/* DXT blocks lie in order, swizzled or not, as on the Xbox; 8- and 16-bit texels where the RSX has their
	format, with a remap to the Xbox's channels; the rest as A8R8G8B8 */
	entry->gpu.format = xgpu_texture_rsx_format(description, &remap);
	entry->gpu.remap = (uint32_t)remap;
	entry->gpu.pitch = (uint32_t)xgpu_texture_rsx_pitch(description, 0);
	/* Xbox linear textures take texel coordinates; a swizzled one's pitch is a row's bytes */
	if (description->linear && !description->compressed)
		entry->gpu.format |= GCM_TEXTURE_FORMAT_LIN | GCM_TEXTURE_FORMAT_UNRM;
	else if (!description->compressed)
		entry->gpu.pitch = (uint32_t)(xgpu_texture_rsx_level_bytes(description, 0) /
			(description->height * description->depth));
	texture_epoch++;
}

/* the descriptions' words compared (newlib's memcmp goes a byte at a time where an address is not 8-aligned) */
static BOOL gpu_texture_same(const struct ps3_gpu_texture *a, const struct ps3_gpu_texture *b)
{
	const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
	uint32_t differ = 0;
	unsigned long index;

	for (index = 0; index < sizeof(*a) / 4; index++)
		differ |= x[index] ^ y[index];
	return !differ;
}

/* the texture cache's memory, which only the cache's disk reads fill */
static BOOL texture_cache_holds(const unsigned char *source, unsigned long size)
{
	const unsigned char *base = physical_memory_get_texture_cache_base_address();

	return base && source >= base && source + size <= base + TEXTURE_CACHE_BYTES;
}

/* once a frame, or fresh: the texels checked and uploaded if they changed, and the button textures looked up */
static struct texture_entry *texture_check(struct texture_entry *entry, BOOL fresh, const DWORD *palette,
	DWORD palette_hash)
{
	DWORD data = entry->data;
	const unsigned char *source;
	unsigned long size;
	__uint64 hash;
	uint64_t span;
	BOOL cached, changed;
	int style = hud_hires_rsx_style();

	/* once a frame: the game writes texels with the CPU (the text cache) */
	entry->checked_frame = texture_frame;
	entry->placeholder = hud_hires_rsx_placeholder(data, &entry->hires_gpu);
	if (entry->placeholder)
		return entry;
	source = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
	size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
	if (!size || !platform_is_contiguous(source) || !platform_is_contiguous(source + size - 1))
		return entry->memory ? entry : NULL;
	/* the cache sets a header up again, Lock 0, for each texture it loads (fresh); a CPU writer of texels there
	(Bink, once it decodes) must put Lock to 0 after each write */
	cached = texture_cache_holds(source, size);
	if (cached && entry->memory && entry->checked && !fresh && entry->hires_style == style)
		return entry;
	span = ps3_timer_now();
	/* the texture cache's texels change only at its loads there (fresh), which upload without a hash; the others are
	hashed at each check */
	if (cached)
	{
		changed = !entry->memory || !entry->checked || fresh;
	}
	else
	{
		hash = texture_content_hash(source, size) ^ palette_hash;
		changed = !entry->memory || hash != entry->content_hash;
		entry->content_hash = hash;
	}
	span = ps3_timer_add(_ps3_timer_texture_hash, span);
	if (changed)
	{
		texture_upload(entry, source, palette);
		entry->hires_style = -2;
		ps3_timer_add(_ps3_timer_texture_upload, span);
	}
	/* (which bitmap is here may have changed with the texels) */
	if (entry->memory && entry->hires_style != style)
	{
		const struct xgpu_texture_description *description = &entry->description;

		entry->hires_style = style;
		entry->hires = !palette && !description->linear && !description->cube_map && description->depth == 1 &&
			hud_hires_rsx_override(source, description->width, description->height, description->levels > 1 ?
			xgpu_texture_level_offset(description, 1) : xgpu_texture_face_size(description), &entry->hires_gpu);
	}
	entry->checked = cached && entry->memory;
	return entry->memory ? entry : NULL;
}

static struct texture_entry *texture_get(struct stage_texture *texture, const DWORD *palette, DWORD palette_hash)
{
	DWORD data = texture->data, format_word = texture->format, size_word = texture->size;
	BOOL fresh = texture->fresh != 0, placeholder, hires;
	struct texture_entry **bucket = &texture_buckets[texture_bucket_index(data, format_word, size_word)];
	struct texture_entry *entry, *result;
	struct ps3_gpu_texture hires_gpu;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word &&
			entry->palette_hash == palette_hash)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->palette_hash = palette_hash;
		xgpu_texture_describe(format_word, size_word, &entry->description);
		entry->next = *bucket;
		*bucket = entry;
	}
	entry->last_used_frame = texture_frame;
	texture->fresh = 0;
	if (entry->checked_frame == texture_frame && !fresh)
		return entry->memory || entry->placeholder ? entry : NULL;
	/* (a button texture taken or let go moves texture_epoch, as an upload does) */
	placeholder = entry->placeholder;
	hires = entry->hires;
	hires_gpu = entry->hires_gpu;
	result = texture_check(entry, fresh, palette, palette_hash);
	if (entry->placeholder != placeholder || entry->hires != hires || !gpu_texture_same(&entry->hires_gpu, &hires_gpu))
		texture_epoch++;
	return result;
}

/* drops the textures not used for a while */
static void textures_begin_frame(void)
{
	unsigned long index;

	texture_frame++;
	texture_epoch++;
	if (texture_frame % 600)
		return;
	for (index = 0; index < TEXTURE_BUCKET_COUNT; index++)
	{
		struct texture_entry **link = &texture_buckets[index];

		while (*link)
		{
			struct texture_entry *entry = *link;

			if (texture_frame - entry->last_used_frame > TEXTURE_IDLE_FRAMES)
			{
				*link = entry->next;
				ps3_gpu_memory_free(entry->memory, (uint32_t)entry->bytes, _ps3_gpu_memory_texture);
				free(entry);
			}
			else
			{
				link = &entry->next;
			}
		}
	}
}

/* the RSX copies, and the part that no draw used in the last STATISTICS_FRAMES frames */
static void textures_measure(unsigned long *count, unsigned long *bytes, unsigned long *unused_bytes)
{
	struct texture_entry *entry;
	unsigned long index;

	*count = *bytes = *unused_bytes = 0;
	for (index = 0; index < TEXTURE_BUCKET_COUNT; index++)
	{
		for (entry = texture_buckets[index]; entry; entry = entry->next)
		{
			if (!entry->memory)
				continue;
			(*count)++;
			*bytes += entry->bytes;
			if (texture_frame - entry->last_used_frame > STATISTICS_FRAMES)
				*unused_bytes += entry->bytes;
		}
	}
}

/* ---------- texture stages */

static unsigned long stage_texture_mode(int stage)
{
	return (render_state[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
}

static unsigned char address_mode(DWORD mode)
{
	switch (mode)
	{
	case D3DTADDRESS_MIRROR: return GCM_TEXTURE_MIRRORED_REPEAT;
	case D3DTADDRESS_CLAMP: return GCM_TEXTURE_CLAMP_TO_EDGE;
	case D3DTADDRESS_BORDER: return GCM_TEXTURE_BORDER;
	case D3DTADDRESS_CLAMPTOEDGE: return GCM_TEXTURE_CLAMP_TO_EDGE;
	default: return GCM_TEXTURE_REPEAT;
	}
}

static unsigned char anisotropy_level(DWORD maximum)
{
	return maximum >= 16 ? 7 : maximum >= 12 ? 6 : maximum >= 10 ? 5 : maximum >= 8 ? 4 : maximum >= 6 ? 3 :
		maximum >= 4 ? 2 : maximum >= 2 ? 1 : 0;
}

static void sampler_fill(int stage, struct ps3_gpu_texture *texture)
{
	DWORD *state = texture_state[stage];
	BOOL mipmapped = texture->levels > 1 && !(texture->format & GCM_TEXTURE_FORMAT_UNRM);
	DWORD mip_filter = mipmapped ? state[D3DTSS_MIPFILTER] : D3DTEXF_NONE;
	BOOL point = state[D3DTSS_MINFILTER] == D3DTEXF_POINT;
	long bias = (long)(dword_to_float(state[D3DTSS_MIPMAPLODBIAS]) * 256.0f);
	DWORD top = texture->levels ? texture->levels - 1 : 0;
	DWORD lowest = state[D3DTSS_MAXMIPLEVEL] < top ? state[D3DTSS_MAXMIPLEVEL] : top;

	if (mip_filter == D3DTEXF_NONE)
		texture->minify = point ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;
	else if (mip_filter == D3DTEXF_POINT)
		texture->minify = point ? GCM_TEXTURE_NEAREST_MIPMAP_NEAREST : GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
	else
		texture->minify = point ? GCM_TEXTURE_NEAREST_MIPMAP_LINEAR : GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
	texture->magnify = state[D3DTSS_MAGFILTER] == D3DTEXF_POINT ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;
	texture->wrap_s = address_mode(state[D3DTSS_ADDRESSU]);
	texture->wrap_t = address_mode(state[D3DTSS_ADDRESSV]);
	texture->wrap_r = address_mode(state[D3DTSS_ADDRESSW]);
	texture->anisotropy = state[D3DTSS_MINFILTER] == D3DTEXF_ANISOTROPIC ? anisotropy_level(state[D3DTSS_MAXANISOTROPY]) : 0;
	/* 4.8 fixed point: the bias signed in 13 bits */
	texture->bias = (uint16_t)(bias < -4096 ? -4096 : bias > 4095 ? 4095 : bias) & 0x1fff;
	texture->minimum_level = (uint16_t)(mipmapped ? lowest << 8 : 0);
	texture->maximum_level = (uint16_t)(mipmapped ? top << 8 : 0);
	texture->border = state[D3DTSS_BORDERCOLOR];
}

/* a stage's palette as last read: its data, its words, as ARGB (palettes keep the Xbox's order: trap 27), their hash */
static struct
{
	DWORD data;
	DWORD hash;
	DWORD words[256] __attribute__((aligned(16)));
	DWORD entries[256];
} palettes[TEXTURE_STAGES];

/* whether the stage's palette holds the words last read (the game's one palette never changes; reading it took 256
swaps and multiplies a draw) */
static BOOL palette_same(int stage)
{
	DWORD data = device.stages[stage].palette;
	const DWORD *words;

	if (!data || palettes[stage].data != data || !palettes[stage].hash)
		return FALSE;
	words = (const DWORD *)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
	if ((uintptr_t)words & 15)
		return !memcmp(words, palettes[stage].words, sizeof(palettes[stage].words));
	return ps3_vmx_equal(words, palettes[stage].words, sizeof(palettes[stage].words));
}

/* the palette's hash (0 for none) and its 256 entries as ARGB, read again when its words change */
static DWORD palette_load(int stage, const DWORD **entries)
{
	DWORD data = device.stages[stage].palette, hash = 2166136261UL;
	int index;

	if (!data)
		return 0;
	if (!palette_same(stage))
	{
		memcpy(palettes[stage].words, PLATFORM_PHYSICAL_TO_VIRTUAL(data), sizeof(palettes[stage].words));
		for (index = 0; index < 256; index++)
		{
			palettes[stage].entries[index] = __builtin_bswap32(palettes[stage].words[index]);
			hash = (hash ^ palettes[stage].entries[index]) * 16777619UL;
		}
		palettes[stage].data = data;
		palettes[stage].hash = hash ? hash : 1;
	}
	*entries = palettes[stage].entries;
	return palettes[stage].hash;
}

/* (the timer build) the specular split the stamped draws are in, -1 with no span on (ps3_timers_draw_stamps); the
textures bound for the draw while one is */
static int draw_stamps_part = -1;
static struct ps3_gpu_texture bound_textures[TEXTURE_STAGES];
static uint8_t bound_texture_mask;

/* bind_textures' part of the fragment key: each stage's sampler, the stages whose texel coordinates are scaled */
struct texture_samplers
{
	uint8_t types[TEXTURE_STAGES];
	uint8_t scaled;
};

/* each stage's last bind: on, its texture set (else its unit off); palette, its texture's palette read. Kept while
the stage's texture and sampler states, the texture modes, its palette's words and texture_epoch stay */
static struct
{
	/* (8-aligned: its memset is stores of doublewords, not newlib's) */
	struct ps3_gpu_texture bound __attribute__((aligned(8)));
	BOOL kept, on, palette;
	unsigned long epoch;
	uint8_t sampler_type, scaled;
} stage_binds[TEXTURE_STAGES];
/* the RSX's state and the button icons' style the stages were bound with */
static uint32_t binds_gpu_epoch;
static int binds_style;

/* a stage's texture to its unit, and its bind kept (stage_binds) */
static void stage_bind(int stage)
{
	struct stage_texture *texture = &device.stages[stage];
	unsigned long mode = stage_texture_mode(stage);
	struct ps3_gpu_texture *bound = &stage_binds[stage].bound;
	struct render_target *target;
	BOOL hires = FALSE;

	stage_binds[stage].on = FALSE;
	stage_binds[stage].palette = FALSE;
	stage_binds[stage].scaled = 0;
	stage_binds[stage].sampler_type = mode == 0x11 ? _rsx_sampler_2d : _rsx_sampler_none;
	device.texture_scale[stage][0] = device.texture_scale[stage][1] = 1.0f;
	if (!texture->bound || !texture->data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
	{
		ps3_gpu_set_texture(stage, NULL);
		return;
	}
	memset(&stage_binds[stage].bound, 0, sizeof(stage_binds[stage].bound));
	target = render_target_find(texture->data);
	if (target)
	{
		struct xgpu_texture_description description;
		/* (the screen's colour drawn smaller so far: the view's target at its scale) */
		BOOL smaller = scaled.on && scaled.width && target == scaled.color;
		const float *scale = smaller ? scaled.scale : target->scale;

		/* a rendered target: its surface, linear, its one level */
		xgpu_texture_describe(texture->format, texture->size, &description);
		bound->offset = smaller ? scaled.view.color_offset : target->offset;
		bound->pitch = smaller ? scaled.view.color_pitch : target->pitch;
		bound->width = (uint16_t)(smaller ? scaled.width : target->width);
		bound->height = (uint16_t)(smaller ? scaled.height : target->height);
		bound->depth = 1;
		bound->format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN |
			(description.linear ? GCM_TEXTURE_FORMAT_UNRM : 0);
		bound->levels = 1;
		bound->dimension = GCM_TEXTURE_DIMS_2D;
		/* the game's texel coordinates are in its units; the pixels are the screen's scale (ps3_rsx_fp.c) */
		if (description.linear && (scale[0] != 1.0f || scale[1] != 1.0f))
		{
			stage_binds[stage].scaled = (uint8_t)(1 << stage);
			device.texture_scale[stage][0] = scale[0];
			device.texture_scale[stage][1] = scale[1];
		}
	}
	else
	{
		const DWORD *palette = NULL;
		DWORD palette_hash = 0;
		struct texture_entry *entry;

		if (((texture->format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b)
		{
			palette_hash = palette_load(stage, &palette);
			stage_binds[stage].palette = palette_hash != 0;
		}
		entry = texture_get(texture, palette_hash ? palette : NULL, palette_hash);
		if (!entry)
		{
			ps3_gpu_set_texture(stage, NULL);
			return;
		}
		hires = entry->hires || entry->placeholder;
		*bound = hires ? entry->hires_gpu : entry->gpu;
	}
	sampler_fill(stage, bound);
	/* a button icon's texture is drawn smaller than it is: every level, filtered (hud_hires_rsx.c) */
	if (hires)
	{
		bound->minify = GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
		bound->magnify = GCM_TEXTURE_LINEAR;
		bound->minimum_level = 0;
		bound->maximum_level = (uint16_t)((bound->levels - 1) << 8);
	}
	ps3_gpu_set_texture(stage, bound);
	stage_binds[stage].sampler_type = bound->cube ? _rsx_sampler_cube :
		bound->dimension == GCM_TEXTURE_DIMS_3D ? _rsx_sampler_3d : _rsx_sampler_2d;
	stage_binds[stage].on = TRUE;
}

/* the stages' textures; a stage whose inputs stay keeps its bind, which its unit still has (bound_textures) */
static void bind_textures(struct texture_samplers *samplers)
{
	uint32_t changes = draw_state_changes, gpu_epoch = ps3_gpu_state_epoch();
	int style = hud_hires_rsx_style(), stage;

	if (gpu_epoch != binds_gpu_epoch || style != binds_style)
	{
		binds_gpu_epoch = gpu_epoch;
		binds_style = style;
		texture_epoch++;
	}
	samplers->scaled = 0;
	bound_texture_mask = 0;
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		if (!stage_binds[stage].kept || stage_binds[stage].epoch != texture_epoch ||
			(changes & (CHANGED_TEXTURE_MODES | CHANGED_SAMPLER(stage) | CHANGED_TEXTURE(stage))) ||
			(stage_binds[stage].palette && !palette_same(stage)))
		{
			stage_bind(stage);
			/* (after its own upload, if any: an upload moves the epoch for the other stages) */
			stage_binds[stage].epoch = texture_epoch;
			stage_binds[stage].kept = TRUE;
			ps3_timer_counts[_ps3_timer_draw_textures]++;
		}
		samplers->types[stage] = stage_binds[stage].sampler_type;
		samplers->scaled |= stage_binds[stage].scaled;
		if (draw_stamps_part >= 0 && stage_binds[stage].on)
		{
			bound_textures[stage] = stage_binds[stage].bound;
			bound_texture_mask |= (uint8_t)(1 << stage);
		}
	}
	draw_state_changes &= ~CHANGED_TEXTURES;
}

/* ---------- fragment programs, cached by their key */

struct fragment_entry
{
	struct fragment_entry *next;
	DWORD hash;
	struct rsx_fragment_key key;
	struct rsx_fragment_program program;
	BOOL translated;
	/* (the timer build) its key for the stamped draws; its last stamped draws' parts (specular, effects), textures */
	uint16_t stamp_key;
	uint8_t stamp_parts[2];
	uint8_t stamp_texture_mask;
	struct ps3_gpu_texture stamp_textures[TEXTURE_STAGES];
	/* its index in the Direct3D SPU's table, -1 none */
	int32_t spu_index;
};

#define FRAGMENT_BUCKETS 1024
/* the stamped pixel shaders logged, most RSX time first */
#define SHADERS_LOGGED 8

static struct fragment_entry *fragment_buckets[FRAGMENT_BUCKETS];
static unsigned long fragment_program_count, fragment_translated_count;
static BOOL fragment_pack_loaded;
/* the last draw's (consecutive draws mostly use the same pixel shader) */
static struct fragment_entry *fragment_last;

typedef char fragment_key_size_check[sizeof(struct rsx_fragment_key) % 4 == 0 ? 1 : -1];

/* a rotate and an exclusive or a word, then one mix: a multiply a word (FNV's) waited for the one before on the PPE */
static DWORD hash_words(const void *data, unsigned long size)
{
	const DWORD *words = data;
	DWORD hash = 0;

	for (size /= 4; size; size--)
		hash = ((hash << 5) | (hash >> 27)) ^ *words++;
	hash ^= hash >> 16;
	hash *= 0x85ebca6bUL;
	return hash ^ (hash >> 13);
}

/* the keys' words compared (newlib's memcmp goes a byte at a time where an address is not 8-aligned) */
static BOOL fragment_key_same(const struct rsx_fragment_key *a, const struct rsx_fragment_key *b)
{
	const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
	unsigned long index;

	for (index = 0; index < sizeof(*a) / 4; index++)
	{
		if (x[index] != y[index])
			return FALSE;
	}
	return TRUE;
}

static void spu_fragment_add(struct fragment_entry *entry);

static void fragment_entry_add(struct fragment_entry *entry)
{
	struct fragment_entry **bucket = &fragment_buckets[entry->hash % FRAGMENT_BUCKETS];

	fragment_program_count++;
	entry->stamp_key = (uint16_t)(fragment_program_count < PS3_DRAW_STAMP_KEYS ? fragment_program_count :
		PS3_DRAW_STAMP_KEYS - 1);
	spu_fragment_add(entry);
	entry->next = *bucket;
	/* (the game thread's shaders_log walks the buckets: the entry whole before it is seen) */
	__sync_synchronize();
	*bucket = entry;
}

/* the shader pack's programs (tools/ps3_shader_pack.c) into the cache: its keys are not translated at a draw */
static void fragment_pack_load(void)
{
	struct fragment_entry *entries = calloc(rsx_packed_fragment_count ? rsx_packed_fragment_count : 1,
		sizeof(*entries));
	uint32_t index;
#ifdef PS3_SHADER_PACK_CHECK
	unsigned long pack_differences = 0;
#endif

	fragment_pack_loaded = TRUE;
	if (!entries)
		return;
	for (index = 0; index < rsx_packed_fragment_count; index++)
	{
		struct fragment_entry *entry = &entries[index];

		entry->key = rsx_packed_fragments[index].key;
		entry->hash = hash_words(&entry->key, sizeof(entry->key));
		entry->program = rsx_packed_fragments[index].program;
		entry->translated = TRUE;
		fragment_entry_add(entry);
#ifdef PS3_SHADER_PACK_CHECK
		/* the PC's translation against the PPE's (make SHADER_PACK_CHECK=1) */
		{
			struct rsx_fragment_program program;
			const struct rsx_fragment_program *packed = &entry->program;

			if (rsx_fragment_program_translate(&entry->key, &program) != 0 ||
				program.word_count != packed->word_count || program.control != packed->control ||
				program.register_count != packed->register_count || program.texcoord_mask != packed->texcoord_mask ||
				program.uniform_count != packed->uniform_count ||
				memcmp(program.uniforms, packed->uniforms, sizeof(program.uniforms)) ||
				memcmp(program.microcode, packed->microcode, program.word_count * sizeof(uint32_t)))
			{
				pack_differences++;
			}
			rsx_fragment_program_free(&program);
		}
#endif
	}
#ifdef PS3_SHADER_PACK_CHECK
	for (index = 0; index < rsx_packed_vertex_count; index++)
	{
		struct rsx_vertex_program program;
		const struct rsx_vertex_program *packed = &rsx_packed_vertices[index].program;

		if (rsx_vertex_program_translate(rsx_packed_vertices[index].nv2a, rsx_packed_vertices[index].nv2a_count,
			&program) != 0 || program.instruction_count != packed->instruction_count ||
			program.input_mask != packed->input_mask || program.output_mask != packed->output_mask ||
			program.register_count != packed->register_count ||
			memcmp(program.microcode, packed->microcode, program.instruction_count * 4 * sizeof(uint32_t)))
		{
			pack_differences++;
		}
		rsx_vertex_program_free(&program);
	}
	platform_log("Direct3D: shader pack check: %lu programs differ from the PPE's translation", pack_differences);
#endif
	platform_log("Direct3D: shader pack: %lu pixel shaders, %lu vertex shaders",
		(unsigned long)rsx_packed_fragment_count, (unsigned long)rsx_packed_vertex_count);
}

static const struct rsx_fragment_program *fragment_program_get(const struct rsx_fragment_key *key)
{
	struct fragment_entry *entry;
	DWORD hash;

	if (fragment_last && fragment_key_same(&fragment_last->key, key))
		return fragment_last->translated ? &fragment_last->program : NULL;
	if (!fragment_pack_loaded)
		fragment_pack_load();
	hash = hash_words(key, sizeof(*key));
	for (entry = fragment_buckets[hash % FRAGMENT_BUCKETS]; entry; entry = entry->next)
	{
		if (entry->hash == hash && fragment_key_same(&entry->key, key))
			break;
	}
	if (!entry)
	{
		char text[RSX_FRAGMENT_KEY_TEXT];

		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->hash = hash;
		entry->key = *key;
		entry->translated = rsx_fragment_program_translate(key, &entry->program) == 0;
		fragment_translated_count++;
		/* a key the pack lacks, as a line of port/ps3/shader_keys.txt */
		rsx_fragment_key_text(key, text);
		if (!entry->translated)
			platform_log("Direct3D: cannot translate a pixel shader: %s", text);
		else
			platform_log("Direct3D: pixel shader %lu not in the pack, translated (%lu words): %s",
				fragment_translated_count, (unsigned long)entry->program.word_count, text);
		fragment_entry_add(entry);
	}
	fragment_last = entry;
	return entry->translated ? &entry->program : NULL;
}

/* (the timer build) a stamped frame's draw: its RSX time by its pixel shader and part (ps3_timers_draw_stamps) */
static void draw_stamp(void)
{
	struct fragment_entry *entry = fragment_last;

	if (draw_stamps_part < 0 || !entry ||
		!ps3_gpu_draw_stamp((uint16_t)(entry->stamp_key | draw_stamps_part << PS3_DRAW_STAMP_PART_SHIFT)))
		return;
	entry->stamp_parts[draw_stamps_part > _ps3_stamp_specular_fog] = (uint8_t)draw_stamps_part;
	entry->stamp_texture_mask = bound_texture_mask;
	memcpy(entry->stamp_textures, bound_textures, sizeof(bound_textures));
}

/* the specular split the next draws are in: the first from the span's start, the next after each split */
static void draw_stamps_split(int part)
{
	if (draw_stamps_part >= 0 && part >= _ps3_timer_specular_lights && part < _ps3_timer_specular_fog)
		draw_stamps_part = part - _ps3_timer_specular_lights + 1;
}

/* the part the next draws are in (-1 none): a span starts where they were in none */
static void draw_stamps_set(int part)
{
	int was = draw_stamps_part;

	draw_stamps_part = part;
	if (part < 0 || was < 0)
		ps3_gpu_draw_stamps(part >= 0);
}

/* ps3_timers.h's stamp parts */
static const char *const stamp_part_names[] =
{
	"lights", "specular lightmaps", "reflection masks", "mirrors", "reflections", "transparent", "fog", "the rest",
	"weather", "particles", "particle systems", "contrails", "other effect shaders", "model", "generic", "chicago",
	"water", "glass", "meter", "plasma", "other shaders", "decals", "detail objects",
};

typedef char stamp_names_check[sizeof(stamp_part_names) / sizeof(stamp_part_names[0]) == _ps3_stamp_count &&
	offsetof(struct fragment_entry, stamp_texture_mask) == offsetof(struct fragment_entry, stamp_parts) + 2 &&
	PS3_DRAW_STAMP_KEYS <= 1 << PS3_DRAW_STAMP_PART_SHIFT && _ps3_stamp_count < 0xffff >> PS3_DRAW_STAMP_PART_SHIFT ?
	1 : -1];

/* ps3_timers.h: the stamped pixel shaders of the specular part, then of the effects pass, by their RSX time, most
first, each with its program and last textures */
static void shaders_log(void)
{
	static const char *const list_names[] = { "specular part", "effects pass" };
	struct fragment_entry *top[SHADERS_LOGGED], *entry;
	unsigned long bucket, count, index, list;
	double frames = ps3_draw_stamp_frames;

	if (!ps3_draw_stamp_frames)
		return;
	for (list = 0; list < 2; list++)
	{
		const uint64_t *times = ps3_draw_stamp_nanoseconds[list];

		count = 0;
		for (bucket = 0; bucket < FRAGMENT_BUCKETS; bucket++)
		{
			for (entry = fragment_buckets[bucket]; entry; entry = entry->next)
			{
				uint64_t time = times[entry->stamp_key];

				if (!ps3_draw_stamp_draws[list][entry->stamp_key] ||
					(count == SHADERS_LOGGED && times[top[count - 1]->stamp_key] >= time))
				{
					continue;
				}
				index = count < SHADERS_LOGGED ? count++ : SHADERS_LOGGED - 1;
				for (; index > 0 && times[top[index - 1]->stamp_key] < time; index--)
					top[index] = top[index - 1];
				top[index] = entry;
			}
		}
		ps3_log_printf("[time] %s's pixel shaders on the RSX, ms a stamped frame (%u frames stamped, %u draws past "
			"their stamps)\n", list_names[list], (unsigned)ps3_draw_stamp_frames, (unsigned)ps3_draw_stamp_dropped);
		for (index = 0; index < count; index++)
		{
			char textures[256];
			unsigned long length = 0;
			int stage;

			entry = top[index];
			textures[0] = 0;
			for (stage = 0; stage < TEXTURE_STAGES && length < sizeof(textures); stage++)
			{
				const struct ps3_gpu_texture *texture = &entry->stamp_textures[stage];

				if (!(entry->stamp_texture_mask & (1 << stage)))
					continue;
				length += (unsigned long)snprintf(textures + length, sizeof(textures) - length,
					"%s %d %s %ux%u format %02x, %u levels, filters %u/%u, anisotropy %u", length ? ";" : "", stage,
					texture->cube ? "cube" : texture->dimension == GCM_TEXTURE_DIMS_3D ? "3D" : "2D",
					(unsigned)texture->width, (unsigned)texture->height, (unsigned)texture->format,
					(unsigned)texture->levels, (unsigned)texture->minify, (unsigned)texture->magnify,
					(unsigned)texture->anisotropy);
			}
			/* (a slot is 16 bytes: an instruction, or the inline constant after one) */
			ps3_log_printf("[time] pixel shader %u (%s): %.2f ms, %.1f draws; %u slots, %u registers, %u "
				"combiners, texture modes %05x; textures:%s\n", (unsigned)entry->stamp_key,
				entry->stamp_parts[list] < _ps3_stamp_count ? stamp_part_names[entry->stamp_parts[list]] : "?",
				times[entry->stamp_key] / frames * 1.0e-6, ps3_draw_stamp_draws[list][entry->stamp_key] / frames,
				(unsigned)(entry->program.word_count / 4), (unsigned)entry->program.register_count,
				(unsigned)(entry->key.combiner_count & 0xff), (unsigned)entry->key.texture_modes,
				length ? textures : " none");
		}
	}
}

static DWORD blend_equation(DWORD operation);

/* the fragments the draw's blend leaves the target unchanged by (8-bit), when it writes no depth or stencil: a kill
mode (rsx_fragment_key's), 0 none */
static uint8_t unchanged_fragments(void)
{
	DWORD *rs = render_state;
	DWORD write = rs[D3DRS_COLORWRITEENABLE], source = rs[D3DRS_SRCBLEND], destination = rs[D3DRS_DESTBLEND];
	DWORD equation = blend_equation(rs[D3DRS_BLENDOP]);
	unsigned channels = ((write & D3DCOLORWRITEENABLE_RED) ? 1u : 0u) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? 2u : 0u) | ((write & D3DCOLORWRITEENABLE_BLUE) ? 4u : 0u) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? 8u : 0u);

	if (!rs[D3DRS_ALPHABLENDENABLE] || !channels || (rs[D3DRS_ZENABLE] && rs[D3DRS_ZWRITEENABLE]))
		return 0;
	if (rs[D3DRS_STENCILENABLE] && (rs[D3DRS_STENCILWRITEMASK] & 0xff) && (rs[D3DRS_STENCILFAIL] != D3DSTENCILOP_KEEP ||
		rs[D3DRS_STENCILZFAIL] != D3DSTENCILOP_KEEP || rs[D3DRS_STENCILPASS] != D3DSTENCILOP_KEEP))
		return 0;
	/* alpha 0 adds nothing; one more, the colour of the premultiplied blend */
	if (equation == GCM_FUNC_ADD && source == D3DBLEND_SRCALPHA &&
		(destination == D3DBLEND_INVSRCALPHA || destination == D3DBLEND_ONE))
		return RSX_KILL_ZERO | 8u << 4;
	if (equation == GCM_FUNC_ADD && source == D3DBLEND_ONE && destination == D3DBLEND_INVSRCALPHA)
		return (uint8_t)(RSX_KILL_ZERO | (channels | 8u) << 4);
	if (source == D3DBLEND_ONE && destination == D3DBLEND_ONE)
	{
		if (equation == GCM_FUNC_ADD || equation == GCM_FUNC_REVERSE_SUBTRACT || equation == GCM_FUNC_MAX)
			return (uint8_t)(RSX_KILL_ZERO | channels << 4);
		if (equation == GCM_FUNC_MIN)
			return (uint8_t)(RSX_KILL_ONE | channels << 4);
	}
	/* the multiply: 1 keeps the target */
	if (equation == GCM_FUNC_ADD && source == D3DBLEND_DESTCOLOR && destination == D3DBLEND_ZERO)
		return (uint8_t)(RSX_KILL_ONE | channels << 4);
	return 0;
}

/* unchanged_fragments' alpha 0: the alpha test kills it (apply_raster_state) when the draw has none of its own */
#define KILL_ALPHA_ZERO (RSX_KILL_ZERO | 8u << 4)

/* whether the draw's own alpha test fails this alpha (0 to 255): GCM's functions 0x200 to 0x207 are bits for less,
equal and greater */
static int alpha_test_removes(unsigned alpha)
{
	DWORD *rs = render_state;
	DWORD function = (rs[D3DRS_ALPHAFUNC] ? rs[D3DRS_ALPHAFUNC] : GCM_NEVER) - GCM_NEVER;
	unsigned reference = rs[D3DRS_ALPHAREF] & 0xff;

	if (!rs[D3DRS_ALPHATESTENABLE] || function > 7)
		return 0;
	return !(function & (alpha < reference ? 1u : alpha == reference ? 2u : 4u));
}

static uint8_t fragment_kill_mode(void)
{
	uint8_t mode = unchanged_fragments();

	if (mode == KILL_ALPHA_ZERO && !render_state[D3DRS_ALPHATESTENABLE])
		return 0;
	/* the KIL's fragments all have its alpha: when the draw's own alpha test removes that, the KIL only costs */
	if ((mode & (8u << 4)) && alpha_test_removes((mode & 3) == RSX_KILL_ONE ? 255 : 0))
		return 0;
	return mode;
}

static void fragment_key_build(struct rsx_fragment_key *key)
{
	DWORD *rs = render_state;
	int index;

	for (index = 0; index < 8; index++)
	{
		key->rgb_inputs[index] = rs[D3DRS_PSRGBINPUTS0 + index];
		key->rgb_outputs[index] = rs[D3DRS_PSRGBOUTPUTS0 + index];
		key->alpha_inputs[index] = rs[D3DRS_PSALPHAINPUTS0 + index];
		key->alpha_outputs[index] = rs[D3DRS_PSALPHAOUTPUTS0 + index];
	}
	key->final_inputs_abcd = rs[D3DRS_PSFINALCOMBINERINPUTSABCD];
	key->final_inputs_efg = rs[D3DRS_PSFINALCOMBINERINPUTSEFG];
	key->combiner_count = rs[D3DRS_PSCOMBINERCOUNT];
	key->texture_modes = rs[D3DRS_PSTEXTUREMODES];
	key->dot_mapping = rs[D3DRS_PSDOTMAPPING];
	key->input_texture = rs[D3DRS_PSINPUTTEXTURE];
	key->compare_mode = rs[D3DRS_PSCOMPAREMODE];
	for (index = 0; index < TEXTURE_STAGES; index++)
	{
		key->alpha_kill[index] = texture_state[index][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[index] = (unsigned char)((texture_state[index][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key->fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)rs[D3DRS_FOGTABLEMODE];
	key->kill_mode = fragment_kill_mode();
}

#define TEXTURE_STATE(stage, type) (D3DRS_MAX + (stage) * D3DTSS_MAX + (type))
#define STAGE_STATE_PARTS(stage) \
	[TEXTURE_STATE(stage, D3DTSS_ADDRESSU) ... TEXTURE_STATE(stage, D3DTSS_MAXANISOTROPY)] = CHANGED_SAMPLER(stage), \
	[TEXTURE_STATE(stage, D3DTSS_BORDERCOLOR)] = CHANGED_SAMPLER(stage), \
	[TEXTURE_STATE(stage, D3DTSS_COLORSIGN)] = CHANGED_FRAGMENT_KEY, \
	[TEXTURE_STATE(stage, D3DTSS_ALPHAKILL)] = CHANGED_FRAGMENT_KEY

/* the parts of the draw state each state goes into (states_apply): fragment_key_build's and sampler_fill's states */
static const uint8_t state_parts[D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX] =
{
	[D3DRS_PSALPHAINPUTS0 ... D3DRS_PSFINALCOMBINERINPUTSEFG] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSALPHAOUTPUTS0 ... D3DRS_PSCOMPAREMODE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSRGBOUTPUTS0 ... D3DRS_PSCOMBINERCOUNT] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSDOTMAPPING] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSINPUTTEXTURE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_FOGENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_FOGTABLEMODE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_PSTEXTUREMODES] = CHANGED_TEXTURE_MODES | CHANGED_FRAGMENT_KEY,
	/* fragment_kill_mode's */
	[D3DRS_ALPHATESTENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_ALPHABLENDENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_SRCBLEND] = CHANGED_FRAGMENT_KEY,
	[D3DRS_DESTBLEND] = CHANGED_FRAGMENT_KEY,
	[D3DRS_BLENDOP] = CHANGED_FRAGMENT_KEY,
	[D3DRS_COLORWRITEENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_ZENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_ZWRITEENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILENABLE] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILWRITEMASK] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILFAIL] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILZFAIL] = CHANGED_FRAGMENT_KEY,
	[D3DRS_STENCILPASS] = CHANGED_FRAGMENT_KEY,
	STAGE_STATE_PARTS(0), STAGE_STATE_PARTS(1), STAGE_STATE_PARTS(2), STAGE_STATE_PARTS(3),
};

/* (sampler_fill's states but the border colour are ADDRESSU to MAXANISOTROPY: 10 to 18) */
typedef char state_parts_check[D3DTSS_MAXSTAGES == TEXTURE_STAGES && D3DTSS_ADDRESSU == 10 &&
	D3DTSS_MAXANISOTROPY == 18 ? 1 : -1];

/* the last draw's fragment program, kept while the fragment key's states and the samplers stay (a program not found
is looked for again) */
static struct
{
	BOOL valid;
	struct texture_samplers samplers;
	const struct rsx_fragment_program *program;
} fragment_kept;

/* the draw's fragment program; its key made only when the states it reads or the samplers changed */
static const struct rsx_fragment_program *fragment_program_find(const struct texture_samplers *samplers, uint64_t *span)
{
	const struct texture_samplers *kept = &fragment_kept.samplers;
	const struct rsx_fragment_program *program;
	struct rsx_fragment_key key;

	if (fragment_kept.valid && !(draw_state_changes & CHANGED_FRAGMENT_KEY) && samplers->scaled == kept->scaled &&
		samplers->types[0] == kept->types[0] && samplers->types[1] == kept->types[1] &&
		samplers->types[2] == kept->types[2] && samplers->types[3] == kept->types[3])
		return fragment_kept.program;
	/* (fragment_key_build writes every field but these) */
	fragment_key_build(&key);
	memcpy(key.sampler_type, samplers->types, sizeof(key.sampler_type));
	key.texture_scaled = samplers->scaled;
	*span = ps3_timer_add(_ps3_timer_draw_key, *span);
	rsx_fragment_key_clean(&key);
	program = fragment_program_get(&key);
	fragment_kept.valid = program != NULL;
	fragment_kept.samplers = *samplers;
	fragment_kept.program = program;
	draw_state_changes &= ~CHANGED_FRAGMENT_KEY;
	ps3_timer_counts[_ps3_timer_draw_fragment]++;
	return program;
}

/* a colour constant converted again only when its state word changes (a model's draw changes a few of them) */
static void colour_update(DWORD now, DWORD *source, BOOL valid, float *out)
{
	if (valid && now == *source)
		return;
	*source = now;
	color_to_vec4(now, out);
}

/* a float constant's words as the states hold them (a float taken from a word goes through memory on the PPE) */
static void words_set(float *out, DWORD x, DWORD y, DWORD z, DWORD w)
{
	DWORD *words = (DWORD *)out;

	words[0] = x;
	words[1] = y;
	words[2] = z;
	words[3] = w;
}

/* the per-draw constants (ps3_rsx_program.h's _rsx_uniform_*) */
static void fragment_uniforms_build(float uniforms[_rsx_uniform_count][4])
{
	static DWORD colours[19];
	static float converted[_rsx_uniform_texture_scale][4];
	static BOOL valid;
	DWORD *rs = render_state;
	int stage;

	for (stage = 0; stage < 8; stage++)
	{
		colour_update(rs[D3DRS_PSCONSTANT0_0 + stage], &colours[stage * 2], valid, converted[_rsx_uniform_c0 + stage]);
		colour_update(rs[D3DRS_PSCONSTANT1_0 + stage], &colours[stage * 2 + 1], valid,
			converted[_rsx_uniform_c1 + stage]);
	}
	colour_update(rs[D3DRS_PSFINALCOMBINERCONSTANT0], &colours[16], valid, converted[_rsx_uniform_final_c0]);
	colour_update(rs[D3DRS_PSFINALCOMBINERCONSTANT1], &colours[17], valid, converted[_rsx_uniform_final_c1]);
	colour_update(rs[D3DRS_FOGCOLOR], &colours[18], valid, converted[_rsx_uniform_fog_color]);
	valid = TRUE;
	words_set(converted[_rsx_uniform_fog], rs[D3DRS_FOGSTART], rs[D3DRS_FOGEND], rs[D3DRS_FOGDENSITY], 0);
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		DWORD *state = texture_state[stage];

		words_set(converted[_rsx_uniform_bump_matrix + stage], state[D3DTSS_BUMPENVMAT00], state[D3DTSS_BUMPENVMAT01],
			state[D3DTSS_BUMPENVMAT10], state[D3DTSS_BUMPENVMAT11]);
		words_set(converted[_rsx_uniform_bump_luminance + stage], state[D3DTSS_BUMPENVLSCALE],
			state[D3DTSS_BUMPENVLOFFSET], 0, 0);
	}
	memcpy(uniforms, converted, sizeof(converted));
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		uniforms[_rsx_uniform_texture_scale + stage][0] = device.texture_scale[stage][0];
		uniforms[_rsx_uniform_texture_scale + stage][1] = device.texture_scale[stage][1];
		uniforms[_rsx_uniform_texture_scale + stage][2] = 1.0f;
		uniforms[_rsx_uniform_texture_scale + stage][3] = 1.0f;
	}
}

/* ---------- raster state */

static DWORD blend_equation(DWORD operation)
{
	switch (operation)
	{
	case D3DBLENDOP_SUBTRACT: return GCM_FUNC_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:
	case D3DBLENDOP_REVSUBTRACTSIGNED: return GCM_FUNC_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN: return GCM_FUNC_MIN;
	case D3DBLENDOP_MAX: return GCM_FUNC_MAX;
	default: return GCM_FUNC_ADD;
	}
}

/* the NV2A's scissor follows the viewport, which keeps split screens apart (d3d8_gl.c); in the target's pixels, made
again only when the viewport or the target changes (a pixel's float conversions go through memory on the PPE) */
static void viewport_scissor(long *left, long *top, long *right, long *bottom)
{
	static struct
	{
		DWORD x, y, width, height;
		float scale[2];
		unsigned long pixel_width, pixel_height;
		long left, top, right, bottom;
		BOOL valid;
	} kept;

	if (!kept.valid || kept.x != device.viewport.X || kept.y != device.viewport.Y ||
		kept.width != device.viewport.Width || kept.height != device.viewport.Height ||
		!(kept.scale[0] == device.target_scale[0]) || !(kept.scale[1] == device.target_scale[1]) ||
		kept.pixel_width != device.target_pixel_width || kept.pixel_height != device.target_pixel_height)
	{
		kept.x = device.viewport.X;
		kept.y = device.viewport.Y;
		kept.width = device.viewport.Width;
		kept.height = device.viewport.Height;
		kept.scale[0] = device.target_scale[0];
		kept.scale[1] = device.target_scale[1];
		kept.pixel_width = device.target_pixel_width;
		kept.pixel_height = device.target_pixel_height;
		kept.left = scaled_pixel((float)device.viewport.X, device.target_scale[0]);
		kept.top = scaled_pixel((float)device.viewport.Y, device.target_scale[1]);
		kept.right = scaled_pixel((float)(device.viewport.X + device.viewport.Width), device.target_scale[0]);
		kept.bottom = scaled_pixel((float)(device.viewport.Y + device.viewport.Height), device.target_scale[1]);
		if (kept.left < 0)
			kept.left = 0;
		if (kept.top < 0)
			kept.top = 0;
		if (kept.right > (long)device.target_pixel_width)
			kept.right = (long)device.target_pixel_width;
		if (kept.bottom > (long)device.target_pixel_height)
			kept.bottom = (long)device.target_pixel_height;
		kept.valid = TRUE;
	}
	*left = kept.left;
	*top = kept.top;
	*right = kept.right;
	*bottom = kept.bottom;
}

/* FALSE when the viewport leaves nothing of the target */
static BOOL apply_raster_state(BOOL has_depth)
{
	DWORD *rs = render_state;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	BOOL depth_test = has_depth && rs[D3DRS_ZENABLE];
	struct ps3_gpu_state state;
	long left, top, right, bottom;

	viewport_scissor(&left, &top, &right, &bottom);
	if (light_bounds.on)
		light_bounds_scissor(&left, &top, &right, &bottom);
	if (left >= right || top >= bottom)
		return FALSE;

	memset(&state, 0, sizeof(state));
	state.color_mask = ((write & D3DCOLORWRITEENABLE_RED) ? GCM_COLOR_MASK_R : 0) |
		((write & D3DCOLORWRITEENABLE_GREEN) ? GCM_COLOR_MASK_G : 0) |
		((write & D3DCOLORWRITEENABLE_BLUE) ? GCM_COLOR_MASK_B : 0) |
		((write & D3DCOLORWRITEENABLE_ALPHA) ? GCM_COLOR_MASK_A : 0);
	if (depth_test)
		state.depth_function = rs[D3DRS_ZFUNC] ? rs[D3DRS_ZFUNC] : GCM_NEVER;
	state.depth_write = depth_test && rs[D3DRS_ZWRITEENABLE];
	if (has_depth && rs[D3DRS_STENCILENABLE])
		state.stencil_function = rs[D3DRS_STENCILFUNC] ? rs[D3DRS_STENCILFUNC] : GCM_NEVER;
	/* the Xbox's stencil operations are GCM's, 0 included (zero) */
	state.stencil_reference = rs[D3DRS_STENCILREF] & 0xff;
	state.stencil_mask = rs[D3DRS_STENCILMASK] & 0xff;
	state.stencil_write_mask = rs[D3DRS_STENCILWRITEMASK] & 0xff;
	state.stencil_fail = rs[D3DRS_STENCILFAIL];
	state.stencil_depth_fail = rs[D3DRS_STENCILZFAIL];
	state.stencil_pass = rs[D3DRS_STENCILPASS];
	state.blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
	state.blend_source = state.blend_source_alpha = rs[D3DRS_SRCBLEND];
	state.blend_destination = state.blend_destination_alpha = rs[D3DRS_DESTBLEND];
	state.blend_equation = blend_equation(rs[D3DRS_BLENDOP]);
	state.blend_color = rs[D3DRS_BLENDCOLOR];
	if (rs[D3DRS_ALPHATESTENABLE])
		state.alpha_function = rs[D3DRS_ALPHAFUNC] ? rs[D3DRS_ALPHAFUNC] : GCM_NEVER;
	state.alpha_reference = rs[D3DRS_ALPHAREF] & 0xff;
	/* alpha 0 leaves the target unchanged: without the draw's own alpha test, one kills it (fragment_kill_mode) */
	if (!rs[D3DRS_ALPHATESTENABLE] && unchanged_fragments() == KILL_ALPHA_ZERO)
	{
		state.alpha_function = GCM_GREATER;
		state.alpha_reference = 0;
	}
	/* the picture is not mirrored (rows from the top, as ps3gl's), so the RSX's winding is Direct3D's */
	state.front_face = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW ? GCM_FRONTFACE_CCW : GCM_FRONTFACE_CW;
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
		state.cull_face = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? GCM_CULL_FRONT : GCM_CULL_BACK;
	state.polygon_mode = rs[D3DRS_FILLMODE] ? rs[D3DRS_FILLMODE] : GCM_POLYGON_MODE_FILL;
	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias) */
	state.polygon_offset = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	/* (the floats' words copied as they are) */
	memcpy(&state.polygon_offset_factor, &rs[D3DRS_POLYGONOFFSETZSLOPESCALE], sizeof(float));
	memcpy(&state.polygon_offset_units, &rs[D3DRS_POLYGONOFFSETZOFFSET], sizeof(float));
	state.scissor_x = (uint16_t)left;
	state.scissor_y = (uint16_t)top;
	state.scissor_width = (uint16_t)(right - left);
	state.scissor_height = (uint16_t)(bottom - top);
	state.depth_minimum = device.viewport.MinZ;
	state.depth_maximum = device.viewport.MaxZ;
	if (light_bounds.on && depth_test)
	{
		/* (the stored depths of the viewport's range) */
		float range = device.viewport.MaxZ - device.viewport.MinZ;

		state.depth_bounds = 1;
		state.depth_bounds_minimum = device.viewport.MinZ + light_bounds.bounds.z0 * range;
		state.depth_bounds_maximum = device.viewport.MinZ + light_bounds.bounds.z1 * range;
	}
	if (particles.bound)
		particles_state(&state);
	ps3_gpu_set_state(&state);
	return TRUE;
}

/* ---------- vertex shaders */

static unsigned long vertex_type_bytes(unsigned long type)
{
	switch (type)
	{
	case D3DVSDT_FLOAT1: return 4;
	case D3DVSDT_FLOAT2: return 8;
	case D3DVSDT_FLOAT3: return 12;
	case D3DVSDT_FLOAT4: return 16;
	case D3DVSDT_D3DCOLOR: return 4;
	case D3DVSDT_SHORT1: return 2;
	case D3DVSDT_SHORT2: return 4;
	case D3DVSDT_SHORT3: return 6;
	case D3DVSDT_SHORT4: return 8;
	case D3DVSDT_NORMSHORT1: return 2;
	case D3DVSDT_NORMSHORT2: return 4;
	case D3DVSDT_NORMSHORT3: return 6;
	case D3DVSDT_NORMSHORT4: return 8;
	case D3DVSDT_NORMPACKED3: return 4;
	case D3DVSDT_PBYTE1: return 1;
	case D3DVSDT_PBYTE2: return 2;
	case D3DVSDT_PBYTE3: return 3;
	case D3DVSDT_PBYTE4: return 4;
	case D3DVSDT_FLOAT2H: return 12;
	case SPU_VERTICES_TEXCOORD_WORD: return 4;
	default: return 0;
	}
}

static void parse_declaration(struct vertex_shader_object *object, const DWORD *declaration)
{
	unsigned long stream = 0;
	unsigned long offsets[16] = { 0 };

	for (; declaration && *declaration != D3DVSD_END(); declaration++)
	{
		DWORD token = *declaration;
		unsigned long token_type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;

		switch (token_type)
		{
		case D3DVSD_TOKEN_STREAM:
			stream = token & D3DVSD_STREAMNUMBERMASK;
			break;
		case D3DVSD_TOKEN_STREAMDATA:
			if (token & D3DVSD_DATALOADTYPEMASK)
			{
				/* skip: the count is in dwords, or in bytes with bit 27 */
				unsigned long count = (token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT;

				offsets[stream] += (token & 0x08000000) ? count : count * 4;
			}
			else if (object->element_count < VERTEX_ATTRIBUTE_COUNT)
			{
				struct vertex_element *element = &object->elements[object->element_count++];

				element->reg = (unsigned char)(token & D3DVSD_VERTEXREGMASK);
				element->stream = (unsigned char)stream;
				element->type = (unsigned char)((token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT);
				element->bytes = (unsigned char)vertex_type_bytes(element->type);
				element->offset = (unsigned short)offsets[stream];
				offsets[stream] += element->bytes;
			}
			break;
		case D3DVSD_TOKEN_CONSTMEM:
			declaration += ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT) * 4;
			break;
		case D3DVSD_TOKEN_EXT:
			declaration += (token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
			break;
		default:
			break;
		}
	}
}

HRESULT WINAPI D3DDevice_CreateVertexShader(CONST DWORD *declaration, CONST DWORD *function, DWORD *handle, DWORD usage)
{
	struct vertex_shader_object *object = calloc(1, sizeof(*object));

	(void)usage;
	if (!object)
		return E_OUTOFMEMORY;
	object->signature = VERTEX_SHADER_SIGNATURE;
	object->id = front.next_vertex_shader_id++;
	if (function)
	{
		/* the header: the program type in the low word, the instruction count in the high */
		object->instruction_count = function[0] >> 16;
		object->instructions = malloc(object->instruction_count * 4 * sizeof(DWORD));
		if (object->instructions)
			memcpy(object->instructions, function + 1, object->instruction_count * 4 * sizeof(DWORD));
	}
	parse_declaration(object, declaration);
	/* heap pointers are below 4 GB on lv2; odd values are FVF codes, shader handles even */
	*handle = (DWORD)(__uint64)object;
	return S_OK;
}

static struct vertex_shader_object *vertex_shader_from_handle(DWORD handle)
{
	struct vertex_shader_object *object = (struct vertex_shader_object *)(__uint64)handle;

	if (!handle || (handle & 1) || object->signature != VERTEX_SHADER_SIGNATURE)
		return NULL;
	return object;
}

void WINAPI D3DDevice_DeleteVertexShader(DWORD handle)
{
	/* programs stay cached; the object is small */
	(void)handle;
}

enum
{
	_shader_set,
	_shader_load,
	_shader_select
};

/* (the Direct3D thread) a shader record */
static void vertex_shader_apply(uint32_t kind, struct vertex_shader_object *object, uint32_t address)
{
	switch (kind)
	{
	case _shader_set:
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
		break;
	case _shader_load:
		device.program_slots[address] = object;
		break;
	default:
		if (object)
			device.vertex_shader = object;
		if (address < VERTEX_PROGRAM_SLOTS)
			device.program_address = address;
		break;
	}
}

static void vertex_shader_record(uint32_t kind, struct vertex_shader_object *object, uint32_t address)
{
	uint64_t *payload = record_begin(_record_vertex_shader, sizeof(uint64_t), kind, address);

	*payload = (uint64_t)object;
	record_commit();
}

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
	{
		front.vertex_shader = object;
		vertex_shader_record(_shader_set, object, 0);
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		vertex_shader_record(_shader_load, vertex_shader_from_handle(handle), address);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
		front.vertex_shader = object;
	vertex_shader_record(_shader_select, object, address);
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	*size = object ? object->instruction_count : 0;
}

/* the timer build: the game thread's time in a call, without its waits for the ring's room (_ps3_timer_thread_wait) */
static uint64_t front_call_start, front_call_waits;

static void front_call_begin(void)
{
	front_call_waits = ps3_timers[_ps3_timer_thread_wait];
	front_call_start = ps3_timer_now();
}

static void front_call_end(int timer)
{
	ps3_timers[timer] += ps3_timer_now() - front_call_start - (ps3_timers[_ps3_timer_thread_wait] - front_call_waits);
}

static void constants_record(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + VERTEX_CONSTANT_BIAS;
	void *payload;

	if (first < 0 || first >= VERTEX_CONSTANT_COUNT || !constant_count)
		return;
	if (first + (long)constant_count > VERTEX_CONSTANT_COUNT)
		constant_count = VERTEX_CONSTANT_COUNT - first;
	payload = record_begin(_record_constants, constant_count * sizeof(device.constants[0]), (uint32_t)first,
		(uint32_t)constant_count);
	memcpy(payload, constant_data, constant_count * sizeof(device.constants[0]));
	record_commit();
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	front_call_begin();
	constants_record(reg, constant_data, constant_count);
	front_call_end(_ps3_timer_front_constants);
}

/* the program that runs: the one loaded at the selected address, else the current shader's own */
static struct vertex_shader_object *current_program(void)
{
	struct vertex_shader_object *program = device.program_slots[device.program_address];

	return program ? program : device.vertex_shader;
}

static const struct rsx_vertex_program *vertex_program_get(struct vertex_shader_object *object)
{
	if (!object->translated && !object->untranslatable)
	{
		uint32_t index;

		for (index = 0; index < rsx_packed_vertex_count && object->instructions; index++)
		{
			const struct rsx_packed_vertex *packed = &rsx_packed_vertices[index];

			if (packed->nv2a_count == object->instruction_count &&
				!memcmp(packed->nv2a, object->instructions, packed->nv2a_count * 4 * sizeof(uint32_t)))
			{
				object->program = packed->program;
				object->translated = TRUE;
				return &object->program;
			}
		}
		object->translated = object->instructions &&
			rsx_vertex_program_translate(object->instructions, object->instruction_count, &object->program) == 0;
		object->untranslatable = !object->translated;
		if (object->untranslatable)
			platform_log("Direct3D: cannot translate vertex shader %lu (%lu instructions)", object->id,
				object->instruction_count);
		else
			platform_log("Direct3D: vertex shader %lu not in the pack, translated: %lu NV2A instructions, %lu RSX, "
				"inputs %04lx, outputs %05lx", object->id, object->instruction_count,
				(unsigned long)object->program.instruction_count, (unsigned long)object->program.input_mask,
				(unsigned long)object->program.output_mask);
	}
	return object->translated ? &object->program : NULL;
}

/* ---------- draws */

/* the state, programs and textures of a draw; the vertex program's on success */
static const struct rsx_vertex_program *prepare_draw_state(void)
{
	uint64_t span = ps3_timer_now(), colours;
	struct vertex_shader_object *object = current_program();
	const struct rsx_vertex_program *vertex_program;
	const struct rsx_fragment_program *fragment_program;
	struct texture_samplers samplers;
	float uniforms[_rsx_uniform_count][4];
	BOOL has_depth = FALSE;

	if (!device.gpu_ready)
		return NULL;
	if (!object || !device.vertex_shader || !(vertex_program = vertex_program_get(object)))
	{
		statistics.skipped_program++;
		return NULL;
	}
	span = ps3_timer_add(_ps3_timer_draw_program, span);
	if (!bind_targets(&has_depth))
	{
		statistics.skipped_target++;
		return NULL;
	}
	span = ps3_timer_add(_ps3_timer_draw_targets, span);
	if (!apply_raster_state(has_depth))
	{
		statistics.skipped_scissor++;
		return NULL;
	}
	span = ps3_timer_add(_ps3_timer_draw_raster, span);
	screen_constants_update();
	span = ps3_timer_add(_ps3_timer_draw_screen, span);

	bind_textures(&samplers);
	span = ps3_timer_add(_ps3_timer_draw_textures, span);
	fragment_program = fragment_program_find(&samplers, &span);
	if (!fragment_program)
	{
		statistics.skipped_program++;
		return NULL;
	}
	colours = span = ps3_timer_add(_ps3_timer_draw_fragment, span);
	fragment_uniforms_build(uniforms);
	span = ps3_timer_add(_ps3_timer_draw_uniforms, span);
	if (ps3_gpu_set_fragment_program(fragment_program, (const float (*)[4])uniforms) != 0)
	{
		statistics.skipped_scratch++;
		return NULL;
	}
	span = ps3_timer_add(_ps3_timer_draw_colours, colours);
	ps3_gpu_set_vertex_program(vertex_program);
	span = ps3_timer_add(_ps3_timer_draw_vertex_program, span);
	constants_upload();
	ps3_timer_add(_ps3_timer_draw_constants, span);
	return vertex_program;
}

static const struct rsx_vertex_program *prepare_draw(void)
{
	uint64_t start = ps3_timer_now();
	const struct rsx_vertex_program *program = prepare_draw_state();

	ps3_timer_add(_ps3_timer_draw_state, start);
	return program;
}

/* reads a float the vertex data may hold at any address (trap 23) */
static float vertex_float(const unsigned char *source)
{
	union { DWORD d; float f; } value;

	if (!((__uint64)source & 3))
		return *(const float *)source;
	value.d = ((DWORD)source[0] << 24) | ((DWORD)source[1] << 16) | ((DWORD)source[2] << 8) | source[3];
	return value.f;
}

static short vertex_short(const unsigned char *source)
{
	return (short)(((unsigned short)source[0] << 8) | source[1]);
}

/* one element as four floats, as the NV2A reads it; vertex data is big-endian (the map swap) */
static void vertex_element_convert(unsigned char type, const unsigned char *source, float *out)
{
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

	switch (type)
	{
	case D3DVSDT_FLOAT4:
		w = vertex_float(source + 12);
		/* fall through */
	case D3DVSDT_FLOAT3:
	case D3DVSDT_FLOAT2H:
		z = vertex_float(source + 8);
		/* fall through */
	case D3DVSDT_FLOAT2:
		y = vertex_float(source + 4);
		/* fall through */
	case D3DVSDT_FLOAT1:
		x = vertex_float(source);
		break;
	case D3DVSDT_D3DCOLOR:
		/* the word ARGB: its bytes A R G B */
		x = byte_floats[source[1]];
		y = byte_floats[source[2]];
		z = byte_floats[source[3]];
		w = byte_floats[source[0]];
		break;
	case D3DVSDT_SHORT4:
		w = (float)vertex_short(source + 6);
		/* fall through */
	case D3DVSDT_SHORT3:
		z = (float)vertex_short(source + 4);
		/* fall through */
	case D3DVSDT_SHORT2:
		y = (float)vertex_short(source + 2);
		/* fall through */
	case D3DVSDT_SHORT1:
		x = (float)vertex_short(source);
		break;
	case D3DVSDT_NORMSHORT4:
		w = vertex_short(source + 6) / 32767.0f;
		/* fall through */
	case D3DVSDT_NORMSHORT3:
		z = vertex_short(source + 4) / 32767.0f;
		/* fall through */
	case D3DVSDT_NORMSHORT2:
		y = vertex_short(source + 2) / 32767.0f;
		/* fall through */
	case D3DVSDT_NORMSHORT1:
		x = vertex_short(source) / 32767.0f;
		if (x < -1.0f) x = -1.0f;
		if (y < -1.0f) y = -1.0f;
		if (z < -1.0f) z = -1.0f;
		if (w < -1.0f) w = -1.0f;
		break;
	case D3DVSDT_NORMPACKED3:
	{
		DWORD packed = ((DWORD)source[0] << 24) | ((DWORD)source[1] << 16) | ((DWORD)source[2] << 8) | source[3];

		/* 11, 11 and 10 signed bits, as nv2a_vsh.c's unpack_normpacked3 */
		x = (float)((int)(packed << 21) >> 21) / 1023.0f;
		y = (float)((int)(packed << 10) >> 21) / 1023.0f;
		z = (float)((int)packed >> 22) / 511.0f;
		break;
	}
	case D3DVSDT_PBYTE4:
		w = byte_floats[source[3]];
		/* fall through */
	case D3DVSDT_PBYTE3:
		z = byte_floats[source[2]];
		/* fall through */
	case D3DVSDT_PBYTE2:
		y = byte_floats[source[1]];
		/* fall through */
	case D3DVSDT_PBYTE1:
		x = byte_floats[source[0]];
		break;
	case SPU_VERTICES_TEXCOORD_WORD:
		/* the word's low half, then its high (big-endian: bytes 2 and 3, then 0 and 1) */
		x = vertex_short(source + 2) / 32767.0f;
		y = vertex_short(source) / 32767.0f;
		break;
	default:
		break;
	}
	out[0] = x;
	out[1] = y;
	out[2] = z;
	out[3] = w;
}

/* ---------- the conversions on an SPU (spu_vertices.c), which converts as vertex_element_convert does */

typedef char spu_vertices_types_check[D3DVSDT_FLOAT1 == SPU_VERTICES_FLOAT1 && D3DVSDT_FLOAT2 == SPU_VERTICES_FLOAT2 &&
	D3DVSDT_FLOAT3 == SPU_VERTICES_FLOAT3 && D3DVSDT_FLOAT4 == SPU_VERTICES_FLOAT4 &&
	D3DVSDT_FLOAT2H == SPU_VERTICES_FLOAT2H && D3DVSDT_D3DCOLOR == SPU_VERTICES_D3DCOLOR &&
	D3DVSDT_NORMSHORT1 == SPU_VERTICES_NORMSHORT1 && D3DVSDT_NORMSHORT2 == SPU_VERTICES_NORMSHORT2 &&
	D3DVSDT_NORMSHORT3 == SPU_VERTICES_NORMSHORT3 && D3DVSDT_NORMSHORT4 == SPU_VERTICES_NORMSHORT4 &&
	D3DVSDT_NORMPACKED3 == SPU_VERTICES_NORMPACKED3 && D3DVSDT_SHORT1 == SPU_VERTICES_SHORT1 &&
	D3DVSDT_SHORT2 == SPU_VERTICES_SHORT2 && D3DVSDT_SHORT3 == SPU_VERTICES_SHORT3 &&
	D3DVSDT_SHORT4 == SPU_VERTICES_SHORT4 && D3DVSDT_PBYTE1 == SPU_VERTICES_PBYTE1 &&
	D3DVSDT_PBYTE2 == SPU_VERTICES_PBYTE2 && D3DVSDT_PBYTE3 == SPU_VERTICES_PBYTE3 &&
	D3DVSDT_PBYTE4 == SPU_VERTICES_PBYTE4 ? 1 : -1];

/* an element read from vertex data: vertex v's at base + v * stride */
struct vertex_input
{
	const unsigned char *base;
	unsigned long stride;
	unsigned char type;
	unsigned char bytes;
	unsigned char reg;
};

static BOOL spu_address(const void *address)
{
	return ((__uint64)(uintptr_t)address >> 32) == 0;
}

/* count vertices of the inputs as float4s into out, a vertex's together: an SPU task, or here. The RSX reads the task's
output only after it is done (ps3_gpu.c waits before each flush) */
static void vertices_convert(const struct vertex_input *inputs, unsigned long slot_count, unsigned long count,
	float *out)
{
	spu_vertices_task *task = NULL;
	unsigned long vertex, slot;
	BOOL addresses = spu_address(out);

	for (slot = 0; slot < slot_count; slot++)
	{
		addresses = addresses && spu_address(inputs[slot].base);
		statistics.type_elements[inputs[slot].type & 127] += count;
	}
	if (count && addresses && slot_count <= SPU_VERTICES_SLOTS)
		task = ps3_spu_vertices_task();
	if (task)
	{
		task->kind = SPU_VERTICES_CONVERT;
		task->slot_count = (uint16_t)slot_count;
		task->count = (uint32_t)count;
		task->output = (uint32_t)(uintptr_t)out;
		task->subtract = 0;
		for (slot = 0; slot < slot_count; slot++)
		{
			task->slots[slot].source = (uint32_t)(uintptr_t)inputs[slot].base;
			task->slots[slot].stride = (uint32_t)inputs[slot].stride;
			task->slots[slot].type = inputs[slot].type;
			task->slots[slot].bytes = inputs[slot].bytes;
		}
		ps3_spu_vertices_commit();
		return;
	}
	for (vertex = 0; vertex < count; vertex++)
	{
		for (slot = 0; slot < slot_count; slot++, out += 4)
			vertex_element_convert(inputs[slot].type, inputs[slot].base + vertex * inputs[slot].stride, out);
	}
}

/* count indices less subtract into out, whose room is rounded up to 16 bytes: an SPU task, or here */
static void indices_convert(const WORD *source, unsigned long count, unsigned long subtract, unsigned short *out)
{
	spu_vertices_task *task = NULL;
	unsigned long index;

	if (count && spu_address(source) && spu_address(out))
		task = ps3_spu_vertices_task();
	if (task)
	{
		task->kind = SPU_VERTICES_INDICES;
		task->slot_count = 0;
		task->count = (uint32_t)count;
		task->output = (uint32_t)(uintptr_t)out;
		task->subtract = (uint32_t)subtract;
		task->slots[0].source = (uint32_t)(uintptr_t)source;
		ps3_spu_vertices_commit();
		return;
	}
	for (index = 0; index < count; index++)
		out[index] = (unsigned short)(source[index] - subtract);
}

/* ps3_spu.c's fallback: a task the SPU left, run here */
static void vertices_task_run(const spu_vertices_task *task)
{
	unsigned long index, slot;

	if (task->kind == SPU_VERTICES_INDICES)
	{
		const WORD *source = (const WORD *)(uintptr_t)task->slots[0].source;
		unsigned short *out = (unsigned short *)(uintptr_t)task->output;

		for (index = 0; index < task->count; index++)
			out[index] = (unsigned short)(source[index] - task->subtract);
	}
	else if (task->kind == SPU_VERTICES_CONVERT)
	{
		float *out = (float *)(uintptr_t)task->output;

		for (index = 0; index < task->count; index++)
		{
			for (slot = 0; slot < task->slot_count; slot++, out += 4)
				vertex_element_convert((unsigned char)task->slots[slot].type,
					(const unsigned char *)(uintptr_t)task->slots[slot].source + index * task->slots[slot].stride, out);
		}
	}
	else if (task->kind == SPU_VERTICES_FRAGMENT)
	{
		ps3_gpu_fragment_task_run(task);
	}
	else if (task->kind == SPU_VERTICES_TEXTURE)
	{
		xgpu_texture_rsx_task_run((const spu_vertices_texture *)task);
	}
	else if (task->kind == SPU_VERTICES_HASH)
	{
		const spu_vertices_texture *hash = (const spu_vertices_texture *)task;
		uint32_t *out = (uint32_t *)(uintptr_t)hash->output;
		uint64_t result = xgpu_texture_hash((const unsigned char *)(uintptr_t)hash->source, hash->units);

		out[0] = (uint32_t)(result >> 32);
		out[1] = (uint32_t)result;
	}
}

/* a constant for an input the declaration leaves out: one value read by every vertex */
static BOOL attribute_constant(unsigned long reg, const float *value)
{
	uint32_t offset;
	float *memory = ps3_gpu_scratch(16, 16, &offset);

	if (!memory)
		return FALSE;
	memory[0] = value[0];
	memory[1] = value[1];
	memory[2] = value[2];
	memory[3] = value[3];
	ps3_gpu_set_attribute((uint32_t)reg, offset, 0, 4);
	return TRUE;
}

/* ---------- the vertex cache: the map's vertex buffers (models, the structure BSP) live in the tag cache,
which changes only at a map or BSP load (cache_files.c), so their elements are converted once */

struct vertex_cache_entry
{
	struct vertex_cache_entry *next;
	/* the element in vertex 0 of its stream */
	const unsigned char *source;
	unsigned long stride;
	unsigned char type;
	/* vertices [0, count) converted, as float4s at offset; room for capacity */
	unsigned long count;
	unsigned long capacity;
	float *memory;
	uint32_t offset;
};

#define VERTEX_CACHE_BUCKET_COUNT 4096
/* one RSX block for the copies, emptied at each load; the largest load (a10g's BSP and all of a10's models,
every element the shaders read) is 24.3 MB */
#define VERTEX_ARENA_BYTES (48UL * 1024 * 1024)

/* the models' index buffers, in the tag cache as their vertices: each copied once into the arena, its largest index
checked against its buffer's vertex count */
struct index_cache_entry
{
	struct index_cache_entry *next;
	const WORD *source;
	unsigned long count;
	unsigned long vertices;
	uint32_t offset;
};

#define INDEX_CACHE_BUCKET_COUNT 2048

static struct vertex_cache_entry *vertex_cache_buckets[VERTEX_CACHE_BUCKET_COUNT];
static struct index_cache_entry *index_cache_buckets[INDEX_CACHE_BUCKET_COUNT];
static unsigned long vertex_cache_arrays;
/* the tag cache, as its loads give it (d3d8_tag_cache_loaded) */
static const unsigned char *static_vertices_begin;
static const unsigned char *static_vertices_end;

static struct
{
	unsigned char *memory;
	uint32_t offset;
	unsigned long used;
	BOOL failed;
} vertex_arena;

static unsigned long vertex_cache_bucket_index(const unsigned char *source, unsigned long stride, unsigned char type)
{
	__uint64 address = (__uint64)source;

	return (unsigned long)(((address >> 4) ^ (address >> 16) ^ (stride * 2654435761UL) ^ type) %
		VERTEX_CACHE_BUCKET_COUNT);
}

/* the arena's RSX block, once */
static void vertex_arena_reserve(void)
{
	if (!vertex_arena.memory && !vertex_arena.failed)
	{
		vertex_arena.memory = ps3_gpu_memory_allocate((uint32_t)VERTEX_ARENA_BYTES, 128, _ps3_gpu_memory_vertices,
			&vertex_arena.offset);
		vertex_arena.failed = !vertex_arena.memory;
		platform_log("Direct3D: vertex cache of %lu MB%s", (unsigned long)(VERTEX_ARENA_BYTES >> 20),
			vertex_arena.failed ? ": no RSX memory, vertices are converted at each draw" : "");
	}
}

/* bytes of the arena, in order; it has no frees: a load empties it */
static float *vertex_arena_allocate(unsigned long bytes, uint32_t *offset)
{
	vertex_arena_reserve();
	bytes = (bytes + 127) & ~127UL;
	if (!vertex_arena.memory || bytes > VERTEX_ARENA_BYTES - vertex_arena.used)
		return NULL;
	*offset = vertex_arena.offset + (uint32_t)vertex_arena.used;
	vertex_arena.used += bytes;
	return (float *)(vertex_arena.memory + vertex_arena.used - bytes);
}

/* cache_files.c, before a load writes the tag cache: the draws that read it are done first */
void d3d8_tag_cache_loading(void)
{
	direct3d_drain();
}

/* cache_files.c: a map's tag data or a BSP was loaded into [address, address + size) of the tag cache;
every copy goes, as they share one block */
void d3d8_tag_cache_loaded(void const *address, unsigned long size)
{
	const unsigned char *begin = address, *end = begin + size;
	unsigned long index;

	/* the caches are the Direct3D thread's: it waits idle meanwhile */
	direct3d_drain();
	if (spu.on)
	{
		spu_d3d_record_tag_cache *span;

		/* (the SPU empties its caches and waits for the RSX before the arena is used again) */
		if (!static_vertices_end || begin < static_vertices_begin)
			static_vertices_begin = begin;
		if (end > static_vertices_end)
			static_vertices_end = end;
		span = record_begin(_record_tag_cache, sizeof(*span), 0, 0);
		if (span)
		{
			span->begin = (uint32_t)(uintptr_t)begin;
			span->end = (uint32_t)(uintptr_t)end;
			span->pad[0] = span->pad[1] = 0;
			record_commit();
		}
		return;
	}
	for (index = 0; index < VERTEX_CACHE_BUCKET_COUNT; index++)
	{
		while (vertex_cache_buckets[index])
		{
			struct vertex_cache_entry *entry = vertex_cache_buckets[index];

			vertex_cache_buckets[index] = entry->next;
			free(entry);
		}
	}
	for (index = 0; index < INDEX_CACHE_BUCKET_COUNT; index++)
	{
		while (index_cache_buckets[index])
		{
			struct index_cache_entry *entry = index_cache_buckets[index];

			index_cache_buckets[index] = entry->next;
			free(entry);
		}
	}
	vertex_cache_arrays = 0;
	if (vertex_arena.used)
	{
		/* the frames in flight can still read the copies */
		ps3_gpu_wait_idle();
		vertex_arena.used = 0;
	}
	if (!static_vertices_end || begin < static_vertices_begin)
		static_vertices_begin = begin;
	if (end > static_vertices_end)
		static_vertices_end = end;
}

/* rasterizer_xbox_draw_primitives.c, before each indexed draw of a map vertex buffer (BSP material, model
part): its vertex count, so the copies of its elements get their full size at once */
void d3d8_draw_vertex_count(unsigned long vertex_count)
{
	front.draw_vertex_count = vertex_count;
}

/* the RSX copy of one element of a stream with vertices [0, first + count) converted, room for the buffer's
[0, extent) if known (else 0); NULL outside the tag cache, or with no room left (the draw converts it then) */
static const struct vertex_cache_entry *vertex_cache_get(const unsigned char *source, unsigned long stride,
	unsigned char type, unsigned long bytes, unsigned long first, unsigned long count, unsigned long extent)
{
	struct vertex_cache_entry **bucket, *entry;
	unsigned long needed = first + count;
	struct vertex_input input;

	if (!static_vertices_end || source < static_vertices_begin ||
		source + (needed - 1) * stride + bytes > static_vertices_end)
		return NULL;
	bucket = &vertex_cache_buckets[vertex_cache_bucket_index(source, stride, type)];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->source == source && entry->stride == stride && entry->type == type)
			break;
	}
	if (entry && entry->count >= needed)
		return entry;
	if (!entry || needed > entry->capacity)
	{
		/* the buffer's size when the draw gives it, so the copy never grows; without it a BSP buffer's range
		grows with what is visible: twice the room (the old copy stays to the next load), as far as the tag cache */
		unsigned long capacity = extent >= needed ? extent :
			entry && entry->capacity * 2 > needed ? entry->capacity * 2 : needed;
		unsigned long limit = stride ? (unsigned long)((static_vertices_end - bytes - source) / stride) + 1 : needed;
		struct vertex_cache_entry *created = NULL;
		uint32_t offset;
		float *memory;

		if (capacity > limit)
			capacity = limit;
		if (!entry && !(created = calloc(1, sizeof(*created))))
			return NULL;
		memory = vertex_arena_allocate(capacity * 16, &offset);
		if (!memory)
		{
			free(created);
			statistics.cache_refused++;
			return NULL;
		}
		if (created)
		{
			entry = created;
			entry->source = source;
			entry->stride = stride;
			entry->type = type;
			entry->next = *bucket;
			*bucket = entry;
			vertex_cache_arrays++;
		}
		entry->memory = memory;
		entry->offset = offset;
		entry->capacity = capacity;
		entry->count = 0;
	}
	/* only vertices no draw has read yet */
	input.base = source + entry->count * stride;
	input.stride = stride;
	input.type = type;
	input.bytes = (unsigned char)bytes;
	input.reg = 0;
	vertices_convert(&input, 1, needed - entry->count, entry->memory + entry->count * 4);
	statistics.cached_elements += needed - entry->count;
	entry->count = needed;
	ps3_gpu_vertices_written();
	return entry;
}

/* vertices [first, first + count) of the declaration's streams as float4s for the program's inputs: the
map's from the vertex cache, the rest converted into the scratch, interleaved, in one pass */
static BOOL setup_vertices(const struct rsx_vertex_program *program, unsigned long first, unsigned long count,
	unsigned long extent)
{
	const struct vertex_shader_object *declaration = device.vertex_shader;
	struct vertex_input inputs[VERTEX_ATTRIBUTE_COUNT];
	unsigned long slot_count = 0, index, slot, stride;
	DWORD placed = 0;
	uint32_t offset = 0;
	float *out = NULL;

	for (index = 0; index < declaration->element_count && slot_count < 15; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		const unsigned char *base = device.streams[element->stream].base;
		const struct vertex_cache_entry *cached;
		const unsigned char *source;

		if (element->reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << element->reg)) ||
			(placed & (1u << element->reg)) || !base || element->type == D3DVSDT_NONE)
			continue;
		placed |= 1u << element->reg;
		source = base + element->offset;
		cached = vertex_cache_get(source, device.streams[element->stream].stride, element->type, element->bytes,
			first, count, extent);
		if (cached)
		{
			ps3_gpu_set_attribute(element->reg, cached->offset + (uint32_t)(first * 16), 16, 4);
			continue;
		}
		inputs[slot_count].stride = device.streams[element->stream].stride;
		inputs[slot_count].base = source + first * inputs[slot_count].stride;
		inputs[slot_count].type = element->type;
		inputs[slot_count].bytes = element->bytes;
		inputs[slot_count].reg = element->reg;
		slot_count++;
	}
	stride = slot_count * 16;
	if (slot_count)
	{
		out = ps3_gpu_scratch((uint32_t)(count * stride), 16, &offset);
		if (!out)
			return FALSE;
		vertices_convert(inputs, slot_count, count, out);
		statistics.converted_elements += count * slot_count;
	}
	for (slot = 0; slot < slot_count; slot++)
		ps3_gpu_set_attribute(inputs[slot].reg, offset + (uint32_t)(slot * 16), (uint32_t)stride, 4);
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (placed & (1u << index))
			continue;
		if (!(program->input_mask & (1u << index)))
			ps3_gpu_set_attribute((uint32_t)index, 0, 0, 0);
		else if (!attribute_constant(index, device.attributes[index]))
			return FALSE;
	}
	return TRUE;
}

/* a map buffer's vertices [0, extent) whole from the vertex cache, each element read from the base vertex: TRUE when
the cache holds every element the program reads, so the indices need no rebasing */
static BOOL setup_cached_vertices(const struct rsx_vertex_program *program, unsigned long base, unsigned long extent)
{
	const struct vertex_shader_object *declaration = device.vertex_shader;
	const struct vertex_cache_entry *cached[VERTEX_ATTRIBUTE_COUNT];
	DWORD placed = 0;
	unsigned long index;

	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		const unsigned char *base = device.streams[element->stream].base;

		if (element->reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << element->reg)) ||
			(placed & (1u << element->reg)) || !base || element->type == D3DVSDT_NONE)
			continue;
		cached[element->reg] = vertex_cache_get(base + element->offset, device.streams[element->stream].stride,
			element->type, element->bytes, 0, extent, extent);
		if (!cached[element->reg])
			return FALSE;
		placed |= 1u << element->reg;
	}
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (placed & (1u << index))
			ps3_gpu_set_attribute((uint32_t)index, cached[index]->offset + (uint32_t)(base * 16), 16, 4);
		else if (!(program->input_mask & (1u << index)))
			ps3_gpu_set_attribute((uint32_t)index, 0, 0, 0);
		else if (!attribute_constant(index, device.attributes[index]))
			return FALSE;
	}
	return TRUE;
}

static unsigned short largest_index(const WORD *source, unsigned long count)
{
	unsigned short largest = 0;
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		if (source[index] > largest)
			largest = source[index];
	}
	return largest;
}

/* a model's indices in the tag cache: the arena's copy, made at their first draw if each reads one of the buffer's
vertices (fewer than vertices) */
static BOOL index_cache_offset(const WORD *source, unsigned long count, unsigned long vertices, uint32_t *offset)
{
	const unsigned char *bytes = (const unsigned char *)source;
	struct index_cache_entry **bucket, *entry;
	WORD *copy;

	if (!static_vertices_end || bytes < static_vertices_begin || bytes + count * sizeof(WORD) > static_vertices_end)
		return FALSE;
	bucket = &index_cache_buckets[(unsigned long)((((__uint64)source >> 1) ^ ((__uint64)source >> 13) ^ count) %
		INDEX_CACHE_BUCKET_COUNT)];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->source == source && entry->count == count && entry->vertices == vertices)
		{
			*offset = entry->offset;
			return TRUE;
		}
	}
	if (largest_index(source, count) >= vertices || !(entry = calloc(1, sizeof(*entry))))
		return FALSE;
	copy = (WORD *)vertex_arena_allocate(count * sizeof(WORD), &entry->offset);
	if (!copy)
	{
		free(entry);
		return FALSE;
	}
	indices_convert(source, count, 0, copy);
	entry->source = source;
	entry->count = count;
	entry->vertices = vertices;
	entry->next = *bucket;
	*bucket = entry;
	*offset = entry->offset;
	return TRUE;
}

/* the indices into the scratch as they are; FALSE if one reads past the buffer's vertices */
static BOOL index_copy(const WORD *source, unsigned long count, unsigned long vertices, uint32_t *offset)
{
	unsigned short *indices = ps3_gpu_scratch((uint32_t)((count * sizeof(unsigned short) + 15) & ~15UL), 128, offset);
	unsigned long index;
	unsigned int bits = 0;

	if (!indices)
		return FALSE;
	/* (their OR is at least the largest, without a branch an index; past the buffer: a closer look) */
	for (index = 0; index < count; index++)
		bits |= source[index];
	if (bits >= vertices && largest_index(source, count) >= vertices)
		return FALSE;
	indices_convert(source, count, 0, indices);
	return TRUE;
}

static uint32_t primitive_type(D3DPRIMITIVETYPE type)
{
	/* D3DPT's values are GCM's; ps3gl draws polygons as fans */
	return type == D3DPT_POLYGON ? GCM_TYPE_TRIANGLE_FAN : (uint32_t)type;
}

static unsigned long bit_count(DWORD value)
{
	unsigned long count = 0;

	for (; value; value &= value - 1)
		count++;
	return count;
}

/* a draw record; stream_mask's streams follow in order, then the indices it copied, then the vertices */
struct record_draw
{
	uint32_t primitive;
	/* vertices, or indices */
	uint32_t count;
	/* the first vertex, or the indices' base vertex */
	uint32_t start;
	/* indices: the buffer's vertices from vertex 0 (d3d8_draw_vertex_count), or 0 */
	uint32_t extent;
	/* indices: their range, when the game's thread read it to copy vertices */
	uint32_t ranged;
	uint32_t minimum;
	uint32_t maximum;
	uint32_t stream_mask;
	/* indices: in the tag cache, or the record's copy */
	uint64_t indices;
	/* this draw's number (texture Lock words, ring.completed_draws) */
	uint32_t draw;
	uint32_t pad;
};

/* vertex v of the stream at base + v * stride */
struct record_stream
{
	uint64_t base;
	uint32_t stride;
	uint32_t pad;
};

/* each vertex follows: the written registers' 4 floats, in register order */
struct record_immediate
{
	uint32_t primitive;
	uint32_t count;
	uint32_t written;
	uint32_t draw;
};

/* the rectangles follow (a of the record) */
struct record_clear
{
	uint32_t color;
	uint32_t z;
	uint32_t stencil;
	uint32_t pad;
};

#define RECORD_ELEMENTS 4

/* vertices the game's thread wrote into the record, after this: each element's from there plus its offset, a vertex
every stride bytes (d3d8_draw_decals_begin) */
struct record_elements
{
	uint32_t primitive;
	uint32_t count;
	uint32_t draw;
	uint32_t element_count;
	struct
	{
		uint32_t reg;
		uint32_t type;
		uint32_t offset;
		uint32_t stride;
	} elements[RECORD_ELEMENTS];
};

typedef char record_size_check[sizeof(struct record_draw) % 16 == 0 && sizeof(struct record_stream) == 16 &&
	sizeof(struct record_immediate) == 16 && sizeof(struct record_clear) == 16 &&
	sizeof(struct record_elements) % 16 == 0 ? 1 : -1];

/* ---------- draws, as the Direct3D thread runs their records */

static void streams_load(const struct record_draw *draw)
{
	const struct record_stream *stream = (const struct record_stream *)(draw + 1);
	unsigned long index;

	for (index = 0; index < 16; index++)
	{
		if (draw->stream_mask & (1u << index))
		{
			device.streams[index].base = (const unsigned char *)stream->base;
			device.streams[index].stride = stream->stride;
			stream++;
		}
	}
}

static void draw_vertices_run(const struct record_draw *draw)
{
	const struct rsx_vertex_program *program;
	uint64_t span;
	BOOL ready;

	streams_load(draw);
	if (!draw->count || !(program = prepare_draw()))
		return;
	span = ps3_timer_now();
	ready = setup_vertices(program, draw->start, draw->count, 0);
	ps3_timer_add(_ps3_timer_draw_vertices, span);
	if (!ready)
	{
		statistics.skipped_scratch++;
		return;
	}
	ps3_gpu_draw(primitive_type((D3DPRIMITIVETYPE)draw->primitive), 0, draw->count);
	statistics.draws++;
	draw_stamp();
}

static void draw_indexed_run(const struct record_draw *draw)
{
	const struct rsx_vertex_program *program;
	const WORD *index_data = (const WORD *)draw->indices;
	unsigned long vertex_count = draw->count, extent = draw->extent, base_vertex_index = draw->start;
	unsigned long index, minimum = 0xffff, maximum = 0;
	uint32_t offset;
	unsigned short *indices;
	uint64_t span;
	BOOL ready;

	streams_load(draw);
	if (!vertex_count || !index_data || !(program = prepare_draw()))
		return;
	span = ps3_timer_now();
	/* a map buffer the vertex cache holds whole: its indices as they are, a model's from the index cache */
	if (extent && setup_cached_vertices(program, base_vertex_index, extent))
	{
		unsigned long vertices = extent - base_vertex_index;

		span = ps3_timer_add(_ps3_timer_draw_vertices, span);
		ready = index_cache_offset(index_data, vertex_count, vertices, &offset) ||
			index_copy(index_data, vertex_count, vertices, &offset);
		ps3_timer_add(_ps3_timer_draw_indices, span);
		if (ready)
		{
			ps3_gpu_draw_indexed(primitive_type((D3DPRIMITIVETYPE)draw->primitive), offset, vertex_count);
			statistics.draws++;
			draw_stamp();
			return;
		}
		span = ps3_timer_now();
	}
	if (draw->ranged)
	{
		minimum = draw->minimum;
		maximum = draw->maximum;
	}
	else
	{
		for (index = 0; index < vertex_count; index++)
		{
			if (index_data[index] < minimum)
				minimum = index_data[index];
			if (index_data[index] > maximum)
				maximum = index_data[index];
		}
	}
	span = ps3_timer_add(_ps3_timer_draw_indices, span);
	/* index i is vertex base + i; the converted vertices start at the smallest index */
	ready = setup_vertices(program, base_vertex_index + minimum, maximum - minimum + 1, extent);
	span = ps3_timer_add(_ps3_timer_draw_vertices, span);
	if (!ready || !(indices = ps3_gpu_scratch((vertex_count * sizeof(unsigned short) + 15) & ~15UL, 128, &offset)))
	{
		statistics.skipped_scratch++;
		return;
	}
	indices_convert(index_data, vertex_count, minimum, indices);
	ps3_timer_add(_ps3_timer_draw_indices, span);
	ps3_gpu_draw_indexed(primitive_type((D3DPRIMITIVETYPE)draw->primitive), offset, vertex_count);
	statistics.draws++;
	draw_stamp();
}

static void immediate_run(const struct record_immediate *immediate)
{
	const struct rsx_vertex_program *program;
	const float *data = (const float *)(immediate + 1);
	unsigned long count = immediate->count, regs[VERTEX_ATTRIBUTE_COUNT], slots[VERTEX_ATTRIBUTE_COUNT];
	unsigned long reg_count = 0, written_count = 0, index, vertex;
	uint32_t offset = 0;
	float *out;

	if (!count || !(program = prepare_draw()))
		return;
	/* a written register's place among a vertex's; the others keep their value for every vertex */
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
		slots[index] = immediate->written & (1u << index) ? written_count++ : VERTEX_ATTRIBUTE_COUNT;
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT && reg_count < 15; index++)
	{
		if (program->input_mask & (1u << index))
			regs[reg_count++] = index;
	}
	out = reg_count ? ps3_gpu_scratch((uint32_t)(count * reg_count * 16), 16, &offset) : NULL;
	if (reg_count && !out)
	{
		statistics.skipped_scratch++;
		return;
	}
	for (vertex = 0; vertex < count; vertex++)
	{
		const float *source = data + vertex * written_count * 4;

		for (index = 0; index < reg_count; index++, out += 4)
		{
			unsigned long slot = slots[regs[index]];

			memcpy(out, slot < VERTEX_ATTRIBUTE_COUNT ? source + slot * 4 : device.attributes[regs[index]],
				4 * sizeof(float));
		}
	}
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
		ps3_gpu_set_attribute((uint32_t)index, 0, 0, 0);
	for (index = 0; index < reg_count; index++)
		ps3_gpu_set_attribute((uint32_t)regs[index], offset + (uint32_t)(index * 16), (uint32_t)(reg_count * 16), 4);
	ps3_gpu_draw(primitive_type((D3DPRIMITIVETYPE)immediate->primitive), 0, (uint32_t)count);
	statistics.draws++;
	draw_stamp();
}

/* the elements the program reads converted into the scratch (an SPU task, as setup_vertices'), the others its
registers' values */
static void elements_run(const struct record_elements *record)
{
	const struct rsx_vertex_program *program;
	const unsigned char *data = (const unsigned char *)(record + 1);
	struct vertex_input inputs[RECORD_ELEMENTS];
	unsigned long count = record->count, slot_count = 0, index, slot, stride;
	DWORD placed = 0;
	uint32_t offset = 0;
	uint64_t span;
	float *out = NULL;

	if (!count || !(program = prepare_draw()))
		return;
	span = ps3_timer_now();
	for (index = 0; index < record->element_count && index < RECORD_ELEMENTS; index++)
	{
		unsigned long reg = record->elements[index].reg;

		if (reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << reg)) || (placed & (1u << reg)))
			continue;
		placed |= 1u << reg;
		inputs[slot_count].base = data + record->elements[index].offset;
		inputs[slot_count].stride = record->elements[index].stride;
		inputs[slot_count].type = (unsigned char)record->elements[index].type;
		inputs[slot_count].bytes = (unsigned char)vertex_type_bytes(record->elements[index].type);
		inputs[slot_count].reg = (unsigned char)reg;
		slot_count++;
	}
	stride = slot_count * 16;
	if (slot_count)
	{
		out = ps3_gpu_scratch((uint32_t)(count * stride), 16, &offset);
		if (!out)
		{
			statistics.skipped_scratch++;
			return;
		}
		vertices_convert(inputs, slot_count, count, out);
		statistics.converted_elements += count * slot_count;
	}
	for (slot = 0; slot < slot_count; slot++)
		ps3_gpu_set_attribute(inputs[slot].reg, offset + (uint32_t)(slot * 16), (uint32_t)stride, 4);
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
	{
		if (placed & (1u << index))
			continue;
		if (!(program->input_mask & (1u << index)))
			ps3_gpu_set_attribute((uint32_t)index, 0, 0, 0);
		else if (!attribute_constant(index, device.attributes[index]))
		{
			statistics.skipped_scratch++;
			return;
		}
	}
	ps3_timer_add(_ps3_timer_draw_vertices, span);
	ps3_gpu_draw(primitive_type((D3DPRIMITIVETYPE)record->primitive), 0, (uint32_t)count);
	statistics.draws++;
	draw_stamp();
}

static void clear_run(const struct record *record)
{
	const struct record_clear *clear = (const struct record_clear *)(record + 1);
	const D3DRECT *rectangles = (const D3DRECT *)(clear + 1);
	DWORD count = record->a, flags = record->b, color = clear->color, stencil = clear->stencil;
	BOOL has_depth = FALSE;
	DWORD gpu_flags, depth;
	long left, top, right, bottom;
	DWORD index;
	float z;

	memcpy(&z, &clear->z, sizeof(z));
	if (!device.gpu_ready || !bind_targets(&has_depth))
		return;
	/* the Xbox clears the channels named: the fog screen clears only alpha */
	gpu_flags = flags & (D3DCLEAR_TARGET_R | D3DCLEAR_TARGET_G | D3DCLEAR_TARGET_B | D3DCLEAR_TARGET_A);
	if (has_depth)
		gpu_flags |= flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
	if (!gpu_flags)
		return;
	if (z < 0.0f)
		z = 0.0f;
	/* 1.0 * 16777215 + 0.5 rounds to 2^24 in a float, which shifted out to a depth of 0 */
	depth = z < 1.0f ? (DWORD)(z * 16777215.0f + 0.5f) : 0xffffffUL;
	if (depth > 0xffffffUL)
		depth = 0xffffffUL;
	depth = (depth << 8) | (stencil & 0xff);
	/* the NV2A clips a clear to the viewport, which keeps split-screen windows apart; a rectangle moves with the
	menus' centring (d3d8_gl.c); in the target's pixels */
	for (index = 0; index < (count ? count : 1); index++)
	{
		long offset = 0;

		left = (long)device.viewport.X;
		top = (long)device.viewport.Y;
		right = left + (long)device.viewport.Width;
		bottom = top + (long)device.viewport.Height;
		if (count)
		{
			if (rectangles[index].x1 > left) left = rectangles[index].x1;
			if (rectangles[index].y1 > top) top = rectangles[index].y1;
			if (rectangles[index].x2 < right) right = rectangles[index].x2;
			if (rectangles[index].y2 < bottom) bottom = rectangles[index].y2;
			offset = ui_offset;
		}
		left = scaled_pixel((float)(left + offset), device.target_scale[0]);
		top = scaled_pixel((float)top, device.target_scale[1]);
		right = scaled_pixel((float)(right + offset), device.target_scale[0]);
		bottom = scaled_pixel((float)bottom, device.target_scale[1]);
		if (left < 0) left = 0;
		if (top < 0) top = 0;
		if (right > (long)device.target_pixel_width) right = (long)device.target_pixel_width;
		if (bottom > (long)device.target_pixel_height) bottom = (long)device.target_pixel_height;
		if (left < right && top < bottom)
			ps3_gpu_clear(gpu_flags, color, depth, (uint32_t)left, (uint32_t)top, (uint32_t)(right - left),
				(uint32_t)(bottom - top));
	}
}

/* ---------- draws, as the game's thread records them */

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	front.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	front.streams[stream_number].stride = stride;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	front.base_vertex_index = base_vertex_index;
	D3D__IndexData = index_data ? (WORD *)(__uint64)index_data->Data : NULL;
}

/* the tag cache's data, which stays until a load; the game waits for the Direct3D thread before one */
static BOOL static_data(const void *begin, unsigned long bytes)
{
	const unsigned char *data = begin;

	return static_vertices_end && data >= static_vertices_begin && data + bytes <= static_vertices_end;
}

typedef char states_vmx_check[D3DRS_MAX % 16 == 0 && (D3DTSS_MAXSTAGES * D3DTSS_MAX) % 16 == 0 ? 1 : -1];

/* the render and texture stage states the game changed since the last record (the inline setters write them), in
their order: the game sets most of them before each draw, so all are compared (ps3_vmx_words_changed) */
static void record_states(void)
{
	uint32_t pairs[(D3DRS_MAX + D3DTSS_MAXSTAGES * D3DTSS_MAX) * 2] __attribute__((aligned(8)));
	uint64_t *payload;
	unsigned long count, index;

	count = ps3_vmx_words_changed(D3D__RenderState, front.recorded_render_state, D3DRS_MAX, 0, pairs);
	/* (a stage's states follow the last's: D3DRS_MAX + stage * D3DTSS_MAX + state) */
	count += ps3_vmx_words_changed(&D3D__TextureState[0][0], &front.recorded_texture_state[0][0],
		D3DTSS_MAXSTAGES * D3DTSS_MAX, D3DRS_MAX, pairs + count * 2);
	if (count)
	{
		/* (the pairs a doubleword each: newlib's memcpy goes a byte at a time under 32 bytes) */
		payload = record_begin(_record_states, count * 2 * sizeof(uint32_t), (uint32_t)count, 0);
		for (index = 0; index < count; index++)
			payload[index] = ((const uint64_t *)pairs)[index];
		record_commit();
	}
}

static void record_attributes(void)
{
	DWORD mask = 0;
	unsigned long reg, count = 0;
	float *payload;

	/* (the registers' words compared, not a memcmp call each) */
	for (reg = 0; reg < VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		const uint32_t *now = (const uint32_t *)front.attributes[reg];
		const uint32_t *recorded = (const uint32_t *)front.recorded_attributes[reg];

		if ((now[0] ^ recorded[0]) | (now[1] ^ recorded[1]) | (now[2] ^ recorded[2]) | (now[3] ^ recorded[3]))
		{
			mask |= 1u << reg;
			count++;
		}
	}
	if (!mask)
		return;
	payload = record_begin(_record_attributes, count * sizeof(front.attributes[0]), mask, 0);
	for (reg = 0; reg < VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		if (mask & (1u << reg))
		{
			memcpy(front.recorded_attributes[reg], front.attributes[reg], sizeof(front.attributes[0]));
			memcpy(payload, front.attributes[reg], sizeof(front.attributes[0]));
			payload += 4;
		}
	}
	record_commit();
}

/* the bound surfaces' words, which the game can change between draws (the back buffer's width) */
static void record_targets(void)
{
	struct surface_words targets[2];

	surface_words_read(front.render_target, &targets[0]);
	surface_words_read(front.depth_stencil, &targets[1]);
	if (memcmp(targets, front.recorded_targets, sizeof(targets)))
	{
		memcpy(front.recorded_targets, targets, sizeof(targets));
		memcpy(record_begin(_record_render_target, sizeof(targets), 0, 0), targets, sizeof(targets));
		record_commit();
	}
}

/* (every field compared, not a memcmp call) */
static BOOL stage_texture_same(const struct stage_texture *a, const struct stage_texture *b)
{
	return !((a->bound ^ b->bound) | (a->data ^ b->data) | (a->format ^ b->format) | (a->size ^ b->size) |
		(a->palette ^ b->palette) | (a->fresh ^ b->fresh));
}

static void record_textures(DWORD draw)
{
	unsigned long stage;

	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		D3DBaseTexture *texture = front.textures[stage];
		D3DPalette *palette = front.palettes[stage];
		struct stage_texture now;

		now.bound = texture != NULL;
		now.data = texture ? texture->Data : 0;
		now.format = texture ? texture->Format : 0;
		now.size = texture ? texture->Size : 0;
		now.palette = palette ? palette->Data : 0;
		now.fresh = texture && !texture->Lock;
		/* as the Xbox's runtime: Lock holds the last draw that reads it (d3d8_resource_busy) */
		if (texture)
			texture->Lock = draw;
		if (!stage_texture_same(&now, &front.recorded_stages[stage]))
		{
			memcpy(record_begin(_record_texture, sizeof(now), (uint32_t)stage, 0), &now, sizeof(now));
			record_commit();
			front.recorded_stages[stage] = now;
			front.recorded_stages[stage].fresh = 0;
		}
	}
}

/* the timer build: a part of a call since start, without the ring's waits in it (front_call_end) */
static uint64_t front_part(int timer, uint64_t start, uint64_t *waits)
{
	uint64_t now = ps3_timer_now();

	ps3_timers[timer] += now - start - (ps3_timers[_ps3_timer_thread_wait] - *waits);
	*waits = ps3_timers[_ps3_timer_thread_wait];
	return now;
}

/* before a draw, what the game changed that its record does not carry; the draw's number */
static DWORD draw_prologue(void)
{
	DWORD draw = ring.recorded_draws + 1;
	uint64_t waits = ps3_timers[_ps3_timer_thread_wait], span = ps3_timer_now();

	if (!draw)
		draw = 1;
	ring.recorded_draws = draw;
	record_states();
	span = front_part(_ps3_timer_front_states, span, &waits);
	record_attributes();
	span = front_part(_ps3_timer_front_attributes, span, &waits);
	record_targets();
	span = front_part(_ps3_timer_front_targets, span, &waits);
	record_textures(draw);
	front_part(_ps3_timer_front_textures, span, &waits);
	return draw;
}

/* game memory into a record (16-byte aligned) 16 bytes at a time, counted for the timer log (newlib's memcpy goes a
byte at a time where an address is not 8-aligned) */
static void record_copy(void *destination, const void *source, unsigned long bytes)
{
	if ((uintptr_t)destination & 15)
		memcpy(destination, source, bytes);
	else
		ps3_vmx_copy_to_quadwords(destination, source, (uint32_t)bytes);
	ps3_timer_counts[_ps3_timer_front_draws] += (uint32_t)bytes;
}

/* a draw's streams: the tag cache's read where they are, the others copied (the game can write them again at once) */
struct stream_plan
{
	DWORD mask;
	DWORD copied;
	const unsigned char *sources[16];
	UINT strides[16];
	/* the bytes of a vertex its elements read */
	unsigned long spans[16];
};

/* (a stream's entries are written when mask first takes it: no other is read) */
static void streams_plan(struct stream_plan *plan)
{
	const struct vertex_shader_object *declaration = front.vertex_shader;
	unsigned long index;

	plan->mask = 0;
	plan->copied = 0;
	if (!declaration)
		return;
	for (index = 0; index < declaration->element_count; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		unsigned long stream = element->stream, span = element->offset + element->bytes;

		if (stream >= 16)
			continue;
		if (!(plan->mask & (1u << stream)))
		{
			DWORD data = front.streams[stream].data;

			plan->mask |= 1u << stream;
			plan->strides[stream] = front.streams[stream].stride;
			plan->sources[stream] = data ? (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(data) : NULL;
			plan->spans[stream] = 0;
			if (plan->sources[stream] && !static_data(plan->sources[stream], 1))
				plan->copied |= 1u << stream;
		}
		if (span > plan->spans[stream])
			plan->spans[stream] = span;
	}
}

static unsigned long stream_copy_bytes(const struct stream_plan *plan, unsigned long stream, unsigned long count)
{
	return count ? (count - 1) * plan->strides[stream] + plan->spans[stream] : 0;
}

static unsigned long streams_copy_bytes(const struct stream_plan *plan, unsigned long count)
{
	unsigned long index, bytes = 0;

	for (index = 0; index < 16; index++)
	{
		if (plan->copied & (1u << index))
			bytes += (stream_copy_bytes(plan, index, count) + 15) & ~15UL;
	}
	return bytes;
}

/* the record's streams, and the copies of vertices [first, first + count) at copies */
static void streams_write(const struct stream_plan *plan, struct record_draw *draw, unsigned char *copies,
	unsigned long first, unsigned long count)
{
	struct record_stream *stream = (struct record_stream *)(draw + 1);
	unsigned long index;

	draw->stream_mask = plan->mask;
	for (index = 0; index < 16; index++)
	{
		if (!(plan->mask & (1u << index)))
			continue;
		stream->stride = plan->strides[index];
		stream->pad = 0;
		stream->base = (uint64_t)plan->sources[index];
		if (plan->copied & (1u << index))
		{
			unsigned long bytes = stream_copy_bytes(plan, index, count);

			record_copy(copies, plan->sources[index] + first * plan->strides[index], bytes);
			/* (vertex first is the copy's first) */
			stream->base = (uint64_t)copies - (uint64_t)first * plan->strides[index];
			copies += (bytes + 15) & ~15UL;
		}
		stream++;
	}
}

static void draw_vertices_record(D3DPRIMITIVETYPE primitive_type_value, UINT start_vertex, UINT vertex_count)
{
	struct stream_plan plan;
	struct record_draw *draw;
	unsigned long streams;
	DWORD number;

	if (!vertex_count)
		return;
	number = draw_prologue();
	streams_plan(&plan);
	streams = bit_count(plan.mask);
	draw = record_begin(_record_draw, sizeof(*draw) + streams * sizeof(struct record_stream) +
		streams_copy_bytes(&plan, vertex_count), 0, 0);
	if (!draw)
		return;
	memset(draw, 0, sizeof(*draw));
	draw->primitive = primitive_type_value;
	draw->count = vertex_count;
	draw->start = start_vertex;
	draw->draw = number;
	streams_write(&plan, draw, (unsigned char *)(draw + 1) + streams * sizeof(struct record_stream), start_vertex,
		vertex_count);
	record_commit();
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type_value, UINT start_vertex, UINT vertex_count)
{
	front_call_begin();
	draw_vertices_record(primitive_type_value, start_vertex, vertex_count);
	front_call_end(_ps3_timer_front_draws);
}

static void draw_indexed_record(D3DPRIMITIVETYPE primitive_type_value, UINT vertex_count, CONST WORD *index_data)
{
	/* the buffer's vertices from its vertex 0, as the cache counts them; for this draw only */
	unsigned long extent = front.draw_vertex_count ? front.base_vertex_index + front.draw_vertex_count : 0;
	unsigned long streams, index_bytes = 0, first = 0, count = 0;
	uint32_t minimum = 0xffff, maximum = 0;
	struct stream_plan plan;
	struct record_draw *draw;
	unsigned char *next;
	BOOL copied_indices;
	DWORD number;

	front.draw_vertex_count = 0;
	if (!vertex_count || !index_data)
		return;
	number = draw_prologue();
	streams_plan(&plan);
	/* indices out of the tag cache are copied, as the Xbox put them in its push buffer */
	copied_indices = !static_data(index_data, vertex_count * sizeof(WORD));
	if (copied_indices)
		index_bytes = (vertex_count * sizeof(WORD) + 15) & ~15UL;
	if (plan.copied)
	{
		ps3_vmx_index_range(index_data, (uint32_t)vertex_count, &minimum, &maximum);
		first = front.base_vertex_index + minimum;
		count = maximum - minimum + 1;
	}
	streams = bit_count(plan.mask);
	draw = record_begin(_record_draw_indexed, sizeof(*draw) + streams * sizeof(struct record_stream) + index_bytes +
		streams_copy_bytes(&plan, count), 0, 0);
	if (!draw)
		return;
	memset(draw, 0, sizeof(*draw));
	draw->primitive = primitive_type_value;
	draw->count = vertex_count;
	draw->start = front.base_vertex_index;
	draw->extent = extent;
	draw->draw = number;
	next = (unsigned char *)(draw + 1) + streams * sizeof(struct record_stream);
	draw->indices = (uint64_t)index_data;
	if (copied_indices)
	{
		record_copy(next, index_data, vertex_count * sizeof(WORD));
		draw->indices = (uint64_t)next;
		next += index_bytes;
	}
	if (plan.copied)
	{
		draw->ranged = 1;
		draw->minimum = minimum;
		draw->maximum = maximum;
	}
	streams_write(&plan, draw, next, first, count);
	record_commit();
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type_value, UINT vertex_count, CONST WORD *index_data)
{
	front_call_begin();
	draw_indexed_record(primitive_type_value, vertex_count, index_data);
	front_call_end(_ps3_timer_front_draws);
}

/* ---------- immediate mode */

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type_value)
{
	front.immediate_active = TRUE;
	front.immediate_type = primitive_type_value;
	front.immediate_count = 0;
	front.immediate_written = 0;
	front.immediate_changed = 0;
}

static void immediate_layout_set(DWORD layout)
{
	unsigned long reg;

	front.immediate_layout = layout;
	front.immediate_reg_count = 0;
	for (reg = 0; reg < VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		if (layout & (1u << reg))
			front.immediate_regs[front.immediate_reg_count++] = (unsigned char)reg;
	}
	front.immediate_floats = front.immediate_reg_count * 4;
}

/* the vertices so far with the registers changed since the layout was fixed, at their values before */
static void immediate_widen(void)
{
	DWORD old_layout = front.immediate_layout;
	unsigned long old_floats = front.immediate_floats, count = front.immediate_count, vertex, slot, old_slot;
	unsigned long capacity;
	const float *in = front.immediate_vertices;
	float *vertices, *out;

	immediate_layout_set(old_layout | front.immediate_changed);
	capacity = (count + 256) * front.immediate_floats;
	vertices = malloc(capacity * sizeof(float));
	if (!vertices)
	{
		/* (those registers then take their new value for every vertex) */
		immediate_layout_set(old_layout);
		front.immediate_changed = 0;
		return;
	}
	for (vertex = 0, out = vertices; vertex < count; vertex++, in += old_floats)
	{
		for (slot = 0, old_slot = 0; slot < front.immediate_reg_count; slot++, out += 4)
		{
			unsigned long reg = front.immediate_regs[slot];

			if (old_layout & (1u << reg))
				memcpy(out, in + 4 * old_slot++, 4 * sizeof(float));
			else
				memcpy(out, front.immediate_before[reg], 4 * sizeof(float));
		}
	}
	free(front.immediate_vertices);
	front.immediate_vertices = vertices;
	front.immediate_capacity = capacity;
	front.immediate_changed = 0;
}

static void immediate_emit(void)
{
	unsigned long slot, needed;
	float *out;

	if (!front.immediate_count)
	{
		immediate_layout_set(front.immediate_written);
		front.immediate_changed = 0;
	}
	else if (front.immediate_changed)
	{
		immediate_widen();
	}
	needed = (front.immediate_count + 1) * front.immediate_floats;
	if (needed > front.immediate_capacity)
	{
		unsigned long capacity = front.immediate_capacity ? front.immediate_capacity * 2 : 4096;
		float *vertices;

		while (capacity < needed)
			capacity *= 2;
		vertices = realloc(front.immediate_vertices, capacity * sizeof(float));
		if (!vertices)
			return;
		front.immediate_vertices = vertices;
		front.immediate_capacity = capacity;
	}
	out = front.immediate_vertices + front.immediate_count * front.immediate_floats;
	for (slot = 0; slot < front.immediate_reg_count; slot++, out += 4)
		memcpy(out, front.attributes[front.immediate_regs[slot]], 4 * sizeof(float));
	front.immediate_count++;
}

static void immediate_record(void)
{
	unsigned long count = front.immediate_count;
	struct record_immediate *immediate;
	DWORD number;

	front.immediate_active = FALSE;
	if (!count)
		return;
	/* a register changed after the last vertex: the vertices keep its value before */
	if (front.immediate_changed)
		immediate_widen();
	number = draw_prologue();
	immediate = record_begin(_record_immediate, sizeof(*immediate) + count * front.immediate_floats * sizeof(float), 0,
		0);
	if (!immediate)
		return;
	immediate->primitive = front.immediate_type;
	immediate->count = count;
	immediate->written = front.immediate_layout;
	immediate->draw = number;
	record_copy(immediate + 1, front.immediate_vertices, count * front.immediate_floats * sizeof(float));
	record_commit();
}

void WINAPI D3DDevice_End(void)
{
	front_call_begin();
	immediate_record();
	front_call_end(_ps3_timer_front_draws);
}

/* the elements record the game's thread is writing (elements_begin to elements_end) */
static struct record_elements *front_elements;

/* a draw of count vertices that the caller writes at the place given, before elements_end: elements (each its register,
type, offset in the data and stride) over bytes of data; NULL: no draw */
static void *elements_begin(D3DPRIMITIVETYPE primitive_type_value, const uint32_t (*elements)[4],
	unsigned long element_count, unsigned long count, unsigned long bytes)
{
	struct record_elements *record;
	unsigned long index;
	DWORD number;

	front_elements = NULL;
	if (!count || !element_count || element_count > RECORD_ELEMENTS)
		return NULL;
	front_call_begin();
	number = draw_prologue();
	record = record_begin(_record_elements, sizeof(*record) + bytes, 0, 0);
	if (record)
	{
		memset(record, 0, sizeof(*record));
		record->primitive = primitive_type_value;
		record->count = (uint32_t)count;
		record->draw = number;
		record->element_count = (uint32_t)element_count;
		for (index = 0; index < element_count; index++)
		{
			record->elements[index].reg = elements[index][0];
			record->elements[index].type = elements[index][1];
			record->elements[index].offset = elements[index][2];
			record->elements[index].stride = elements[index][3];
		}
		front_elements = record;
	}
	front_call_end(_ps3_timer_front_draws);
	return record ? record + 1 : NULL;
}

/* the record to the Direct3D thread; the registers keep the last vertex's values, as Begin, SetVertexData and End
leave them (the next draw's prologue records them) */
static void elements_end(void)
{
	struct record_elements *record = front_elements;
	const unsigned char *data;
	unsigned long index;

	if (!record)
		return;
	front_call_begin();
	data = (const unsigned char *)(record + 1);
	for (index = 0; index < record->element_count; index++)
	{
		if (record->elements[index].reg < VERTEX_ATTRIBUTE_COUNT)
			vertex_element_convert((unsigned char)record->elements[index].type, data + record->elements[index].offset +
				(record->count - 1) * record->elements[index].stride, front.attributes[record->elements[index].reg]);
	}
	front_elements = NULL;
	record_commit();
	front_call_end(_ps3_timer_front_draws);
}

/* rasterizer_xbox_decals.c: a draw of count decal vertices written into its record: each one's decal_vertex (its
position's 3 floats, its coordinates' word), then each one's colour's 4 bytes (SetVertexData4ub's); NULL: no draw */
void *d3d8_draw_decals_begin(DWORD count)
{
	uint32_t elements[3][4] =
	{
		{ 0, D3DVSDT_FLOAT3, 0, 16 },
		{ 4, SPU_VERTICES_TEXCOORD_WORD, 12, 16 },
		{ 9, D3DVSDT_PBYTE4, 0, 4 },
	};

	elements[2][2] = (uint32_t)count * 16;
	ps3_timer_counts[_ps3_timer_decal_draws]++;
	ps3_timer_counts[_ps3_timer_decal_vertices] += count;
	return elements_begin(D3DPT_QUADLIST, (const uint32_t (*)[4])elements, 3, count, (unsigned long)count * 20);
}

void d3d8_draw_decals_end(void)
{
	elements_end();
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;
	DWORD bit;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= VERTEX_ATTRIBUTE_COUNT)
		return;
	bit = 1u << reg;
	if (front.immediate_active && front.immediate_count && !(front.immediate_layout & bit) &&
		!(front.immediate_changed & bit))
	{
		memcpy(front.immediate_before[reg], front.attributes[reg], sizeof(front.attributes[0]));
		front.immediate_changed |= bit;
	}
	front.attributes[reg][0] = a;
	front.attributes[reg][1] = b;
	front.attributes[reg][2] = c;
	front.attributes[reg][3] = d;
	if (front.immediate_active)
	{
		front.immediate_written |= bit;
		/* like the hardware, writing register 0 completes a vertex */
		if (emit || reg == 0)
			immediate_emit();
	}
}

void WINAPI D3DDevice_SetVertexData2f(INT reg, FLOAT a, FLOAT b)
{
	set_attribute(reg, a, b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4f(INT reg, FLOAT a, FLOAT b, FLOAT c, FLOAT d)
{
	set_attribute(reg, a, b, c, d);
}

void WINAPI D3DDevice_SetVertexData2s(INT reg, SHORT a, SHORT b)
{
	set_attribute(reg, (float)a, (float)b, 0.0f, 1.0f);
}

void WINAPI D3DDevice_SetVertexData4ub(INT reg, BYTE a, BYTE b, BYTE c, BYTE d)
{
	set_attribute(reg, a / 255.0f, b / 255.0f, c / 255.0f, d / 255.0f);
}

void WINAPI D3DDevice_SetVertexDataColor(INT reg, D3DCOLOR color)
{
	float value[4];

	color_to_vec4(color, value);
	set_attribute(reg, value[0], value[1], value[2], value[3]);
}

/* ---------- clearing */

static void clear_record(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	unsigned long rectangle_count = count && rectangles ? count : 0;
	struct record_clear *clear;

	record_targets();
	clear = record_begin(_record_clear, sizeof(*clear) + rectangle_count * sizeof(D3DRECT), (uint32_t)rectangle_count,
		flags);
	if (!clear)
		return;
	clear->color = color;
	memcpy(&clear->z, &z, sizeof(z));
	clear->stencil = stencil;
	clear->pad = 0;
	if (rectangle_count)
		memcpy(clear + 1, rectangles, rectangle_count * sizeof(D3DRECT));
	record_commit();
}

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	front_call_begin();
	clear_record(count, rectangles, flags, color, z, stencil);
	front_call_end(_ps3_timer_front_draws);
}

/* ---------- the Direct3D thread: the records in a ring, which the game's thread writes and this thread runs */

static void render_target_apply(const struct surface_words *targets, BOOL reset);

/* a record that runs on the game's thread: before the device's thread starts, after it stops, or a large one */
static unsigned char record_scratch[64 * 1024] __attribute__((aligned(16)));

static void states_apply(const uint32_t *pairs, uint32_t count)
{
	uint32_t changes = 0;

	for (; count; count--, pairs += 2)
	{
		changes |= state_parts[pairs[0]];
		if (pairs[0] < D3DRS_MAX)
			render_state[pairs[0]] = pairs[1];
		else
			(&texture_state[0][0])[pairs[0] - D3DRS_MAX] = pairs[1];
	}
	draw_state_changes |= changes;
}

static void attributes_apply(DWORD mask, const float *values)
{
	unsigned long reg;

	for (reg = 0; reg < VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		if (mask & (1u << reg))
		{
			memcpy(device.attributes[reg], values, sizeof(device.attributes[0]));
			values += 4;
		}
	}
}

static void record_execute(const struct record *record)
{
	const void *payload = record + 1;

	switch (record->type)
	{
	case _record_states:
		states_apply(payload, record->a);
		break;
	case _record_attributes:
		attributes_apply(record->a, payload);
		break;
	case _record_texture:
		device.stages[record->a] = *(const struct stage_texture *)payload;
		draw_state_changes |= CHANGED_TEXTURE(record->a);
		break;
	case _record_vertex_shader:
		vertex_shader_apply(record->a, (struct vertex_shader_object *)*(const uint64_t *)payload, record->b);
		break;
	case _record_constants:
		constants_store(record->a, payload, record->b);
		break;
	case _record_constant_mode:
		device.shader_constant_mode = (D3DSHADERCONSTANTMODE)record->a;
		viewport_update_constants();
		break;
	case _record_viewport:
		memcpy(&device.viewport, payload, sizeof(device.viewport));
		viewport_update_constants();
		break;
	case _record_render_target:
		render_target_apply(payload, record->b != 0);
		break;
	case _record_ui_offset:
		ui_offset = (long)(int32_t)record->a;
		break;
	case _record_clear:
		clear_run(record);
		break;
	case _record_draw:
		draw_vertices_run(payload);
		break;
	case _record_draw_indexed:
		draw_indexed_run(payload);
		break;
	case _record_immediate:
		immediate_run(payload);
		break;
	case _record_elements:
		elements_run(payload);
		break;
	case _record_timestamp:
		ps3_gpu_timestamp((int)record->a);
		draw_stamps_split((int)record->a);
		break;
	case _record_draw_stamps:
		draw_stamps_set((int)record->a - 1);
		break;
	case _record_present:
		present_run();
		break;
	case _record_particles:
		if (record->a)
			particles_begin(payload);
		else
			particles_end();
		break;
	case _record_light_bounds:
		light_bounds.on = record->a != 0;
		if (light_bounds.on)
			light_bounds.bounds = *(const spu_d3d_record_light_bounds *)payload;
		break;
	case _record_scale:
		if (record->a)
			scale_begin(payload);
		else
			scale_end();
		break;
	default:
		break;
	}
}

/* the draw a record carries, else 0 */
static DWORD record_draw_number(const struct record *record)
{
	if (record->type == _record_draw || record->type == _record_draw_indexed)
		return ((const struct record_draw *)(record + 1))->draw;
	if (record->type == _record_immediate)
		return ((const struct record_immediate *)(record + 1))->draw;
	if (record->type == _record_elements)
		return ((const struct record_elements *)(record + 1))->draw;
	return 0;
}

/* the records before position are run, and their room given back */
static void consumed_advance(uint64_t position)
{
	if (ring.consumed == position)
		return;
	ring.consumed = position;
	__sync_synchronize();
	if (ring.producers_waiting)
	{
		ps3_host_lock_enter(ring.lock);
		ps3_host_condition_broadcast(ring.progress);
		ps3_host_lock_leave(ring.lock);
	}
}

/* the records whose SPU tasks (which read their vertices) are not known done: the room from start on waits for the
SPU's tail to pass head. In order; the Direct3D thread's */
#define PENDING_RECORDS 1024

static struct
{
	uint64_t start;
	uint32_t head;
} pending_records[PENDING_RECORDS];
static unsigned long pending_first, pending_count;

static void pending_add(uint64_t start, uint32_t head)
{
	if (pending_count == PENDING_RECORDS)
	{
		ps3_spu_vertices_wait();
		pending_count = 0;
	}
	pending_records[(pending_first + pending_count) % PENDING_RECORDS].start = start;
	pending_records[(pending_first + pending_count) % PENDING_RECORDS].head = head;
	pending_count++;
}

/* the room that can come back now, without a wait: up to the first record whose tasks are not done */
static uint64_t room_returnable(uint64_t position)
{
	while (pending_count && ps3_spu_vertices_done(pending_records[pending_first].head))
	{
		pending_first = (pending_first + 1) % PENDING_RECORDS;
		pending_count--;
	}
	return pending_count ? pending_records[pending_first].start : position;
}

/* a record's room comes back after its SPU tasks: while the game's thread waits for room, as far as they are done;
when this thread goes idle, all of it after a wait */
static void direct3d_thread(void *unused)
{
	uint64_t position = 0;

	(void)unused;
	ring.consumer = ps3_host_thread_id();
	for (;;)
	{
		uint64_t published = ring.published, span;

		if (position == published)
		{
			if (ring.consumed != position)
			{
				ps3_spu_vertices_wait();
				pending_count = 0;
				consumed_advance(position);
				continue;
			}
			if (ring.stop)
				break;
			ps3_host_lock_enter(ring.lock);
			ring.consumer_sleeping = 1;
			__sync_synchronize();
			if (ring.consumed == ring.published && !ring.stop)
				ps3_host_condition_wait(ring.work, WAIT_MICROSECONDS);
			ring.consumer_sleeping = 0;
			ps3_host_lock_leave(ring.lock);
			continue;
		}
		/* the records after the position that showed them */
		__asm__ volatile("lwsync" : : : "memory");
		span = ps3_timer_now();
		while (position != published)
		{
			const struct record *record = (const struct record *)(ring.memory + position % RING_BYTES);
			DWORD draw = record_draw_number(record);
			uint32_t head = ps3_spu_vertices_head();
			uint64_t record_span = ps3_timer_now();

			record_execute(record);
			if (record->type <= _record_present)
			{
				ps3_timer_add(_ps3_timer_record_wrap + (int)record->type, record_span);
				ps3_timer_counts[_ps3_timer_record_wrap + record->type]++;
			}
			/* the record read before its room is given back */
			__asm__ volatile("lwsync" : : : "memory");
			if (draw)
				ring.completed_draws = draw;
			if (ps3_spu_vertices_head() != head)
				pending_add(position, ps3_spu_vertices_head());
			position += record->bytes;
			if (ring.producers_waiting || !pending_count)
				consumed_advance(room_returnable(position));
		}
		ps3_timer_add(_ps3_timer_thread_busy, span);
	}
	ring.stopped = 1;
	__sync_synchronize();
	ps3_host_lock_enter(ring.lock);
	ps3_host_condition_broadcast(ring.progress);
	ps3_host_lock_leave(ring.lock);
}

/* (the game's thread) the records so far to the Direct3D thread */
static void direct3d_publish(void)
{
	if (!ring.running || ring.dead || ring.published == ring.written)
		return;
	__asm__ volatile("lwsync" : : : "memory");
	ring.published = ring.written;
	__sync_synchronize();
	if (spu.on)
	{
		/* (its store ends the SPU's reservation of the bell's line) */
		spu.bell->published = ring.published;
		return;
	}
	if (ring.consumer_sleeping)
	{
		ps3_host_lock_enter(ring.lock);
		ps3_host_condition_broadcast(ring.work);
		ps3_host_lock_leave(ring.lock);
	}
}

/* the records run so far, their room given back */
static uint64_t ring_consumed(void)
{
	return spu.on ? spu.echo->consumed : ring.consumed;
}

/* until the Direct3D thread has run the records before this position (published ones) */
static void direct3d_wait(uint64_t position)
{
	uint64_t span;

	if (!ring.running || (int64_t)(ring_consumed() - position) >= 0)
		return;
	span = ps3_timer_now();
	if (spu.on)
		spu_wait(position);
	while (!spu.on && (int64_t)(ring.consumed - position) < 0 && !ring.stopped)
	{
		ps3_host_lock_enter(ring.lock);
		ring.producers_waiting++;
		__sync_synchronize();
		if ((int64_t)(ring.consumed - position) < 0 && !ring.stopped)
			ps3_host_condition_wait(ring.progress, WAIT_MICROSECONDS);
		ring.producers_waiting--;
		ps3_host_lock_leave(ring.lock);
	}
	ps3_timer_add(_ps3_timer_thread_wait, span);
}

/* (the game's thread) every record run: the Direct3D thread waits for the next, and the game's thread may use its
state until it records one */
static void direct3d_drain(void)
{
	if (!ring.running)
		return;
	direct3d_publish();
	direct3d_wait(ring.written);
}

/* room for bytes more in the ring */
static void ring_room(unsigned long bytes)
{
	if (ring.written + bytes - ring_consumed() <= RING_BYTES)
		return;
	direct3d_publish();
	direct3d_wait(ring.written + bytes - RING_BYTES);
}

/* the lines a record starts, zeroed in the cache (a store that misses reads its line from memory first): those past
its start, which hold no record, within the room given back */
static void ring_lines_zero(unsigned long bytes)
{
	uint64_t line = (ring.written + RING_LINE - 1) & ~(uint64_t)(RING_LINE - 1);
	uint64_t end = ring.written + bytes, room = ring_consumed() + RING_BYTES - RING_LINE + 1;

	if (room < end)
		end = room;
	if (line < end)
		ps3_vmx_zero_lines(ring.memory + line % RING_BYTES, (uint32_t)((end - line + RING_LINE - 1) / RING_LINE));
}

static void *record_begin(uint32_t type, unsigned long payload_bytes, uint32_t a, uint32_t b)
{
	unsigned long bytes = (sizeof(struct record) + payload_bytes + 15) & ~15UL;
	struct record *record;

	/* (the SPU has the state: a large record waits for the room, one past half the ring is dropped) */
	if (spu.on && bytes > RING_BYTES / 2)
	{
		platform_log("Direct3D: a record of %lu KB dropped", bytes / 1024);
		return NULL;
	}
	if (ring.running && !ring.dead && (bytes <= RECORD_LARGEST || spu.on))
	{
		unsigned long offset = (unsigned long)(ring.written % RING_BYTES);

		/* a record never wraps: the end's bytes are skipped */
		if (offset + bytes > RING_BYTES)
		{
			ring_room(RING_BYTES - offset);
			record = (struct record *)(ring.memory + offset);
			record->type = _record_wrap;
			record->bytes = (uint32_t)(RING_BYTES - offset);
			ring.written += RING_BYTES - offset;
		}
		ring_room(bytes);
		ring_lines_zero(bytes);
		record = (struct record *)(ring.memory + ring.written % RING_BYTES);
		ring.current_inline = FALSE;
	}
	else
	{
		if (bytes <= sizeof(record_scratch))
		{
			record = (struct record *)record_scratch;
		}
		else
		{
			if (bytes > ring.large_bytes)
			{
				free(ring.large);
				ring.large = malloc(bytes);
				ring.large_bytes = ring.large ? bytes : 0;
			}
			if (!ring.large || ring.dead)
				return NULL;
			record = (struct record *)ring.large;
		}
		ring.current_inline = TRUE;
	}
	record->type = type;
	record->bytes = (uint32_t)bytes;
	record->a = a;
	record->b = b;
	ring.current = record;
	return record + 1;
}

static void record_commit(void)
{
	struct record *record = ring.current;

	if (ring.dead)
		return;
	if (ring.current_inline)
	{
		/* after the records before it; the next inline record reuses its memory, which the SPU reads first */
		direct3d_drain();
		record_execute(record);
		ps3_spu_vertices_wait();
		return;
	}
	ring.written += record->bytes;
	if (ring.written - ring.published >= PUBLISH_BYTES)
		direct3d_publish();
}

static void direct3d_start(void)
{
#if DIRECT3D_THREAD
	/* (on whole lines: ring_lines_zero) */
	unsigned char *memory = malloc(RING_BYTES + RING_LINE);

	ring.memory = memory ? (unsigned char *)(((uintptr_t)memory + RING_LINE - 1) & ~(uintptr_t)(RING_LINE - 1)) : NULL;
	ring.lock = ps3_host_lock_create(FALSE);
	ring.work = ring.lock ? ps3_host_condition_create(ring.lock) : NULL;
	ring.progress = ring.lock ? ps3_host_condition_create(ring.lock) : NULL;
	if (!ring.memory || !ring.work || !ring.progress)
	{
		platform_log("Direct3D: no memory for the Direct3D thread's ring");
		return;
	}
	ring.producer = ps3_host_thread_id();
	ring.running = TRUE;
	/* the records' work on an SPU, else on the PPE's other thread */
	if (spu_direct3d_start())
	{
		platform_log("Direct3D: the records run on an SPU (%lu pixel shaders in its table)",
			(unsigned long)spu.fragment_count);
		ps3_gpu_set_stop_hook(direct3d_stop);
		return;
	}
	/* above the game's threads, as the vertical blank's: it sleeps while it has no records */
	if (ps3_host_thread_create(direct3d_thread, NULL, 0x100000, PS3_HOST_PRIORITY_NORMAL - 1, "direct3d") != 0)
	{
		ring.running = FALSE;
		return;
	}
	ps3_gpu_set_stop_hook(direct3d_stop);
#endif
}

/* the Direct3D thread ends after the records published (the process ends, or the display thread takes the RSX) */
static void direct3d_stop(void)
{
	uint64_t start;

	if (!ring.running || ring.dead)
		return;
	if (ps3_host_thread_id() == ring.producer)
		direct3d_publish();
	ring.dead = TRUE;
	if (spu.on)
	{
		spu.stop = 1;
		ps3_spu_direct3d_stop();
		start = ps3_host_microseconds();
		while (!spu.stopped && ps3_host_microseconds() - start < 3000000)
			ps3_host_sleep_microseconds(1000);
		return;
	}
	if (ps3_host_thread_id() == ring.consumer)
		return;
	ring.stop = 1;
	__sync_synchronize();
	ps3_host_lock_enter(ring.lock);
	ps3_host_condition_broadcast(ring.work);
	ps3_host_lock_leave(ring.lock);
	start = ps3_host_microseconds();
	while (!ring.stopped && ps3_host_microseconds() - start < 3000000)
		ps3_host_sleep_microseconds(1000);
}

/* a resource's last draw not done yet (a texture's Lock: draw_prologue), else any record not done */
static BOOL resource_pending(const DWORD *fields)
{
	if (!ring.running || ring.dead || ring_consumed() == ring.written)
		return FALSE;
	if ((fields[0] & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_TEXTURE)
		return fields[2] && (int32_t)(fields[2] - (spu.on ? spu.echo->completed_draws : ring.completed_draws)) > 0;
	return TRUE;
}

/* d3d8_resources.c: whether the Direct3D thread can still read a resource (D3DResource_IsBusy) */
BOOL d3d8_resource_busy(D3DResource *resource)
{
	if (ps3_host_thread_id() == ring.producer)
		direct3d_publish();
	return resource_pending((const DWORD *)resource);
}

/* d3d8_resources.c: until it cannot (D3DResource_BlockUntilNotBusy, a release) */
void d3d8_resource_wait(D3DResource *resource)
{
	const DWORD *fields = (const DWORD *)resource;

	if (!ring.running || ring.dead)
		return;
	if (ps3_host_thread_id() == ring.producer)
		direct3d_publish();
	while (spu.on && resource_pending(fields) && !spu.stopped)
		ps3_host_sleep_microseconds(20);
	while (!spu.on && resource_pending(fields) && !ring.stopped)
	{
		ps3_host_lock_enter(ring.lock);
		ring.producers_waiting++;
		__sync_synchronize();
		if (resource_pending(fields) && !ring.stopped)
			ps3_host_condition_wait(ring.progress, WAIT_MICROSECONDS);
		ring.producers_waiting--;
		ps3_host_lock_leave(ring.lock);
	}
}

/* ps3_timers.h: a split of the RSX's frame, in the records' order; returns the time now */
uint64_t ps3_timers_rsx_wait(int part)
{
	if (device.created)
	{
		record_begin(_record_timestamp, 0, (uint32_t)part, 0);
		record_commit();
	}
	return ps3_timer_now();
}

/* ps3_timers.h: the draws from here stamped in a part or not (-1), in the records' order */
void ps3_timers_draw_stamps(int part)
{
	ps3_draw_stamps_part = part;
	if (device.created)
	{
		record_begin(_record_draw_stamps, 0, (uint32_t)(part + 1), 0);
		record_commit();
	}
}

/* ---------- the Direct3D thread's work on an SPU (spu_direct3d.c): the service thread calls its chunks of commands
and answers its requests (memory, translations, the flip) */

typedef char spu_records_check[(int)_record_present == (int)_spu_d3d_record_present &&
	(int)_record_screen == (int)_spu_d3d_record_screen && (int)_record_tag_cache == (int)_spu_d3d_record_tag_cache &&
	(int)_record_label == (int)_spu_d3d_record_label && (int)_record_particles == (int)_spu_d3d_record_particles &&
	(int)_record_light_bounds == (int)_spu_d3d_record_light_bounds &&
	(int)_record_scale == (int)_spu_d3d_record_scale &&
	sizeof(struct record) == sizeof(spu_d3d_record) && sizeof(struct record_draw) == sizeof(spu_d3d_record_draw) &&
	sizeof(struct record_elements) == sizeof(spu_d3d_record_elements) &&
	sizeof(struct stage_texture) == sizeof(spu_d3d_stage_texture) &&
	sizeof(struct surface_words) == sizeof(spu_d3d_surface_words) && sizeof(D3DVIEWPORT8) == sizeof(spu_d3d_viewport) &&
	sizeof(struct ps3_gpu_texture) == 40 && sizeof(struct rsx_fragment_key) == SPU_D3D_KEY_WORDS * 4 ? 1 : -1];
typedef char spu_shader_check[offsetof(struct vertex_shader_object, elements) == SPU_D3D_SHADER_ELEMENTS &&
	offsetof(struct vertex_shader_object, element_count) == SPU_D3D_SHADER_ELEMENT_COUNT &&
	offsetof(struct vertex_shader_object, program) == SPU_D3D_SHADER_PROGRAM &&
	offsetof(struct vertex_shader_object, translated) == SPU_D3D_SHADER_TRANSLATED &&
	offsetof(struct vertex_shader_object, untranslatable) == SPU_D3D_SHADER_UNTRANSLATABLE &&
	sizeof(struct vertex_shader_object) <= SPU_D3D_SHADER_BYTES && sizeof(struct vertex_element) == 6 ? 1 : -1];
/* spu_direct3d.c's copies of the XDK's values and of ps3_timers.h's */
typedef char spu_values_check[D3DRS_PSTEXTUREMODES == 116 && D3DRS_FOGCOLOR == 118 && D3DRS_FILLMODE == 119 &&
	D3DRS_ZENABLE == 123 && D3DRS_CULLMODE == 127 && D3DRS_POLYGONOFFSETZSLOPESCALE == 77 &&
	D3DRS_SOLIDOFFSETENABLE == 81 && D3DRS_BLENDCOLOR == 75 && D3DRS_STENCILWRITEMASK == 73 && D3DRS_ZFUNC == 57 &&
	D3DTSS_BORDERCOLOR == 29 && D3DTSS_BUMPENVMAT10 == 25 && D3DTSS_BUMPENVMAT11 == 24 && D3DTSS_ALPHAKILL == 21 &&
	D3DTSS_MIPMAPLODBIAS == 16 && D3DTSS_MAX == 32 && D3DRS_MAX == 144 && D3DBLENDOP_REVSUBTRACTSIGNED == 61445 &&
	D3DFRONT_CCW == 2305 && D3DTALPHAKILL_ENABLE == 4 && D3DTEXF_ANISOTROPIC == 3 && D3DTADDRESS_CLAMPTOEDGE == 5 &&
	D3DBLEND_ZERO == 0 && D3DBLEND_ONE == 1 && D3DBLEND_SRCALPHA == 770 && D3DBLEND_INVSRCALPHA == 771 &&
	D3DBLEND_DESTCOLOR == 774 && D3DSTENCILOP_KEEP == 7680 && D3DRS_ALPHABLENDENABLE == 59 && D3DRS_SRCBLEND == 62 &&
	D3DRS_DESTBLEND == 63 && D3DRS_ZWRITEENABLE == 64 && D3DRS_COLORWRITEENABLE == 67 && D3DRS_STENCILZFAIL == 68 &&
	D3DRS_STENCILPASS == 69 && D3DRS_BLENDOP == 74 && D3DRS_STENCILENABLE == 124 && D3DRS_STENCILFAIL == 125 &&
	D3DRS_ALPHATESTENABLE == 60 &&
	_ps3_timer_record_present - _ps3_timer_thread_busy == _spu_d3d_timer_record_present &&
	_ps3_timer_draw_constants - _ps3_timer_thread_busy == _spu_d3d_timer_draw_constants &&
	_ps3_timer_present == SPU_D3D_TIMER_PRESENT && _ps3_timer_specular_lights == SPU_D3D_TIMER_SPECULAR_LIGHTS &&
	_ps3_timer_specular_fog == SPU_D3D_TIMER_SPECULAR_FOG && _ps3_stamp_specular_fog == SPU_D3D_STAMP_SPECULAR_FOG &&
	PS3_DRAW_STAMP_PART_SHIFT == SPU_D3D_STAMP_PART_SHIFT ? 1 : -1];

/* the label the start's test of the chunks writes */
#define SPU_TEST_LABEL 72
#define SPU_TEST_VALUE 0x5ca1ab1eu
#define SPU_CHUNKS_BYTES (6UL * 1024 * 1024)
/* the pack's pixel shaders and the ones translated at a draw */
#define SPU_FRAGMENTS_SPARE 512


/* (fragment_entry_add) the entry in the SPU's table, first in its bucket */
static void spu_fragment_add(struct fragment_entry *entry)
{
	spu_d3d_fragment *out;
	uint32_t index, bucket;

	entry->spu_index = -1;
	if (!spu.fragments || spu.fragment_count == spu.fragment_capacity)
		return;
	index = spu.fragment_count;
	out = &spu.fragments[index];
	memset(out, 0, sizeof(*out));
	memcpy(out->key, &entry->key, sizeof(out->key));
	out->hash = entry->hash;
	bucket = entry->hash % SPU_D3D_FRAGMENT_BUCKETS;
	out->next = spu_fragment_buckets[bucket];
	out->stamp_key = entry->stamp_key;
	out->translated = entry->translated != 0;
	out->microcode = (uint32_t)(uintptr_t)entry->program.microcode;
	out->word_count = entry->program.word_count;
	out->control = entry->program.control;
	out->register_count = entry->program.register_count;
	out->texcoord_mask = entry->program.texcoord_mask;
	out->uniform_count = entry->program.uniform_count;
	for (index = 0; index < entry->program.uniform_count && index < 32; index++)
		out->uniforms[index] = (uint32_t)entry->program.uniforms[index].word << 8 | entry->program.uniforms[index].uniform;
	out->ppe_entry = (uint32_t)(uintptr_t)entry;
	__sync_synchronize();
	spu_fragment_buckets[bucket] = (int32_t)spu.fragment_count;
	entry->spu_index = (int32_t)spu.fragment_count++;
}

/* the palette's 256 entries as ARGB (palette_load's), from its data */
static void spu_palette_entries(DWORD data, DWORD *entries)
{
	const DWORD *words = (const DWORD *)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
	int index;

	for (index = 0; index < 256; index++)
		entries[index] = __builtin_bswap32(words[index]);
}

/* texture_check's PPE part for the SPU's entry: the upload (texture_upload), the button icons' texture for it */
static void spu_texture_request(const uint32_t *words, spu_d3d_response *answer)
{
	uint32_t index = words[0];
	BOOL changed = words[1] != 0;
	DWORD palette_data = words[2], palette[256];
	int style = (int)words[3];
	uint32_t written = ps3_gpu_textures_written_count();
	unsigned long epoch = texture_epoch;
	spu_d3d_texture *spu_entry;
	struct texture_entry entry;
	const unsigned char *source;

	if (index >= SPU_D3D_TEXTURES)
		return;
	spu_entry = &spu.textures[index];
	source = (const unsigned char *)(uintptr_t)spu_entry->source;
	memset(&entry, 0, sizeof(entry));
	entry.data = spu_entry->data;
	entry.format_word = spu_entry->format_word;
	entry.size_word = spu_entry->size_word;
	xgpu_texture_describe(entry.format_word, entry.size_word, &entry.description);
	entry.memory = (void *)(uintptr_t)spu_entry->memory;
	entry.bytes = spu_entry->bytes;
	if (palette_data)
		spu_palette_entries(palette_data, palette);
	if (changed && source)
	{
		uint64_t span = ps3_timer_now();

		texture_upload(&entry, source, palette_data ? palette : NULL);
		/* (the SPU's texels before its commands sample them) */
		ps3_spu_vertices_wait();
		spu_entry->memory = (uint32_t)(uintptr_t)entry.memory;
		spu_entry->bytes = (uint32_t)entry.bytes;
		memcpy(spu_entry->gpu, &entry.gpu, sizeof(spu_entry->gpu));
		spu_entry->hires_style = -2;
		ps3_timer_add(_ps3_timer_texture_upload, span);
	}
	if (spu_entry->memory && spu_entry->hires_style != style)
	{
		const struct xgpu_texture_description *description = &entry.description;
		struct ps3_gpu_texture hires_gpu;

		memcpy(&hires_gpu, spu_entry->hires_gpu, sizeof(hires_gpu));
		spu_entry->hires_style = (int8_t)style;
		spu_entry->hires = !palette_data && spu_entry->hires_candidate && source &&
			hud_hires_rsx_override(source, description->width, description->height, description->levels > 1 ?
			xgpu_texture_level_offset(description, 1) : xgpu_texture_face_size(description), &hires_gpu);
		memcpy(spu_entry->hires_gpu, &hires_gpu, sizeof(hires_gpu));
	}
	/* (an upload moved texture_epoch; texels written, as ps3_gpu.c's texture cache flag) */
	answer->words[0] = texture_epoch != epoch;
	answer->words[1] = ps3_gpu_textures_written_count() != written;
	answer->status = 1;
}

/* render_target_lookup's allocation */
static void spu_target_request(const uint32_t *words, spu_d3d_response *answer)
{
	uint32_t zcull_before[4], zcull_after[4], offset, pitch, bytes;
	void *memory;

	ps3_gpu_zcull_state(zcull_before);
	memory = ps3_gpu_target_allocate(words[1], words[2], words[3] != 0, words[4] != 0, words[5] != 0, &offset, &pitch,
		&bytes);
	if (!memory || spu.target_count == sizeof(spu.targets) / sizeof(spu.targets[0]))
	{
		if (memory)
			ps3_gpu_target_free(memory, bytes);
		platform_log("Direct3D: no RSX memory for a %lux%lu target", (unsigned long)words[1], (unsigned long)words[2]);
		return;
	}
	spu.targets[spu.target_count].memory = memory;
	spu.targets[spu.target_count++].bytes = bytes;
	ps3_gpu_zcull_state(zcull_after);
	answer->status = 1;
	answer->words[0] = offset;
	answer->words[1] = pitch;
	answer->words[2] = (uint32_t)(uintptr_t)memory;
	memcpy(&answer->words[3], zcull_after, sizeof(zcull_after));
	/* (a new ZCull surface's data is not good until its first full clear) */
	answer->words[7] = zcull_after[0] && (!zcull_before[0] || zcull_after[1] != zcull_before[1]);
}

/* fragment_program_get's lookup and translation of a cleaned key: the SPU table's index, -1 none */
static void spu_fragment_request(const uint32_t *words, spu_d3d_response *answer)
{
	struct rsx_fragment_key key;
	struct fragment_entry *entry;
	DWORD hash;

	memcpy(&key, words, sizeof(key));
	if (!fragment_pack_loaded)
		fragment_pack_load();
	hash = hash_words(&key, sizeof(key));
	for (entry = fragment_buckets[hash % FRAGMENT_BUCKETS]; entry; entry = entry->next)
	{
		if (entry->hash == hash && fragment_key_same(&entry->key, &key))
			break;
	}
	if (!entry)
	{
		char text[RSX_FRAGMENT_KEY_TEXT];

		entry = calloc(1, sizeof(*entry));
		if (!entry)
		{
			answer->words[0] = (uint32_t)-1;
			return;
		}
		entry->hash = hash;
		entry->key = key;
		entry->translated = rsx_fragment_program_translate(&key, &entry->program) == 0;
		fragment_translated_count++;
		rsx_fragment_key_text(&key, text);
		if (!entry->translated)
			platform_log("Direct3D: cannot translate a pixel shader: %s", text);
		else
			platform_log("Direct3D: pixel shader %lu not in the pack, translated (%lu words): %s",
				fragment_translated_count, (unsigned long)entry->program.word_count, text);
		fragment_entry_add(entry);
	}
	answer->words[0] = (uint32_t)entry->spu_index;
	answer->status = entry->spu_index >= 0;
}

/* ps3_gpu_present's end: its log, the flip and the next frame */
static void spu_present_end(const uint32_t *words, spu_d3d_response *answer)
{
	struct ps3_gpu_frame_spu frame;
	unsigned int placeholders[8][2];
	int index;

	ps3_draw_stamp_dropped += words[2];
	if (words[1])
		ps3_gpu_log_spu(&spu_statistics_block);
	memset(&frame, 0, sizeof(frame));
	ps3_gpu_present_end_spu(&frame);
	answer->words[0] = frame.index;
	answer->words[1] = frame.finished;
	answer->words[2] = frame.programs_reset;
	answer->words[3] = frame.stamps_read;
	answer->words[4] = (uint32_t)hud_hires_rsx_style();
	hud_hires_rsx_placeholders(placeholders);
	for (index = 0; index < 8; index++)
	{
		answer->words[5 + 2 * index] = placeholders[index][0];
		answer->words[6 + 2 * index] = placeholders[index][1];
	}
	answer->status = 1;
}

/* present_run's lines every STATISTICS_FRAMES frames, from the SPU's statistics */
static void spu_log(void)
{
	const spu_d3d_statistics *s = &spu_statistics_block;
	struct ps3_spu_vertices_statistics vertices;
	unsigned long frame_number = s->frame, frames = frame_number == 1 ? 1 : STATISTICS_FRAMES, type;
	char types[512];
	size_t used = 0;

	platform_log("Direct3D: frame %lu: %lu draws; skipped %lu (program), %lu (target), %lu (scratch), "
		"%lu (viewport); %lu texture uploads, %lu KB (%lu KB by the SPU; %lu KB of checks hashed by it); %lu pixel "
		"shaders (%lu translated at a draw); %lu textures in RSX memory, %lu KB (%lu KB not drawn for %lu frames)",
		frame_number, (unsigned long)s->draws, (unsigned long)s->skipped_program, (unsigned long)s->skipped_target,
		(unsigned long)s->skipped_scratch, (unsigned long)s->skipped_scissor, statistics.texture_uploads,
		statistics.texture_bytes / 1024, statistics.texture_spu_bytes / 1024, (unsigned long)s->texture_hashed / 1024,
		fragment_program_count, fragment_translated_count, (unsigned long)s->textures,
		(unsigned long)s->texture_bytes / 1024, (unsigned long)s->texture_unused_bytes / 1024,
		(unsigned long)STATISTICS_FRAMES);
	platform_log("Direct3D: frame %lu: vertex elements converted: %lu into the cache, %lu at draws; vertex "
		"cache %lu arrays, %lu of %lu KB, %lu refused", frame_number, (unsigned long)s->cached_elements,
		(unsigned long)s->converted_elements, (unsigned long)s->arrays, (unsigned long)s->arena_used / 1024,
		(unsigned long)(VERTEX_ARENA_BYTES / 1024), (unsigned long)s->cache_refused);
	ps3_spu_vertices_statistics(&vertices);
	platform_log("Direct3D: frame %lu: the Direct3D SPU: %lu chunks, %lu KB of commands, %lu requests (%lu us a "
		"frame waiting for them); the conversions' SPU %s: %lu tasks, %lu us a frame", frame_number,
		(unsigned long)s->chunks, (unsigned long)s->chunk_bytes / 1024, (unsigned long)s->requests,
		(unsigned long)(s->request_ticks / 80 / frames), vertices.up ? "on" : "off", (unsigned long)vertices.tasks,
		(unsigned long)(vertices.busy_microseconds / frames));
	for (type = 0; type < SPU_D3D_TYPE_ELEMENTS && used < sizeof(types) - 32; type++)
	{
		if (s->type_elements[type])
			used += (size_t)snprintf(types + used, sizeof(types) - used, "%s0x%02lx %lu", used ? ", " : "", type,
				(unsigned long)s->type_elements[type]);
	}
	platform_log("Direct3D: frame %lu: the elements converted by type: %s", frame_number, used ? types : "none");
	statistics.texture_uploads = statistics.texture_bytes = statistics.texture_spu_bytes = 0;
}

static void spu_request_answer(const spu_d3d_request *request, spu_d3d_response *answer)
{
	const uint32_t *words = request->words;
	uint32_t index;

	memset(answer, 0, sizeof(*answer));
	switch (request->kind)
	{
	case _spu_d3d_request_vertex_program:
	{
		struct vertex_shader_object *object = (struct vertex_shader_object *)(uintptr_t)words[0];

		if (object && object->signature == VERTEX_SHADER_SIGNATURE)
			answer->status = vertex_program_get(object) != NULL;
		break;
	}
	case _spu_d3d_request_fragment_program:
		spu_fragment_request(words, answer);
		break;
	case _spu_d3d_request_target:
		spu_target_request(words, answer);
		break;
	case _spu_d3d_request_texture:
		spu_texture_request(words, answer);
		break;
	case _spu_d3d_request_placeholder:
	{
		struct ps3_gpu_texture gpu;
		uint32_t written = ps3_gpu_textures_written_count();

		memset(&gpu, 0, sizeof(gpu));
		answer->status = hud_hires_rsx_asset_texture((long)words[0], (int)words[1], &gpu);
		memcpy(answer->words, &gpu, sizeof(gpu));
		answer->words[10] = ps3_gpu_textures_written_count() != written;
		break;
	}
	case _spu_d3d_request_wait_idle:
		ps3_gpu_wait_idle();
		answer->status = 1;
		break;
	case _spu_d3d_request_present_begin:
		ps3_gpu_present_begin_spu(answer->words);
		answer->status = 1;
		break;
	case _spu_d3d_request_present_end:
		spu_present_end(words, answer);
		break;
	case _spu_d3d_request_free:
		for (index = 0; index < words[SPU_D3D_REQUEST_WORDS - 1] && index < SPU_D3D_REQUEST_WORDS / 2; index++)
			ps3_gpu_memory_free((void *)(uintptr_t)words[2 * index], words[2 * index + 1], _ps3_gpu_memory_texture);
		answer->status = 1;
		break;
	case _spu_d3d_request_arena:
		vertex_arena_reserve();
		answer->words[0] = (uint32_t)(uintptr_t)vertex_arena.memory;
		answer->words[1] = vertex_arena.offset;
		answer->words[2] = vertex_arena.memory ? (uint32_t)VERTEX_ARENA_BYTES : 0;
		answer->status = 1;
		break;
	case _spu_d3d_request_log:
		spu_log();
		answer->status = 1;
		break;
	default:
		break;
	}
}

/* the SPU's chunks called as they come, its requests answered */
static void spu_service_thread(void *unused)
{
	uint32_t offsets[SPU_D3D_CHUNK_LIST];

	(void)unused;
	while (!spu.stop)
	{
		uint32_t request, head, count = 0;

		ps3_spu_direct3d_wait(0, 1000);
		if (spu.stop)
			break;
		ps3_host_lock_enter(spu.gpu_lock);
		/* (the request before the chunks: those published before it are in this count) */
		request = spu.echo->request;
		__asm__ volatile("lwsync" : : : "memory");
		head = spu.echo->chunks;
		__asm__ volatile("lwsync" : : : "memory");
		while (spu.called != head && count < SPU_D3D_CHUNK_LIST)
			offsets[count++] = spu_chunk_list[spu.called++ & (SPU_D3D_CHUNK_LIST - 1)].offset;
		if (count)
		{
			ps3_gpu_call_chunks(offsets, count);
			spu.bell->called = spu.called;
		}
		if (request != spu.answered && spu_request_block.number == request)
		{
			spu_request_answer(&spu_request_block, &spu_response_block);
			spu_response_block.number = request;
			spu.answered = request;
			__asm__ volatile("lwsync" : : : "memory");
			spu.bell->response = request;
		}
		ps3_host_lock_leave(spu.gpu_lock);
	}
	spu.stopped = 1;
	__sync_synchronize();
}

/* the records run up to position (published ones), the SPU's echo waited for */
static void spu_wait(uint64_t position)
{
	uint32_t spins = 0;

	while ((int64_t)(spu.echo->consumed - position) < 0 && !spu.stopped)
	{
		/* (db16cyc a while: the SPU's records take microseconds) */
		if (++spins < 500)
		{
			__asm__ volatile("or 31,31,31\n\tor 31,31,31\n\tor 31,31,31\n\tor 31,31,31");
			continue;
		}
		ps3_spu_direct3d_drain(1);
		spu.bell->notify = position;
		__sync_synchronize();
		if ((int64_t)(spu.echo->consumed - position) < 0)
			ps3_spu_direct3d_wait(1, 1000);
		spu.bell->notify = 0;
	}
	__asm__ volatile("lwsync" : : : "memory");
}

/* (the game thread, the SPU drained) until the service thread has called every chunk */
static void spu_chunks_wait(void)
{
	while (spu.on && !spu.stopped && spu.called != spu.echo->chunks)
		ps3_host_sleep_microseconds(50);
}

static void direct3d_publish(void);
static BOOL spu_direct3d_test(void);

/* the SPU's set-up from the device's state now; TRUE when it runs and its first chunk reached the RSX */
static BOOL spu_direct3d_start(void)
{
	spu_d3d_setup *setup = &spu.setup;
	uint32_t spare_bytes = 0, offset = 0, index;
	void *spare = ps3_gpu_command_spare(&spare_bytes);

	if (!spare || spare_bytes < SPU_CHUNKS_BYTES || !ps3_gpu_io_offset(spare, &offset))
	{
		platform_log("Direct3D: no IO memory for the Direct3D SPU's commands");
		return FALSE;
	}
	spu.gpu_lock = ps3_host_lock_create(FALSE);
	spu.textures = ps3_spu_buffer_allocate(SPU_D3D_TEXTURES * sizeof(spu_d3d_texture));
	spu.arrays = ps3_spu_buffer_allocate(SPU_D3D_ARRAYS * sizeof(spu_d3d_array));
	spu.index_arrays = ps3_spu_buffer_allocate(SPU_D3D_INDEX_ARRAYS * sizeof(spu_d3d_index_array));
	spu.fragment_capacity = rsx_packed_fragment_count + SPU_FRAGMENTS_SPARE;
	spu.fragments = ps3_spu_buffer_allocate(spu.fragment_capacity * sizeof(spu_d3d_fragment));
	if (!spu.gpu_lock || !spu.textures || !spu.arrays || !spu.index_arrays || !spu.fragments)
	{
		platform_log("Direct3D: no memory for the Direct3D SPU's tables");
		free(spu.textures);
		free(spu.arrays);
		free(spu.index_arrays);
		free(spu.fragments);
		spu.textures = NULL;
		spu.arrays = NULL;
		spu.index_arrays = NULL;
		spu.fragments = NULL;
		return FALSE;
	}
	memset(spu.textures, 0, SPU_D3D_TEXTURES * sizeof(spu_d3d_texture));
	for (index = 0; index < SPU_D3D_FRAGMENT_BUCKETS; index++)
		spu_fragment_buckets[index] = -1;
	/* (the pack's programs in both tables before the SPU reads them) */
	if (!fragment_pack_loaded)
		fragment_pack_load();
	memset(setup, 0, sizeof(*setup));
	setup->ring = (uint32_t)(uintptr_t)ring.memory;
	setup->ring_bytes = (uint32_t)RING_BYTES;
	setup->chunks = (uint32_t)(uintptr_t)spare;
	setup->chunks_offset = offset;
	setup->chunks_bytes = (uint32_t)SPU_CHUNKS_BYTES;
	setup->chunk_list = (uint32_t)(uintptr_t)spu_chunk_list;
	setup->request = (uint32_t)(uintptr_t)&spu_request_block;
	setup->response = (uint32_t)(uintptr_t)&spu_response_block;
	setup->statistics = (uint32_t)(uintptr_t)&spu_statistics_block;
	ps3_gpu_direct3d_setup(setup);
	setup->textures = (uint32_t)(uintptr_t)spu.textures;
	setup->arrays = (uint32_t)(uintptr_t)spu.arrays;
	setup->index_arrays = (uint32_t)(uintptr_t)spu.index_arrays;
	setup->fragments = (uint32_t)(uintptr_t)spu.fragments;
	setup->fragment_buckets = (uint32_t)(uintptr_t)spu_fragment_buckets;
	setup->byte_floats = (uint32_t)(uintptr_t)byte_floats;
	setup->initial = (uint32_t)(uintptr_t)&spu_initial;
	setup->texture_cache = (uint32_t)(uintptr_t)physical_memory_get_texture_cache_base_address();
	setup->texture_cache_bytes = (uint32_t)TEXTURE_CACHE_BYTES;
	setup->stamp_part_offset = (uint32_t)offsetof(struct fragment_entry, stamp_parts);
	setup->stamp_textures_offset = (uint32_t)offsetof(struct fragment_entry, stamp_textures);
	memcpy(spu_initial.render_state, render_state, sizeof(spu_initial.render_state));
	memcpy(spu_initial.texture_state, texture_state, sizeof(spu_initial.texture_state));
	memcpy(spu_initial.targets, device.targets, sizeof(spu_initial.targets));
	memcpy(&spu_initial.viewport, &device.viewport, sizeof(spu_initial.viewport));
	spu_initial.shader_constant_mode = (uint32_t)device.shader_constant_mode;
	spu_initial.screen_width = (uint32_t)screen_width;
	spu_initial.screen_scale[0] = screen_scale[0];
	spu_initial.screen_scale[1] = screen_scale[1];
	spu_initial.static_begin = (uint32_t)(uintptr_t)static_vertices_begin;
	spu_initial.static_end = (uint32_t)(uintptr_t)static_vertices_end;
	spu_initial.button_style = hud_hires_rsx_style();
	hud_hires_rsx_placeholders((unsigned int (*)[2])spu_initial.placeholders);
	memcpy(spu_initial.constants, device.constants, sizeof(spu_initial.constants));
	memcpy(spu_initial.attributes, device.attributes, sizeof(spu_initial.attributes));
	__sync_synchronize();
	if (!ps3_spu_direct3d_start(setup))
		return FALSE;
	spu.bell = ps3_spu_direct3d_bell();
	spu.echo = ps3_spu_direct3d_echo();
	spu.called = spu.answered = 0;
	spu.stop = spu.stopped = 0;
	if (ps3_host_thread_create(spu_service_thread, NULL, 0x40000, PS3_HOST_PRIORITY_NORMAL - 1, "d3d_service") != 0)
	{
		platform_log("Direct3D: cannot start the Direct3D SPU's service thread");
		ps3_spu_direct3d_stop();
		return FALSE;
	}
	spu.on = TRUE;
	if (!spu_direct3d_test())
	{
		ps3_spu_direct3d_report("its first chunk did not reach the RSX in 200 ms");
		spu.on = FALSE;
		spu.stop = 1;
		ps3_spu_direct3d_stop();
		while (!spu.stopped)
			ps3_host_sleep_microseconds(1000);
		ring.written = ring.published = ring.consumed = 0;
		return FALSE;
	}
	return TRUE;
}

/* a label the SPU's commands write, in a chunk the service thread calls */
static BOOL spu_direct3d_test(void)
{
	volatile uint32_t *label = ps3_gpu_label(SPU_TEST_LABEL);
	uint64_t start = ps3_host_microseconds();

	if (!label)
		return FALSE;
	*label = 0;
	__sync_synchronize();
	record_begin(_record_label, 0, SPU_TEST_LABEL, SPU_TEST_VALUE);
	record_commit();
	direct3d_publish();
	while (*label != SPU_TEST_VALUE && ps3_host_microseconds() - start < 200000)
		ps3_host_sleep_microseconds(200);
	return *label == SPU_TEST_VALUE;
}

/* ps3_timers.h's Direct3D thread timers: the SPU's since the last log */
static void spu_timers_collect(void)
{
	const spu_d3d_statistics *s = &spu_statistics_block;
	int index;

	if (!spu.on)
		return;
	for (index = 0; index < _spu_d3d_timer_count; index++)
	{
		if (index == _spu_d3d_timer_thread_wait || index == _spu_d3d_timer_texture_upload)
			continue;
		ps3_timers[_ps3_timer_thread_busy + index] += s->timers[index] - spu.timers[index];
		ps3_timer_counts[_ps3_timer_thread_busy + index] += s->counts[index] - spu.counts[index];
		spu.timers[index] = s->timers[index];
		spu.counts[index] = s->counts[index];
	}
}

/* ---------- device creation */

Direct3D *WINAPI Direct3DCreate8(UINT sdk_version)
{
	(void)sdk_version;
	return (Direct3D *)1;
}

void WINAPI Direct3D_SetPushBufferSize(DWORD push_buffer_size, DWORD segment_count)
{
	(void)push_buffer_size;
	(void)segment_count;
}

HRESULT WINAPI Direct3D_CreateDevice(UINT adapter, D3DDEVTYPE device_type, void *unused, DWORD behavior_flags,
	D3DPRESENT_PARAMETERS *presentation_parameters, D3DDevice **returned_device)
{
	(void)adapter;
	(void)device_type;
	(void)unused;
	(void)behavior_flags;
	if (!device.created)
	{
		unsigned long width, height;
		int index;

		memset(&device, 0, sizeof(device));
		memset(&front, 0, sizeof(front));
		ui_offset = 0;
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		width = device.presentation.BackBufferWidth ? device.presentation.BackBufferWidth : SCREEN_WIDTH;
		height = device.presentation.BackBufferHeight ? device.presentation.BackBufferHeight : SCREEN_HEIGHT;
		/* room for the widest screen, which WIDESCREEN can switch to (halo_screen_commit) */
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, SCREEN_MAXIMUM_WIDTH, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, SCREEN_MAXIMUM_WIDTH, height);
		d3d8_surface_resize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_resize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
		front.render_target = &device.back_buffer;
		front.depth_stencil = &device.depth_buffer;
		surface_words_read(front.render_target, &device.targets[0]);
		surface_words_read(front.depth_stencil, &device.targets[1]);
		memcpy(front.recorded_targets, device.targets, sizeof(device.targets));
		for (index = 0; index < D3DTS_MAX; index++)
		{
			front.transforms[index]._11 = 1.0f;
			front.transforms[index]._22 = 1.0f;
			front.transforms[index]._33 = 1.0f;
			front.transforms[index]._44 = 1.0f;
		}
		for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
			device.attributes[index][3] = front.attributes[index][3] = front.recorded_attributes[index][3] = 1.0f;
		for (index = 0; index < 256; index++)
			byte_floats[index] = index / 255.0f;
		platform_log("Direct3D: vertex and index conversions on %s",
			ps3_spu_vertices_start(vertices_task_run) ? "an SPU" : "the PPE");
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MaxZ = 1.0f;
		front.next_vertex_shader_id = 1;
		D3D__RenderState[D3DRS_ZENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZWRITEENABLE] = TRUE;
		D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		D3D__RenderState[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
		D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
		D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
		D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
		D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
		D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
		D3D__RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
		D3D__RenderState[D3DRS_STENCILMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILWRITEMASK] = 0xff;
		D3D__RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
		D3D__RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
		for (index = 0; index < D3DTSS_MAXSTAGES; index++)
		{
			D3D__TextureState[index][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
			D3D__TextureState[index][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MINFILTER] = D3DTEXF_POINT;
			D3D__TextureState[index][D3DTSS_MAXANISOTROPY] = 1;
		}
		memcpy(render_state, D3D__RenderState, sizeof(render_state));
		memcpy(texture_state, D3D__TextureState, sizeof(texture_state));
		memcpy(front.recorded_render_state, D3D__RenderState, sizeof(front.recorded_render_state));
		memcpy(front.recorded_texture_state, D3D__TextureState, sizeof(front.recorded_texture_state));
		device.constants_dirty_first = _rsx_vp_constant_count;
		constants_initialize();
		viewport_update_constants();
		vertical_blank_start();
		device.gpu_ready = ps3_gpu_initialize() == 0;
		/* (its first read sets the screen up: before the Direct3D thread can make one) */
		halo_screen_width();
		if (device.gpu_ready && config_boolean("display.half_resolution_particles"))
			particles_targets();
		if (device.gpu_ready && config_boolean("display.dynamic_resolution"))
			scaled_target();
		device.created = TRUE;
		ps3_timers_shaders_log = shaders_log;
		ps3_timers_direct3d_collect = spu_timers_collect;
		if (device.gpu_ready)
			direct3d_start();
		platform_log("Direct3D: %lux%lu on the RSX%s, back buffer at 0x%08lx; %s", width, height,
			device.gpu_ready ? "" : " (not available: nothing is drawn)", (unsigned long)device.back_buffer.Data,
			ring.running ? "the Direct3D thread draws" : "the game's thread draws");
	}
	/* the game reaches the device only through the D3DDevice_ functions */
	*returned_device = (D3DDevice *)&device;
	return S_OK;
}

ULONG WINAPI D3DDevice_Release(void)
{
	return 1;
}

void WINAPI D3DDevice_GetDeviceCaps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxVolumeExtent = 512;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xfffff;
	caps->MaxVertexIndex = 0xffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 192;
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxPixelShaderValue = 1.0f;
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	(void)back_buffer;
	(void)type;
	/* like Direct3D, the caller gets a reference it must release */
	device.back_buffer.Common++;
	*result = &device.back_buffer;
}

HRESULT WINAPI D3DDevice_GetDepthStencilSurface(D3DSurface **result)
{
	*result = front.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

/* (the Direct3D thread) the targets' words; reset: a SetRenderTarget's */
static void render_target_apply(const struct surface_words *targets, BOOL reset)
{
	memcpy(device.targets, targets, sizeof(device.targets));
	if (!reset)
		return;
	/* like Direct3D, the viewport becomes the whole new target */
	if (device.targets[0].valid)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.targets[0].format, device.targets[0].size, &description);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = description.width;
		device.viewport.Height = description.height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	struct surface_words targets[2];

	if (render_target)
		front.render_target = render_target;
	front.depth_stencil = depth_stencil;
	surface_words_read(front.render_target, &targets[0]);
	surface_words_read(front.depth_stencil, &targets[1]);
	memcpy(front.recorded_targets, targets, sizeof(targets));
	memcpy(record_begin(_record_render_target, sizeof(targets), 0, 1), targets, sizeof(targets));
	record_commit();
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	memcpy(record_begin(_record_viewport, sizeof(*viewport), 0, 0), viewport, sizeof(*viewport));
	record_commit();
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		front.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = front.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	record_begin(_record_constant_mode, 0, (uint32_t)mode, 0);
	record_commit();
}

/* ---------- synchronisation: the Direct3D thread keeps the order of the records, and the RSX of its commands */

BOOL WINAPI D3DDevice_IsBusy(void)
{
	if (ps3_host_thread_id() == ring.producer)
		direct3d_publish();
	return ring.running && !ring.dead && ring_consumed() != ring.written;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
	if (ps3_host_thread_id() == ring.producer)
		direct3d_publish();
}

void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE type, D3DCALLBACK callback, DWORD context)
{
	(void)type;
	/* the "GPU" reaches the callback at once */
	if (callback)
		callback(context);
}

/* ---------- visibility tests: not counted yet, so nothing is visible */

void WINAPI D3DDevice_BeginVisibilityTest(void)
{
	front.visibility_test_active = TRUE;
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	(void)index;
	front.visibility_test_active = FALSE;
	return S_OK;
}

HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD index, UINT *result, ULONGLONG *time_stamp)
{
	(void)index;
	if (result)
		*result = 0;
	if (time_stamp)
		*time_stamp = 0;
	return S_OK;
}

/* ---------- render and texture stage state */

void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD method, DWORD value)
{
	/* callers also store the value in D3D__RenderState themselves */
	(void)method;
	(void)value;
}

void D3DFASTCALL D3DDevice_SetRenderState_Deferred(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value);

void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE state, DWORD value)
{
	if (state == D3DRS_ZBIAS)
		D3DDevice_SetRenderState_ZBias(value);
	else if ((unsigned long)state < D3DRS_MAX)
		D3D__RenderState[state] = value;
}

/* as the Xbox's D3D8: a polygon offset of -bias depth units and -bias/4 of the depth slope, for every
fill mode (without the slope, decals fight with the surface under them seen at an angle) */
void WINAPI D3DDevice_SetRenderState_ZBias(DWORD value)
{
	float offset = -(float)value;
	float slope = offset * 0.25f;
	DWORD enable = value != 0;

	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, sizeof(slope));
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &offset, sizeof(offset));
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = enable;
	D3D__RenderState[D3DRS_ZBIAS] = value;
}

#define COMPLEX_RENDER_STATE(name, state) \
	void WINAPI D3DDevice_SetRenderState_##name(DWORD value) { D3D__RenderState[state] = value; }

COMPLEX_RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES)
COMPLEX_RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
COMPLEX_RENDER_STATE(FogColor, D3DRS_FOGCOLOR)
COMPLEX_RENDER_STATE(FillMode, D3DRS_FILLMODE)
COMPLEX_RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE)
COMPLEX_RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
COMPLEX_RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
COMPLEX_RENDER_STATE(ZEnable, D3DRS_ZENABLE)
COMPLEX_RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE)
COMPLEX_RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
COMPLEX_RENDER_STATE(FrontFace, D3DRS_FRONTFACE)
COMPLEX_RENDER_STATE(CullMode, D3DRS_CULLMODE)
COMPLEX_RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR)
COMPLEX_RENDER_STATE(LogicOp, D3DRS_LOGICOP)
COMPLEX_RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
COMPLEX_RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
COMPLEX_RENDER_STATE(MultiSampleType, D3DRS_MULTISAMPLETYPE)
COMPLEX_RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
COMPLEX_RENDER_STATE(LineWidth, D3DRS_LINEWIDTH)
COMPLEX_RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
COMPLEX_RENDER_STATE(YuvEnable, D3DRS_YUVENABLE)
COMPLEX_RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
COMPLEX_RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE)
COMPLEX_RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
COMPLEX_RENDER_STATE(RopZRead, D3DRS_ROPZREAD)
COMPLEX_RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)

void D3DFASTCALL D3DDevice_SetTextureState_Deferred(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_TEXCOORDINDEX] = value;
}

void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_BORDERCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD stage, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES)
		D3D__TextureState[stage][D3DTSS_COLORKEYCOLOR] = value;
}

void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage < D3DTSS_MAXSTAGES && (unsigned long)type < D3DTSS_MAX)
		D3D__TextureState[stage][type] = value;
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < TEXTURE_STAGES)
		front.textures[stage] = texture;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < TEXTURE_STAGES)
		front.palettes[stage] = palette;
}

void WINAPI D3DDevice_SetPixelShaderProgram(D3DPIXELSHADERDEF *definition)
{
	/* the definition's members are the pixel shader render states */
	if (!definition)
		return;
	memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], definition->PSAlphaInputs, sizeof(definition->PSAlphaInputs));
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = definition->PSFinalCombinerInputsABCD;
	D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = definition->PSFinalCombinerInputsEFG;
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], definition->PSConstant0, sizeof(definition->PSConstant0));
	memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], definition->PSConstant1, sizeof(definition->PSConstant1));
	memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], definition->PSAlphaOutputs, sizeof(definition->PSAlphaOutputs));
	memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], definition->PSRGBInputs, sizeof(definition->PSRGBInputs));
	D3D__RenderState[D3DRS_PSCOMPAREMODE] = definition->PSCompareMode;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = definition->PSFinalCombinerConstant0;
	D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = definition->PSFinalCombinerConstant1;
	memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], definition->PSRGBOutputs, sizeof(definition->PSRGBOutputs));
	D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = definition->PSCombinerCount;
	D3D__RenderState[D3DRS_PSTEXTUREMODES] = definition->PSTextureModes;
	D3D__RenderState[D3DRS_PSDOTMAPPING] = definition->PSDotMapping;
	D3D__RenderState[D3DRS_PSINPUTTEXTURE] = definition->PSInputTexture;
}

/* ---------- presentation */

unsigned long platform_d3d_frame_count(void)
{
	return front.frame;
}

/* (the Direct3D thread) a frame's end */
static void present_run(void)
{
	if (device.gpu_ready)
	{
		struct surface_words words;
		struct render_target *back_buffer;

		/* (a view the game left smaller is scaled up for the frame) */
		scale_end();
		surface_words_read(&device.back_buffer, &words);
		back_buffer = render_target_get(&words, FALSE);
		if (back_buffer)
			ps3_gpu_present(back_buffer->offset, back_buffer->pitch, (uint32_t)back_buffer->width,
				(uint32_t)back_buffer->height);
		else
			ps3_gpu_present(0, 0, 0, 0);
		/* a new frame: its programs and constants are sent again */
		constants_mark(0, _rsx_vp_constant_count - 1);
	}
	textures_begin_frame();
	device.frame++;
	if (device.frame == 1 || device.frame % STATISTICS_FRAMES == 0)
	{
		unsigned long texture_count, texture_bytes, unused_bytes;

		textures_measure(&texture_count, &texture_bytes, &unused_bytes);
		platform_log("Direct3D: frame %lu: %lu draws; skipped %lu (program), %lu (target), %lu (scratch), "
			"%lu (viewport); %lu texture uploads, %lu KB (%lu KB by the SPU; %lu KB of checks hashed by it); %lu pixel "
			"shaders (%lu translated at a draw); %lu textures in RSX memory, %lu KB (%lu KB not drawn for %lu frames)",
			device.frame, statistics.draws, statistics.skipped_program, statistics.skipped_target,
			statistics.skipped_scratch, statistics.skipped_scissor, statistics.texture_uploads,
			statistics.texture_bytes / 1024, statistics.texture_spu_bytes / 1024, statistics.texture_hashed_spu / 1024,
			fragment_program_count, fragment_translated_count, texture_count,
			texture_bytes / 1024, unused_bytes / 1024, (unsigned long)STATISTICS_FRAMES);
		platform_log("Direct3D: frame %lu: vertex elements converted: %lu into the cache, %lu at draws; vertex "
			"cache %lu arrays, %lu of %lu KB, %lu refused", device.frame, statistics.cached_elements,
			statistics.converted_elements, vertex_cache_arrays, vertex_arena.used / 1024,
			(unsigned long)(VERTEX_ARENA_BYTES / 1024), statistics.cache_refused);
		{
			struct ps3_spu_vertices_statistics spu;
			unsigned long frames = device.frame == 1 ? 1 : STATISTICS_FRAMES, type;
			char types[512];
			size_t used = 0;

			ps3_spu_vertices_statistics(&spu);
			platform_log("Direct3D: frame %lu: the conversions' SPU %s: %lu tasks, %lu elements, %lu indices, %lu us a "
				"frame (its waits for input %lu, for output %lu); the waits that found tasks not done: %lu, %lu us a "
				"frame", device.frame, spu.up ? "on" : "off", (unsigned long)spu.tasks, (unsigned long)spu.elements,
				(unsigned long)spu.indices, (unsigned long)(spu.busy_microseconds / frames),
				(unsigned long)(spu.input_wait_microseconds / frames), (unsigned long)(spu.output_wait_microseconds / frames),
				(unsigned long)spu.waits, (unsigned long)(spu.wait_microseconds / frames));
			for (type = 0; type < 128 && used < sizeof(types) - 32; type++)
			{
				if (statistics.type_elements[type])
					used += (size_t)snprintf(types + used, sizeof(types) - used, "%s0x%02lx %lu", used ? ", " : "", type,
						statistics.type_elements[type]);
			}
			platform_log("Direct3D: frame %lu: the elements converted by type: %s", device.frame, used ? types : "none");
		}
		memset(&statistics, 0, sizeof(statistics));
	}
}

/* the Xbox keeps at most two frames queued behind its 60 Hz display; the Direct3D thread draws one frame behind the
game's thread */
void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	uint64_t span;

	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	{
		struct surface_words *words = record_begin(_record_present, sizeof(*words), 0, 0);

		if (words)
		{
			surface_words_read(&device.back_buffer, words);
			record_commit();
		}
	}
	direct3d_publish();
	front.frame++;
	direct3d_wait(front.previous_present);
	front.previous_present = ring.written;
	if (ps3_host_exit_requested())
	{
		direct3d_stop();
		ps3_host_exit(0);
	}
	vertical_blank_start();
	if (!vertical_blank_started)
		return;
	span = ps3_timer_now();
	ps3_host_lock_enter(vertical_blank_lock);
	/* with interpolation, frames come at the display's rate, paced by the flips (ps3_video.c), as d3d8_gl.c's */
	if (halo_interpolation_enabled())
	{
		flip_count++;
	}
	else
	{
		while (pending_flips >= 2)
			ps3_host_condition_wait(vertical_blank_condition, 0);
		pending_flips++;
	}
	ps3_host_lock_leave(vertical_blank_lock);
	ps3_timer_add(_ps3_timer_vblank_wait, span);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}
