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
