/* Custom Edition maps on the PS3 (HALO_CUSTOM_EDITION): the platform_ce_* hooks of a console that lends its own
tag cache's memory to the map, and the map's bitmaps made the Xbox's (warthog-xbox's xbox_ce.c, little-endian texels). */

#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "cache/physical_memory_map.h"
#include "rasterizer/rasterizer_swizzle.h"

#include <string.h>

/* (xbox_memory.c's) */
int ps3_ce_tag_cache_take(unsigned long tag_cache, unsigned long size);
void ps3_ce_tag_cache_give_back(void);
/* (d3d8_rsx.c's) */
void d3d8_tag_cache_loading(void);
/* (xbox_tag_swap.c's) */
void ce_byte_swap_structure_bsp(void *bsp, unsigned long address, long size);
unsigned long ce_resources_allocate(unsigned long size);
boolean ce_hud_bitmap_is_meter(void const *bitmap);
boolean ce_models_bitmap_is_multipurpose(void const *bitmap);
char const *tag_get_name(long tag_index);

/* (the 0x01600000 of physical_memory_map.c) */
#define PS3_XBOX_TAG_CACHE_SIZE 0x01600000UL
#define CE_FACES 6

/* (xbox_texture_cache.c's, local to each unit) */
enum
{
	_ce_bitmap_type_2d,
	_ce_bitmap_type_3d,
	_ce_bitmap_type_cube_map,
};

enum
{
	_ce_bitmap_compressed_bit = 1,
	_ce_bitmap_linear_bit = 4,
};

/* the map's tag cache is the Xbox tag cache's memory, which holds nothing while a Custom Edition map is loaded */
int platform_ce_alias = 1;

/* ---------- the tag cache's memory (cache_files.c) */

boolean ps3_ce_tag_cache_map(
	void)
{
	d3d8_tag_cache_loading();
	return ps3_ce_tag_cache_take((unsigned long)XBOX_ADDRESS(physical_memory_get_tag_cache_base_address()),
		PS3_XBOX_TAG_CACHE_SIZE) != 0;
}

void ps3_ce_tag_cache_unmap(
	void)
{
	ps3_ce_tag_cache_give_back();
}

/* (the 64 MB Xbox's: its memory borrowed for the load and its tag cache's last megabyte moved; none here) */
void platform_ce_load_begin(
	void)
{
}

void platform_ce_load_end(
	void)
{
}

void platform_ce_alias_whole(
	void)
{
}

unsigned long platform_ce_room_end(
	unsigned long end_free)
{
	return end_free;
}

void *platform_ce_scratch_malloc(
	size_t size)
{
	return malloc(size);
}

void platform_ce_scratch_free(
	void *address)
{
	free(address);
}

void *platform_ce_heap_realloc(
	void *address,
	size_t size)
{
	return realloc(address, size);
}

/* ce_bsp.c's and ce_models.c's XPhysicalAlloc (Makefile): the converted vertices in the tag cache's room, which
the checks count (ce_map_checks.c) and the BSP's or the map's unload gives back */
void *ps3_ce_vertex_alloc(
	unsigned long size,
	unsigned long physical_address,
	unsigned long alignment,
	unsigned long protect)
{
	unsigned long address = ce_resources_allocate((size + 15) & ~15UL);

	(void)physical_address;
	(void)alignment;
	(void)protect;
	return address ? xbox_pointer(address) : NULL;
}

void ps3_ce_vertex_free(
	void *address)
{
	(void)address;
}

/* ---------- byte order (ce_map_checks.c: a structure BSP read to be checked, as cache_files.c swaps one) */

void ps3_ce_structure_bsp_byte_swap(
	void *data,
	unsigned long address,
	unsigned long size)
{
	ce_byte_swap_structure_bsp(data, address, (long)size);
}

/* ---------- a Custom Edition map's bitmaps (xbox_texture_cache.c): Halo PC's layout made the Xbox's */

static unsigned long ce_morton(
	unsigned long x,
	unsigned long y,
	unsigned long z,
	unsigned long width,
	unsigned long height,
	unsigned long depth)
{
	unsigned long result = 0;
	unsigned long bit = 1;

	while (width > 1 || height > 1 || depth > 1)
	{
		if (width > 1)
		{
			if (x & 1)
				result |= bit;
			bit <<= 1;
			x >>= 1;
			width >>= 1;
		}
		if (height > 1)
		{
			if (y & 1)
				result |= bit;
			bit <<= 1;
			y >>= 1;
			height >>= 1;
		}
		if (depth > 1)
		{
			if (z & 1)
				result |= bit;
			bit <<= 1;
			z >>= 1;
			depth >>= 1;
		}
	}
	return result;
}

static void ce_swizzle_level(
	byte *destination,
	byte const *source,
	long width,
	long height,
	long depth,
	long pixel_bytes)
{
	long x, y, z;

	for (z = 0; z < depth; z++)
	{
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				memcpy(destination + ce_morton(x, y, z, width, height, depth) * pixel_bytes, source, pixel_bytes);
				source += pixel_bytes;
			}
		}
	}
}

static void ce_put_level(
	byte *destination,
	byte const *source,
	struct bitmap_data *bitmap,
	short level,
	long bytes)
{
	long pixel_bytes = bitmap_format_get_bits_per_pixel(bitmap->format) / 8;

	if (!TEST_FLAG(bitmap->flags, _ce_bitmap_compressed_bit) &&
		(pixel_bytes == 1 || pixel_bytes == 2 || pixel_bytes == 4))
	{
		ce_swizzle_level(destination, source, bitmap_mipmap_get_width(bitmap, level),
			bitmap_mipmap_get_height(bitmap, level),
			bitmap->type == _ce_bitmap_type_3d ? bitmap_mipmap_get_depth(bitmap, level) : 1, pixel_bytes);
	}
	else
	{
		memcpy(destination, source, bytes);
	}
}

/* a meter's or multipurpose map's channels where the Xbox's shaders read them; the texels are little-endian
A8R8G8B8, so bytes B, G, R, A */
static void ce_bitmap_channels(
	struct bitmap_data *bitmap,
	long size)
{
	boolean meter = ce_hud_bitmap_is_meter(bitmap);
	boolean multipurpose = ce_models_bitmap_is_multipurpose(bitmap);
	byte *pixel = (byte *)xbox_pointer(bitmap->base_address);
	long index;

	if ((!meter && !multipurpose) || TEST_FLAG(bitmap->flags, _ce_bitmap_compressed_bit) ||
		bitmap_format_get_bits_per_pixel(bitmap->format) != 32)
	{
		return;
	}
	for (index = 0; index < size / 4; index++, pixel += 4)
	{
		byte b = pixel[0], g = pixel[1], r = pixel[2], a = pixel[3];

		/* (a meter: its fill order in the color, its shape in alpha; a multipurpose map: specular,
		self-illumination, color change, auxiliary) */
		pixel[0] = a;
		pixel[1] = meter ? a : g;
		pixel[2] = meter ? a : b;
		pixel[3] = r;
	}
}

void platform_ce_bitmap_loaded(
	struct bitmap_data *bitmap)
{
	long size = rasterizer_xbox_bitmap_get_pixel_data_size(bitmap);
	long source_size = bitmap->pixels_size > size ? bitmap->pixels_size : size;
	short levels = rasterizer_xbox_bitmap_get_max_mipmap_count(bitmap);
	byte *pixels = (byte *)xbox_pointer(bitmap->base_address);
	byte *source;
	short level;

	if (TEST_FLAG(bitmap->flags, _ce_bitmap_linear_bit))
		return;
	/* (nothing moves in a compressed bitmap that isn't a cube map) */
	if (TEST_FLAG(bitmap->flags, _ce_bitmap_compressed_bit) && bitmap->type != _ce_bitmap_type_cube_map)
		return;
	source = (byte *)malloc(source_size);
	if (!source)
	{
		static boolean reported;

		if (!reported)
		{
			error(_error_silent, "Custom Edition maps: no memory to lay out a bitmap of %ld bytes (%s)", source_size,
				bitmap->tag_index != NONE ? tag_get_name(bitmap->tag_index) : "(none)");
		}
		reported = TRUE;
		return;
	}
	memcpy(source, pixels, bitmap->pixels_size);
	if (bitmap->type == _ce_bitmap_type_cube_map)
	{
		long face_stride = size / CE_FACES;
		long face_offset = 0;
		long pc_offset = 0;

		for (level = 0; level <= levels; level++)
		{
			long level_bytes = bitmap_mipmap_get_pixel_data_size(bitmap, level) / CE_FACES;
			long face;

			for (face = 0; face < CE_FACES; face++)
			{
				/* (Halo PC's second and third faces are the Xbox's third and second) */
				long pc_face = face == 1 ? 2 : face == 2 ? 1 : face;

				ce_put_level(pixels + face * face_stride + face_offset, source + pc_offset + pc_face * level_bytes,
					bitmap, level, level_bytes);
			}
			face_offset += level_bytes;
			pc_offset += level_bytes * CE_FACES;
		}
	}
	else
	{
		long offset = 0;

		for (level = 0; level <= levels; level++)
		{
			long level_bytes = bitmap_mipmap_get_pixel_data_size(bitmap, level);

			ce_put_level(pixels + offset, source + offset, bitmap, level, level_bytes);
			offset += level_bytes;
		}
	}
	free(source);
	ce_bitmap_channels(bitmap, size);
}
