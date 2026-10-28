/* PS3_TAG_SWAP_CHECK.C: runs the map swap (port/ps3/xbox/xbox_tag_swap.c) on the PC over Xbox maps and their BSPs, and
reports its errors and the bytes it read twice or never. Run it with tools/ps3_tag_swap_check.sh (needs zlib). */

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
#define MAXIMUM_LISTED_GAPS 8

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("    ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

static uint8_t *coverage;
static uint32_t coverage_address;
static uint32_t coverage_size;
static uint32_t coverage_outside;

static void cover(uint32_t address, uint32_t size)
{
	uint32_t index;

	if (address < coverage_address || address - coverage_address + (uint64_t)size > coverage_size)
	{
		coverage_outside++;
		return;
	}
	for (index = 0; index < size; index++)
	{
		uint8_t *count = &coverage[address - coverage_address + index];

		if (*count < 255)
			(*count)++;
	}
}

static uint32_t read32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint32_t read32_little(const uint8_t *bytes)
{
	return (uint32_t)bytes[3] << 24 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[1] << 8 | bytes[0];
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

/* the map as the game's cache copy writes it: the header, then the inflated rest */
static uint8_t *decompress_map(uint8_t *file, uint32_t file_size, uint32_t *size)
{
	uint32_t length = read32_little(file + 8);
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

static int report(const char *what, const struct tag_swap_statistics *statistics)
{
	uint32_t index, uncovered = 0, twice = 0, gaps = 0;

	printf("  %s: %u tags, %u structs, %u blocks, %u references (%u stale), %u data, %u model parts, "
		"%u bytes swapped, %u unknown groups, %u errors\n", what, statistics->tags, statistics->structs,
		statistics->blocks, statistics->references, statistics->stale_references, statistics->data,
		statistics->model_parts, statistics->bytes_swapped, statistics->unknown_groups, statistics->errors);
	for (index = 0; index < coverage_size; index++)
	{
		if (coverage[index] > 1)
			twice++;
		if (!coverage[index])
		{
			uint32_t end = index;

			while (end < coverage_size && !coverage[end])
				end++;
			uncovered += end - index;
			if (end - index >= 8 && gaps++ < MAXIMUM_LISTED_GAPS)
				printf("    gap %08x+%x\n", coverage_address + index, end - index);
			index = end - 1;
		}
	}
	printf("    %u of %u bytes not reached, %u reached twice, %u marks outside\n", uncovered, coverage_size, twice,
		coverage_outside);
	return statistics->errors || statistics->unknown_groups || twice || coverage_outside;
}

static int check_map(const char *path)
{
	uint32_t file_size, size, tag_offset, tag_size, scenario_index, index;
	uint8_t *map = read_file(path, &file_size);
	uint8_t header[CACHE_FILE_HEADER_SIZE];
	uint8_t *tags, *scenario, *bsps;
	struct tag_swap_statistics statistics;
	int failed = 0;

	printf("%s\n", path);
	if (!map || file_size < CACHE_FILE_HEADER_SIZE || !(map = decompress_map(map, file_size, &size)))
	{
		printf("  cannot read or inflate it\n");
		return 1;
	}
	memcpy(header, map, sizeof(header));
	if (!tag_swap_cache_file_header(header) || memcmp(header, "head", 4) || memcmp(header + 0x7FC, "foot", 4) ||
		read32(header + 4) != 5 || read32(header + 8) != size || tag_swap_cache_file_header(header))
	{
		printf("  bad header\n");
		return 1;
	}
	tag_offset = read32(header + 0x10);
	tag_size = read32(header + 0x14);
	printf("  \"%.32s\", build \"%.32s\", type %u, %u bytes, tag data %u bytes\n", (char *)header + 0x20,
		(char *)header + 0x40, (header[0x60] << 8) | header[0x61], size, tag_size);
	if (tag_offset > size || tag_size > size - tag_offset)
	{
		printf("  bad tag data range\n");
		return 1;
	}

	tags = map + tag_offset;
	coverage = calloc(tag_size, 1);
	coverage_address = TAG_CACHE_ADDRESS;
	coverage_size = tag_size;
	coverage_outside = 0;
	if (!tag_swap_tag_data(tags, TAG_CACHE_ADDRESS, tag_size, &statistics, cover) ||
		tag_swap_tag_data(tags, TAG_CACHE_ADDRESS, tag_size, &statistics, NULL) || memcmp(tags + 0x20, "tags", 4))
	{
		printf("  the tag data did not swap once\n");
		return 1;
	}
	failed |= report("tag data", &statistics);
	free(coverage);

	/* each structure BSP that the scenario lists loads at its address after the tag data */
	scenario_index = read32(tags + 4) & 0xFFFF;
	scenario = tags + read32(tags) - TAG_CACHE_ADDRESS + scenario_index * 0x20;
	scenario = tags + read32(scenario + 20) - TAG_CACHE_ADDRESS;
	bsps = tags + read32(scenario + TAG_SWAP_SCENARIO_STRUCTURE_BSPS + 4) - TAG_CACHE_ADDRESS;
	for (index = 0; index < read32(scenario + TAG_SWAP_SCENARIO_STRUCTURE_BSPS); index++)
	{
		const uint8_t *bsp = bsps + index * SCENARIO_BSP_SIZE;
		uint32_t start = read32(bsp + TAG_SWAP_SCENARIOBSP_BSP_START);
		uint32_t bsp_size = read32(bsp + TAG_SWAP_SCENARIOBSP_BSP_SIZE);
		uint32_t address = read32(bsp + TAG_SWAP_SCENARIOBSP_BSP_ADDRESS);
		uint8_t *bytes;
		char what[64];

		if (start > size || bsp_size > size - start || address < TAG_CACHE_ADDRESS + tag_size)
		{
			printf("  BSP %u: bad range %08x+%x at %08x\n", index, start, bsp_size, address);
			failed = 1;
			continue;
		}
		bytes = malloc(bsp_size);
		memcpy(bytes, map + start, bsp_size);
		coverage = calloc(bsp_size, 1);
		coverage_address = address;
		coverage_size = bsp_size;
		coverage_outside = 0;
		snprintf(what, sizeof(what), "BSP %u (%u bytes at %08x)", index, bsp_size, address);
		if (!tag_swap_structure_bsp(bytes, address, bsp_size, &statistics, cover) ||
			tag_swap_structure_bsp(bytes, address, bsp_size, &statistics, NULL))
		{
			printf("  BSP %u did not swap once\n", index);
			failed = 1;
		}
		else
		{
			failed |= report(what, &statistics);
		}
		free(coverage);
		free(bytes);
	}
	free(map);
	return failed;
}

int main(int argument_count, char **arguments)
{
	int failed = 0, index;

	if (argument_count < 2)
	{
		fprintf(stderr, "usage: %s map...\n", arguments[0]);
		return 2;
	}
	for (index = 1; index < argument_count; index++)
		failed |= check_map(arguments[index]);
	printf(failed ? "FAILED\n" : "all maps passed\n");
	return failed;
}
