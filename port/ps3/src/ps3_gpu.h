/* PS3_GPU.H: the RSX for the Direct3D device (port/ps3/xbox/d3d8_rsx.c). Fixed-width types only: the
Xbox layer has the game's 32-bit long. Offsets are RSX local memory offsets. */

#ifndef __HALO_PS3_GPU_H
#define __HALO_PS3_GPU_H

#include <stdint.h>

#include "ps3_rsx_program.h"

/* ---------- start-up and frames */

/* takes the RSX from the display thread (ps3_main.c); 0 on success */
int ps3_gpu_initialize(void);

/* draws a colour surface pixel for pixel in the middle of the display (scaled down to fit if larger), flips at the
next vsync and starts the next frame (its scratch, after the RSX has finished the frame before last) */
void ps3_gpu_present(uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height);
/* returns when the RSX has done every command so far (a backend label, written once its pipeline drains) */
void ps3_gpu_wait_idle(void);
/* the display's size, and whether it is 16:9 */
void ps3_gpu_display(uint32_t *width, uint32_t *height, int *widescreen);
/* the RSX's time at this split of the frame, without a stall: a timestamp report, read two frames on */
void ps3_gpu_timestamp(int part);
/* (the timer build) the draws from here stamped (on) or not, in one frame of eight: each draw's RSX time by its key
(ps3_draw_stamp_nanoseconds), read with the frame's timestamps */
void ps3_gpu_draw_stamps(int on);
void ps3_gpu_draw_stamp(uint16_t key);
/* the thread that draws ends at stop_drawing (the process ends, or the display thread takes the RSX back) */
void ps3_gpu_set_stop_hook(void (*hook)(void));
void ps3_gpu_stop_drawing(void);

/* ---------- memory */

/* what a block of RSX local memory holds, for the [mem] heartbeat (ps3_main.c) */
enum
{
	_ps3_gpu_memory_texture,
	_ps3_gpu_memory_target,
	_ps3_gpu_memory_vertices,
	_ps3_gpu_memory_scratch,
	_ps3_gpu_memory_program,
	_ps3_gpu_memory_count
};

/* RSX local memory: the CPU writes it in order (write-combined) and never reads it */
void *ps3_gpu_memory_allocate(uint32_t size, uint32_t alignment, int kind, uint32_t *offset);
/* the size and kind it was allocated with; freed once the RSX has finished the frame that can still read it */
void ps3_gpu_memory_free(void *memory, uint32_t size, int kind);

/* PSL1GHT's RSX heap keeps no totals: these come from the calls above and the display buffers */
struct ps3_gpu_memory_usage
{
	/* 0 until the device takes the RSX */
	uint64_t heap;
	uint64_t display;
	uint64_t used;
	uint64_t peak;
	uint64_t bytes[_ps3_gpu_memory_count];
	uint32_t counts[_ps3_gpu_memory_count];
	uint32_t refused;
};

void ps3_gpu_memory_usage(struct ps3_gpu_memory_usage *usage);
/* this frame's scratch (vertices, indices, programs); NULL when this frame's half is full */
void *ps3_gpu_scratch(uint32_t size, uint32_t alignment, uint32_t *offset);

/* ---------- render targets */

struct ps3_gpu_surface
{
	uint32_t color_offset;
	uint32_t color_pitch;
	uint32_t depth_offset;
	uint32_t depth_pitch;
	uint16_t width;
	uint16_t height;
};

/* A8R8G8B8 colour and Z24S8 depth surfaces */
void ps3_gpu_set_surface(const struct ps3_gpu_surface *surface);
/* a target's memory: tiled where asked and a region is free (the depth Z-compressed; ZCull for zcull, once), else
linear; its pitch and the bytes it took */
void *ps3_gpu_target_allocate(uint32_t width, uint32_t height, int depth, int tiled, int zcull, uint32_t *offset,
	uint32_t *pitch, uint32_t *bytes);
void ps3_gpu_target_free(void *memory, uint32_t bytes);
/* flags: D3DCLEAR's (1 depth, 2 stencil, 0x10 0x20 0x40 0x80 red green blue alpha); the rectangle from
the top left. depth_stencil: the 24-bit depth above the stencil byte. */
void ps3_gpu_clear(uint32_t flags, uint32_t argb, uint32_t depth_stencil, uint32_t x, uint32_t y, uint32_t width,
	uint32_t height);

/* ---------- render state */

/* the values are GCM's, which are the Xbox's (and OpenGL's); 0 disables a test */
struct ps3_gpu_state
{
	uint32_t color_mask;
	uint32_t depth_function;
	uint32_t depth_write;
	uint32_t stencil_function;
	uint32_t stencil_reference;
	uint32_t stencil_mask;
	uint32_t stencil_write_mask;
	uint32_t stencil_fail;
	uint32_t stencil_depth_fail;
	uint32_t stencil_pass;
	uint32_t blend;
	uint32_t blend_source;
	uint32_t blend_destination;
	uint32_t blend_equation;
	uint32_t blend_color;
	uint32_t alpha_function;
	uint32_t alpha_reference;
	/* 0, GCM_CULL_FRONT or GCM_CULL_BACK */
	uint32_t cull_face;
	uint32_t front_face;
	uint32_t polygon_mode;
	uint32_t polygon_offset;
	float polygon_offset_factor;
	float polygon_offset_units;
	/* the scissor rectangle from the top left; the viewport covers the whole surface */
	uint16_t scissor_x;
	uint16_t scissor_y;
	uint16_t scissor_width;
	uint16_t scissor_height;
	float depth_minimum;
	float depth_maximum;
};

void ps3_gpu_set_state(const struct ps3_gpu_state *state);

/* ---------- programs */

/* uploads the program unless it is the one loaded */
void ps3_gpu_set_vertex_program(const struct rsx_vertex_program *program);
void ps3_gpu_set_vertex_constants(uint32_t first, uint32_t count, const float *values);
/* a copy of the program with these uniform values in the scratch, unless the last draw had the same;
0 on success, -1 when the scratch is full */
int ps3_gpu_set_fragment_program(const struct rsx_fragment_program *program, const float uniforms[][4]);

/* ---------- textures */

struct ps3_gpu_texture
{
	uint32_t offset;
	uint32_t pitch;
	uint32_t border;
	uint16_t width;
	uint16_t height;
	uint16_t depth;
	/* GCM_TEXTURE_FORMAT_*, with LIN and UNRM */
	uint8_t format;
	uint8_t levels;
	uint8_t dimension;
	uint8_t cube;
	uint8_t minify;
	uint8_t magnify;
	uint8_t wrap_s;
	uint8_t wrap_t;
	uint8_t wrap_r;
	uint8_t anisotropy;
	/* 4.8 fixed point: the bias signed (13 bits), the levels unsigned */
	uint16_t bias;
	uint16_t minimum_level;
	uint16_t maximum_level;
	/* GCM's remap word, 0 for the identity */
	uint32_t remap;
};

/* NULL turns the unit off */
void ps3_gpu_set_texture(uint32_t unit, const struct ps3_gpu_texture *texture);
/* texels were written: the RSX's texture cache is flushed before the next draw */
void ps3_gpu_textures_written(void);

/* ---------- vertices and draws */

/* attribute index reads elements floats at offset, stride bytes apart (0: one value for every
vertex); elements 0 turns it off */
void ps3_gpu_set_attribute(uint32_t index, uint32_t offset, uint32_t stride, uint32_t elements);
/* vertices were written outside the scratch (the vertex cache): the RSX's vertex cache is flushed before
the next draw */
void ps3_gpu_vertices_written(void);
/* primitive: GCM_TYPE_*, which are D3DPT_*'s */
void ps3_gpu_draw(uint32_t primitive, uint32_t first, uint32_t count);
/* 16-bit indices at offset */
void ps3_gpu_draw_indexed(uint32_t primitive, uint32_t offset, uint32_t count);

#endif
