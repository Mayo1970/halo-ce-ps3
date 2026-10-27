/* SPU_DIRECT3D.H: the Direct3D thread's work on an SPU (spu_direct3d.c): the records, the draw state and the RSX commands,
in chunks the PPE's service thread calls (d3d8_rsx.c). The SPU, ps3_spu.c, ps3_gpu.c and d3d8_rsx.c share it. */

#ifndef __HALO_PS3_SPU_DIRECT3D_H
#define __HALO_PS3_SPU_DIRECT3D_H

#include <stdint.h>

#define SPU_D3D_RUNNING 1
#define SPU_D3D_EXITED 2
/* the SPU thread's event port to the service thread's queue, and to the game thread's */
#define SPU_D3D_SERVICE_PORT 17
#define SPU_D3D_GAME_PORT 18

/* the tables the SPU keeps: textures, the vertex cache's arrays, the index cache's, render targets, vertex shaders */
#define SPU_D3D_TEXTURES 2048
#define SPU_D3D_ARRAYS 4096
#define SPU_D3D_INDEX_ARRAYS 2048
#define SPU_D3D_TARGETS 64
#define SPU_D3D_FRAGMENTS 2048
#define SPU_D3D_FRAGMENT_BUCKETS 1024
/* the chunks published and not yet called, a power of two */
#define SPU_D3D_CHUNK_LIST 256
/* a record's commands at most: the ring's room ahead of each record */
#define SPU_D3D_RECORD_COMMANDS (64 * 1024)
/* the frames whose chunks' room the SPU tracks */
#define SPU_D3D_FRAMES 4

/* ---------- the records (d3d8_rsx.c writes them, the SPU runs them) */

enum
{
	_spu_d3d_record_wrap,
	_spu_d3d_record_states,
	_spu_d3d_record_attributes,
	_spu_d3d_record_texture,
	_spu_d3d_record_vertex_shader,
	_spu_d3d_record_constants,
	_spu_d3d_record_constant_mode,
	_spu_d3d_record_viewport,
	_spu_d3d_record_render_target,
	_spu_d3d_record_ui_offset,
	_spu_d3d_record_clear,
	_spu_d3d_record_draw,
	_spu_d3d_record_draw_indexed,
	_spu_d3d_record_immediate,
	_spu_d3d_record_elements,
	_spu_d3d_record_timestamp,
	_spu_d3d_record_draw_stamps,
	_spu_d3d_record_present,
	/* the SPU's only: the screen's new targets (halo_screen_commit), a tag cache load (d3d8_tag_cache_loaded), a
	backend label written (a: its index, b: its value; the start's test of the chunks) */
	_spu_d3d_record_screen,
	_spu_d3d_record_tag_cache,
	_spu_d3d_record_label,
	/* the half resolution particles' start (a 1: their targets follow) and end (a 0) */
	_spu_d3d_record_particles,
	/* the lights' bounds (d3d8_light_bounds): on (a 1: the bounds follow) and off (a 0) */
	_spu_d3d_record_light_bounds,
	/* the dynamic resolution (d3d8_dynamic_resolution): the screen's 3D view drawn smaller (a 1: the scale follows),
	then scaled up over the screen (a 0) */
	_spu_d3d_record_scale,
	/* the SPU's only: RSX memory copied into main memory (bsp_keep.c's BSPs) */
	_spu_d3d_record_copy,
	_spu_d3d_record_count
};

/* each record starts 16-aligned with this header; bytes: the whole record, a multiple of 16 */
typedef struct
{
	uint32_t type;
	uint32_t bytes;
	uint32_t a;
	uint32_t b;
} spu_d3d_record;

/* a draw record; stream_mask's streams follow in order, then the indices it copied, then the vertices */
typedef struct
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
	/* this draw's number (texture Lock words, the completed draws) */
	uint32_t draw;
	uint32_t pad;
} spu_d3d_record_draw;

/* vertex v of the stream at base + v * stride */
typedef struct
{
	uint64_t base;
	uint32_t stride;
	uint32_t pad;
} spu_d3d_record_stream;

/* each vertex follows: the written registers' 4 floats, in register order */
typedef struct
{
	uint32_t primitive;
	uint32_t count;
	uint32_t written;
	uint32_t draw;
} spu_d3d_record_immediate;

/* the rectangles follow (a of the record) */
typedef struct
{
	uint32_t color;
	uint32_t z;
	uint32_t stencil;
	uint32_t pad;
} spu_d3d_record_clear;

#define SPU_D3D_RECORD_ELEMENTS 4

/* vertices the game's thread wrote into the record, after this: each element's from there plus its offset, a vertex
every stride bytes (d3d8_draw_decals_begin) */
typedef struct
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
	} elements[SPU_D3D_RECORD_ELEMENTS];
} spu_d3d_record_elements;

/* a surface's words, as the game's thread read them */
typedef struct
{
	uint32_t valid;
	uint32_t data;
	uint32_t format;
	uint32_t size;
} spu_d3d_surface_words;

/* a stage's texture header and palette data; fresh: the header was set up again (Lock 0) since its last draw */
typedef struct
{
	uint32_t bound;
	uint32_t data;
	uint32_t format;
	uint32_t size;
	uint32_t palette;
	uint32_t fresh;
} spu_d3d_stage_texture;

/* D3DVIEWPORT8 */
typedef struct
{
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	float minimum_z;
	float maximum_z;
} spu_d3d_viewport;

/* the present record's: the back buffer's words; the screen record's: the screen's width and scale */
typedef struct
{
	spu_d3d_surface_words back_buffer;
} spu_d3d_record_present;

typedef struct
{
	uint32_t width;
	float scale[2];
	uint32_t pad;
} spu_d3d_record_screen;

/* the particles record's: the half targets' colour and depth, their size in pixels */
typedef struct
{
	uint32_t color_offset;
	uint32_t color_pitch;
	uint32_t depth_offset;
	uint32_t depth_pitch;
	uint32_t width;
	uint32_t height;
	uint32_t pad[2];
} spu_d3d_record_particles;

/* the light bounds record's: a box's clip space bounds, x and y in -1 to 1 (x0 over x1: none), the depth in 0 to 1 */
typedef struct
{
	float x0;
	float y0;
	float x1;
	float y1;
	float z0;
	float z1;
	uint32_t pad[2];
} spu_d3d_record_light_bounds;

/* the tag cache's span after a load */
typedef struct
{
	uint32_t begin;
	uint32_t end;
	uint32_t pad[2];
} spu_d3d_record_tag_cache;

/* the scale record's: the screen colour's data, the view's scale in eighths (5 to 7), the target of the screen's size
the view is drawn into */
typedef struct
{
	uint32_t data;
	uint32_t eighths;
	uint32_t color_offset;
	uint32_t color_pitch;
} spu_d3d_record_scale;

/* the copy record's: the RSX memory's offset, the main memory's IO offset, the bytes */
typedef struct
{
	uint32_t source;
	uint32_t destination;
	uint32_t bytes;
	uint32_t pad;
} spu_d3d_record_copy;

typedef char spu_d3d_record_sizes_check[sizeof(spu_d3d_record_copy) == 16 &&
	sizeof(spu_d3d_record_draw) % 16 == 0 && sizeof(spu_d3d_record_stream) == 16 &&
	sizeof(spu_d3d_record_immediate) == 16 && sizeof(spu_d3d_record_clear) == 16 &&
	sizeof(spu_d3d_record_elements) % 16 == 0 && sizeof(spu_d3d_viewport) == 24 &&
	sizeof(spu_d3d_record_light_bounds) == 32 && sizeof(spu_d3d_record_scale) == 16 ? 1 : -1];

/* ---------- the state the PPE's vertex shader objects hold (d3d8_rsx.c's struct vertex_shader_object, checked there) */

#define SPU_D3D_SHADER_ELEMENTS 20
#define SPU_D3D_SHADER_ELEMENT_COUNT 116
#define SPU_D3D_SHADER_PROGRAM 120
#define SPU_D3D_SHADER_TRANSLATED 144
#define SPU_D3D_SHADER_UNTRANSLATABLE 148
#define SPU_D3D_SHADER_BYTES 160

/* ---------- the tables in main memory */

/* a texture the SPU keeps (d3d8_rsx.c's texture_entry): its key, the PPE's RSX copy, what its checks found */
typedef struct
{
	uint32_t data;
	uint32_t format_word;
	uint32_t size_word;
	uint32_t palette_hash;
	/* the PPE's: its RSX copy (0: none) and size; its texels' address and size, 0 when the check cannot read them */
	uint32_t memory;
	uint32_t bytes;
	uint32_t source;
	uint32_t size;
	uint64_t content_hash;
	uint32_t checked_frame;
	uint32_t last_used_frame;
	uint8_t valid;
	uint8_t checked;
	uint8_t placeholder;
	uint8_t hires;
	int8_t hires_style;
	/* in the texture cache; whether a hires bitmap can stand for it */
	uint8_t cached;
	uint8_t hires_candidate;
	uint8_t pad0;
	uint32_t pad1[2];
	/* ps3_gpu.h's struct ps3_gpu_texture (40 bytes) */
	uint32_t gpu[10];
	uint32_t hires_gpu[10];
} spu_d3d_texture;

/* the vertex cache: an element of a map buffer's stream, vertices [0, count) as float4s at offset (room for capacity) */
typedef struct
{
	uint32_t source;
	uint32_t stride;
	uint32_t type;
	uint32_t count;
	uint32_t capacity;
	uint32_t offset;
	uint32_t memory;
	uint32_t pad;
} spu_d3d_array;

/* the index cache: a model's indices copied into the arena */
typedef struct
{
	uint32_t source;
	uint32_t count;
	uint32_t vertices;
	uint32_t offset;
} spu_d3d_index_array;

/* a pixel shader's program as the PPE keeps it for the SPU (d3d8_rsx.c's fragment table): its key's 43 words, the
microcode's address (half-swapped, uniforms zero) and the program's words */
#define SPU_D3D_KEY_WORDS 43

typedef struct
{
	uint32_t key[SPU_D3D_KEY_WORDS];
	uint32_t hash;
	/* the next in its bucket (an index, -1 none), the stamped draws' key */
	int32_t next;
	uint32_t stamp_key;
	uint32_t translated;
	uint32_t microcode;
	uint32_t word_count;
	uint32_t control;
	uint32_t register_count;
	uint32_t texcoord_mask;
	uint32_t uniform_count;
	/* each uniform's word << 8 | its uniform; the PPE's fragment_entry, whose stamp fields the SPU writes */
	uint32_t uniforms[32];
	uint32_t ppe_entry;
	uint32_t pad[2];
} spu_d3d_fragment;

typedef char spu_d3d_table_sizes_check[sizeof(spu_d3d_texture) == 144 && sizeof(spu_d3d_array) == 32 &&
	sizeof(spu_d3d_index_array) == 16 && sizeof(spu_d3d_fragment) == 352 ? 1 : -1];

/* ---------- the SPU's set-up and the PPE's view of it */

typedef struct
{
	/* the record ring (its size a power of two) */
	uint32_t ring;
	uint32_t ring_bytes;
	/* the chunks' ring (IO-mapped main memory) and its IO offset; the list of chunks published */
	uint32_t chunks;
	uint32_t chunks_offset;
	uint32_t chunks_bytes;
	uint32_t chunk_list;
	/* the request and its answer; the counters and timers the SPU puts at each present */
	uint32_t request;
	uint32_t response;
	uint32_t statistics;
	/* RSX local memory: the scratch (two halves), the vertex arena */
	uint32_t scratch;
	uint32_t scratch_offset;
	uint32_t scratch_half;
	uint32_t scratch_bytes;
	uint32_t arena;
	uint32_t arena_offset;
	uint32_t arena_bytes;
	/* the tables */
	uint32_t textures;
	uint32_t arrays;
	uint32_t index_arrays;
	uint32_t fragments;
	uint32_t fragment_buckets;
	/* b / 255 for each byte, as the PPE's floats */
	uint32_t byte_floats;
	/* the device's state at the start (spu_d3d_initial) */
	uint32_t initial;
	/* the texture cache (texture_cache_holds); the button icons' placeholders and style (hud_hires_rsx.c) */
	uint32_t texture_cache;
	uint32_t texture_cache_bytes;
	uint32_t placeholders;
	uint32_t button_style;
	/* the timestamps of the last three frames (spu_d3d_stamps), and the stamped draws' (spu_d3d_draw_stamps) */
	uint32_t stamps;
	uint32_t draw_stamps;
	/* the present's blit (ps3_gpu.c): its vertex program's 8 words, its fragment program's offset */
	uint32_t blit_vertex;
	uint32_t blit_fragment_offset;
	/* where a fragment_entry (d3d8_rsx.c) has its stamped draws' parts (specular, effects), then their texture mask; its
	textures */
	uint32_t stamp_part_offset;
	uint32_t stamp_textures_offset;
	uint32_t frame_index;
	/* the half resolution particles' depth program (ps3_gpu.c's particles_depth_build) */
	uint32_t particles_depth_offset;
	uint32_t pad[27];
} __attribute__((aligned(128))) spu_d3d_setup;

/* ps3_gpu.c's timestamps of a frame (its stamps[3]), and its stamped draws */
#define SPU_D3D_TIMESTAMP_PARTS 64
#define SPU_D3D_DRAW_STAMPS 480

typedef struct
{
	uint16_t parts[SPU_D3D_TIMESTAMP_PARTS];
	uint32_t draws[SPU_D3D_TIMESTAMP_PARTS];
	uint32_t count;
	uint32_t frame;
	/* the scale the screen's 3D view was drawn at, in eighths (the dynamic resolution; 0: whole) */
	uint32_t eighths;
	uint32_t pad;
} spu_d3d_stamps;

typedef struct
{
	uint16_t keys[SPU_D3D_DRAW_STAMPS];
	uint32_t count;
	uint32_t frame;
	int32_t on;
	uint32_t pad;
} spu_d3d_draw_stamps;

typedef char spu_d3d_stamps_check[sizeof(spu_d3d_stamps) % 16 == 0 && sizeof(spu_d3d_draw_stamps) % 16 == 0 ? 1 : -1];

/* ps3_timers.h's Direct3D timers from _ps3_timer_thread_busy, in its order (d3d8_rsx.c checks it) */
enum
{
	_spu_d3d_timer_thread_busy,
	_spu_d3d_timer_thread_wait,
	_spu_d3d_timer_record_wrap,
	_spu_d3d_timer_record_present = _spu_d3d_timer_record_wrap + _spu_d3d_record_present,
	_spu_d3d_timer_draw_state,
	_spu_d3d_timer_draw_textures,
	_spu_d3d_timer_texture_hash,
	_spu_d3d_timer_texture_upload,
	_spu_d3d_timer_draw_colours,
	_spu_d3d_timer_draw_vertices,
	_spu_d3d_timer_draw_indices,
	_spu_d3d_timer_draw_program,
	_spu_d3d_timer_draw_targets,
	_spu_d3d_timer_draw_raster,
	_spu_d3d_timer_draw_screen,
	_spu_d3d_timer_draw_key,
	_spu_d3d_timer_draw_fragment,
	_spu_d3d_timer_draw_uniforms,
	_spu_d3d_timer_draw_vertex_program,
	_spu_d3d_timer_draw_constants,
	_spu_d3d_timer_count
};

/* ps3_timers.h's: the present's timer, the specular part's first split and its fog split; the specular part's last
stamp part, and a stamp's part shift */
#define SPU_D3D_TIMER_PRESENT 3
#define SPU_D3D_TIMER_SPECULAR_LIGHTS 14
#define SPU_D3D_TIMER_SPECULAR_FOG 20
#define SPU_D3D_STAMP_SPECULAR_FOG 6
#define SPU_D3D_STAMP_PART_SHIFT 10

/* the PPE writes it; its stores end the SPU's reservation of the line, which wakes the SPU */
typedef struct
{
	/* the game thread: the records published; an event when the records run reach notify (0: none) */
	volatile uint64_t published;
	volatile uint64_t notify;
	/* the service thread: the request answered, the chunks called */
	volatile uint32_t response;
	volatile uint32_t called;
	volatile uint32_t quit;
	uint32_t pad[25];
} __attribute__((aligned(128))) spu_d3d_bell;

/* the SPU puts it; the PPE only reads it */
typedef struct
{
	volatile uint32_t state;
	volatile uint32_t ls_address;
	/* the records run, their room given back; the last draw done */
	volatile uint64_t consumed;
	volatile uint32_t completed_draws;
	/* the last request made, the chunks published */
	volatile uint32_t request;
	volatile uint32_t chunks;
	volatile uint32_t stage;
	volatile uint32_t wakes;
	volatile uint32_t timeouts;
	volatile uint32_t record_type;
	volatile uint32_t events_refused;
	uint32_t pad[20];
} __attribute__((aligned(128))) spu_d3d_echo;

/* a published chunk: commands ending in a RETURN, at an IO offset */
typedef struct
{
	uint32_t offset;
	uint32_t bytes;
} spu_d3d_chunk;

/* ---------- requests: the SPU waits for the answer (bell.response) */

enum
{
	_spu_d3d_request_none,
	/* translate the vertex shader object at a (its program fields then hold the answer) */
	_spu_d3d_request_vertex_program,
	/* the pixel shader of the key in words; answer: the fragment table's index, -1 none */
	_spu_d3d_request_fragment_program,
	/* a render target: data, units width and height, depth, screen; answer: its memory */
	_spu_d3d_request_target,
	/* a texture's check (texture_check): the texture table's entry a, fresh b, palette c, the style d */
	_spu_d3d_request_texture,
	/* the placeholder's texture of asset a in style b; answer: the gpu words */
	_spu_d3d_request_placeholder,
	/* the RSX idle (before the vertex arena is used again) */
	_spu_d3d_request_wait_idle,
	/* the flip's slot; answer: the display buffer */
	_spu_d3d_request_present_begin,
	/* the frame's label and flip after the chunks, the next frame's start; answer: its number */
	_spu_d3d_request_present_end,
	/* a free of RSX memory: words of address, size pairs */
	_spu_d3d_request_free,
	/* the vertex arena's RSX memory, at its first use as d3d8_rsx.c's (answer: its address and offset, 0 none) */
	_spu_d3d_request_arena,
	/* present_run's statistics lines (the statistics put before) */
	_spu_d3d_request_log,
	_spu_d3d_request_count
};

#define SPU_D3D_REQUEST_WORDS 62

typedef struct
{
	uint32_t number;
	uint32_t kind;
	uint32_t words[SPU_D3D_REQUEST_WORDS];
} __attribute__((aligned(128))) spu_d3d_request;

typedef struct
{
	uint32_t number;
	uint32_t status;
	uint32_t words[SPU_D3D_REQUEST_WORDS];
} __attribute__((aligned(128))) spu_d3d_response;

/* ---------- the state the device starts with (Direct3D_CreateDevice) */

typedef struct
{
	uint32_t render_state[144];
	uint32_t texture_state[4][32];
	spu_d3d_surface_words targets[2];
	spu_d3d_viewport viewport;
	uint32_t shader_constant_mode;
	uint32_t screen_width;
	float screen_scale[2];
	/* the tag cache's span */
	uint32_t static_begin;
	uint32_t static_end;
	/* the button icons' style and placeholders (data, asset) */
	int32_t button_style;
	uint32_t placeholders[8][2];
	uint32_t pad[3];
	float constants[198][4];
	float attributes[16][4];
} __attribute__((aligned(128))) spu_d3d_initial;

/* ---------- the counters and timers (ps3_timers.h's Direct3D thread timers, timebase ticks), put at each present */

#define SPU_D3D_TIMERS 64
#define SPU_D3D_TYPE_ELEMENTS 128

typedef struct
{
	uint64_t timers[SPU_D3D_TIMERS];
	uint32_t counts[SPU_D3D_TIMERS];
	/* d3d8_rsx.c's statistics */
	uint32_t draws, skipped_program, skipped_target, skipped_scratch, skipped_scissor;
	uint32_t texture_hashed;
	uint32_t cached_elements, converted_elements, cache_refused;
	uint32_t arrays, arena_used;
	uint32_t textures, texture_bytes, texture_unused_bytes;
	/* ps3_gpu.c's */
	uint32_t gpu_draws, vertex_uploads, vertex_switches, fragment_copies, texture_loads;
	uint32_t scratch_peak, scratch_drops;
	/* the SPU's own: chunks, their bytes, requests, its time waiting for them */
	uint32_t chunks, chunk_bytes, requests, request_ticks;
	uint32_t frame;
	uint32_t pad[5];
	uint32_t type_elements[SPU_D3D_TYPE_ELEMENTS];
} __attribute__((aligned(128))) spu_d3d_statistics;

#endif
