/* BYTE_SWAPPING.H */

#ifndef __BYTE_SWAPPING_H
#define __BYTE_SWAPPING_H
#pragma once

/* ---------- constants */

enum
{
	_1byte = 1,
	_2byte = -2,
	_4byte = -4,
	_8byte = -8,
	_begin_bs_array = -100,
	_end_bs_array = -101,
	_extern_bs_definition = -102,
	BYTE_SWAP_DEFINITION_SIGNATURE = 'bysw'
};

/* ---------- macros */

#define SWAP2(q) (((q)>>8) | (((q)<<8)&0xff00))
#define SWAP4(q) (((q)>>24) | (((q)>>8)&0xff00) | (((q)<<8)&0x00ff0000) | (((q)<<24)&0xff000000))

/* port: a field reversed in place at any alignment (the PC builds' little-endian network data on a
big-endian host): a scalar, an array of scalars, or every real or short of an aggregate */
#define BYTE_SWAP_SCALAR(size) ((void)sizeof(char[(size) <= 4 ? 1 : -1]))
#define BYTE_SWAP_FIELD(field) (BYTE_SWAP_SCALAR(sizeof(field)), byte_swap_parts(&(field), sizeof(field), 1))
#define BYTE_SWAP_ARRAY(field) \
	(BYTE_SWAP_SCALAR(sizeof((field)[0])), byte_swap_parts((field), sizeof((field)[0]), NUMBEROF(field)))
#define BYTE_SWAP_REALS(field) byte_swap_parts(&(field), sizeof(real), sizeof(field) / sizeof(real))
#define BYTE_SWAP_SHORTS(field) byte_swap_parts(&(field), sizeof(short), sizeof(field) / sizeof(short))

/* ---------- types */

typedef long byte_swap_code;

struct byte_swap_definition
{
	char *HALO_PTR32 name;
	long size;
	byte_swap_code *HALO_PTR32 codes;
	unsigned long signature;
	boolean verified;
};

/* ---------- prototypes/BYTE_SWAPPING.C */

void byte_swap_memory(void *memory, long count, byte_swap_code code);
long byte_swap_codes_size(char *name, byte_swap_code *codes);
void byte_swap_data(struct byte_swap_definition *definition, void *data, long data_count);
void byte_swap_data_explicit(char *name, long size, byte_swap_code *codes, long data_count, void *data);
void byte_swap_parts(void *data, long part_size, long count);

#endif // __BYTE_SWAPPING_H
