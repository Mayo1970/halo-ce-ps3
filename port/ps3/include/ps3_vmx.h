/* PS3_VMX.H: exact VMX for the simulation (port/ps3/src/ps3_vmx.c): each lane runs the scalar code's float operations
in its order, so the PC builds' floats; and memory streams. Fixed-width types only: the game's units include it */

#ifndef __HALO_PS3_VMX_H
#define __HALO_PS3_VMX_H

#include <stdint.h>

/* 1 when VMX gives the scalar results (VSCR's NJ off and checked on denormals, at the first call); 0: use the scalar code */
int ps3_vmx_exact(void);
/* fast_vector_intersects_sphere (real_math.c) for count spheres (x, y, z, radius, 16-byte aligned), four at once: bit i
set when the segment from point along direction misses sphere i; count up to 32 */
uint32_t ps3_vmx_segment_misses_spheres(const float *const *spheres, uint32_t count, const float *point,
	const float *direction);
/* matrix4x3_multiply (matrix_math.c) on real_matrix4x3s (scale, then forward, left, up, position): any 4-byte
alignment, result may be a or b */
void ps3_vmx_matrix4x3_multiply(const float *a, const float *b, float *result);
/* transparent_model_world_bounds' loop: count model vertices (32 bytes compressed, else 68) skinned by up to 64
real_matrix4x3s; 1 with bounds (low x, y, z, high x, y, z) if every vertex is valid, else 0 */
int ps3_vmx_model_bounds(const unsigned char *vertices, uint32_t count, uint32_t stride, int compressed,
	const float *matrices, uint32_t matrix_count, float *bounds);
/* the least and the greatest of count indices (0xffff and 0 for none), 8 at once */
void ps3_vmx_index_range(const uint16_t *indices, uint32_t count, uint32_t *minimum, uint32_t *maximum);

/* memcpy, memset to 0 and a test for all zeroes, for large buffers: the source's lines touched ahead, a destination
line written whole zeroed in the cache first (not read); fastest with both 128-byte aligned, any alignment works */
void ps3_vmx_copy(void *destination, const void *source, uint32_t size);
void ps3_vmx_zero(void *destination, uint32_t size);
/* 1 when size bytes from source are all zeroes */
int ps3_vmx_zeroes(const void *source, uint32_t size);
/* memcpy to a 16-byte aligned destination from any source, 16 bytes at a time (newlib's goes a byte at a time where an
address is not 8-aligned) */
void ps3_vmx_copy_to_quadwords(void *destination, const void *source, uint32_t size);
/* count 128-byte lines from address (128-aligned) zeroed in the cache, not read from memory first; nothing where dcbz
does not zero exactly a line (the caller's stores then read it) */
void ps3_vmx_zero_lines(void *address, uint32_t count);
/* 1 when size bytes (a multiple of 16) at a and b (both 16-byte aligned) are the same */
int ps3_vmx_equal(const void *a, const void *b, uint32_t size);
/* the words of now unlike recorded's (both 16-byte aligned, count a multiple of 16), as (first + index, value) pairs in
their order, which recorded then holds; the pairs found. 64 bytes compared at once, the lines touched first */
uint32_t ps3_vmx_words_changed(const uint32_t *now, uint32_t *recorded, uint32_t count, uint32_t first,
	uint32_t *pairs);

#endif
