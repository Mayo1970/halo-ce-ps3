/* The shader pack: the build translates the pixel shader keys of port/ps3/shader_keys.txt and the game's vertex
shaders with the current translator (port/ps3/src/ps3_rsx_*.c), and writes them as a C unit the PS3 links in. */

#include "ps3_rsx_program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXIMUM_KEYS 8192

static struct rsx_fragment_key keys[MAXIMUM_KEYS];
static uint32_t key_count;

static uint32_t shader_words[0x10000];
static uint32_t shader_word_count;
static uint32_t shader_offsets[128], shader_count;

static int read_keys(const char *path)
{
	char line[4096];
	unsigned long number = 0;
	FILE *file = fopen(path, "r");

	if (!file)
	{
		fprintf(stderr, "ps3_shader_pack: cannot read %s\n", path);
		return 0;
	}
	while (fgets(line, sizeof(line), file))
	{
		struct rsx_fragment_key key;
		uint32_t index;

		number++;
		if (line[strspn(line, " \t\r\n")] == 0 || line[strspn(line, " \t")] == '#')
			continue;
		if (rsx_fragment_key_parse(line, &key) != 0)
		{
			fprintf(stderr, "%s:%lu: not a key\n", path, number);
			fclose(file);
			return 0;
		}
		rsx_fragment_key_clean(&key);
		for (index = 0; index < key_count && memcmp(&keys[index], &key, sizeof(key)); index++)
			;
		if (index < key_count)
			continue;
		if (key_count == MAXIMUM_KEYS)
		{
			fprintf(stderr, "%s: more than %d keys\n", path, MAXIMUM_KEYS);
			fclose(file);
			return 0;
		}
		keys[key_count++] = key;
	}
	fclose(file);
	return 1;
}

/* the game's vertex shaders, as tools/ps3_rsx_shader_check.c reads them */
static int read_vertex_shaders(const char *source_root)
{
	char path[1024], line[4096];
	FILE *file;

	snprintf(path, sizeof(path), "%s/source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_data.inc", source_root);
	file = fopen(path, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file) && !strstr(line, "*/"))
		;
	while (fgets(line, sizeof(line), file))
	{
		char *cursor = line;

		while ((cursor = strstr(cursor, "0x")) != NULL && shader_word_count < 0x10000)
			shader_words[shader_word_count++] = (uint32_t)strtoul(cursor, &cursor, 16);
	}
	fclose(file);
	snprintf(path, sizeof(path), "%s/source/rasterizer/xbox/rasterizer_xbox_vertex_shaders.c", source_root);
	file = fopen(path, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file))
	{
		unsigned offset, bytes;

		if (sscanf(line, " VERTEX_SHADER_ENTRY(0x%x, 0x%x)", &offset, &bytes) == 2 && shader_count < 128 &&
			offset / 4 < shader_word_count)
		{
			shader_offsets[shader_count++] = offset;
		}
	}
	fclose(file);
	return shader_count != 0;
}

static void write_words(FILE *out, const char *name, const uint32_t *words, uint32_t count)
{
	uint32_t index;

	fprintf(out, "static uint32_t %s[] =\n{", name);
	for (index = 0; index < count; index++)
		fprintf(out, "%s0x%08x,", index % 8 ? " " : "\n\t", words[index]);
	fprintf(out, "%s};\n\n", count ? "\n" : "\n\t0\n");
}

static void write_key(FILE *out, const struct rsx_fragment_key *key)
{
	int index;

	fprintf(out, "\t\t{\n\t\t\t{");
	for (index = 0; index < 8; index++)
		fprintf(out, "%s0x%08x", index ? ", " : " ", key->rgb_inputs[index]);
	fprintf(out, " },\n\t\t\t{");
	for (index = 0; index < 8; index++)
		fprintf(out, "%s0x%08x", index ? ", " : " ", key->rgb_outputs[index]);
	fprintf(out, " },\n\t\t\t{");
	for (index = 0; index < 8; index++)
		fprintf(out, "%s0x%08x", index ? ", " : " ", key->alpha_inputs[index]);
	fprintf(out, " },\n\t\t\t{");
	for (index = 0; index < 8; index++)
		fprintf(out, "%s0x%08x", index ? ", " : " ", key->alpha_outputs[index]);
	fprintf(out, " },\n\t\t\t0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x,\n",
		key->final_inputs_abcd, key->final_inputs_efg, key->combiner_count, key->texture_modes, key->dot_mapping,
		key->input_texture, key->compare_mode);
	fprintf(out, "\t\t\t{ %u, %u, %u, %u }, { %u, %u, %u, %u }, { %u, %u, %u, %u }, %u, %u, %u, 0,\n\t\t},\n",
		key->sampler_type[0], key->sampler_type[1], key->sampler_type[2], key->sampler_type[3], key->alpha_kill[0],
		key->alpha_kill[1], key->alpha_kill[2], key->alpha_kill[3], key->color_sign[0], key->color_sign[1],
		key->color_sign[2], key->color_sign[3], key->fog_enable, key->fog_table_mode, key->texture_scaled);
}

int main(int argc, char *argv[])
{
	static struct rsx_fragment_program fragments[MAXIMUM_KEYS];
	static struct rsx_vertex_program vertices[128];
	static int fragment_failed[MAXIMUM_KEYS];
	const char *keys_path = NULL, *source_root = ".", *out_path = NULL;
	uint32_t index, slot, failures = 0;
	char name[64];
	FILE *out;
	int argument;

	for (argument = 1; argument + 1 < argc; argument += 2)
	{
		if (!strcmp(argv[argument], "--keys"))
			keys_path = argv[argument + 1];
		else if (!strcmp(argv[argument], "--source"))
			source_root = argv[argument + 1];
		else if (!strcmp(argv[argument], "--out"))
			out_path = argv[argument + 1];
	}
	if (!keys_path || !out_path)
	{
		fprintf(stderr, "usage: ps3_shader_pack --keys shader_keys.txt --source ROOT --out pack.c\n");
		return 2;
	}
	if (!read_keys(keys_path))
		return 1;
	if (!read_vertex_shaders(source_root))
	{
		fprintf(stderr, "ps3_shader_pack: cannot read the vertex shaders under %s\n", source_root);
		return 1;
	}

	for (index = 0; index < key_count; index++)
	{
		fragment_failed[index] = rsx_fragment_program_translate(&keys[index], &fragments[index]) != 0;
		if (fragment_failed[index])
		{
			char text[RSX_FRAGMENT_KEY_TEXT];

			rsx_fragment_key_text(&keys[index], text);
			fprintf(stderr, "ps3_shader_pack: cannot translate %s\n", text);
			failures++;
		}
	}
	for (index = 0; index < shader_count; index++)
	{
		const uint32_t *program = shader_words + shader_offsets[index] / 4;

		if (rsx_vertex_program_translate(program + 1, program[0] >> 16, &vertices[index]) != 0)
		{
			fprintf(stderr, "ps3_shader_pack: cannot translate vertex shader %u\n", index);
			return 1;
		}
	}

	out = fopen(out_path, "w");
	if (!out)
	{
		fprintf(stderr, "ps3_shader_pack: cannot write %s\n", out_path);
		return 1;
	}
	fprintf(out, "/* Made by tools/ps3_shader_pack.c from %s and the game's vertex shaders: do not edit. */\n\n"
		"#include \"ps3_rsx_program.h\"\n\n", keys_path);
	for (index = 0; index < key_count; index++)
	{
		if (fragment_failed[index])
			continue;
		snprintf(name, sizeof(name), "fragment_%u", index);
		write_words(out, name, fragments[index].microcode, fragments[index].word_count);
	}
	for (index = 0; index < shader_count; index++)
	{
		const uint32_t *program = shader_words + shader_offsets[index] / 4;

		snprintf(name, sizeof(name), "vertex_nv2a_%u", index);
		write_words(out, name, program + 1, (program[0] >> 16) * 4);
		snprintf(name, sizeof(name), "vertex_%u", index);
		write_words(out, name, vertices[index].microcode, vertices[index].instruction_count * 4);
	}

	fprintf(out, "const struct rsx_packed_fragment rsx_packed_fragments[] =\n{\n");
	for (index = 0, slot = 0; index < key_count; index++)
	{
		const struct rsx_fragment_program *program = &fragments[index];
		uint32_t uniform;

		if (fragment_failed[index])
			continue;
		fprintf(out, "\t{\n");
		write_key(out, &keys[index]);
		fprintf(out, "\t\t{ fragment_%u, %u, 0x%x, %u, 0x%x, %u,\n\t\t\t{", index, program->word_count,
			program->control, program->register_count, program->texcoord_mask, program->uniform_count);
		for (uniform = 0; uniform < program->uniform_count; uniform++)
			fprintf(out, "%s{ %u, %u }", uniform ? ", " : " ", program->uniforms[uniform].word,
				program->uniforms[uniform].uniform);
		fprintf(out, "%s},\n\t\t},\n\t},\n", program->uniform_count ? " " : " { 0, 0 } ");
		slot++;
	}
	if (!slot)
		fprintf(out, "\t{ { { 0 } }, { 0 } },\n");
	fprintf(out, "};\n\nconst uint32_t rsx_packed_fragment_count = %u;\n\n", slot);

	fprintf(out, "const struct rsx_packed_vertex rsx_packed_vertices[] =\n{\n");
	for (index = 0; index < shader_count; index++)
	{
		const uint32_t *program = shader_words + shader_offsets[index] / 4;

		fprintf(out, "\t{ vertex_nv2a_%u, %u, { vertex_%u, %u, 0x%x, 0x%x, %u } },\n", index, program[0] >> 16, index,
			vertices[index].instruction_count, vertices[index].input_mask, vertices[index].output_mask,
			vertices[index].register_count);
	}
	fprintf(out, "};\n\nconst uint32_t rsx_packed_vertex_count = %u;\n", shader_count);
	fclose(out);
	printf("shader pack: %u pixel shaders (%u keys could not be translated), %u vertex shaders\n", slot, failures,
		shader_count);
	return 0;
}
