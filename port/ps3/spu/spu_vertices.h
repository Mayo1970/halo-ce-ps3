/* SPU_VERTICES.H: the Direct3D thread's vertex and index conversions and pixel shader copies on an SPU (spu_vertices.c),
in a ring of tasks. The SPU, the host layer (ps3_spu.c), ps3_gpu.c and d3d8_rsx.c share it: fixed-width types only. */

#ifndef __HALO_PS3_SPU_VERTICES_H
#define __HALO_PS3_SPU_VERTICES_H

#include <stdint.h>

/* tasks, a power of two */
#define SPU_VERTICES_RING 1024
/* the elements of a draw (d3d8_rsx.c's setup_vertices) */
#define SPU_VERTICES_SLOTS 15

#define SPU_VERTICES_RUNNING 1
#define SPU_VERTICES_EXITED 2

/* spu_vertices_task.kind */
#define SPU_VERTICES_CONVERT 1
#define SPU_VERTICES_INDICES 2
#define SPU_VERTICES_FRAGMENT 3
#define SPU_VERTICES_TEXTURE 4
#define SPU_VERTICES_HASH 5

/* a FRAGMENT task's constants (their places), their values, and the most bytes of its microcode (one DMA) */
#define SPU_VERTICES_OCCURRENCES 32
#define SPU_VERTICES_VALUES 11
#define SPU_VERTICES_FRAGMENT_BYTES (16384 - 32)

/* the Xbox's vertex element types (the XDK's D3DVSDT_*), which the SPU converts as d3d8_rsx.c does */
#define SPU_VERTICES_FLOAT1 0x12
#define SPU_VERTICES_FLOAT2 0x22
#define SPU_VERTICES_FLOAT3 0x32
#define SPU_VERTICES_FLOAT4 0x42
#define SPU_VERTICES_FLOAT2H 0x72
#define SPU_VERTICES_D3DCOLOR 0x40
#define SPU_VERTICES_NORMSHORT1 0x11
#define SPU_VERTICES_NORMSHORT2 0x21
#define SPU_VERTICES_NORMSHORT3 0x31
#define SPU_VERTICES_NORMSHORT4 0x41
#define SPU_VERTICES_NORMPACKED3 0x16
#define SPU_VERTICES_SHORT1 0x15
#define SPU_VERTICES_SHORT2 0x25
#define SPU_VERTICES_SHORT3 0x35
#define SPU_VERTICES_SHORT4 0x45
#define SPU_VERTICES_PBYTE1 0x14
#define SPU_VERTICES_PBYTE2 0x24
#define SPU_VERTICES_PBYTE3 0x34
#define SPU_VERTICES_PBYTE4 0x44
/* not the Xbox's: a native 32-bit word, its low 16 bits then its high 16, each over 32767 unclamped (the decals') */
#define SPU_VERTICES_TEXCOORD_WORD 0x2E

/* an element: where vertex 0 of the task has it, and its size (the DMA reads no further) */
typedef struct
{
	uint32_t source;
	uint32_t stride;
	uint32_t type;
	uint32_t bytes;
} spu_vertices_slot;

/* CONVERT: count vertices, each slot's element as 4 floats, a vertex's slots together. INDICES: count 16-bit indices
from slots[0].source, each less subtract. The output is 16-aligned, its size rounded up to 16 bytes */
typedef struct
{
	uint16_t kind;
	uint16_t slot_count;
	uint32_t count;
	uint32_t output;
	uint32_t subtract;
	spu_vertices_slot slots[SPU_VERTICES_SLOTS];
} spu_vertices_task;

/* FRAGMENT, in a task's place (ps3_gpu.c): a pixel shader's microcode, bytes from source, into output, each constant's
4 words its value's with their 16-bit halves swapped. An occurrence: its word / 4 << 4 | its value's index */
typedef struct
{
	uint16_t kind;
	uint16_t occurrence_count;
	uint32_t bytes;
	uint32_t output;
	uint32_t source;
	uint16_t occurrences[SPU_VERTICES_OCCURRENCES];
	uint32_t values[SPU_VERTICES_VALUES][4];
} spu_vertices_fragment;

/* spu_vertices_texture.conversion: the bytes as they are, 16-bit texels to the RSX's byte order, or each texel to an
A8R8G8B8 word (xbox_texture_layout.c's convert_texel, by texel_kind) */
#define SPU_TEXTURE_COPY 0
#define SPU_TEXTURE_SWAP16 1
#define SPU_TEXTURE_ARGB 2

/* xbox_texture_layout.c's texel kinds, which it checks against its own */
enum
{
	SPU_TEXEL_UNKNOWN,
	SPU_TEXEL_A8R8G8B8, SPU_TEXEL_X8R8G8B8, SPU_TEXEL_R5G6B5, SPU_TEXEL_A1R5G5B5, SPU_TEXEL_X1R5G5B5, SPU_TEXEL_A4R4G4B4,
	SPU_TEXEL_L8, SPU_TEXEL_AL8, SPU_TEXEL_A8, SPU_TEXEL_A8L8, SPU_TEXEL_P8, SPU_TEXEL_G8B8, SPU_TEXEL_R8B8, SPU_TEXEL_R6G5B5,
	SPU_TEXEL_L16, SPU_TEXEL_V16U16, SPU_TEXEL_A8B8G8R8, SPU_TEXEL_B8G8R8A8, SPU_TEXEL_R8G8B8A8, SPU_TEXEL_R5G5B5A1,
	SPU_TEXEL_R4G4B4A4, SPU_TEXEL_YUY2, SPU_TEXEL_UYVY, SPU_TEXEL_D24S8, SPU_TEXEL_D16,
};

/* TEXTURE (texture_upload): units (COPY: bytes) of in_bytes at source to out_bytes at output; swizzled levels one stream
(rows 0), linear rows source_pitch to output_pitch, the rest zero; 16-aligned. HASH: texture_hash of units bytes, 16 out */
typedef struct
{
	uint16_t kind;
	uint8_t conversion;
	uint8_t texel_kind;
	uint32_t source;
	uint32_t output;
	uint32_t units;
	uint32_t rows;
	uint32_t source_pitch;
	uint32_t output_pitch;
	uint8_t in_bytes;
	uint8_t out_bytes;
	uint8_t pad[226];
} spu_vertices_texture;

/* read once at the start */
typedef struct
{
	uint32_t ring;
	uint32_t pad[31];
} __attribute__((aligned(128))) spu_vertices_setup;

/* the PPE writes it; its store ends the SPU's reservation of the line, which wakes the SPU */
typedef struct
{
	volatile uint32_t head;
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) spu_vertices_bell;

/* the SPU puts it after each task, behind the task's output; the PPE only reads it */
typedef struct
{
	volatile uint32_t state;
	volatile uint32_t tail;
	/* timebase ticks (the SPU's decrementer) in tasks; the vertex elements and the indices done */
	volatile uint32_t busy_ticks;
	volatile uint32_t elements;
	volatile uint32_t indices;
	volatile uint32_t wakes;
	volatile uint32_t timeouts;
	/* where the SPU's own copy is, for the PPE's read when a task does not end */
	volatile uint32_t ls_address;
	volatile uint32_t stage;
	/* of busy_ticks: the waits for the DMA of the tasks and their inputs, and for the outputs' buffers */
	volatile uint32_t input_wait_ticks;
	volatile uint32_t output_wait_ticks;
	/* the texels' bytes converted and the bytes hashed */
	volatile uint32_t texture_bytes;
	volatile uint32_t hash_bytes;
	uint32_t pad[19];
} __attribute__((aligned(128))) spu_vertices_echo;

typedef char spu_vertices_layout_check[sizeof(spu_vertices_task) == 256 ? 1 : -1];
typedef char spu_vertices_fragment_check[sizeof(spu_vertices_fragment) == sizeof(spu_vertices_task) ? 1 : -1];
typedef char spu_vertices_texture_check[sizeof(spu_vertices_texture) == sizeof(spu_vertices_task) ? 1 : -1];

/* a texture's content hash (d3d8_rsx.c's check of a texture outside the texture cache), the same on the PPE and on the
SPU: 16 lanes of FNV-1a on its 32-bit words in order (lane: the word's index mod 16), its last bytes into lane 0 */
#define TEXTURE_HASH_LANES 16
#define TEXTURE_HASH_PRIME 16777619u
#define TEXTURE_HASH_BASIS 2166136261u

static inline uint64_t texture_hash_finish(const uint32_t lanes[TEXTURE_HASH_LANES], uint32_t size)
{
	uint32_t a = TEXTURE_HASH_BASIS ^ size, b = 0x9e3779b9u;
	int lane;

	for (lane = 0; lane < TEXTURE_HASH_LANES; lane++)
	{
		a = (a ^ lanes[lane]) * TEXTURE_HASH_PRIME;
		b = (b ^ lanes[TEXTURE_HASH_LANES - 1 - lane]) * TEXTURE_HASH_PRIME;
	}
	return (uint64_t)a << 32 | b;
}

#endif
