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

/* memcpy, memset to 0 and a test for all zeroes, for large buffers: the source's lines touched ahead, a destination
line written whole zeroed in the cache first (not read); fastest with both 128-byte aligned, any alignment works */
void ps3_vmx_copy(void *destination, const void *source, uint32_t size);
void ps3_vmx_zero(void *destination, uint32_t size);
/* 1 when size bytes from source are all zeroes */
int ps3_vmx_zeroes(const void *source, uint32_t size);

#endif
