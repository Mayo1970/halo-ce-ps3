/* XBOX_TAG_SWAP.C: swaps an Xbox map's header, tag data and structure BSPs to the PS3's byte order in place when
cache_files.c reads them (GOALS.md, G3), with Invader's layouts (xbox_tag_swap_tables.h) and the map's own pointers. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xbox_tag_swap.h"
#include "xbox_tag_swap_tables.h"

void platform_log(const char *format, ...);

#define NUMBER_OF(array) (sizeof(array) / sizeof((array)[0]))
#define TAG_HEADER_SIZE 0x24
#define TAG_INSTANCE_SIZE 0x20
#define D3D_RESOURCE_SIZE 12
#define BSP_HEADER_SIZE 0x18
#define DATA_ARRAY_HEADER_SIZE 0x38
#define SYNTAX_NODE_SIZE 20
#define COMPRESSED_ANIMATION_HEADER_SIZE 0x2C
#define MAXIMUM_ANIMATION_NODES 64
#define MAXIMUM_DEPTH 16
#define MAXIMUM_LOGGED_ERRORS 16
/* rasterizer_geometry.h */
#define MODEL_VERTEX_TYPE_UNCOMPRESSED 4
#define MODEL_VERTEX_TYPE_COMPRESSED 5

struct walk
{
	uint8_t *bytes;
	uint32_t address;
	uint32_t size;
	int depth;
	const uint8_t *instances;
	uint32_t tag_count;
	struct tag_swap_statistics *statistics;
	tag_swap_coverage_proc coverage;
};

static void swap_struct(struct walk *walk, uint32_t address, uint16_t struct_index);

/* ---------- bytes */

static uint32_t read32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint16_t read16(const uint8_t *bytes)
{
	return (uint16_t)(bytes[0] << 8 | bytes[1]);
}

static void swap2(uint8_t *bytes, uint32_t count)
{
	while (count--)
	{
		uint8_t byte0 = bytes[0];

		bytes[0] = bytes[1];
		bytes[1] = byte0;
		bytes += 2;
	}
}

static void swap4(uint8_t *bytes, uint32_t count)
{
	while (count--)
	{
		uint8_t byte0 = bytes[0], byte1 = bytes[1];

		bytes[0] = bytes[3];
		bytes[1] = bytes[2];
		bytes[2] = byte1;
		bytes[3] = byte0;
		bytes += 4;
	}
}

/* ---------- the walk */

static void walk_error(struct walk *walk, const char *format, ...)
{
	char text[200];
	va_list arguments;

	if (++walk->statistics->errors > MAXIMUM_LOGGED_ERRORS)
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	platform_log("tag swap: %s", text);
}

/* the bytes of [address, address + size) if the region holds all of them */
static uint8_t *walk_bytes(struct walk *walk, uint32_t address, uint32_t size)
{
	if (address < walk->address || address - walk->address > walk->size ||
		size > walk->size - (address - walk->address))
	{
		return NULL;
	}
	return walk->bytes + (address - walk->address);
}

static void walk_cover(struct walk *walk, uint32_t address, uint32_t size)
{
	if (walk->coverage)
		walk->coverage(address, size);
}

static uint32_t walk_address(struct walk *walk, const uint8_t *bytes)
{
	return walk->address + (uint32_t)(bytes - walk->bytes);
}

/* Xbox models keep their vertices behind the part's D3D vertex buffer, and their triangle strip in tag data */
static void swap_model_part(struct walk *walk, const uint8_t *part)
{
	uint32_t triangle_count = read32(part + TAG_SWAP_MODELGEOMETRYPART_TRIANGLE_COUNT);
	uint32_t triangles = read32(part + TAG_SWAP_MODELGEOMETRYPART_TRIANGLE_OFFSET);
	uint16_t vertex_type = read16(part + TAG_SWAP_MODELGEOMETRYPART_VERTEX_TYPE);
	uint32_t vertex_count = read32(part + TAG_SWAP_MODELGEOMETRYPART_VERTEX_COUNT);
	uint32_t vertex_header = read32(part + TAG_SWAP_MODELGEOMETRYPART_VERTEX_OFFSET);

	walk->statistics->model_parts++;
	if (vertex_count)
	{
		uint16_t vertex_struct = vertex_type == MODEL_VERTEX_TYPE_COMPRESSED ?
			TAG_SWAP_STRUCT_MODELVERTEXCOMPRESSED : TAG_SWAP_STRUCT_MODELVERTEXUNCOMPRESSED;
		uint32_t vertex_size = tag_swap_structs[vertex_struct].size;
		const uint8_t *header = walk_bytes(walk, vertex_header, D3D_RESOURCE_SIZE);
		uint32_t vertices = header ? read32(header + 4) : 0;
		uint32_t index;

		if ((vertex_type != MODEL_VERTEX_TYPE_COMPRESSED && vertex_type != MODEL_VERTEX_TYPE_UNCOMPRESSED) ||
			!header || vertex_count > 0xFFFF || !walk_bytes(walk, vertices, vertex_count * vertex_size))
		{
			walk_error(walk, "model part %08x: %u vertices of type %u at %08x", walk_address(walk, part),
				vertex_count, vertex_type, vertices);
		}
		else
		{
			for (index = 0; index < vertex_count; index++)
				swap_struct(walk, vertices + index * vertex_size, vertex_struct);
		}
	}
	if (triangle_count)
	{
		uint8_t *indices = triangle_count < 0xFFFF ? walk_bytes(walk, triangles, (triangle_count + 2) * 2) : NULL;

		if (!indices)
		{
			walk_error(walk, "model part %08x: %u triangles at %08x", walk_address(walk, part), triangle_count,
				triangles);
		}
		else
		{
			walk_cover(walk, triangles, (triangle_count + 2) * 2);
			swap2(indices, triangle_count + 2);
			walk->statistics->bytes_swapped += (triangle_count + 2) * 2;
		}
	}
}

/* the script nodes: a data_array (data.h), with each node's value as a long (hs_scenario_definitions.h) */
static void swap_script_syntax(struct walk *walk, uint8_t *bytes, uint32_t size)
{
	uint32_t maximum_count;
	uint32_t index;

	if (size < DATA_ARRAY_HEADER_SIZE)
	{
		walk_error(walk, "script syntax data of %u bytes", size);
		return;
	}
	maximum_count = (uint32_t)bytes[0x21] << 8 | bytes[0x20];
	if (bytes[0x22] != SYNTAX_NODE_SIZE || bytes[0x23] ||
		DATA_ARRAY_HEADER_SIZE + maximum_count * SYNTAX_NODE_SIZE > size)
	{
		walk_error(walk, "script syntax data: %u nodes of %u bytes in %u bytes", maximum_count,
			(uint32_t)bytes[0x23] << 8 | bytes[0x22], size);
		return;
	}
	/* maximum count, size; signature; first free index, count, actual count, next identifier; data */
	swap2(bytes + 0x20, 2);
	swap4(bytes + 0x28, 1);
	swap2(bytes + 0x2C, 4);
	swap4(bytes + 0x34, 1);
	for (index = 0; index < maximum_count; index++)
	{
		uint8_t *node = bytes + DATA_ARRAY_HEADER_SIZE + index * SYNTAX_NODE_SIZE;

		swap2(node, 4);
		swap4(node + 8, 3);
	}
	walk->statistics->bytes_swapped += DATA_ARRAY_HEADER_SIZE - 0x20 + maximum_count * SYNTAX_NODE_SIZE;
}

static int animation_node_flag(const uint8_t *flags, uint32_t node_index)
{
	return read32(flags + 4 * (node_index / 32)) >> (node_index % 32) & 1;
}

/* the bytes of a frame for the nodes with (animated) or without the flags: rotations are 4 shorts,
translations 3 reals, scales a real (model_animations.c) */
static uint32_t animation_frame_size(const uint8_t *animation, int animated)
{
	uint32_t node_count = read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_COUNT);
	uint32_t node_index;
	uint32_t size = 0;

	for (node_index = 0; node_index < node_count; node_index++)
	{
		if (animation_node_flag(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_ROTATION_FLAG_DATA, node_index) ==
			animated)
		{
			size += 8;
		}
		if (animation_node_flag(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_TRANSFORM_FLAG_DATA, node_index) ==
			animated)
		{
			size += 12;
		}
		if (animation_node_flag(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_SCALE_FLAG_DATA, node_index) ==
			animated)
		{
			size += 4;
		}
	}
	return size;
}

static uint8_t *swap_animation_frame(const uint8_t *animation, uint8_t *bytes, int animated)
{
	uint32_t node_count = read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_COUNT);
	uint32_t node_index;

	for (node_index = 0; node_index < node_count; node_index++)
	{
		if (animation_node_flag(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_ROTATION_FLAG_DATA, node_index) ==
			animated)
		{
			swap2(bytes, 4);
			bytes += 8;
		}
		if (animation_node_flag(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_TRANSFORM_FLAG_DATA, node_index) ==
			animated)
		{
			swap4(bytes, 3);
			bytes += 12;
		}
		if (animation_node_flag(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_SCALE_FLAG_DATA, node_index) ==
			animated)
		{
			swap4(bytes, 1);
			bytes += 4;
		}
	}
	return bytes;
}

/* one frame of the nodes that the animation does not move */
static void swap_animation_default_data(struct walk *walk, const uint8_t *animation, uint8_t *bytes, uint32_t size)
{
	if (read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_COUNT) > MAXIMUM_ANIMATION_NODES ||
		animation_frame_size(animation, 0) != size)
	{
		walk_error(walk, "animation %08x: %u bytes of default data", walk_address(walk, animation), size);
		return;
	}
	swap_animation_frame(animation, bytes, 0);
	walk->statistics->bytes_swapped += size;
}

/* the keyframes after the compressed animation header (model_animations.c): each section
runs to the next one's offset, in the order of the header's offsets */
static void swap_compressed_animation(struct walk *walk, const uint8_t *animation, uint8_t *bytes, uint32_t size)
{
	static const uint8_t unit_sizes[12] = { 4, 2, 2, 2, 4, 2, 4, 4, 4, 2, 4, 4 };
	uint32_t starts[13];
	uint32_t section;

	if (size < COMPRESSED_ANIMATION_HEADER_SIZE)
	{
		walk_error(walk, "animation %08x: %u bytes of compressed data", walk_address(walk, animation), size);
		return;
	}
	starts[0] = COMPRESSED_ANIMATION_HEADER_SIZE;
	for (section = 0; section < 11; section++)
		starts[section + 1] = (uint32_t)bytes[4 * section + 3] << 24 | (uint32_t)bytes[4 * section + 2] << 16 |
			(uint32_t)bytes[4 * section + 1] << 8 | bytes[4 * section];
	starts[12] = size;
	for (section = 0; section < 12; section++)
	{
		if (starts[section + 1] < starts[section] || starts[section + 1] > size ||
			(starts[section + 1] - starts[section]) % unit_sizes[section])
		{
			walk_error(walk, "animation %08x: compressed section %u at %u..%u of %u", walk_address(walk, animation),
				section, starts[section], starts[section + 1], size);
			return;
		}
	}
	swap4(bytes, COMPRESSED_ANIMATION_HEADER_SIZE / 4);
	for (section = 0; section < 12; section++)
	{
		uint32_t count = (starts[section + 1] - starts[section]) / unit_sizes[section];

		if (unit_sizes[section] == 2)
			swap2(bytes + starts[section], count);
		else
			swap4(bytes + starts[section], count);
	}
	walk->statistics->bytes_swapped += size;
}

/* the event stream of a recording of version 4 (recorded_animation_playback.c): the unit's controls by the
version of their layout (recorded_animation_initialize.c), its 3 view directions, then events to the end */
static void swap_recorded_animation(struct walk *walk, const uint8_t *animation, uint8_t *bytes, uint32_t size)
{
	uint32_t control_version = animation[TAG_SWAP_SCENARIORECORDEDANIMATION_UNIT_CONTROL_DATA_VERSION];
	uint32_t controls_size = 52 + (control_version >= 2 ? 4 : 0) + (control_version >= 3 ? 2 : 0) +
		(control_version >= 4 ? 2 : 0);
	uint32_t offset = controls_size + 12;

	if (animation[TAG_SWAP_SCENARIORECORDEDANIMATION_VERSION] != 4 || control_version > 4 || offset > size)
	{
		walk_error(walk, "recording %08x: version %u, controls %u, %u bytes", walk_address(walk, animation),
			animation[TAG_SWAP_SCENARIORECORDEDANIMATION_VERSION], control_version, size);
		return;
	}
	/* two bytes, three shorts (the last unused), a 2D and three 3D vectors; a long; a short; a short */
	swap2(bytes + 2, 3);
	swap4(bytes + 8, 11);
	if (control_version >= 2)
		swap4(bytes + 52, 1);
	if (control_version >= 3)
		swap2(bytes + 56, 1);
	if (control_version >= 4)
		swap2(bytes + 58, 1);
	swap2(bytes + controls_size, 6);
	for (;;)
	{
		/* MSVC put the time delta in the header byte's low 2 bits, the event type in the high 6 */
		uint32_t time_delta = bytes[offset] & 3, event_type = bytes[offset] >> 2;
		uint32_t header_size = time_delta == 3 ? 3 : time_delta == 2 ? 2 : 1;
		uint32_t event_size = event_type <= 1 ? 0 : event_type == 2 || event_type == 3 ? 1 :
			event_type == 4 || event_type == 5 ? 2 : event_type == 6 ? 8 : event_type >= 7 && event_type <= 14 ? 2 :
			event_type >= 15 && event_type <= 22 ? 4 : 0xFFFFFFFF;

		if (event_size == 0xFFFFFFFF || offset + header_size + event_size > size)
		{
			walk_error(walk, "recording %08x: event %u at %u of %u", walk_address(walk, animation), event_type,
				offset, size);
			return;
		}
		if (time_delta == 3)
			swap2(bytes + offset + 1, 1);
		offset += header_size;
		if (event_type == 1)
			break;
		if (event_type == 4 || event_type == 5 || event_type >= 15)
			swap2(bytes + offset, event_size / 2);
		else if (event_type == 6)
			swap4(bytes + offset, 2);
		offset += event_size;
	}
	walk->statistics->bytes_swapped += offset;
}

/* the moving nodes of each frame, then the compressed form if the animation has one */
static void swap_animation_frame_data(struct walk *walk, const uint8_t *animation, uint8_t *bytes, uint32_t size)
{
	uint32_t frame_count = read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_FRAME_COUNT);
	uint32_t frame_size = read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_FRAME_SIZE);
	int compressed = read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_FLAGS) & 1;
	uint32_t compressed_offset = read32(animation + TAG_SWAP_MODELANIMATIONSANIMATION_OFFSET_TO_COMPRESSED_DATA);
	uint32_t frames_size = compressed ? compressed_offset : size;
	uint32_t frame_index;
	uint8_t *frame = bytes;

	if (read16(animation + TAG_SWAP_MODELANIMATIONSANIMATION_NODE_COUNT) > MAXIMUM_ANIMATION_NODES ||
		animation_frame_size(animation, 1) != frame_size || frames_size > size ||
		(frames_size && frames_size != frame_count * frame_size))
	{
		walk_error(walk, "animation %08x: %u bytes of frame data, %u frames of %u bytes, compressed %d at %u",
			walk_address(walk, animation), size, frame_count, frame_size, compressed, compressed_offset);
		return;
	}
	if (frames_size)
	{
		for (frame_index = 0; frame_index < frame_count; frame_index++)
			frame = swap_animation_frame(animation, frame, 1);
		walk->statistics->bytes_swapped += frames_size;
	}
	if (compressed)
		swap_compressed_animation(walk, animation, bytes + compressed_offset, size - compressed_offset);
}

/* a lightmap material's rendered vertices, then its lightmap vertices */
static void swap_bsp_vertices(struct walk *walk, const uint8_t *material, uint32_t address, uint32_t size,
	int compressed)
{
	uint32_t rendered_count = read32(material + TAG_SWAP_SCENARIOSTRUCTUREBSPMATERIAL_RENDERED_VERTICES_COUNT);
	uint32_t lightmap_count = read32(material + TAG_SWAP_SCENARIOSTRUCTUREBSPMATERIAL_LIGHTMAP_VERTICES_COUNT);
	uint16_t rendered_struct = compressed ? TAG_SWAP_STRUCT_SCENARIOSTRUCTUREBSPMATERIALCOMPRESSEDRENDEREDVERTEX :
		TAG_SWAP_STRUCT_SCENARIOSTRUCTUREBSPMATERIALUNCOMPRESSEDRENDEREDVERTEX;
	uint16_t lightmap_struct = compressed ? TAG_SWAP_STRUCT_SCENARIOSTRUCTUREBSPMATERIALCOMPRESSEDLIGHTMAPVERTEX :
		TAG_SWAP_STRUCT_SCENARIOSTRUCTUREBSPMATERIALUNCOMPRESSEDLIGHTMAPVERTEX;
	uint32_t rendered_size = tag_swap_structs[rendered_struct].size;
	uint32_t lightmap_size = tag_swap_structs[lightmap_struct].size;
	uint32_t index;

	if (rendered_count > size / rendered_size || lightmap_count > size / lightmap_size ||
		rendered_count * rendered_size + lightmap_count * lightmap_size != size)
	{
		walk_error(walk, "material %08x: %u bytes for %u and %u vertices", walk_address(walk, material), size,
			rendered_count, lightmap_count);
		return;
	}
	for (index = 0; index < rendered_count; index++)
		swap_struct(walk, address + index * rendered_size, rendered_struct);
	address += rendered_count * rendered_size;
	for (index = 0; index < lightmap_count; index++)
		swap_struct(walk, address + index * lightmap_size, lightmap_struct);
}

/* data: size, an unused word, the file offset, the address, a definition (tag_groups.h) */
static void swap_data(struct walk *walk, const uint8_t *owner, const uint8_t *data, uint16_t kind)
{
	uint32_t size = read32(data);
	uint32_t address = read32(data + 12);
	uint8_t *bytes;

	walk->statistics->data++;
	if (!size || kind == _tag_swap_data_file)
		return;
	bytes = walk_bytes(walk, address, size);
	if (!bytes)
	{
		walk_error(walk, "data %08x: %u bytes at %08x", walk_address(walk, data), size, address);
		return;
	}
	switch (kind)
	{
	case _tag_swap_data_bsp_uncompressed_vertices:
	case _tag_swap_data_bsp_compressed_vertices:
		swap_bsp_vertices(walk, owner, address, size, kind == _tag_swap_data_bsp_compressed_vertices);
		return;
	}
	walk_cover(walk, address, size);
	switch (kind)
	{
	case _tag_swap_data_swap2:
		swap2(bytes, size / 2);
		walk->statistics->bytes_swapped += size;
		break;
	case _tag_swap_data_swap4:
		swap4(bytes, size / 4);
		walk->statistics->bytes_swapped += size;
		break;
	case _tag_swap_data_script_syntax:
		swap_script_syntax(walk, bytes, size);
		break;
	case _tag_swap_data_animation_default_data:
		swap_animation_default_data(walk, owner, bytes, size);
		break;
	case _tag_swap_data_animation_frame_data:
		swap_animation_frame_data(walk, owner, bytes, size);
		break;
	case _tag_swap_data_recorded_animation:
		swap_recorded_animation(walk, owner, bytes, size);
		break;
	}
}

/* reference: group, name, name length, tag index (tag_groups.h); a few in the released maps name
another tag than their index, which the game never follows */
static void check_reference(struct walk *walk, const uint8_t *reference)
{
	uint32_t group_tag = read32(reference);
	uint32_t index = read32(reference + 12);
	const uint8_t *instance;

	walk->statistics->references++;
	if (index == 0xFFFFFFFF || !walk->instances)
		return;
	instance = walk->instances + (index & 0xFFFF) * TAG_INSTANCE_SIZE;
	if ((index & 0xFFFF) >= walk->tag_count || read32(instance + 12) != index ||
		(read32(instance) != group_tag && read32(instance + 4) != group_tag && read32(instance + 8) != group_tag))
	{
		walk->statistics->stale_references++;
	}
}

/* block: count, address, a definition (tag_groups.h) */
static void swap_block(struct walk *walk, const uint8_t *block, uint16_t element_struct)
{
	uint32_t count = read32(block);
	uint32_t address = read32(block + 4);
	uint32_t element_size = tag_swap_structs[element_struct].size;
	uint32_t index;

	walk->statistics->blocks++;
	if (!count)
		return;
	if (count > walk->size / element_size || !walk_bytes(walk, address, count * element_size))
	{
		walk_error(walk, "block %08x: %u elements of %u bytes at %08x", walk_address(walk, block), count,
			element_size, address);
		return;
	}
	for (index = 0; index < count; index++)
		swap_struct(walk, address + index * element_size, element_struct);
}

static void swap_struct(struct walk *walk, uint32_t address, uint16_t struct_index)
{
	const struct tag_swap_struct *definition = &tag_swap_structs[struct_index];
	uint8_t *base = walk_bytes(walk, address, definition->size);
	uint8_t *bytes = base;
	uint16_t op_index;

	if (!base || walk->depth >= MAXIMUM_DEPTH)
	{
		walk_error(walk, "struct %u at %08x, depth %d", struct_index, address, walk->depth);
		return;
	}
	walk->depth++;
	walk->statistics->structs++;
	walk_cover(walk, address, definition->size);
	for (op_index = 0; op_index < definition->op_count; op_index++)
	{
		const struct tag_swap_op *op = &tag_swap_ops[definition->first_op + op_index];

		switch (op->operation)
		{
		case _tag_swap_skip:
			bytes += op->count;
			break;
		case _tag_swap_2:
			swap2(bytes, op->count);
			walk->statistics->bytes_swapped += 2 * op->count;
			bytes += 2 * op->count;
			break;
		case _tag_swap_4:
			swap4(bytes, op->count);
			walk->statistics->bytes_swapped += 4 * op->count;
			bytes += 4 * op->count;
			break;
		case _tag_swap_block:
			swap4(bytes, 3);
			swap_block(walk, bytes, op->argument);
			bytes += 12;
			break;
		case _tag_swap_reference:
			swap4(bytes, 4);
			check_reference(walk, bytes);
			bytes += 16;
			break;
		case _tag_swap_data:
			swap4(bytes, 5);
			swap_data(walk, base, bytes, op->argument);
			bytes += 20;
			break;
		}
	}
	if (definition->hook == _tag_swap_hook_model_part)
		swap_model_part(walk, base);
	walk->depth--;
}

static long find_group(uint32_t group_tag)
{
	uint32_t low = 0, high = NUMBER_OF(tag_swap_groups);

	while (low < high)
	{
		uint32_t middle = (low + high) / 2;

		if (tag_swap_groups[middle].group_tag == group_tag)
			return tag_swap_groups[middle].struct_index;
		if (tag_swap_groups[middle].group_tag < group_tag)
			low = middle + 1;
		else
			high = middle;
	}
	return -1;
}

/* an array of D3D resource headers: common, data, lock */
static void swap_d3d_resources(struct walk *walk, uint32_t count, uint32_t address, const char *what)
{
	uint8_t *bytes;

	if (!count)
		return;
	bytes = count < 0x10000 ? walk_bytes(walk, address, count * D3D_RESOURCE_SIZE) : NULL;
	if (!bytes)
	{
		walk_error(walk, "%u %s at %08x", count, what, address);
		return;
	}
	walk_cover(walk, address, count * D3D_RESOURCE_SIZE);
	swap4(bytes, count * D3D_RESOURCE_SIZE / 4);
}

static void walk_begin(struct walk *walk, void *bytes, uint32_t address, uint32_t size,
	struct tag_swap_statistics *statistics, tag_swap_coverage_proc coverage)
{
	memset(statistics, 0, sizeof(*statistics));
	walk->bytes = bytes;
	walk->address = address;
	walk->size = size;
	walk->depth = 0;
	walk->instances = NULL;
	walk->tag_count = 0;
	walk->statistics = statistics;
	walk->coverage = coverage;
}

/* ---------- public code */

int tag_swap_cache_file_header(void *header)
{
	uint8_t *bytes = header;

	if (memcmp(bytes, "daeh", 4))
		return 0;
	/* signature, version, length, padding, tag data offset and size, unused; type, unused; checksum; footer */
	swap4(bytes, 8);
	swap2(bytes + 0x60, 2);
	swap4(bytes + 0x64, 1);
	swap4(bytes + 0x7FC, 1);
	return 1;
}

int tag_swap_tag_data(void *bytes, uint32_t address, uint32_t size, struct tag_swap_statistics *statistics,
	tag_swap_coverage_proc coverage)
{
	struct walk walk;
	uint8_t *header = bytes;
	uint8_t *instances;
	uint32_t tag_count;
	uint32_t index;

	if (size < TAG_HEADER_SIZE || memcmp(header + 0x20, "sgat", 4))
		return 0;
	walk_begin(&walk, bytes, address, size, statistics, coverage);
	/* instances, scenario, checksum, count, vertex buffers (count, address), index buffers (same), signature */
	swap4(header, TAG_HEADER_SIZE / 4);
	walk_cover(&walk, address, TAG_HEADER_SIZE);
	tag_count = read32(header + 12);
	instances = tag_count < 0x10000 ? walk_bytes(&walk, read32(header), tag_count * TAG_INSTANCE_SIZE) : NULL;
	if (!instances)
	{
		walk_error(&walk, "%u tags at %08x", tag_count, read32(header));
		return 1;
	}
	walk_cover(&walk, read32(header), tag_count * TAG_INSTANCE_SIZE);
	swap4(instances, tag_count * TAG_INSTANCE_SIZE / 4);
	walk.instances = instances;
	walk.tag_count = tag_count;
	/* the model parts read their vertex buffers' data addresses */
	swap_d3d_resources(&walk, read32(header + 16), read32(header + 20), "vertex buffers");
	swap_d3d_resources(&walk, read32(header + 24), read32(header + 28), "index buffers");
	for (index = 0; index < tag_count; index++)
	{
		const uint8_t *instance = instances + index * TAG_INSTANCE_SIZE;
		uint32_t group_tag = read32(instance);
		uint32_t name = read32(instance + 16);
		uint32_t tag_address = read32(instance + 20);
		long struct_index;

		if (walk.coverage)
		{
			const uint8_t *text = walk_bytes(&walk, name, 1);
			uint32_t length = 0;

			while (text && walk_bytes(&walk, name + length, 1) && text[length])
				length++;
			if (text)
				walk_cover(&walk, name, length + 1);
		}
		/* the structure BSPs load later (tag_swap_structure_bsp) */
		if (!tag_address)
			continue;
		struct_index = find_group(group_tag);
		if (struct_index < 0)
		{
			statistics->unknown_groups++;
			walk_error(&walk, "tag %u: unknown group %08x", index, group_tag);
			continue;
		}
		statistics->tags++;
		swap_struct(&walk, tag_address, (uint16_t)struct_index);
	}
	return 1;
}

int tag_swap_structure_bsp(void *bytes, uint32_t address, uint32_t size, struct tag_swap_statistics *statistics,
	tag_swap_coverage_proc coverage)
{
	struct walk walk;
	uint8_t *header = bytes;

	if (size < BSP_HEADER_SIZE || memcmp(header + 0x14, "psbs", 4))
		return 0;
	walk_begin(&walk, bytes, address, size, statistics, coverage);
	/* the structure BSP, its vertex buffers (count, address), its lightmap vertex buffers (same), signature */
	swap4(header, BSP_HEADER_SIZE / 4);
	walk_cover(&walk, address, BSP_HEADER_SIZE);
	swap_d3d_resources(&walk, read32(header + 4), read32(header + 8), "vertex buffers");
	swap_d3d_resources(&walk, read32(header + 12), read32(header + 16), "lightmap vertex buffers");
	statistics->tags++;
	swap_struct(&walk, read32(header), TAG_SWAP_STRUCT_SCENARIOSTRUCTUREBSP);
	return 1;
}

#ifdef HALO_PS3
/* ---------- the game's calls (cache_files.c) */

static void log_statistics(const char *what, const struct tag_swap_statistics *statistics, uint64_t microseconds)
{
	platform_log("%s swapped in %u ms: %u tags, %u structs, %u blocks, %u references (%u stale), %u data, "
		"%u model parts, %u bytes, %u unknown groups, %u errors", what, (unsigned int)(microseconds / 1000),
		statistics->tags, statistics->structs, statistics->blocks, statistics->references,
		statistics->stale_references, statistics->data, statistics->model_parts, statistics->bytes_swapped,
		statistics->unknown_groups, statistics->errors);
}

int cache_files_swap_header(void *header)
{
	return tag_swap_cache_file_header(header);
}

void cache_files_swap_tag_data(void *tag_data, long size)
{
	struct tag_swap_statistics statistics;
	uint64_t start = ps3_host_microseconds();

	if (tag_swap_tag_data(tag_data, (uint32_t)(uintptr_t)tag_data, (uint32_t)size, &statistics, NULL))
		log_statistics("tag data", &statistics, ps3_host_microseconds() - start);
}

void cache_files_swap_structure_bsp(void *structure_bsp, long size)
{
	struct tag_swap_statistics statistics;
	uint64_t start = ps3_host_microseconds();

	if (tag_swap_structure_bsp(structure_bsp, (uint32_t)(uintptr_t)structure_bsp, (uint32_t)size, &statistics, NULL))
		log_statistics("structure BSP", &statistics, ps3_host_microseconds() - start);
}
#endif
