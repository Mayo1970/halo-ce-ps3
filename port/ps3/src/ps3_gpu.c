/* The RSX behind the Direct3D device, with the hardware-proven patterns of ioQuake3-PS3 and xashPS3's
ps3gl. Only the game's thread calls it, once it has taken the RSX from the display thread. */

#include "ps3_gpu.h"
#include "ps3_system.h"
#include "ps3_video.h"

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
} deferred_frees[DEFERRED_FREE_COUNT];
static uint32_t deferred_free_count;

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
	uint32_t fragment_copies;
	uint32_t texture_loads;
} statistics;

/* ---------- memory */

void *ps3_gpu_memory_allocate(uint32_t size, uint32_t alignment, uint32_t *offset)
{
	void *memory = rsxMemalign(alignment < 64 ? 64 : alignment, size);

	if (memory)
		rsxAddressToOffset(memory, offset);
	return memory;
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
			rsxFree(deferred_frees[index].memory);
			deferred_frees[index] = deferred_frees[--deferred_free_count];
		}
		else
		{
			index++;
		}
	}
}

void ps3_gpu_memory_free(void *memory)
{
	if (!memory)
		return;
	if (!ready)
	{
		rsxFree(memory);
		return;
	}
	if (deferred_free_count == DEFERRED_FREE_COUNT)
	{
		finish();
		deferred_frees_run(1);
	}
	deferred_frees[deferred_free_count].memory = memory;
	deferred_frees[deferred_free_count].frame = frame_index;
	deferred_free_count++;
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
	viewport_valid = 0;
	/* the targets before can be sampled next */
	texture_cache_dirty = 1;
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
}

/* ---------- programs */

void ps3_gpu_set_vertex_program(const struct rsx_vertex_program *program)
{
	rsxVertexProgram header;

	if (!ready || program == vertex_program)
		return;
	memset(&header, 0, sizeof(header));
	header.num_regs = (u16)program->register_count;
	header.num_insn = (u16)program->instruction_count;
	header.input_mask = program->input_mask;
	header.output_mask = program->output_mask;
	rsxLoadVertexProgramBlock(context, &header, program->microcode);
	vertex_program = program;
	statistics.vertex_uploads++;
}

void ps3_gpu_set_vertex_constants(uint32_t first, uint32_t count, const float *values)
{
	if (ready && count)
		rsxLoadVertexProgramParameterBlock(context, first, count, values);
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
	description.remap = REMAP_IDENTITY;
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
}

void ps3_gpu_draw_indexed(uint32_t primitive, uint32_t offset, uint32_t count)
{
	if (!ready || !count)
		return;
	draw_begin();
	rsxDrawIndexArray(context, (u8)primitive, offset, count, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
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
	memory = ps3_gpu_memory_allocate(sizeof(words) + 64, 64, offset);
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
	/* 4:3 in the middle of the display */
	picture_width = display_height * 4 <= display_width * 3 ? display_height * 4 / 3 : display_width;
	picture_height = display_height * 4 <= display_width * 3 ? display_height : display_width * 3 / 4;
	left = (float)(display_width - picture_width) * 0.5f;
	top = (float)(display_height - picture_height) * 0.5f;

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
	rsxSetClearColor(context, 0xff000000u);
	rsxClearSurface(context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);

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
		start = ps3_time_seconds();
		while (!frame_finished(frame_index - 2))
		{
			if (ps3_time_seconds() - start > FENCE_TIMEOUT_SECONDS)
			{
				if (fence_timeouts++ < 10)
					ps3_log_printf("[gpu] frame %u: the RSX did not finish frame %u (label %u)\n",
						(unsigned)frame_index, (unsigned)(frame_index - 2), (unsigned)*frame_label);
				break;
			}
			usleep(20);
		}
	}
	deferred_frees_run(0);
	caches_invalidate();
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

void ps3_gpu_present(uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height)
{
	if (!ready)
		return;
	ps3_video_begin_frame();
	if (width && height)
		blit_draw(offset, pitch, width, height);
	/* the RSX writes it once this frame's draws are done (a command label can come early: ioq3) */
	rsxSetWriteBackendLabel(context, FRAME_LABEL, frame_index);
	ps3_video_end_frame();
	if (frame_index % STATISTICS_FRAMES == 0)
	{
		ps3_log_printf("[gpu] frame %u: %u draws, %u vertex programs, %u fragment programs, %u textures a frame; "
			"scratch peak %u KB of %u, %u dropped\n", (unsigned)frame_index, (unsigned)(statistics.draws / STATISTICS_FRAMES),
			(unsigned)(statistics.vertex_uploads / STATISTICS_FRAMES), (unsigned)(statistics.fragment_copies / STATISTICS_FRAMES),
			(unsigned)(statistics.texture_loads / STATISTICS_FRAMES), (unsigned)(scratch.peak / 1024),
			(unsigned)(SCRATCH_HALF_SIZE / 1024), (unsigned)scratch.drops);
		memset(&statistics, 0, sizeof(statistics));
	}
	frame_begin();
}

/* ---------- start-up */

int ps3_gpu_initialize(void)
{
	if (ready)
		return 0;
	ps3_display_acquire();
	context = ps3_video_context();
	if (!context)
	{
		ps3_log_printf("[gpu] no RSX context: nothing is drawn\n");
		return -1;
	}
	scratch.base = ps3_gpu_memory_allocate(2 * SCRATCH_HALF_SIZE, 128, &scratch.offset);
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
	caches_invalidate();
	ready = 1;
	ps3_log_printf("[gpu] ready: scratch 2 x %u KB at 0x%08x\n", (unsigned)(SCRATCH_HALF_SIZE / 1024),
		(unsigned)scratch.offset);
	return 0;
}
