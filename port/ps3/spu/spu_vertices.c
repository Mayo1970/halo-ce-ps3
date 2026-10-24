/* SPU_VERTICES.C: an SPU thread that runs the Direct3D thread's conversions (d3d8_rsx.c): a draw's vertex elements to
float4s, its indices less a base and its pixel shader's copy with constants, into RSX memory, as the PPE's bit for bit. */

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>

#include <stddef.h>

#include "spu_vertices.h"

#define TAG_TASKS 1
/* and + 1: a tag each for the two input and the two output buffers */
#define TAG_INPUT 2
#define TAG_OUTPUT 4
#define TAG_ECHO 6
/* 1 ms of the timebase: a wake if a reservation event is lost */
#define DECREMENTER_TICKS 79800
/* 2 KB of tasks: one DMA */
#define TASK_BATCH 8
/* an output buffer's float4s (16 KB, one DMA), and the most bytes a fetch asks for */
#define OUTPUT_QUADS 1024
#define FETCH_BYTES (16384 - 32)
/* a fetch's lines hold up to 30 bytes more, and an unaligned load reads up to 31 past its byte */
#define INPUT_SLOP 96
#define INDEX_BATCH 4096
/* the most bytes of a vertex that one fetch's slots span (a stream's elements) */
#define GROUP_SPAN 2048

/* slots that one fetch serves: the same stride, their bytes of a vertex within GROUP_SPAN */
typedef struct
{
	uint32_t base;
	uint32_t stride;
	uint32_t span;
	uint32_t slots;
} slot_group;

static spu_vertices_bell bell;
static spu_vertices_echo echo;
/* the echo as each output tag carries it out */
static spu_vertices_echo echo_copies[2];
static spu_vertices_setup setup;
static spu_vertices_task tasks[TASK_BATCH] __attribute__((aligned(128)));
static vec_float4 outputs[2][OUTPUT_QUADS] __attribute__((aligned(128)));
static unsigned char inputs[2][16384 + INPUT_SLOP] __attribute__((aligned(128)));
static uint64_t bell_ea, echo_ea;
/* the output buffer the next pass fills; the buffers the task put into, its last one; the last echo's buffer (2: none) */
static uint32_t output_next, task_buffers, last_buffer, echo_buffer = 2;

static const vec_uchar16 byte_order = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const vec_float4 defaults = { 0.0f, 0.0f, 0.0f, 1.0f };
/* the first n lanes of an element come from the vertex, the rest from the defaults */
static const vec_uint4 lanes[5] =
{
	{ 0, 0, 0, 0 },
	{ ~0u, 0, 0, 0 },
	{ ~0u, ~0u, 0, 0 },
	{ ~0u, ~0u, ~0u, 0 },
	{ ~0u, ~0u, ~0u, ~0u },
};

/* ---------- DMA */

static void dma_wait(uint32_t tags)
{
	mfc_write_tag_mask(tags);
	mfc_read_tag_status_all();
}

/* the wait's decrementer ticks */
static uint32_t timed_wait(uint32_t tags)
{
	uint32_t start = spu_read_decrementer();

	dma_wait(tags);
	return start - spu_read_decrementer();
}

/* (the start's and the end's: every output and echo before it are done) */
static void put_echo(void)
{
	dma_wait((1 << TAG_OUTPUT) | (1 << (TAG_OUTPUT + 1)));
	mfc_put(&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	dma_wait(1 << TAG_ECHO);
}

/* starts fetching size bytes (1 to FETCH_BYTES) at ea into input buffer k, with the 16-byte lines that hold them (a line
never crosses a page); returns where ea's byte will be */
static const unsigned char *fetch_start(uint32_t k, uint32_t ea, uint32_t size)
{
	uint32_t first = ea & ~15u, end = (ea + size + 15) & ~15u;

	mfc_get(inputs[k], first, end - first, TAG_INPUT + k, 0, 0);
	return inputs[k] + (ea & 15);
}

/* the next output buffer, once its last put (and an echo behind it) is done */
static vec_float4 *output_take(void)
{
	echo.output_wait_ticks += timed_wait(1 << (TAG_OUTPUT + output_next));
	return outputs[output_next];
}

static void output_put(uint32_t ea, uint32_t bytes)
{
	mfc_put(outputs[output_next], ea, bytes, TAG_OUTPUT + output_next, 0, 0);
	task_buffers |= 1 << output_next;
	last_buffer = output_next;
	output_next ^= 1;
}

/* the task's tail out, without a wait for its output: the echo goes fenced behind the last buffer's put, after the
other buffer's puts of the task and the echo before it, so the PPE never sees a tail before its output or an older one */
static void task_publish(uint32_t tail)
{
	uint32_t buffer = task_buffers ? last_buffer : 0, other = buffer ^ 1;

	if (!task_buffers)
		echo.output_wait_ticks += timed_wait((1 << TAG_OUTPUT) | (1 << (TAG_OUTPUT + 1)));
	else if ((task_buffers & (1 << other)) || echo_buffer == other)
		echo.output_wait_ticks += timed_wait(1 << (TAG_OUTPUT + other));
	echo.tail = tail;
	echo_copies[buffer] = echo;
	mfc_putf(&echo_copies[buffer], echo_ea, sizeof(echo), TAG_OUTPUT + buffer, 0, 0);
	echo_buffer = buffer;
	task_buffers = 0;
}

/* the bytes at p the pattern picks (0 to 15 from p; 0x80 a zero byte), p's quadword and the next; the pattern's bytes
are added as halfwords (they never carry) */
static inline vec_uchar16 load_pattern(const unsigned char *p, vec_uchar16 pattern)
{
	const vec_uchar16 *q = (const vec_uchar16 *)((uintptr_t)p & ~15u);
	vec_ushort8 shift = spu_splats((unsigned short)(((uintptr_t)p & 15) * 0x0101));

	return spu_shuffle(q[0], q[1], (vec_uchar16)spu_add((vec_ushort8)pattern, shift));
}

/* ---------- elements (d3d8_rsx.c's vertex_element_convert) */

/* a lane's quotient from its window, the first 32 bits of the quotient's binary digits: normalized, its top 24 bits and
the next one rounded (the bits after are never all 0: no ties); exponents: 159 less the window's bits, 127 for 32 */
static inline vec_float4 window_float(vec_uint4 window, vec_uint4 exponents)
{
	vec_uint4 zeros = spu_cntlz(window);
	vec_uint4 top = spu_sl(window, zeros);
	vec_uint4 rounded = spu_add(spu_rlmask(top, -8), spu_and(spu_rlmask(top, -7), 1));

	return spu_mul(spu_convtf(rounded, 24), (vec_float4)spu_sl(spu_sub(exponents, zeros), 23));
}

/* x / (2^n - 1) for whole x, as the float division rounds it: its binary digits repeat |x|'s n bits, so the window is
|x| plus its copies shifted by n, 2n (a shift of 32 adds nothing). |x| over largest gives 1; the sign is x's */
static inline vec_float4 divide_repeating(vec_int4 value, vec_uint4 shifts1, vec_uint4 shifts2, vec_uint4 exponents,
	vec_uint4 largest)
{
	vec_int4 sign = spu_rlmaska(value, -31);
	vec_uint4 magnitude = (vec_uint4)spu_sub(spu_xor(value, sign), sign);
	vec_uint4 window = spu_add(spu_add(magnitude, spu_sl(magnitude, shifts1)), spu_sl(magnitude, shifts2));
	vec_float4 quotient = spu_sel(window_float(window, exponents), spu_splats(1.0f), spu_cmpgt(magnitude, largest));

	return (vec_float4)spu_or((vec_uint4)quotient, spu_and((vec_uint4)value, spu_splats(0x80000000u)));
}

/* four big-endian 16-bit words, sign-extended */
static inline vec_int4 element_shorts(const unsigned char *p)
{
	static const vec_uchar16 spread = { 0, 1, 0x80, 0x80, 2, 3, 0x80, 0x80, 4, 5, 0x80, 0x80, 6, 7, 0x80, 0x80 };

	return spu_rlmaska((vec_int4)load_pattern(p, spread), -16);
}

/* n / 32767, and -1 for -32768 (d3d8_rsx.c's clamp: no other quotient is under -1) */
static inline vec_float4 element_normshorts(const unsigned char *p, vec_uint4 mask)
{
	vec_float4 value = divide_repeating(element_shorts(p), spu_splats(15u), spu_splats(32u), spu_splats(129u),
		spu_splats(32767u));

	return spu_sel(defaults, value, mask);
}

/* the decals' word: its low 16 bits, then its high, each over 32767 with no clamp (-32768 too: its window is exact) */
static inline vec_float4 element_texcoord_word(const unsigned char *p)
{
	static const vec_uchar16 halves =
	{
		2, 3, 0x80, 0x80, 0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	};
	vec_float4 value = divide_repeating(spu_rlmaska((vec_int4)load_pattern(p, halves), -16), spu_splats(15u),
		spu_splats(32u), spu_splats(129u), spu_splats(~0u));

	return spu_sel(defaults, value, lanes[2]);
}

/* 11, 11 and 10 signed bits, over 1023, 1023 and 511 */
static inline vec_float4 element_normpacked3(const unsigned char *p)
{
	static const vec_uchar16 splat = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };
	static const vec_uint4 left = { 21, 10, 0, 0 };
	static const vec_int4 right = { -21, -21, -22, 0 };
	static const vec_uint4 shifts1 = { 10, 10, 9, 32 }, shifts2 = { 20, 20, 18, 32 }, exponents = { 129, 129, 132, 129 };
	vec_int4 value = spu_rlmaska((vec_int4)spu_sl((vec_uint4)load_pattern(p, splat), left), right);

	return spu_sel(defaults, divide_repeating(value, shifts1, shifts2, exponents, spu_splats(~0u)), lanes[3]);
}

/* b / 255 for the bytes that replicate puts four times in each lane (the window: 4 copies of 8 bits) */
static inline vec_float4 element_bytes(const unsigned char *p, vec_uchar16 replicate)
{
	return window_float((vec_uint4)load_pattern(p, replicate), spu_splats(127u));
}

/* the n elements of a slot at data, into every slots-th float4 of out; four vertices at a time, their work interleaved */
#define CONVERT_EACH(element) \
	do \
	{ \
		for (vertex = 0; vertex + 4 <= n; vertex += 4, data += 4 * stride, out += 4 * slots) \
		{ \
			vec_float4 v0 = element(data), v1 = element(data + stride); \
			vec_float4 v2 = element(data + 2 * stride), v3 = element(data + 3 * stride); \
			out[0] = v0; \
			out[slots] = v1; \
			out[2 * slots] = v2; \
			out[3 * slots] = v3; \
		} \
		for (; vertex < n; vertex++, data += stride, out += slots) \
			out[0] = element(data); \
	} \
	while (0)

static void slot_convert(uint32_t type, const unsigned char *data, uint32_t stride, uint32_t n, vec_float4 *out,
	uint32_t slots)
{
	/* the word ARGB: its bytes A R G B */
	static const vec_uchar16 color = { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 0, 0, 0, 0 };
	static const vec_uchar16 bytes = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
	vec_uint4 mask = lanes[type == SPU_VERTICES_FLOAT2H ? 3 : type >> 4 <= 4 ? type >> 4 : 0];
	uint32_t vertex;

#define FLOATS(p) spu_sel(defaults, (vec_float4)load_pattern(p, byte_order), mask)
#define SHORTS(p) spu_sel(defaults, spu_convtf(element_shorts(p), 0), mask)
#define NORMSHORTS(p) element_normshorts(p, mask)
#define COLOR(p) element_bytes(p, color)
#define BYTES(p) spu_sel(defaults, element_bytes(p, bytes), mask)
#define DEFAULTS(p) defaults
	switch (type)
	{
	case SPU_VERTICES_FLOAT1:
	case SPU_VERTICES_FLOAT2:
	case SPU_VERTICES_FLOAT3:
	case SPU_VERTICES_FLOAT4:
	case SPU_VERTICES_FLOAT2H:
		CONVERT_EACH(FLOATS);
		break;
	case SPU_VERTICES_SHORT1:
	case SPU_VERTICES_SHORT2:
	case SPU_VERTICES_SHORT3:
	case SPU_VERTICES_SHORT4:
		CONVERT_EACH(SHORTS);
		break;
	case SPU_VERTICES_NORMSHORT1:
	case SPU_VERTICES_NORMSHORT2:
	case SPU_VERTICES_NORMSHORT3:
	case SPU_VERTICES_NORMSHORT4:
		CONVERT_EACH(NORMSHORTS);
		break;
	case SPU_VERTICES_NORMPACKED3:
		CONVERT_EACH(element_normpacked3);
		break;
	case SPU_VERTICES_TEXCOORD_WORD:
		CONVERT_EACH(element_texcoord_word);
		break;
	case SPU_VERTICES_D3DCOLOR:
		CONVERT_EACH(COLOR);
		break;
	case SPU_VERTICES_PBYTE1:
	case SPU_VERTICES_PBYTE2:
	case SPU_VERTICES_PBYTE3:
	case SPU_VERTICES_PBYTE4:
		CONVERT_EACH(BYTES);
		break;
	default:
		CONVERT_EACH(DEFAULTS);
		break;
	}
#undef FLOATS
#undef SHORTS
#undef NORMSHORTS
#undef COLOR
#undef BYTES
#undef DEFAULTS
}

/* ---------- tasks */

/* the task's slots with bytes in groups that one fetch serves (a stream's elements together) */
static uint32_t groups_make(const spu_vertices_task *task, slot_group *groups)
{
	uint32_t count = 0, slot, group, start = 0, end = 0;

	for (slot = 0; slot < task->slot_count; slot++)
	{
		const spu_vertices_slot *element = &task->slots[slot];

		if (!element->bytes)
			continue;
		for (group = 0; group < count; group++)
		{
			start = element->source < groups[group].base ? element->source : groups[group].base;
			end = element->source + element->bytes > groups[group].base + groups[group].span ?
				element->source + element->bytes : groups[group].base + groups[group].span;
			if (element->stride && groups[group].stride == element->stride && end - start <= GROUP_SPAN)
				break;
		}
		if (group == count)
		{
			groups[group].base = element->source;
			groups[group].stride = element->stride;
			groups[group].span = element->bytes;
			groups[group].slots = 0;
			count++;
		}
		else
		{
			groups[group].base = start;
			groups[group].span = end - start;
		}
		groups[group].slots |= 1u << slot;
	}
	return count;
}

/* a group's bytes for vertices [first, first + n), started into input buffer k */
static const unsigned char *group_fetch(const slot_group *group, uint32_t first, uint32_t n, uint32_t k)
{
	return fetch_start(k, group->base + first * group->stride, (n - 1) * group->stride + group->span);
}

/* a pass at a time: its float4s in one output buffer, each group's bytes in one input buffer; each fetch runs while the
group before it converts */
static void convert_task(const spu_vertices_task *task)
{
	slot_group groups[SPU_VERTICES_SLOTS];
	uint32_t slots = task->slot_count, count = task->count, first = 0, limit, slot, group, group_count, k = 0;
	const unsigned char *data = NULL;

	if (!slots || slots > SPU_VERTICES_SLOTS || !count)
		return;
	group_count = groups_make(task, groups);
	limit = OUTPUT_QUADS / slots;
	for (group = 0; group < group_count; group++)
	{
		uint32_t stride = groups[group].stride, most = FETCH_BYTES - 16 - groups[group].span;

		if (stride && most / stride + 1 < limit)
			limit = most / stride + 1;
	}
	if (group_count)
		data = group_fetch(&groups[0], 0, count < limit ? count : limit, k);
	while (first < count)
	{
		uint32_t n = count - first < limit ? count - first : limit;
		vec_float4 *out = output_take();

		for (slot = 0; slot < slots; slot++)
		{
			if (!task->slots[slot].bytes)
				slot_convert(0, NULL, 0, n, out + slot, slots);
		}
		for (group = 0; group < group_count; group++)
		{
			uint32_t next_group = group + 1 < group_count ? group + 1 : 0;
			uint32_t next_first = group + 1 < group_count ? first : first + n;
			const unsigned char *next = NULL;

			if (next_first < count)
				next = group_fetch(&groups[next_group], next_first,
					count - next_first < limit ? count - next_first : limit, k ^ 1);
			echo.input_wait_ticks += timed_wait(1 << (TAG_INPUT + k));
			for (slot = 0; slot < slots; slot++)
			{
				const spu_vertices_slot *element = &task->slots[slot];

				if (groups[group].slots & (1u << slot))
					slot_convert(element->type, data + (element->source - groups[group].base), element->stride, n,
						out + slot, slots);
			}
			data = next;
			k ^= 1;
		}
		output_put(task->output + first * slots * 16, n * slots * 16);
		first += n;
	}
	echo.elements += count * slots;
}

/* (a last batch's whole lines: its output's room is rounded up for them) */
static void indices_task(const spu_vertices_task *task)
{
	vec_ushort8 subtract = spu_splats((unsigned short)task->subtract);
	uint32_t count = task->count, first = 0, k = 0;
	const unsigned char *data;

	if (!count)
		return;
	data = fetch_start(k, task->slots[0].source, (count < INDEX_BATCH ? count : INDEX_BATCH) * 2);
	while (first < count)
	{
		uint32_t n = count - first < INDEX_BATCH ? count - first : INDEX_BATCH, quads = (n + 7) / 8, quad;
		uint32_t next_first = first + n;
		const unsigned char *next = NULL;
		vec_ushort8 *out = (vec_ushort8 *)output_take();

		if (next_first < count)
			next = fetch_start(k ^ 1, task->slots[0].source + next_first * 2,
				(count - next_first < INDEX_BATCH ? count - next_first : INDEX_BATCH) * 2);
		echo.input_wait_ticks += timed_wait(1 << (TAG_INPUT + k));
		for (quad = 0; quad < quads; quad++)
			out[quad] = spu_sub((vec_ushort8)load_pattern(data + quad * 16, byte_order), subtract);
		output_put(task->output + first * 2, quads * 16);
		data = next;
		k ^= 1;
		first = next_first;
	}
	echo.indices += count;
}

/* (ps3_gpu.c's copy on the PPE: the microcode, then each constant's value; a source at any address) */
static void fragment_task(const spu_vertices_task *task)
{
	const spu_vertices_fragment *fragment = (const spu_vertices_fragment *)task;
	uint32_t bytes = fragment->bytes, quads = bytes / 16, quad, index;
	const unsigned char *data;
	vec_uint4 *out;

	if (!quads || (bytes & 15) || bytes > SPU_VERTICES_FRAGMENT_BYTES ||
		fragment->occurrence_count > SPU_VERTICES_OCCURRENCES)
		return;
	data = fetch_start(0, fragment->source, bytes);
	out = (vec_uint4 *)output_take();
	echo.input_wait_ticks += timed_wait(1 << TAG_INPUT);
	for (quad = 0; quad < quads; quad++)
		out[quad] = (vec_uint4)load_pattern(data + quad * 16, byte_order);
	for (index = 0; index < fragment->occurrence_count; index++)
	{
		uint32_t occurrence = fragment->occurrences[index], at = occurrence >> 4, value = occurrence & 15;

		if (at < quads && value < SPU_VERTICES_VALUES)
			out[at] = spu_rl(*(const vec_uint4 *)fragment->values[value], 16);
	}
	output_put(fragment->output, bytes);
}

/* ---------- textures (xbox_texture_layout.c's xgpu_texture_rsx_task_run, bit for bit) */

/* the last of an output buffer's put: its 16-byte part, then its tail of up to 15 bytes in naturally aligned pieces (the
output is 16-aligned where the buffer starts) */
static void output_put_exact(uint32_t ea, uint32_t bytes)
{
	uint32_t whole = bytes & ~15u, offset = whole, piece;

	if (whole)
		mfc_put(outputs[output_next], ea, whole, TAG_OUTPUT + output_next, 0, 0);
	for (piece = 8; piece; piece >>= 1)
	{
		if ((bytes - offset) & piece)
		{
			mfc_put((unsigned char *)outputs[output_next] + offset, ea + offset, piece, TAG_OUTPUT + output_next, 0, 0);
			offset += piece;
		}
	}
	task_buffers |= 1 << output_next;
	last_buffer = output_next;
	output_next ^= 1;
}

static inline uint32_t byte_at(const unsigned char *p)
{
	return p[0];
}

static inline uint32_t expand5(uint32_t v) { return (v << 3) | (v >> 2); }
static inline uint32_t expand6(uint32_t v) { return (v << 2) | (v >> 4); }
static inline uint32_t expand4(uint32_t v) { return v * 0x11; }

static inline uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/* convert_texel's word for a texel of the kinds a task takes (no palette, no YUV pairs) */
static uint32_t texel_argb(uint32_t kind, const unsigned char *s)
{
	uint32_t v16 = s[0] | (s[1] << 8);
	uint32_t v32 = v16 | (s[2] << 16) | ((uint32_t)s[3] << 24);

	switch (kind)
	{
	case SPU_TEXEL_A8R8G8B8: return v32;
	case SPU_TEXEL_X8R8G8B8: return v32 | 0xff000000u;
	case SPU_TEXEL_R5G6B5: return argb(255, expand5(v16 >> 11), expand6((v16 >> 5) & 0x3f), expand5(v16 & 0x1f));
	case SPU_TEXEL_A1R5G5B5: return argb((v16 & 0x8000) ? 255 : 0, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case SPU_TEXEL_X1R5G5B5: return argb(255, expand5((v16 >> 10) & 0x1f), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case SPU_TEXEL_A4R4G4B4: return argb(expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf), expand4(v16 & 0xf));
	case SPU_TEXEL_L8: return argb(255, s[0], s[0], s[0]);
	case SPU_TEXEL_AL8: return argb(s[0], s[0], s[0], s[0]);
	case SPU_TEXEL_A8: return argb(s[0], 255, 255, 255);
	case SPU_TEXEL_A8L8: return argb(s[1], s[0], s[0], s[0]);
	case SPU_TEXEL_P8: return argb(255, s[0], s[0], s[0]);
	case SPU_TEXEL_G8B8: return argb(255, s[0], s[1], 0);
	case SPU_TEXEL_R8B8: return argb(255, s[1], 0, s[0]);
	case SPU_TEXEL_R6G5B5: return argb(255, expand6(v16 >> 10), expand5((v16 >> 5) & 0x1f), expand5(v16 & 0x1f));
	case SPU_TEXEL_L16: return argb(255, s[1], s[1], s[1]);
	case SPU_TEXEL_V16U16: return argb(255, s[1], s[3], 0);
	case SPU_TEXEL_A8B8G8R8: return argb(s[3], s[0], s[1], s[2]);
	case SPU_TEXEL_B8G8R8A8: return argb(s[0], s[1], s[2], s[3]);
	case SPU_TEXEL_R8G8B8A8: return argb(s[0], s[3], s[2], s[1]);
	case SPU_TEXEL_R5G5B5A1: return argb((v16 & 1) ? 255 : 0, expand5(v16 >> 11), expand5((v16 >> 6) & 0x1f), expand5((v16 >> 1) & 0x1f));
	case SPU_TEXEL_R4G4B4A4: return argb(expand4(v16 & 0xf), expand4(v16 >> 12), expand4((v16 >> 8) & 0xf), expand4((v16 >> 4) & 0xf));
	case SPU_TEXEL_D24S8: return argb(255, s[3], s[3], s[3]);
	case SPU_TEXEL_D16: return argb(255, s[1], s[1], s[1]);
	default: return v32;
	}
}

/* n units from in to out; in and out 16-aligned, n whole quadwords of input for the vector forms (the rest is never
read: a buffer's last quadword holds the tail) */
static void units_convert(const spu_vertices_texture *task, const unsigned char *in, unsigned char *out, uint32_t n)
{
	static const vec_uchar16 swap16 = { 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14 };
	static const vec_uchar16 swap32 = { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };
	uint32_t bytes = n * task->in_bytes, quads = (bytes + 15) / 16, quad, unit;
	const vec_uchar16 *source = (const vec_uchar16 *)in;
	vec_uchar16 *destination = (vec_uchar16 *)out;

	if (task->conversion == SPU_TEXTURE_COPY)
	{
		for (quad = 0; quad < quads; quad++)
			destination[quad] = source[quad];
		return;
	}
	if (task->conversion == SPU_TEXTURE_SWAP16)
	{
		for (quad = 0; quad < quads; quad++)
			destination[quad] = spu_shuffle(source[quad], source[quad], swap16);
		return;
	}
	/* the Xbox's little-endian words as A8R8G8B8 words, alpha one for X8R8G8B8 */
	if (task->in_bytes == 4 && (task->texel_kind == SPU_TEXEL_A8R8G8B8 || task->texel_kind == SPU_TEXEL_X8R8G8B8))
	{
		vec_uint4 alpha = spu_splats(task->texel_kind == SPU_TEXEL_X8R8G8B8 ? 0xff000000u : 0u);

		for (quad = 0; quad < quads; quad++)
			destination[quad] = (vec_uchar16)spu_or((vec_uint4)spu_shuffle(source[quad], source[quad], swap32), alpha);
		return;
	}
	for (unit = 0; unit < n; unit++)
		((uint32_t *)out)[unit] = texel_argb(task->texel_kind, in + unit * task->in_bytes);
}

/* a stream of units in buffers' worth: each fetch runs while the one before converts */
static void texture_stream(const spu_vertices_texture *task, uint32_t source, uint32_t output, uint32_t units)
{
	/* units a pass takes: a whole number of quadwords in and out, both within a buffer */
	uint32_t per = (FETCH_BYTES & ~15u) / task->in_bytes, out_per = sizeof(outputs[0]) / task->out_bytes;
	uint32_t first = 0, k = 0;
	const unsigned char *data;

	if (out_per < per)
		per = out_per;
	per &= ~15u;
	data = fetch_start(k, source, (units < per ? units : per) * task->in_bytes);
	while (first < units)
	{
		uint32_t n = units - first < per ? units - first : per, next_first = first + n;
		const unsigned char *next = NULL;
		unsigned char *out = (unsigned char *)output_take();

		if (next_first < units)
		{
			next = fetch_start(k ^ 1, source + next_first * task->in_bytes,
				(units - next_first < per ? units - next_first : per) * task->in_bytes);
		}
		echo.input_wait_ticks += timed_wait(1 << (TAG_INPUT + k));
		units_convert(task, data, out, n);
		output_put_exact(output + first * task->out_bytes, n * task->out_bytes);
		data = next;
		k ^= 1;
		first = next_first;
	}
}

/* a linear level's rows: each row's units, then zero to its pitch, a row a buffer */
static void texture_rows(const spu_vertices_texture *task)
{
	uint32_t row, k = 0, row_bytes = task->units * task->in_bytes, data_bytes = task->units * task->out_bytes;
	const unsigned char *data = fetch_start(k, task->source, row_bytes);

	for (row = 0; row < task->rows; row++)
	{
		const unsigned char *next = NULL;
		unsigned char *out = (unsigned char *)output_take();
		uint32_t index;

		if (row + 1 < task->rows)
			next = fetch_start(k ^ 1, task->source + (row + 1) * task->source_pitch, row_bytes);
		echo.input_wait_ticks += timed_wait(1 << (TAG_INPUT + k));
		units_convert(task, data, out, task->units);
		for (index = data_bytes; index < task->output_pitch; index++)
			out[index] = 0;
		output_put(task->output + row * task->output_pitch, task->output_pitch);
		data = next;
		k ^= 1;
	}
}

static void texture_task(const spu_vertices_task *task)
{
	const spu_vertices_texture *texture = (const spu_vertices_texture *)task;

	if (!texture->units || !texture->in_bytes || !texture->out_bytes || (texture->source & 15) || (texture->output & 15))
		return;
	if (texture->rows)
	{
		if (texture->output_pitch > sizeof(outputs[0]) || (texture->output_pitch & 15) ||
			texture->units * texture->in_bytes > FETCH_BYTES || texture->units * texture->out_bytes > texture->output_pitch)
			return;
		texture_rows(texture);
		echo.texture_bytes += texture->rows * texture->output_pitch;
	}
	else
	{
		texture_stream(texture, texture->source, texture->output, texture->units);
		echo.texture_bytes += texture->units * texture->out_bytes;
	}
}

/* x * the FNV prime, each lane: x << 24 plus x * 0x193 from its halves' products (mod 2^32) */
static inline vec_uint4 times_prime(vec_uint4 x)
{
	static const vec_ushort8 low = { 0, 0x193, 0, 0x193, 0, 0x193, 0, 0x193 };

	return spu_add(spu_sl(x, 24), spu_add(spu_mulo((vec_ushort8)x, low), (vec_uint4)spu_mulh((vec_short8)x,
		(vec_short8)low)));
}

/* texture_hash: 64 bytes at a time into the 16 lanes, the rest a word and a byte at a time; the result's 16 bytes out */
static void hash_task(const spu_vertices_task *task)
{
	const spu_vertices_texture *hash = (const spu_vertices_texture *)task;
	uint32_t size = hash->units, per = FETCH_BYTES & ~63u, first = 0, k = 0, index;
	vec_uint4 lanes[4];
	uint32_t words[TEXTURE_HASH_LANES];
	uint64_t result;
	const unsigned char *data;
	uint32_t *out;

	if ((hash->source & 15) || (hash->output & 15))
		return;
	for (index = 0; index < 4; index++)
	{
		vec_uint4 lane = { 4 * index, 4 * index + 1, 4 * index + 2, 4 * index + 3 };

		lanes[index] = spu_xor(spu_splats(TEXTURE_HASH_BASIS), lane);
	}
	data = size ? fetch_start(k, hash->source, size < per ? size : per) : NULL;
	while (first < size)
	{
		uint32_t n = size - first < per ? size - first : per, next_first = first + n, groups = n / 64, group;
		const unsigned char *next = NULL;
		const vec_uint4 *quads = (const vec_uint4 *)data;

		if (next_first < size)
			next = fetch_start(k ^ 1, hash->source + next_first, size - next_first < per ? size - next_first : per);
		echo.input_wait_ticks += timed_wait(1 << (TAG_INPUT + k));
		for (group = 0; group < groups; group++, quads += 4)
		{
			lanes[0] = times_prime(spu_xor(lanes[0], quads[0]));
			lanes[1] = times_prime(spu_xor(lanes[1], quads[1]));
			lanes[2] = times_prime(spu_xor(lanes[2], quads[2]));
			lanes[3] = times_prime(spu_xor(lanes[3], quads[3]));
		}
		/* (only the last pass has a part of 64 bytes) */
		if (n & 63)
		{
			uint32_t word, words_left = (n & 63) / 4, byte;
			const unsigned char *rest = data + groups * 64;

			for (index = 0; index < 4; index++)
			{
				words[4 * index] = spu_extract(lanes[index], 0);
				words[4 * index + 1] = spu_extract(lanes[index], 1);
				words[4 * index + 2] = spu_extract(lanes[index], 2);
				words[4 * index + 3] = spu_extract(lanes[index], 3);
			}
			for (word = 0; word < words_left; word++)
			{
				uint32_t value = byte_at(rest + 4 * word) << 24 | byte_at(rest + 4 * word + 1) << 16 |
					byte_at(rest + 4 * word + 2) << 8 | byte_at(rest + 4 * word + 3);

				words[word] = (words[word] ^ value) * TEXTURE_HASH_PRIME;
			}
			for (byte = words_left * 4; byte < (n & 63); byte++)
				words[0] = (words[0] ^ byte_at(rest + byte)) * TEXTURE_HASH_PRIME;
			for (index = 0; index < 4; index++)
			{
				vec_uint4 lane = { words[4 * index], words[4 * index + 1], words[4 * index + 2], words[4 * index + 3] };

				lanes[index] = lane;
			}
		}
		data = next;
		k ^= 1;
		first = next_first;
	}
	for (index = 0; index < 4; index++)
	{
		words[4 * index] = spu_extract(lanes[index], 0);
		words[4 * index + 1] = spu_extract(lanes[index], 1);
		words[4 * index + 2] = spu_extract(lanes[index], 2);
		words[4 * index + 3] = spu_extract(lanes[index], 3);
	}
	result = texture_hash_finish(words, size);
	out = (uint32_t *)output_take();
	out[0] = (uint32_t)(result >> 32);
	out[1] = (uint32_t)result;
	out[2] = out[3] = 0;
	output_put(hash->output, 16);
	echo.hash_bytes += size;
}

/* each task's tail goes out behind its output (task_publish), while the next task runs */
static uint32_t run_tasks(uint32_t tail, uint32_t head)
{
	while (tail != head)
	{
		uint32_t index = tail & (SPU_VERTICES_RING - 1), count = head - tail, task;

		if (count > TASK_BATCH)
			count = TASK_BATCH;
		if (count > SPU_VERTICES_RING - index)
			count = SPU_VERTICES_RING - index;
		echo.stage = 1;
		mfc_get(tasks, setup.ring + index * sizeof(spu_vertices_task), count * sizeof(spu_vertices_task), TAG_TASKS,
			0, 0);
		echo.input_wait_ticks += timed_wait(1 << TAG_TASKS);
		for (task = 0; task < count; task++)
		{
			uint32_t start = spu_read_decrementer();

			echo.stage = 1 + tasks[task].kind;
			if (tasks[task].kind == SPU_VERTICES_CONVERT)
				convert_task(&tasks[task]);
			else if (tasks[task].kind == SPU_VERTICES_INDICES)
				indices_task(&tasks[task]);
			else if (tasks[task].kind == SPU_VERTICES_FRAGMENT)
				fragment_task(&tasks[task]);
			else if (tasks[task].kind == SPU_VERTICES_TEXTURE)
				texture_task(&tasks[task]);
			else if (tasks[task].kind == SPU_VERTICES_HASH)
				hash_task(&tasks[task]);
			echo.busy_ticks += start - spu_read_decrementer();
			echo.stage = 0;
			task_publish(++tail);
		}
	}
	return tail;
}

int main(uint64_t bell_address, uint64_t echo_address, uint64_t setup_address)
{
	uint32_t tail = 0;

	bell_ea = bell_address;
	echo_ea = echo_address;
	mfc_get(&setup, setup_address, sizeof(setup), TAG_TASKS, 0, 0);
	dma_wait(1 << TAG_TASKS);
	echo.state = SPU_VERTICES_RUNNING;
	echo.ls_address = (uint32_t)&echo;
	put_echo();
	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DECREMENTER_TICKS);
	for (;;)
	{
		uint32_t events;

		/* the check after each reservation closes the race with a store that lands before it; the tasks before a
		quit are done */
		mfc_getllar((void *)&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if (bell.head != tail)
		{
			tail = run_tasks(tail, bell.head);
			continue;
		}
		if (bell.quit)
			break;
		events = spu_read_event_status();
		spu_write_event_ack(events);
		if (events & MFC_LLR_LOST_EVENT)
			echo.wakes++;
		if (events & MFC_DECREMENTER_EVENT)
		{
			echo.timeouts++;
			spu_write_decrementer(DECREMENTER_TICKS);
		}
	}
	echo.state = SPU_VERTICES_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
