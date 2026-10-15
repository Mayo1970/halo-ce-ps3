/* The Direct3D 8 device on the RSX: port/linux/src/d3d8_gl.c's device, with vertex programs and combiners
as runtime microcode (ps3_rsx_*.c) and the RSX through ps3_gpu.c. Resources: d3d8_resources.c. */

#include <string.h>
#include <stdlib.h>

#include "halo_ui_pointer.h"
#include "ps3_gpu.h"
#include "ps3_timers.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
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

/* ---------- state the XDK header's inline functions read and write */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
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

static struct
{
	BOOL created;
	BOOL gpu_ready;
	D3DPRESENT_PARAMETERS presentation;
	D3DSurface back_buffer;
	D3DSurface depth_buffer;
	D3DSurface *render_target;
	D3DSurface *depth_stencil;
	D3DVIEWPORT8 viewport;
	D3DMATRIX transforms[D3DTS_MAX];
	D3DBaseTexture *textures[TEXTURE_STAGES];
	D3DPalette *palettes[TEXTURE_STAGES];
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

	struct
	{
		DWORD data;
		UINT stride;
	} streams[16];
	/* SetIndices' base vertex, added to every index of an indexed draw */
	UINT base_vertex_index;
	/* the vertex count of the map vertex buffer that the next indexed draw reads (d3d8_draw_vertex_count) */
	unsigned long draw_vertex_count;

	/* the current value of each input register (SetVertexData) */
	float attributes[VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long immediate_capacity;

	/* the bound targets' size (bind_targets) */
	unsigned long target_width;
	unsigned long target_height;

	BOOL visibility_test_active;
	unsigned long frame;
	unsigned long next_vertex_shader_id;
} device;

static struct
{
	unsigned long draws, skipped_program, skipped_target, skipped_scratch, skipped_scissor;
	unsigned long texture_uploads, texture_bytes;
	/* vertex elements converted into the vertex cache and at draws; copies the cache had no room for */
	unsigned long cached_elements, converted_elements, cache_refused;
} statistics;

static float dword_to_float(DWORD value)
{
	union { DWORD d; float f; } u;

	u.d = value;
	return u.f;
}

static void color_to_vec4(D3DCOLOR color, float *out)
{
	out[0] = ((color >> 16) & 0xff) / 255.0f;
	out[1] = ((color >> 8) & 0xff) / 255.0f;
	out[2] = (color & 0xff) / 255.0f;
	out[3] = ((color >> 24) & 0xff) / 255.0f;
}

/* ---------- the screen: the Xbox's 640x480, 4:3 in the middle of the display */

long halo_screen_width(void)
{
	return SCREEN_WIDTH;
}

void halo_screen_ui_offset(unsigned char centered)
{
	(void)centered;
}

long halo_screen_commit(void)
{
	return SCREEN_WIDTH;
}

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)menus_active;
	(void)pointer;
	return 0;
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
	if (device.depth_stencil)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.depth_stencil->Format, device.depth_stencil->Size, &description);
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

/* the NV2A's screen space to the RSX's clip space for the bound target, its pixel centres at
integers (Direct3D's) moved to halves (the RSX's); ps3_rsx_vp.c's epilogue reads them */
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
	values[1][0] = 1.0f / width - 1.0f;
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
	unsigned long width;
	unsigned long height;
	BOOL depth;
	void *memory;
	uint32_t offset;
	uint32_t pitch;
	unsigned long last_rendered;
};

#define RENDER_TARGET_BUCKET_COUNT 256

static struct render_target *render_target_buckets[RENDER_TARGET_BUCKET_COUNT];

static struct render_target **render_target_bucket(DWORD data)
{
	return &render_target_buckets[((data >> 12) ^ (data >> 20)) % RENDER_TARGET_BUCKET_COUNT];
}

static BOOL format_is_depth(DWORD format)
{
	return format == D3DFMT_D24S8 || format == D3DFMT_F24S8 || format == D3DFMT_D16 || format == D3DFMT_F16 ||
		format == D3DFMT_LIN_D24S8 || format == D3DFMT_LIN_F24S8 || format == D3DFMT_LIN_D16 || format == D3DFMT_LIN_F16;
}

/* data 0: the device's own targets, for a draw without a colour or depth surface */
static struct render_target *render_target_lookup(DWORD data, unsigned long width, unsigned long height, BOOL depth)
{
	struct render_target *target;
	uint32_t pitch = (uint32_t)((width * 4 + 63) & ~63UL);

	for (target = *render_target_bucket(data); target; target = target->next)
	{
		if (target->data == data && target->width == width && target->height == height && target->depth == depth)
			return target;
	}
	target = calloc(1, sizeof(*target));
	if (!target)
		return NULL;
	target->memory = ps3_gpu_memory_allocate(pitch * (uint32_t)height, 128, _ps3_gpu_memory_target, &target->offset);
	if (!target->memory)
	{
		platform_log("Direct3D: no RSX memory for a %lux%lu target", width, height);
		free(target);
		return NULL;
	}
	target->data = data;
	target->width = width;
	target->height = height;
	target->depth = depth;
	target->pitch = pitch;
	target->next = *render_target_bucket(data);
	*render_target_bucket(data) = target;
	return target;
}

static struct render_target *render_target_get(const D3DSurface *surface, BOOL depth)
{
	struct xgpu_texture_description description;

	if (!surface || !surface->Data)
		return NULL;
	xgpu_texture_describe(surface->Format, surface->Size, &description);
	if (format_is_depth(description.format) != depth || !description.width || !description.height)
		return NULL;
	return render_target_lookup(surface->Data, description.width, description.height, depth);
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

/* the RSX surface for the current targets; FALSE if there is nothing to draw into */
static BOOL bind_targets(BOOL *has_depth)
{
	struct render_target *color = render_target_get(device.render_target, FALSE);
	struct render_target *depth = render_target_get(device.depth_stencil, TRUE);
	struct ps3_gpu_surface surface;

	if (!color && !depth)
		return FALSE;
	if (!color)
		color = render_target_lookup(0, depth->width, depth->height, FALSE);
	*has_depth = depth && color && depth->width >= color->width && depth->height >= color->height;
	if (!*has_depth && color)
		depth = render_target_lookup(0, color->width, color->height, TRUE);
	if (!color || !depth)
		return FALSE;
	color->last_rendered = device.frame + 1;
	surface.color_offset = color->offset;
	surface.color_pitch = color->pitch;
	surface.depth_offset = depth->offset;
	surface.depth_pitch = depth->pitch;
	surface.width = (uint16_t)color->width;
	surface.height = (uint16_t)color->height;
	ps3_gpu_set_surface(&surface);
	device.target_width = color->width;
	device.target_height = color->height;
	return TRUE;
}

/* ---------- textures: RSX copies, converted again when their texels change */

struct texture_entry
{
	struct texture_entry *next;
	DWORD data, format_word, size_word, palette_hash;
	__uint64 content_hash;
	unsigned long checked_frame;
	unsigned long last_used_frame;
	/* the mark put in the header's Lock when these texels were last checked (texture_get) */
	DWORD stamp;
	struct xgpu_texture_description description;
	void *memory;
	unsigned long bytes;
	struct ps3_gpu_texture gpu;
};

#define TEXTURE_BUCKET_COUNT 1024
#define TEXTURE_IDLE_FRAMES 1800
/* xbox_texture_cache.c's XBOX_TEXTURE_CACHE_SIZE */
#define TEXTURE_CACHE_BYTES 0x1600000UL

void *physical_memory_get_texture_cache_base_address(void);

static struct texture_entry *texture_buckets[TEXTURE_BUCKET_COUNT];
static unsigned long texture_frame = 1;
static DWORD texture_stamp;

static unsigned long texture_bucket_index(DWORD data, DWORD format_word, DWORD size_word)
{
	return ((data >> 7) ^ (format_word * 2654435761UL) ^ size_word) % TEXTURE_BUCKET_COUNT;
}

/* four lanes, so the multiplies overlap on the in-order PPE */
static __uint64 hash_bytes(const unsigned char *bytes, unsigned long size)
{
	__uint64 lanes[4] = { 0xcbf29ce484222325ULL, 0x84222325cbf29ce4ULL, 0x9e3779b97f4a7c15ULL, 0x7f4a7c159e3779b9ULL };
	unsigned long index, words = size / 8;
	const __uint64 *data = (const __uint64 *)bytes;

	for (index = 0; index + 4 <= words; index += 4)
	{
		lanes[0] = (lanes[0] ^ data[index]) * 0x100000001b3ULL;
		lanes[1] = (lanes[1] ^ data[index + 1]) * 0x100000001b3ULL;
		lanes[2] = (lanes[2] ^ data[index + 2]) * 0x100000001b3ULL;
		lanes[3] = (lanes[3] ^ data[index + 3]) * 0x100000001b3ULL;
	}
	for (index = index * 8; index < size; index++)
		lanes[0] = (lanes[0] ^ bytes[index]) * 0x100000001b3ULL;
	return lanes[0] ^ (lanes[1] * 3) ^ (lanes[2] * 5) ^ (lanes[3] * 7);
}

static unsigned char rsx_compressed_format(DWORD format)
{
	switch (format)
	{
	case 0x0c: return GCM_TEXTURE_FORMAT_DXT1;
	case 0x0e: return GCM_TEXTURE_FORMAT_DXT23;
	default: return GCM_TEXTURE_FORMAT_DXT45;
	}
}

static void texture_upload(struct texture_entry *entry, const unsigned char *source, const DWORD *palette)
{
	const struct xgpu_texture_description *description = &entry->description;
	unsigned long faces = description->cube_map ? 6 : 1;
	unsigned long levels = description->linear ? 1 : description->levels;
	unsigned long source_face = xgpu_texture_face_size(description);
	unsigned long face_bytes = 0, face, level, offset;
	unsigned char *memory;
	uint32_t memory_offset;

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
		for (level = 0, offset = 0; level < levels; level++)
		{
			xgpu_texture_rsx_convert_level(description, level,
				source + face * source_face + xgpu_texture_level_offset(description, level), palette,
				memory + face * face_bytes + offset);
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
	if (description->compressed)
	{
		/* DXT blocks lie in order, swizzled or not, as on the Xbox */
		entry->gpu.format = rsx_compressed_format(description->format);
		entry->gpu.pitch = (uint32_t)xgpu_texture_rsx_pitch(description, 0);
	}
	else if (description->linear)
	{
		/* Xbox linear textures take texel coordinates */
		entry->gpu.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN | GCM_TEXTURE_FORMAT_UNRM;
		entry->gpu.pitch = (uint32_t)xgpu_texture_rsx_pitch(description, 0);
	}
	else
	{
		entry->gpu.format = GCM_TEXTURE_FORMAT_A8R8G8B8;
		entry->gpu.pitch = (uint32_t)(description->width * 4);
	}
}

/* the texture cache's memory, which only the cache's disk reads fill */
static BOOL texture_cache_holds(const unsigned char *source, unsigned long size)
{
	const unsigned char *base = physical_memory_get_texture_cache_base_address();

	return base && source >= base && source + size <= base + TEXTURE_CACHE_BYTES;
}

static struct texture_entry *texture_get(D3DBaseTexture *texture, const DWORD *palette, DWORD palette_hash)
{
	DWORD data = texture->Data, format_word = texture->Format, size_word = texture->Size;
	struct texture_entry **bucket = &texture_buckets[texture_bucket_index(data, format_word, size_word)];
	struct texture_entry *entry;
	const unsigned char *source;
	unsigned long size;
	__uint64 hash;
	uint64_t span;
	BOOL cached;

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
	if (entry->checked_frame == texture_frame)
		return entry->memory ? entry : NULL;
	/* once a frame: the game writes texels with the CPU (the text cache) */
	entry->checked_frame = texture_frame;
	source = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
	size = xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);
	if (!size || !platform_is_contiguous(source) || !platform_is_contiguous(source + size - 1))
		return entry->memory ? entry : NULL;
	/* the cache sets a header up again, Lock 0, for each texture it loads; a CPU writer of texels there
	(Bink, once it decodes) must put Lock to 0 after each write */
	cached = texture_cache_holds(source, size);
	if (cached && entry->memory && entry->stamp && texture->Lock == entry->stamp)
		return entry;
	span = ps3_timer_now();
	hash = hash_bytes(source, size) ^ palette_hash;
	span = ps3_timer_add(_ps3_timer_texture_hash, span);
	if (!entry->memory || hash != entry->content_hash)
	{
		texture_upload(entry, source, palette);
		entry->content_hash = hash;
		ps3_timer_add(_ps3_timer_texture_upload, span);
	}
	if (cached && entry->memory)
	{
		if (!++texture_stamp)
			texture_stamp = 1;
		entry->stamp = texture_stamp;
		texture->Lock = texture_stamp;
	}
	return entry->memory ? entry : NULL;
}

/* drops the textures not used for a while */
static void textures_begin_frame(void)
{
	unsigned long index;

	texture_frame++;
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
	return (D3D__RenderState[D3DRS_PSTEXTUREMODES] >> (5 * stage)) & 0x1f;
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
	DWORD *state = D3D__TextureState[stage];
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

/* the palette's 256 entries as ARGB (palettes keep the Xbox's order: trap 27) */
static DWORD palette_load(int stage, DWORD palette[256])
{
	D3DPalette *source = device.palettes[stage];
	const DWORD *entries;
	DWORD hash = 2166136261UL;
	int index;

	if (!source || !source->Data)
		return 0;
	entries = (const DWORD *)PLATFORM_PHYSICAL_TO_VIRTUAL(source->Data);
	for (index = 0; index < 256; index++)
	{
		palette[index] = __builtin_bswap32(entries[index]);
		hash = (hash ^ palette[index]) * 16777619UL;
	}
	return hash ? hash : 1;
}

static void bind_textures(struct rsx_fragment_key *key)
{
	int stage;

	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		D3DBaseTexture *texture = device.textures[stage];
		unsigned long mode = stage_texture_mode(stage);
		struct render_target *target;
		struct ps3_gpu_texture bound;

		key->sampler_type[stage] = mode == 0x11 ? _rsx_sampler_2d : _rsx_sampler_none;
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
		{
			ps3_gpu_set_texture(stage, NULL);
			continue;
		}
		memset(&bound, 0, sizeof(bound));
		target = render_target_find(texture->Data);
		if (target)
		{
			struct xgpu_texture_description description;

			/* a rendered target: its surface, linear, its one level */
			xgpu_texture_describe(texture->Format, texture->Size, &description);
			bound.offset = target->offset;
			bound.pitch = target->pitch;
			bound.width = (uint16_t)target->width;
			bound.height = (uint16_t)target->height;
			bound.depth = 1;
			bound.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN |
				(description.linear ? GCM_TEXTURE_FORMAT_UNRM : 0);
			bound.levels = 1;
			bound.dimension = GCM_TEXTURE_DIMS_2D;
		}
		else
		{
			DWORD palette[256], palette_hash = 0;
			struct texture_entry *entry;

			if (((texture->Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b)
				palette_hash = palette_load(stage, palette);
			entry = texture_get(texture, palette_hash ? palette : NULL, palette_hash);
			if (!entry)
			{
				ps3_gpu_set_texture(stage, NULL);
				continue;
			}
			bound = entry->gpu;
		}
		sampler_fill(stage, &bound);
		ps3_gpu_set_texture(stage, &bound);
		key->sampler_type[stage] = bound.cube ? _rsx_sampler_cube :
			bound.dimension == GCM_TEXTURE_DIMS_3D ? _rsx_sampler_3d : _rsx_sampler_2d;
	}
}

/* ---------- fragment programs, cached by their key */

struct fragment_entry
{
	struct fragment_entry *next;
	DWORD hash;
	struct rsx_fragment_key key;
	struct rsx_fragment_program program;
	BOOL translated;
};

#define FRAGMENT_BUCKETS 1024

static struct fragment_entry *fragment_buckets[FRAGMENT_BUCKETS];
static unsigned long fragment_program_count;

typedef char fragment_key_size_check[sizeof(struct rsx_fragment_key) % 4 == 0 ? 1 : -1];

static DWORD hash_words(const void *data, unsigned long size)
{
	const DWORD *words = data;
	DWORD hash = 2166136261UL;

	for (size /= 4; size; size--)
		hash = (hash ^ *words++) * 16777619UL;
	return hash;
}

static const struct rsx_fragment_program *fragment_program_get(const struct rsx_fragment_key *key)
{
	/* consecutive draws mostly use the same pixel shader */
	static struct fragment_entry *last;
	struct fragment_entry **bucket, *entry;
	DWORD hash;

	if (last && !memcmp(&last->key, key, sizeof(*key)))
		return last->translated ? &last->program : NULL;
	hash = hash_words(key, sizeof(*key));
	bucket = &fragment_buckets[hash % FRAGMENT_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->key, key, sizeof(*key)))
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->hash = hash;
		entry->key = *key;
		entry->translated = rsx_fragment_program_translate(key, &entry->program) == 0;
		fragment_program_count++;
		if (!entry->translated)
			platform_log("Direct3D: cannot translate a pixel shader (%lu combiners, texture modes %05lx)",
				(unsigned long)(key->combiner_count & 0xff), (unsigned long)key->texture_modes);
		else if (fragment_program_count <= 8)
			platform_log("Direct3D: pixel shader %lu: %lu combiners, texture modes %05lx, %lu words, %lu uniforms",
				fragment_program_count, (unsigned long)(key->combiner_count & 0xff), (unsigned long)key->texture_modes,
				(unsigned long)entry->program.word_count, (unsigned long)entry->program.uniform_count);
		entry->next = *bucket;
		*bucket = entry;
	}
	last = entry;
	return entry->translated ? &entry->program : NULL;
}

static void fragment_key_build(struct rsx_fragment_key *key)
{
	DWORD *rs = D3D__RenderState;
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
		key->alpha_kill[index] = D3D__TextureState[index][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key->color_sign[index] = (unsigned char)((D3D__TextureState[index][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key->fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key->fog_table_mode = (unsigned char)rs[D3DRS_FOGTABLEMODE];
}

/* the per-draw constants (ps3_rsx_program.h's _rsx_uniform_*) */
static void fragment_uniforms_build(float uniforms[_rsx_uniform_count][4])
{
	DWORD *rs = D3D__RenderState;
	int stage;

	for (stage = 0; stage < 8; stage++)
	{
		color_to_vec4(rs[D3DRS_PSCONSTANT0_0 + stage], uniforms[_rsx_uniform_c0 + stage]);
		color_to_vec4(rs[D3DRS_PSCONSTANT1_0 + stage], uniforms[_rsx_uniform_c1 + stage]);
	}
	color_to_vec4(rs[D3DRS_PSFINALCOMBINERCONSTANT0], uniforms[_rsx_uniform_final_c0]);
	color_to_vec4(rs[D3DRS_PSFINALCOMBINERCONSTANT1], uniforms[_rsx_uniform_final_c1]);
	color_to_vec4(rs[D3DRS_FOGCOLOR], uniforms[_rsx_uniform_fog_color]);
	uniforms[_rsx_uniform_fog][0] = dword_to_float(rs[D3DRS_FOGSTART]);
	uniforms[_rsx_uniform_fog][1] = dword_to_float(rs[D3DRS_FOGEND]);
	uniforms[_rsx_uniform_fog][2] = dword_to_float(rs[D3DRS_FOGDENSITY]);
	uniforms[_rsx_uniform_fog][3] = 0.0f;
	for (stage = 0; stage < TEXTURE_STAGES; stage++)
	{
		DWORD *state = D3D__TextureState[stage];

		uniforms[_rsx_uniform_bump_matrix + stage][0] = dword_to_float(state[D3DTSS_BUMPENVMAT00]);
		uniforms[_rsx_uniform_bump_matrix + stage][1] = dword_to_float(state[D3DTSS_BUMPENVMAT01]);
		uniforms[_rsx_uniform_bump_matrix + stage][2] = dword_to_float(state[D3DTSS_BUMPENVMAT10]);
		uniforms[_rsx_uniform_bump_matrix + stage][3] = dword_to_float(state[D3DTSS_BUMPENVMAT11]);
		uniforms[_rsx_uniform_bump_luminance + stage][0] = dword_to_float(state[D3DTSS_BUMPENVLSCALE]);
		uniforms[_rsx_uniform_bump_luminance + stage][1] = dword_to_float(state[D3DTSS_BUMPENVLOFFSET]);
		uniforms[_rsx_uniform_bump_luminance + stage][2] = 0.0f;
		uniforms[_rsx_uniform_bump_luminance + stage][3] = 0.0f;
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

/* FALSE when the viewport leaves nothing of the target */
static BOOL apply_raster_state(BOOL has_depth)
{
	DWORD *rs = D3D__RenderState;
	DWORD write = rs[D3DRS_COLORWRITEENABLE];
	BOOL depth_test = has_depth && rs[D3DRS_ZENABLE];
	struct ps3_gpu_state state;
	long left, top, right, bottom;

	/* the NV2A's scissor follows the viewport, which keeps split screens apart (d3d8_gl.c) */
	left = (long)device.viewport.X;
	top = (long)device.viewport.Y;
	right = left + (long)device.viewport.Width;
	bottom = top + (long)device.viewport.Height;
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > (long)device.target_width)
		right = (long)device.target_width;
	if (bottom > (long)device.target_height)
		bottom = (long)device.target_height;
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
	state.blend_source = rs[D3DRS_SRCBLEND];
	state.blend_destination = rs[D3DRS_DESTBLEND];
	state.blend_equation = blend_equation(rs[D3DRS_BLENDOP]);
	state.blend_color = rs[D3DRS_BLENDCOLOR];
	if (rs[D3DRS_ALPHATESTENABLE])
		state.alpha_function = rs[D3DRS_ALPHAFUNC] ? rs[D3DRS_ALPHAFUNC] : GCM_NEVER;
	state.alpha_reference = rs[D3DRS_ALPHAREF] & 0xff;
	/* the picture is not mirrored (rows from the top, as ps3gl's), so the RSX's winding is Direct3D's */
	state.front_face = rs[D3DRS_FRONTFACE] == D3DFRONT_CCW ? GCM_FRONTFACE_CCW : GCM_FRONTFACE_CW;
	if (rs[D3DRS_CULLMODE] != D3DCULL_NONE)
		state.cull_face = rs[D3DRS_CULLMODE] == rs[D3DRS_FRONTFACE] ? GCM_CULL_FRONT : GCM_CULL_BACK;
	state.polygon_mode = rs[D3DRS_FILLMODE] ? rs[D3DRS_FILLMODE] : GCM_POLYGON_MODE_FILL;
	/* D3DRS_ZBIAS is expressed in these states (D3DDevice_SetRenderState_ZBias) */
	state.polygon_offset = rs[D3DRS_SOLIDOFFSETENABLE] != 0;
	state.polygon_offset_factor = dword_to_float(rs[D3DRS_POLYGONOFFSETZSLOPESCALE]);
	state.polygon_offset_units = dword_to_float(rs[D3DRS_POLYGONOFFSETZOFFSET]);
	state.scissor_x = (uint16_t)left;
	state.scissor_y = (uint16_t)top;
	state.scissor_width = (uint16_t)(right - left);
	state.scissor_height = (uint16_t)(bottom - top);
	state.depth_minimum = device.viewport.MinZ;
	state.depth_maximum = device.viewport.MaxZ;
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
	object->id = device.next_vertex_shader_id++;
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

void WINAPI D3DDevice_SetVertexShader(DWORD handle)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
	{
		device.vertex_shader = object;
		device.program_address = 0;
		device.program_slots[0] = object;
	}
}

void WINAPI D3DDevice_LoadVertexShader(DWORD handle, DWORD address)
{
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_slots[address] = vertex_shader_from_handle(handle);
}

void WINAPI D3DDevice_SelectVertexShader(DWORD handle, DWORD address)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	if (object)
		device.vertex_shader = object;
	if (address < VERTEX_PROGRAM_SLOTS)
		device.program_address = address;
}

void WINAPI D3DDevice_GetVertexShaderSize(DWORD handle, UINT *size)
{
	struct vertex_shader_object *object = vertex_shader_from_handle(handle);

	*size = object ? object->instruction_count : 0;
}

void WINAPI D3DDevice_SetVertexShaderConstant(INT reg, CONST void *constant_data, DWORD constant_count)
{
	long first = reg + VERTEX_CONSTANT_BIAS;

	if (first < 0 || first >= VERTEX_CONSTANT_COUNT)
		return;
	if (first + (long)constant_count > VERTEX_CONSTANT_COUNT)
		constant_count = VERTEX_CONSTANT_COUNT - first;
	constants_store((unsigned long)first, constant_data, constant_count);
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
		object->translated = object->instructions &&
			rsx_vertex_program_translate(object->instructions, object->instruction_count, &object->program) == 0;
		object->untranslatable = !object->translated;
		if (object->untranslatable)
			platform_log("Direct3D: cannot translate vertex shader %lu (%lu instructions)", object->id,
				object->instruction_count);
		else
			platform_log("Direct3D: vertex shader %lu: %lu NV2A instructions, %lu RSX, inputs %04lx, outputs %05lx",
				object->id, object->instruction_count, (unsigned long)object->program.instruction_count,
				(unsigned long)object->program.input_mask, (unsigned long)object->program.output_mask);
	}
	return object->translated ? &object->program : NULL;
}

/* ---------- draws */

/* the state, programs and textures of a draw; the vertex program's on success */
static const struct rsx_vertex_program *prepare_draw_state(void)
{
	struct vertex_shader_object *object = current_program();
	const struct rsx_vertex_program *vertex_program;
	const struct rsx_fragment_program *fragment_program;
	struct rsx_fragment_key key;
	float uniforms[_rsx_uniform_count][4];
	BOOL has_depth = FALSE;
	uint64_t span;

	if (!device.gpu_ready)
		return NULL;
	if (!object || !device.vertex_shader || !(vertex_program = vertex_program_get(object)))
	{
		statistics.skipped_program++;
		return NULL;
	}
	if (!bind_targets(&has_depth))
	{
		statistics.skipped_target++;
		return NULL;
	}
	if (!apply_raster_state(has_depth))
	{
		statistics.skipped_scissor++;
		return NULL;
	}
	screen_constants_update();

	memset(&key, 0, sizeof(key));
	fragment_key_build(&key);
	span = ps3_timer_now();
	bind_textures(&key);
	ps3_timer_add(_ps3_timer_draw_textures, span);
	fragment_program = fragment_program_get(&key);
	if (!fragment_program)
	{
		statistics.skipped_program++;
		return NULL;
	}
	span = ps3_timer_now();
	fragment_uniforms_build(uniforms);
	if (ps3_gpu_set_fragment_program(fragment_program, (const float (*)[4])uniforms) != 0)
	{
		statistics.skipped_scratch++;
		return NULL;
	}
	ps3_timer_add(_ps3_timer_draw_colours, span);
	ps3_gpu_set_vertex_program(vertex_program);
	constants_upload();
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

static float byte_floats[256];

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
	default:
		break;
	}
	out[0] = x;
	out[1] = y;
	out[2] = z;
	out[3] = w;
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

static struct vertex_cache_entry *vertex_cache_buckets[VERTEX_CACHE_BUCKET_COUNT];
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

/* bytes of the arena, in order; it has no frees: a load empties it */
static float *vertex_arena_allocate(unsigned long bytes, uint32_t *offset)
{
	if (!vertex_arena.memory && !vertex_arena.failed)
	{
		vertex_arena.memory = ps3_gpu_memory_allocate((uint32_t)VERTEX_ARENA_BYTES, 128, _ps3_gpu_memory_vertices,
			&vertex_arena.offset);
		vertex_arena.failed = !vertex_arena.memory;
		platform_log("Direct3D: vertex cache of %lu MB%s", (unsigned long)(VERTEX_ARENA_BYTES >> 20),
			vertex_arena.failed ? ": no RSX memory, vertices are converted at each draw" : "");
	}
	bytes = (bytes + 127) & ~127UL;
	if (!vertex_arena.memory || bytes > VERTEX_ARENA_BYTES - vertex_arena.used)
		return NULL;
	*offset = vertex_arena.offset + (uint32_t)vertex_arena.used;
	vertex_arena.used += bytes;
	return (float *)(vertex_arena.memory + vertex_arena.used - bytes);
}

/* cache_files.c: a map's tag data or a BSP was loaded into [address, address + size) of the tag cache;
every copy goes, as they share one block */
void d3d8_tag_cache_loaded(void const *address, unsigned long size)
{
	const unsigned char *begin = address, *end = begin + size;
	unsigned long index;

	for (index = 0; index < VERTEX_CACHE_BUCKET_COUNT; index++)
	{
		while (vertex_cache_buckets[index])
		{
			struct vertex_cache_entry *entry = vertex_cache_buckets[index];

			vertex_cache_buckets[index] = entry->next;
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
	device.draw_vertex_count = vertex_count;
}

/* the RSX copy of one element of a stream with vertices [0, first + count) converted, room for the buffer's
[0, extent) if known (else 0); NULL outside the tag cache, or with no room left (the draw converts it then) */
static const struct vertex_cache_entry *vertex_cache_get(const unsigned char *source, unsigned long stride,
	unsigned char type, unsigned long bytes, unsigned long first, unsigned long count, unsigned long extent)
{
	struct vertex_cache_entry **bucket, *entry;
	unsigned long needed = first + count, vertex;
	float *out;

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
	/* only vertices no draw has read yet, in one pass into write-combined memory */
	for (vertex = entry->count, out = entry->memory + vertex * 4; vertex < needed; vertex++, out += 4)
		vertex_element_convert(type, source + vertex * stride, out);
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
	struct
	{
		const unsigned char *base;
		unsigned long stride;
		unsigned char type;
		unsigned char reg;
	} inputs[VERTEX_ATTRIBUTE_COUNT];
	unsigned long slot_count = 0, index, slot, vertex, stride;
	DWORD placed = 0;
	uint32_t offset = 0;
	float *out = NULL;

	for (index = 0; index < declaration->element_count && slot_count < 15; index++)
	{
		const struct vertex_element *element = &declaration->elements[index];
		DWORD data = device.streams[element->stream].data;
		const struct vertex_cache_entry *cached;
		const unsigned char *source;

		if (element->reg >= VERTEX_ATTRIBUTE_COUNT || !(program->input_mask & (1u << element->reg)) ||
			(placed & (1u << element->reg)) || !data || element->type == D3DVSDT_NONE)
			continue;
		placed |= 1u << element->reg;
		source = (const unsigned char *)PLATFORM_PHYSICAL_TO_VIRTUAL(data) + element->offset;
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
		inputs[slot_count].reg = element->reg;
		slot_count++;
	}
	stride = slot_count * 16;
	if (slot_count)
	{
		out = ps3_gpu_scratch((uint32_t)(count * stride), 16, &offset);
		if (!out)
			return FALSE;
		for (vertex = 0; vertex < count; vertex++)
		{
			for (slot = 0; slot < slot_count; slot++, out += 4)
				vertex_element_convert(inputs[slot].type, inputs[slot].base + vertex * inputs[slot].stride, out);
		}
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

static uint32_t primitive_type(D3DPRIMITIVETYPE type)
{
	/* D3DPT's values are GCM's; ps3gl draws polygons as fans */
	return type == D3DPT_POLYGON ? GCM_TYPE_TRIANGLE_FAN : (uint32_t)type;
}

void WINAPI D3DDevice_SetStreamSource(UINT stream_number, D3DVertexBuffer *stream_data, UINT stride)
{
	if (stream_number >= 16)
		return;
	device.streams[stream_number].data = stream_data ? stream_data->Data : 0;
	device.streams[stream_number].stride = stride;
}

void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *index_data, UINT base_vertex_index)
{
	device.base_vertex_index = base_vertex_index;
	D3D__IndexData = index_data ? (WORD *)(__uint64)index_data->Data : NULL;
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type_value, UINT start_vertex, UINT vertex_count)
{
	const struct rsx_vertex_program *program;
	uint64_t span;
	BOOL ready;

	if (!vertex_count || !(program = prepare_draw()))
		return;
	span = ps3_timer_now();
	ready = setup_vertices(program, start_vertex, vertex_count, 0);
	ps3_timer_add(_ps3_timer_draw_vertices, span);
	if (!ready)
	{
		statistics.skipped_scratch++;
		return;
	}
	ps3_gpu_draw(primitive_type(primitive_type_value), 0, vertex_count);
	statistics.draws++;
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type_value, UINT vertex_count, CONST WORD *index_data)
{
	const struct rsx_vertex_program *program;
	unsigned long index, minimum = 0xffff, maximum = 0;
	/* the buffer's vertices from its vertex 0, as the cache counts them; for this draw only */
	unsigned long extent = device.draw_vertex_count ? device.base_vertex_index + device.draw_vertex_count : 0;
	uint32_t offset;
	unsigned short *indices;
	uint64_t span;
	BOOL ready;

	device.draw_vertex_count = 0;
	if (!vertex_count || !index_data || !(program = prepare_draw()))
		return;
	span = ps3_timer_now();
	for (index = 0; index < vertex_count; index++)
	{
		if (index_data[index] < minimum)
			minimum = index_data[index];
		if (index_data[index] > maximum)
			maximum = index_data[index];
	}
	span = ps3_timer_add(_ps3_timer_draw_indices, span);
	/* index i is vertex base + i; the converted vertices start at the smallest index */
	ready = setup_vertices(program, device.base_vertex_index + minimum, maximum - minimum + 1, extent);
	span = ps3_timer_add(_ps3_timer_draw_vertices, span);
	if (!ready || !(indices = ps3_gpu_scratch(vertex_count * sizeof(unsigned short), 128, &offset)))
	{
		statistics.skipped_scratch++;
		return;
	}
	for (index = 0; index < vertex_count; index++)
		indices[index] = (unsigned short)(index_data[index] - minimum);
	ps3_timer_add(_ps3_timer_draw_indices, span);
	ps3_gpu_draw_indexed(primitive_type(primitive_type_value), offset, vertex_count);
	statistics.draws++;
}

/* ---------- immediate mode */

void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE primitive_type_value)
{
	device.immediate_active = TRUE;
	device.immediate_type = primitive_type_value;
	device.immediate_count = 0;
}

static void immediate_emit(void)
{
	unsigned long floats = VERTEX_ATTRIBUTE_COUNT * 4;

	if (device.immediate_count == device.immediate_capacity)
	{
		unsigned long capacity = device.immediate_capacity ? device.immediate_capacity * 2 : 256;
		float *vertices = realloc(device.immediate_vertices, capacity * floats * sizeof(float));

		if (!vertices)
			return;
		device.immediate_vertices = vertices;
		device.immediate_capacity = capacity;
	}
	memcpy(device.immediate_vertices + device.immediate_count * floats, device.attributes, floats * sizeof(float));
	device.immediate_count++;
}

void WINAPI D3DDevice_End(void)
{
	const struct rsx_vertex_program *program;
	unsigned long count = device.immediate_count, regs[VERTEX_ATTRIBUTE_COUNT], reg_count = 0, index, vertex;
	uint32_t offset = 0;
	float *out;

	device.immediate_active = FALSE;
	if (!count || !(program = prepare_draw()))
		return;
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
		const float *source = device.immediate_vertices + vertex * VERTEX_ATTRIBUTE_COUNT * 4;

		for (index = 0; index < reg_count; index++, out += 4)
			memcpy(out, source + regs[index] * 4, 4 * sizeof(float));
	}
	for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
		ps3_gpu_set_attribute((uint32_t)index, 0, 0, 0);
	for (index = 0; index < reg_count; index++)
		ps3_gpu_set_attribute((uint32_t)regs[index], offset + (uint32_t)(index * 16), (uint32_t)(reg_count * 16), 4);
	ps3_gpu_draw(primitive_type(device.immediate_type), 0, (uint32_t)count);
	statistics.draws++;
}

static void set_attribute(INT reg, float a, float b, float c, float d)
{
	BOOL emit = FALSE;

	if (reg == D3DVSDE_VERTEX)
	{
		reg = 0;
		emit = TRUE;
	}
	if (reg < 0 || reg >= VERTEX_ATTRIBUTE_COUNT)
		return;
	device.attributes[reg][0] = a;
	device.attributes[reg][1] = b;
	device.attributes[reg][2] = c;
	device.attributes[reg][3] = d;
	/* like the hardware, writing register 0 completes a vertex */
	if (device.immediate_active && (emit || reg == 0))
		immediate_emit();
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

void WINAPI D3DDevice_Clear(DWORD count, CONST D3DRECT *rectangles, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	BOOL has_depth = FALSE;
	DWORD gpu_flags, depth;
	long left, top, right, bottom;
	DWORD index;

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
	/* the NV2A clips a clear to the viewport, which keeps split-screen windows apart */
	for (index = 0; index < (count && rectangles ? count : 1); index++)
	{
		left = (long)device.viewport.X;
		top = (long)device.viewport.Y;
		right = left + (long)device.viewport.Width;
		bottom = top + (long)device.viewport.Height;
		if (count && rectangles)
		{
			if (rectangles[index].x1 > left) left = rectangles[index].x1;
			if (rectangles[index].y1 > top) top = rectangles[index].y1;
			if (rectangles[index].x2 < right) right = rectangles[index].x2;
			if (rectangles[index].y2 < bottom) bottom = rectangles[index].y2;
		}
		if (left < 0) left = 0;
		if (top < 0) top = 0;
		if (right > (long)device.target_width) right = (long)device.target_width;
		if (bottom > (long)device.target_height) bottom = (long)device.target_height;
		if (left < right && top < bottom)
			ps3_gpu_clear(gpu_flags, color, depth, (uint32_t)left, (uint32_t)top, (uint32_t)(right - left),
				(uint32_t)(bottom - top));
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
		if (presentation_parameters)
			device.presentation = *presentation_parameters;
		width = device.presentation.BackBufferWidth ? device.presentation.BackBufferWidth : SCREEN_WIDTH;
		height = device.presentation.BackBufferHeight ? device.presentation.BackBufferHeight : SCREEN_HEIGHT;
		d3d8_surface_initialize(&device.back_buffer, D3DFMT_LIN_A8R8G8B8, width, height);
		d3d8_surface_initialize(&device.depth_buffer, D3DFMT_LIN_D24S8, width, height);
		device.render_target = &device.back_buffer;
		device.depth_stencil = &device.depth_buffer;
		for (index = 0; index < D3DTS_MAX; index++)
		{
			device.transforms[index]._11 = 1.0f;
			device.transforms[index]._22 = 1.0f;
			device.transforms[index]._33 = 1.0f;
			device.transforms[index]._44 = 1.0f;
		}
		for (index = 0; index < VERTEX_ATTRIBUTE_COUNT; index++)
			device.attributes[index][3] = 1.0f;
		for (index = 0; index < 256; index++)
			byte_floats[index] = index / 255.0f;
		device.viewport.Width = width;
		device.viewport.Height = height;
		device.viewport.MaxZ = 1.0f;
		device.next_vertex_shader_id = 1;
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
		device.constants_dirty_first = _rsx_vp_constant_count;
		constants_initialize();
		viewport_update_constants();
		vertical_blank_start();
		device.gpu_ready = ps3_gpu_initialize() == 0;
		device.created = TRUE;
		platform_log("Direct3D: %lux%lu on the RSX%s, back buffer at 0x%08lx", width, height,
			device.gpu_ready ? "" : " (not available: nothing is drawn)", (unsigned long)device.back_buffer.Data);
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
	*result = device.depth_stencil;
	if (!*result)
		return D3DERR_NOTFOUND;
	(*result)->Common++;
	return S_OK;
}

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (render_target)
		device.render_target = render_target;
	device.depth_stencil = depth_stencil;
	/* like Direct3D, the viewport becomes the whole new target */
	if (device.render_target)
	{
		struct xgpu_texture_description description;

		xgpu_texture_describe(device.render_target->Format, device.render_target->Size, &description);
		device.viewport.X = 0;
		device.viewport.Y = 0;
		device.viewport.Width = description.width;
		device.viewport.Height = description.height;
		device.viewport.MinZ = 0.0f;
		device.viewport.MaxZ = 1.0f;
	}
	viewport_update_constants();
}

void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *viewport)
{
	device.viewport = *viewport;
	viewport_update_constants();
}

void WINAPI D3DDevice_SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		device.transforms[state] = *matrix;
}

void WINAPI D3DDevice_GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
	if ((unsigned long)state < D3DTS_MAX)
		*matrix = device.transforms[state];
}

void WINAPI D3DDevice_SetFlickerFilter(DWORD filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL enable) { (void)enable; }

void WINAPI D3DDevice_SetShaderConstantMode(D3DSHADERCONSTANTMODE mode)
{
	device.shader_constant_mode = mode;
	viewport_update_constants();
}

/* ---------- synchronisation: the RSX keeps the order of its commands */

BOOL WINAPI D3DDevice_IsBusy(void)
{
	return FALSE;
}

void WINAPI D3DDevice_KickPushBuffer(void)
{
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
	device.visibility_test_active = TRUE;
}

HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD index)
{
	(void)index;
	device.visibility_test_active = FALSE;
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
		device.textures[stage] = texture;
}

void WINAPI D3DDevice_SetPalette(DWORD stage, D3DPalette *palette)
{
	if (stage < TEXTURE_STAGES)
		device.palettes[stage] = palette;
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
	return device.frame;
}

/* the Xbox keeps at most two frames queued behind its 60 Hz display */
void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	uint64_t span;

	(void)source_rectangle;
	(void)destination_rectangle;
	(void)unused;
	(void)unused2;
	if (device.gpu_ready)
	{
		struct render_target *back_buffer = render_target_get(&device.back_buffer, FALSE);

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
			"%lu (viewport); %lu texture uploads, %lu KB; %lu pixel shaders; %lu textures in RSX memory, %lu KB "
			"(%lu KB not drawn for %lu frames)", device.frame, statistics.draws, statistics.skipped_program,
			statistics.skipped_target, statistics.skipped_scratch, statistics.skipped_scissor,
			statistics.texture_uploads, statistics.texture_bytes / 1024, fragment_program_count, texture_count,
			texture_bytes / 1024, unused_bytes / 1024, (unsigned long)STATISTICS_FRAMES);
		platform_log("Direct3D: frame %lu: vertex elements converted: %lu into the cache, %lu at draws; vertex "
			"cache %lu arrays, %lu of %lu KB, %lu refused", device.frame, statistics.cached_elements,
			statistics.converted_elements, vertex_cache_arrays, vertex_arena.used / 1024,
			(unsigned long)(VERTEX_ARENA_BYTES / 1024), statistics.cache_refused);
		memset(&statistics, 0, sizeof(statistics));
	}
	if (ps3_host_exit_requested())
		ps3_host_exit(0);
	vertical_blank_start();
	if (!vertical_blank_started)
		return;
	span = ps3_timer_now();
	ps3_host_lock_enter(vertical_blank_lock);
	while (pending_flips >= 2)
		ps3_host_condition_wait(vertical_blank_condition, 0);
	pending_flips++;
	ps3_host_lock_leave(vertical_blank_lock);
	ps3_timer_add(_ps3_timer_vblank_wait, span);
}

HRESULT WINAPI D3DDevice_PersistDisplay(void)
{
	return S_OK;
}
