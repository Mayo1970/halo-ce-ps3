/* XBOX_TAG_SWAP.H: the swap of an Xbox map's little-endian data to the PS3's byte order (xbox_tag_swap.c).
Fixed-width types only: tools/ps3_tag_swap_check.c builds the same code on the PC. */

#ifndef __XBOX_TAG_SWAP_H
#define __XBOX_TAG_SWAP_H

#include <stdint.h>

enum tag_swap_operation
{
	_tag_swap_skip,
	_tag_swap_2,
	_tag_swap_4,
	_tag_swap_block,
	_tag_swap_reference,
	_tag_swap_data,
};

/* what a data field holds (tools/ps3_tag_swap.py, DATA_KINDS) */
enum tag_swap_data_kind
{
	_tag_swap_data_bytes,
	_tag_swap_data_file,
	_tag_swap_data_swap2,
	_tag_swap_data_swap4,
	_tag_swap_data_script_syntax,
	_tag_swap_data_animation_default_data,
	_tag_swap_data_animation_frame_data,
	_tag_swap_data_recorded_animation,
	_tag_swap_data_bsp_uncompressed_vertices,
	_tag_swap_data_bsp_compressed_vertices,
};

enum tag_swap_hook
{
	_tag_swap_hook_none,
	_tag_swap_hook_model_part,
};

/* count units (bytes for a skip); argument: a block's element struct, or a data kind */
struct tag_swap_op
{
	uint8_t operation;
	uint16_t argument;
	uint16_t count;
};

struct tag_swap_struct
{
	uint32_t size;
	uint16_t first_op;
	uint16_t op_count;
	uint8_t hook;
};

struct tag_swap_group
{
	uint32_t group_tag;
	uint16_t struct_index;
};

struct tag_swap_statistics
{
	uint32_t tags;
	uint32_t structs;
	uint32_t blocks;
	uint32_t references;
	uint32_t data;
	uint32_t model_parts;
	uint32_t bytes_swapped;
	uint32_t unknown_groups;
	uint32_t stale_references;
	uint32_t errors;
};

/* tag_swap_coverage marks each byte the swap reads or skips (the PC check); NULL on the PS3 */
typedef void (*tag_swap_coverage_proc)(uint32_t address, uint32_t size);

/* A map header (0x800 bytes), from a map or a cache file: swapped if it is in the Xbox byte order.
Returns 1 if it swapped. A header that is already swapped stays as it is. */
int tag_swap_cache_file_header(void *header);

/* The tag data that cache_files.c read to its Xbox address: swapped if its tag header is in the
Xbox byte order. bytes is where the data is in memory (the address itself on the PS3). */
int tag_swap_tag_data(void *bytes, uint32_t address, uint32_t size, struct tag_swap_statistics *statistics,
	tag_swap_coverage_proc coverage);

/* a structure BSP that cache_files.c read to its address after the tag data */
int tag_swap_structure_bsp(void *bytes, uint32_t address, uint32_t size, struct tag_swap_statistics *statistics,
	tag_swap_coverage_proc coverage);

#endif
