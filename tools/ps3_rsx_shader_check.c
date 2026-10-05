/* The PC check of port/ps3/src/ps3_rsx_*.c: the microcode against cgcomp's assembler, and the translated
programs against interpreters of the NV2A's semantics (port/linux/src/nv2a_vsh.c, nv2a_psh.c). */

#include "ps3_rsx_program.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *cgcomp = "/usr/local/ps3dev/bin/cgcomp";
static const char *work = "/tmp";
static unsigned long failures;
static int verbose;

static void fail(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	putchar('\n');
	failures++;
}

/* ---------- random numbers (xorshift) */

static uint32_t random_state = 0x12345678;

static uint32_t random_next(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static float random_float(float low, float high)
{
	return low + (high - low) * (float)(random_next() & 0xffffff) / (float)0x1000000;
}

static uint32_t random_below(uint32_t count)
{
	return random_next() % count;
}

static int close_enough(float a, float b, float tolerance)
{
	float scale = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);

	if (isnan(a) || isnan(b))
		return isnan(a) && isnan(b);
	if (isinf(a) || isinf(b))
		return a == b || scale > 1.0e15f;
	return fabsf(a - b) <= tolerance * (scale > 1.0f ? scale : 1.0f);
}

/* ---------- the assembler text cgcomp reads */

static const char *components = "xyzw";

static void print_mask(FILE *file, int mask)
{
	int component;

	if (mask == _rsx_xyzw)
		return;
	fputc('.', file);
	for (component = 0; component < 4; component++)
	{
		if (mask & (1 << component))
			fputc(components[component], file);
	}
}

static void print_swizzle(FILE *file, const struct rsx_source *source)
{
	fprintf(file, ".%c%c%c%c", components[source->swizzle[0]], components[source->swizzle[1]],
		components[source->swizzle[2]], components[source->swizzle[3]]);
}

static const char *vertex_output_name(int output)
{
	static char name[32];

	switch (output)
	{
	case _rsx_vp_output_position: return "result.position";
	case _rsx_vp_output_color0: return "result.color.primary";
	case _rsx_vp_output_color1: return "result.color.secondary";
	case _rsx_vp_output_back_color0: return "result.color.back.primary";
	case _rsx_vp_output_back_color1: return "result.color.back.secondary";
	case _rsx_vp_output_fog: return "result.fogcoord";
	case _rsx_vp_output_point_size: return "result.pointsize";
	default:
		snprintf(name, sizeof(name), "result.texcoord[%d]", output - _rsx_vp_output_texture0);
		return name;
	}
}

static void print_vertex_source(FILE *file, const struct rsx_source *source)
{
	if (source->negate)
		fputc('-', file);
	switch (source->type)
	{
	case _rsx_register_temporary: fprintf(file, "R%u", source->index); break;
	case _rsx_register_input: fprintf(file, "v[%u]", source->index); break;
	/* cgcomp cannot assemble c[A0.x + n]: assembled as c[n], the relative flag compared apart */
	case _rsx_register_constant: fprintf(file, "c[%u]", source->index); break;
	default: fprintf(file, "R0"); break;
	}
	print_swizzle(file, source);
}

static void print_vertex(FILE *file, const struct rsx_instruction *instructions, uint32_t count)
{
	static const char *vector[] =
	{
		"NOP", "MOV", "MUL", "ADD", "MAD", "DP3", "DPH", "DP4", "DST", "MIN", "MAX", "SLT", "SGE", "ARL",
	};
	static const char *scalar[] = { "NOP", "MOV", "RCP", "RCC", "RSQ", "EXP", "LOG", "LIT" };
	uint32_t index;

	fprintf(file, "!!VP2.0\n");
	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];
		int is_scalar = (instruction->opcode & _rsx_vp_scalar) != 0;
		int opcode = instruction->opcode & 0x1f;

		fprintf(file, "%s%s ", is_scalar ? scalar[opcode] : vector[opcode], instruction->saturate ? "_SAT" : "");
		switch (instruction->destination_type)
		{
		case _rsx_register_temporary: fprintf(file, "R%u", instruction->destination); break;
		case _rsx_register_address: fprintf(file, "A0"); break;
		case _rsx_register_output: fprintf(file, "%s", vertex_output_name(instruction->destination)); break;
		default: fprintf(file, "RC"); break;
		}
		print_mask(file, instruction->mask);
		if (is_scalar)
		{
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[2]);
		}
		else if (opcode == _rsx_vp_mov || opcode == _rsx_vp_arl)
		{
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[0]);
		}
		else if (opcode == _rsx_vp_add)
		{
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[0]);
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[2]);
		}
		else if (opcode == _rsx_vp_mad)
		{
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[0]);
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[1]);
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[2]);
		}
		else
		{
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[0]);
			fprintf(file, ", ");
			print_vertex_source(file, &instruction->source[1]);
		}
		fprintf(file, ";\n");
	}
	fprintf(file, "END\n");
}

static const char *fragment_input_name(int index)
{
	static char name[16];

	switch (index)
	{
	case _rsx_fp_input_color0: return "COL0";
	case _rsx_fp_input_color1: return "COL1";
	case _rsx_fp_input_fog: return "FOGC";
	default:
		snprintf(name, sizeof(name), "TEX%d", index - _rsx_fp_input_texture0);
		return name;
	}
}

static void print_fragment_source(FILE *file, const struct rsx_source *source, int constant_index)
{
	if (source->negate)
		fputc('-', file);
	if (source->absolute)
		fputc('|', file);
	switch (source->type)
	{
	case _rsx_register_temporary: fprintf(file, "R%u", source->index); break;
	case _rsx_register_input: fprintf(file, "f[%s]", fragment_input_name(source->index)); break;
	case _rsx_register_constant: fprintf(file, "c[%d]", constant_index); break;
	default: fprintf(file, "R0"); break;
	}
	print_swizzle(file, source);
	if (source->absolute)
		fputc('|', file);
}

static void print_fragment(FILE *file, const struct rsx_instruction *instructions, uint32_t count)
{
	static const char *conditions[] = { "FL", "LT", "EQ", "LE", "GT", "NE", "GE", "TR" };
	uint32_t index;

	fprintf(file, "!!FP2.0\n");
	for (index = 0; index < count; index++)
	{
		if (rsx_fragment_reads_constant(&instructions[index]))
			fprintf(file, "#const c[%u] = %.9g %.9g %.9g %.9g\n", index, instructions[index].constant[0],
				instructions[index].constant[1], instructions[index].constant[2], instructions[index].constant[3]);
	}
	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];
		int sources = 0, position, sampling = 0;
		const char *name = "NOP";

		switch (instruction->opcode)
		{
		case _rsx_fp_mov: name = "MOV"; sources = 1; break;
		case _rsx_fp_mul: name = "MUL"; sources = 2; break;
		case _rsx_fp_add: name = "ADD"; sources = 2; break;
		case _rsx_fp_mad: name = "MAD"; sources = 3; break;
		case _rsx_fp_dp3: name = "DP3"; sources = 2; break;
		case _rsx_fp_dp4: name = "DP4"; sources = 2; break;
		case _rsx_fp_min: name = "MIN"; sources = 2; break;
		case _rsx_fp_max: name = "MAX"; sources = 2; break;
		case _rsx_fp_slt: name = "SLT"; sources = 2; break;
		case _rsx_fp_sge: name = "SGE"; sources = 2; break;
		case _rsx_fp_seq: name = "SEQ"; sources = 2; break;
		case _rsx_fp_frc: name = "FRC"; sources = 1; break;
		case _rsx_fp_flr: name = "FLR"; sources = 1; break;
		case _rsx_fp_rcp: name = "RCP"; sources = 1; break;
		case _rsx_fp_ex2: name = "EX2"; sources = 1; break;
		case _rsx_fp_lg2: name = "LG2"; sources = 1; break;
		case _rsx_fp_tex: name = "TEX"; sources = 1; sampling = 1; break;
		case _rsx_fp_txp: name = "TXP"; sources = 1; sampling = 1; break;
		default: break;
		}
		if (instruction->opcode == _rsx_fp_kil)
		{
			fprintf(file, "KIL %s.%c%c%c%c;\n", conditions[instruction->condition],
				components[instruction->condition_swizzle[0]], components[instruction->condition_swizzle[1]],
				components[instruction->condition_swizzle[2]], components[instruction->condition_swizzle[3]]);
			continue;
		}
		fprintf(file, "%s%s%s%s ", name, sampling ? "" : "R", instruction->condition_update ? "C" : "",
			instruction->saturate ? "_SAT" : "");
		if (instruction->destination_type == _rsx_register_none)
			fprintf(file, "RC");
		else if (instruction->destination_type == _rsx_register_output)
			fprintf(file, "result.color");
		else
			fprintf(file, "R%u", instruction->destination);
		print_mask(file, instruction->mask);
		for (position = 0; position < sources; position++)
		{
			fprintf(file, ", ");
			print_fragment_source(file, &instruction->source[position], (int)index);
		}
		if (sampling)
			fprintf(file, ", texture[%u], 2D", instruction->texture_unit);
		fprintf(file, ";\n");
	}
	fprintf(file, "END\n");
}

/* ---------- cgcomp */

static uint32_t read_be32(const unsigned char *bytes)
{
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

/* assembles the text, giving its microcode words (fragment programs: halves swapped back) */
static uint32_t *cgcomp_assemble(int fragment, const char *text_path, uint32_t *word_count)
{
	char command[1024], output[512];
	unsigned char *bytes;
	uint32_t *words, offset, count, index;
	long size;
	FILE *file;

	snprintf(output, sizeof(output), "%s.o", text_path);
	snprintf(command, sizeof(command), "%s -a %s %s %s > %s.log 2>&1", cgcomp, fragment ? "-f" : "-v", text_path,
		output, text_path);
	if (system(command) != 0)
		return NULL;
	file = fopen(output, "rb");
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc((size_t)size);
	if (fread(bytes, 1, (size_t)size, file) != (size_t)size)
	{
		fclose(file);
		free(bytes);
		return NULL;
	}
	fclose(file);
	count = ((uint32_t)bytes[10] << 8) | bytes[11];
	offset = read_be32(bytes + 20);
	words = malloc(count * 4 * sizeof(uint32_t) + 4);
	for (index = 0; index < count * 4; index++)
	{
		uint32_t word = read_be32(bytes + offset + index * 4);

		words[index] = fragment ? (word << 16) | (word >> 16) : word;
	}
	free(bytes);
	*word_count = count * 4;
	return words;
}

/* ---------- NV2A vertex programs: the reference (nv2a_vsh.c) */

#define TARGET_WIDTH 640.0f
#define TARGET_HEIGHT 480.0f
#define DEPTH_SCALE 16777215.0f

struct vertex_state
{
	float inputs[16][4];
	float constants[512][4];
};

static uint32_t field(const uint32_t *instruction, int word, int low_bit, int bit_count)
{
	return (instruction[word] >> low_bit) & ((1u << bit_count) - 1);
}

/* NV2A mask (x in bit 3) as component bits (x in bit 0) */
static uint32_t nv2a_mask_bits(uint32_t mask)
{
	return ((mask & 8) >> 3) | ((mask & 4) >> 1) | ((mask & 2) << 1) | ((mask & 1) << 3);
}

static int clamp_index(int index)
{
	return index < 0 ? 0 : index > 191 ? 191 : index;
}

static void nv2a_operand(const uint32_t *instruction, char which, const struct vertex_state *state,
	float temporaries[13][4], int a0, float out[4], uint32_t *unwritten_reads, const uint8_t written[13])
{
	uint32_t negate, swizzle[4], index, mux, component;
	const float *value;
	static const float zero[4];

	switch (which)
	{
	case 'A':
		negate = field(instruction, 1, 8, 1);
		swizzle[0] = field(instruction, 1, 6, 2); swizzle[1] = field(instruction, 1, 4, 2);
		swizzle[2] = field(instruction, 1, 2, 2); swizzle[3] = field(instruction, 1, 0, 2);
		index = field(instruction, 2, 28, 4);
		mux = field(instruction, 2, 26, 2);
		break;
	case 'B':
		negate = field(instruction, 2, 25, 1);
		swizzle[0] = field(instruction, 2, 23, 2); swizzle[1] = field(instruction, 2, 21, 2);
		swizzle[2] = field(instruction, 2, 19, 2); swizzle[3] = field(instruction, 2, 17, 2);
		index = field(instruction, 2, 13, 4);
		mux = field(instruction, 2, 11, 2);
		break;
	default:
		negate = field(instruction, 2, 10, 1);
		swizzle[0] = field(instruction, 2, 8, 2); swizzle[1] = field(instruction, 2, 6, 2);
		swizzle[2] = field(instruction, 2, 4, 2); swizzle[3] = field(instruction, 2, 2, 2);
		index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		mux = field(instruction, 3, 28, 2);
		break;
	}
	switch (mux)
	{
	case 1:
		value = temporaries[index > 12 ? 12 : index];
		if (index < 12)
		{
			for (component = 0; component < 4; component++)
			{
				if (!(written[index] & (1u << swizzle[component])))
					*unwritten_reads |= 1;
			}
		}
		break;
	case 2: value = state->inputs[field(instruction, 1, 9, 4)]; break;
	case 3:
		value = field(instruction, 3, 1, 1) ? state->constants[clamp_index(a0 + (int)field(instruction, 1, 13, 8))] :
			state->constants[field(instruction, 1, 13, 8)];
		break;
	default: value = zero; break;
	}
	for (component = 0; component < 4; component++)
		out[component] = negate ? -value[swizzle[component]] : value[swizzle[component]];
}

static float rcc(float x)
{
	float r = 1.0f / x;

	if (r > 0.0f)
		return r < 5.42101e-20f ? 5.42101e-20f : r > 1.884467e+19f ? 1.884467e+19f : r;
	return r > -5.42101e-20f ? -5.42101e-20f : r < -1.884467e+19f ? -1.884467e+19f : r;
}

/* the scalar unit, with the operand C */
static void scalar_unit(int opcode, const float c[4], float out[4])
{
	int component;

	switch (opcode)
	{
	case 1: memcpy(out, c, 16); return;
	case 2: out[0] = 1.0f / c[0]; break;
	case 3: out[0] = rcc(c[0]); break;
	case 4: out[0] = 1.0f / sqrtf(fabsf(c[0])); break;
	case 5:
		out[0] = exp2f(floorf(c[0]));
		out[1] = c[0] - floorf(c[0]);
		out[2] = exp2f(c[0]);
		out[3] = 1.0f;
		return;
	case 6:
	{
		float x = fabsf(c[0]);

		if (x == 0.0f)
		{
			out[0] = -1.0e30f; out[1] = 1.0f; out[2] = -1.0e30f; out[3] = 1.0f;
			return;
		}
		out[0] = floorf(log2f(x));
		out[1] = x / exp2f(out[0]);
		out[2] = log2f(x);
		out[3] = 1.0f;
		return;
	}
	case 7:
	{
		float power = c[3] < -127.9961f ? -127.9961f : c[3] > 127.9961f ? 127.9961f : c[3];

		out[0] = 1.0f;
		out[1] = c[0] > 0.0f ? c[0] : 0.0f;
		out[2] = c[0] > 0.0f ? powf(c[1] > 0.0f ? c[1] : 0.0f, power) : 0.0f;
		out[3] = 1.0f;
		return;
	}
	default: out[0] = 0.0f; break;
	}
	for (component = 1; component < 4; component++)
		out[component] = out[0];
}

static void vector_unit(int opcode, const float a[4], const float b[4], const float c[4], float out[4])
{
	int component;

	switch (opcode)
	{
	case 1: case 13: memcpy(out, a, 16); return;
	case 2: for (component = 0; component < 4; component++) out[component] = a[component] * b[component]; return;
	case 3: for (component = 0; component < 4; component++) out[component] = a[component] + c[component]; return;
	case 4: for (component = 0; component < 4; component++) out[component] = a[component] * b[component] + c[component]; return;
	case 5: out[0] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
	case 6: out[0] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + b[3]; break;
	case 7: out[0] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; break;
	case 8: out[0] = 1.0f; out[1] = a[1] * b[1]; out[2] = a[2]; out[3] = b[3]; return;
	case 9: for (component = 0; component < 4; component++) out[component] = a[component] < b[component] ? a[component] : b[component]; return;
	case 10: for (component = 0; component < 4; component++) out[component] = a[component] > b[component] ? a[component] : b[component]; return;
	case 11: for (component = 0; component < 4; component++) out[component] = a[component] < b[component] ? 1.0f : 0.0f; return;
	case 12: for (component = 0; component < 4; component++) out[component] = a[component] >= b[component] ? 1.0f : 0.0f; return;
	default: memset(out, 0, 16); return;
	}
	for (component = 1; component < 4; component++)
		out[component] = out[0];
}

static void masked_write(float target[4], const float value[4], uint32_t mask)
{
	int component;

	for (component = 0; component < 4; component++)
	{
		if (mask & (1u << component))
			target[component] = value[component];
	}
}

/* outputs[0] is the screen position, outputs[n] the NV2A's o[n]; 1 if a temporary was read unwritten */
static int nv2a_run(const uint32_t *program, uint32_t count, const struct vertex_state *state, float outputs[16][4])
{
	float temporaries[13][4];
	uint8_t written[13];
	uint32_t index, unwritten = 0;
	int a0 = 0, output;

	memset(temporaries, 0, sizeof(temporaries));
	memset(written, 0, sizeof(written));
	temporaries[12][3] = 1.0f;
	for (output = 0; output < 16; output++)
	{
		outputs[output][0] = outputs[output][1] = outputs[output][2] = 0.0f;
		outputs[output][3] = 1.0f;
	}
	for (output = 0; output < 4; output++)
		outputs[5][output] = 1.0f;
	for (index = 0; index < count; index++)
	{
		const uint32_t *instruction = program + index * 4;
		uint32_t mac = field(instruction, 1, 21, 4), ilu = field(instruction, 1, 25, 3);
		uint32_t temporary = field(instruction, 3, 20, 4);
		float a[4], b[4], c[4], mac_result[4], ilu_result[4];

		if (mac > 13)
			mac = 0;
		{
			/* only the operands the units read count as reads */
			uint32_t ignored = 0;
			int reads_a = mac != 0, reads_b = mac == 4 || (mac >= 2 && mac != 3 && mac <= 12);
			int reads_c = mac == 3 || mac == 4 || ilu != 0;

			nv2a_operand(instruction, 'A', state, temporaries, a0, a, reads_a ? &unwritten : &ignored, written);
			nv2a_operand(instruction, 'B', state, temporaries, a0, b, reads_b ? &unwritten : &ignored, written);
			nv2a_operand(instruction, 'C', state, temporaries, a0, c, reads_c ? &unwritten : &ignored, written);
		}
		vector_unit((int)mac, a, b, c, mac_result);
		scalar_unit((int)ilu, c, ilu_result);
		if (mac == 13)
		{
			a0 = (int)floorf(mac_result[0] + 0.001f);
		}
		else if (mac && field(instruction, 3, 24, 4))
		{
			masked_write(temporaries[temporary > 12 ? 12 : temporary], mac_result, nv2a_mask_bits(field(instruction, 3, 24, 4)));
			if (temporary < 12)
				written[temporary] |= (uint8_t)nv2a_mask_bits(field(instruction, 3, 24, 4));
		}
		if (ilu && field(instruction, 3, 16, 4))
		{
			uint32_t destination = mac ? 1 : temporary;

			masked_write(temporaries[destination > 12 ? 12 : destination], ilu_result,
				nv2a_mask_bits(field(instruction, 3, 16, 4)));
			if (destination < 12)
				written[destination] |= (uint8_t)nv2a_mask_bits(field(instruction, 3, 16, 4));
		}
		if (field(instruction, 3, 12, 4) && field(instruction, 3, 11, 1) &&
			(field(instruction, 3, 2, 1) ? ilu : (mac && mac != 13)))
		{
			uint32_t address = field(instruction, 3, 3, 8);
			const float *source = field(instruction, 3, 2, 1) ? ilu_result : mac_result;

			if (address == 0)
				masked_write(temporaries[12], source, nv2a_mask_bits(field(instruction, 3, 12, 4)));
			else if (address < 16)
				masked_write(outputs[address], source, nv2a_mask_bits(field(instruction, 3, 12, 4)));
		}
		if (field(instruction, 3, 0, 1))
			break;
	}
	memcpy(outputs[0], temporaries[12], 16);
	for (output = 3; output <= 8; output++)
	{
		int component;

		if (output == 5 || output == 6)
			continue;
		for (component = 0; component < 4; component++)
			outputs[output][component] = outputs[output][component] < 0.0f ? 0.0f :
				outputs[output][component] > 1.0f ? 1.0f : outputs[output][component];
	}
	return unwritten != 0;
}

/* ---------- RSX vertex programs: an interpreter of the instructions */

static void rsx_vertex_source(const struct rsx_source *source, float temporaries[32][4], const struct vertex_state *state,
	int a0, float out[4])
{
	static const float zero[4];
	const float *value = zero;
	int component;

	switch (source->type)
	{
	case _rsx_register_temporary: value = temporaries[source->index & 31]; break;
	case _rsx_register_input: value = state->inputs[source->index & 15]; break;
	case _rsx_register_constant:
		value = state->constants[source->relative ? clamp_index(a0 + source->index) : source->index & 511];
		break;
	default: break;
	}
	for (component = 0; component < 4; component++)
	{
		out[component] = value[source->swizzle[component]];
		if (source->absolute)
			out[component] = fabsf(out[component]);
		if (source->negate)
			out[component] = -out[component];
	}
}

/* outputs as the RSX's results */
static void rsx_vertex_run(const struct rsx_instruction *instructions, uint32_t count, const struct vertex_state *state,
	float outputs[16][4])
{
	float temporaries[32][4], a[4], b[4], c[4], result[4];
	uint32_t index;
	int a0 = 0, component;

	/* the RSX's temporaries start undefined: a value the comparison would catch */
	for (index = 0; index < 32; index++)
		temporaries[index][0] = temporaries[index][1] = temporaries[index][2] = temporaries[index][3] = 7.0e6f;
	memset(outputs, 0, 16 * 4 * sizeof(float));
	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];
		int opcode = instruction->opcode & 0x1f;

		rsx_vertex_source(&instruction->source[0], temporaries, state, a0, a);
		rsx_vertex_source(&instruction->source[1], temporaries, state, a0, b);
		rsx_vertex_source(&instruction->source[2], temporaries, state, a0, c);
		if (instruction->opcode & _rsx_vp_scalar)
			scalar_unit(opcode, c, result);
		else
			vector_unit(opcode, a, b, c, result);
		if (instruction->saturate)
		{
			for (component = 0; component < 4; component++)
				result[component] = result[component] < 0.0f ? 0.0f : result[component] > 1.0f ? 1.0f : result[component];
		}
		switch (instruction->destination_type)
		{
		case _rsx_register_temporary: masked_write(temporaries[instruction->destination & 31], result, instruction->mask); break;
		case _rsx_register_output: masked_write(outputs[instruction->destination & 15], result, instruction->mask); break;
		case _rsx_register_address: a0 = (int)floorf(result[0]); break;
		default: break;
		}
	}
}

/* ---------- vertex programs: the check */

static uint32_t shader_words[0x10000];
static uint32_t shader_word_count;
static uint32_t shader_offsets[128], shader_bytes[128], shader_count;

static int load_vertex_shaders(const char *source_root)
{
	char path[1024], line[4096];
	FILE *file;

	snprintf(path, sizeof(path), "%s/source/rasterizer/xbox/rasterizer_xbox_vertex_shaders_data.inc", source_root);
	file = fopen(path, "r");
	if (!file)
		return 0;
	/* past the header comment, which names the payload's size in hex too */
	while (fgets(line, sizeof(line), file) && !strstr(line, "*/"))
		;
	while (fgets(line, sizeof(line), file))
	{
		char *cursor = line;

		while ((cursor = strstr(cursor, "0x")) != NULL)
		{
			shader_words[shader_word_count++] = (uint32_t)strtoul(cursor, &cursor, 16);
		}
	}
	fclose(file);
	snprintf(path, sizeof(path), "%s/source/rasterizer/xbox/rasterizer_xbox_vertex_shaders.c", source_root);
	file = fopen(path, "r");
	if (!file)
		return 0;
	while (fgets(line, sizeof(line), file))
	{
		unsigned offset, bytes;

		if (sscanf(line, " VERTEX_SHADER_ENTRY(0x%x, 0x%x)", &offset, &bytes) == 2 && shader_count < 128)
		{
			shader_offsets[shader_count] = offset;
			shader_bytes[shader_count] = bytes;
			shader_count++;
		}
	}
	fclose(file);
	return shader_count != 0;
}

static void vertex_constants(struct vertex_state *state)
{
	int index;

	for (index = 0; index < 192; index++)
	{
		int component;

		for (component = 0; component < 4; component++)
			state->constants[index][component] = random_float(-1.0f, 1.0f);
	}
	/* Direct3D's viewport constants for a 640x480 target, depth 0..1 */
	state->constants[58][0] = TARGET_WIDTH * 0.5f;
	state->constants[58][1] = -TARGET_HEIGHT * 0.5f;
	state->constants[58][2] = DEPTH_SCALE;
	state->constants[58][3] = 0.0f;
	state->constants[59][0] = TARGET_WIDTH * 0.5f;
	state->constants[59][1] = TARGET_HEIGHT * 0.5f;
	state->constants[59][2] = 0.0f;
	state->constants[59][3] = 0.0f;
	/* the device's clip-space constants (d3d8_rsx.c), for that target and depth range */
	state->constants[_rsx_vp_constant_screen_scale][0] = 2.0f / TARGET_WIDTH;
	state->constants[_rsx_vp_constant_screen_scale][1] = -2.0f / TARGET_HEIGHT;
	state->constants[_rsx_vp_constant_screen_scale][2] = 2.0f / DEPTH_SCALE;
	state->constants[_rsx_vp_constant_screen_scale][3] = 0.0f;
	state->constants[_rsx_vp_constant_screen_offset][0] = 1.0f / TARGET_WIDTH - 1.0f;
	state->constants[_rsx_vp_constant_screen_offset][1] = 1.0f - 1.0f / TARGET_HEIGHT;
	state->constants[_rsx_vp_constant_screen_offset][2] = -1.0f;
	state->constants[_rsx_vp_constant_screen_offset][3] = 0.0f;
	memcpy(state->constants[_rsx_vp_constant_default], (float[4]){ 0.0f, 0.0f, 0.0f, 1.0f }, 16);
	memcpy(state->constants[_rsx_vp_constant_one], (float[4]){ 1.0f, 1.0f, 1.0f, 1.0f }, 16);
	memcpy(state->constants[_rsx_vp_constant_address_bias], (float[4]){ 0.001f, 0.0f, 0.0f, 0.0f }, 16);
	memcpy(state->constants[_rsx_vp_constant_zero], (float[4]){ 0.0f, 0.0f, 0.0f, 0.0f }, 16);
}

static void check_vertex_shader(uint32_t shader, uint32_t *unwritten_shaders, uint32_t *relative_shaders)
{
	const uint32_t *program = shader_words + shader_offsets[shader] / 4;
	uint32_t count = program[0] >> 16;
	static struct rsx_instruction instructions[RSX_VP_MAXIMUM_INSTRUCTIONS];
	uint32_t instruction_count = 0, index, word_count = 0;
	uint32_t words[RSX_VP_MAXIMUM_INSTRUCTIONS * 4], *assembled;
	char path[512];
	FILE *file;
	int trial, relative = 0, unwritten = 0;

	if (rsx_vertex_program_instructions(program + 1, count, instructions, RSX_VP_MAXIMUM_INSTRUCTIONS,
		&instruction_count) != 0)
	{
		fail("vertex shader %u: translation failed", shader);
		return;
	}
	for (index = 0; index < instruction_count; index++)
		rsx_vertex_encode(&instructions[index], index + 1 == instruction_count, words + index * 4);

	/* the words against cgcomp's */
	snprintf(path, sizeof(path), "%s/vs%02u.vp", work, shader);
	file = fopen(path, "w");
	print_vertex(file, instructions, instruction_count);
	fclose(file);
	assembled = cgcomp_assemble(0, path, &word_count);
	if (!assembled || word_count != instruction_count * 4)
	{
		fail("vertex shader %u: cgcomp gave %u words for %u instructions (%s)", shader, word_count,
			instruction_count, path);
	}
	else
	{
		for (index = 0; index < word_count; index++)
		{
			uint32_t ours = words[index];

			if ((index & 3) == 3 && (ours & 2))
			{
				/* the relative flag (cgcomp has no c[A0.x + n]) */
				ours &= ~2u;
				relative = 1;
			}
			if (ours != assembled[index])
			{
				const uint32_t *a = words + (index & ~3u), *b = assembled + (index & ~3u);

				fail("vertex shader %u: instruction %u word %u: ours %08x %08x %08x %08x, cgcomp %08x %08x %08x %08x "
					"(type %u destination %u, %s)", shader, index / 4, index & 3, a[0], a[1], a[2], a[3], b[0], b[1],
					b[2], b[3], instructions[index / 4].destination_type, instructions[index / 4].destination, path);
				break;
			}
		}
	}
	free(assembled);
	if (relative)
		(*relative_shaders)++;

	/* the results against the NV2A's */
	for (trial = 0; trial < 200; trial++)
	{
		static struct vertex_state state;
		float reference[16][4], results[16][4];
		int input, component;
		float window[3], w;

		for (input = 0; input < 16; input++)
		{
			for (component = 0; component < 4; component++)
				state.inputs[input][component] = random_float(-1.0f, 1.0f);
		}
		/* positions in front of the camera, small node indices */
		state.inputs[0][3] = 1.0f;
		vertex_constants(&state);
		unwritten |= nv2a_run(program + 1, count, &state, reference);
		rsx_vertex_run(instructions, instruction_count, &state, results);

		w = results[_rsx_vp_output_position][3];
		if (fabsf(w) < 1.0e-6f || fabsf(reference[0][3]) < 1.0e-3f)
			continue;
		for (component = 0; component < 2; component++)
		{
			float ndc = results[_rsx_vp_output_position][component] / w;

			window[component] = component == 0 ? ndc * TARGET_WIDTH * 0.5f + TARGET_WIDTH * 0.5f :
				ndc * -TARGET_HEIGHT * 0.5f + TARGET_HEIGHT * 0.5f;
		}
		window[2] = (results[_rsx_vp_output_position][2] / w) * 0.5f + 0.5f;
		if (!close_enough(window[0], reference[0][0] + 0.5f, 1.0e-3f) ||
			!close_enough(window[1], reference[0][1] + 0.5f, 1.0e-3f) ||
			!close_enough(window[2], reference[0][2] / DEPTH_SCALE, 1.0e-3f))
		{
			fail("vertex shader %u trial %d: window %g %g %g, NV2A screen %g %g %g (w %g)", shader, trial,
				window[0], window[1], window[2], reference[0][0] + 0.5f, reference[0][1] + 0.5f,
				reference[0][2] / DEPTH_SCALE, w);
			break;
		}
		{
			static const int nv2a_outputs[] = { 3, 4, 5, 9, 10, 11, 12 };
			static const int rsx_outputs[] =
			{
				_rsx_vp_output_color0, _rsx_vp_output_color1, _rsx_vp_output_fog, _rsx_vp_output_texture0,
				_rsx_vp_output_texture0 + 1, _rsx_vp_output_texture0 + 2, _rsx_vp_output_texture0 + 3,
			};
			int output;

			for (output = 0; output < 7; output++)
			{
				for (component = 0; component < (nv2a_outputs[output] == 5 ? 1 : 4); component++)
				{
					if (!close_enough(results[rsx_outputs[output]][component], reference[nv2a_outputs[output]][component],
						1.0e-4f))
					{
						fail("vertex shader %u trial %d: o[%d].%c ours %g, NV2A %g", shader, trial, nv2a_outputs[output],
							components[component], results[rsx_outputs[output]][component],
							reference[nv2a_outputs[output]][component]);
						trial = 1000;
						break;
					}
				}
				if (trial == 1000)
					break;
			}
		}
	}
	if (unwritten)
		(*unwritten_shaders)++;
	if (verbose)
		printf("vertex shader %2u: %3u NV2A instructions, %3u RSX%s%s\n", shader, count, instruction_count,
			relative ? ", relative" : "", unwritten ? ", reads unwritten temporaries" : "");
}

/* ---------- NV2A pixel shaders: the reference (nv2a_psh.c) */

struct fragment_inputs
{
	float color0[4], color1[4], texcoords[4][4], fog;
	float uniforms[_rsx_uniform_count][4];
};

/* the textures: smooth functions of the coordinates, the same for both sides */
static void texture_sample(int unit, int sampler, const float coordinates[4], float out[4])
{
	float u = coordinates[0], v = coordinates[1], w = sampler == _rsx_sampler_2d ? 0.0f : coordinates[2];

	out[0] = 0.5f + 0.5f * sinf(1.3f * u + 0.7f * v + 0.4f * w + (float)unit);
	out[1] = 0.5f + 0.5f * sinf(0.4f * u - 1.1f * v + 0.9f * w + 2.0f * (float)unit);
	out[2] = 0.5f + 0.5f * cosf(0.9f * u + 0.3f * v - 0.6f * w + 3.0f * (float)unit);
	out[3] = 0.5f + 0.5f * cosf(0.5f * u + 1.7f * v + 0.2f * w + 4.0f * (float)unit);
}

static float saturate(float x)
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float signed_byte(float x)
{
	float b = floorf(x * 255.0f + 0.5f);

	return (b >= 128.0f ? b - 256.0f : b) / 127.0f;
}

struct combiner_registers
{
	float v0[4], v1[4], t[4][4], r0[4], r1[4], fog[4], zero[4];
	float v1r0_sum[4], ef_product[4];
	float c0[4], c1[4];
};

static const float *reference_register(struct combiner_registers *r, uint32_t reg, int final)
{
	switch (reg)
	{
	case 1: return r->c0;
	case 2: return r->c1;
	case 3: return r->fog;
	case 4: return r->v0;
	case 5: return r->v1;
	case 8: case 9: case 10: case 11: return r->t[reg - 8];
	case 12: return r->r0;
	case 13: return r->r1;
	case 14: return final ? r->v1r0_sum : r->zero;
	case 15: return final ? r->ef_product : r->zero;
	default: return r->zero;
	}
}

static float reference_channel(const float *value, uint32_t input, int alpha, int component)
{
	int alpha_channel = (input & 0x10) != 0;

	if (alpha)
		return value[alpha_channel ? 3 : 2];
	return alpha_channel ? value[3] : value[component];
}

static float reference_mapping(float x, uint32_t mapping)
{
	switch (mapping)
	{
	case 0x00: return x > 0.0f ? x : 0.0f;
	case 0x20: return 1.0f - saturate(x);
	case 0x40: return 2.0f * (x > 0.0f ? x : 0.0f) - 1.0f;
	case 0x60: return 1.0f - 2.0f * (x > 0.0f ? x : 0.0f);
	case 0x80: return (x > 0.0f ? x : 0.0f) - 0.5f;
	case 0xa0: return 0.5f - (x > 0.0f ? x : 0.0f);
	case 0xc0: return x;
	default: return -x;
	}
}

static float reference_output_mapping(float x, uint32_t flags)
{
	switch (flags & 0x38)
	{
	case 0x08: x = x - 0.5f; break;
	case 0x10: x = x * 2.0f; break;
	case 0x18: x = (x - 0.5f) * 2.0f; break;
	case 0x20: x = x * 4.0f; break;
	case 0x30: x = x * 0.5f; break;
	default: break;
	}
	return x < -1.0f ? -1.0f : x > 1.0f ? 1.0f : x;
}

static float *reference_destination(struct combiner_registers *r, uint32_t reg)
{
	switch (reg)
	{
	case 4: return r->v0;
	case 5: return r->v1;
	case 8: case 9: case 10: case 11: return r->t[reg - 8];
	case 12: return r->r0;
	case 13: return r->r1;
	default: return NULL;
	}
}

static void reference_dot_input(const struct rsx_fragment_key *key, struct combiner_registers *r, int stage, float out[3])
{
	uint32_t mapping = (key->dot_mapping >> ((stage - 1) * 4)) & 7;
	int input = stage == 2 ? (int)((key->input_texture >> 16) & 1) : stage == 3 ? (int)((key->input_texture >> 20) & 3) : 0;
	int component;

	for (component = 0; component < 3; component++)
	{
		float t = r->t[input][component];

		switch (mapping)
		{
		case 0: out[component] = t; break;
		case 1: out[component] = (t * 255.0f - 128.0f) / 127.0f; break;
		case 3: out[component] = signed_byte(t); break;
		default: out[component] = t * 2.0f - 1.0f; break;
		}
	}
}

static float dot3(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* 1 when the fragment is discarded */
static int reference_fragment(const struct rsx_fragment_key *key, const struct fragment_inputs *in, float result[4])
{
	struct combiner_registers r;
	float dots[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	uint32_t combiner_count = key->combiner_count & 0xff;
	int stage, component;

	memset(&r, 0, sizeof(r));
	memcpy(r.v0, in->color0, 16);
	memcpy(r.v1, in->color1, 16);
	if (combiner_count > 8)
		combiner_count = 8;

	for (stage = 0; stage < 4; stage++)
	{
		int mode = (int)((key->texture_modes >> (5 * stage)) & 0x1f);
		const float *xt = in->texcoords[stage];
		float coordinates[4];

		if (key->sampler_type[stage] == _rsx_sampler_none && mode != 4 && mode != 5 && mode != 0x11 && mode != 0x0a)
			mode = 0;
		memset(r.t[stage], 0, 16);
		switch (mode)
		{
		case 1: case 2:
			for (component = 0; component < 3; component++)
				coordinates[component] = xt[component] / xt[3];
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			break;
		case 3:
			texture_sample(stage, key->sampler_type[stage], xt, r.t[stage]);
			break;
		case 4:
			for (component = 0; component < 4; component++)
				r.t[stage][component] = saturate(xt[component]);
			break;
		case 5:
		{
			uint32_t compare = (key->compare_mode >> (4 * stage)) & 0xf;

			for (component = 0; component < 4; component++)
			{
				if ((compare & (1u << component)) ? xt[component] >= 0.0f : xt[component] < 0.0f)
					return 1;
			}
			break;
		}
		case 6: case 7:
		{
			const float *matrix = in->uniforms[_rsx_uniform_bump_matrix + stage];
			const float *luminance = in->uniforms[_rsx_uniform_bump_luminance + stage];
			float d0 = signed_byte(r.t[stage - 1][0]), d1 = signed_byte(r.t[stage - 1][1]);

			coordinates[0] = xt[0] + matrix[0] * d0 + matrix[2] * d1;
			coordinates[1] = xt[1] + matrix[1] * d0 + matrix[3] * d1;
			coordinates[2] = 0.0f;
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			if (mode == 7)
			{
				float scale = saturate(luminance[0] * r.t[stage - 1][2] + luminance[1]);

				for (component = 0; component < 3; component++)
					r.t[stage][component] *= scale;
			}
			break;
		}
		case 0x11: case 0x0a:
		{
			float input[3];

			reference_dot_input(key, &r, stage, input);
			dots[stage] = dot3(xt, input);
			break;
		}
		case 9:
		{
			float input[3];

			reference_dot_input(key, &r, stage, input);
			dots[stage] = dot3(xt, input);
			coordinates[0] = dots[stage - 1];
			coordinates[1] = dots[stage];
			coordinates[2] = 0.0f;
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			break;
		}
		case 0x0b:
		{
			float input[3];

			reference_dot_input(key, &r, stage, input);
			dots[stage] = dot3(xt, input);
			reference_dot_input(key, &r, 3, input);
			dots[3] = dot3(in->texcoords[3], input);
			coordinates[0] = dots[1];
			coordinates[1] = dots[2];
			coordinates[2] = dots[3];
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			break;
		}
		case 0x0c: case 0x12:
		{
			float input[3], n[3], e[3], factor;

			reference_dot_input(key, &r, stage, input);
			dots[stage] = dot3(xt, input);
			n[0] = dots[1]; n[1] = dots[2]; n[2] = dots[3];
			if (mode == 0x0c)
			{
				e[0] = in->texcoords[1][3]; e[1] = in->texcoords[2][3]; e[2] = in->texcoords[3][3];
			}
			else
			{
				memcpy(e, in->uniforms[_rsx_uniform_c0], sizeof(e));
			}
			factor = dot3(n, n);
			factor = 2.0f * dot3(n, e) / (factor > 1.0e-20f ? factor : 1.0e-20f);
			for (component = 0; component < 3; component++)
				coordinates[component] = n[component] * factor - e[component];
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			break;
		}
		case 0x0d: case 0x0e:
		{
			float input[3];

			reference_dot_input(key, &r, stage, input);
			dots[stage] = dot3(xt, input);
			coordinates[0] = dots[1];
			coordinates[1] = dots[2];
			coordinates[2] = dots[3];
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			break;
		}
		case 0x0f: case 0x10:
		{
			int source = stage == 2 ? (int)((key->input_texture >> 16) & 1) :
				stage == 3 ? (int)((key->input_texture >> 20) & 3) : 0;

			coordinates[0] = mode == 0x0f ? r.t[source][3] : r.t[source][1];
			coordinates[1] = mode == 0x0f ? r.t[source][0] : r.t[source][2];
			coordinates[2] = 0.0f;
			coordinates[3] = 1.0f;
			texture_sample(stage, key->sampler_type[stage], coordinates, r.t[stage]);
			break;
		}
		default:
			mode = 0;
			break;
		}
		if (key->color_sign[stage] && mode)
		{
			static const int channels[4] = { 3, 0, 1, 2 };
			int bit;

			for (bit = 0; bit < 4; bit++)
			{
				if (key->color_sign[stage] & (1 << bit))
					r.t[stage][channels[bit]] = signed_byte(r.t[stage][channels[bit]]);
			}
		}
		if (key->alpha_kill[stage] && mode && r.t[stage][3] == 0.0f)
			return 1;
	}

	{
		float factor = 1.0f;
		const float *fog = in->uniforms[_rsx_uniform_fog];

		if (key->fog_enable)
		{
			switch (key->fog_table_mode)
			{
			case 1: factor = expf(-fog[2] * in->fog); break;
			case 2: factor = expf(-(fog[2] * in->fog) * (fog[2] * in->fog)); break;
			case 3: factor = (fog[1] - in->fog) / ((fog[1] - fog[0]) > 1.0e-6f ? (fog[1] - fog[0]) : 1.0e-6f); break;
			default: factor = in->fog; break;
			}
		}
		memcpy(r.fog, in->uniforms[_rsx_uniform_fog_color], 12);
		r.fog[3] = saturate(factor);
	}
	r.r0[3] = r.t[0][3];

	for (stage = 0; stage < (int)combiner_count; stage++)
	{
		int unique_c0 = (key->combiner_count & 0x1000) != 0;
		int unique_c1 = (key->combiner_count & 0x10000) != 0;
		int mux_msb = (key->combiner_count & 0x100) != 0;
		float ab[2][4], cd[2][4], sum[2][4];
		int portion;

		memcpy(r.c0, in->uniforms[_rsx_uniform_c0 + (unique_c0 ? stage : 0)], 16);
		memcpy(r.c1, in->uniforms[_rsx_uniform_c1 + (unique_c1 ? stage : 0)], 16);
		for (portion = 0; portion < 2; portion++)
		{
			int alpha = portion == 1;
			uint32_t inputs = alpha ? key->alpha_inputs[stage] : key->rgb_inputs[stage];
			uint32_t flags = (alpha ? key->alpha_outputs[stage] : key->rgb_outputs[stage]) >> 12;
			float values[4][3];
			int which, count = alpha ? 1 : 3;
			int selector;

			for (which = 0; which < 4; which++)
			{
				uint32_t input = (inputs >> (24 - 8 * which)) & 0xff;
				const float *value = reference_register(&r, input & 0x0f, 0);

				for (component = 0; component < count; component++)
					values[which][component] = reference_mapping(reference_channel(value, input, alpha, component), input & 0xe0);
			}
			if (!alpha && (flags & 0x02))
				ab[0][0] = ab[0][1] = ab[0][2] = dot3(values[0], values[1]);
			else
				for (component = 0; component < count; component++)
					ab[portion][component] = values[0][component] * values[1][component];
			if (!alpha && (flags & 0x01))
				cd[0][0] = cd[0][1] = cd[0][2] = dot3(values[2], values[3]);
			else
				for (component = 0; component < count; component++)
					cd[portion][component] = values[2][component] * values[3][component];
			selector = mux_msb ? r.r0[3] >= 0.5f : ((int)(r.r0[3] * 255.0f + 0.5f) & 1) != 0;
			for (component = 0; component < count; component++)
			{
				if (flags & 0x04)
					sum[portion][component] = selector ? cd[portion][component] : ab[portion][component];
				else
					sum[portion][component] = ab[portion][component] + cd[portion][component];
				ab[portion][component] = reference_output_mapping(ab[portion][component], flags);
				cd[portion][component] = reference_output_mapping(cd[portion][component], flags);
				sum[portion][component] = reference_output_mapping(sum[portion][component], flags);
			}
		}
		for (portion = 0; portion < 2; portion++)
		{
			int alpha = portion == 1;
			uint32_t outputs = alpha ? key->alpha_outputs[stage] : key->rgb_outputs[stage];
			uint32_t flags = outputs >> 12;
			float *ab_destination = reference_destination(&r, (outputs >> 4) & 0x0f);
			float *cd_destination = reference_destination(&r, outputs & 0x0f);
			float *sum_destination = reference_destination(&r, (outputs >> 8) & 0x0f);

			if (alpha)
			{
				if (ab_destination) ab_destination[3] = ab[1][0];
				if (cd_destination) cd_destination[3] = cd[1][0];
				if (sum_destination) sum_destination[3] = sum[1][0];
				continue;
			}
			if (ab_destination)
			{
				memcpy(ab_destination, ab[0], 12);
				if (flags & 0x80)
					ab_destination[3] = ab[0][2];
			}
			if (cd_destination)
			{
				memcpy(cd_destination, cd[0], 12);
				if (flags & 0x40)
					cd_destination[3] = cd[0][2];
			}
			if (sum_destination)
				memcpy(sum_destination, sum[0], 12);
		}
	}

	if (!key->final_inputs_abcd && !key->final_inputs_efg)
	{
		memcpy(result, r.r0, 16);
	}
	else
	{
		uint32_t efg = key->final_inputs_efg, abcd = key->final_inputs_abcd, settings = efg & 0xff;
		float values[6][3], g;
		int which;

		memcpy(r.c0, in->uniforms[_rsx_uniform_final_c0], 16);
		memcpy(r.c1, in->uniforms[_rsx_uniform_final_c1], 16);
		for (which = 0; which < 2; which++)
		{
			uint32_t input = (efg >> (24 - 8 * which)) & 0xff;
			const float *value = reference_register(&r, input & 0x0f, 1);

			for (component = 0; component < 3; component++)
			{
				float x = saturate(reference_channel(value, input, 0, component));

				values[4 + which][component] = (input & 0x20) ? 1.0f - x : x;
			}
		}
		for (component = 0; component < 3; component++)
		{
			float v1 = saturate(r.v1[component]), r0 = saturate(r.r0[component]);

			r.ef_product[component] = values[4][component] * values[5][component];
			r.v1r0_sum[component] = ((settings & 0x40) ? 1.0f - v1 : v1) + ((settings & 0x20) ? 1.0f - r0 : r0);
			if (settings & 0x80)
				r.v1r0_sum[component] = saturate(r.v1r0_sum[component]);
		}
		for (which = 0; which < 4; which++)
		{
			uint32_t input = (abcd >> (24 - 8 * which)) & 0xff;
			const float *value = reference_register(&r, input & 0x0f, 1);

			for (component = 0; component < 3; component++)
			{
				float x = saturate(reference_channel(value, input, 0, component));

				values[which][component] = (input & 0x20) ? 1.0f - x : x;
			}
		}
		{
			uint32_t input = (efg >> 8) & 0xff;
			float x = saturate(reference_channel(reference_register(&r, input & 0x0f, 1), input, 1, 0));

			g = (input & 0x20) ? 1.0f - x : x;
		}
		for (component = 0; component < 3; component++)
			result[component] = values[0][component] * values[1][component] +
				(1.0f - values[0][component]) * values[2][component] + values[3][component];
		result[3] = g;
	}
	for (component = 0; component < 4; component++)
		result[component] = saturate(result[component]);
	return 0;
}

/* ---------- RSX fragment programs: an interpreter of the instructions */

static int rsx_fragment_run(const struct rsx_instruction *instructions, uint32_t count,
	const struct rsx_fragment_key *key, const struct fragment_inputs *in, float result[4])
{
	float registers[64][4], condition[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	uint32_t index;
	int component;

	for (index = 0; index < 64; index++)
		registers[index][0] = registers[index][1] = registers[index][2] = registers[index][3] = 7.0e6f;
	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];
		float sources[3][4], value[4];
		int position;

		for (position = 0; position < 3; position++)
		{
			const struct rsx_source *source = &instruction->source[position];
			float raw[4];

			memset(raw, 0, sizeof(raw));
			switch (source->type)
			{
			case _rsx_register_temporary: memcpy(raw, registers[source->index & 63], 16); break;
			case _rsx_register_input:
				if (source->index == _rsx_fp_input_color0) memcpy(raw, in->color0, 16);
				else if (source->index == _rsx_fp_input_color1) memcpy(raw, in->color1, 16);
				else if (source->index == _rsx_fp_input_fog) raw[0] = raw[1] = raw[2] = raw[3] = in->fog;
				else memcpy(raw, in->texcoords[(source->index - _rsx_fp_input_texture0) & 3], 16);
				break;
			case _rsx_register_constant:
				if (instruction->uniform != RSX_NO_UNIFORM)
					memcpy(raw, in->uniforms[instruction->uniform], 16);
				else
					memcpy(raw, instruction->constant, 16);
				break;
			default: break;
			}
			for (component = 0; component < 4; component++)
			{
				float x = raw[source->swizzle[component]];

				if (source->absolute) x = fabsf(x);
				if (source->negate) x = -x;
				sources[position][component] = x;
			}
		}
		switch (instruction->opcode)
		{
		case _rsx_fp_mov: memcpy(value, sources[0], 16); break;
		case _rsx_fp_mul: for (component = 0; component < 4; component++) value[component] = sources[0][component] * sources[1][component]; break;
		case _rsx_fp_add: for (component = 0; component < 4; component++) value[component] = sources[0][component] + sources[1][component]; break;
		case _rsx_fp_mad: for (component = 0; component < 4; component++) value[component] = sources[0][component] * sources[1][component] + sources[2][component]; break;
		case _rsx_fp_dp3: value[0] = value[1] = value[2] = value[3] = dot3(sources[0], sources[1]); break;
		case _rsx_fp_dp4: value[0] = value[1] = value[2] = value[3] = dot3(sources[0], sources[1]) + sources[0][3] * sources[1][3]; break;
		case _rsx_fp_min: for (component = 0; component < 4; component++) value[component] = fminf(sources[0][component], sources[1][component]); break;
		case _rsx_fp_max: for (component = 0; component < 4; component++) value[component] = fmaxf(sources[0][component], sources[1][component]); break;
		case _rsx_fp_slt: for (component = 0; component < 4; component++) value[component] = sources[0][component] < sources[1][component]; break;
		case _rsx_fp_sge: for (component = 0; component < 4; component++) value[component] = sources[0][component] >= sources[1][component]; break;
		case _rsx_fp_seq: for (component = 0; component < 4; component++) value[component] = sources[0][component] == sources[1][component]; break;
		case _rsx_fp_frc: for (component = 0; component < 4; component++) value[component] = sources[0][component] - floorf(sources[0][component]); break;
		case _rsx_fp_flr: for (component = 0; component < 4; component++) value[component] = floorf(sources[0][component]); break;
		case _rsx_fp_rcp: value[0] = value[1] = value[2] = value[3] = 1.0f / sources[0][0]; break;
		case _rsx_fp_ex2: value[0] = value[1] = value[2] = value[3] = exp2f(sources[0][0]); break;
		case _rsx_fp_lg2: value[0] = value[1] = value[2] = value[3] = log2f(sources[0][0]); break;
		case _rsx_fp_tex: texture_sample(instruction->texture_unit, key->sampler_type[instruction->texture_unit & 3], sources[0], value); break;
		case _rsx_fp_txp:
		{
			float coordinates[4];

			for (component = 0; component < 3; component++)
				coordinates[component] = sources[0][component] / sources[0][3];
			coordinates[3] = 1.0f;
			texture_sample(instruction->texture_unit, key->sampler_type[instruction->texture_unit & 3], coordinates, value);
			break;
		}
		case _rsx_fp_kil:
			for (component = 0; component < 4; component++)
			{
				float c = condition[instruction->condition_swizzle[component]];
				int pass;

				switch (instruction->condition)
				{
				case _rsx_condition_less: pass = c < 0.0f; break;
				case _rsx_condition_equal: pass = c == 0.0f; break;
				case _rsx_condition_less_equal: pass = c <= 0.0f; break;
				case _rsx_condition_greater: pass = c > 0.0f; break;
				case _rsx_condition_not_equal: pass = c != 0.0f; break;
				case _rsx_condition_greater_equal: pass = c >= 0.0f; break;
				case _rsx_condition_always: pass = 1; break;
				default: pass = 0; break;
				}
				if (pass)
					return 1;
			}
			continue;
		default: memset(value, 0, sizeof(value)); break;
		}
		if (instruction->saturate)
		{
			for (component = 0; component < 4; component++)
				value[component] = saturate(value[component]);
		}
		for (component = 0; component < 4; component++)
		{
			if (!(instruction->mask & (1 << component)))
				continue;
			if (instruction->condition_update)
				condition[component] = value[component];
			if (instruction->destination_type != _rsx_register_none)
				registers[instruction->destination & 63][component] = value[component];
		}
	}
	memcpy(result, registers[0], 16);
	return 0;
}

/* ---------- fragment programs: the check */

static uint32_t random_input_byte(int final)
{
	static const uint32_t registers[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15 };
	uint32_t reg = registers[random_below(final ? 14 : 14)];

	return reg | (random_below(2) ? 0x10 : 0) | (final ? (random_below(2) ? 0x20 : 0) : (random_below(8) << 5));
}

static uint32_t random_output_register(void)
{
	static const uint32_t registers[] = { 0, 0, 4, 5, 8, 9, 10, 11, 12, 12, 13 };

	return registers[random_below(11)];
}

static void random_key(struct rsx_fragment_key *key)
{
	uint32_t stage, count = 1 + random_below(8);
	static const uint8_t modes[] = { 0, 1, 1, 1, 2, 3, 4, 5, 6, 7, 9, 0x0a, 0x0f, 0x10, 0x11 };

	memset(key, 0, sizeof(*key));
	key->combiner_count = count | (random_below(2) ? 0x100 : 0) | (random_below(2) ? 0x1000 : 0) |
		(random_below(2) ? 0x10000 : 0);
	for (stage = 0; stage < count; stage++)
	{
		key->rgb_inputs[stage] = (random_input_byte(0) << 24) | (random_input_byte(0) << 16) |
			(random_input_byte(0) << 8) | random_input_byte(0);
		key->alpha_inputs[stage] = (random_input_byte(0) << 24) | (random_input_byte(0) << 16) |
			(random_input_byte(0) << 8) | random_input_byte(0);
		key->rgb_outputs[stage] = (random_below(256) << 12) | (random_output_register() << 8) |
			(random_output_register() << 4) | random_output_register();
		/* dot products and blue to alpha are rgb only */
		key->alpha_outputs[stage] = ((random_below(256) & 0x3c) << 12) | (random_output_register() << 8) |
			(random_output_register() << 4) | random_output_register();
	}
	if (random_below(4))
	{
		uint32_t which;

		for (which = 0; which < 4; which++)
		{
			uint32_t input;

			do
				input = random_input_byte(1);
			while ((input & 0x0f) == 15 && which < 2);
			key->final_inputs_abcd |= input << (24 - 8 * which);
		}
		for (which = 0; which < 3; which++)
		{
			uint32_t input;

			/* E and F cannot read the registers made from them */
			do
				input = random_input_byte(1);
			while (which < 2 && (input & 0x0f) >= 14);
			key->final_inputs_efg |= input << (24 - 8 * which);
		}
		key->final_inputs_efg |= random_below(8) << 5;
	}
	for (stage = 0; stage < 4; stage++)
	{
		uint32_t mode = modes[random_below(sizeof(modes))];

		if (stage == 0 && mode >= 6)
			mode = 1;
		if ((mode == 0x0f || mode == 0x10) && stage < 1)
			mode = 1;
		key->texture_modes |= mode << (5 * stage);
		key->sampler_type[stage] = mode == 3 ? _rsx_sampler_cube : mode == 2 ? _rsx_sampler_3d :
			random_below(8) ? _rsx_sampler_2d : _rsx_sampler_none;
		key->alpha_kill[stage] = random_below(6) == 0;
		key->color_sign[stage] = random_below(4) == 0 ? (uint8_t)random_below(16) : 0;
	}
	/* the reflection and 3D lookups read the dot products of stages 1 to 3 */
	switch (random_below(6))
	{
	case 0:
	case 1:
	{
		static const uint32_t last[] = { 0x0c, 0x12, 0x0d, 0x0e };
		uint32_t mode = last[random_below(4)];

		key->texture_modes &= 0x1f;
		key->texture_modes |= (0x11u << 5) | ((mode == 0x0c || mode == 0x12 ? 0x0bu : 0x11u) << 10) | (mode << 15);
		key->sampler_type[1] = _rsx_sampler_none;
		key->sampler_type[2] = mode == 0x0c || mode == 0x12 ? _rsx_sampler_cube : _rsx_sampler_none;
		key->sampler_type[3] = mode == 0x0d ? _rsx_sampler_3d : _rsx_sampler_cube;
		break;
	}
	default:
		break;
	}
	key->dot_mapping = random_next() & 0x777;
	key->input_texture = (random_below(2) << 16) | (random_below(3) << 20);
	key->compare_mode = random_next() & 0xffff;
	key->fog_enable = (uint8_t)random_below(2);
	key->fog_table_mode = (uint8_t)random_below(4);
}

static void check_fragment_key(const struct rsx_fragment_key *key, uint32_t number, uint32_t *instruction_total)
{
	static struct rsx_instruction instructions[RSX_FP_MAXIMUM_INSTRUCTIONS];
	static uint32_t words[RSX_FP_MAXIMUM_INSTRUCTIONS * 8];
	uint32_t count = 0, word_count, assembled_count = 0, index;
	uint32_t *assembled;
	char path[512];
	FILE *file;
	int trial;

	if (rsx_fragment_program_instructions(key, instructions, RSX_FP_MAXIMUM_INSTRUCTIONS, &count) < 0)
	{
		fail("fragment key %u: translation failed", number);
		return;
	}
	*instruction_total += count;
	word_count = rsx_fragment_encode(instructions, count, words);
	snprintf(path, sizeof(path), "%s/fp%04u.fp", work, number);
	file = fopen(path, "w");
	/* the key, as comments cgcomp skips */
	fprintf(file, "# count %08x modes %05x dot %03x input %06x compare %04x final %08x %08x fog %u %u\n",
		key->combiner_count, key->texture_modes, key->dot_mapping, key->input_texture, key->compare_mode,
		key->final_inputs_abcd, key->final_inputs_efg, key->fog_enable, key->fog_table_mode);
	for (index = 0; index < 8; index++)
		fprintf(file, "# stage %u rgb %08x %08x alpha %08x %08x\n", index, key->rgb_inputs[index],
			key->rgb_outputs[index], key->alpha_inputs[index], key->alpha_outputs[index]);
	fprintf(file, "# samplers %u %u %u %u kill %u %u %u %u sign %x %x %x %x\n", key->sampler_type[0],
		key->sampler_type[1], key->sampler_type[2], key->sampler_type[3], key->alpha_kill[0], key->alpha_kill[1],
		key->alpha_kill[2], key->alpha_kill[3], key->color_sign[0], key->color_sign[1], key->color_sign[2],
		key->color_sign[3]);
	print_fragment(file, instructions, count);
	fclose(file);
	assembled = cgcomp_assemble(1, path, &assembled_count);
	if (!assembled || assembled_count != word_count)
	{
		fail("fragment key %u: cgcomp gave %u words, ours %u (%s)", number, assembled_count, word_count, path);
	}
	else
	{
		for (index = 0; index < word_count; index++)
		{
			if (words[index] != assembled[index])
			{
				fail("fragment key %u: word %u: ours %08x, cgcomp %08x (%s)", number, index, words[index],
					assembled[index], path);
				break;
			}
		}
	}
	free(assembled);

	/* the RSX's registers start undefined: no read before a write */
	{
		uint8_t written[64];

		memset(written, 0, sizeof(written));
		for (index = 0; index < count; index++)
		{
			const struct rsx_instruction *instruction = &instructions[index];
			int position, component, sources = instruction->opcode == _rsx_fp_mad ? 3 :
				instruction->opcode == _rsx_fp_mov || instruction->opcode == _rsx_fp_frc ||
				instruction->opcode == _rsx_fp_flr || instruction->opcode == _rsx_fp_tex ||
				instruction->opcode == _rsx_fp_txp || instruction->opcode == _rsx_fp_rcp ||
				instruction->opcode == _rsx_fp_ex2 || instruction->opcode == _rsx_fp_lg2 ? 1 :
				instruction->opcode == _rsx_fp_kil ? 0 : 2;

			for (position = 0; position < sources; position++)
			{
				const struct rsx_source *source = &instruction->source[position];
				int used = instruction->opcode == _rsx_fp_dp3 ? 7 :
					instruction->opcode == _rsx_fp_rcp || instruction->opcode == _rsx_fp_ex2 ||
					instruction->opcode == _rsx_fp_lg2 ? 1 : instruction->mask;

				if (source->type != _rsx_register_temporary)
					continue;
				for (component = 0; component < 4; component++)
				{
					if ((used & (1 << component)) && !(written[source->index] & (1 << source->swizzle[component])))
					{
						fail("fragment key %u: instruction %u reads R%u.%c unwritten (%s)", number, index,
							source->index, components[source->swizzle[component]], path);
						return;
					}
				}
			}
			if (instruction->destination_type != _rsx_register_none)
				written[instruction->destination] |= instruction->mask;
		}
	}

	for (trial = 0; trial < 100; trial++)
	{
		struct fragment_inputs in;
		float reference[4], result[4];
		int reference_killed, killed, component, uniform;

		for (component = 0; component < 4; component++)
		{
			int stage;

			in.color0[component] = random_float(0.0f, 1.0f);
			in.color1[component] = random_float(0.0f, 1.0f);
			for (stage = 0; stage < 4; stage++)
				in.texcoords[stage][component] = random_float(-1.0f, 1.0f);
		}
		for (component = 0; component < 4; component++)
			in.texcoords[component][3] = random_float(0.5f, 1.5f);
		in.fog = random_float(0.0f, 1.0f);
		for (uniform = 0; uniform < _rsx_uniform_count; uniform++)
		{
			for (component = 0; component < 4; component++)
				in.uniforms[uniform][component] = random_float(0.0f, 1.0f);
		}
		in.uniforms[_rsx_uniform_fog][1] = in.uniforms[_rsx_uniform_fog][0] + random_float(0.1f, 1.0f);
		reference_killed = reference_fragment(key, &in, reference);
		killed = rsx_fragment_run(instructions, count, key, &in, result);
		if (killed != reference_killed)
		{
			fail("fragment key %u trial %d: killed %d, NV2A %d (%s)", number, trial, killed, reference_killed, path);
			return;
		}
		if (killed)
			continue;
		for (component = 0; component < 4; component++)
		{
			if (!close_enough(result[component], reference[component], 2.0e-3f))
			{
				fail("fragment key %u trial %d: %c ours %g, NV2A %g (%s)", number, trial, components[component],
					result[component], reference[component], path);
				return;
			}
		}
	}
}

/* ---------- main */

int main(int argc, char *argv[])
{
	const char *source_root = argc > 1 ? argv[1] : ".";
	uint32_t shader, unwritten = 0, relative = 0, number, fragment_keys = 300, instruction_total = 0;

	if (argc > 2)
		work = argv[2];
	if (getenv("CGCOMP"))
		cgcomp = getenv("CGCOMP");
	if (getenv("KEYS"))
		fragment_keys = (uint32_t)atoi(getenv("KEYS"));
	if (getenv("SEED"))
		random_state = (uint32_t)strtoul(getenv("SEED"), NULL, 0) | 1;
	verbose = getenv("VERBOSE") != NULL;

	if (!load_vertex_shaders(source_root))
	{
		printf("cannot read the vertex shaders under %s\n", source_root);
		return 2;
	}
	for (shader = 0; shader < shader_count; shader++)
		check_vertex_shader(shader, &unwritten, &relative);
	printf("vertex shaders: %u checked, %u with relative constants, %u read unwritten temporaries\n", shader_count,
		relative, unwritten);

	for (number = 0; number < fragment_keys; number++)
	{
		struct rsx_fragment_key key;

		random_key(&key);
		check_fragment_key(&key, number, &instruction_total);
	}
	printf("fragment programs: %u random keys, %u instructions on average\n", fragment_keys,
		instruction_total / fragment_keys);
	printf("%lu failures\n", failures);
	return failures ? 1 : 0;
}
