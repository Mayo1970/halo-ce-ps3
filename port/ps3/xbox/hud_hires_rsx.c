/* The PS3's button icons (display.button_icons): hud_hires.c's textures for the button bitmaps and message icons only,
on the RSX. hud_hires_tags.c finds their bitmaps; d3d8_rsx.c draws a texture in a bitmap's place. */

#include <stdlib.h>
#include <string.h>

#include "port_config.h"
#include "ps3_gpu.h"
#include "zlib_prefixed.h"

/* GCM's values (gcm_sys.h cannot be included with the game's 32-bit long) */
#define GCM_TEXTURE_FORMAT_A8R8G8B8 5
#define GCM_TEXTURE_DIMS_2D 2

/* a texture: its RSX texels (tools/ps3_button_assets.py embed), compressed, and their size */
struct ps3_button_texels
{
	const unsigned char *data;
	unsigned int size;
	unsigned int texels_size;
};

/* the bitmap it stands for (as hud_hires.h's), its size and mip levels, and its texels for STYLE A and STYLE B */
struct ps3_button_embedded
{
	const char *tag;
	int bitmap;
	unsigned int width, height;
	unsigned int crc;
	unsigned int sprites;
	unsigned int size;
	unsigned int levels;
	struct ps3_button_texels styles[2];
};

/* a HUD sprite drawn from a cell of its own (tools/ps3_button_assets.py): x0, x1, y0, y1 in fractions, the width in texels */
struct ps3_button_place
{
	int asset;
	int sequence;
	int frame, frames;
	float cell[4];
	float advance;
};

#include "ps3_buttons.h"

#define BUTTON_ASSETS (sizeof(ps3_buttons) / sizeof(ps3_buttons[0]))
#define MAXIMUM_PLACEHOLDERS 8

/* the game's (port/linux/game/hud_hires_tags.c) */
long hud_hires_asset_at(unsigned long address, long width, long height);

/* 0 the maps' own buttons, 1 STYLE A, 2 STYLE B; -1 before the game thread first reads it */
static volatile int button_style = -1;

/* each style's RSX texture of each asset, made by the Direct3D thread when first drawn */
static struct
{
	struct ps3_gpu_texture gpu;
	int made;
	int failed;
} textures[BUTTON_ASSETS][2];

static struct
{
	volatile DWORD data;
	volatile long asset;
} placeholders[MAXIMUM_PLACEHOLDERS];

/* the game thread's read of display.button_icons (config_string's text is the writer's, the game thread's) */
void hud_hires_style_commit(void)
{
	static unsigned long read_at = (unsigned long)-1;
	const char *style;

	if (read_at == config_changes())
		return;
	read_at = config_changes();
	style = config_string("display.button_icons");
	button_style = !strcmp(style, "xbox") ? 0 : !strcmp(style, "style_b") ? 2 : 1;
}

int hud_hires_rsx_style(void)
{
	return button_style;
}

/* ---------- hud_hires_tags.c's */

long hud_hires_asset_count(void)
{
	return (long)BUTTON_ASSETS;
}

char const *hud_hires_asset_tag(long asset)
{
	return ps3_buttons[asset].tag;
}

long hud_hires_asset_bitmap(long asset)
{
	return ps3_buttons[asset].bitmap;
}

long hud_hires_asset_fits(long asset, long width, long height)
{
	return width == (long)ps3_buttons[asset].width && height == (long)ps3_buttons[asset].height;
}

unsigned long hud_hires_asset_sprites(long asset)
{
	return ps3_buttons[asset].sprites;
}

/* (the game thread, so the style is read here: the check is kept until the settings change) */
int hud_hires_sprites_drawable(long asset, unsigned long address, unsigned long level0_size)
{
	hud_hires_style_commit();
	return button_style > 0 && asset >= 0 && asset < (long)BUTTON_ASSETS && ps3_buttons[asset].sprites &&
		(unsigned int)z_crc32(0, (const Bytef *)(__uint64)address, (uInt)level0_size) == ps3_buttons[asset].crc;
}

int hud_hires_asset_sprite_place(long asset, short sequence, short frame, float *cell, float *advance)
{
	unsigned int index;

	for (index = 0; index < sizeof(ps3_button_places) / sizeof(ps3_button_places[0]); index++)
	{
		const struct ps3_button_place *place = &ps3_button_places[index];

		if (place->asset == asset && place->sequence == sequence && frame % place->frames == place->frame)
		{
			memcpy(cell, place->cell, sizeof(place->cell));
			*advance = place->advance;
			return 1;
		}
	}
	return 0;
}

void hud_hires_register_placeholder(long asset, unsigned long const *texture)
{
	long index, free_index = -1;

	for (index = 0; index < MAXIMUM_PLACEHOLDERS; index++)
	{
		if (placeholders[index].data && placeholders[index].asset == asset)
			break;
		if (!placeholders[index].data && free_index < 0)
			free_index = index;
	}
	if (index == MAXIMUM_PLACEHOLDERS)
		index = free_index;
	if (index < 0)
		return;
	placeholders[index].asset = asset;
	placeholders[index].data = texture ? texture[1] : 0;
}

/* ---------- d3d8_rsx.c's (the Direct3D thread) */

static int texture_make(long asset, int style, struct ps3_gpu_texture *gpu)
{
	const struct ps3_button_embedded *embedded = &ps3_buttons[asset];
	const struct ps3_button_texels *texels = &embedded->styles[style - 1];
	uLongf size = texels->texels_size;
	unsigned char *decoded, *memory;
	uint32_t offset;

	if (textures[asset][style - 1].made)
	{
		*gpu = textures[asset][style - 1].gpu;
		return 1;
	}
	if (textures[asset][style - 1].failed)
		return 0;
	textures[asset][style - 1].failed = 1;
	decoded = malloc(texels->texels_size);
	if (!decoded || z_uncompress(decoded, &size, texels->data, texels->size) != Z_OK || size != texels->texels_size)
	{
		platform_log("button icons: could not decode %s's texture", embedded->tag);
		free(decoded);
		return 0;
	}
	memory = ps3_gpu_memory_allocate(texels->texels_size, 128, _ps3_gpu_memory_texture, &offset);
	if (!memory)
	{
		platform_log("button icons: no RSX memory for %s's texture", embedded->tag);
		free(decoded);
		return 0;
	}
	memcpy(memory, decoded, texels->texels_size);
	free(decoded);
	ps3_gpu_textures_written();
	/* swizzled A8R8G8B8, every level to 1x1 */
	memset(gpu, 0, sizeof(*gpu));
	gpu->offset = offset;
	gpu->pitch = embedded->size * 4;
	gpu->width = (uint16_t)embedded->size;
	gpu->height = (uint16_t)embedded->size;
	gpu->depth = 1;
	gpu->format = GCM_TEXTURE_FORMAT_A8R8G8B8;
	gpu->levels = (uint8_t)embedded->levels;
	gpu->dimension = GCM_TEXTURE_DIMS_2D;
	gpu->remap = 0xaae4;
	textures[asset][style - 1].gpu = *gpu;
	textures[asset][style - 1].made = 1;
	textures[asset][style - 1].failed = 0;
	return 1;
}

/* the texture drawn for the bitmap whose first level (level0_size bytes) is at texels, of this size: the style
not XBOX, and the pixels those the texture was drawn for (the English maps') */
int hud_hires_rsx_override(const unsigned char *texels, unsigned long width, unsigned long height,
	unsigned long level0_size, struct ps3_gpu_texture *gpu)
{
	int style = button_style;
	long asset;

	if (style <= 0)
		return 0;
	asset = hud_hires_asset_at((unsigned long)(__uint64)texels, (long)width, (long)height);
	if (asset < 0 || asset >= (long)BUTTON_ASSETS || ps3_buttons[asset].sprites)
		return 0;
	if ((unsigned int)z_crc32(0, texels, (uInt)level0_size) != ps3_buttons[asset].crc)
		return 0;
	return texture_make(asset, style, gpu);
}

/* the texture of the sprites the game draws from the placeholder whose Data this is */
int hud_hires_rsx_placeholder(DWORD data, struct ps3_gpu_texture *gpu)
{
	int style = button_style;
	long index;

	for (index = 0; data && style > 0 && index < MAXIMUM_PLACEHOLDERS; index++)
	{
		if (placeholders[index].data == data)
			return texture_make(placeholders[index].asset, style, gpu);
	}
	return 0;
}

/* (the Direct3D SPU's) an asset's texture in a style */
int hud_hires_rsx_asset_texture(long asset, int style, struct ps3_gpu_texture *gpu)
{
	if (asset < 0 || asset >= (long)BUTTON_ASSETS || style < 1 || style > 2)
		return 0;
	return texture_make(asset, style, gpu);
}

/* (the Direct3D SPU's) the placeholders' data and assets */
void hud_hires_rsx_placeholders(unsigned int words[MAXIMUM_PLACEHOLDERS][2])
{
	long index;

	for (index = 0; index < MAXIMUM_PLACEHOLDERS; index++)
	{
		words[index][0] = (unsigned int)placeholders[index].data;
		words[index][1] = (unsigned int)placeholders[index].asset;
	}
}
