/* CACHE_FILES.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/cache/cache_files.c. */

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "tag_files/tag_groups.h"
#include "tag_files/files.h"
#include "cache_files.h"
#include "physical_memory_map.h"
#include "sound_cache.h"
#include "texture_cache.h"
#include "interface/ui_widget.h"
#include "scenario/scenario_definitions.h"
#include "sound/sound_manager.h"

/* ---------- constants */

/* ---------- macros */

#define STRUCTURE_BSP_TAG 'sbsp'
#define CACHE_FILE_TAG_HEADER_SIGNATURE 'tags'
#define CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE 'sbsp'
#define CACHE_FILE_HEADER_SIGNATURE 'head'
#define CACHE_FILE_FOOTER_SIGNATURE 'foot'

#ifdef HALO_PS3
/* port: unlike x86, the PPE can see the cache thread's read_complete before the data it covers */
#define CACHE_IO_BARRIER() __sync_synchronize()
#else
#define CACHE_IO_BARRIER()
#endif

/* ---------- structures */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *HALO_PTR32 name;
	void *HALO_PTR32 base_address;
	unsigned long unused[2];
};

struct cache_file_tag_header
{
	struct cache_file_tag_instance *HALO_PTR32 tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	void *HALO_PTR32 vertex_buffers;
	long index_buffer_count;
	void *HALO_PTR32 index_buffers;
	unsigned long signature;
};

struct cache_file_structure_bsp_header
{
	void *HALO_PTR32 base_address;
	long vertex_buffer_count;
	void *HALO_PTR32 vertex_buffers;
	long index_buffer_count;
	void *HALO_PTR32 index_buffers;
	unsigned long signature;
};

struct cache_file_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
	byte reserved18[8];
	char name[0x20];
	char build[0x20];
	byte reserved60[4];
	unsigned long checksum;
	byte reserved68[0x794];
	unsigned long footer_signature;
};

struct cache_file_globals
{
	boolean tags_loaded;
	byte pad1[3];
	struct cache_file_header header;
	struct cache_file_tag_header *HALO_PTR32 tag_header;
	struct cache_file_structure_bsp_header *HALO_PTR32 structure_bsp_header;
};

typedef char verify_cache_file_tag_instance_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

typedef char verify_cache_file_tag_header_count_offset[
	offsetof(struct cache_file_tag_header, tag_count) == 0xC ? 1 : -1];

typedef char verify_cache_file_globals_size[
	sizeof(struct cache_file_globals) == 0x80C ? 1 : -1];
typedef char verify_cache_file_header_size[
	sizeof(struct cache_file_header) == 0x800 ? 1 : -1];

/* ---------- prototypes */

static struct cache_file_tag_instance *cache_get_tag_instance(
	long tag_index);

/* ---------- globals */

static struct cache_file_globals cache_file_globals = { 0 };
extern struct cache_file_tag_instance *global_tag_instances;
static char const *data_00316820[] =
{
	"d:\\maps_de\\",
	"d:\\maps_fr\\",
	"d:\\maps_es\\",
	"d:\\maps_it\\",
	"d:\\maps\\",
	NULL
};

/* ---------- private code */

static struct cache_file_tag_instance *cache_get_tag_instance(
	long tag_index)
{
	short absolute_index;
	struct cache_file_tag_instance *tag_instance;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		518,
		cache_file_globals.tags_loaded);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		519,
		global_tag_instances);

	absolute_index = (short)tag_index;
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		522,
		absolute_index >= 0 && absolute_index < cache_file_globals.tag_header->tag_count,
		csprintf(temporary, "i don't think %08x is a tag index", tag_index));

	tag_instance = &global_tag_instances[absolute_index];
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		526,
		!(tag_index & 0xFFFF0000) || tag_instance->tag_index == tag_index,
		csprintf(temporary, "i don't think %08x is a tag index", tag_index));

	return tag_instance;
}

/* ---------- public code */

char const *cache_files_map_directory(
	void)
{
	char const *map_directory;
	struct file_reference reference;
	long directory_index;

	switch (XGetLanguage())
	{
	case XC_LANGUAGE_GERMAN:
		map_directory = "d:\\maps_de\\";
		break;
	case XC_LANGUAGE_FRENCH:
		map_directory = "d:\\maps_fr\\";
		break;
	case XC_LANGUAGE_SPANISH:
		map_directory = "d:\\maps_es\\";
		break;
	case XC_LANGUAGE_ITALIAN:
		map_directory = "d:\\maps_it\\";
		break;
	default:
		map_directory = "d:\\maps\\";
		break;
	}

	if (!file_exists(file_reference_create_from_path(&reference, map_directory, TRUE)))
	{
		for (directory_index = 0; data_00316820[directory_index]; directory_index++)
		{
			if (file_exists(file_reference_create_from_path(
				&reference,
				data_00316820[directory_index],
				TRUE)))
			{
				map_directory = data_00316820[directory_index];
				break;
			}
		}

		match_vassert(
			"c:\\halo\\SOURCE\\cache\\cache_files.c",
			60,
			data_00316820[directory_index],
			"no valid map directory exists");
	}

	return map_directory;
}

void scenario_tags_unload(
	void)
{
	/* port: the high-res HUD forgets this map's bitmaps (port/linux/game/hud_hires_tags.c) */
	{
		extern void hud_hires_tags_unloaded(void);

		hud_hires_tags_unloaded();
	}
	sound_cache_close();
	texture_cache_close();
	/* port: the menus' tags go and the map's own table comes back (port/linux/game/menu_tags.c),
	after the texture cache, which writes to the bitmaps it loaded (theirs too) as it closes */
	{
		extern void menu_tags_unloaded(void);

		menu_tags_unloaded();
	}
	cache_file_close();
	tags_header_deregister_vertex_and_index_buffers(cache_file_globals.tag_header);
	cache_file_globals.tags_loaded = FALSE;
	global_tag_instances = NULL;

	return;
}

/* port: the loaded tags' table, and its tags' count, for the menus' tags
(port/linux/game/menu_tags.c), which a copy with theirs added replaces */
void *cache_files_tag_instances(
	long *count)
{
	*count = cache_file_globals.tags_loaded ? cache_file_globals.tag_header->tag_count : 0;
	return cache_file_globals.tags_loaded ? cache_file_globals.tag_header->tag_instances : NULL;
}

void cache_files_set_tag_instances(
	void *instances,
	long count)
{
	cache_file_globals.tag_header->tag_instances = instances;
	cache_file_globals.tag_header->tag_count = count;
	global_tag_instances = instances;
}

void tag_files_open(
	void)
{
	cache_files_initialize();

	return;
}

void tag_files_close(
	void)
{
	cache_files_dispose();

	return;
}

unsigned long cache_files_get_checksum(
	void)
{
	return cache_file_globals.header.checksum;
}

unsigned long tag_groups_checksum(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		327,
		cache_file_globals.tags_loaded);

	return cache_file_globals.tag_header->checksum;
}

long tag_loaded(
	long group_tag,
	char const *name)
{
	short absolute_index;
	long result = NONE;

	if (cache_file_globals.tags_loaded)
	{
		match_assert(
			"c:\\halo\\SOURCE\\cache\\cache_files.c",
			346,
			global_tag_instances);

		for (absolute_index = 0;
			absolute_index < cache_file_globals.tag_header->tag_count;
			absolute_index++)
		{
			if (group_tag == global_tag_instances[absolute_index].group_tag &&
				!_stricmp(name, global_tag_instances[absolute_index].name))
			{
				result = global_tag_instances[absolute_index].tag_index;
				break;
			}
		}
	}

	return result;
}

void cache_files_enable_writes(
	void)
{
	XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READWRITE);

	return;
}

void cache_files_disable_writes(
	void)
{
	XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READONLY);
	XPhysicalProtect(
		cache_file_globals.tag_header->vertex_buffers,
		cache_file_globals.tag_header->vertex_buffer_count * 12,
		PAGE_READWRITE);
	XPhysicalProtect(
		cache_file_globals.tag_header->index_buffers,
		cache_file_globals.tag_header->index_buffer_count * 12,
		PAGE_READWRITE);

	if (cache_file_globals.structure_bsp_header)
	{
		XPhysicalProtect(
			cache_file_globals.structure_bsp_header->vertex_buffers,
			cache_file_globals.structure_bsp_header->vertex_buffer_count * 12,
			PAGE_READWRITE);
		XPhysicalProtect(
			cache_file_globals.structure_bsp_header->index_buffers,
			cache_file_globals.structure_bsp_header->index_buffer_count * 12,
			PAGE_READWRITE);
	}

	return;
}

boolean tag_block_resize(
	struct tag_block *block,
	long count)
{
	error(_error_silent, "tag_block_resize() is not supported with a cache file active");

	return FALSE;
}

boolean tag_data_resize(
	struct tag_data *data,
	long size)
{
	error(_error_silent, "tag_data_resize() is not supported with a cache file active");

	return FALSE;
}

long tag_block_add_element(
	struct tag_block *block)
{
	error(_error_silent, "tag_block_add_element() is not supported with a cache file active");

	return NONE;
}

void tag_block_delete_element(
	struct tag_block *block,
	long element_index)
{
	error(_error_silent, "tag_block_delete_element() is not supported with a cache file active");

	return;
}

long tag_load(
	long group_tag,
	char const *name,
	unsigned long flags)
{
	error(_error_silent, "tag_load() is not supported with a cache file active");

	return NONE;
}

void tag_unload(
	long tag_index)
{
	error(_error_silent, "tag_unload() is not supported with a cache file active");

	return;
}

void tag_file_get_path(
	long group_tag,
	char const *name,
	char *path)
{
	error(_error_silent, "tag_file_get_path() is not supported with a cache file active");
	path[0] = 0;

	return;
}

void tag_reference_set(
	struct tag_reference *reference,
	unsigned long group_tag,
	char const *name)
{
	error(_error_silent, "tag_reference_set() is not supported with a cache file active");

	return;
}

void tag_iterator_new(
	struct tag_iterator *iterator,
	long group_tag)
{
	iterator->absolute_index = 0;
	iterator->group_tag = group_tag;

	return;
}

long tag_iterator_next(
	struct tag_iterator *iterator)
{
	long result = NONE;

	while (iterator->absolute_index < cache_file_globals.tag_header->tag_count)
	{
		struct cache_file_tag_instance *tag_instance =
			&global_tag_instances[iterator->absolute_index++];

		if (tag_instance &&
			(iterator->group_tag == NONE ||
			iterator->group_tag == tag_instance->group_tag ||
			iterator->group_tag == tag_instance->parent_group_tags[0] ||
			iterator->group_tag == tag_instance->parent_group_tags[1]))
		{
			result = tag_instance->tag_index;
			break;
		}
	}

	return result;
}

boolean cache_file_header_verify(
	struct cache_file_header *header,
	char const *scenario_name,
	boolean fatal)
{
#ifdef HALO_PS3
	/* port: each header the game reads comes here; an Xbox one is little-endian (port/ps3/xbox/xbox_tag_swap.c) */
	extern int cache_files_swap_header(void *header);

	cache_files_swap_header(header);
#endif
	if (header->header_signature != CACHE_FILE_HEADER_SIGNATURE ||
		header->footer_signature != CACHE_FILE_FOOTER_SIGNATURE ||
		header->file_length < 0 ||
		header->file_length > 0x11600000 ||
		csstrlen(header->name) > 31)
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				544,
				FALSE,
				csprintf(temporary, "'%s' does not appear to be a cache file", scenario_name));
		}

		return FALSE;
	}

	if (header->version != 5)
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				548,
				FALSE,
				csprintf(temporary, "the cache file '%s' is an old version", scenario_name));
		}

		return FALSE;
	}

	return TRUE;
}

/* port: the released maps' builds, by region; they play multiplayer together, other builds cannot open the
multiplayer menu (ui_widget_launch_widget). PAL maps play as the NTSC maps (port/linux/game/pal_tags.c) */
static struct
{
	char const *build;
	char const *region;
} const cache_file_builds[] =
{
	{ "01.01.14.2342", "PAL" },
	{ "01.10.12.2276", "NTSC" },
	{ "01.08.15.1749", "NTSC" },
};

/* the region of a build's maps ("PAL" or "NTSC") if it is listed above, else
NULL; build is a cache file header's (which need not end it) */
char const *cache_files_build_region(
	char const *build)
{
	short index;

	for (index = 0; index < NUMBEROF(cache_file_builds); index++)
	{
		if (!csstrncmp(build, cache_file_builds[index].build, sizeof(cache_file_globals.header.build)))
			return cache_file_builds[index].region;
	}

	return NULL;
}

/* the region of the loaded map's build if it plays multiplayer, else NULL;
build gets the build */
char const *cache_files_multiplayer_region(
	char build[0x20])
{
	csstrncpy(build, cache_file_globals.header.build, 0x20);
	build[0x1F] = 0;

	return cache_files_build_region(cache_file_globals.header.build);
}

/* whether the named map plays multiplayer with the others: FALSE only for a build not listed above, as the object and
damage messages name tags by index, which differs between builds; build gets the map's build, empty if unread */
boolean cache_files_map_plays_multiplayer(
	char const *map_name,
	char build[0x20])
{
	struct cache_file_header header;
	char path[256];
	HANDLE file;
	boolean result = TRUE;

	build[0] = 0;
	if (!map_name || !map_name[0])
		return TRUE;
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), tag_name_strip_path(map_name));
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		unsigned long bytes_read;

		if (ReadFile(file, &header, sizeof(header), &bytes_read, NULL) &&
			bytes_read == sizeof(header) &&
			cache_file_header_verify(&header, path, FALSE))
		{
			csstrncpy(build, header.build, 0x20);
			build[0x1F] = 0;
			result = cache_files_build_region(header.build) != NULL;
		}
		CloseHandle(file);
	}

	return result;
}

/* tells the player that maps of a build (a cache file header's) do not play
multiplayer: map_name the map's, or NULL for the player's maps */
void cache_files_show_multiplayer_unavailable(
	char const *map_name,
	char const *build)
{
	void platform_log(char const *format, ...);
	void platform_show_message(char const *title, char const *message);
	char message[320];

	if (map_name)
	{
		platform_log("multiplayer is unavailable: the map %s is of build %s, which is not supported", map_name, build);
		snprintf(
			message,
			sizeof(message),
			"The map %s (build %s) isn't supported for multiplayer yet.\n\nAsk in the Discord to get it added.",
			tag_name_strip_path(map_name),
			build);
	}
	else
	{
		platform_log("multiplayer is unavailable: maps of build %s are not supported", build);
		snprintf(
			message,
			sizeof(message),
			"Your maps (build %s) aren't supported for multiplayer yet.\n\nAsk in the Discord to get them added.",
			build);
	}
	platform_show_message("Halo: multiplayer unavailable", message);

	return;
}

boolean cache_files_give_time_to_precache(
	char const *map_name)
{
	boolean result = FALSE;

	/* port: no map named yet is nothing to precache. An internet client asks before the host names the map, and with
	all six cache slots full the empty name matched none, so it read as a missing map (the damaged disc error) */
	if (!map_name || !map_name[0])
		return FALSE;
	if (cache_files_precache_map_loaded(map_name))
	{
		result = TRUE;
	}
	else
	{
		if (cache_files_precache_in_progress() &&
			!cache_files_precache_is_copying_map(map_name))
		{
			cache_files_precache_map_end();
		}

		if (cache_files_precache_in_progress())
		{
			real progress;
			short status = cache_files_precache_map_status(&progress);

			if (status == 2)
				display_error_damaged_media();
			else if (status == 1)
				cache_files_precache_map_end();
		}
		else
		{
			cache_files_precache_set_priority(0);
			if (!cache_files_precache_map_begin(map_name, FALSE))
				display_error_damaged_media();
		}
	}

	return result;
}

long scenario_tags_load(
	char const *scenario_name)
{
	long result;
	char const *stripped_scenario_name;
	void *tag_cache_base_address;
	boolean read_complete;

	stripped_scenario_name = tag_name_strip_path(scenario_name);
	result = NONE;
	texture_cache_open();
	sound_cache_open();
	if (cache_file_open(stripped_scenario_name, &cache_file_globals.header))
	{
		tag_cache_base_address = physical_memory_get_tag_cache_base_address();
		if (cache_file_header_verify(&cache_file_globals.header, scenario_name, TRUE))
		{
			csmemset(tag_cache_base_address, 0xCD, 0x01600000);
			cache_file_read(
				NONE,
				cache_file_globals.header.tag_data_offset,
				cache_file_globals.header.tag_data_size,
				tag_cache_base_address,
				&read_complete,
				TRUE);
			while (!read_complete)
			{
				SwitchToThread();
			}
			CACHE_IO_BARRIER();
#ifdef HALO_PS3
			/* port: the map's tag data is little-endian (port/ps3/xbox/xbox_tag_swap.c) */
			{
				extern void cache_files_swap_tag_data(void *tag_data, long size);

				cache_files_swap_tag_data(tag_cache_base_address, cache_file_globals.header.tag_data_size);
			}
#endif

			cache_file_globals.tag_header = tag_cache_base_address;
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				0x94,
				cache_file_globals.tag_header->signature == CACHE_FILE_TAG_HEADER_SIGNATURE,
				csprintf(
					temporary,
					"signature is '%c%c%c%c', should be '%c%c%c%c'",
					(char)(cache_file_globals.tag_header->signature >> 24),
					(char)(cache_file_globals.tag_header->signature >> 16),
					(char)(cache_file_globals.tag_header->signature >> 8),
					(char)cache_file_globals.tag_header->signature,
					't',
					'a',
					'g',
					's'));
			global_tag_instances = cache_file_globals.tag_header->tag_instances;
			tags_header_register_vertex_and_index_buffers(cache_file_globals.tag_header);
			cache_file_globals.tags_loaded = TRUE;
			/* port: a PAL map played as the NTSC maps are (port/linux/game/pal_tags.c) */
			{
				extern void pal_tags_loaded(char const *build);

				pal_tags_loaded(cache_file_globals.header.build);
			}
			/* port: the menus' tags, added to the map's (port/linux/game/menu_tags.c) */
			{
				extern void menu_tags_loaded(char const *map_name);

				menu_tags_loaded(cache_file_globals.header.name);
			}
			/* port: the bitmaps the high-res HUD stands for (port/linux/game/hud_hires_tags.c) */
			{
				extern void hud_hires_tags_loaded(void);

				hud_hires_tags_loaded();
			}
			result = cache_file_globals.tag_header->scenario_tag_index;
		}
	}

	return result;
}

boolean scenario_structure_bsp_load(
	struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_tag_instance *tag_instance;
	byte *tag_cache_base_address;

	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		0x01600000 - cache_file_globals.header.tag_data_size);
	{
		boolean read_complete;

		cache_file_read(
			NONE,
			reference->file_offset,
			reference->file_size,
			reference->base_address,
			&read_complete,
			TRUE);
		while (!read_complete)
		{
			SwitchToThread();
			if (system_milliseconds() - sound_render_time() > 33)
			{
				sound_idle();
			}
		}
		CACHE_IO_BARRIER();
	}
#ifdef HALO_PS3
	/* port: the map's structure BSPs are little-endian (port/ps3/xbox/xbox_tag_swap.c) */
	{
		extern void cache_files_swap_structure_bsp(void *structure_bsp, long size);

		cache_files_swap_structure_bsp(reference->base_address, reference->file_size);
	}
#endif

	cache_file_globals.structure_bsp_header = reference->base_address;
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xE0,
		cache_file_globals.structure_bsp_header->signature==CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE);
	structure_bsp_header_register_vertex_buffers(cache_file_globals.structure_bsp_header);
	tag_instance = cache_get_tag_instance(reference->structure_bsp.index);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xEA,
		!tag_instance->base_address);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xEB,
		tag_instance->group_tag==STRUCTURE_BSP_TAG);
	tag_instance->base_address = cache_file_globals.structure_bsp_header->base_address;

	return TRUE;
}

void scenario_structure_bsp_unload(
	struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_tag_instance *tag_instance;

	structure_bsp_header_deregister_vertex_buffers(cache_file_globals.structure_bsp_header);
	tag_instance = cache_get_tag_instance(reference->structure_bsp.index);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		256,
		tag_instance->base_address);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		257,
		tag_instance->group_tag==STRUCTURE_BSP_TAG);
	tag_instance->base_address = NULL;
	cache_file_globals.structure_bsp_header = NULL;

	return;
}

void *tag_get(
	long group_tag,
	long tag_index)
{
	char expected_group[16];
	char returned_group[16];

	struct cache_file_tag_instance *tag_instance = cache_get_tag_instance(tag_index);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		298,
		tag_instance->group_tag == group_tag ||
		tag_instance->parent_group_tags[0] == group_tag ||
		tag_instance->parent_group_tags[1] == group_tag,
		csprintf(
			temporary,
			"expected tag group '%s' but got '%s' for %08x",
			tag_to_string(group_tag, expected_group),
			tag_to_string(tag_instance->group_tag, returned_group),
			tag_index)
	);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		302,
		tag_instance->base_address,
		csprintf(temporary, "can't get() a tag with a base address!")
	);
	
	return tag_instance->base_address;
}

/* whether the index is a loaded tag of the group or a parent group: the netcode names tags that
another machine sent (port/linux/game/network_damage.c), on which tag_get would only assert */
boolean tag_index_is_group(
	long tag_index,
	long group_tag)
{
	short absolute_index = (short)tag_index;
	struct cache_file_tag_instance *tag_instance;

	if (tag_index == NONE || !cache_file_globals.tags_loaded || !global_tag_instances ||
		absolute_index < 0 || absolute_index >= cache_file_globals.tag_header->tag_count)
	{
		return FALSE;
	}
	tag_instance = &global_tag_instances[absolute_index];
	return tag_instance->tag_index == tag_index && tag_instance->base_address &&
		(tag_instance->group_tag == group_tag || tag_instance->parent_group_tags[0] == group_tag ||
			tag_instance->parent_group_tags[1] == group_tag);
}

char *tag_get_name(
	long tag_index)
{
	return cache_get_tag_instance(tag_index)->name;
}

unsigned long tag_get_group_tag(
	long tag_index)
{
	return cache_get_tag_instance(tag_index)->group_tag;
}
