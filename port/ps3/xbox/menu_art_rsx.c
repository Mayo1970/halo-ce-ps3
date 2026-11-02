/* The Chupathingy menus on the PS3: their XML (menu_files.c's embedded files) and their pictures as 1x RSX textures
(tools/ps3_menu_art.py), which d3d8_rsx.c draws in place of the placeholder textures menu_files.c registers. */

#include <stdlib.h>
#include <string.h>

#include "menu_files.h"
#include "ps3_gpu.h"
#include "zlib_prefixed.h"

/* GCM's values (gcm_sys.h cannot be included with the game's 32-bit long) */
#define GCM_TEXTURE_FORMAT_A8R8G8B8 5
#define GCM_TEXTURE_FORMAT_LIN 0x20
#define GCM_TEXTURE_DIMS_2D 2
/* menu_files.c's MAXIMUM_ART */
#define MAXIMUM_REGISTERED 1024

/* a picture at its size in the 640x480 menu: A8R8G8B8 rows, compressed */
struct ps3_menu_picture
{
	const char *name;
	unsigned short width, height;
	const unsigned char *data;
	unsigned int size;
};

#include "ps3_menu_assets.h"

#define PICTURE_COUNT (sizeof(ps3_menu_pictures) / sizeof(ps3_menu_pictures[0]))

/* (the game thread's) each placeholder's Data and its picture; the reader rechecks after a change */
static volatile struct
{
	DWORD data;
	int picture;
} registered[MAXIMUM_REGISTERED];
static volatile int registered_count;
static volatile unsigned int registered_changes, forgotten;

/* (the Direct3D thread's) */
static struct
{
	struct ps3_gpu_texture gpu;
	void *memory;
	uint32_t bytes;
	int made, failed;
} textures[PICTURE_COUNT];
static unsigned int seen_changes, seen_forgotten, epoch;

static int picture_find(const char *name)
{
	int low = 0, high = (int)PICTURE_COUNT - 1;

	while (low <= high)
	{
		int middle = (low + high) / 2;
		int order = strcmp(name, ps3_menu_pictures[middle].name);

		if (!order)
			return middle;
		if (order < 0)
			high = middle - 1;
		else
			low = middle + 1;
	}
	return -1;
}

void menu_art_rsx_register(unsigned long data, const char *png)
{
	int picture = picture_find(png), index, count = registered_count;

	if (picture < 0)
	{
		platform_log("menus: %s has no PS3 texture (tools/ps3_menu_art.py build)", png);
		return;
	}
	for (index = 0; index < count && registered[index].data != data; index++)
		;
	if (index == MAXIMUM_REGISTERED)
		return;
	/* (the picture before the Data, which the reader matches) */
	registered[index].picture = picture;
	__sync_synchronize();
	registered[index].data = data;
	__sync_synchronize();
	if (index == count)
		registered_count = count + 1;
	registered_changes++;
}

void menu_art_rsx_forget(void)
{
	registered_count = 0;
	__sync_synchronize();
	forgotten++;
	registered_changes++;
}

/* (once a frame) the epoch of the registrations: a texture checks its picture again when it moves. The textures of
art forgotten go (ps3_gpu_memory_free waits for the frames that draw them) */
unsigned int menu_art_rsx_frame(void)
{
	unsigned int changes = registered_changes, forgets = forgotten;
	unsigned long index;

	if (forgets != seen_forgotten)
	{
		seen_forgotten = forgets;
		for (index = 0; index < PICTURE_COUNT; index++)
		{
			if (textures[index].made)
				ps3_gpu_memory_free(textures[index].memory, textures[index].bytes, _ps3_gpu_memory_texture);
			memset(&textures[index], 0, sizeof(textures[index]));
		}
	}
	if (changes != seen_changes)
	{
		seen_changes = changes;
		epoch++;
	}
	return epoch;
}

static int texture_make(int picture, struct ps3_gpu_texture *gpu)
{
	const struct ps3_menu_picture *source = &ps3_menu_pictures[picture];
	uLongf size = (uLongf)source->width * source->height * 4;
	uint32_t pitch = ((uint32_t)source->width * 4 + 63) & ~63u, offset, row;
	unsigned char *decoded, *memory;

	if (textures[picture].made)
	{
		*gpu = textures[picture].gpu;
		return 1;
	}
	if (textures[picture].failed)
		return 0;
	textures[picture].failed = 1;
	decoded = malloc(size);
	if (!decoded || z_uncompress(decoded, &size, source->data, source->size) != Z_OK ||
		size != (uLongf)source->width * source->height * 4)
	{
		platform_log("menus: could not decode %s", source->name);
		free(decoded);
		return 0;
	}
	memory = ps3_gpu_memory_allocate(pitch * source->height, 128, _ps3_gpu_memory_texture, &offset);
	if (!memory)
	{
		platform_log("menus: no RSX memory for %s (%u KB)", source->name, (unsigned)(pitch * source->height / 1024));
		free(decoded);
		return 0;
	}
	/* (in order, a row at a time: the RSX's memory is write-combined) */
	for (row = 0; row < source->height; row++)
		memcpy(memory + row * pitch, decoded + row * source->width * 4, (size_t)source->width * 4);
	free(decoded);
	ps3_gpu_textures_written();
	/* linear, with the coordinates the placeholder's: 0 to 1 over the picture */
	memset(gpu, 0, sizeof(*gpu));
	gpu->offset = offset;
	gpu->pitch = pitch;
	gpu->width = source->width;
	gpu->height = source->height;
	gpu->depth = 1;
	gpu->format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	gpu->levels = 1;
	gpu->dimension = GCM_TEXTURE_DIMS_2D;
	gpu->remap = 0xaae4;
	textures[picture].gpu = *gpu;
	textures[picture].memory = memory;
	textures[picture].bytes = pitch * source->height;
	textures[picture].made = 1;
	textures[picture].failed = 0;
	return 1;
}

/* (the Direct3D thread) the picture's texture for the placeholder whose Data this is */
int menu_art_rsx_texture(DWORD data, struct ps3_gpu_texture *gpu)
{
	int count = registered_count, index;

	for (index = 0; data && index < count; index++)
	{
		if (registered[index].data == data)
			return texture_make(registered[index].picture, gpu);
	}
	return 0;
}
