/* PS3_VMX.C: exact VMX for the simulation, and memory streams (ps3_vmx.h). A product is a multiply-add of -0 (one
rounding, as the scalar multiply); adds and subtracts round to nearest as the PC's; no estimates; NJ off keeps denormals */

#include "ps3_vmx.h"
#include "ps3_system.h"

#include <altivec.h>
#include <string.h>

/* -1 until the first call compares VMX with the FPU */
static int exact = -1;

static inline vector float product(vector float a, vector float b)
{
	return vec_madd(a, b, (vector float){ -0.0f, -0.0f, -0.0f, -0.0f });
}

/* NJ is per thread: each call sets it first; the memory clobber keeps the call's loads, and so its operations, after it */
static inline void java_mode(void)
{
	vector unsigned int zero = { 0, 0, 0, 0 };

	__asm__ volatile("mtvscr %0" : : "v"(zero) : "memory");
}

int ps3_vmx_exact(void)
{
	/* denormal products, sums and inputs, and signed zeros */
	static const float a_values[4] __attribute__((aligned(16))) = { 1e-20f, 1.5e-39f, -0.0f, 3.0e-39f };
	static const float b_values[4] __attribute__((aligned(16))) = { 1e-20f, 0.75f, 1.0f, -2.5e-39f };
	const float *a = a_values, *b = b_values;
	union { vector float v; uint32_t words[4]; } products, sums;
	int lane;

	if (exact >= 0)
		return exact;
	/* (the compiler cannot fold what it cannot see) */
	__asm__("" : "+r"(a), "+r"(b));
	java_mode();
	products.v = product(vec_ld(0, a), vec_ld(0, b));
	sums.v = vec_add(vec_ld(0, a), vec_ld(0, b));
	exact = 1;
	for (lane = 0; lane < 4; lane++)
	{
		union { float f; uint32_t word; } fpu_product, fpu_sum;

		fpu_product.f = a[lane] * b[lane];
		fpu_sum.f = a[lane] + b[lane];
		if (fpu_product.word != products.words[lane] || fpu_sum.word != sums.words[lane])
			exact = 0;
	}
	ps3_log_printf("[vmx] %s\n", exact ? "exact: VSCR's NJ off, the denormals as the FPU's" :
		"not exact (denormals differ): the scalar code");
	return exact;
}

/* fast_vector_intersects_sphere's operations, a sphere a lane: c < 0 hits; then b >= 0 or disc <= 0 misses; then
-a - b < 0 or its square under disc hits. Comparisons as the scalar code's, so a NaN goes the same way */
uint32_t ps3_vmx_segment_misses_spheres(const float *const *spheres, uint32_t count, const float *point,
	const float *direction)
{
	const vector float zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	const vector unsigned int sign = { 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u };
	const vector unsigned int four = { 4, 4, 4, 4 };
	vector unsigned int weights = { 1, 2, 4, 8 }, misses = { 0, 0, 0, 0 };
	union { vector unsigned int v; uint32_t words[4]; } bits;
	uint32_t first;

	java_mode();
	const vector float point_x = { point[0], point[0], point[0], point[0] };
	const vector float point_y = { point[1], point[1], point[1], point[1] };
	const vector float point_z = { point[2], point[2], point[2], point[2] };
	const vector float i = { direction[0], direction[0], direction[0], direction[0] };
	const vector float j = { direction[1], direction[1], direction[1], direction[1] };
	const vector float k = { direction[2], direction[2], direction[2], direction[2] };
	vector float a = vec_add(vec_add(product(i, i), product(j, j)), product(k, k));
	vector float minus_a = (vector float)vec_xor((vector unsigned int)a, sign);

	for (first = 0; first < count; first += 4)
	{
		/* the lanes past count take the last sphere; their bits are dropped */
		const float *last = spheres[count - 1];
		vector float s0 = vec_ld(0, spheres[first]);
		vector float s1 = vec_ld(0, first + 1 < count ? spheres[first + 1] : last);
		vector float s2 = vec_ld(0, first + 2 < count ? spheres[first + 2] : last);
		vector float s3 = vec_ld(0, first + 3 < count ? spheres[first + 3] : last);
		vector float t0 = vec_mergeh(s0, s2), t1 = vec_mergeh(s1, s3), t2 = vec_mergel(s0, s2), t3 = vec_mergel(s1, s3);
		vector float x = vec_mergeh(t0, t1), y = vec_mergel(t0, t1), z = vec_mergeh(t2, t3), r = vec_mergel(t2, t3);
		vector float px = vec_sub(point_x, x), py = vec_sub(point_y, y), pz = vec_sub(point_z, z);
		vector float c = vec_sub(vec_add(vec_add(product(px, px), product(py, py)), product(pz, pz)), product(r, r));
		vector float b = vec_add(vec_add(product(i, px), product(j, py)), product(k, pz));
		vector float disc = vec_sub(product(b, b), product(a, c));
		vector float reach = vec_sub(minus_a, b);
		vector unsigned int inside = (vector unsigned int)vec_cmpgt(zero, c);
		vector unsigned int away = (vector unsigned int)vec_cmpge(b, zero);
		vector unsigned int apart = (vector unsigned int)vec_cmpge(zero, disc);
		vector unsigned int ahead = vec_or((vector unsigned int)vec_cmpgt(zero, reach),
			(vector unsigned int)vec_cmpgt(disc, product(reach, reach)));
		vector unsigned int hit = vec_or(inside, vec_andc(vec_andc(ahead, away), apart));

		misses = vec_or(misses, vec_andc(weights, hit));
		weights = vec_sl(weights, four);
	}
	bits.v = misses;
	return (bits.words[0] | bits.words[1] | bits.words[2] | bits.words[3]) & (count < 32 ? (1u << count) - 1 : ~0u);
}

/* the 3 floats at p in lanes 0 to 2, from the quadwords that hold them only */
static inline vector float load3(const float *p)
{
	return vec_perm(vec_ld(0, p), vec_ld(11, p), vec_lvsl(0, p));
}

/* a row of the product, as the scalar code: (b_row[0] * a's forward + b_row[1] * its left) + b_row[2] * its up */
#define PRODUCT_ROW(b_row) \
	vec_add(vec_add(vec_madd(vec_splat(b_row, 0), forward, minus_zero), vec_madd(vec_splat(b_row, 1), left, minus_zero)), \
		vec_madd(vec_splat(b_row, 2), up, minus_zero))

/* a word store takes the lane its address picks, so each value goes splatted */
#define STORE_ROW(row, p) \
	(vec_ste(vec_splat(row, 0), 0, (p)), vec_ste(vec_splat(row, 1), 0, (p) + 1), vec_ste(vec_splat(row, 2), 0, (p) + 2))

/* the position's row also times a's scale, plus a's position. Every load comes before the first store, so result may be
a or b */
void ps3_vmx_matrix4x3_multiply(const float *a, const float *b, float *result)
{
	const vector float minus_zero = { -0.0f, -0.0f, -0.0f, -0.0f };

	java_mode();
	vector float a_scale = vec_splat(load3(a), 0), b_scale = vec_splat(load3(b), 0);
	vector float forward = load3(a + 1), left = load3(a + 4), up = load3(a + 7), position = load3(a + 10);
	vector float b0 = load3(b + 1), b1 = load3(b + 4), b2 = load3(b + 7), b3 = load3(b + 10);
	vector float row0 = PRODUCT_ROW(b0), row1 = PRODUCT_ROW(b1), row2 = PRODUCT_ROW(b2);
	vector float row3 = vec_add(vec_madd(PRODUCT_ROW(b3), a_scale, minus_zero), position);

	vec_ste(vec_madd(a_scale, b_scale, minus_zero), 0, result);
	STORE_ROW(row0, result + 1);
	STORE_ROW(row1, result + 4);
	STORE_ROW(row2, result + 7);
	STORE_ROW(row3, result + 10);
}

/* ---------- the transparent sort's model bounds (ps3_vmx_model_bounds): BOUNDS BEGIN */

#define BOUNDS_MAXIMUM_MATRICES 64

/* a real_matrix4x3 in lanes 0 to 2: its scale (splatted), forward, left, up, position */
typedef struct
{
	vector float scale, forward, left, up, position;
} bounds_matrix;

/* matrix4x3_transform_point: the point times the scale (times 1 changes no float), then up z + left y + forward x +
position, in that order */
static inline vector float bounds_transform(const bounds_matrix *m, vector float point, vector float minus_zero)
{
	vector float scaled = vec_madd(point, m->scale, minus_zero);

	return vec_add(vec_add(vec_add(vec_madd(m->up, vec_splat(scaled, 2), minus_zero),
		vec_madd(m->left, vec_splat(scaled, 1), minus_zero)), vec_madd(m->forward, vec_splat(scaled, 0), minus_zero)),
		m->position);
}

/* a vertex as the scalar loop takes it: invalid set as its tests fail (the result is then 0 whatever the rest), each
node read within the matrices (an invalid index reads the first) */
static inline vector float bounds_vertex(const unsigned char *v, int compressed, const bounds_matrix *m,
	uint32_t matrix_count, int *invalid)
{
	const vector float zero = { 0.0f, 0.0f, 0.0f, 0.0f }, one = { 1.0f, 1.0f, 1.0f, 1.0f };
	const vector float minus_zero = { -0.0f, -0.0f, -0.0f, -0.0f };
	const vector float inverse = { 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f };
	const vector float epsilon = { 0.00001f, 0.00001f, 0.00001f, 0.00001f };
	const vector unsigned int sign = { 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u };
	vector float point = load3((const float *)v), world = zero, weight0, weight1;
	int32_t node0, node1;

	if (matrix_count == 1)
		return bounds_transform(m, point, minus_zero);
	if (compressed)
	{
		/* the weight's halfword first, sign-extended: over 32767; the nodes' bytes over 3 */
		vector signed short halves = (vector signed short)vec_perm(vec_ld(30, v), vec_ld(31, v), vec_lvsl(30, v));
		uint32_t raw0 = v[28], raw1 = v[29];

		weight0 = vec_splat(vec_madd(vec_ctf(vec_unpackh(halves), 0), inverse, minus_zero), 0);
		weight1 = vec_sub(one, weight0);
		node0 = (int32_t)(raw0 / 3);
		node1 = (int32_t)(raw1 / 3);
		if ((vec_all_gt(weight0, zero) && raw0 % 3) || (vec_all_gt(weight1, zero) && raw1 % 3))
			*invalid = 1;
	}
	else
	{
		vector float weights = (vector float)vec_perm(vec_ld(60, v), vec_ld(67, v), vec_lvsl(60, v));

		weight0 = vec_splat(weights, 0);
		weight1 = vec_splat(weights, 1);
		node0 = *(const int16_t *)(v + 56);
		node1 = *(const int16_t *)(v + 58);
		/* !(fabs(w0 + w1 - 1) < 0.00001) */
		if (!vec_all_lt((vector float)vec_andc((vector unsigned int)vec_sub(vec_add(weight0, weight1), one), sign),
			epsilon))
			*invalid = 1;
	}
	if (!vec_all_ge(weight0, zero) || !vec_all_le(weight0, one) || !vec_all_ge(weight1, zero) ||
		!vec_all_le(weight1, one))
		*invalid = 1;
	if (vec_all_gt(weight0, zero))
	{
		if (node0 < 0 || (uint32_t)node0 >= matrix_count)
		{
			*invalid = 1;
			node0 = 0;
		}
		world = vec_add(world, vec_madd(weight0, bounds_transform(&m[node0], point, minus_zero), minus_zero));
	}
	if (vec_all_gt(weight1, zero))
	{
		if (node1 < 0 || (uint32_t)node1 >= matrix_count)
		{
			*invalid = 1;
			node1 = 0;
		}
		world = vec_add(world, vec_madd(weight1, bounds_transform(&m[node1], point, minus_zero), minus_zero));
	}
	return world;
}

int ps3_vmx_model_bounds(const unsigned char *vertices, uint32_t count, uint32_t stride, int compressed,
	const float *matrices, uint32_t matrix_count, float *bounds)
{
	const vector float zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	const vector float minimum = { -1000000.0f, -1000000.0f, -1000000.0f, -1000000.0f };
	const vector float maximum = { 1000000.0f, 1000000.0f, 1000000.0f, 1000000.0f };
	const vector unsigned int lane3 = { 0, 0, 0, ~0u };
	bounds_matrix m[BOUNDS_MAXIMUM_MATRICES];
	vector float low = zero, high = zero;
	float out[8] __attribute__((aligned(16)));
	uint32_t index;
	int invalid = 0;

	if (!count || !matrix_count || matrix_count > BOUNDS_MAXIMUM_MATRICES)
		return 0;
	java_mode();
	for (index = 0; index < matrix_count; index++)
	{
		const float *matrix = matrices + index * 13;

		m[index].scale = vec_splat(load3(matrix), 0);
		m[index].forward = load3(matrix + 1);
		m[index].left = load3(matrix + 4);
		m[index].up = load3(matrix + 7);
		m[index].position = load3(matrix + 10);
	}
	for (index = 0; index < count; index++)
	{
		/* (lane 3 holds no float of the vertex: 0, which passes) */
		vector float world = vec_sel(bounds_vertex(vertices + index * stride, compressed, m, matrix_count, &invalid),
			zero, lane3);

		if (!vec_all_gt(world, minimum) || !vec_all_lt(world, maximum))
			invalid = 1;
		/* MIN (a > b ? b : a) and MAX (a > b ? a : b), as the scalar code's */
		if (index)
		{
			low = vec_sel(low, world, (vector unsigned int)vec_cmpgt(low, world));
			high = vec_sel(world, high, (vector unsigned int)vec_cmpgt(high, world));
		}
		else
		{
			low = world;
			high = world;
		}
	}
	if (invalid)
		return 0;
	vec_st(low, 0, out);
	vec_st(high, 16, out);
	for (index = 0; index < 3; index++)
	{
		bounds[index] = out[index];
		bounds[3 + index] = out[4 + index];
	}
	return 1;
}

/* BOUNDS END */

/* ---------- an indexed draw's index range (ps3_vmx_index_range) */

void ps3_vmx_index_range(const uint16_t *indices, uint32_t count, uint32_t *minimum, uint32_t *maximum)
{
	uint16_t lanes[8] __attribute__((aligned(16)));
	uint32_t low = 0xffff, high = 0, index = 0, lane;

	/* one at a time to the first quadword, then 8 at once */
	for (; index < count && ((uintptr_t)(indices + index) & 15); index++)
	{
		if (indices[index] < low)
			low = indices[index];
		if (indices[index] > high)
			high = indices[index];
	}
	if (count - index >= 8)
	{
		vector unsigned short lows = vec_splat_u16(-1), highs = vec_splat_u16(0);

		for (; index + 8 <= count; index += 8)
		{
			vector unsigned short quad = vec_ld(0, indices + index);

			lows = vec_min(lows, quad);
			highs = vec_max(highs, quad);
		}
		vec_st(lows, 0, lanes);
		for (lane = 0; lane < 8; lane++)
		{
			if (lanes[lane] < low)
				low = lanes[lane];
		}
		vec_st(highs, 0, lanes);
		for (lane = 0; lane < 8; lane++)
		{
			if (lanes[lane] > high)
				high = lanes[lane];
		}
	}
	for (; index < count; index++)
	{
		if (indices[index] < low)
			low = indices[index];
		if (indices[index] > high)
			high = indices[index];
	}
	*minimum = low;
	*maximum = high;
}

/* ---------- memory streams (ps3_vmx_copy and co.): STREAM BEGIN */

/* the PPE's cache line; the source this far ahead is touched (dcbt) so its lines come in while earlier ones copy */
#define STREAM_LINE 128
#define STREAM_AHEAD (8 * STREAM_LINE)

typedef vector unsigned char stream_vector;

static inline void stream_touch(const void *address)
{
	__asm__ volatile("dcbt 0,%0" : : "r"(address));
}

/* the line in the cache as zeroes, not read from memory first: only for a line written whole after it */
static inline void stream_zero_line(void *address)
{
	__asm__ volatile("dcbz 0,%0" : : "r"(address) : "memory");
}

/* 1 when dcbz zeroes the 128-byte line and nothing else (checked at the first call): else plain stores */
static int stream_dcbz_lines(void)
{
	static int lines = -1;
	static unsigned char probe[3 * STREAM_LINE] __attribute__((aligned(STREAM_LINE)));
	int index, zeroes = 0;

	if (lines >= 0)
		return lines;
	memset(probe, 0xff, sizeof(probe));
	stream_zero_line(probe + STREAM_LINE);
	for (index = 0; index < (int)sizeof(probe); index++)
		zeroes += !probe[index];
	lines = zeroes == STREAM_LINE && !probe[STREAM_LINE] && !probe[2 * STREAM_LINE - 1];
	ps3_log_printf("[vmx] dcbz zeroes %d bytes: %s\n", zeroes, lines ? "whole lines written with it" : "not used");
	return lines;
}

void ps3_vmx_copy(void *destination, const void *source, uint32_t size)
{
	unsigned char *to = destination;
	const unsigned char *from = source;
	uint32_t head = (uint32_t)(-(uintptr_t)to & (STREAM_LINE - 1));
	int dcbz = stream_dcbz_lines();

	if (head > size)
		head = size;
	memcpy(to, from, head);
	to += head;
	from += head;
	size -= head;
	/* vec_ld drops an address's low 4 bits: the lines only with the source 16-byte aligned too */
	if (!((uintptr_t)from & 15))
	{
		for (; size >= STREAM_LINE; size -= STREAM_LINE, to += STREAM_LINE, from += STREAM_LINE)
		{
			stream_vector v0, v1, v2, v3, v4, v5, v6, v7;

			stream_touch(from + STREAM_AHEAD);
			v0 = vec_ld(0, from);
			v1 = vec_ld(16, from);
			v2 = vec_ld(32, from);
			v3 = vec_ld(48, from);
			v4 = vec_ld(64, from);
			v5 = vec_ld(80, from);
			v6 = vec_ld(96, from);
			v7 = vec_ld(112, from);
			if (dcbz)
				stream_zero_line(to);
			vec_st(v0, 0, to);
			vec_st(v1, 16, to);
			vec_st(v2, 32, to);
			vec_st(v3, 48, to);
			vec_st(v4, 64, to);
			vec_st(v5, 80, to);
			vec_st(v6, 96, to);
			vec_st(v7, 112, to);
		}
	}
	memcpy(to, from, size);
}

void ps3_vmx_zero(void *destination, uint32_t size)
{
	unsigned char *to = destination;
	uint32_t head = (uint32_t)(-(uintptr_t)to & (STREAM_LINE - 1));
	int dcbz = stream_dcbz_lines();
	const stream_vector zero = vec_splat_u8(0);

	if (head > size)
		head = size;
	memset(to, 0, head);
	to += head;
	size -= head;
	for (; size >= STREAM_LINE; size -= STREAM_LINE, to += STREAM_LINE)
	{
		if (dcbz)
		{
			stream_zero_line(to);
			continue;
		}
		vec_st(zero, 0, to);
		vec_st(zero, 16, to);
		vec_st(zero, 32, to);
		vec_st(zero, 48, to);
		vec_st(zero, 64, to);
		vec_st(zero, 80, to);
		vec_st(zero, 96, to);
		vec_st(zero, 112, to);
	}
	memset(to, 0, size);
}

int ps3_vmx_zeroes(const void *source, uint32_t size)
{
	const unsigned char *from = source;
	const stream_vector zero = vec_splat_u8(0);
	stream_vector any = zero;

	for (; size && ((uintptr_t)from & 15); size--, from++)
	{
		if (*from)
			return 0;
	}
	for (; size >= STREAM_LINE; size -= STREAM_LINE, from += STREAM_LINE)
	{
		stream_touch(from + STREAM_AHEAD);
		any = vec_or(any, vec_or(vec_or(vec_or(vec_ld(0, from), vec_ld(16, from)), vec_or(vec_ld(32, from),
			vec_ld(48, from))), vec_or(vec_or(vec_ld(64, from), vec_ld(80, from)), vec_or(vec_ld(96, from),
			vec_ld(112, from)))));
		/* a byte set ends it at the next 4 KB */
		if (!(((uintptr_t)from + STREAM_LINE) & 4095) && !vec_all_eq(any, zero))
			return 0;
	}
	if (!vec_all_eq(any, zero))
		return 0;
	for (; size; size--, from++)
	{
		if (*from)
			return 0;
	}
	return 1;
}

/* STREAM END */
