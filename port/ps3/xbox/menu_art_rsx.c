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
#define MAXIMUM_MAP_FRAMES 128

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
/* a registration's picture for a button icon: BUTTON_PICTURE(asset), below 0 (hud_hires_rsx.c's assets) */
#define BUTTON_PICTURE(asset) (-2 - (int)(asset))

long hud_hires_asset_count(void);
char const *hud_hires_asset_tag(long asset);
unsigned long hud_hires_asset_sprites(long asset);
int hud_hires_rsx_style(void);
int hud_hires_rsx_asset_texture(long asset, int style, struct ps3_gpu_texture *gpu);

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
} textures[PICTURE_COUNT + MAXIMUM_MAP_FRAMES];
static struct
{
	char tag[256], path[256];
	long index, width, height, offset, bytes, row_pitch;
	DWORD format, size;
} map_frames[MAXIMUM_MAP_FRAMES];
static int map_frame_count;
static unsigned int seen_changes, seen_forgotten, epoch;

int xgpu_texture_decode_menu(const struct xgpu_texture_description *description, const unsigned char *source,
	unsigned long bytes, unsigned long row_pitch, DWORD *destination);

int menu_art_rsx_map_frame(const char *tag, long frame, long *width, long *height)
{
	int index;

	for (index = 0; index < map_frame_count; index++)
	{
		if (map_frames[index].index == frame && !_stricmp(map_frames[index].tag, tag))
		{
			*width = map_frames[index].width;
			*height = map_frames[index].height;
			return index;
		}
	}
	return -1;
}

void menu_art_rsx_keep_map_frame(const char *tag, long frame, long width, long height, long offset, long bytes,
	unsigned long format, unsigned long size, long row_pitch, const char *path)
{
	long old_width, old_height;
	int index;

	if (!tag || !path || strlen(tag) >= sizeof(map_frames[0].tag) || strlen(path) >= sizeof(map_frames[0].path) ||
		width <= 0 || width > 2048 || height <= 0 || height > 2048 || offset < 0 || bytes <= 0 ||
		bytes > 16 * 1024 * 1024 || row_pitch < 0 || map_frame_count == MAXIMUM_MAP_FRAMES ||
		menu_art_rsx_map_frame(tag, frame, &old_width, &old_height) >= 0)
		return;
	index = map_frame_count;
	strcpy(map_frames[index].tag, tag);
	strcpy(map_frames[index].path, path);
	map_frames[index].index = frame;
	map_frames[index].width = width;
	map_frames[index].height = height;
	map_frames[index].offset = offset;
	map_frames[index].bytes = bytes;
	map_frames[index].row_pitch = row_pitch;
	map_frames[index].format = format;
	map_frames[index].size = size;
	__sync_synchronize();
	map_frame_count = index + 1;
}

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

/* the button icon asset (a whole bitmap, not a sheet) for a ui.map bitmap tag, or -1 */
static long button_asset(const char *tag)
{
	long asset, count = hud_hires_asset_count();

	for (asset = 0; tag && asset < count; asset++)
	{
		if (!hud_hires_asset_sprites(asset) && !_stricmp(tag, hud_hires_asset_tag(asset)))
			return asset;
	}
	return -1;
}

/* (menu_tags.c) whether a game map's menus draw this missing ui.map button as the PS3's icon */
int menu_art_rsx_button(const char *tag)
{
	return button_asset(tag) >= 0;
}

void menu_art_rsx_register(unsigned long data, const char *png)
{
	long asset = button_asset(png);
	int picture = asset >= 0 ? BUTTON_PICTURE(asset) : picture_find(png), index, count = registered_count;

	if (picture == -1)
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

void menu_art_rsx_register_map(unsigned long data, int frame)
{
	int index, count = registered_count;

	if (frame < 0 || frame >= map_frame_count)
		return;
	for (index = 0; index < count && registered[index].data != data; index++)
		;
	if (index == MAXIMUM_REGISTERED)
		return;
	registered[index].picture = (int)PICTURE_COUNT + frame;
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
		for (index = 0; index < PICTURE_COUNT + MAXIMUM_MAP_FRAMES; index++)
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
	const struct ps3_menu_picture *source = picture < PICTURE_COUNT ? &ps3_menu_pictures[picture] : NULL;
	int frame = picture - (int)PICTURE_COUNT;
	unsigned int width = source ? source->width : map_frames[frame].width;
	unsigned int height = source ? source->height : map_frames[frame].height;
	uLongf size = (uLongf)width * height * 4;
	uint32_t pitch = ((uint32_t)width * 4 + 63) & ~63u, offset, row;
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
	if (!decoded)
		return 0;
	if (source)
	{
		if (z_uncompress(decoded, &size, source->data, source->size) != Z_OK || size != (uLongf)width * height * 4)
		{
			free(decoded);
			return 0;
		}
	}
	else
	{
		struct xgpu_texture_description description;
		unsigned char *pixels = calloc(1, map_frames[frame].bytes + 4);
		HANDLE file = CreateFileA(map_frames[frame].path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		OVERLAPPED position;
		DWORD got = 0;
		int ok;

		memset(&position, 0, sizeof(position));
		position.Offset = map_frames[frame].offset;
		xgpu_texture_describe(map_frames[frame].format, map_frames[frame].size, &description);
		ok = pixels && file != INVALID_HANDLE_VALUE && ReadFile(file, pixels, map_frames[frame].bytes, &got, &position) &&
			got == (DWORD)map_frames[frame].bytes && description.width == width && description.height == height &&
			xgpu_texture_decode_menu(&description, pixels, got, map_frames[frame].row_pitch, (DWORD *)decoded);
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
		free(pixels);
		if (!ok)
		{
			free(decoded);
			return 0;
		}
	}
	memory = ps3_gpu_memory_allocate(pitch * height, 128, _ps3_gpu_memory_texture, &offset);
	if (!memory)
	{
		platform_log("menus: no RSX memory for picture (%u KB)", (unsigned)(pitch * height / 1024));
		free(decoded);
		return 0;
	}
	/* (in order, a row at a time: the RSX's memory is write-combined) */
	for (row = 0; row < height; row++)
		memcpy(memory + row * pitch, decoded + row * width * 4, (size_t)width * 4);
	free(decoded);
	ps3_gpu_textures_written();
	/* linear, with the coordinates the placeholder's: 0 to 1 over the picture */
	memset(gpu, 0, sizeof(*gpu));
	gpu->offset = offset;
	gpu->pitch = pitch;
	gpu->width = width;
	gpu->height = height;
	gpu->depth = 1;
	gpu->format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
	gpu->levels = 1;
	gpu->dimension = GCM_TEXTURE_DIMS_2D;
	gpu->remap = 0xaae4;
	textures[picture].gpu = *gpu;
	textures[picture].memory = memory;
	textures[picture].bytes = pitch * height;
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
		{
			int picture = registered[index].picture;

			/* (none in the Xbox style: its art is ui.map's own, which a game map has not got) */
			if (picture < -1)
				return hud_hires_rsx_asset_texture(-2 - picture, hud_hires_rsx_style(), gpu);
			if (picture >= (int)PICTURE_COUNT && !texture_make(picture, gpu))
			{
				picture = picture_find("ce/port/blank__0.png");
				return picture >= 0 && texture_make(picture, gpu);
			}
			return texture_make(picture, gpu);
		}
	}
	return 0;
}
