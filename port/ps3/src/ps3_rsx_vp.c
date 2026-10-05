/* NV2A vertex programs as RSX vertex programs. The RSX's vertex unit descends from the NV2A's: the
same opcodes, registers and operand fields (port/linux/src/nv2a_vsh.c decodes them). */

#include "ps3_rsx_program.h"

#include <stdlib.h>
#include <string.h>

/* RSX temporaries past the NV2A's r0-r11: r12 (the position), scratch, the kept clip position */
enum
{
	_temporary_position = 12,
	_temporary_mac = 13,
	_temporary_ilu = 14,
	_temporary_clip = 15,
	_temporary_epilogue = 16,
	_temporary_epilogue2 = 17,
	_temporary_address = 18,
	_temporary_count = 19,
};

enum
{
	_mac_nop, _mac_mov, _mac_mul, _mac_add, _mac_mad, _mac_dp3, _mac_dph, _mac_dp4,
	_mac_dst, _mac_min, _mac_max, _mac_slt, _mac_sge, _mac_arl,
};

enum
{
	_ilu_nop, _ilu_mov, _ilu_rcp, _ilu_rcc, _ilu_rsq, _ilu_exp, _ilu_log, _ilu_lit,
};

enum
{
	_mux_unknown, _mux_temporary, _mux_input, _mux_constant,
};

struct context
{
	struct rsx_instruction *instructions;
	uint32_t count;
	uint32_t maximum;
	int overflow;
	/* the components each output (o[]) receives */
	uint8_t written[16];
};

static uint32_t field(const uint32_t *instruction, int word, int low_bit, int bit_count)
{
	return (instruction[word] >> low_bit) & ((1u << bit_count) - 1);
}

/* the NV2A's masks have x in bit 3 */
static uint8_t nv2a_mask(uint32_t mask)
{
	return (uint8_t)(((mask & 8) >> 3) | ((mask & 4) >> 1) | ((mask & 2) << 1) | ((mask & 1) << 3));
}

static struct rsx_source source_none(void)
{
	struct rsx_source source;

	memset(&source, 0, sizeof(source));
	source.type = _rsx_register_none;
	source.swizzle[0] = 0;
	source.swizzle[1] = 1;
	source.swizzle[2] = 2;
	source.swizzle[3] = 3;
	return source;
}

static struct rsx_source source_register(int type, int index)
{
	struct rsx_source source = source_none();

	source.type = (uint8_t)type;
	source.index = (uint16_t)index;
	return source;
}

static struct rsx_source source_swizzled(struct rsx_source source, int x, int y, int z, int w)
{
	source.swizzle[0] = (uint8_t)x;
	source.swizzle[1] = (uint8_t)y;
	source.swizzle[2] = (uint8_t)z;
	source.swizzle[3] = (uint8_t)w;
	return source;
}

static void emit(struct context *context, int opcode, int destination_type, int destination, int mask,
	int saturate, struct rsx_source a, struct rsx_source b, struct rsx_source c)
{
	struct rsx_instruction *instruction;

	if (context->count >= context->maximum)
	{
		context->overflow = 1;
		return;
	}
	instruction = &context->instructions[context->count++];
	memset(instruction, 0, sizeof(*instruction));
	instruction->opcode = (uint8_t)opcode;
	instruction->destination_type = (uint8_t)destination_type;
	instruction->destination = (uint8_t)destination;
	instruction->mask = (uint8_t)mask;
	instruction->saturate = (uint8_t)saturate;
	instruction->condition = _rsx_condition_always;
	instruction->condition_swizzle[1] = 1;
	instruction->condition_swizzle[2] = 2;
	instruction->condition_swizzle[3] = 3;
	instruction->uniform = RSX_NO_UNIFORM;
	instruction->source[0] = a;
	instruction->source[1] = b;
	instruction->source[2] = c;
}

static void emit_move(struct context *context, int destination_type, int destination, int mask, int saturate,
	struct rsx_source source)
{
	emit(context, _rsx_vp_mov, destination_type, destination, mask, saturate, source, source_none(), source_none());
}

/* ---------- operands */

static struct rsx_source operand(const uint32_t *instruction, char which, int relative)
{
	uint32_t negate, swizzle[4], index, mux;
	struct rsx_source source;

	switch (which)
	{
	case 'A':
		negate = field(instruction, 1, 8, 1);
		swizzle[0] = field(instruction, 1, 6, 2);
		swizzle[1] = field(instruction, 1, 4, 2);
		swizzle[2] = field(instruction, 1, 2, 2);
		swizzle[3] = field(instruction, 1, 0, 2);
		index = field(instruction, 2, 28, 4);
		mux = field(instruction, 2, 26, 2);
		break;
	case 'B':
		negate = field(instruction, 2, 25, 1);
		swizzle[0] = field(instruction, 2, 23, 2);
		swizzle[1] = field(instruction, 2, 21, 2);
		swizzle[2] = field(instruction, 2, 19, 2);
		swizzle[3] = field(instruction, 2, 17, 2);
		index = field(instruction, 2, 13, 4);
		mux = field(instruction, 2, 11, 2);
		break;
	default:
		negate = field(instruction, 2, 10, 1);
		swizzle[0] = field(instruction, 2, 8, 2);
		swizzle[1] = field(instruction, 2, 6, 2);
		swizzle[2] = field(instruction, 2, 4, 2);
		swizzle[3] = field(instruction, 2, 2, 2);
		index = (field(instruction, 2, 0, 2) << 2) | field(instruction, 3, 30, 2);
		mux = field(instruction, 3, 28, 2);
		break;
	}
	switch (mux)
	{
	case _mux_temporary:
		/* r12 reads back the position output */
		source = source_register(_rsx_register_temporary, index == 12 ? _temporary_position : (int)index);
		break;
	case _mux_input:
		source = source_register(_rsx_register_input, (int)field(instruction, 1, 9, 4));
		break;
	case _mux_constant:
		source = source_register(_rsx_register_constant, (int)field(instruction, 1, 13, 8));
		source.relative = (uint8_t)relative;
		break;
	default:
		source = source_register(_rsx_register_constant, _rsx_vp_constant_zero);
		break;
	}
	source.negate = (uint8_t)negate;
	source.swizzle[0] = (uint8_t)swizzle[0];
	source.swizzle[1] = (uint8_t)swizzle[1];
	source.swizzle[2] = (uint8_t)swizzle[2];
	source.swizzle[3] = (uint8_t)swizzle[3];
	return source;
}

static uint32_t temporary_bit(const struct rsx_source *source)
{
	return source->type == _rsx_register_temporary ? 1u << source->index : 0;
}

/* NV2A output addresses (o[]) as RSX outputs; -1 for the unused ones */
static int output_register(uint32_t address)
{
	switch (address)
	{
	case 0: return _rsx_vp_output_position;
	case 3: return _rsx_vp_output_color0;
	case 4: return _rsx_vp_output_color1;
	case 5: return _rsx_vp_output_fog;
	case 6: return _rsx_vp_output_point_size;
	case 7: return _rsx_vp_output_back_color0;
	case 8: return _rsx_vp_output_back_color1;
	case 9: case 10: case 11: case 12: return _rsx_vp_output_texture0 + (int)address - 9;
	default: return -1;
	}
}

static int output_saturates(int output)
{
	return output >= _rsx_vp_output_color0 && output <= _rsx_vp_output_back_color1;
}

/* ---------- one NV2A instruction */

/* a unit's operation and where its result goes: a temporary and an output (o[0] is a temporary) */
struct unit
{
	int active;
	int opcode;
	struct rsx_source source[3];
	uint32_t reads;
	int temporary;
	int temporary_mask;
	int output;
	int output_mask;
};

static uint32_t unit_writes(const struct unit *unit)
{
	uint32_t writes = 0;

	if (unit->temporary >= 0 && unit->temporary_mask)
		writes |= 1u << unit->temporary;
	if (unit->output == _rsx_vp_output_position && unit->output_mask)
		writes |= 1u << _temporary_position;
	return writes;
}

static int unit_destinations(const struct unit *unit)
{
	return (unit->temporary >= 0 && unit->temporary_mask != 0) + (unit->output >= 0 && unit->output_mask != 0);
}

static void unit_write(struct context *context, const struct unit *unit, int scratch)
{
	if (unit->output >= 0 && unit->output_mask)
	{
		if (unit->output == _rsx_vp_output_position)
			emit_move(context, _rsx_register_temporary, _temporary_position, unit->output_mask, 0,
				source_register(_rsx_register_temporary, scratch));
		else
			emit_move(context, _rsx_register_output, unit->output, unit->output_mask, output_saturates(unit->output),
				source_register(_rsx_register_temporary, scratch));
		context->written[unit->output] |= (uint8_t)unit->output_mask;
	}
	if (unit->temporary >= 0 && unit->temporary_mask)
		emit_move(context, _rsx_register_temporary, unit->temporary, unit->temporary_mask, 0,
			source_register(_rsx_register_temporary, scratch));
}

/* the unit's result into its destinations, or with deferred set, into scratch only */
static void unit_emit(struct context *context, const struct unit *unit, int scratch, int deferred)
{
	if (!unit->active)
		return;
	if (deferred || unit_destinations(unit) > 1)
	{
		/* the first destination written could be one of the unit's own operands */
		emit(context, unit->opcode, _rsx_register_temporary, scratch, _rsx_xyzw, 0,
			unit->source[0], unit->source[1], unit->source[2]);
		if (!deferred)
			unit_write(context, unit, scratch);
		return;
	}
	if (unit->output >= 0 && unit->output_mask)
	{
		if (unit->output == _rsx_vp_output_position)
			emit(context, unit->opcode, _rsx_register_temporary, _temporary_position, unit->output_mask, 0,
				unit->source[0], unit->source[1], unit->source[2]);
		else
			emit(context, unit->opcode, _rsx_register_output, unit->output, unit->output_mask,
				output_saturates(unit->output), unit->source[0], unit->source[1], unit->source[2]);
		context->written[unit->output] |= (uint8_t)unit->output_mask;
	}
	else if (unit->temporary >= 0 && unit->temporary_mask)
	{
		emit(context, unit->opcode, _rsx_register_temporary, unit->temporary, unit->temporary_mask, 0,
			unit->source[0], unit->source[1], unit->source[2]);
	}
}

static void address_emit(struct context *context, const struct unit *mac)
{
	struct rsx_source value = mac->source[0];

	/* as port/linux/src/nv2a_vsh.c: a computed index can fall just short of its integer */
	if (value.type != _rsx_register_constant)
	{
		emit(context, _rsx_vp_add, _rsx_register_temporary, _temporary_address, _rsx_x, 0,
			value, source_none(),
			source_swizzled(source_register(_rsx_register_constant, _rsx_vp_constant_address_bias), 0, 0, 0, 0));
		value = source_swizzled(source_register(_rsx_register_temporary, _temporary_address), 0, 0, 0, 0);
	}
	emit(context, _rsx_vp_arl, _rsx_register_address, 0, _rsx_x, 0, value, source_none(), source_none());
}

static int translate_instruction(struct context *context, const uint32_t *instruction, int *clip_kept)
{
	uint32_t mac_opcode = field(instruction, 1, 21, 4);
	uint32_t ilu_opcode = field(instruction, 1, 25, 3);
	uint32_t mac_mask = field(instruction, 3, 24, 4);
	uint32_t temporary = field(instruction, 3, 20, 4);
	uint32_t ilu_mask = field(instruction, 3, 16, 4);
	uint32_t output_mask = field(instruction, 3, 12, 4);
	uint32_t output_is_register = field(instruction, 3, 11, 1);
	uint32_t output_address = field(instruction, 3, 3, 8);
	uint32_t output_from_ilu = field(instruction, 3, 2, 1);
	int relative = (int)field(instruction, 3, 1, 1);
	struct rsx_source a = operand(instruction, 'A', relative);
	struct rsx_source b = operand(instruction, 'B', relative);
	struct rsx_source c = operand(instruction, 'C', relative);
	struct unit mac, ilu;
	int output = output_mask && output_is_register ? output_register(output_address) : -1;
	int ilu_first, both_hazards, address_after_ilu;

	memset(&mac, 0, sizeof(mac));
	memset(&ilu, 0, sizeof(ilu));
	mac.temporary = ilu.temporary = -1;
	mac.output = ilu.output = -1;

	/* the screen-space conversion takes rcc of the clip position's w: keep that position */
	if (ilu_opcode == _ilu_rcc && c.type == _rsx_register_temporary && c.index == _temporary_position)
	{
		emit_move(context, _rsx_register_temporary, _temporary_clip, _rsx_xyzw, 0,
			source_register(_rsx_register_temporary, _temporary_position));
		*clip_kept = 1;
	}

	if (mac_opcode != _mac_nop)
	{
		mac.active = 1;
		mac.source[0] = source_none();
		mac.source[1] = source_none();
		mac.source[2] = source_none();
		switch (mac_opcode)
		{
		case _mac_mov: case _mac_arl:
			mac.source[0] = a;
			break;
		case _mac_add:
			mac.source[0] = a;
			mac.source[2] = c;
			break;
		case _mac_mad:
			mac.source[0] = a;
			mac.source[1] = b;
			mac.source[2] = c;
			break;
		case _mac_mul: case _mac_dp3: case _mac_dph: case _mac_dp4: case _mac_dst:
		case _mac_min: case _mac_max: case _mac_slt: case _mac_sge:
			mac.source[0] = a;
			mac.source[1] = b;
			break;
		default:
			/* opcodes 14 and 15 do nothing on the NV2A either */
			mac.active = 0;
			break;
		}
		mac.opcode = (int)mac_opcode;
		mac.reads = temporary_bit(&mac.source[0]) | temporary_bit(&mac.source[1]) | temporary_bit(&mac.source[2]);
		if (mac.active && mac_opcode != _mac_arl && mac_mask)
		{
			mac.temporary = temporary == 12 ? _temporary_position : (int)temporary;
			mac.temporary_mask = nv2a_mask(mac_mask);
		}
		if (mac.active && mac_opcode != _mac_arl && !output_from_ilu && output >= 0)
		{
			mac.output = output;
			mac.output_mask = nv2a_mask(output_mask);
		}
	}
	if (ilu_opcode != _ilu_nop)
	{
		static const int opcodes[] =
		{
			0, _rsx_vp_mov, _rsx_vp_rcp, _rsx_vp_rcc, _rsx_vp_rsq, _rsx_vp_exp, _rsx_vp_log, _rsx_vp_lit,
		};

		ilu.active = 1;
		ilu.opcode = opcodes[ilu_opcode];
		ilu.source[0] = source_none();
		ilu.source[1] = source_none();
		ilu.source[2] = source_none();
		/* the scalar unit's mov moves the whole vector: the vector unit's does the same */
		ilu.source[ilu_opcode == _ilu_mov ? 0 : 2] = c;
		ilu.reads = temporary_bit(&c);
		if (ilu_mask)
		{
			/* with both units running, the scalar result goes to r1 */
			int destination = mac.active ? 1 : (int)temporary;

			ilu.temporary = destination == 12 ? _temporary_position : destination;
			ilu.temporary_mask = nv2a_mask(ilu_mask);
		}
		if (output_from_ilu && output >= 0)
		{
			ilu.output = output;
			ilu.output_mask = nv2a_mask(output_mask);
		}
	}

	/* each unit reads its operands before either writes */
	ilu_first = ilu.active && mac.active && (unit_writes(&mac) & ilu.reads);
	both_hazards = ilu_first && (unit_writes(&ilu) & mac.reads);
	address_after_ilu = mac.active && mac.opcode == _mac_arl && ilu.active && c.relative;
	if (both_hazards)
	{
		unit_emit(context, &ilu, _temporary_ilu, 1);
		unit_emit(context, &mac, _temporary_mac, 0);
		unit_write(context, &ilu, _temporary_ilu);
	}
	else if (ilu_first || address_after_ilu)
	{
		unit_emit(context, &ilu, _temporary_ilu, 0);
		if (mac.active && mac.opcode == _mac_arl)
			address_emit(context, &mac);
		else
			unit_emit(context, &mac, _temporary_mac, 0);
	}
	else
	{
		if (mac.active && mac.opcode == _mac_arl)
			address_emit(context, &mac);
		else
			unit_emit(context, &mac, _temporary_mac, 0);
		unit_emit(context, &ilu, _temporary_ilu, 0);
	}
	return (int)field(instruction, 3, 0, 1);
}

/* ---------- the program */

/* the temporaries a program reads before it writes them: 0 on the NV2A, undefined on the RSX */
static uint32_t unwritten_scan(const uint32_t *nv2a, uint32_t count)
{
	uint8_t written[12];
	uint32_t index, unwritten = 0;

	memset(written, 0, sizeof(written));
	for (index = 0; index < count; index++)
	{
		const uint32_t *instruction = nv2a + index * 4;
		uint32_t mac = field(instruction, 1, 21, 4), ilu = field(instruction, 1, 25, 3);
		/* the operands each unit reads, and the components the scalar unit takes */
		int reads_a = mac != _mac_nop && mac <= _mac_arl;
		int reads_b = mac == _mac_mad || (mac >= _mac_mul && mac != _mac_add && mac <= _mac_sge);
		int reads_c = (mac == _mac_add || mac == _mac_mad) || ilu != _ilu_nop;
		int c_components = (mac == _mac_add || mac == _mac_mad || ilu == _ilu_mov) ? 0xf :
			ilu == _ilu_lit ? 0xb : 0x1;
		char operands[3] = { 'A', 'B', 'C' };
		int which;

		if (mac > _mac_arl)
			reads_a = reads_b = 0;
		for (which = 0; which < 3; which++)
		{
			struct rsx_source source;
			int component, components = which == 2 ? c_components : 0xf;

			if ((which == 0 && !reads_a) || (which == 1 && !reads_b) || (which == 2 && !reads_c))
				continue;
			source = operand(instruction, operands[which], 0);
			if (source.type != _rsx_register_temporary || source.index >= 12)
				continue;
			for (component = 0; component < 4; component++)
			{
				if ((components & (1 << component)) && !(written[source.index] & (1 << source.swizzle[component])))
					unwritten |= 1u << source.index;
			}
		}
		if (mac != _mac_nop && mac < _mac_arl && field(instruction, 3, 24, 4) && field(instruction, 3, 20, 4) < 12)
			written[field(instruction, 3, 20, 4)] |= nv2a_mask(field(instruction, 3, 24, 4));
		if (ilu != _ilu_nop && field(instruction, 3, 16, 4))
		{
			uint32_t destination = mac != _mac_nop ? 1 : field(instruction, 3, 20, 4);

			if (destination < 12)
				written[destination] |= nv2a_mask(field(instruction, 3, 16, 4));
		}
		if (field(instruction, 3, 0, 1))
			break;
	}
	return unwritten;
}

/* the outputs a program writes, to set the ones it leaves (in part) to the NV2A's defaults */
static void outputs_scan(const uint32_t *nv2a, uint32_t count, uint8_t written[16])
{
	uint32_t index;

	memset(written, 0, 16);
	for (index = 0; index < count; index++)
	{
		const uint32_t *instruction = nv2a + index * 4;
		int output = field(instruction, 3, 11, 1) ? output_register(field(instruction, 3, 3, 8)) : -1;
		int unit = field(instruction, 3, 2, 1) ? (int)field(instruction, 1, 25, 3) : (int)field(instruction, 1, 21, 4);

		if (output >= 0 && unit)
			written[output] |= nv2a_mask(field(instruction, 3, 12, 4));
		if (field(instruction, 3, 0, 1))
			break;
	}
}

/* clip space for the RSX's GL-style viewport, from the NV2A's screen space: the clip position kept
at the rcc when there is one (no division by w, which loses precision near the camera) */
static void epilogue(struct context *context, int clip_kept)
{
	struct rsx_source scale = source_register(_rsx_register_constant, _rsx_vp_constant_screen_scale);
	struct rsx_source offset = source_register(_rsx_register_constant, _rsx_vp_constant_screen_offset);
	struct rsx_source result = source_register(_rsx_register_temporary, _temporary_epilogue);
	struct rsx_source factor = source_register(_rsx_register_temporary, _temporary_epilogue2);
	struct rsx_source position;

	if (clip_kept)
	{
		struct rsx_source clip = source_register(_rsx_register_temporary, _temporary_clip);

		/* screen = clip * c[-38] / w + c[-37], so clip' = clip * (c[-38] * scale) + (c[-37] * scale + offset) * w */
		emit_move(context, _rsx_register_temporary, _temporary_epilogue, _rsx_xyzw, 0, scale);
		emit(context, _rsx_vp_mul, _rsx_register_temporary, _temporary_epilogue2, _rsx_xyzw, 0, result,
			source_register(_rsx_register_constant, _rsx_vp_constant_viewport_offset), source_none());
		emit(context, _rsx_vp_add, _rsx_register_temporary, _temporary_epilogue2, _rsx_xyzw, 0, factor,
			source_none(), offset);
		emit(context, _rsx_vp_mul, _rsx_register_temporary, _temporary_epilogue, _rsx_xyzw, 0, result,
			source_register(_rsx_register_constant, _rsx_vp_constant_viewport_scale), source_none());
		emit(context, _rsx_vp_mul, _rsx_register_temporary, _temporary_epilogue, _rsx_xyzw, 0, clip, result,
			source_none());
		emit(context, _rsx_vp_mad, _rsx_register_temporary, _temporary_epilogue, _rsx_xyz, 0, factor,
			source_swizzled(clip, 3, 3, 3, 3), result);
		position = clip;
	}
	else
	{
		position = source_register(_rsx_register_temporary, _temporary_position);
		emit_move(context, _rsx_register_temporary, _temporary_epilogue, _rsx_xyzw, 0, scale);
		emit(context, _rsx_vp_mad, _rsx_register_temporary, _temporary_epilogue, _rsx_xyzw, 0, position, result,
			offset);
		emit(context, _rsx_vp_mul, _rsx_register_temporary, _temporary_epilogue, _rsx_xyz, 0, result,
			source_swizzled(position, 3, 3, 3, 3), source_none());
	}
	emit_move(context, _rsx_register_temporary, _temporary_epilogue, _rsx_w, 0, source_swizzled(position, 3, 3, 3, 3));
	emit_move(context, _rsx_register_output, _rsx_vp_output_position, _rsx_xyzw, 0, result);
}

int rsx_vertex_program_instructions(const uint32_t *nv2a, uint32_t nv2a_count,
	struct rsx_instruction *instructions, uint32_t maximum, uint32_t *count)
{
	struct context context;
	uint8_t written[16];
	int clip_kept = 0, output;
	uint32_t index;

	memset(&context, 0, sizeof(context));
	context.instructions = instructions;
	context.maximum = maximum;

	/* the NV2A's defaults for what the program leaves unwritten: 0 0 0 1, and fog 1 */
	outputs_scan(nv2a, nv2a_count, written);
	for (output = _rsx_vp_output_color0; output <= _rsx_vp_output_texture0 + 3; output++)
	{
		if (output == _rsx_vp_output_back_color0 || output == _rsx_vp_output_back_color1 ||
			output == _rsx_vp_output_point_size)
			continue;
		if (output == _rsx_vp_output_fog)
		{
			if (!(written[output] & _rsx_x))
				emit_move(&context, _rsx_register_output, output, _rsx_xyzw, 0,
					source_register(_rsx_register_constant, _rsx_vp_constant_one));
		}
		else if (written[output] != _rsx_xyzw)
		{
			emit_move(&context, _rsx_register_output, output, _rsx_xyzw, 0,
				source_register(_rsx_register_constant, _rsx_vp_constant_default));
		}
	}
	emit_move(&context, _rsx_register_temporary, _temporary_position, _rsx_xyzw, 0,
		source_register(_rsx_register_constant, _rsx_vp_constant_default));
	{
		uint32_t unwritten = unwritten_scan(nv2a, nv2a_count);
		int temporary;

		for (temporary = 0; temporary < 12; temporary++)
		{
			if (unwritten & (1u << temporary))
				emit_move(&context, _rsx_register_temporary, temporary, _rsx_xyzw, 0,
					source_register(_rsx_register_constant, _rsx_vp_constant_zero));
		}
	}

	for (index = 0; index < nv2a_count; index++)
	{
		if (translate_instruction(&context, nv2a + index * 4, &clip_kept))
			break;
	}
	epilogue(&context, clip_kept);
	*count = context.count;
	return context.overflow ? -1 : 0;
}

int rsx_vertex_program_translate(const uint32_t *nv2a, uint32_t nv2a_count, struct rsx_vertex_program *program)
{
	struct rsx_instruction *instructions = malloc(RSX_VP_MAXIMUM_INSTRUCTIONS * sizeof(*instructions));
	uint32_t count = 0, index, position;

	memset(program, 0, sizeof(*program));
	if (!instructions)
		return -1;
	if (rsx_vertex_program_instructions(nv2a, nv2a_count, instructions, RSX_VP_MAXIMUM_INSTRUCTIONS, &count) != 0 ||
		!(program->microcode = malloc(count * 4 * sizeof(uint32_t))))
	{
		free(instructions);
		return -1;
	}
	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];

		rsx_vertex_encode(instruction, index + 1 == count, program->microcode + index * 4);
		for (position = 0; position < 3; position++)
		{
			if (instruction->source[position].type == _rsx_register_input)
				program->input_mask |= 1u << instruction->source[position].index;
		}
		if (instruction->destination_type == _rsx_register_output)
		{
			int output = instruction->destination;

			/* the result mask bits as cgcomp sets them */
			if (output == _rsx_vp_output_color0 || output == _rsx_vp_output_back_color0)
				program->output_mask |= (1 << 0) | (1 << 2);
			else if (output == _rsx_vp_output_color1 || output == _rsx_vp_output_back_color1)
				program->output_mask |= (1 << 1) | (1 << 3);
			else if (output == _rsx_vp_output_fog)
				program->output_mask |= 1 << 4;
			else if (output == _rsx_vp_output_point_size)
				program->output_mask |= 1 << 5;
			else if (output >= _rsx_vp_output_texture0)
				program->output_mask |= 0x4000u << (output - _rsx_vp_output_texture0);
		}
	}
	free(instructions);
	program->instruction_count = count;
	program->register_count = _temporary_count;
	return 0;
}

void rsx_vertex_program_free(struct rsx_vertex_program *program)
{
	free(program->microcode);
	memset(program, 0, sizeof(*program));
}
