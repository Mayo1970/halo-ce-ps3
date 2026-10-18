/* RSX program microcode from instructions, field for field as cgcomp's compilervp.cpp and
compilerfp.cpp (PSL1GHT tools/cgcomp) encode them; tools/ps3_rsx_shader_check.sh compares. */

#include "ps3_rsx_program.h"

#include <string.h>

/* ---------- vertex programs */

/* the vertex program's masks have x in bit 3 */
static uint32_t vertex_mask(uint32_t mask)
{
	return ((mask & 1) << 3) | ((mask & 2) << 1) | ((mask & 4) >> 1) | ((mask & 8) >> 3);
}

static void vertex_source(const struct rsx_source *source, int position, uint32_t words[4])
{
	uint32_t bits;

	switch (source->type)
	{
	case _rsx_register_temporary:
		bits = 1 | ((uint32_t)source->index << 2);
		break;
	case _rsx_register_input:
		bits = 2;
		words[1] |= (uint32_t)(source->index & 0xf) << 8;
		break;
	case _rsx_register_constant:
		bits = 3;
		words[1] |= (uint32_t)(source->index & 0x3ff) << 12;
		/* c[A0.x + index] (nouveau's nv40 vertprog encoding: cgcomp drops it) */
		if (source->relative)
			words[3] |= 1 << 1;
		break;
	default:
		bits = 2;
		break;
	}
	if (source->negate)
		bits |= 1 << 16;
	if (source->absolute)
		words[0] |= 1u << (21 + position);
	bits |= ((uint32_t)source->swizzle[0] << 14) | ((uint32_t)source->swizzle[1] << 12) |
		((uint32_t)source->swizzle[2] << 10) | ((uint32_t)source->swizzle[3] << 8);
	switch (position)
	{
	case 0:
		words[1] |= (bits >> 9) & 0xff;
		words[2] |= (bits & 0x1ff) << 23;
		break;
	case 1:
		words[2] |= (bits & 0x1ffff) << 6;
		break;
	default:
		words[2] |= (bits >> 11) & 0x3f;
		words[3] |= (bits & 0x7ff) << 21;
		break;
	}
}

void rsx_vertex_encode(const struct rsx_instruction *instruction, int last, uint32_t words[4])
{
	int scalar = (instruction->opcode & _rsx_vp_scalar) != 0;
	uint32_t opcode = instruction->opcode & 0x1f;
	uint32_t mask = vertex_mask(instruction->mask);
	int position;

	/* no condition test: always, identity condition swizzle */
	words[0] = (_rsx_condition_always << 10) | (0x1b << 2);
	words[1] = 0;
	words[2] = 0;
	words[3] = 0;
	if (instruction->saturate)
		words[0] |= 1 << 26;

	switch (instruction->destination_type)
	{
	case _rsx_register_temporary:
	case _rsx_register_address:
		words[3] |= 0x1f << 2;
		if (scalar)
			words[3] |= (uint32_t)instruction->destination << 7;
		else
			words[0] |= (uint32_t)instruction->destination << 15;
		break;
	case _rsx_register_output:
		words[3] |= (uint32_t)instruction->destination << 2;
		if (scalar)
			words[3] |= (1 << 12) | (0x3f << 7);
		else
			words[0] |= (1u << 30) | (0x3f << 15);
		break;
	default:
		words[3] |= 0x1f << 2;
		if (scalar)
			words[3] |= 0x3f << 7;
		else
			words[0] |= 0x3f << 15;
		break;
	}

	for (position = 0; position < 3; position++)
		vertex_source(&instruction->source[position], position, words);

	if (scalar)
	{
		words[1] |= opcode << 27;
		words[0] |= 0x3f << 15;
		words[3] |= mask << 17;
	}
	else
	{
		words[1] |= opcode << 22;
		words[3] |= 0x3f << 7;
		words[3] |= mask << 13;
	}
	if (last)
		words[3] |= 1;
}

/* ---------- fragment programs */

int rsx_fragment_reads_constant(const struct rsx_instruction *instruction)
{
	int position;

	for (position = 0; position < 3; position++)
	{
		if (instruction->source[position].type == _rsx_register_constant)
			return 1;
	}
	return 0;
}

/* cgcomp's IsPCDisablingInstruction: these read a texture coordinate without perspective correction */
static int disables_perspective(const struct rsx_instruction *instruction)
{
	switch (instruction->opcode)
	{
	case _rsx_fp_dp3:
	case _rsx_fp_dp4:
	case _rsx_fp_mul:
		return 1;
	default:
		return 0;
	}
}

static void fragment_source(const struct rsx_instruction *instruction, int position, uint32_t words[4])
{
	const struct rsx_source *source = &instruction->source[position];
	uint32_t bits = 0;

	switch (source->type)
	{
	case _rsx_register_input:
		bits = 1;
		words[0] |= (uint32_t)(source->index & 0xf) << 13;
		if (source->index >= _rsx_fp_input_texture0 && source->index <= _rsx_fp_input_texture0 + 7)
			words[3] |= ((uint32_t)disables_perspective(instruction) << 31) | (0x7fcu << 19);
		break;
	case _rsx_register_temporary:
		bits = (uint32_t)(source->index & 0x3f) << 2;
		/* cgcomp's NVFX_FP_REG_SRC_HALF */
		if (source->half)
			bits |= 1 << 8;
		break;
	case _rsx_register_constant:
		bits = 2;
		break;
	default:
		break;
	}
	if (source->negate)
		bits |= 1 << 17;
	if (source->absolute)
		bits |= position == 0 ? 1u << 29 : 1u << 18;
	bits |= ((uint32_t)source->swizzle[0] << 9) | ((uint32_t)source->swizzle[1] << 11) |
		((uint32_t)source->swizzle[2] << 13) | ((uint32_t)source->swizzle[3] << 15);
	words[position + 1] |= bits;
}

uint32_t rsx_fragment_encode(const struct rsx_instruction *instructions, uint32_t count, uint32_t *words)
{
	uint32_t index, word_count = 0, last_word = 0;

	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];
		uint32_t *hw = words + word_count;
		int position;

		last_word = word_count;
		hw[0] = ((uint32_t)instruction->opcode << 24) | ((uint32_t)(instruction->mask & 0xf) << 9);
		hw[1] = ((uint32_t)instruction->condition << 18) | ((uint32_t)instruction->condition_swizzle[0] << 21) |
			((uint32_t)instruction->condition_swizzle[1] << 23) | ((uint32_t)instruction->condition_swizzle[2] << 25) |
			((uint32_t)instruction->condition_swizzle[3] << 27);
		hw[2] = 0;
		hw[3] = 0;
		if (instruction->saturate)
			hw[0] |= 1u << 31;
		if (instruction->condition_update)
			hw[0] |= 1 << 8;
		if (instruction->destination_type == _rsx_register_none)
			hw[0] |= (1u << 30) | (0x3f << 1);
		else
			hw[0] |= (uint32_t)(instruction->destination & 0x3f) << 1;
		/* cgcomp's NVFX_FP_OP_OUT_REG_HALF: an H register (the ALU stays 32-bit: precision 0) */
		if (instruction->destination_type != _rsx_register_none && instruction->destination_half)
			hw[0] |= 1 << 7;
		if (instruction->opcode == _rsx_fp_tex || instruction->opcode == _rsx_fp_txp)
			hw[0] |= (uint32_t)(instruction->texture_unit & 0xf) << 17;
		for (position = 0; position < 3; position++)
			fragment_source(instruction, position, hw);
		word_count += 4;
		if (rsx_fragment_reads_constant(instruction))
		{
			memcpy(words + word_count, instruction->constant, 4 * sizeof(uint32_t));
			word_count += 4;
		}
	}
	if (word_count)
		words[last_word] |= 1;
	return word_count;
}

/* the RSX reads each fragment program word with its 16-bit halves swapped */
void rsx_fragment_program_set_uniform(uint32_t *microcode, uint32_t word, const float value[4])
{
	int component;

	for (component = 0; component < 4; component++)
	{
		uint32_t bits;

		memcpy(&bits, &value[component], sizeof(bits));
		microcode[word + component] = (bits << 16) | (bits >> 16);
	}
}
