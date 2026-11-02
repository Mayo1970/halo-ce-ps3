/* PS3_TAG_SWAP_COMPARE.C: our full swap (xbox_tag_swap.c) against the game's (tag_byte_swap.c) plus our vertex pass,
byte for byte over Xbox maps, the differences by tag group. Run it with tools/ps3_tag_swap_compare.sh (needs zlib). */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "xbox_tag_swap.h"
#include "xbox_tag_swap_tables.h"

#define CACHE_FILE_HEADER_SIZE 0x800
#define TAG_CACHE_ADDRESS 0x803A6000u
#define SCENARIO_BSP_SIZE 0x20
#define INSTANCE_SIZE 0x20
#define MAXIMUM_GROUPS 8192
#define EXAMPLES 2
/* (the game's test's: where struct structure_bsp keeps its clusters' visibility) */
#define STRUCTURE_BSP_CLUSTER_DATA 0x140

/* the game's swap (built with TAG_BYTE_SWAP_EMULATE_BIG_ENDIAN) */
void tag_byte_swap_tags(void *base, unsigned long base_address, unsigned long size);
void tag_byte_swap_structure_bsp(void *bsp, unsigned long bsp_address, unsigned long region_address,
	unsigned long region_size);
extern unsigned char *tag_byte_swap_test_base;
extern unsigned long tag_byte_swap_test_size;
extern unsigned char *tag_byte_swap_test_counts;

static int quiet;

void platform_log(const char *format, ...)
{
	va_list arguments;

	if (quiet)
		return;
	va_start(arguments, format);
	printf("    ours: ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

void error(int type, const char *format, ...)
{
	va_list arguments;

	(void)type;
	if (quiet)
		return;
	va_start(arguments, format);
	printf("    game: ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

/* the game swaps these elsewhere: kept as they are here, they show as differences of their own groups */
void animation_graph_byte_swap_data(void *graph) { (void)graph; }
unsigned long structure_bsp_cluster_data_offset(void) { return STRUCTURE_BSP_CLUSTER_DATA; }

static uint32_t le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[3] << 24 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[1] << 8 | bytes[0];
}

static void be32_put(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

/* the game's cache_byte_swap.c reads natively: its words made big-endian here, as the game's test does */
static void swap_words(uint8_t *base, uint32_t address, uint32_t size, uint32_t at, uint32_t words)
{
	uint32_t offset = at - address, index;

	if (offset >= size || words > (size - offset) / 4)
		return;
	for (index = 0; index < words; index++)
		be32_put(base + offset + index * 4, le32(base + offset + index * 4));
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
	FILE *file = fopen(path, "rb");
	uint8_t *bytes;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc(length ? length : 1);
	if (!bytes || fread(bytes, 1, length, file) != (size_t)length)
	{
		free(bytes);
		fclose(file);
		return NULL;
	}
	fclose(file);
	*size = (uint32_t)length;
	return bytes;
}

static uint8_t *decompress_map(uint8_t *file, uint32_t file_size, uint32_t *size)
{
	uint32_t length = le32(file + 8);
	uint8_t *map;
	z_stream stream;

	if (length == file_size)
	{
		*size = file_size;
		return file;
	}
	map = malloc(length);
	if (!map)
		return NULL;
	memcpy(map, file, CACHE_FILE_HEADER_SIZE);
	memset(&stream, 0, sizeof(stream));
	stream.next_in = file + CACHE_FILE_HEADER_SIZE;
	stream.avail_in = file_size - CACHE_FILE_HEADER_SIZE;
	stream.next_out = map + CACHE_FILE_HEADER_SIZE;
	stream.avail_out = length - CACHE_FILE_HEADER_SIZE;
	if (inflateInit(&stream) != Z_OK || inflate(&stream, Z_FINISH) != Z_STREAM_END ||
		stream.total_out != length - CACHE_FILE_HEADER_SIZE)
	{
		free(map);
		return NULL;
	}
	inflateEnd(&stream);
	free(file);
	*size = length;
	return map;
}

/* the differences of one field (a group's tags, our walk's struct and offset, or our data kind): their bytes, and
the first few places */
static struct group_differences
{
	uint32_t group;
	uint32_t label;
	uint8_t operation;
	uint32_t bytes;
	int examples;
	struct { uint32_t tag, offset; uint8_t ours[4], game[4]; } example[EXAMPLES];
} groups[MAXIMUM_GROUPS];
static int group_count;

static struct group_differences *group_find(uint32_t group, uint32_t label, uint8_t operation)
{
	int index;

	for (index = 0; index < group_count; index++)
	{
		if (groups[index].group == group && groups[index].label == label && groups[index].operation == operation)
			return &groups[index];
	}
	if (group_count == MAXIMUM_GROUPS)
		return &groups[MAXIMUM_GROUPS - 1];
	memset(&groups[group_count], 0, sizeof(groups[group_count]));
	groups[group_count].group = group;
	groups[group_count].label = label;
	groups[group_count].operation = operation;
	return &groups[group_count++];
}

/* our walk's labels of the region being swapped (xbox_tag_swap.c, TAG_SWAP_LABELS): struct << 16 | offset */
#define NO_LABEL 0xFFFFFFFFu
#define NO_OPERATION 0xFF
static uint32_t *labels;
static uint8_t *label_operations;
static uint32_t label_address, label_size;
static int labelling;

void tag_swap_label(uint32_t address, uint32_t size, uint16_t struct_index, uint32_t offset, int operation)
{
	uint32_t index;

	if (!labelling || address < label_address || address - label_address >= label_size)
		return;
	for (index = 0; index < size && address - label_address + index < label_size; index++)
	{
		labels[address - label_address + index] = (uint32_t)struct_index << 16 | (offset & 0xFFFF);
		label_operations[address - label_address + index] = (uint8_t)operation;
	}
}

static void labels_begin(uint32_t address, uint32_t size)
{
	free(labels);
	free(label_operations);
	labels = malloc((size_t)size * sizeof(*labels));
	label_operations = malloc(size);
	memset(labels, 0xFF, (size_t)size * sizeof(*labels));
	memset(label_operations, NO_OPERATION, size);
	label_address = address;
	label_size = size;
}

/* (the tag a byte is in: the one whose data starts last at or before it; tags are laid out one after another) */
struct tag_start
{
	uint32_t address;
	uint32_t group;
	uint32_t index;
};

static int start_compare(const void *a, const void *b)
{
	uint32_t x = ((const struct tag_start *)a)->address, y = ((const struct tag_start *)b)->address;

	return x < y ? -1 : x > y;
}

static void difference(uint32_t group, uint32_t tag, uint32_t offset, uint32_t label_offset, const uint8_t *ours,
	const uint8_t *game)
{
	struct group_differences *entry = group_find(group, labels[label_offset], label_operations[label_offset]);

	entry->bytes++;
	if (entry->examples < EXAMPLES && (!entry->examples ||
		entry->example[entry->examples - 1].tag != tag || offset - entry->example[entry->examples - 1].offset >= 4))
	{
		entry->example[entry->examples].tag = tag;
		entry->example[entry->examples].offset = offset;
		memcpy(entry->example[entry->examples].ours, ours, 4);
		memcpy(entry->example[entry->examples].game, game, 4);
		entry->examples++;
	}
}

static void print_group(uint32_t group)
{
	putchar(group >> 24 ? (int)(group >> 24) : '.');
	putchar((group >> 16) & 0xFF ? (int)((group >> 16) & 0xFF) : '.');
	putchar((group >> 8) & 0xFF ? (int)((group >> 8) & 0xFF) : '.');
	putchar(group & 0xFF ? (int)(group & 0xFF) : '.');
}

static uint32_t report(const char *what, const char *names, uint32_t names_address, uint32_t names_size)
{
	uint32_t total = 0;
	int index, example;

	for (index = 0; index < group_count; index++)
		total += groups[index].bytes;
	printf("  %s: %u bytes differ\n", what, total);
	for (index = 0; index < group_count; index++)
	{
		struct group_differences *entry = &groups[index];

		printf("    ");
		print_group(entry->group);
		if (entry->label == NO_LABEL)
			printf(" (not reached by ours): %u bytes\n", entry->bytes);
		else if (entry->label >> 16 == TAG_SWAP_LABEL_DATA)
			printf(" data kind %u: %u bytes\n", entry->label & 0xFFFF, entry->bytes);
		else
			printf(" struct %u +0x%x op %u: %u bytes\n", entry->label >> 16, entry->label & 0xFFFF, entry->operation,
				entry->bytes);
		for (example = 0; example < entry->examples; example++)
		{
			uint32_t name = entry->example[example].tag;
			const char *text = names && name - names_address < names_size ? names + (name - names_address) : "?";

			printf("      %s +0x%x: ours %02x%02x%02x%02x, game %02x%02x%02x%02x\n", text,
				entry->example[example].offset,
				entry->example[example].ours[0], entry->example[example].ours[1], entry->example[example].ours[2],
				entry->example[example].ours[3], entry->example[example].game[0], entry->example[example].game[1],
				entry->example[example].game[2], entry->example[example].game[3]);
		}
	}
	group_count = 0;
	return total;
}

static int compare_map(const char *path)
{
	uint32_t file_size, size, tag_offset, tag_size, instances_address, count, index, scenario_index, total = 0;
	uint32_t scenario, bsp_count, bsp_references;
	uint8_t *map = read_file(path, &file_size);
	uint8_t *original, *ours, *game, *bsps;
	struct tag_swap_statistics statistics;
	struct tag_start *starts;

	printf("%s\n", path);
	if (!map || file_size < CACHE_FILE_HEADER_SIZE || !(map = decompress_map(map, file_size, &size)))
	{
		printf("  cannot read or inflate it\n");
		return 1;
	}
	tag_offset = le32(map + 0x10);
	tag_size = le32(map + 0x14);
	if (tag_offset > size || tag_size > size - tag_offset)
	{
		printf("  bad tag data range\n");
		return 1;
	}
	original = map + tag_offset;
	ours = malloc(tag_size);
	game = malloc(tag_size);
	memcpy(ours, original, tag_size);
	memcpy(game, original, tag_size);

	/* the tags' starts, read before either swap */
	instances_address = le32(original);
	count = le32(original + 0xC);
	starts = malloc((count + 1) * sizeof(*starts));
	for (index = 0; index < count; index++)
	{
		const uint8_t *instance = original + (instances_address - TAG_CACHE_ADDRESS) + index * INSTANCE_SIZE;

		starts[index].address = le32(instance + 0x14);
		starts[index].group = le32(instance);
		starts[index].index = index;
	}
	/* (the header and the instance table: a group of their own) */
	starts[count].address = TAG_CACHE_ADDRESS;
	starts[count].group = 0x68656164; /* 'head' */
	starts[count].index = count;
	qsort(starts, count + 1, sizeof(*starts), start_compare);

	quiet = 1;
	labels_begin(TAG_CACHE_ADDRESS, tag_size);
	labelling = 1;
	if (!tag_swap_tag_data(ours, TAG_CACHE_ADDRESS, tag_size, &statistics, NULL))
		printf("  ours: the tag data did not swap\n");
	labelling = 0;
	quiet = 0;
	swap_words(game, TAG_CACHE_ADDRESS, tag_size, TAG_CACHE_ADDRESS, 9);
	swap_words(game, TAG_CACHE_ADDRESS, tag_size, instances_address, count * 8);
	/* (and the D3D headers of the vertex and index buffers, as cache_byte_swap.c's cache_file_tags_byte_swap) */
	swap_words(game, TAG_CACHE_ADDRESS, tag_size, le32(original + 0x14), le32(original + 0x10) * 3);
	swap_words(game, TAG_CACHE_ADDRESS, tag_size, le32(original + 0x1C), le32(original + 0x18) * 3);
	tag_byte_swap_test_counts = NULL;
	tag_byte_swap_tags(game, TAG_CACHE_ADDRESS, tag_size);
	/* (and the vertices it leaves, as cache_files.c has them swapped on the PS3) */
	if (!tag_swap_tag_vertices(game, TAG_CACHE_ADDRESS, tag_size, &statistics))
		printf("  ours: the model vertices did not swap\n");
	printf("  vertex pass: %u models, %u structs, %u model parts, %u bytes swapped, %u errors\n", statistics.tags,
		statistics.structs, statistics.model_parts, statistics.bytes_swapped, statistics.errors);

	{
		uint32_t offset, low = 0;

		for (offset = 0; offset < tag_size; offset++)
		{
			uint32_t address = TAG_CACHE_ADDRESS + offset, high = count + 1;

			if (ours[offset] == game[offset])
				continue;
			while (high - low > 1)
			{
				uint32_t middle = (low + high) / 2;

				if (starts[middle].address <= address)
					low = middle;
				else
					high = middle;
			}
			if (starts[low].address > address)
				low = 0;
			{
				const uint8_t *instance = original + (instances_address - TAG_CACHE_ADDRESS) +
					starts[low].index * INSTANCE_SIZE;
				uint32_t name = starts[low].index < count ? le32(instance + 0x10) : 0;
				uint32_t at = offset & ~3u;

				difference(starts[low].group, name, address - starts[low].address, offset, ours + at, game + at);
			}
		}
	}
	total += report("tag data", (const char *)original, TAG_CACHE_ADDRESS, tag_size);

	/* each structure BSP the scenario lists, from the unswapped tag data */
	scenario_index = le32(original + 4) & 0xFFFF;
	scenario = le32(original + (instances_address - TAG_CACHE_ADDRESS) + scenario_index * INSTANCE_SIZE + 0x14) -
		TAG_CACHE_ADDRESS;
	bsp_count = le32(original + scenario + TAG_SWAP_SCENARIO_STRUCTURE_BSPS);
	bsp_references = le32(original + scenario + TAG_SWAP_SCENARIO_STRUCTURE_BSPS + 4) - TAG_CACHE_ADDRESS;
	bsps = original + bsp_references;
	for (index = 0; index < bsp_count; index++)
	{
		const uint8_t *reference = bsps + index * SCENARIO_BSP_SIZE;
		uint32_t start = le32(reference + TAG_SWAP_SCENARIOBSP_BSP_START);
		uint32_t bsp_size = le32(reference + TAG_SWAP_SCENARIOBSP_BSP_SIZE);
		uint32_t address = le32(reference + TAG_SWAP_SCENARIOBSP_BSP_ADDRESS);
		uint8_t *bsp_ours, *bsp_game;
		uint32_t bsp_address, offset;
		char what[64];

		if (start > size || bsp_size > size - start)
		{
			printf("  BSP %u: bad range\n", index);
			continue;
		}
		bsp_ours = malloc(bsp_size);
		bsp_game = malloc(bsp_size);
		memcpy(bsp_ours, map + start, bsp_size);
		memcpy(bsp_game, map + start, bsp_size);
		quiet = 1;
		labels_begin(address, bsp_size);
		labelling = 1;
		if (!tag_swap_structure_bsp(bsp_ours, address, bsp_size, &statistics, NULL))
			printf("  ours: BSP %u did not swap\n", index);
		labelling = 0;
		quiet = 0;
		/* (as the game's test: the header, its buffers' descriptions, then the bsp by its layout) */
		swap_words(bsp_game, address, bsp_size, address, 6);
		swap_words(bsp_game, address, bsp_size, le32(map + start + 8), le32(map + start + 4) * 3);
		swap_words(bsp_game, address, bsp_size, le32(map + start + 0x10), le32(map + start + 0xC) * 3);
		bsp_address = le32(map + start);
		tag_byte_swap_structure_bsp(bsp_game + (bsp_address - address), bsp_address, address, bsp_size);
		if (!tag_swap_structure_bsp_vertices(bsp_game, address, bsp_size, &statistics))
			printf("  ours: BSP %u's vertices did not swap\n", index);
		for (offset = 0; offset < bsp_size; offset++)
		{
			if (bsp_ours[offset] != bsp_game[offset])
				difference(0x73627370 /* 'sbsp' */, 0, offset, offset, bsp_ours + (offset & ~3u),
					bsp_game + (offset & ~3u));
		}
		snprintf(what, sizeof(what), "BSP %u (%u bytes at %08x)", index, bsp_size, address);
		total += report(what, NULL, 0, 0);
		free(bsp_ours);
		free(bsp_game);
	}
	free(starts);
	free(ours);
	free(game);
	free(map);
	return total != 0;
}

int main(int argument_count, char **arguments)
{
	int index, failed = 0;

	if (argument_count < 2)
	{
		printf("usage: ps3_tag_swap_compare <map>...\n");
		return 2;
	}
	for (index = 1; index < argument_count; index++)
		failed |= compare_map(arguments[index]);
	return failed;
}
