/* SPU_GAME.C: the game thread's jobs (spu_game.h): the checkpoint's copies (exact); the particles' rays and the render's
jobs (render only: the SPU rounds toward zero), each after the game function it names. */

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <sys/spu_thread.h>

#include <stddef.h>
#include <string.h>

#include "spu_game.h"

#define TAG_JOBS 1
#define TAG_PARAMS 2
#define TAG_CACHE 3
#define TAG_BULK 4
#define TAG_OUTPUT 5
#define TAG_ECHO 6
/* and + 1 to + 3: the copies' buffers */
#define TAG_COPY 8
#define OUTPUT_TAGS ((1 << TAG_OUTPUT) | (15 << TAG_COPY))
/* 1 ms of the timebase: a wake if a reservation event is lost */
#define DECREMENTER_TICKS 79800
#define JOB_BATCH 8

#define NONE (-1)
#define LONG_MIN_BIT 0x80000000u
#define REAL_MAX 3.4028235e38f
#define REAL_EPSILON 0.0001f
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

/* the tag data cache: 2 ways of 128-byte lines */
#define CACHE_SETS 256
#define CACHE_WAYS 2

/* the jobs' LS, one job at a time */
#define SCRATCH_BYTES (96 * 1024)
#define COPY_CHUNK 16384
#define COPY_BUFFERS 4

static spu_game_bell bell;
static spu_game_echo echo;
static spu_game_setup setup;
static spu_game_job jobs[JOB_BATCH] __attribute__((aligned(128)));
static uint64_t bell_ea, echo_ea;
static uint32_t current_kind;

static unsigned char cache_lines[CACHE_SETS][CACHE_WAYS][128] __attribute__((aligned(128)));
static uint32_t cache_tags[CACHE_SETS][CACHE_WAYS];
static unsigned char cache_next[CACHE_SETS];
static uint32_t cache_generation;

static unsigned char scratch[SCRATCH_BYTES] __attribute__((aligned(128)));

/* ---------- DMA */

static void dma_wait(uint32_t tags)
{
	uint32_t start = spu_read_decrementer();

	mfc_write_tag_mask(tags);
	mfc_read_tag_status_all();
	echo.dma_ticks[current_kind] += start - spu_read_decrementer();
}

/* 16-aligned LS and EA, a 16-multiple size, in pieces of 16 KB */
static void dma_get(void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	unsigned char *to = ls;

	while (size)
	{
		uint32_t n = size < 16384 ? size : 16384;

		mfc_get(to, ea, n, tag, 0, 0);
		to += n;
		ea += n;
		size -= n;
	}
}

/* size bytes at ea into ls (room for size + 32), with the 16-byte lines that hold them; where ea's byte is */
static void *fetch(void *ls, uint32_t ea, uint32_t size)
{
	uint32_t first = ea & ~15u, end = (ea + size + 15) & ~15u;

	if (!size)
		return ls;
	dma_get(ls, first, end - first, TAG_BULK);
	dma_wait(1 << TAG_BULK);
	return (unsigned char *)ls + (ea & 15);
}

/* size bytes from ls to ea, whose low 4 bits are equal, without a byte outside them: the ends in 1, 2, 4 and 8 */
static void put_exact(const void *ls, uint32_t ea, uint32_t size, uint32_t tag)
{
	uint32_t from = (uint32_t)ls;

	while (size && (ea & 15))
	{
		uint32_t n = ea & -ea;

		while (n > size)
			n >>= 1;
		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
	while (size >= 16)
	{
		uint32_t n = size & ~15u;

		if (n > 16384)
			n = 16384;
		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
	while (size)
	{
		uint32_t n = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;

		mfc_put((void *)from, ea, n, tag, 0, 0);
		from += n;
		ea += n;
		size -= n;
	}
}

static uint32_t cache_hits;

static void put_echo(void)
{
	echo.cache_hits += cache_hits;
	cache_hits = 0;
	dma_wait(OUTPUT_TAGS);
	mfc_put(&echo, echo_ea, sizeof(echo), TAG_ECHO, 0, 0);
	dma_wait(1 << TAG_ECHO);
}

/* ---------- the tag data cache (the structure bsp's blocks, the tables the PPE keeps for a bsp) */

static void cache_check(uint32_t generation)
{
	if (generation != cache_generation)
	{
		memset(cache_tags, 0, sizeof(cache_tags));
		cache_generation = generation;
	}
}

/* (cache_hits: the hits since the last echo) */
static unsigned char cache_bounce[128] __attribute__((aligned(16)));

static __attribute__((noinline)) const unsigned char *cache_fill(uint32_t line, uint32_t set)
{
	uint32_t way = cache_next[set];

	cache_next[set] = way ^ 1;
	mfc_get(cache_lines[set][way], line, 128, TAG_CACHE, 0, 0);
	dma_wait(1 << TAG_CACHE);
	cache_tags[set][way] = line | 1;
	echo.cache_misses++;
	return cache_lines[set][way];
}

static inline const unsigned char *cache_line(uint32_t line)
{
	uint32_t set = ((line >> 7) ^ (line >> 15)) & (CACHE_SETS - 1);

	uint32_t hit0 = cache_tags[set][0] == (line | 1), hit1 = cache_tags[set][1] == (line | 1);

	if (__builtin_expect(hit0 | hit1, 1))
	{
		cache_hits++;
		return cache_lines[set][hit1];
	}
	return cache_fill(line, set);
}

/* an element across two lines, copied with its address's alignment */
static __attribute__((noinline)) const unsigned char *cache_straddle(uint32_t ea, uint32_t size)
{
	uint32_t offset = ea & 127, first = 128 - offset;
	unsigned char *out = cache_bounce + (ea & 15);

	memcpy(out, cache_line(ea & ~127u) + offset, first);
	memcpy(out + first, cache_line((ea & ~127u) + 128), size - first);
	return out;
}

/* size bytes (at most 64) at ea, in place: read them before the next cache access */
static inline const unsigned char *cache_ptr(uint32_t ea, uint32_t size)
{
	uint32_t offset = ea & 127;

	if (__builtin_expect(offset + size <= 128, 1))
		return cache_line(ea & ~127u) + offset;
	return cache_straddle(ea, size);
}

/* size bytes (a multiple of 4, at most 64) at a 4-aligned ea into words */
static inline void cache_read(void *out, uint32_t ea, uint32_t size)
{
	const uint32_t *from = (const uint32_t *)cache_ptr(ea, size);
	uint32_t *to = out, index;

	for (index = 0; index < size / 4; index++)
		to[index] = from[index];
}

static inline uint32_t cache_u32(uint32_t ea)
{
	return *(const uint32_t *)(cache_line(ea & ~127u) + (ea & 127));
}

static inline int16_t cache_s16(uint32_t ea)
{
	return *(const int16_t *)(cache_line(ea & ~127u) + (ea & 127));
}

/* ---------- maths (render only: the SPU's own roundings) */

static inline float absolute(float x)
{
	return x < 0.0f ? -x : x;
}

static inline float reciprocal(float x)
{
	vec_float4 v = spu_promote(x, 0), r = spu_re(v);

	r = spu_madd(spu_nmsub(v, r, spu_splats(1.0f)), r, r);
	return spu_extract(r, 0);
}

static inline float divide(float a, float b)
{
	return a * reciprocal(b);
}

static inline float square_root(float x)
{
	vec_float4 v, r;

	if (!(x > 0.0f))
		return 0.0f;
	v = spu_promote(x, 0);
	r = spu_rsqrte(v);
	/* one Newton step: r (3 - x r r) / 2 */
	r = spu_mul(spu_mul(r, spu_splats(0.5f)), spu_nmsub(spu_mul(v, r), r, spu_splats(3.0f)));
	return spu_extract(spu_mul(v, r), 0);
}

static inline float truncate_real(float x)
{
	return absolute(x) < 8388608.0f ? (float)(int32_t)x : x;
}

/* C's fmod: the sign of x, less than |w| */
static float real_modulo(float x, float w)
{
	if (w == 0.0f)
		return 0.0f;
	return x - w * truncate_real(divide(x, w));
}

/* sine and cosine of x (radians) by a polynomial after reduction to [-pi/4, pi/4] */
static void sine_cosine(float x, float *sine, float *cosine)
{
	float quarter = x * 0.63661977f, k = truncate_real(quarter + (quarter < 0.0f ? -0.5f : 0.5f));
	float r = (x - k * 1.5703125f) - k * 4.8382673e-4f, r2 = r * r;
	float s = r * (1.0f + r2 * (-0.16666654f + r2 * (8.3321608e-3f + r2 * -1.9515296e-4f)));
	float c = 1.0f + r2 * (-0.5f + r2 * (4.1666645e-2f + r2 * (-1.3887316e-3f + r2 * 2.4433157e-5f)));
	int32_t quadrant = (int32_t)k & 3;

	*sine = quadrant == 0 ? s : quadrant == 1 ? c : quadrant == 2 ? -s : -c;
	*cosine = quadrant == 0 ? c : quadrant == 1 ? -s : quadrant == 2 ? -c : s;
}

static inline float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* normalize3d: the magnitude, 0 (the vector kept) under the epsilon */
static float normalize(float *v)
{
	float magnitude = square_root(dot3(v, v));

	if (absolute(magnitude) < REAL_EPSILON)
		return 0.0f;
	{
		float scale = reciprocal(magnitude);

		v[0] *= scale;
		v[1] *= scale;
		v[2] *= scale;
	}
	return magnitude;
}

static inline void cross3(const float *a, const float *b, float *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static inline uint32_t bit_test(const uint32_t *bits, uint32_t index)
{
	return (bits[index >> 5] >> (index & 31)) & 1;
}

/* ---------- COPY, ZEROES, ZERO: the checkpoint's state (game_state_xbox.c) */

static void copy_job(uint32_t source, uint32_t target, uint32_t size)
{
	uint32_t offset = 0, k = 0;

	/* each buffer's get after its last put, its put after its get: fences in the buffer's tag */
	while (offset < size)
	{
		uint32_t n = size - offset < COPY_CHUNK ? size - offset : COPY_CHUNK;
		void *buffer = scratch + k * COPY_CHUNK;

		mfc_getf(buffer, source + offset, n, TAG_COPY + k, 0, 0);
		mfc_putf(buffer, target + offset, n, TAG_COPY + k, 0, 0);
		offset += n;
		k = (k + 1) & (COPY_BUFFERS - 1);
	}
	dma_wait(15 << TAG_COPY);
}

static uint32_t zeroes_job(uint32_t source, uint32_t size)
{
	vec_uint4 any = spu_splats(0u);
	uint32_t issued = 0, done = 0, k = 0;

	while (issued < size && issued < COPY_BUFFERS * COPY_CHUNK)
	{
		uint32_t n = size - issued < COPY_CHUNK ? size - issued : COPY_CHUNK;

		mfc_get(scratch + (issued / COPY_CHUNK) * COPY_CHUNK, source + issued, n, TAG_COPY + issued / COPY_CHUNK, 0, 0);
		issued += n;
	}
	while (done < size)
	{
		uint32_t n = size - done < COPY_CHUNK ? size - done : COPY_CHUNK, quad;
		const vec_uint4 *data = (const vec_uint4 *)(scratch + k * COPY_CHUNK);

		dma_wait(1 << (TAG_COPY + k));
		for (quad = 0; quad < n / 16; quad++)
			any = spu_or(any, data[quad]);
		done += n;
		if (issued < size)
		{
			uint32_t next = size - issued < COPY_CHUNK ? size - issued : COPY_CHUNK;

			mfc_get(scratch + k * COPY_CHUNK, source + issued, next, TAG_COPY + k, 0, 0);
			issued += next;
		}
		k = (k + 1) & (COPY_BUFFERS - 1);
	}
	any = spu_or(any, spu_rlqwbyte(any, 8));
	any = spu_or(any, spu_rlqwbyte(any, 4));
	return spu_extract(any, 0) == 0;
}

static void zero_job(uint32_t target, uint32_t size)
{
	uint32_t offset = 0;

	memset(scratch, 0, COPY_CHUNK);
	while (offset < size)
	{
		uint32_t n = size - offset < COPY_CHUNK ? size - offset : COPY_CHUNK;

		mfc_put(scratch, target + offset, n, TAG_COPY, 0, 0);
		offset += n;
	}
	dma_wait(1 << TAG_COPY);
}

static void answer_put(uint32_t ea, uint32_t status, uint32_t value)
{
	static spu_game_answer answer;

	dma_wait(1 << TAG_OUTPUT);
	answer.status = status;
	answer.value = value;
	mfc_putf(&answer, ea, sizeof(answer), TAG_OUTPUT, 0, 0);
}

/* ---------- SHADOW: structure_visibility_build_surfaces (structure_visibility.c) */

#define INTERSECTION_OUT 0
#define INTERSECTION_SPANNING 1
#define INTERSECTION_IN 2
/* the game's own limit; past the SPU's stack (retail: 66 deep) the job is refused */
#define TRAVERSAL_DEPTH 1024
#define SHADOW_STACK 192
#define MAXIMUM_SURFACES 0x20000
/* the triangles' staging after a list's chunk (24 KB and an address's offset) */
#define SHADOW_TRIANGLES 57600

typedef struct
{
	spu_game_shadow p;
	uint32_t found;
	uint32_t refused;
	uint32_t *flags;
	const uint32_t *environment;
	int32_t *out;
	unsigned char *chunk;
} shadow_state;

/* a node of the traversal, or a leaf to visit, with its parent node's bounds */
typedef struct
{
	int32_t index;
	int16_t depth;
	int16_t leaf;
	int16_t intersection;
	int16_t plane_count;
	float bounds[6];
	uint32_t pad;
} shadow_entry;

static int32_t bounding_rectangles_intersect(const float *test, const float *rectangle)
{
	if (rectangle[1] < test[0] || rectangle[0] > test[1] || rectangle[3] < test[2] || rectangle[2] > test[3] ||
		rectangle[5] < test[4] || rectangle[4] > test[5])
		return INTERSECTION_OUT;
	if (test[0] < rectangle[0] || test[1] > rectangle[1] || test[2] < rectangle[2] || test[3] > rectangle[3] ||
		test[4] < rectangle[4] || test[5] > rectangle[5])
		return INTERSECTION_SPANNING;
	return INTERSECTION_IN;
}

/* the corners four at a time (lanes: x0 y0, x1 y0, x0 y1, x1 y1; at z0, then z1), each the sum's order */
static int32_t planes_intersect_rectangle(const float *bounds, int32_t plane_count, const float (*planes)[4])
{
	vec_float4 xs = { bounds[0], bounds[1], bounds[0], bounds[1] }, ys = { bounds[2], bounds[2], bounds[3], bounds[3] };
	vec_float4 z0 = spu_splats(bounds[4]), z1 = spu_splats(bounds[5]), zero = spu_splats(0.0f);
	vec_uint4 behind = spu_splats(0u);
	int32_t plane_index;

	for (plane_index = 0; plane_index < plane_count; plane_index++)
	{
		const float *n = planes[plane_index];
		vec_float4 c = spu_splats(n[2]), d = spu_splats(n[3]);
		vec_float4 xy = spu_add(spu_mul(spu_splats(n[0]), xs), spu_mul(spu_splats(n[1]), ys));
		vec_uint4 m0 = spu_cmpgt(zero, spu_sub(spu_add(xy, spu_mul(c, z0)), d));
		vec_uint4 m1 = spu_cmpgt(zero, spu_sub(spu_add(xy, spu_mul(c, z1)), d));

		if (spu_extract(spu_gather(spu_and(m0, m1)), 0) == 0xF)
			return INTERSECTION_OUT;
		behind = spu_or(behind, spu_or(m0, m1));
	}
	return spu_extract(spu_gather(behind), 0) ? INTERSECTION_SPANNING : INTERSECTION_IN;
}

static inline float dequantize(float minimum, float maximum, uint32_t value)
{
	return value == 255 ? maximum : (maximum - minimum) * ((float)value * (1.0f / 255.0f)) + minimum;
}

static void dequantize_rectangle(const float *parent, const unsigned char *bytes, float *out)
{
	uint32_t axis;

	for (axis = 0; axis < 6; axis++)
		out[axis] = dequantize(parent[axis & ~1u], parent[axis | 1], bytes[axis]);
}

/* a surface found, as the traversals add one: FALSE when the room is full */
static inline int shadow_add(shadow_state *s, int32_t surface_index)
{
	if ((uint32_t)surface_index >= (uint32_t)s->p.structure.surface_count)
		return 1;
	if (bit_test(s->environment, surface_index) && !bit_test(s->flags, surface_index))
	{
		if (s->found >= (uint32_t)s->p.maximum_count)
			return 0;
		s->flags[surface_index >> 5] |= 1u << (surface_index & 31);
		s->out[s->found++] = surface_index;
	}
	return 1;
}

static void shadow_leaf(shadow_state *s, int32_t leaf_index, const float *parent_bounds, int32_t plane_count,
	int32_t intersection)
{
	const spu_game_structure *structure = &s->p.structure;
	const unsigned char *leaf;
	int32_t first, count, reference;
	float leaf_bounds[6];

	leaf_index &= 0x7FFFFFFF;
	if (leaf_index >= structure->leaf_count)
		return;
	leaf = cache_ptr(structure->leaves + leaf_index * 16, 16);
	count = *(const int16_t *)(leaf + 10);
	first = *(const int32_t *)(leaf + 12);
	if (first < 0 || count < 0 || first > structure->surface_reference_count - count)
		return;
	dequantize_rectangle(parent_bounds, leaf, leaf_bounds);
	if (intersection != INTERSECTION_IN)
	{
		int32_t rectangle = bounding_rectangles_intersect(leaf_bounds, s->p.bounds);
		int32_t planes = planes_intersect_rectangle(leaf_bounds, plane_count, (const float (*)[4])s->p.planes);

		intersection = rectangle < planes ? rectangle : planes;
	}
	if (!intersection)
		return;
	for (reference = first; reference < first + count; reference++)
	{
		if (!shadow_add(s, (int32_t)cache_u32(structure->surface_references + reference * 8)))
			return;
	}
}

static int shadow_push(shadow_entry *stack, int32_t *top, int32_t index, int32_t depth, int32_t leaf,
	int32_t intersection, int32_t plane_count, const float *bounds)
{
	shadow_entry *entry;

	if (*top >= SHADOW_STACK)
		return 0;
	entry = &stack[(*top)++];
	entry->index = index;
	entry->depth = (int16_t)depth;
	entry->leaf = (int16_t)leaf;
	entry->intersection = (int16_t)intersection;
	entry->plane_count = (int16_t)plane_count;
	memcpy(entry->bounds, bounds, sizeof(entry->bounds));
	return 1;
}

/* structure_visibility_build_surfaces_traverse_node's recursion on a stack of its own: a node's second child pushed
first, so its first child's nodes and leaves come off before it, as the recursion visits them */
static void shadow_nodes(shadow_state *s)
{
	const spu_game_structure *structure = &s->p.structure;
	shadow_entry stack[SHADOW_STACK];
	int32_t top = 0;

	shadow_push(stack, &top, 0, 0, 0, INTERSECTION_SPANNING, s->p.plane_count, structure->world_bounds);
	while (top > 0 && s->found < (uint32_t)s->p.maximum_count)
	{
		shadow_entry entry = stack[--top];
		int32_t intersection = entry.intersection, plane_count = entry.plane_count, side;
		uint32_t node[3];
		float node_bounds[6], plane[4], distance;
		int32_t descend[2];

		if (entry.leaf)
		{
			shadow_leaf(s, entry.index, entry.bounds, plane_count, intersection);
			continue;
		}
		if (entry.index < 0 || entry.index >= structure->node_count || entry.index >= structure->bsp3d_node_count ||
			entry.depth >= TRAVERSAL_DEPTH)
			continue;
		dequantize_rectangle(entry.bounds, cache_ptr(structure->nodes + entry.index * 6, 6), node_bounds);
		if (intersection != INTERSECTION_IN)
		{
			int32_t rectangle = bounding_rectangles_intersect(node_bounds, s->p.bounds);
			int32_t planes = rectangle ?
				planes_intersect_rectangle(node_bounds, plane_count, (const float (*)[4])s->p.planes) :
				INTERSECTION_OUT;

			if (planes == INTERSECTION_IN)
				plane_count = 0;
			intersection = rectangle < planes ? rectangle : planes;
		}
		if (!intersection)
			continue;
		cache_read(node, structure->bsp3d_nodes + entry.index * 12, 12);
		if ((int32_t)node[0] < 0 || (int32_t)node[0] >= structure->plane_count)
			continue;
		cache_read(plane, structure->planes + node[0] * 16, 16);
		distance = s->p.center[0] * plane[0] + s->p.center[1] * plane[1] + s->p.center[2] * plane[2] - plane[3];
		descend[0] = distance < s->p.radius;
		descend[1] = distance > -s->p.radius;
		for (side = 1; side >= 0; side--)
		{
			int32_t child = (int32_t)node[1 + side];

			if (!descend[side] || child == NONE)
				continue;
			if (!shadow_push(stack, &top, child, entry.depth + 1, (child & LONG_MIN_BIT) != 0, intersection,
				plane_count, node_bounds))
			{
				s->refused = 1;
				return;
			}
		}
	}
}

static void shadow_clusters(shadow_state *s)
{
	const spu_game_structure *structure = &s->p.structure;
	int32_t list_index;

	for (list_index = 0; list_index < s->p.cluster_count && s->found < (uint32_t)s->p.maximum_count; list_index++)
	{
		int32_t cluster_index = s->p.clusters[list_index], subcluster_index;
		uint32_t cluster = structure->clusters + cluster_index * structure->cluster_size, block[2];

		if (cluster_index < 0 || cluster_index >= structure->cluster_count)
			continue;
		cache_read(block, cluster + structure->cluster_subclusters_offset, 8);
		for (subcluster_index = 0; subcluster_index < (int32_t)block[0] &&
			s->found < (uint32_t)s->p.maximum_count; subcluster_index++)
		{
			uint32_t subcluster = block[1] + subcluster_index * structure->subcluster_size, list[2], done;
			float bounds[6];
			int32_t intersection;

			cache_read(bounds, subcluster, 24);
			intersection = bounding_rectangles_intersect(s->p.bounds, bounds) ?
				planes_intersect_rectangle(bounds, s->p.plane_count, (const float (*)[4])s->p.planes) :
				INTERSECTION_OUT;
			if (!intersection)
				continue;
			cache_read(list, subcluster + structure->subcluster_surfaces_offset, 8);
			for (done = 0; done < list[0];)
			{
				uint32_t n = list[0] - done < 1024 ? list[0] - done : 1024, index;
				const int32_t *indices = fetch(s->chunk, list[1] + done * 4, n * 4);

				for (index = 0; index < n; index++)
				{
					if (!shadow_add(s, indices[index]))
						break;
				}
				if (index < n)
					break;
				done += n;
			}
		}
	}
}

static uint32_t flags_ascending(uint32_t *flags, uint32_t words, int32_t *out);

/* the found surfaces ascending in place of the walk's order, their triangles put (put_exact) behind them */
static void shadow_triangles(shadow_state *s, uint32_t words)
{
	int16_t *triangles = (int16_t *)(scratch + SHADOW_TRIANGLES + (s->p.triangles & 15));
	uint32_t n, found = flags_ascending(s->flags, words, s->out);

	for (n = 0; n < found; n++)
	{
		const int16_t *surface = (const int16_t *)cache_ptr(s->p.structure.surfaces + s->out[n] * 6, 6);

		triangles[n * 3] = surface[0];
		triangles[n * 3 + 1] = surface[1];
		triangles[n * 3 + 2] = surface[2];
	}
	if (found)
		put_exact(triangles, s->p.triangles, found * 6, TAG_OUTPUT);
}

static void shadow_job(uint32_t params, uint32_t size)
{
	shadow_state *s = (shadow_state *)scratch;
	uint32_t words, bytes;

	if (size > sizeof(spu_game_shadow) || (params & 15))
	{
		echo.refused++;
		return;
	}
	memset(s, 0, sizeof(*s));
	dma_get(&s->p, params, (size + 15) & ~15u, TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	cache_check(s->p.structure.generation);
	words = ((uint32_t)s->p.structure.surface_count + 31) / 32;
	bytes = (words * 4 + 15) & ~15u;
	if (s->p.structure.surface_count < 0 || s->p.structure.surface_count > MAXIMUM_SURFACES ||
		s->p.maximum_count > 4096 || s->p.plane_count > SPU_GAME_SHADOW_PLANES ||
		s->p.cluster_count > SPU_GAME_SHADOW_CLUSTERS || (s->p.triangles & 1))
	{
		echo.refused++;
		answer_put(s->p.answer, SPU_GAME_REFUSED, 0);
		return;
	}
	/* after the state: the flags found (16 KB at most), the window's (with a fetch's slack), the indices found (16
	KB), a list's chunk (4 KB with slack) */
	s->flags = (uint32_t *)(scratch + 4096);
	s->out = (int32_t *)(scratch + 37120);
	s->chunk = scratch + 53504;
	memset(s->flags, 0, bytes);
	s->environment = fetch(scratch + 20480, s->p.environment_flags, words * 4);
	if (s->p.cluster_count >= 0)
		shadow_clusters(s);
	else
		shadow_nodes(s);
	if (s->refused)
	{
		echo.refused++;
		answer_put(s->p.answer, SPU_GAME_REFUSED, 0);
		return;
	}
	if (s->p.triangles)
		shadow_triangles(s, words);
	if (s->found)
		mfc_put(s->out, s->p.surfaces, (s->found * 4 + 15) & ~15u, TAG_OUTPUT, 0, 0);
	answer_put(s->p.answer, SPU_GAME_DONE, s->found);
}

/* ---------- WEATHER: weather_particle_system_render's update and sprites (weather_particle_systems.c, build_sprite of
render_sprite.c, point_physics_update of point_physics.c, the wind of wind.c) */

/* the weather's datum (weather_particle_systems.c's struct weather_particle, 0x54 bytes) */
typedef struct
{
	int16_t identifier;
	uint16_t pad02;
	float position[3];
	float velocity[3];
	float acceleration[3];
	int16_t sequence_index;
	uint16_t pad2A;
	float sprite_index;
	float rotation;
	float color[4];
	float radius;
	float rotation_rate;
	float animation_rate;
	int32_t next_particle_index;
} weather_particle;

typedef char weather_particle_size_check[sizeof(weather_particle) == 0x54 ? 1 : -1];

/* the point physics' and sprites' flags and modes */
#define PHYSICS_SIMPLE_WIND 3
#define PHYSICS_DAMPED_WIND 4
#define PHYSICS_NO_GRAVITY 5
#define SPRITE_NORMAL 0
#define SPRITE_PARALLEL 1
#define SPRITE_PERPENDICULAR 2
#define DIRECTION_FROM_ACCELERATION 1

/* the weather job's LS after its parameters */
#define WEATHER_PARTICLES 16384
#define WEATHER_SPRITES 61440
#define WEATHER_PLANES 70144
#define WEATHER_PLANES_MAXIMUM 256
#define WEATHER_STAGING 74240
#define WEATHER_STAGING_SPRITES 21
#define WEATHER_VARIANCE 90624
#define WEATHER_PARTICLE_BYTES (WEATHER_SPRITES - WEATHER_PARTICLES - 32)

typedef struct
{
	spu_game_weather *p;
	unsigned char *particles;
	const spu_game_weather_sprites *table;
	uint32_t table_ea;
	const float *variance;
	float (*planes)[4];
	int32_t plane_first[SPU_GAME_WEATHER_POLYHEDRA];
	uint32_t seed;
} weather_state;

/* seed_random and its users (random_math.c) on the SPU's own seed */
static uint32_t seed_random(uint32_t *seed)
{
	*seed = *seed * 1664525u + 1013904223u;
	return *seed >> 16;
}

static int32_t seed_random_range(uint32_t *seed, int32_t lower, int32_t upper)
{
	return lower + (int32_t)(((uint32_t)(upper - lower) * seed_random(seed)) >> 16);
}

static float real_seed_random_range(uint32_t *seed, float lower, float upper)
{
	return lower + (upper - lower) * ((float)seed_random(seed) * (1.0f / 65535.0f));
}

static void seed_random_direction(const spu_game_weather *p, uint32_t *seed, float *direction)
{
	int32_t index = seed_random_range(seed, 0, p->direction_count);

	cache_read(direction, p->directions + index * 12, 12);
}

static weather_particle *weather_get(weather_state *w, int32_t particle_index)
{
	uint32_t absolute = (uint32_t)particle_index & 0xFFFF;

	if (particle_index == NONE || absolute >= (uint32_t)w->p->particle_maximum)
		return NULL;
	return (weather_particle *)(w->particles + absolute * w->p->particle_size);
}

static void weather_table(weather_state *w, const spu_game_weather_type *type)
{
	if (w->table_ea != type->sprites)
	{
		w->table = fetch(scratch + WEATHER_SPRITES, type->sprites, type->sprites_size);
		w->table_ea = type->sprites;
	}
}

/* scenario_get_current_from_weather_palette with wind_variance_get */
static void weather_wind(const weather_state *w, const float *position, uint32_t physics_flags, float *wind)
{
	static const float axis_scale[3] = { 0.1f, 0.2f, 0.07f };
	const spu_game_weather *p = w->p;
	float weight, magnitude, variance[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t axis;

	if (!p->wind_valid)
	{
		wind[0] = wind[1] = wind[2] = 0.0f;
		return;
	}
	weight = (physics_flags & (1u << PHYSICS_SIMPLE_WIND)) ? 0.0f : p->wind_variation_weight;
	magnitude = p->wind_variation_weight * p->wind_velocity * (1.0f / 3.0f);
	for (axis = 0; axis < 3; axis++)
	{
		union
		{
			float value;
			uint32_t bits;
		} key;
		const float *sample;

		key.value = absolute((axis_scale[axis] * p->wind_time * p->wind_variation_rate + position[axis]) * 8.0f) +
			8388608.0f;
		sample = w->variance + (axis * 64 + (key.bits & 0x3F)) * 3;
		variance[0] += sample[0];
		variance[1] += sample[1];
		variance[2] += sample[2];
	}
	for (axis = 0; axis < 3; axis++)
	{
		wind[axis] = p->wind_velocity3d[axis] * (1.0f - weight) + variance[axis] * magnitude;
		if (physics_flags & (1u << PHYSICS_DAMPED_WIND))
			wind[axis] *= 1.0f - p->wind_damping;
	}
}

/* weather_particle_update_physics: the acceleration, point_physics_update with no collisions, the drift, the wrap */
static void weather_physics(weather_state *w, const spu_game_weather_type *type, weather_particle *particle,
	uint32_t absolute_index)
{
	const spu_game_weather *p = w->p;
	float dt = p->dt, radius = particle->radius, wind[3], direction[3];
	uint32_t seed = absolute_index, axis;
	int accelerates = type->acceleration_bounds[0] != 0.0f || type->acceleration_bounds[1] != 0.0f;

	if (accelerates && p->update_ticks > 0)
	{
		float magnitude = normalize(particle->acceleration), inverse = 1.0f - type->turning_rate;

		magnitude += real_seed_random_range(&w->seed, -type->change_rate, type->change_rate);
		magnitude = PIN(magnitude, type->acceleration_bounds[0], type->acceleration_bounds[1]);
		seed_random_direction(p, &w->seed, direction);
		for (axis = 0; axis < 3; axis++)
		{
			particle->acceleration[axis] = (direction[axis] * type->turning_rate +
				inverse * particle->acceleration[axis]) * magnitude;
		}
	}
	if (accelerates)
	{
		for (axis = 0; axis < 3; axis++)
			particle->velocity[axis] = particle->acceleration[axis] * dt + particle->velocity[axis];
	}
	if (!(radius >= 0.0f))
		radius = 0.0f;
	if (dt != 0.0f)
	{
		float mass = type->mass_over_radius_cubed, buoyancy, friction, t, radius_squared = radius * radius;

		weather_wind(w, particle->position, type->physics_flags, wind);
		if (p->under_water)
		{
			mass += p->water_mass;
			buoyancy = type->water_buoyancy_scale;
			friction = type->water_friction * radius_squared;
		}
		else
		{
			mass += p->air_mass;
			buoyancy = type->air_buoyancy_scale;
			friction = type->air_friction * radius_squared;
		}
		mass = mass * (radius_squared * radius);
		if (type->physics_flags & (1u << PHYSICS_NO_GRAVITY))
			buoyancy = 0.0f;
		particle->velocity[2] = p->gravity * buoyancy * dt + particle->velocity[2];
		if (mass == 0.0f)
		{
			t = friction == 0.0f ? 0.0f : 1.0f;
		}
		else
		{
			t = divide(dt, mass) * friction;
			t = PIN(t, 0.0f, 1.0f);
		}
		for (axis = 0; axis < 3; axis++)
		{
			particle->velocity[axis] += (wind[axis] - particle->velocity[axis]) * t;
			particle->position[axis] += particle->velocity[axis] * dt;
		}
	}
	seed_random_direction(p, &seed, direction);
	for (axis = 0; axis < 3; axis++)
	{
		float scale = 0.001f * dt * 30.0f, x = direction[axis] * scale + particle->position[axis];
		float wrapped = real_modulo(x, type->box_width);

		particle->position[axis] = wrapped + (x < 0.0f ? type->box_width : 0.0f);
	}
}

static void weather_update(weather_state *w, const spu_game_weather_type *type)
{
	const spu_game_weather *p = w->p;
	int32_t particle_index = type->first_particle_index, steps = 0;

	weather_table(w, type);
	while (particle_index != NONE && steps++ < p->particle_maximum)
	{
		weather_particle *particle = weather_get(w, particle_index);
		int32_t sequence;
		float count;

		if (!particle)
			break;
		sequence = particle->sequence_index;
		count = sequence >= 0 && sequence < w->table->sequence_count ? (float)w->table->sprite_count[sequence] : 0.0f;
		particle->sprite_index = real_modulo(particle->sprite_index + particle->animation_rate * p->dt, count);
		particle->rotation += ((particle_index & 1) ? -1.0f : 1.0f) * particle->rotation_rate * p->dt;
		weather_physics(w, type, particle, (uint32_t)particle_index & 0xFFFF);
		particle_index = particle->next_particle_index;
	}
}

/* real_argb_color_to_pixel32, the channels from 0 to 1 */
static uint32_t color_pixel(const float *color)
{
	uint32_t channel[4], index;

	for (index = 0; index < 4; index++)
	{
		float value = color[index] * 255.0f + 0.5f;

		channel[index] = value > 0.0f ? (uint32_t)(int32_t)value : 0;
	}
	return channel[3] | (channel[2] << 8) | (channel[1] << 16) | (channel[0] << 24);
}

/* render_frustum_cube_view_fraction */
static float cube_view_fraction(const float *projection, const float *bounds)
{
	float inverse_z0, inverse_z1, left, bottom, right, top, fraction, a, b;

	if (bounds[4] >= 0.0f)
		return 0.0f;
	if (bounds[5] >= 0.0f)
		return 1.0f;
	inverse_z0 = reciprocal(bounds[4]);
	inverse_z1 = reciprocal(bounds[5]);
	a = (bounds[4] * projection[1] + bounds[0] * projection[0]) * -inverse_z0;
	b = (bounds[5] * projection[1] + bounds[0] * projection[0]) * -inverse_z1;
	left = a < b ? a : b;
	left = left > -1.0f ? left : -1.0f;
	a = (bounds[4] * projection[3] + bounds[2] * projection[2]) * -inverse_z0;
	b = (bounds[5] * projection[3] + bounds[2] * projection[2]) * -inverse_z1;
	bottom = a < b ? a : b;
	bottom = bottom > -1.0f ? bottom : -1.0f;
	a = (bounds[4] * projection[1] + bounds[1] * projection[0]) * -inverse_z0;
	b = (bounds[5] * projection[1] + bounds[1] * projection[0]) * -inverse_z1;
	right = a > b ? a : b;
	right = right < 1.0f ? right : 1.0f;
	a = (bounds[4] * projection[3] + bounds[3] * projection[2]) * -inverse_z0;
	b = (bounds[5] * projection[3] + bounds[3] * projection[2]) * -inverse_z1;
	top = a > b ? a : b;
	top = top < 1.0f ? top : 1.0f;
	fraction = (right - left) * (top - bottom) * 0.25f;
	return fraction > 0.0f ? fraction : 0.0f;
}

typedef struct
{
	unsigned char *staging;
	uint32_t vertices;
	int32_t count;
	int32_t flushed;
	/* the order of the slot's first sprite (when build_sprite_get_group makes its group), NONE: none */
	int32_t order;
} weather_slot;

/* a build_sprites_begin call: the window's values, its sprites' table, its mode and shader's fade, its limit, its
counts and slots */
typedef struct
{
	const float *world_to_view;
	const float *projection;
	float projection_world_to_screen_x;
	const float *viewer_up;
	const float *viewer_forward;
	int32_t *big_sprite_count;
	float *coverage;
	const spu_game_weather_sprites *table;
	int32_t mode;
	int32_t fade_mode;
	int32_t fade_alpha;
	int32_t maximum;
	int32_t *sprite_count;
	float *centroid;
	int32_t orders;
	weather_slot *slots;
} sprite_call;

static void weather_flush(weather_slot *slot)
{
	int32_t sprites = slot->count - slot->flushed;

	if (sprites <= 0)
		return;
	put_exact(slot->staging, slot->vertices + slot->flushed * 96, sprites * 96, TAG_OUTPUT);
	dma_wait(1 << TAG_OUTPUT);
	slot->flushed = slot->count;
}

/* build_sprite in world space (render_sprite.c), the u and v mirrors its only flags */
static void sprite_build(sprite_call *c, int32_t sequence, int32_t sprite_index, const float *position,
	const float *direction_world, float rotation, float scale, const float *color, float fade, uint32_t mirrors)
{
	const spu_game_weather_sprites *table = c->table;
	const spu_game_weather_sprite *sprite;
	const float *m = c->world_to_view;
	float origin[3], direction[3], forward[3], left[3], up[3], x, y, z, sine = 0.0f, cosine = 1.0f, alpha;
	float bounds[6] = { REAL_MAX, -REAL_MAX, REAL_MAX, -REAL_MAX, REAL_MAX, -REAL_MAX };
	uint32_t pixel, vertex;
	weather_slot *slot;
	float *out;

	if (*c->sprite_count >= c->maximum || sequence < 0 || sequence >= table->sequence_count ||
		table->first_sprite[sequence] == NONE || sprite_index < 0 || sprite_index >= table->sprite_count[sequence])
		return;
	sprite = &table->sprites[table->first_sprite[sequence] + sprite_index];
	if (sprite->slot < 0 || sprite->slot >= SPU_GAME_WEATHER_GROUPS)
		return;
	slot = &c->slots[sprite->slot];
	/* (the group is made before its vertices are known) */
	if (slot->order == NONE)
		slot->order = c->orders++;
	if (!slot->vertices || slot->count >= c->maximum)
		return;
	if (rotation != 0.0f)
		sine_cosine(rotation, &sine, &cosine);
	x = position[0];
	y = position[1];
	z = position[2];
	if (m[0] != 1.0f)
	{
		x *= m[0];
		y *= m[0];
		z *= m[0];
	}
	origin[0] = m[7] * z + m[4] * y + m[1] * x + m[10];
	origin[1] = m[8] * z + m[5] * y + m[2] * x + m[11];
	origin[2] = m[9] * z + m[6] * y + m[3] * x + m[12];
	direction[0] = direction_world[0] * m[1] + direction_world[1] * m[4] + direction_world[2] * m[7];
	direction[1] = direction_world[0] * m[2] + direction_world[1] * m[5] + direction_world[2] * m[8];
	direction[2] = direction_world[0] * m[3] + direction_world[1] * m[6] + direction_world[2] * m[9];
	if (c->mode == SPRITE_PARALLEL)
	{
		memcpy(forward, direction, sizeof(forward));
		normalize(forward);
		cross3(origin, forward, left);
		normalize(left);
	}
	else if (c->mode == SPRITE_PERPENDICULAR)
	{
		const float *reference = c->viewer_up;
		float dot = dot3(direction, reference), along, vector_cross[3];

		if (dot * dot > dot3(direction, direction) * 0.99f)
			reference = c->viewer_forward;
		cross3(direction, reference, forward);
		normalize(forward);
		memcpy(up, direction, sizeof(up));
		normalize(up);
		/* rotate_vector_about_axis(left = forward, up, sine -1, cosine 0) */
		along = dot3(up, forward);
		vector_cross[0] = up[2] * forward[1] - forward[2] * up[1];
		vector_cross[1] = up[0] * forward[2] - forward[0] * up[2];
		vector_cross[2] = up[1] * forward[0] - forward[1] * up[0];
		left[0] = along * up[0] + vector_cross[0];
		left[1] = along * up[1] + vector_cross[1];
		left[2] = along * up[2] + vector_cross[2];
	}
	else
	{
		forward[0] = 1.0f;
		forward[1] = forward[2] = 0.0f;
		left[0] = left[2] = 0.0f;
		left[1] = 1.0f;
	}
	if (c->mode == SPRITE_NORMAL && scale == 0.0f)
		scale = -divide(origin[2], c->projection_world_to_screen_x);
	scale = table->widths[sprite->slot] * scale;
	if (c->fade_mode && c->mode != SPRITE_NORMAL)
	{
		float vertex_fade;

		cross3(forward, left, up);
		vertex_fade = absolute(divide(dot3(up, origin), square_root(dot3(origin, origin))));
		if (c->fade_mode == 2)
			vertex_fade = 1.0f - vertex_fade;
		fade *= vertex_fade;
	}
	pixel = color_pixel(color);
	alpha = c->fade_alpha ? fade * 255.0f : (float)(pixel >> 24) * fade;
	pixel = (pixel & 0x00FFFFFF) | (((uint32_t)(int32_t)alpha & 0xFF) << 24);
	out = (float *)(slot->staging + (slot->count - slot->flushed) * 96);
	for (vertex = 0; vertex < 4; vertex++, out += 6)
	{
		float u = (((vertex >> 1) ^ vertex) & 1) ? sprite->bounds[1] : sprite->bounds[0];
		float v = (vertex & 2) ? sprite->bounds[2] : sprite->bounds[3];
		float offset_x = u - (sprite->bounds[0] + sprite->registration[0]);
		float offset_y = (sprite->registration[1] + sprite->bounds[2]) - v;
		float sprite_x = offset_x * cosine - offset_y * sine, sprite_y = offset_y * cosine + offset_x * sine;
		uint32_t axis;

		if (mirrors & SPU_GAME_SPRITE_U_MIRROR)
			sprite_x = -sprite_x;
		if (mirrors & SPU_GAME_SPRITE_V_MIRROR)
			sprite_y = -sprite_y;
		for (axis = 0; axis < 3; axis++)
		{
			float point = (forward[axis] * sprite_x + left[axis] * sprite_y) * scale + origin[axis];

			out[axis] = point;
			if (point < bounds[axis * 2])
				bounds[axis * 2] = point;
			if (point > bounds[axis * 2 + 1])
				bounds[axis * 2 + 1] = point;
		}
		out[3] = u;
		out[4] = v;
		((uint32_t *)out)[5] = pixel;
	}
	c->centroid[0] += origin[0];
	c->centroid[1] += origin[1];
	c->centroid[2] += origin[2];
	slot->count++;
	(*c->sprite_count)++;
	{
		float coverage = cube_view_fraction(c->projection, bounds);

		*c->coverage += coverage;
		if (coverage > 0.5f && (*c->big_sprite_count)++ > 10)
		{
			slot->count--;
			(*c->sprite_count)--;
		}
	}
	if (slot->count - slot->flushed == WEATHER_STAGING_SPRITES)
		weather_flush(slot);
}

/* weather_particle_system_render's sprites: each particle in the first box it is visible in, out of the polyhedra */
static void weather_render(weather_state *w, spu_game_weather_type *type)
{
	spu_game_weather *p = w->p;
	static const float global_up[3] = { 0.0f, 0.0f, 1.0f };
	weather_slot slots[SPU_GAME_WEATHER_GROUPS];
	sprite_call call;
	int32_t particle_index = type->first_particle_index, steps = 0, index, plane_total = 0;
	float far_clip = type->distance_fade[3] < type->box_width ? type->distance_fade[3] : type->box_width;

	if (!type->particle_count)
		return;
	weather_table(w, type);
	for (index = 0; index < type->polyhedron_count; index++)
	{
		w->plane_first[index] = plane_total;
		plane_total += type->polyhedron_plane_counts[index] > 0 ? type->polyhedron_plane_counts[index] : 0;
	}
	if (plane_total > WEATHER_PLANES_MAXIMUM)
		return;
	for (index = 0; index < type->polyhedron_count; index++)
	{
		int32_t count = type->polyhedron_plane_counts[index], done;
		unsigned char bounce[16 * 32 + 32] __attribute__((aligned(16)));

		for (done = 0; done < count; done += 32)
		{
			int32_t n = count - done < 32 ? count - done : 32;

			memcpy(w->planes[w->plane_first[index] + done],
				fetch(bounce, type->polyhedron_planes[index] + done * 16, n * 16), n * 16);
		}
	}
	for (index = 0; index < SPU_GAME_WEATHER_GROUPS; index++)
	{
		slots[index].staging = scratch + WEATHER_STAGING + index * 2048 + (type->vertices[index] & 15);
		slots[index].vertices = type->vertices[index];
		slots[index].count = 0;
		slots[index].flushed = 0;
		slots[index].order = NONE;
	}
	call.world_to_view = p->world_to_view;
	call.projection = p->projection;
	call.projection_world_to_screen_x = p->projection_world_to_screen_x;
	call.viewer_up = p->viewer_up;
	call.viewer_forward = p->viewer_forward;
	call.big_sprite_count = &p->big_sprite_count;
	call.coverage = &p->coverage;
	call.table = w->table;
	call.mode = type->render_mode;
	call.fade_mode = type->fade_mode;
	call.fade_alpha = type->fade_alpha;
	call.maximum = type->particle_count;
	call.sprite_count = &type->sprite_count;
	call.centroid = type->centroid;
	call.orders = 0;
	call.slots = slots;
	while (particle_index != NONE && steps++ < p->particle_maximum)
	{
		weather_particle *particle = weather_get(w, particle_index);
		float evaluations[5], position[3], depth;
		int32_t plane, box, polyhedron;

		if (!particle)
			break;
		particle_index = particle->next_particle_index;
		for (plane = 0; plane < 5; plane++)
			evaluations[plane] = dot3(type->clip_planes[plane], particle->position) - type->clip_planes[plane][3];
		for (box = 0; box < type->box_count; box++)
		{
			for (plane = 0; plane < 5; plane++)
			{
				if (!(type->box_distances[box][plane] + evaluations[plane] < 0.0f))
					break;
			}
			if (plane == 5)
				break;
		}
		if (box >= type->box_count)
			continue;
		position[0] = type->boxes[box][0] + particle->position[0];
		position[1] = type->boxes[box][1] + particle->position[1];
		position[2] = type->boxes[box][2] + particle->position[2];
		depth = (position[0] - p->camera_position[0]) * p->camera_forward[0] +
			(position[1] - p->camera_position[1]) * p->camera_forward[1] +
			(position[2] - p->camera_position[2]) * p->camera_forward[2];
		if (!(depth > type->distance_fade[0] && depth < far_clip))
			continue;
		for (polyhedron = 0; polyhedron < type->polyhedron_count; polyhedron++)
		{
			const float (*planes)[4] = (const float (*)[4])w->planes + w->plane_first[polyhedron];

			for (plane = 0; plane < type->polyhedron_plane_counts[polyhedron]; plane++)
			{
				if (dot3(planes[plane], position) - planes[plane][3] < 0.0f)
					break;
			}
			if (plane == type->polyhedron_plane_counts[polyhedron])
				break;
		}
		if (polyhedron == type->polyhedron_count)
		{
			float fade_in = divide(depth - type->distance_fade[0], type->distance_fade[1] - type->distance_fade[0]);
			float fade_out = divide(depth - type->distance_fade[2], far_clip - type->distance_fade[2]);
			const float *direction = type->direction_source == DIRECTION_FROM_ACCELERATION ?
				particle->acceleration : particle->velocity;

			fade_in = PIN(fade_in, 0.0f, 1.0f);
			fade_out = 1.0f - PIN(fade_out, 0.0f, 1.0f);
			if (type->render_mode != SPRITE_NORMAL && dot3(direction, direction) == 0.0f)
				direction = global_up;
			sprite_build(&call, particle->sequence_index, (int16_t)(int32_t)particle->sprite_index, position, direction,
				particle->rotation, 2.0f * particle->radius * type->one_over_sprite_width, particle->color,
				fade_in * fade_out, 0);
		}
	}
	for (index = 0; index < SPU_GAME_WEATHER_GROUPS; index++)
	{
		weather_flush(&slots[index]);
		type->slot_sprites[index] = slots[index].count;
	}
}

/* the answers go back in the parameters: each type's sprites, and type_count as -1 less it once done */
static void weather_job(uint32_t params, uint32_t size)
{
	weather_state state;
	uint32_t bytes, index;

	if (size > sizeof(spu_game_weather) || (params & 15))
	{
		echo.refused++;
		return;
	}
	memset(&state, 0, sizeof(state));
	state.p = (spu_game_weather *)scratch;
	dma_get(state.p, params, (size + 15) & ~15u, TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	bytes = (uint32_t)state.p->particle_maximum * (uint32_t)state.p->particle_size;
	if (state.p->particle_size != (int32_t)sizeof(weather_particle) || bytes > WEATHER_PARTICLE_BYTES ||
		state.p->type_count < 0 || state.p->type_count > SPU_GAME_WEATHER_TYPES || state.p->direction_count <= 0)
	{
		echo.refused++;
		return;
	}
	for (index = 0; index < (uint32_t)state.p->type_count; index++)
	{
		if (state.p->types[index].sprites_size > sizeof(spu_game_weather_sprites) ||
			state.p->types[index].polyhedron_count > SPU_GAME_WEATHER_POLYHEDRA ||
			state.p->types[index].box_count > SPU_GAME_WEATHER_BOXES)
		{
			echo.refused++;
			return;
		}
	}
	state.particles = fetch(scratch + WEATHER_PARTICLES, state.p->particles, bytes);
	if (state.p->wind_valid)
		state.variance = fetch(scratch + WEATHER_VARIANCE, state.p->wind_variance, 3 * 64 * 12);
	state.planes = (float (*)[4])(scratch + WEATHER_PLANES);
	state.seed = state.p->seed;
	for (index = 0; index < (uint32_t)state.p->type_count; index++)
		weather_update(&state, &state.p->types[index]);
	for (index = 0; index < (uint32_t)state.p->type_count; index++)
		weather_render(&state, &state.p->types[index]);
	put_exact(state.particles, state.p->particles, bytes, TAG_OUTPUT);
	state.p->type_count = -state.p->type_count - 1;
	mfc_putf(state.p, params, (size + 15) & ~15u, TAG_OUTPUT, 0, 0);
}

/* ---------- PARTICLES: point_physics_update's rays (point_physics.c) through collision_test_vector's structure and
media tests (collisions.c: collision_bsp_test_vector of collision_bsp.c, bsp2d_test_point, bsp3d_test_point) */

/* _collision_test_* */
#define TEST_FRONT_FACING 0
#define TEST_BACK_FACING 1
#define TEST_IGNORE_TWO_SIDED 2
#define TEST_IGNORE_INVISIBLE 3
#define TEST_IGNORE_BREAKABLE 4
#define TEST_STRUCTURE 5
#define TEST_MEDIA 6
/* _point_physics_collided_with_structure_bit, _water_bit */
#define COLLIDED_STRUCTURE 2
#define COLLIDED_WATER 3
/* collision_surface_flags, collision_leaf_flags, contents (collision_bsp.c) */
#define SURFACE_INVISIBLE 1
#define SURFACE_BREAKABLE 3
#define LEAF_TWO_SIDED 0
#define CONTENTS_UNKNOWN 0
#define CONTENTS_EMPTY 1
#define CONTENTS_SEMI_EMPTY 2
#define CONTENTS_SOLID 3
#define BSP3D_DEPTH 128
#define BSP2D_DEPTH 128
#define EDGES_PER_SURFACE 8
#define RESULT_STRUCTURE 0
#define RESULT_MEDIA 1
#define PARTICLES_BATCH 256

typedef struct
{
	const spu_game_particles *p;
	const uint32_t *breakable;
	uint32_t flags;
	const float *point;
	const float *vector;
	/* collision_bsp_test_vector_result's, and the context's */
	float t;
	int32_t plane_index;
	int32_t surface_index;
	int32_t plane_designator;
	uint32_t surface_flags;
	int32_t material_index;
	int32_t leaf_count;
	int32_t first_leaf;
	int32_t last_leaf;
	int32_t last_leaf_index;
	uint32_t last_contents;
	int32_t last_plane_index;
	int32_t nodes_left;
} ray_state;

typedef struct
{
	int32_t type;
	float t;
	float plane[4];
	int32_t material_type;
	int32_t leaf_index;
	int32_t cluster_index;
	float point[3];
} ray_result;

static const unsigned char projection_mappings[3][2][2] =
{
	{ { 2, 1 }, { 1, 2 } },
	{ { 0, 2 }, { 2, 0 } },
	{ { 1, 0 }, { 0, 1 } },
};

static int32_t bsp2d_test_point(const spu_game_particles *p, int32_t node_index, const float *point)
{
	int32_t depth = 0;

	while (!(node_index & LONG_MIN_BIT))
	{
		float node[5];

		if (node_index >= p->bsp2d_node_count || depth++ >= BSP2D_DEPTH)
			return NONE;
		cache_read(node, p->bsp2d_nodes + node_index * 20, 20);
		node_index = ((const int32_t *)node)[3 + ((node[0] * point[0] + node[1] * point[1]) - node[2] >= 0.0f)];
	}
	return node_index != NONE ? node_index & 0x7FFFFFFF : NONE;
}

static int edge_ring_continues(const spu_game_particles *p, int32_t edge_index, int32_t edge_count)
{
	return edge_index >= 0 && edge_index < p->edge_count && edge_count < EDGES_PER_SURFACE;
}

/* collision_surface_test_point */
static int surface_test_point(const ray_state *r, int32_t surface_index, int32_t projection, int32_t sign,
	const float *point)
{
	const spu_game_particles *p = r->p;
	const unsigned char *mapping = projection_mappings[projection][sign];
	int32_t surface[3], edge_index, edge_count = 0;

	if (surface_index < 0 || surface_index >= p->surface_count)
		return 0;
	cache_read(surface, p->surfaces + surface_index * 12, 12);
	if (!edge_ring_continues(p, surface[1], 0))
		return 0;
	if ((surface[2] >> 24) & (1 << SURFACE_BREAKABLE))
	{
		uint32_t breakable_index = (surface[2] >> 16) & 0xFF;

		if (breakable_index < (uint32_t)p->breakable_count && !bit_test(r->breakable, breakable_index))
			return 0;
	}
	edge_index = surface[1];
	do
	{
		int32_t edge[6], reverse;
		float vertex0[4], vertex1[4], point0[2], point1[2];

		cache_read(edge, p->edges + edge_index * 24, 24);
		reverse = edge[5] == surface_index;
		if (edge[reverse] < 0 || edge[reverse] >= p->vertex_count || edge[!reverse] < 0 ||
			edge[!reverse] >= p->vertex_count)
			return 0;
		cache_read(vertex0, p->vertices + edge[reverse] * 16, 12);
		cache_read(vertex1, p->vertices + edge[!reverse] * 16, 12);
		point0[0] = vertex0[mapping[0]];
		point0[1] = vertex0[mapping[1]];
		point1[0] = vertex1[mapping[0]];
		point1[1] = vertex1[mapping[1]];
		if ((point[0] - point0[0]) * (point1[1] - point0[1]) - (point[1] - point0[1]) * (point1[0] - point0[0]) > 0.0f)
			return 0;
		edge_index = edge[2 + reverse];
	}
	while (edge_index != surface[1] && edge_ring_continues(p, edge_index, ++edge_count));
	return 1;
}

/* collision_leaf_test_vector */
static int32_t leaf_test_vector(const ray_state *r, int32_t leaf_index, int32_t plane_index, float t, int test_surface)
{
	const spu_game_particles *p = r->p;
	int32_t leaf[2], reference;

	cache_read(leaf, p->collision_leaves + leaf_index * 8, 8);
	for (reference = leaf[1]; reference < leaf[1] + (int16_t)leaf[0]; reference++)
	{
		int32_t entry[2];

		cache_read(entry, p->bsp2d_references + reference * 8, 8);
		if ((entry[0] & 0x7FFFFFFF) == plane_index && plane_index < p->structure.plane_count)
		{
			float plane[4], hit[3], point2d[2], absolute_i, absolute_j, absolute_k;
			int32_t projection, sign, surface_index;

			cache_read(plane, p->structure.planes + plane_index * 16, 16);
			absolute_i = absolute(plane[0]);
			absolute_j = absolute(plane[1]);
			absolute_k = absolute(plane[2]);
			if (absolute_k >= absolute_j && absolute_k >= absolute_i)
				projection = 2;
			else
				projection = absolute_j >= absolute_i ? 1 : 0;
			sign = (plane[projection] > 0.0f) != ((entry[0] & LONG_MIN_BIT) != 0);
			hit[0] = r->vector[0] * t + r->point[0];
			hit[1] = r->vector[1] * t + r->point[1];
			hit[2] = r->vector[2] * t + r->point[2];
			point2d[0] = hit[projection_mappings[projection][sign][0]];
			point2d[1] = hit[projection_mappings[projection][sign][1]];
			surface_index = bsp2d_test_point(p, entry[1], point2d);
			if (surface_index >= 0 && surface_index < p->surface_count &&
				(!test_surface || surface_test_point(r, surface_index, projection, sign, point2d)))
				return surface_index;
		}
	}
	return NONE;
}

static int valid_leaf(const spu_game_particles *p, int32_t leaf_index)
{
	int32_t leaf[2], count;

	if (leaf_index < 0 || leaf_index >= p->collision_leaf_count)
		return 0;
	cache_read(leaf, p->collision_leaves + leaf_index * 8, 8);
	count = (int16_t)leaf[0];
	return count == 0 || (leaf[1] >= 0 && count > 0 && leaf[1] <= p->bsp2d_reference_count - count);
}

/* collision_bsp_test_vector_recursive's leaf: 1 at a surface hit */
static __attribute__((noinline)) int ray_leaf(ray_state *r, int32_t node_index, float t0)
{
	const spu_game_particles *p = r->p;
	int32_t leaf_index = NONE, test_leaf_index;
	uint32_t contents = CONTENTS_SOLID;
	int test_surface = 0;

	if (node_index != NONE && valid_leaf(p, node_index & 0x7FFFFFFF))
	{
		leaf_index = node_index & 0x7FFFFFFF;
		contents = ((uint32_t)cache_s16(p->collision_leaves + leaf_index * 8) & (1 << LEAF_TWO_SIDED)) ?
			CONTENTS_SEMI_EMPTY : CONTENTS_EMPTY;
	}
	if ((r->flags & (1 << TEST_FRONT_FACING)) &&
		(r->last_contents == CONTENTS_EMPTY || r->last_contents == CONTENTS_SEMI_EMPTY) &&
		contents == CONTENTS_SOLID)
	{
		test_leaf_index = r->last_leaf_index;
	}
	else if ((r->flags & (1 << TEST_BACK_FACING)) && r->last_contents == CONTENTS_SOLID &&
		(contents == CONTENTS_EMPTY || contents == CONTENTS_SEMI_EMPTY))
	{
		test_leaf_index = leaf_index;
	}
	else if (!(r->flags & (1 << TEST_IGNORE_TWO_SIDED)) && r->last_contents == CONTENTS_SEMI_EMPTY &&
		contents == CONTENTS_SEMI_EMPTY)
	{
		test_leaf_index = (r->flags & (1 << TEST_FRONT_FACING)) ? r->last_leaf_index : leaf_index;
		test_surface = 1;
	}
	else
	{
		test_leaf_index = NONE;
	}
	if (test_leaf_index != NONE)
	{
		int32_t surface_index = leaf_test_vector(r, test_leaf_index, r->last_plane_index, t0, test_surface);

		if (surface_index != NONE)
		{
			int32_t surface[3];
			uint32_t flags;

			cache_read(surface, p->surfaces + surface_index * 12, 12);
			flags = (uint32_t)surface[2] >> 24;
			if ((!(flags & (1 << SURFACE_INVISIBLE)) || !(r->flags & (1 << TEST_IGNORE_INVISIBLE))) &&
				(!(flags & (1 << SURFACE_BREAKABLE)) || !(r->flags & (1 << TEST_IGNORE_BREAKABLE))))
			{
				r->t = t0;
				r->plane_index = r->last_plane_index;
				r->surface_index = surface_index;
				r->plane_designator = surface[0];
				r->surface_flags = flags;
				r->material_index = (int16_t)surface[2];
				return 1;
			}
		}
	}
	if (leaf_index != NONE)
	{
		if (!r->leaf_count++)
			r->first_leaf = leaf_index;
		r->last_leaf = leaf_index;
	}
	r->last_leaf_index = leaf_index;
	r->last_contents = contents;
	return 0;
}

/* a node or leaf of the traversal: its segment of the ray, its depth, the plane that becomes the last before it */
typedef struct
{
	int32_t node_index;
	float t0;
	float t1;
	int16_t depth;
	int16_t has_plane;
	int32_t plane;
	uint32_t pad[2];
} ray_entry;

/* collision_bsp_test_vector_recursive on a stack of its own: a spanned node's far side pushed first, with its plane
made the last when it comes off, after the near side's nodes and leaves, as the recursion visits them */
static int ray_traverse(ray_state *r, float t1)
{
	const spu_game_particles *p = r->p;
	ray_entry stack[BSP3D_DEPTH * 2 + 4];
	int32_t top = 1;

	stack[0].node_index = 0;
	stack[0].t0 = 0.0f;
	stack[0].t1 = t1;
	stack[0].depth = 0;
	stack[0].has_plane = 0;
	while (top > 0)
	{
		ray_entry entry = stack[--top];
		const int32_t *node;
		const float *plane;
		int32_t plane_index, children[2];
		float distance, dot, distance0, distance1;
		int reaches_back, reaches_front;

		r->last_plane_index = entry.has_plane ? entry.plane : r->last_plane_index;
		if (__builtin_expect(entry.node_index & LONG_MIN_BIT, 0))
		{
			if (ray_leaf(r, entry.node_index, entry.t0))
				return 1;
			continue;
		}
		/* (a node the test cannot enter is passed over, as the recursion's call returns; the conditions without
		branches: the SPU predicts none) */
		if (__builtin_expect((entry.node_index >= p->structure.bsp3d_node_count) | (entry.depth >= BSP3D_DEPTH) |
			(top > BSP3D_DEPTH * 2), 0))
			continue;
		if (__builtin_expect(--r->nodes_left < 0, 0))
			continue;
		node = (const int32_t *)cache_ptr(p->structure.bsp3d_nodes + entry.node_index * 12, 12);
		plane_index = node[0];
		children[0] = node[1];
		children[1] = node[2];
		if (__builtin_expect(plane_index < 0 || plane_index >= p->structure.plane_count, 0))
			continue;
		plane = (const float *)cache_ptr(p->structure.planes + plane_index * 16, 16);
		distance = plane[0] * r->point[0] + plane[1] * r->point[1] + plane[2] * r->point[2] - plane[3];
		dot = r->vector[0] * plane[0] + r->vector[1] * plane[1] + r->vector[2] * plane[2];
		distance0 = dot * entry.t0 + distance;
		distance1 = dot * entry.t1 + distance;
		reaches_back = (distance0 < 0.0f) | (distance1 < 0.0f);
		reaches_front = (distance0 >= 0.0f) | (distance1 >= 0.0f);
		if (reaches_back & reaches_front)
		{
			int front = dot > 0.0f;
			float t = -divide(distance, dot);

			stack[top].node_index = children[front];
			stack[top].t0 = t;
			stack[top].t1 = entry.t1;
			stack[top].depth = (int16_t)(entry.depth + 1);
			stack[top].has_plane = 1;
			stack[top].plane = plane_index;
			top++;
			stack[top].node_index = children[!front];
			stack[top].t0 = entry.t0;
			stack[top].t1 = t;
		}
		else
		{
			stack[top].node_index = children[reaches_front];
			stack[top].t0 = entry.t0;
			stack[top].t1 = entry.t1;
		}
		stack[top].depth = (int16_t)(entry.depth + 1);
		stack[top].has_plane = 0;
		top++;
	}
	return 0;
}

/* collision_cluster_index_from_leaf */
static int32_t cluster_from_leaf(const spu_game_particles *p, int32_t leaf_index)
{
	if (leaf_index == NONE || (leaf_index & 0x7FFFFFFF) >= p->structure.leaf_count)
		return NONE;
	return cache_s16(p->structure.leaves + (leaf_index & 0x7FFFFFFF) * 16 + 8);
}

/* scenario_location_from_point: bsp3d_test_point, then the structure leaf's cluster */
static void location_from_point(const spu_game_particles *p, const float *point, int32_t *leaf_index,
	int32_t *cluster_index)
{
	int32_t node_index = 0, depth = 0;

	do
	{
		int32_t node[3];
		float plane[4];

		if (node_index < 0 || node_index >= p->structure.bsp3d_node_count || depth++ >= BSP3D_DEPTH)
		{
			node_index = NONE;
			break;
		}
		cache_read(node, p->structure.bsp3d_nodes + node_index * 12, 12);
		if (node[0] < 0 || node[0] >= p->structure.plane_count)
		{
			node_index = NONE;
			break;
		}
		cache_read(plane, p->structure.planes + node[0] * 16, 16);
		node_index = node[1 + (dot3(plane, point) - plane[3] >= 0.0f)];
	}
	while (!(node_index & LONG_MIN_BIT));
	*leaf_index = node_index != NONE ? node_index & 0x7FFFFFFF : NONE;
	*cluster_index = NONE;
	if (*leaf_index == NONE)
		return;
	if (*leaf_index >= p->structure.leaf_count)
	{
		*leaf_index = NONE;
		return;
	}
	*cluster_index = cache_s16(p->structure.leaves + *leaf_index * 16 + 8);
	if (*cluster_index != NONE && (*cluster_index < 0 || *cluster_index >= p->structure.cluster_count))
	{
		*leaf_index = NONE;
		*cluster_index = NONE;
	}
}

/* collision_test_vector with the structure and media only (point physics never tests objects) */
static int particle_ray(const spu_game_particles *p, const uint32_t *breakable, uint32_t flags, const float *point,
	const float *vector, ray_result *c)
{
	ray_state r;
	int hit = 0;

	c->type = NONE;
	c->t = 1.0f;
	c->leaf_index = NONE;
	c->cluster_index = NONE;
	if (!(flags & ((1 << TEST_STRUCTURE) | (1 << TEST_MEDIA))))
	{
		c->point[0] = point[0] + vector[0];
		c->point[1] = point[1] + vector[1];
		c->point[2] = point[2] + vector[2];
		location_from_point(p, c->point, &c->leaf_index, &c->cluster_index);
		return 0;
	}
	if (!(flags & ((1 << TEST_FRONT_FACING) | (1 << TEST_BACK_FACING))))
		flags |= (1 << TEST_FRONT_FACING) | (1 << TEST_BACK_FACING);
	memset(&r, 0, sizeof(r));
	r.p = p;
	r.breakable = breakable;
	r.flags = flags & 0x1F;
	r.point = point;
	r.vector = vector;
	r.t = REAL_MAX;
	r.last_leaf_index = NONE;
	r.last_plane_index = NONE;
	r.last_contents = CONTENTS_UNKNOWN;
	r.nodes_left = p->structure.bsp3d_node_count;
	if (ray_traverse(&r, 1.0f) && (flags & (1 << TEST_STRUCTURE)))
	{
		c->t = r.t;
		c->type = RESULT_STRUCTURE;
		cache_read(c->plane, p->structure.planes + r.plane_index * 16, 16);
		if (r.plane_designator & LONG_MIN_BIT)
		{
			c->plane[0] = -c->plane[0];
			c->plane[1] = -c->plane[1];
			c->plane[2] = -c->plane[2];
			c->plane[3] = -c->plane[3];
		}
		c->material_type = r.material_index >= 0 && r.material_index < p->material_count ?
			cache_s16(p->material_types + r.material_index * 2) : NONE;
		hit = 1;
	}
	if (r.leaf_count > 0)
	{
		c->leaf_index = r.last_leaf;
		c->cluster_index = cluster_from_leaf(p, r.last_leaf);
	}
	if ((flags & (1 << TEST_MEDIA)) && c->cluster_index != NONE && c->cluster_index >= 0 &&
		c->cluster_index < p->structure.cluster_count)
	{
		spu_game_cluster_water water;

		cache_read(&water, p->cluster_water + c->cluster_index * sizeof(water), sizeof(water));
		if (water.valid)
		{
			float point_distance = dot3(water.plane, point) - water.plane[3], vector_dot = dot3(water.plane, vector);

			if ((point_distance > 0.0f) != (vector_dot > 0.0f) && absolute(point_distance) < absolute(vector_dot) &&
				absolute(vector_dot) >= REAL_EPSILON)
			{
				float t = -divide(point_distance, vector_dot);

				if (t < c->t)
				{
					c->t = t;
					memcpy(c->plane, water.plane, sizeof(c->plane));
					c->type = RESULT_MEDIA;
					if (point_distance < 0.0f)
					{
						c->plane[0] = -c->plane[0];
						c->plane[1] = -c->plane[1];
						c->plane[2] = -c->plane[2];
						c->plane[3] = -c->plane[3];
						c->material_type = p->water_material_type;
					}
					else
					{
						c->material_type = water.material_type;
					}
					hit = 1;
				}
			}
		}
	}
	if (!hit)
		c->t = 1.0f;
	c->point[0] = vector[0] * c->t + point[0];
	c->point[1] = vector[1] * c->t + point[1];
	c->point[2] = vector[2] * c->t + point[2];
	return hit;
}

/* point_physics_update's loop of up to 3 rays */
static void particle_rays(const spu_game_particles *p, const uint32_t *breakable, spu_game_particle *particle)
{
	uint32_t collision_flags = particle->flags, result = 0, axis, i;
	float dt = particle->dt, offset = particle->radius < 0.005f ? particle->radius : 0.005f;

	for (i = 0; dt != 0.0f && i < 3; i++)
	{
		float delta[3], dot, parallel[3];
		ray_result c;

		delta[0] = particle->velocity[0] * dt;
		delta[1] = particle->velocity[1] * dt;
		delta[2] = particle->velocity[2] * dt;
		if (!particle_ray(p, breakable, collision_flags, particle->position, delta, &c))
		{
			if (c.leaf_index != NONE)
			{
				particle->leaf_index = c.leaf_index;
				particle->cluster_index = (int16_t)c.cluster_index;
			}
			memcpy(particle->position, c.point, sizeof(particle->position));
			break;
		}
		result |= c.type == RESULT_MEDIA ? 1u << COLLIDED_WATER : c.type == RESULT_STRUCTURE ?
			1u << COLLIDED_STRUCTURE : 0;
		memcpy(particle->collision_normal, c.plane, sizeof(particle->collision_normal));
		particle->material_type = (int16_t)c.material_type;
		dot = dot3(particle->velocity, c.plane);
		for (axis = 0; axis < 3; axis++)
		{
			parallel[axis] = c.plane[axis] * dot;
			particle->velocity[axis] = (1.0f - particle->contact_friction) * (particle->velocity[axis] - parallel[axis]) -
				parallel[axis] * particle->elasticity;
		}
		if (c.leaf_index != NONE)
		{
			particle->leaf_index = c.leaf_index;
			particle->cluster_index = (int16_t)c.cluster_index;
		}
		for (axis = 0; axis < 3; axis++)
			particle->position[axis] = c.plane[axis] * offset + c.point[axis];
		dt -= c.t * dt;
	}
	particle->flags = result;
}

static void particles_job(uint32_t params, uint32_t size)
{
	spu_game_particles *p = (spu_game_particles *)scratch;
	const uint32_t *breakable;
	int32_t done;

	if (size != sizeof(spu_game_particles) || (params & 15))
	{
		echo.refused++;
		return;
	}
	dma_get(p, params, sizeof(*p), TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	if (p->particle_count < 0 || p->particle_count > SPU_GAME_PARTICLES_MAXIMUM || (p->particles & 15) ||
		p->breakable_count < 0 || p->breakable_count > 1024)
	{
		echo.refused++;
		answer_put(p->answer, SPU_GAME_REFUSED, 0);
		return;
	}
	cache_check(p->structure.generation);
	breakable = fetch(scratch + 256, p->breakable_flags, ((uint32_t)p->breakable_count + 31) / 32 * 4);
	for (done = 0; done < p->particle_count; done += PARTICLES_BATCH)
	{
		int32_t n = p->particle_count - done < PARTICLES_BATCH ? p->particle_count - done : PARTICLES_BATCH, index;
		spu_game_particle *batch = (spu_game_particle *)(scratch + 1024);

		dma_wait(1 << TAG_OUTPUT);
		dma_get(batch, p->particles + done * sizeof(spu_game_particle), n * sizeof(spu_game_particle), TAG_BULK);
		dma_wait(1 << TAG_BULK);
		for (index = 0; index < n; index++)
			particle_rays(p, breakable, &batch[index]);
		mfc_put(batch, p->particles + done * sizeof(spu_game_particle), n * sizeof(spu_game_particle), TAG_OUTPUT, 0,
			0);
	}
	answer_put(p->answer, SPU_GAME_DONE, (uint32_t)p->particle_count);
}

/* ---------- LIGHTS: each light's structure_visibility_build_surfaces (the SHADOW job's traversals), then
structure_render_build_dynamic_triangles' sort (the flags found, in order) and triangles (structure_render.c) */

/* the job's LS around the shadow state's: its parameters, a light, the triangles' staging */
#define LIGHTS_PARAMS 2048
#define LIGHTS_ENTRY 2304
#define LIGHTS_TRIANGLES 57600

/* the surfaces flagged, ascending, into out, the flags cleared (16-aligned, whole quadwords); their count */
static uint32_t flags_ascending(uint32_t *flags, uint32_t words, int32_t *out)
{
	vec_uint4 *quads = (vec_uint4 *)flags;
	uint32_t quad, found = 0;

	for (quad = 0; quad < (words + 3) / 4; quad++)
	{
		uint32_t lane;

		if (!spu_extract(spu_orx(quads[quad]), 0))
			continue;
		for (lane = 0; lane < 4; lane++)
		{
			uint32_t bits = flags[quad * 4 + lane];

			while (bits)
			{
				uint32_t bit = 31 - spu_extract(spu_cntlz(spu_promote(bits & -bits, 0)), 0);

				out[found++] = (int32_t)((quad * 4 + lane) * 32 + bit);
				bits &= bits - 1;
			}
		}
		quads[quad] = spu_splats(0u);
	}
	return found;
}

static void lights_job(uint32_t params, uint32_t size)
{
	shadow_state *s = (shadow_state *)scratch;
	spu_game_lights *p = (spu_game_lights *)(scratch + LIGHTS_PARAMS);
	spu_game_light *light = (spu_game_light *)(scratch + LIGHTS_ENTRY);
	int16_t *triangles = (int16_t *)(scratch + LIGHTS_TRIANGLES);
	uint32_t words, bytes, cursor = 0;
	int32_t index;

	if (size != sizeof(spu_game_lights) || (params & 15))
	{
		echo.refused++;
		return;
	}
	dma_get(p, params, sizeof(*p), TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	if (p->structure.surface_count < 0 || p->structure.surface_count > MAXIMUM_SURFACES || p->light_count < 0 ||
		p->light_count > SPU_GAME_LIGHTS_MAXIMUM || ((p->lights | p->indices | p->triangles) & 15) || p->arena_count < 0)
	{
		echo.refused++;
		answer_put(p->answer, SPU_GAME_REFUSED, 0);
		return;
	}
	memset(s, 0, sizeof(*s));
	s->p.structure = p->structure;
	s->p.maximum_count = SPU_GAME_LIGHT_SURFACES;
	cache_check(p->structure.generation);
	words = ((uint32_t)p->structure.surface_count + 31) / 32;
	bytes = (words * 4 + 15) & ~15u;
	/* (the shadow job's places: the flags found, the window's, the indices found, a list's chunk) */
	s->flags = (uint32_t *)(scratch + 4096);
	s->out = (int32_t *)(scratch + 37120);
	s->chunk = scratch + 53504;
	memset(s->flags, 0, bytes);
	s->environment = fetch(scratch + 20480, p->environment_flags, words * 4);
	for (index = 0; index < p->light_count; index++)
	{
		uint32_t found, rounded, n;

		/* the last light's outputs and answer out before their buffers are used again */
		dma_wait(1 << TAG_OUTPUT);
		dma_get(light, p->lights + index * sizeof(*light), sizeof(*light), TAG_PARAMS);
		dma_wait(1 << TAG_PARAMS);
		light->status = SPU_GAME_REFUSED;
		light->count = 0;
		light->first = 0;
		if (light->cluster_count <= SPU_GAME_LIGHT_CLUSTERS)
		{
			memcpy(s->p.center, light->center, sizeof(s->p.center));
			s->p.radius = light->radius;
			memcpy(s->p.bounds, light->bounds, sizeof(s->p.bounds));
			s->p.cluster_count = light->cluster_count;
			if (light->cluster_count > 0)
			{
				memcpy(s->p.clusters, fetch(s->chunk, light->clusters, (uint32_t)light->cluster_count * 2),
					(uint32_t)light->cluster_count * 2);
			}
			s->found = 0;
			s->refused = 0;
			if (s->p.cluster_count >= 0)
				shadow_clusters(s);
			else
				shadow_nodes(s);
			found = flags_ascending(s->flags, words, s->out);
			/* (each light's arena place at a multiple of 8 surfaces: 16-aligned indices and triangles) */
			rounded = (found + 7) & ~7u;
			if (!s->refused && cursor + rounded <= (uint32_t)p->arena_count)
			{
				for (n = 0; n < found; n++)
				{
					const int16_t *surface = (const int16_t *)cache_ptr(p->structure.surfaces + s->out[n] * 6, 6);

					triangles[n * 3] = surface[0];
					triangles[n * 3 + 1] = surface[1];
					triangles[n * 3 + 2] = surface[2];
				}
				if (rounded)
				{
					put_exact(s->out, p->indices + cursor * 4, rounded * 4, TAG_OUTPUT);
					put_exact(triangles, p->triangles + cursor * 6, rounded * 6, TAG_OUTPUT);
				}
				light->status = SPU_GAME_DONE;
				light->count = (int32_t)found;
				light->first = (int32_t)cursor;
				cursor += rounded;
			}
		}
		mfc_put(light, p->lights + index * sizeof(*light), sizeof(*light), TAG_OUTPUT, 0, 0);
	}
	answer_put(p->answer, SPU_GAME_DONE, (uint32_t)p->light_count);
}

/* ---------- VISIBILITY: structure_visibility_traverse_subclusters (structure_visibility.c, render_frustum_cube_visible of
render_cameras.c), then structure_render_dynamic_triangles_from_bitvector's surfaces and triangles (structure_render.c) */

#define VISIBILITY_FLAGS 12544
#define VISIBILITY_CHUNK 29184
#define VISIBILITY_INDICES 33536
#define VISIBILITY_TRIANGLES 37888
#define VISIBILITY_SURFACES 44288
#define VISIBILITY_BATCH 1024
/* the surfaces (6 bytes each) one fetch of 16 KB holds */
#define VISIBILITY_WINDOW 2730

/* render_frustum_cube_visible without the frustum against the cube: 0 when a side plane has the box's 8 corners out */
static int cluster_cube_visible(const spu_game_visibility_cluster *cluster, const float *b)
{
	const float *f = cluster->world_bounds;
	vec_float4 xs = { b[0], b[1], b[0], b[1] }, ys = { b[2], b[2], b[3], b[3] };
	vec_float4 z0 = spu_splats(b[4]), z1 = spu_splats(b[5]), zero = spu_splats(0.0f);
	int32_t plane_index;

	if (f[1] < b[0] || f[3] < b[2] || f[5] < b[4] || f[0] > b[1] || f[2] > b[3] || f[4] > b[5])
		return 0;
	for (plane_index = 0; plane_index < 4; plane_index++)
	{
		const float *n = cluster->planes[plane_index];
		vec_float4 xy = spu_add(spu_mul(xs, spu_splats(n[0])), spu_mul(ys, spu_splats(n[1])));
		vec_uint4 m0 = spu_cmpgt(spu_sub(spu_add(xy, spu_mul(z0, spu_splats(n[2]))), spu_splats(n[3])), zero);
		vec_uint4 m1 = spu_cmpgt(spu_sub(spu_add(xy, spu_mul(z1, spu_splats(n[2]))), spu_splats(n[3])), zero);

		if (spu_extract(spu_gather(spu_and(m0, m1)), 0) == 0xF)
			return 0;
	}
	return 1;
}

/* the batch's surfaces and triangles out, at done in the outputs */
static void visibility_flush(const spu_game_visibility *p, const int32_t *indices, const int16_t *triangles, uint32_t done,
	uint32_t n)
{
	put_exact(indices, p->indices + done * 4, n * 4, TAG_OUTPUT);
	put_exact(triangles, p->triangles + done * 6, n * 6, TAG_OUTPUT);
	dma_wait(1 << TAG_OUTPUT);
}

static void visibility_job(uint32_t params, uint32_t size)
{
	spu_game_visibility *p = (spu_game_visibility *)scratch;
	const spu_game_structure *structure = &p->structure;
	uint32_t *flags, words, pad_words, count = 0, maximum, quad, done = 0, n = 0, window_first = 0, window_count = 0;
	const vec_uint4 *quads = (const vec_uint4 *)(scratch + VISIBILITY_FLAGS);
	const unsigned char *window = NULL;
	int32_t *indices;
	int16_t *triangles;
	int32_t cluster_index;

	if (size != sizeof(spu_game_visibility) || (params & 15))
	{
		echo.refused++;
		return;
	}
	dma_get(p, params, sizeof(*p), TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	if (structure->surface_count < 0 || structure->surface_count > MAXIMUM_SURFACES || p->cluster_count < 0 ||
		p->cluster_count > SPU_GAME_VISIBILITY_CLUSTERS || p->maximum_count < 0 || ((p->flags | p->indices) & 3) ||
		(p->triangles & 1))
	{
		echo.refused++;
		answer_put(p->answer, SPU_GAME_REFUSED, 0);
		return;
	}
	cache_check(structure->generation);
	maximum = (uint32_t)p->maximum_count;
	words = ((uint32_t)structure->surface_count + 31) / 32;
	/* (the flags at their address's place in a quadword, as put_exact puts them; the words before it zero) */
	pad_words = (p->flags & 15) / 4;
	flags = (uint32_t *)(scratch + VISIBILITY_FLAGS) + pad_words;
	memset(scratch + VISIBILITY_FLAGS, 0, ((pad_words + words) * 4 + 15) & ~15u);
	for (cluster_index = 0; cluster_index < p->cluster_count && count < maximum; cluster_index++)
	{
		const spu_game_visibility_cluster *cluster = &p->clusters[cluster_index];
		uint32_t block[2], subcluster_index;

		if (cluster->cluster_index < 0 || cluster->cluster_index >= structure->cluster_count)
			continue;
		cache_read(block, structure->clusters + cluster->cluster_index * structure->cluster_size +
			structure->cluster_subclusters_offset, 8);
		/* (the game counts them in a short) */
		if (block[0] > 32767)
		{
			echo.refused++;
			answer_put(p->answer, SPU_GAME_REFUSED, 0);
			return;
		}
		for (subcluster_index = 0; subcluster_index < block[0] && count < maximum; subcluster_index++)
		{
			uint32_t subcluster = block[1] + subcluster_index * structure->subcluster_size, list[2], listed;
			float bounds[6];

			cache_read(bounds, subcluster, 24);
			if (!cluster_cube_visible(cluster, bounds))
				continue;
			cache_read(list, subcluster + structure->subcluster_surfaces_offset, 8);
			for (listed = 0; listed < list[0] && count < maximum;)
			{
				uint32_t batch = list[0] - listed < 1024 ? list[0] - listed : 1024, index;
				const int32_t *surfaces = fetch(scratch + VISIBILITY_CHUNK, list[1] + listed * 4, batch * 4);

				for (index = 0; index < batch; index++)
				{
					uint32_t surface = (uint32_t)surfaces[index];

					if (surface >= (uint32_t)structure->surface_count || bit_test(flags, surface))
						continue;
					if (count >= maximum)
						break;
					flags[surface >> 5] |= 1u << (surface & 31);
					count++;
				}
				listed += batch;
			}
		}
	}
	put_exact(flags, p->flags, words * 4, TAG_OUTPUT);
	indices = (int32_t *)(scratch + VISIBILITY_INDICES + (p->indices & 15));
	triangles = (int16_t *)(scratch + VISIBILITY_TRIANGLES + (p->triangles & 15));
	for (quad = 0; quad < (pad_words + words + 3) / 4; quad++)
	{
		uint32_t lane;

		if (!spu_extract(spu_orx(quads[quad]), 0))
			continue;
		for (lane = 0; lane < 4; lane++)
		{
			uint32_t word = quad * 4 + lane - pad_words, bits = ((const uint32_t *)quads)[quad * 4 + lane];

			while (bits)
			{
				uint32_t surface = word * 32 + 31 - spu_extract(spu_cntlz(spu_promote(bits & -bits, 0)), 0);
				const int16_t *vertex_indices;

				/* the surfaces block through 16 KB fetches, ascending as the surfaces come */
				if (surface < window_first || surface >= window_first + window_count)
				{
					window_first = surface;
					window_count = (uint32_t)structure->surface_count - surface < VISIBILITY_WINDOW ?
						(uint32_t)structure->surface_count - surface : VISIBILITY_WINDOW;
					window = fetch(scratch + VISIBILITY_SURFACES, structure->surfaces + surface * 6, window_count * 6);
				}
				vertex_indices = (const int16_t *)(window + (surface - window_first) * 6);
				indices[n] = (int32_t)surface;
				triangles[n * 3] = vertex_indices[0];
				triangles[n * 3 + 1] = vertex_indices[1];
				triangles[n * 3 + 2] = vertex_indices[2];
				if (++n == VISIBILITY_BATCH)
				{
					visibility_flush(p, indices, triangles, done, n);
					done += n;
					n = 0;
				}
				bits &= bits - 1;
			}
		}
	}
	if (n)
		visibility_flush(p, indices, triangles, done, n);
	answer_put(p->answer, SPU_GAME_DONE, count);
}

/* ---------- SPRITES: render_particles' sprites (render_particles.c, render_frustum_sphere_diameter_in_pixels of
render_cameras.c), each group's build_sprite calls (sprite_build) */

#define SPRITES_GROUP 256
#define SPRITES_TABLE 512
#define SPRITES_PARTICLES 9216
#define SPRITES_BATCH 64
#define SPRITES_STAGING 13568

/* render_particles' particle: its size cut, scale and fades, then its sprite */
static void sprite_particle(sprite_call *call, const spu_game_sprites *p, const spu_game_sprite_group *group,
	spu_game_sprite_particle *particle)
{
	const float *m = p->world_to_view, *position = particle->position;
	float depth = m[9] * position[2] + m[6] * position[1] + m[3] * position[0] + m[12];
	float clamped = absolute(depth), diameter;

	clamped = clamped > 0.1f ? clamped : 0.1f;
	diameter = divide(p->projection_world_to_screen[1], clamped) * particle->radius * 2.0f;
	particle->flags &= ~SPU_GAME_SPRITE_BUILT;
	if (diameter > group->lod_cutoff)
	{
		float fade = 1.0f, scale = (particle->radius + particle->radius) * group->one_over_width;
		float remaining_life = particle->lifespan - particle->age;

		particle->flags |= SPU_GAME_SPRITE_BUILT;
		if (diameter < group->minimum_pixels)
			scale = divide(group->minimum_pixels, diameter) * scale;
		if (group->fade_in_time > 0.0f && particle->age < group->fade_in_time)
			fade = divide(particle->age, group->fade_in_time);
		if (group->fade_out_time > 0.0f && remaining_life < group->fade_out_time)
			fade *= divide(remaining_life, group->fade_out_time);
		sprite_build(call, particle->sequence_index, particle->sprite_index, position, particle->direction,
			particle->rotation, scale, particle->color, fade,
			particle->flags & (SPU_GAME_SPRITE_U_MIRROR | SPU_GAME_SPRITE_V_MIRROR));
	}
}

/* each group's answer over it (sprite_count NONE: the PPE builds it), the window's big sprites and coverage in the
parameters */
static void sprites_job(uint32_t params, uint32_t size)
{
	spu_game_sprites *p = (spu_game_sprites *)scratch;
	spu_game_sprite_group *group = (spu_game_sprite_group *)(scratch + SPRITES_GROUP);
	spu_game_sprite_particle *batch = (spu_game_sprite_particle *)(scratch + SPRITES_PARTICLES);
	const spu_game_weather_sprites *table = NULL;
	weather_slot slots[SPU_GAME_SPRITE_SLOTS];
	uint32_t table_ea = 0;
	int32_t group_index;

	if (size != sizeof(spu_game_sprites) || (params & 15))
	{
		echo.refused++;
		return;
	}
	dma_get(p, params, sizeof(*p), TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	if (p->group_count < 0 || p->particle_count < 0 || ((p->groups | p->particles) & 15))
	{
		echo.refused++;
		answer_put(p->answer, SPU_GAME_REFUSED, 0);
		return;
	}
	for (group_index = 0; group_index < p->group_count; group_index++)
	{
		uint32_t group_ea = p->groups + group_index * sizeof(*group);
		sprite_call call;
		int32_t done, slot;

		dma_wait(1 << TAG_OUTPUT);
		dma_get(group, group_ea, sizeof(*group), TAG_PARAMS);
		dma_wait(1 << TAG_PARAMS);
		group->sprite_count = 0;
		group->centroid[0] = group->centroid[1] = group->centroid[2] = 0.0f;
		for (slot = 0; slot < SPU_GAME_SPRITE_SLOTS; slot++)
		{
			group->slot_sprites[slot] = 0;
			group->slot_order[slot] = NONE;
		}
		if (group->first_particle < 0 || group->particle_count < 0 ||
			group->first_particle > p->particle_count - group->particle_count ||
			group->sprites_size > sizeof(spu_game_weather_sprites) || (group->sprites & 15) || !group->sprites)
		{
			group->sprite_count = NONE;
			mfc_put(group, group_ea, sizeof(*group), TAG_OUTPUT, 0, 0);
			continue;
		}
		if (group->sprites != table_ea)
		{
			table = fetch(scratch + SPRITES_TABLE, group->sprites, group->sprites_size);
			table_ea = group->sprites;
		}
		for (slot = 0; slot < SPU_GAME_SPRITE_SLOTS; slot++)
		{
			slots[slot].staging = scratch + SPRITES_STAGING + slot * 2048 + (group->vertices[slot] & 15);
			slots[slot].vertices = group->vertices[slot];
			slots[slot].count = 0;
			slots[slot].flushed = 0;
			slots[slot].order = NONE;
		}
		call.world_to_view = p->world_to_view;
		call.projection = p->projection;
		call.projection_world_to_screen_x = p->projection_world_to_screen[0];
		call.viewer_up = p->viewer_up;
		call.viewer_forward = p->viewer_forward;
		call.big_sprite_count = &p->big_sprite_count;
		call.coverage = &p->coverage;
		call.table = table;
		call.mode = group->mode;
		call.fade_mode = group->fade_mode;
		call.fade_alpha = group->fade_alpha;
		call.maximum = group->particle_count;
		call.sprite_count = &group->sprite_count;
		call.centroid = group->centroid;
		call.orders = 0;
		call.slots = slots;
		for (done = 0; done < group->particle_count; done += SPRITES_BATCH)
		{
			int32_t n = group->particle_count - done < SPRITES_BATCH ? group->particle_count - done : SPRITES_BATCH, index;
			uint32_t ea = p->particles + (uint32_t)(group->first_particle + done) * sizeof(*batch);

			dma_wait(1 << TAG_OUTPUT);
			dma_get(batch, ea, n * sizeof(*batch), TAG_BULK);
			dma_wait(1 << TAG_BULK);
			for (index = 0; index < n; index++)
				sprite_particle(&call, p, group, &batch[index]);
			mfc_put(batch, ea, n * sizeof(*batch), TAG_OUTPUT, 0, 0);
		}
		for (slot = 0; slot < SPU_GAME_SPRITE_SLOTS; slot++)
		{
			weather_flush(&slots[slot]);
			group->slot_sprites[slot] = slots[slot].count;
			group->slot_order[slot] = slots[slot].order;
		}
		mfc_put(group, group_ea, sizeof(*group), TAG_OUTPUT, 0, 0);
	}
	dma_wait(1 << TAG_OUTPUT);
	mfc_put(p, params, sizeof(*p), TAG_OUTPUT, 0, 0);
	answer_put(p->answer, SPU_GAME_DONE, (uint32_t)p->group_count);
}

/* ---------- CONTRAILS: render_contrail's strips (render_contrails.c), with contrail_fade, transition_function_evaluate
(periodic_functions.c) and real_argb_color_to_pixel32's rounding */

#define CONTRAILS_TABLE 256
#define CONTRAILS_ENTRY 1536
#define CONTRAILS_POINTS 2048
#define CONTRAILS_VERTICES 34816
#define CONTRAILS_TRIANGLES 83984

/* rint: to nearest, a tie to even (|x| under 2^23) */
static int32_t round_even(float x)
{
	int32_t n = (int32_t)x;
	float rest = x - (float)n;

	if (rest > 0.5f || (rest == 0.5f && (n & 1)))
		n++;
	else if (rest < -0.5f || (rest == -0.5f && (n & 1)))
		n--;
	return n;
}

/* (long)rint((double)c * 255.0f), exactly: c * 255 is the mantissa times 255 (32 bits) at c's exponent */
static int32_t channel_rint(float c)
{
	union
	{
		float f;
		uint32_t u;
	} bits;
	uint32_t exponent, product, n;

	bits.f = c;
	exponent = (bits.u >> 23) & 0xFF;
	if (!exponent)
		return 0;
	product = ((bits.u & 0x7FFFFF) | 0x800000) * 255u;
	if (exponent >= 150)
	{
		n = exponent - 150 < 8 ? product << (exponent - 150) : 0;
	}
	else if (exponent < 118)
	{
		n = 0;
	}
	else if (exponent == 118)
	{
		/* (under 1: over a half rounds up, a half to the even 0) */
		n = product > 0x80000000u;
	}
	else
	{
		uint32_t shift = 150 - exponent, rest = product & ((1u << shift) - 1), half = 1u << (shift - 1);

		n = product >> shift;
		if (rest > half || (rest == half && (n & 1)))
			n++;
	}
	return (bits.u & 0x80000000u) ? -(int32_t)n : (int32_t)n;
}

/* real_argb_color_to_pixel32 of an argb colour */
static uint32_t contrail_pixel(const float *color)
{
	return (uint32_t)channel_rint(color[3]) | ((uint32_t)channel_rint(color[2]) << 8) |
		((uint32_t)channel_rint(color[1]) << 16) | ((uint32_t)channel_rint(color[0]) << 24);
}

/* transition_function_evaluate on its table of size bytes */
static float transition_evaluate(const unsigned char *table, int32_t size, float value)
{
	float scaled, fraction;
	int32_t index;

	value = PIN(value, 0.0f, 1.0f);
	scaled = value * (float)(size - 1);
	fraction = scaled - truncate_real(scaled);
	index = round_even(scaled - 0.5f);
	if ((int16_t)index == size - 1)
		return (float)table[size - 1] * (1.0f / 255.0f);
	if (index < 0 || index >= size - 1)
		return value;
	return (float)table[index] * (1.0f / 255.0f) * (1.0f - fraction) +
		(float)table[index + 1] * (1.0f / 255.0f) * fraction;
}

/* normalize2d: the magnitude, 0 (the vector kept) under the epsilon */
static float normalize_2d(float *v)
{
	float magnitude = square_root(v[0] * v[0] + v[1] * v[1]);

	if (absolute(magnitude) < REAL_EPSILON)
		return 0.0f;
	{
		float scale = reciprocal(magnitude);

		v[0] *= scale;
		v[1] *= scale;
	}
	return magnitude;
}

/* a state's width, lower and upper argb colours, flags (at width_offset); the width and colour scaled by density */
static void contrail_state(const spu_game_contrail *c, int32_t state_index, float density, float *width, float *color)
{
	float state[10];
	uint32_t flags, k;
	float color_scale = 1.0f;

	cache_read(state, c->states + (uint32_t)state_index * (uint32_t)c->state_size + (uint32_t)c->width_offset, 40);
	flags = ((const uint32_t *)state)[9];
	if (flags & c->color_mask)
		color_scale = density;
	*width = state[0];
	if (flags & c->width_mask)
		*width *= density;
	for (k = 0; k < 4; k++)
		color[k] = (state[5 + k] - state[1 + k]) * color_scale + state[1 + k];
}

static void contrail_strip(const spu_game_contrails *p, const spu_game_contrail *c,
	const spu_game_contrail_point *points, const unsigned char *table, float *vertices, int16_t *triangles)
{
	static const float global_up[3] = { 0.0f, 0.0f, 1.0f };
	int32_t index, count = c->point_count;
	float texture_u = c->texture_u;
	uint32_t *last;

	for (index = 0; index < count; index++)
	{
		const spu_game_contrail_point *point = &points[index], *previous = index ? &points[index - 1] : NULL;
		const float *position = point->position;
		float color[4], width, half_width, orientation[3] = { 0.0f, 0.0f, 0.0f }, fade = 1.0f;
		float *v0 = vertices + index * 12, *v1 = v0 + 6;
		uint32_t k, pixel;

		contrail_state(c, point->state_index, point->density, &width, color);
		half_width = width;
		if (point->flags & SPU_GAME_CONTRAIL_TRANSITIONING)
		{
			float next_width, next_color[4], transition = point->time;

			contrail_state(c, point->state_index + 1, point->density, &next_width, next_color);
			half_width = (next_width - width) * transition + width;
			for (k = 0; k < 4; k++)
				color[k] = (next_color[k] - color[k]) * transition + color[k];
		}
		if (c->tinted)
		{
			color[1] *= c->tint[0];
			color[2] *= c->tint[1];
			color[3] *= c->tint[2];
		}
		half_width *= 0.5f;
		v0[3] = texture_u;
		v0[4] = c->texture_v_far;
		v1[3] = texture_u;
		v1[4] = c->texture_offset_v;
		if (c->render_type == 0)
		{
			v0[0] = position[0];
			v0[1] = position[1];
			v0[2] = position[2] - half_width;
			v1[0] = position[0];
			v1[1] = position[1];
			v1[2] = position[2] + half_width;
			if (c->fade_mode)
			{
				if (previous)
				{
					orientation[0] = previous->position[1] - position[1];
					orientation[1] = position[0] - previous->position[0];
				}
				else
				{
					orientation[0] = position[1] - points[1].position[1];
					orientation[1] = points[1].position[0] - position[0];
				}
				orientation[2] = 0.0f;
				normalize(orientation);
			}
		}
		else if (c->render_type == 1 || c->render_type == 2)
		{
			float perpendicular[2];

			if (previous)
			{
				perpendicular[0] = previous->position[1] - position[1];
				perpendicular[1] = position[0] - previous->position[0];
			}
			else
			{
				perpendicular[0] = position[1] - points[1].position[1];
				perpendicular[1] = points[1].position[0] - position[0];
			}
			normalize_2d(perpendicular);
			v0[0] = position[0] - perpendicular[0] * half_width;
			v0[1] = position[1] - perpendicular[1] * half_width;
			v0[2] = position[2];
			v1[0] = position[0] + perpendicular[0] * half_width;
			v1[1] = position[1] + perpendicular[1] * half_width;
			v1[2] = position[2];
			if (c->fade_mode)
				memcpy(orientation, global_up, sizeof(orientation));
		}
		else
		{
			const float *from = previous ? previous->position : position;
			const float *to = previous ? position : points[1].position;
			float eye[3], tangent[3], normal[3];

			for (k = 0; k < 3; k++)
			{
				eye[k] = p->camera[k] - from[k];
				tangent[k] = to[k] - from[k];
			}
			normal[0] = tangent[2] * eye[1] - tangent[1] * eye[2];
			normal[1] = tangent[0] * eye[2] - tangent[2] * eye[0];
			normal[2] = tangent[1] * eye[0] - tangent[0] * eye[1];
			normalize(normal);
			for (k = 0; k < 3; k++)
			{
				v0[k] = position[k] - normal[k] * half_width;
				v1[k] = position[k] + normal[k] * half_width;
			}
			if (c->fade_mode)
			{
				orientation[0] = normal[2] * tangent[1] - normal[1] * tangent[2];
				orientation[1] = normal[0] * tangent[2] - normal[2] * tangent[0];
				orientation[2] = normal[1] * tangent[0] - normal[0] * tangent[1];
				normalize(orientation);
			}
		}
		if (c->fade_mode)
		{
			float to_camera[3];

			for (k = 0; k < 3; k++)
				to_camera[k] = p->camera[k] - position[k];
			fade = absolute(divide(dot3(orientation, to_camera), square_root(dot3(to_camera, to_camera))));
			if (c->flags & SPU_GAME_CONTRAIL_FADES_SLOWLY)
				fade = transition_evaluate(table, p->transition_size, fade);
			if (c->fade_mode == 2)
				fade = 1.0f - fade;
		}
		color[0] *= fade;
		color[0] = PIN(color[0], 0.0f, 1.0f);
		pixel = contrail_pixel(color);
		((uint32_t *)v0)[5] = pixel;
		((uint32_t *)v1)[5] = pixel;
		texture_u += c->texture_u_step;
	}
	if (!(c->flags & SPU_GAME_CONTRAIL_FIRST_UNFADED))
	{
		((uint32_t *)vertices)[5] &= 0x00FFFFFF;
		((uint32_t *)vertices)[11] &= 0x00FFFFFF;
	}
	last = (uint32_t *)(vertices + (count - 1) * 12);
	if (!(c->flags & SPU_GAME_CONTRAIL_LAST_UNFADED))
	{
		last[5] &= 0x00FFFFFF;
		last[11] &= 0x00FFFFFF;
	}
	for (index = 0; index < count - 1; index++)
	{
		int16_t *triangle = triangles + index * 6;

		triangle[0] = (int16_t)(2 * index);
		triangle[1] = (int16_t)(2 * index + 1);
		triangle[2] = (int16_t)(2 * index + 2);
		triangle[3] = (int16_t)(2 * index + 2);
		triangle[4] = (int16_t)(2 * index + 1);
		triangle[5] = (int16_t)(2 * index + 3);
	}
}

/* each contrail's answer over it */
static void contrails_job(uint32_t params, uint32_t size)
{
	spu_game_contrails *p = (spu_game_contrails *)scratch;
	spu_game_contrail *c = (spu_game_contrail *)(scratch + CONTRAILS_ENTRY);
	spu_game_contrail_point *points = (spu_game_contrail_point *)(scratch + CONTRAILS_POINTS);
	const unsigned char *table = NULL;
	int32_t index;

	if (size != sizeof(spu_game_contrails) || (params & 15))
	{
		echo.refused++;
		return;
	}
	dma_get(p, params, sizeof(*p), TAG_PARAMS);
	dma_wait(1 << TAG_PARAMS);
	if (p->contrail_count < 0 || (p->contrails & 15) || p->transition_size < 2 || p->transition_size > 1024)
	{
		echo.refused++;
		answer_put(p->answer, SPU_GAME_REFUSED, 0);
		return;
	}
	cache_check(p->generation);
	if (p->transition_table)
		table = fetch(scratch + CONTRAILS_TABLE, p->transition_table, (uint32_t)p->transition_size);
	for (index = 0; index < p->contrail_count; index++)
	{
		uint32_t entry_ea = p->contrails + index * sizeof(*c);

		dma_wait(1 << TAG_OUTPUT);
		dma_get(c, entry_ea, sizeof(*c), TAG_PARAMS);
		dma_wait(1 << TAG_PARAMS);
		c->status = SPU_GAME_REFUSED;
		if (c->point_count >= 2 && c->point_count <= SPU_GAME_CONTRAIL_POINTS && !(c->points & 15) &&
			(c->render_type == 0 || c->render_type == 1 || c->render_type == 2 || c->render_type == 4) &&
			(!(c->flags & SPU_GAME_CONTRAIL_FADES_SLOWLY) || !c->fade_mode || table) && c->state_size > 0 &&
			c->width_offset >= 0 && c->width_offset + 40 <= c->state_size && !(c->vertices & 3) && !(c->triangles & 1))
		{
			float *vertices = (float *)(scratch + CONTRAILS_VERTICES + (c->vertices & 15));
			int16_t *triangles = (int16_t *)(scratch + CONTRAILS_TRIANGLES + (c->triangles & 15));
			int32_t k;

			dma_get(points, c->points, (uint32_t)c->point_count * sizeof(*points), TAG_BULK);
			dma_wait(1 << TAG_BULK);
			for (k = 0; k < c->point_count; k++)
			{
				int32_t last = points[k].state_index + ((points[k].flags & SPU_GAME_CONTRAIL_TRANSITIONING) ? 1 : 0);

				if (points[k].state_index < 0 || last >= c->state_count)
					break;
			}
			if (k == c->point_count)
			{
				contrail_strip(p, c, points, table, vertices, triangles);
				put_exact(vertices, c->vertices, (uint32_t)c->point_count * 48, TAG_OUTPUT);
				put_exact(triangles, c->triangles, (uint32_t)(c->point_count - 1) * 12, TAG_OUTPUT);
				c->status = SPU_GAME_DONE;
			}
		}
		mfc_put(c, entry_ea, sizeof(*c), TAG_OUTPUT, 0, 0);
	}
	answer_put(p->answer, SPU_GAME_DONE, (uint32_t)p->contrail_count);
}

/* ---------- the jobs */

static void run_job(const spu_game_job *job)
{
	switch (job->kind)
	{
	case SPU_GAME_COPY:
		if (!((job->source | job->target | job->size) & 15))
			copy_job(job->source, job->target, job->size);
		break;
	case SPU_GAME_ZEROES:
		if (!((job->source | job->target | job->size) & 15))
		{
			uint32_t value = zeroes_job(job->source, job->size);

			answer_put(job->target, SPU_GAME_DONE, value);
		}
		break;
	case SPU_GAME_ZERO:
		if (!((job->target | job->size) & 15))
			zero_job(job->target, job->size);
		break;
	case SPU_GAME_SHADOW:
		shadow_job(job->source, job->size);
		break;
	case SPU_GAME_WEATHER:
		weather_job(job->source, job->size);
		break;
	case SPU_GAME_PARTICLES:
		particles_job(job->source, job->size);
		break;
	case SPU_GAME_LIGHTS:
		lights_job(job->source, job->size);
		break;
	case SPU_GAME_VISIBILITY:
		visibility_job(job->source, job->size);
		break;
	case SPU_GAME_SPRITES:
		sprites_job(job->source, job->size);
		break;
	case SPU_GAME_CONTRAILS:
		contrails_job(job->source, job->size);
		break;
	default:
		echo.refused++;
		break;
	}
}

/* each job's tail goes out once its output is out */
static uint32_t run_jobs(uint32_t tail, uint32_t head)
{
	while (tail != head)
	{
		uint32_t index = tail & (SPU_GAME_RING - 1), count = head - tail, job;

		if (count > JOB_BATCH)
			count = JOB_BATCH;
		if (count > SPU_GAME_RING - index)
			count = SPU_GAME_RING - index;
		current_kind = 0;
		echo.stage = 1;
		mfc_get(jobs, setup.ring + index * sizeof(spu_game_job), count * sizeof(spu_game_job), TAG_JOBS, 0, 0);
		dma_wait(1 << TAG_JOBS);
		for (job = 0; job < count; job++)
		{
			uint32_t start = spu_read_decrementer(), kind = jobs[job].kind < SPU_GAME_KINDS ? jobs[job].kind : 0;

			current_kind = kind;
			echo.stage = 2 + kind;
			run_job(&jobs[job]);
			dma_wait(OUTPUT_TAGS);
			echo.jobs[kind]++;
			echo.busy_ticks[kind] += start - spu_read_decrementer();
			echo.stage = 0;
			echo.tail = ++tail;
			put_echo();
		}
	}
	return tail;
}

int main(uint64_t bell_address, uint64_t echo_address, uint64_t setup_address)
{
	uint32_t tail = 0;

	bell_ea = bell_address;
	echo_ea = echo_address;
	mfc_get(&setup, setup_address, sizeof(setup), TAG_JOBS, 0, 0);
	dma_wait(1 << TAG_JOBS);
	echo.state = SPU_GAME_RUNNING;
	echo.ls_address = (uint32_t)&echo;
	put_echo();
	spu_write_event_mask(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_event_ack(MFC_LLR_LOST_EVENT | MFC_DECREMENTER_EVENT);
	spu_write_decrementer(DECREMENTER_TICKS);
	for (;;)
	{
		uint32_t events;

		/* the check after each reservation closes the race with a store that lands before it; the jobs before a
		quit are done */
		mfc_getllar((void *)&bell, bell_ea, 0, 0);
		mfc_read_atomic_status();
		if (bell.head != tail)
		{
			tail = run_jobs(tail, bell.head);
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
	echo.state = SPU_GAME_EXITED;
	put_echo();
	spu_thread_exit(0);
	return 0;
}
