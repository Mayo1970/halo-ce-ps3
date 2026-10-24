/* NV2A pixel shaders (texture stages, register combiners, the final combiner) as RSX fragment programs,
with the semantics of port/linux/src/nv2a_psh.c. Per-draw constants are inline constants, patched per draw. */

#include "ps3_rsx_program.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* R0 is the colour the program exports */
enum
{
	_register_color = 0,
	_register_v0 = 1,
	_register_v1 = 2,
	_register_t0 = 3,
	_register_r0 = 7,
	_register_r1 = 8,
	_register_fog = 9,
	_register_a = 10,
	_register_b = 11,
	_register_c = 12,
	_register_d = 13,
	_register_ab = 14,
	_register_cd = 15,
	_register_sum = 16,
	_register_scratch = 17,
	_register_scratch2 = 18,
	/* the dot product stages' results, stage n in component n */
	_register_dots = 19,
	_register_coordinates = 20,
	_register_v1r0_sum = 21,
	_register_ef_product = 22,
	/* each combiner stage's ab, cd and sum, kept for clamp_through until the optimizer packs the registers */
	_register_stages = 23,
	_register_count = _register_stages + 3 * 8,
};

/* combiner registers */
enum
{
	_input_zero = 0,
	_input_c0 = 1,
	_input_c1 = 2,
	_input_fog = 3,
	_input_v0 = 4,
	_input_v1 = 5,
	_input_t0 = 8,
	_input_r0 = 12,
	_input_r1 = 13,
	_input_v1r0_sum = 14,
	_input_ef_product = 15,
};

enum
{
	_mode_none = 0x00,
	_mode_project2d = 0x01,
	_mode_project3d = 0x02,
	_mode_cubemap = 0x03,
	_mode_passthru = 0x04,
	_mode_clipplane = 0x05,
	_mode_bumpenvmap = 0x06,
	_mode_bumpenvmap_luminance = 0x07,
	_mode_brdf = 0x08,
	_mode_dot_st = 0x09,
	_mode_dot_zw = 0x0a,
	_mode_dot_reflect_diffuse = 0x0b,
	_mode_dot_reflect_specular = 0x0c,
	_mode_dot_str_3d = 0x0d,
	_mode_dot_str_cube = 0x0e,
	_mode_dependent_ar = 0x0f,
	_mode_dependent_gb = 0x10,
	_mode_dot_product = 0x11,
	_mode_dot_reflect_specular_constant = 0x12,
};

/* the fixed constant most instructions take their numbers from: 0 1 2 0.5 */
enum { _k_zero, _k_one, _k_two, _k_half };

/* the clamp to [-1, 1] a combiner's write makes: one instruction for the optimizer, which can see through it
(clamp_through), then an RSX MAX and MIN (expand_clamps) */
enum { _op_clamp_signed = 0x3e };
static const float fixed_constant[4] = { 0.0f, 1.0f, 2.0f, 0.5f };

struct context
{
	const struct rsx_fragment_key *key;
	struct rsx_instruction *instructions;
	uint32_t count;
	uint32_t maximum;
	int overflow;
	int kill;
	uint32_t texcoord_mask;
};

/* ---------- instructions */

static struct rsx_source source_none(void)
{
	struct rsx_source source;

	memset(&source, 0, sizeof(source));
	source.swizzle[1] = 1;
	source.swizzle[2] = 2;
	source.swizzle[3] = 3;
	return source;
}

static struct rsx_source temporary(int index)
{
	struct rsx_source source = source_none();

	source.type = _rsx_register_temporary;
	source.index = (uint16_t)index;
	return source;
}

static struct rsx_source input(struct context *context, int index)
{
	struct rsx_source source = source_none();

	source.type = _rsx_register_input;
	source.index = (uint16_t)index;
	if (index >= _rsx_fp_input_texture0)
		context->texcoord_mask |= 1u << (index - _rsx_fp_input_texture0);
	return source;
}

/* the instruction's inline constant, read through a swizzle */
static struct rsx_source constant(int x, int y, int z, int w)
{
	struct rsx_source source = source_none();

	source.type = _rsx_register_constant;
	source.swizzle[0] = (uint8_t)x;
	source.swizzle[1] = (uint8_t)y;
	source.swizzle[2] = (uint8_t)z;
	source.swizzle[3] = (uint8_t)w;
	return source;
}

static struct rsx_source fixed(int component)
{
	return constant(component, component, component, component);
}

static struct rsx_source swizzled(struct rsx_source source, int x, int y, int z, int w)
{
	uint8_t original[4];

	memcpy(original, source.swizzle, sizeof(original));
	source.swizzle[0] = original[x];
	source.swizzle[1] = original[y];
	source.swizzle[2] = original[z];
	source.swizzle[3] = original[w];
	return source;
}

static struct rsx_source replicated(struct rsx_source source, int component)
{
	return swizzled(source, component, component, component, component);
}

static struct rsx_source negated(struct rsx_source source)
{
	source.negate = (uint8_t)!source.negate;
	return source;
}

static struct rsx_instruction *emit(struct context *context, int opcode, int destination, int mask,
	struct rsx_source a, struct rsx_source b, struct rsx_source c)
{
	struct rsx_instruction *instruction;

	if (context->count >= context->maximum)
	{
		static struct rsx_instruction discard;

		context->overflow = 1;
		return &discard;
	}
	instruction = &context->instructions[context->count++];
	memset(instruction, 0, sizeof(*instruction));
	instruction->opcode = (uint8_t)opcode;
	instruction->destination_type = destination < 0 ? _rsx_register_none : _rsx_register_temporary;
	instruction->destination = (uint8_t)(destination < 0 ? 0 : destination);
	instruction->mask = (uint8_t)mask;
	instruction->condition = _rsx_condition_always;
	instruction->condition_swizzle[1] = 1;
	instruction->condition_swizzle[2] = 2;
	instruction->condition_swizzle[3] = 3;
	instruction->uniform = RSX_NO_UNIFORM;
	instruction->source[0] = a;
	instruction->source[1] = b;
	instruction->source[2] = c;
	memcpy(instruction->constant, fixed_constant, sizeof(instruction->constant));
	return instruction;
}

static struct rsx_instruction *emit1(struct context *context, int opcode, int destination, int mask,
	struct rsx_source a)
{
	return emit(context, opcode, destination, mask, a, source_none(), source_none());
}

static struct rsx_instruction *emit2(struct context *context, int opcode, int destination, int mask,
	struct rsx_source a, struct rsx_source b)
{
	return emit(context, opcode, destination, mask, a, b, source_none());
}

static void set_constant(struct rsx_instruction *instruction, float x, float y, float z, float w)
{
	instruction->constant[0] = x;
	instruction->constant[1] = y;
	instruction->constant[2] = z;
	instruction->constant[3] = w;
}

static struct rsx_instruction *saturated(struct rsx_instruction *instruction)
{
	instruction->saturate = 1;
	return instruction;
}

/* a per-draw constant into a register */
static void load_uniform(struct context *context, int destination, int mask, int uniform)
{
	struct rsx_instruction *instruction = emit1(context, _rsx_fp_mov, destination, mask, constant(0, 1, 2, 3));

	instruction->uniform = (uint8_t)uniform;
	set_constant(instruction, 0.0f, 0.0f, 0.0f, 0.0f);
}

static void kill_if(struct context *context, int condition, int component)
{
	struct rsx_instruction *instruction = emit(context, _rsx_fp_kil, -1, _rsx_xyzw, source_none(), source_none(),
		source_none());

	instruction->condition = (uint8_t)condition;
	memset(instruction->condition_swizzle, component, sizeof(instruction->condition_swizzle));
	context->kill = 1;
}

/* the condition register from the masked components of a source */
static void condition_set(struct context *context, int mask, struct rsx_source source)
{
	struct rsx_instruction *instruction = emit1(context, _rsx_fp_mov, -1, mask, source);

	instruction->condition_update = 1;
}

/* signed bytes in unsigned texels: (b >= 128 ? b - 256 : b) / 127, b = floor(x * 255 + 0.5) */
static void signed_bytes(struct context *context, int destination, int mask, struct rsx_source value)
{
	set_constant(emit(context, _rsx_fp_mad, _register_scratch, mask, value, fixed(0), fixed(1)),
		255.0f, 0.5f, 128.0f, -256.0f);
	emit1(context, _rsx_fp_flr, _register_scratch, mask, temporary(_register_scratch));
	set_constant(emit2(context, _rsx_fp_sge, _register_scratch2, mask, temporary(_register_scratch), fixed(2)),
		255.0f, 0.5f, 128.0f, -256.0f);
	set_constant(emit(context, _rsx_fp_mad, _register_scratch, mask, temporary(_register_scratch2), fixed(3),
		temporary(_register_scratch)), 255.0f, 0.5f, 128.0f, -256.0f);
	set_constant(emit2(context, _rsx_fp_mul, destination, mask, temporary(_register_scratch), fixed(0)),
		1.0f / 127.0f, 0.0f, 0.0f, 0.0f);
}

/* ---------- texture stages */

static int stage_mode(const struct rsx_fragment_key *key, int stage)
{
	return (int)((key->texture_modes >> (5 * stage)) & 0x1f);
}

static int stage_input(const struct rsx_fragment_key *key, int stage)
{
	switch (stage)
	{
	case 2: return (int)((key->input_texture >> 16) & 1);
	case 3: return (int)((key->input_texture >> 20) & 3);
	default: return 0;
	}
}

/* a dot product stage's input texel, mapped per PSDotMapping, into scratch.xyz */
static void dot_input(struct context *context, int stage)
{
	uint32_t mapping = (context->key->dot_mapping >> ((stage - 1) * 4)) & 7;
	struct rsx_source texel = temporary(_register_t0 + stage_input(context->key, stage));

	switch (mapping)
	{
	case 0:
		emit1(context, _rsx_fp_mov, _register_scratch, _rsx_xyz, texel);
		break;
	case 1:
		set_constant(emit(context, _rsx_fp_mad, _register_scratch, _rsx_xyz, texel, fixed(0), fixed(1)),
			255.0f / 127.0f, -128.0f / 127.0f, 0.0f, 0.0f);
		break;
	case 3:
		signed_bytes(context, _register_scratch, _rsx_xyz, texel);
		break;
	default:
		emit(context, _rsx_fp_mad, _register_scratch, _rsx_xyz, texel, fixed(_k_two), negated(fixed(_k_one)));
		break;
	}
}

/* dot_n = dot(texcoord n, the mapped input) */
static void stage_dot(struct context *context, int stage)
{
	dot_input(context, stage);
	emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + stage));
	emit2(context, _rsx_fp_dp3, _register_dots, 1 << stage, temporary(_register_coordinates),
		temporary(_register_scratch));
}

static void sample(struct context *context, int opcode, int stage, struct rsx_source coordinates)
{
	struct rsx_instruction *instruction;

	/* a linear target drawn at the screen's scale: the game's texel coordinates (its units) to the target's pixels,
	after a bump offset and before a projection's divide */
	if (context->key->texture_scaled & (1 << stage))
	{
		instruction = emit2(context, _rsx_fp_mul, _register_coordinates, _rsx_x | _rsx_y, coordinates,
			constant(0, 1, 2, 3));
		instruction->uniform = (uint8_t)(_rsx_uniform_texture_scale + stage);
		set_constant(instruction, 0.0f, 0.0f, 0.0f, 0.0f);
		coordinates = temporary(_register_coordinates);
	}
	instruction = emit1(context, opcode, _register_t0 + stage, _rsx_xyzw, coordinates);
	instruction->texture_unit = (uint8_t)stage;
}

static void texture_stage(struct context *context, int stage)
{
	const struct rsx_fragment_key *key = context->key;
	int mode = stage_mode(key, stage);
	int texel = _register_t0 + stage;
	struct rsx_source dots = temporary(_register_dots);

	if (key->sampler_type[stage] == _rsx_sampler_none &&
		mode != _mode_passthru && mode != _mode_clipplane && mode != _mode_dot_product && mode != _mode_dot_zw)
		mode = _mode_none;
	switch (mode)
	{
	case _mode_project2d:
	case _mode_project3d:
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + stage));
		sample(context, _rsx_fp_txp, stage, temporary(_register_coordinates));
		break;
	case _mode_cubemap:
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + stage));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	case _mode_passthru:
		saturated(emit1(context, _rsx_fp_mov, texel, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + stage)));
		break;
	case _mode_clipplane:
	{
		uint32_t compare = (key->compare_mode >> (4 * stage)) & 0xf;
		int component;

		condition_set(context, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + stage));
		for (component = 0; component < 4; component++)
		{
			kill_if(context, (compare & (1u << component)) ? _rsx_condition_greater_equal : _rsx_condition_less,
				component);
		}
		emit1(context, _rsx_fp_mov, texel, _rsx_xyzw, fixed(_k_zero));
		break;
	}
	case _mode_bumpenvmap:
	case _mode_bumpenvmap_luminance:
	{
		struct rsx_source previous = temporary(texel - 1);
		struct rsx_source matrix = temporary(_register_scratch2);
		struct rsx_source offsets = temporary(_register_ab);

		/* the signed offsets from the previous stage's red and green */
		signed_bytes(context, _register_ab, _rsx_x | _rsx_y, previous);
		load_uniform(context, _register_scratch2, _rsx_xyzw, _rsx_uniform_bump_matrix + stage);
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + stage));
		emit(context, _rsx_fp_mad, _register_coordinates, _rsx_x | _rsx_y, swizzled(matrix, 0, 1, 1, 1),
			replicated(offsets, 0), temporary(_register_coordinates));
		emit(context, _rsx_fp_mad, _register_coordinates, _rsx_x | _rsx_y, swizzled(matrix, 2, 3, 3, 3),
			replicated(offsets, 1), temporary(_register_coordinates));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		if (mode == _mode_bumpenvmap_luminance)
		{
			struct rsx_source luminance = temporary(_register_scratch2);

			load_uniform(context, _register_scratch2, _rsx_xyzw, _rsx_uniform_bump_luminance + stage);
			saturated(emit(context, _rsx_fp_mad, _register_scratch, _rsx_x, replicated(luminance, 0),
				replicated(previous, 2), replicated(luminance, 1)));
			emit2(context, _rsx_fp_mul, texel, _rsx_xyz, temporary(texel), replicated(temporary(_register_scratch), 0));
		}
		break;
	}
	case _mode_dot_product:
	case _mode_dot_zw:
		stage_dot(context, stage);
		emit1(context, _rsx_fp_mov, texel, _rsx_xyzw, fixed(_k_zero));
		break;
	case _mode_dot_st:
		stage_dot(context, stage);
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, swizzled(dots, stage - 1, stage, stage, stage));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	case _mode_dot_reflect_diffuse:
		/* the normal takes its third component from stage 3's dot product */
		stage_dot(context, stage);
		stage_dot(context, 3);
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, swizzled(dots, 1, 2, 3, 3));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	case _mode_dot_reflect_specular:
	case _mode_dot_reflect_specular_constant:
	{
		struct rsx_source normal = swizzled(dots, 1, 2, 3, 3);
		struct rsx_source eye = temporary(_register_scratch2);
		struct rsx_source factor = temporary(_register_scratch);

		stage_dot(context, stage);
		if (mode == _mode_dot_reflect_specular)
		{
			emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + 1));
			emit1(context, _rsx_fp_mov, _register_scratch2, _rsx_x, replicated(temporary(_register_coordinates), 3));
			emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + 2));
			emit1(context, _rsx_fp_mov, _register_scratch2, _rsx_y, replicated(temporary(_register_coordinates), 3));
			emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, input(context, _rsx_fp_input_texture0 + 3));
			emit1(context, _rsx_fp_mov, _register_scratch2, _rsx_z, replicated(temporary(_register_coordinates), 3));
		}
		else
		{
			load_uniform(context, _register_scratch2, _rsx_xyzw, _rsx_uniform_c0);
		}
		/* r = 2 n dot(n, e) / max(dot(n, n), 1e-20) - e */
		emit2(context, _rsx_fp_dp3, _register_scratch, _rsx_x, normal, eye);
		emit2(context, _rsx_fp_dp3, _register_scratch, _rsx_y, normal, normal);
		set_constant(emit2(context, _rsx_fp_max, _register_scratch, _rsx_y, replicated(factor, 1), fixed(0)),
			1.0e-20f, 0.0f, 0.0f, 0.0f);
		emit1(context, _rsx_fp_rcp, _register_scratch, _rsx_y, replicated(factor, 1));
		emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, replicated(factor, 0), replicated(factor, 1));
		emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, replicated(factor, 0), fixed(_k_two));
		emit(context, _rsx_fp_mad, _register_coordinates, _rsx_xyz, normal, replicated(factor, 0), negated(eye));
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_w, fixed(_k_one));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	}
	case _mode_dot_str_3d:
	case _mode_dot_str_cube:
		stage_dot(context, stage);
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw, swizzled(dots, 1, 2, 3, 3));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	case _mode_dependent_ar:
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw,
			swizzled(temporary(_register_t0 + stage_input(key, stage)), 3, 0, 0, 0));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	case _mode_dependent_gb:
		emit1(context, _rsx_fp_mov, _register_coordinates, _rsx_xyzw,
			swizzled(temporary(_register_t0 + stage_input(key, stage)), 1, 2, 2, 2));
		sample(context, _rsx_fp_tex, stage, temporary(_register_coordinates));
		break;
	default:
		mode = _mode_none;
		emit1(context, _rsx_fp_mov, texel, _rsx_xyzw, fixed(_k_zero));
		break;
	}

	/* channels marked signed (D3DTSS_COLORSIGN): alpha, red, green, blue from bit 0 */
	if (key->color_sign[stage] && mode != _mode_none)
	{
		int mask = ((key->color_sign[stage] & 1) ? _rsx_w : 0) | ((key->color_sign[stage] & 2) ? _rsx_x : 0) |
			((key->color_sign[stage] & 4) ? _rsx_y : 0) | ((key->color_sign[stage] & 8) ? _rsx_z : 0);

		signed_bytes(context, texel, mask, temporary(texel));
	}
	if (key->alpha_kill[stage] && mode != _mode_none)
	{
		condition_set(context, _rsx_w, replicated(temporary(texel), 3));
		kill_if(context, _rsx_condition_equal, 3);
	}
}

/* ---------- combiners */

/* a combiner register as a source; a per-draw constant goes through scratch first */
static struct rsx_source register_source(struct context *context, uint32_t reg, int stage)
{
	const struct rsx_fragment_key *key = context->key;
	int unique_c0 = (key->combiner_count & 0x1000) != 0;
	int unique_c1 = (key->combiner_count & 0x10000) != 0;

	switch (reg)
	{
	case _input_c0:
		load_uniform(context, _register_scratch, _rsx_xyzw,
			stage < 0 ? _rsx_uniform_final_c0 : _rsx_uniform_c0 + (unique_c0 ? stage : 0));
		return temporary(_register_scratch);
	case _input_c1:
		load_uniform(context, _register_scratch, _rsx_xyzw,
			stage < 0 ? _rsx_uniform_final_c1 : _rsx_uniform_c1 + (unique_c1 ? stage : 0));
		return temporary(_register_scratch);
	case _input_fog: return temporary(_register_fog);
	case _input_v0: return temporary(_register_v0);
	case _input_v1: return temporary(_register_v1);
	case 8: case 9: case 10: case 11: return temporary(_register_t0 + (int)reg - 8);
	case _input_r0: return temporary(_register_r0);
	case _input_r1: return temporary(_register_r1);
	case _input_v1r0_sum: return stage < 0 ? temporary(_register_v1r0_sum) : fixed(_k_zero);
	case _input_ef_product: return stage < 0 ? temporary(_register_ef_product) : fixed(_k_zero);
	default: return fixed(_k_zero);
	}
}

/* rgb reads .rgb (or .aaa), alpha reads .a (or .b) */
static struct rsx_source input_channels(struct rsx_source source, uint32_t input, int alpha)
{
	int alpha_channel = (input & 0x10) != 0;

	if (alpha)
		return replicated(source, alpha_channel ? 3 : 2);
	return alpha_channel ? replicated(source, 3) : source;
}

/* a general combiner input, mapped, into register into (rgb or alpha components) */
static void combiner_input(struct context *context, uint32_t input, int alpha, int stage, int into)
{
	int mask = alpha ? _rsx_w : _rsx_xyz;
	struct rsx_source value = input_channels(register_source(context, input & 0x0f, stage), input, alpha);
	struct rsx_source result = temporary(into);

	switch (input & 0xe0)
	{
	case 0x00:
		emit2(context, _rsx_fp_max, into, mask, value, fixed(_k_zero));
		break;
	case 0x20:
		/* 1 - clamp(x, 0, 1) is clamp(1 - x, 0, 1) */
		saturated(emit2(context, _rsx_fp_add, into, mask, negated(value), fixed(_k_one)));
		break;
	case 0x40:
		emit2(context, _rsx_fp_max, into, mask, value, fixed(_k_zero));
		emit(context, _rsx_fp_mad, into, mask, result, fixed(_k_two), negated(fixed(_k_one)));
		break;
	case 0x60:
		emit2(context, _rsx_fp_max, into, mask, value, fixed(_k_zero));
		emit(context, _rsx_fp_mad, into, mask, result, negated(fixed(_k_two)), fixed(_k_one));
		break;
	case 0x80:
		emit2(context, _rsx_fp_max, into, mask, value, fixed(_k_zero));
		emit2(context, _rsx_fp_add, into, mask, result, negated(fixed(_k_half)));
		break;
	case 0xa0:
		emit2(context, _rsx_fp_max, into, mask, value, fixed(_k_zero));
		emit2(context, _rsx_fp_add, into, mask, negated(result), fixed(_k_half));
		break;
	case 0xc0:
		emit1(context, _rsx_fp_mov, into, mask, value);
		break;
	default:
		emit1(context, _rsx_fp_mov, into, mask, negated(value));
		break;
	}
}

/* the output mapping; the clamp to [-1, 1] the registers hold comes with the writes, which keeps the value before it
for the next stage's inputs (clamp_through) */
static void output_mapping(struct context *context, int reg, int mask, uint32_t flags)
{
	struct rsx_source value = temporary(reg);

	switch (flags & 0x38)
	{
	case 0x08:
		emit2(context, _rsx_fp_add, reg, mask, value, negated(fixed(_k_half)));
		break;
	case 0x10:
		emit2(context, _rsx_fp_mul, reg, mask, value, fixed(_k_two));
		break;
	case 0x18:
		emit(context, _rsx_fp_mad, reg, mask, value, fixed(_k_two), negated(fixed(_k_one)));
		break;
	case 0x20:
		set_constant(emit2(context, _rsx_fp_mul, reg, mask, value, fixed(0)), 4.0f, 0.0f, 0.0f, 0.0f);
		break;
	case 0x30:
		emit2(context, _rsx_fp_mul, reg, mask, value, fixed(_k_half));
		break;
	default:
		break;
	}
}

static int destination_register(uint32_t reg)
{
	switch (reg)
	{
	case _input_v0: return _register_v0;
	case _input_v1: return _register_v1;
	case 8: case 9: case 10: case 11: return _register_t0 + (int)reg - 8;
	case _input_r0: return _register_r0;
	case _input_r1: return _register_r1;
	default: return -1;
	}
}

/* the mux's selector into scratch2.x: r0.a >= 0.5, or the lowest bit of its byte */
static void mux_selector(struct context *context)
{
	struct rsx_source r0_alpha = replicated(temporary(_register_r0), 3);
	struct rsx_source selector = replicated(temporary(_register_scratch2), 0);

	if (context->key->combiner_count & 0x100)
	{
		emit2(context, _rsx_fp_sge, _register_scratch2, _rsx_x, r0_alpha, fixed(_k_half));
		return;
	}
	set_constant(emit(context, _rsx_fp_mad, _register_scratch2, _rsx_x, r0_alpha, fixed(0), fixed(1)),
		255.0f, 0.5f, 0.0f, 0.0f);
	/* nv2a_psh.c's int() truncates: the parity of floor(|x|) */
	selector.absolute = 1;
	emit1(context, _rsx_fp_flr, _register_scratch2, _rsx_x, selector);
	selector.absolute = 0;
	emit2(context, _rsx_fp_mul, _register_scratch2, _rsx_x, selector, fixed(_k_half));
	emit1(context, _rsx_fp_frc, _register_scratch2, _rsx_x, selector);
	emit2(context, _rsx_fp_mul, _register_scratch2, _rsx_x, selector, fixed(_k_two));
}

static void combiner_stage(struct context *context, int stage)
{
	const struct rsx_fragment_key *key = context->key;
	uint32_t rgb_inputs = key->rgb_inputs[stage], alpha_inputs = key->alpha_inputs[stage];
	uint32_t rgb_outputs = key->rgb_outputs[stage], alpha_outputs = key->alpha_outputs[stage];
	uint32_t rgb_flags = rgb_outputs >> 12, alpha_flags = alpha_outputs >> 12;
	int portion, ab_register = _register_stages + 3 * stage, cd_register = ab_register + 1;
	int sum_register = ab_register + 2;

	combiner_input(context, (rgb_inputs >> 24) & 0xff, 0, stage, _register_a);
	combiner_input(context, (rgb_inputs >> 16) & 0xff, 0, stage, _register_b);
	combiner_input(context, (rgb_inputs >> 8) & 0xff, 0, stage, _register_c);
	combiner_input(context, rgb_inputs & 0xff, 0, stage, _register_d);
	combiner_input(context, (alpha_inputs >> 24) & 0xff, 1, stage, _register_a);
	combiner_input(context, (alpha_inputs >> 16) & 0xff, 1, stage, _register_b);
	combiner_input(context, (alpha_inputs >> 8) & 0xff, 1, stage, _register_c);
	combiner_input(context, alpha_inputs & 0xff, 1, stage, _register_d);

	emit2(context, (rgb_flags & 0x02) ? _rsx_fp_dp3 : _rsx_fp_mul, ab_register, _rsx_xyz,
		temporary(_register_a), temporary(_register_b));
	emit2(context, _rsx_fp_mul, ab_register, _rsx_w, temporary(_register_a), temporary(_register_b));
	emit2(context, (rgb_flags & 0x01) ? _rsx_fp_dp3 : _rsx_fp_mul, cd_register, _rsx_xyz,
		temporary(_register_c), temporary(_register_d));
	emit2(context, _rsx_fp_mul, cd_register, _rsx_w, temporary(_register_c), temporary(_register_d));
	if ((rgb_flags | alpha_flags) & 0x04)
		mux_selector(context);
	for (portion = 0; portion < 2; portion++)
	{
		int mask = portion ? _rsx_w : _rsx_xyz;
		uint32_t flags = portion ? alpha_flags : rgb_flags;

		if (flags & 0x04)
		{
			/* sum = selector ? cd : ab, as ab - ab * s + cd * s: exact for s 0 or 1. (cd - ab) * s + ab can
			miss cd by an ulp, and a later mux that reads such an r0.a of +-0.5 then selects the other side. */
			emit(context, _rsx_fp_mad, sum_register, mask, negated(temporary(ab_register)),
				replicated(temporary(_register_scratch2), 0), temporary(ab_register));
			emit(context, _rsx_fp_mad, sum_register, mask, temporary(cd_register),
				replicated(temporary(_register_scratch2), 0), temporary(sum_register));
		}
		else
		{
			emit2(context, _rsx_fp_add, sum_register, mask, temporary(ab_register), temporary(cd_register));
		}
		output_mapping(context, ab_register, mask, flags);
		output_mapping(context, cd_register, mask, flags);
		output_mapping(context, sum_register, mask, flags);
	}

	/* the writes, after both portions have read their inputs, in nv2a_psh.c's order */
	for (portion = 0; portion < 2; portion++)
	{
		uint32_t outputs = portion ? alpha_outputs : rgb_outputs;
		uint32_t flags = outputs >> 12;
		int mask = portion ? _rsx_w : _rsx_xyz;
		int ab = destination_register((outputs >> 4) & 0x0f);
		int cd = destination_register(outputs & 0x0f);
		int sum = destination_register((outputs >> 8) & 0x0f);

		if (ab >= 0)
		{
			emit1(context, _op_clamp_signed, ab, mask, temporary(ab_register));
			if (!portion && (flags & 0x80))
				emit1(context, _op_clamp_signed, ab, _rsx_w, replicated(temporary(ab_register), 2));
		}
		if (cd >= 0)
		{
			emit1(context, _op_clamp_signed, cd, mask, temporary(cd_register));
			if (!portion && (flags & 0x40))
				emit1(context, _op_clamp_signed, cd, _rsx_w, replicated(temporary(cd_register), 2));
		}
		if (sum >= 0)
			emit1(context, _op_clamp_signed, sum, mask, temporary(sum_register));
	}
}

/* a final combiner input: clamp(x, 0, 1) or its inverse */
static void final_input(struct context *context, uint32_t input, int alpha, int into)
{
	int mask = alpha ? _rsx_w : _rsx_xyz;
	struct rsx_source value = input_channels(register_source(context, input & 0x0f, -1), input, alpha);

	if (input & 0x20)
		saturated(emit2(context, _rsx_fp_add, into, mask, negated(value), fixed(_k_one)));
	else
		saturated(emit1(context, _rsx_fp_mov, into, mask, value));
}

/* the result into register sum */
static int final_combiner(struct context *context)
{
	const struct rsx_fragment_key *key = context->key;
	uint32_t abcd = key->final_inputs_abcd, efg = key->final_inputs_efg;
	uint32_t settings = efg & 0xff;
	struct rsx_instruction *instruction;

	if (!abcd && !efg)
		return _register_r0;
	final_input(context, (efg >> 24) & 0xff, 0, _register_a);
	final_input(context, (efg >> 16) & 0xff, 0, _register_b);
	emit2(context, _rsx_fp_mul, _register_ef_product, _rsx_xyz, temporary(_register_a), temporary(_register_b));
	emit1(context, _rsx_fp_mov, _register_ef_product, _rsx_w, fixed(_k_zero));

	if (settings & 0x40)
		saturated(emit2(context, _rsx_fp_add, _register_c, _rsx_xyz, negated(temporary(_register_v1)), fixed(_k_one)));
	else
		saturated(emit1(context, _rsx_fp_mov, _register_c, _rsx_xyz, temporary(_register_v1)));
	if (settings & 0x20)
		saturated(emit2(context, _rsx_fp_add, _register_d, _rsx_xyz, negated(temporary(_register_r0)), fixed(_k_one)));
	else
		saturated(emit1(context, _rsx_fp_mov, _register_d, _rsx_xyz, temporary(_register_r0)));
	instruction = emit2(context, _rsx_fp_add, _register_v1r0_sum, _rsx_xyz, temporary(_register_c),
		temporary(_register_d));
	if (settings & 0x80)
		instruction->saturate = 1;
	emit1(context, _rsx_fp_mov, _register_v1r0_sum, _rsx_w, fixed(_k_zero));

	final_input(context, (abcd >> 24) & 0xff, 0, _register_a);
	final_input(context, (abcd >> 16) & 0xff, 0, _register_b);
	final_input(context, (abcd >> 8) & 0xff, 0, _register_c);
	final_input(context, abcd & 0xff, 0, _register_d);
	final_input(context, (efg >> 8) & 0xff, 1, _register_ab);

	/* rgb = A*B + (1-A)*C + D = A*(B-C) + C + D; alpha = G */
	emit2(context, _rsx_fp_add, _register_sum, _rsx_xyz, temporary(_register_b), negated(temporary(_register_c)));
	emit(context, _rsx_fp_mad, _register_sum, _rsx_xyz, temporary(_register_a), temporary(_register_sum),
		temporary(_register_c));
	emit2(context, _rsx_fp_add, _register_sum, _rsx_xyz, temporary(_register_sum), temporary(_register_d));
	emit1(context, _rsx_fp_mov, _register_sum, _rsx_w, temporary(_register_ab));
	return _register_sum;
}

/* does any combiner input read the fog register */
static int reads_fog(const struct rsx_fragment_key *key, uint32_t combiner_count)
{
	uint32_t stage, shift;

	for (stage = 0; stage < combiner_count; stage++)
	{
		for (shift = 0; shift < 32; shift += 8)
		{
			if (((key->rgb_inputs[stage] >> shift) & 0x0f) == _input_fog ||
				((key->alpha_inputs[stage] >> shift) & 0x0f) == _input_fog)
				return 1;
		}
	}
	for (shift = 0; shift < 32; shift += 8)
	{
		if (((key->final_inputs_abcd >> shift) & 0x0f) == _input_fog ||
			(shift && ((key->final_inputs_efg >> shift) & 0x0f) == _input_fog))
			return 1;
	}
	return 0;
}

/* the fog register: rgb the fog colour, alpha the fog factor */
static void fog(struct context *context)
{
	const struct rsx_fragment_key *key = context->key;
	struct rsx_source factor = replicated(temporary(_register_scratch), 0);
	struct rsx_source parameters = temporary(_register_scratch2);
	/* exp(x) = 2^(x log2 e) */
	const float log2_e = 1.44269504f;

	load_uniform(context, _register_fog, _rsx_xyz, _rsx_uniform_fog_color);
	if (!key->fog_enable)
	{
		emit1(context, _rsx_fp_mov, _register_fog, _rsx_w, fixed(_k_one));
		return;
	}
	emit1(context, _rsx_fp_mov, _register_scratch, _rsx_xyzw, input(context, _rsx_fp_input_fog));
	load_uniform(context, _register_scratch2, _rsx_xyzw, _rsx_uniform_fog);
	switch (key->fog_table_mode)
	{
	case 1:
		emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, factor, replicated(parameters, 2));
		set_constant(emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, factor, negated(fixed(0))),
			log2_e, 0.0f, 0.0f, 0.0f);
		emit1(context, _rsx_fp_ex2, _register_scratch, _rsx_x, factor);
		break;
	case 2:
		emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, factor, replicated(parameters, 2));
		emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, factor, factor);
		set_constant(emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, factor, negated(fixed(0))),
			log2_e, 0.0f, 0.0f, 0.0f);
		emit1(context, _rsx_fp_ex2, _register_scratch, _rsx_x, factor);
		break;
	case 3:
		/* (end - f) / max(end - start, 1e-6) */
		emit2(context, _rsx_fp_add, _register_scratch, _rsx_y, replicated(parameters, 1), negated(replicated(parameters, 0)));
		set_constant(emit2(context, _rsx_fp_max, _register_scratch, _rsx_y, replicated(temporary(_register_scratch), 1),
			fixed(0)), 1.0e-6f, 0.0f, 0.0f, 0.0f);
		emit1(context, _rsx_fp_rcp, _register_scratch, _rsx_y, replicated(temporary(_register_scratch), 1));
		emit2(context, _rsx_fp_add, _register_scratch, _rsx_x, replicated(parameters, 1), negated(factor));
		emit2(context, _rsx_fp_mul, _register_scratch, _rsx_x, factor, replicated(temporary(_register_scratch), 1));
		break;
	default:
		break;
	}
	saturated(emit1(context, _rsx_fp_mov, _register_fog, _rsx_w, factor));
}

/* ---------- optimization */

/* the translation keeps every NV2A step; this folds the numbers and clamps it can prove, reads copies at
their source, drops what nothing reads and packs the temporaries */

#define TEMPORARIES 64

enum
{
	_value_register,
	_value_number,
};

/* a component's value: a number, or a component of a temporary (at a version), an input or a uniform,
through a negate and an absolute; low and high bound it */
struct value
{
	uint8_t kind;
	uint8_t type;
	uint8_t negate;
	uint8_t absolute;
	uint8_t component;
	uint16_t index;
	uint16_t version;
	float number;
	float low, high;
};

struct operand
{
	struct rsx_source source;
	struct value values[4];
};

/* a value's identity: equal names are equal values */
struct name
{
	uint8_t kind;
	uint8_t type;
	uint8_t flags;
	uint8_t component;
	uint16_t index;
	uint16_t version;
	uint32_t bits;
};

/* the computation of a component: per-component opcodes keep only that component's operands */
struct expression
{
	uint8_t opcode;
	uint8_t saturate;
	uint8_t texture_unit;
	uint8_t component;
	struct name operands[3][4];
};

struct optimizer
{
	const struct rsx_fragment_key *key;
	struct rsx_instruction *instructions;
	uint32_t count;
	uint32_t uniform_reads;
	/* the temporaries the program uses are below this */
	int registers;
	struct value held[TEMPORARIES][4];
	uint16_t versions[TEMPORARIES][4];
	/* what computed each component, at its version */
	struct expression made[TEMPORARIES][4];
	uint16_t made_versions[TEMPORARIES][4];
};

static int source_count(int opcode)
{
	switch (opcode)
	{
	case _rsx_fp_nop:
	case _rsx_fp_kil:
		return 0;
	case _rsx_fp_mov:
	case _rsx_fp_frc:
	case _rsx_fp_flr:
	case _rsx_fp_tex:
	case _rsx_fp_txp:
	case _rsx_fp_rcp:
	case _rsx_fp_ex2:
	case _rsx_fp_lg2:
	case _op_clamp_signed:
		return 1;
	case _rsx_fp_mad:
		return 3;
	default:
		return 2;
	}
}

static int per_component(int opcode)
{
	switch (opcode)
	{
	case _op_clamp_signed:
	case _rsx_fp_mov:
	case _rsx_fp_mul:
	case _rsx_fp_add:
	case _rsx_fp_mad:
	case _rsx_fp_min:
	case _rsx_fp_max:
	case _rsx_fp_slt:
	case _rsx_fp_sge:
	case _rsx_fp_seq:
	case _rsx_fp_frc:
	case _rsx_fp_flr:
		return 1;
	default:
		return 0;
	}
}

/* the components the sources are read in */
static int read_lanes(const struct rsx_fragment_key *key, const struct rsx_instruction *instruction)
{
	int planar = key->sampler_type[instruction->texture_unit & 3] == _rsx_sampler_2d;

	switch (instruction->opcode)
	{
	case _rsx_fp_nop:
	case _rsx_fp_kil:
		return 0;
	case _rsx_fp_dp3:
		return _rsx_xyz;
	case _rsx_fp_dp4:
		return _rsx_xyzw;
	case _rsx_fp_rcp:
	case _rsx_fp_ex2:
	case _rsx_fp_lg2:
		return _rsx_x;
	/* s t (2D) or s t r, and q to project */
	case _rsx_fp_tex:
		return planar ? _rsx_x | _rsx_y : _rsx_xyz;
	case _rsx_fp_txp:
		return planar ? _rsx_x | _rsx_y | _rsx_w : _rsx_xyzw;
	default:
		return instruction->mask;
	}
}

static int lowest_lane(int lanes)
{
	int lane;

	for (lane = 0; lane < 3 && !(lanes & (1 << lane)); lane++)
		;
	return lane;
}

/* the components no source is read in copy one that is read: no read of a component never written */
static void tidy(const struct rsx_fragment_key *key, struct rsx_instruction *instruction)
{
	int lanes = read_lanes(key, instruction), first = lowest_lane(lanes), position, lane;

	if (!lanes)
		return;
	for (position = 0; position < source_count(instruction->opcode); position++)
	{
		for (lane = 0; lane < 4; lane++)
		{
			if (!(lanes & (1 << lane)))
				instruction->source[position].swizzle[lane] = instruction->source[position].swizzle[first];
		}
	}
}

/* ---------- values */

static struct value number_value(float x)
{
	struct value value;

	memset(&value, 0, sizeof(value));
	value.kind = _value_number;
	value.number = x;
	value.low = x;
	value.high = x;
	return value;
}

static struct value register_value(int type, int index, int component, int version)
{
	struct value value;

	memset(&value, 0, sizeof(value));
	value.kind = _value_register;
	value.type = (uint8_t)type;
	value.index = (uint16_t)index;
	value.component = (uint8_t)component;
	value.version = (uint16_t)version;
	value.low = -INFINITY;
	value.high = INFINITY;
	return value;
}

static int is_number(const struct value *value, float x)
{
	return value->kind == _value_number && value->number == x;
}

static int same_value(const struct value *a, const struct value *b)
{
	if (a->kind != b->kind)
		return 0;
	if (a->kind == _value_number)
		return !memcmp(&a->number, &b->number, sizeof(a->number));
	return a->type == b->type && a->index == b->index && a->component == b->component && a->negate == b->negate &&
		a->absolute == b->absolute && (a->type != _rsx_register_temporary || a->version == b->version);
}

static float clamp01(float x)
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

/* the value through a source's absolute, then its negate */
static struct value modified(struct value value, int negate, int absolute)
{
	float low = value.low, high = value.high, top;

	if (absolute)
	{
		if (value.kind == _value_number)
			value.number = fabsf(value.number);
		value.absolute = value.kind == _value_register;
		value.negate = 0;
		if (high <= 0.0f)
		{
			top = -low;
			low = -high;
			high = top;
		}
		else if (low < 0.0f)
		{
			high = -low > high ? -low : high;
			low = 0.0f;
		}
	}
	if (negate)
	{
		if (value.kind == _value_number)
			value.number = -value.number;
		else
			value.negate = (uint8_t)!value.negate;
		top = -low;
		low = -high;
		high = top;
	}
	value.low = low;
	value.high = high;
	return value;
}

/* what a temporary's component holds now */
static struct value held_value(const struct optimizer *o, int index, int component)
{
	struct value value = o->held[index][component];

	/* the register it copied has changed since: the value is only here now */
	if (value.kind == _value_register && value.type == _rsx_register_temporary &&
		o->versions[value.index][value.component] != value.version)
	{
		struct value own = register_value(_rsx_register_temporary, index, component, o->versions[index][component]);

		own.low = value.low;
		own.high = value.high;
		return own;
	}
	return value;
}

static struct value resolve(const struct optimizer *o, const struct rsx_instruction *instruction,
	const struct rsx_source *source, int lane)
{
	int component = source->swizzle[lane] & 3;
	struct value value;

	if (source->type == _rsx_register_temporary)
	{
		value = held_value(o, source->index & (TEMPORARIES - 1), component);
	}
	else if (source->type == _rsx_register_constant && instruction->uniform == RSX_NO_UNIFORM)
	{
		value = number_value(instruction->constant[component]);
	}
	else
	{
		value = register_value(source->type, source->type == _rsx_register_constant ? instruction->uniform :
			source->index, component, 0);
		/* the vertex programs saturate their colours; the colour uniforms are 0..1 */
		if ((source->type == _rsx_register_input &&
			(source->index == _rsx_fp_input_color0 || source->index == _rsx_fp_input_color1)) ||
			(source->type == _rsx_register_constant && instruction->uniform < _rsx_uniform_fog))
		{
			value.low = 0.0f;
			value.high = 1.0f;
		}
	}
	return modified(value, source->negate, source->absolute);
}

static void negate_operand(struct operand *operand)
{
	int lane;

	operand->source.negate = (uint8_t)!operand->source.negate;
	for (lane = 0; lane < 4; lane++)
		operand->values[lane] = modified(operand->values[lane], 1, 0);
}

static int all_number(const struct operand *operand, int lanes, float x)
{
	int lane;

	for (lane = 0; lane < 4; lane++)
	{
		if ((lanes & (1 << lane)) && !is_number(&operand->values[lane], x))
			return 0;
	}
	return 1;
}

static int all_numbers(const struct operand *operand, int lanes)
{
	int lane;

	for (lane = 0; lane < 4; lane++)
	{
		if ((lanes & (1 << lane)) && operand->values[lane].kind != _value_number)
			return 0;
	}
	return 1;
}

static float fold(int opcode, float a, float b, float c)
{
	switch (opcode)
	{
	case _rsx_fp_mul: return a * b;
	case _rsx_fp_add: return a + b;
	case _rsx_fp_mad: return a * b + c;
	case _rsx_fp_min: return a < b ? a : b;
	case _rsx_fp_max: return a > b ? a : b;
	case _rsx_fp_slt: return a < b ? 1.0f : 0.0f;
	case _rsx_fp_sge: return a >= b ? 1.0f : 0.0f;
	case _rsx_fp_seq: return a == b ? 1.0f : 0.0f;
	case _rsx_fp_frc: return a - floorf(a);
	case _rsx_fp_flr: return floorf(a);
	case _op_clamp_signed: return a < -1.0f ? -1.0f : a > 1.0f ? 1.0f : a;
	default: return a;
	}
}

static void product_bounds(const struct value *a, const struct value *b, float *low, float *high)
{
	float products[4];
	int index;

	products[0] = a->low * b->low;
	products[1] = a->low * b->high;
	products[2] = a->high * b->low;
	products[3] = a->high * b->high;
	*low = INFINITY;
	*high = -INFINITY;
	for (index = 0; index < 4; index++)
	{
		/* 0 * infinity */
		if (products[index] != products[index])
		{
			*low = -INFINITY;
			*high = INFINITY;
			return;
		}
		*low = products[index] < *low ? products[index] : *low;
		*high = products[index] > *high ? products[index] : *high;
	}
}

/* the bounds of a component of the result, before the saturate */
static void bounds(int opcode, const struct operand *operands, int lane, float *low, float *high)
{
	const struct value *a = &operands[0].values[lane], *b = &operands[1].values[lane];
	const struct value *c = &operands[2].values[lane], *x = &operands[0].values[0];
	float l, h;
	int index;

	*low = -INFINITY;
	*high = INFINITY;
	switch (opcode)
	{
	case _rsx_fp_mov:
		*low = a->low;
		*high = a->high;
		break;
	case _op_clamp_signed:
		*low = a->low > -1.0f ? (a->low < 1.0f ? a->low : 1.0f) : -1.0f;
		*high = a->high < 1.0f ? (a->high > -1.0f ? a->high : -1.0f) : 1.0f;
		break;
	case _rsx_fp_mul:
		product_bounds(a, b, low, high);
		break;
	case _rsx_fp_add:
		*low = a->low + b->low;
		*high = a->high + b->high;
		break;
	case _rsx_fp_mad:
		product_bounds(a, b, &l, &h);
		*low = l + c->low;
		*high = h + c->high;
		break;
	case _rsx_fp_dp3:
	case _rsx_fp_dp4:
		*low = *high = 0.0f;
		for (index = 0; index < (opcode == _rsx_fp_dp3 ? 3 : 4); index++)
		{
			product_bounds(&operands[0].values[index], &operands[1].values[index], &l, &h);
			*low += l;
			*high += h;
		}
		break;
	case _rsx_fp_min:
		*low = a->low < b->low ? a->low : b->low;
		*high = a->high < b->high ? a->high : b->high;
		break;
	case _rsx_fp_max:
		*low = a->low > b->low ? a->low : b->low;
		*high = a->high > b->high ? a->high : b->high;
		break;
	case _rsx_fp_slt:
	case _rsx_fp_sge:
	case _rsx_fp_seq:
	case _rsx_fp_frc:
	case _rsx_fp_tex:
	case _rsx_fp_txp:
		*low = 0.0f;
		*high = 1.0f;
		break;
	case _rsx_fp_flr:
		*low = floorf(a->low);
		*high = floorf(a->high);
		break;
	case _rsx_fp_rcp:
		if (x->low > 0.0f || x->high < 0.0f)
		{
			*low = 1.0f / x->high;
			*high = 1.0f / x->low;
		}
		break;
	case _rsx_fp_ex2:
		*low = exp2f(x->low);
		*high = exp2f(x->high);
		break;
	case _rsx_fp_lg2:
		if (x->low > 0.0f)
		{
			*low = log2f(x->low);
			*high = log2f(x->high);
		}
		break;
	default:
		break;
	}
	if (*low != *low)
		*low = -INFINITY;
	if (*high != *high)
		*high = INFINITY;
}

/* each component's result as a copy of an operand, a number or an operation; then the instruction as
the MOV its components agree on */
static void simplify(struct rsx_instruction *instruction, struct operand operands[3], int *count)
{
	int lanes = instruction->mask, lane, copy = -2, flip = 0, numbers = 1;
	float results[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (instruction->opcode == _rsx_fp_mad)
	{
		int which;

		/* a*b + 0 is MUL; 1*b + c and -1*b + c are ADD */
		if (all_number(&operands[2], lanes, 0.0f))
		{
			instruction->opcode = _rsx_fp_mul;
			*count = 2;
		}
		for (which = 0; which < 2 && instruction->opcode == _rsx_fp_mad; which++)
		{
			int minus = all_number(&operands[which], lanes, -1.0f);

			if (!minus && !all_number(&operands[which], lanes, 1.0f))
				continue;
			operands[0] = operands[1 - which];
			if (minus)
				negate_operand(&operands[0]);
			operands[1] = operands[2];
			instruction->opcode = _rsx_fp_add;
			*count = 2;
		}
	}
	for (lane = 0; lane < 4; lane++)
	{
		const struct value *a = &operands[0].values[lane], *b = &operands[1].values[lane];
		const struct value *c = &operands[2].values[lane];
		int outcome = -1, lane_flip = 0, position, constant = 1;
		float result = 0.0f;

		if (!(lanes & (1 << lane)))
			continue;
		for (position = 0; position < *count; position++)
			constant &= operands[position].values[lane].kind == _value_number;
		if (constant)
		{
			outcome = 3;
			result = fold(instruction->opcode, a->number, b->number, c->number);
		}
		else
		{
			switch (instruction->opcode)
			{
			case _rsx_fp_mov:
				outcome = 0;
				break;
			case _op_clamp_signed:
				if (a->low >= -1.0f && a->high <= 1.0f)
					outcome = 0;
				break;
			case _rsx_fp_mul:
				if (is_number(a, 0.0f) || is_number(b, 0.0f))
					outcome = 3;
				else if (is_number(a, 1.0f) || is_number(a, -1.0f))
					outcome = 1, lane_flip = a->number < 0.0f;
				else if (is_number(b, 1.0f) || is_number(b, -1.0f))
					outcome = 0, lane_flip = b->number < 0.0f;
				break;
			case _rsx_fp_add:
				if (is_number(a, 0.0f))
					outcome = 1;
				else if (is_number(b, 0.0f))
					outcome = 0;
				break;
			case _rsx_fp_mad:
				if (is_number(a, 0.0f) || is_number(b, 0.0f))
					outcome = 2;
				break;
			/* saturated, min(x, k >= 1) and max(x, k <= 0) are x */
			case _rsx_fp_min:
				if (instruction->saturate && a->kind == _value_number && a->number >= 1.0f)
					outcome = 1;
				else if (instruction->saturate && b->kind == _value_number && b->number >= 1.0f)
					outcome = 0;
				else if (a->high <= b->low)
					outcome = 0;
				else if (b->high <= a->low)
					outcome = 1;
				break;
			case _rsx_fp_max:
				if (instruction->saturate && a->kind == _value_number && a->number <= 0.0f)
					outcome = 1;
				else if (instruction->saturate && b->kind == _value_number && b->number <= 0.0f)
					outcome = 0;
				else if (a->low >= b->high)
					outcome = 0;
				else if (b->low >= a->high)
					outcome = 1;
				break;
			case _rsx_fp_slt:
				if (a->high < b->low || a->low >= b->high)
					outcome = 3, result = a->high < b->low ? 1.0f : 0.0f;
				break;
			case _rsx_fp_sge:
				if (a->low >= b->high || a->high < b->low)
					outcome = 3, result = a->low >= b->high ? 1.0f : 0.0f;
				break;
			default:
				break;
			}
			if (outcome >= 0 && outcome < 3 && operands[outcome].values[lane].kind == _value_number)
			{
				result = operands[outcome].values[lane].number * (lane_flip ? -1.0f : 1.0f);
				outcome = 3;
			}
		}
		if (outcome == 3)
		{
			results[lane] = instruction->saturate ? clamp01(result) : result;
			copy = -1;
			continue;
		}
		numbers = 0;
		if (outcome < 0 || (copy != -2 && (outcome != copy || lane_flip != flip)))
			copy = -1;
		else if (copy == -2)
			copy = outcome, flip = lane_flip;
	}
	if (numbers)
	{
		instruction->opcode = _rsx_fp_mov;
		instruction->saturate = 0;
		operands[0].source = fixed(0);
		for (lane = 0; lane < 4; lane++)
			operands[0].values[lane] = number_value(results[lane]);
		*count = 1;
	}
	else if (copy >= 0)
	{
		operands[0] = operands[copy];
		if (flip)
			negate_operand(&operands[0]);
		instruction->opcode = _rsx_fp_mov;
		*count = 1;
	}
}

/* ---------- encoding the operands */

/* the source that reads the values, if they are one register's components */
static int value_source(struct rsx_source *source, const struct value values[4], int lanes)
{
	const struct value *first = &values[lowest_lane(lanes)];
	int lane;

	for (lane = 0; lane < 4; lane++)
	{
		const struct value *value = &values[lane];

		if (!(lanes & (1 << lane)))
			continue;
		if (value->kind != _value_register || value->type != first->type || value->index != first->index ||
			value->negate != first->negate || value->absolute != first->absolute)
			return 0;
	}
	*source = source_none();
	source->type = first->type;
	source->index = first->index;
	source->negate = first->negate;
	source->absolute = first->absolute;
	for (lane = 0; lane < 4; lane++)
		source->swizzle[lane] = (lanes & (1 << lane)) ? values[lane].component : first->component;
	return 1;
}

static int slot_find(const float slot[4], int used, float x)
{
	int index;

	for (index = 0; index < used; index++)
	{
		if (!memcmp(&slot[index], &x, sizeof(x)))
			return index;
	}
	return -1;
}

/* a source reading numbers from the instruction's constant, as they are or negated, sharing what is
there; 0 if they do not fit */
static int place_numbers(float slot[4], int *used, struct rsx_source *source, const struct value values[4],
	int lanes)
{
	float trial[2][4];
	int counts[2], negate, lane, choice;

	for (negate = 0; negate < 2; negate++)
	{
		memcpy(trial[negate], slot, sizeof(trial[negate]));
		counts[negate] = *used;
		for (lane = 0; lane < 4 && counts[negate] <= 4; lane++)
		{
			float x = negate ? -values[lane].number : values[lane].number;

			if (!(lanes & (1 << lane)) || slot_find(trial[negate], counts[negate], x) >= 0)
				continue;
			if (counts[negate] < 4)
				trial[negate][counts[negate]] = x;
			counts[negate]++;
		}
	}
	choice = counts[1] < counts[0];
	if (counts[choice] > 4)
		return 0;
	memcpy(slot, trial[choice], sizeof(trial[choice]));
	*used = counts[choice];
	*source = source_none();
	source->type = _rsx_register_constant;
	source->negate = (uint8_t)choice;
	for (lane = 0; lane < 4; lane++)
	{
		const struct value *value = &values[(lanes & (1 << lane)) ? lane : lowest_lane(lanes)];

		source->swizzle[lane] = (uint8_t)slot_find(slot, *used, choice ? -value->number : value->number);
	}
	return 1;
}

/* the operands read from their values where the RSX allows it: one input register, which only MOV and the
lookups read as a texture coordinate (perspective), and one constant, numbers or a single uniform */
static int encode(struct optimizer *o, struct rsx_instruction *instruction, const struct operand *operands,
	int count)
{
	static const uint8_t choices[] = { 7, 6, 5, 3, 4, 2, 1, 0 };
	int lanes = read_lanes(o->key, instruction), had = instruction->uniform != RSX_NO_UNIFORM &&
		rsx_fragment_reads_constant(instruction), registers = 0, position;
	uint32_t pick;

	/* the operands that can keep the source they had; the others are always read from their values */
	for (position = 0; position < count; position++)
	{
		if (operands[position].source.type == _rsx_register_temporary ||
			operands[position].source.type == _rsx_register_input)
			registers |= 1 << position;
	}
	for (pick = 0; pick < sizeof(choices); pick++)
	{
		struct rsx_instruction trial;
		int used = 0, input = -1, fits = 1;

		if (choices[pick] >= (1 << count) || (~choices[pick] & ~registers & ((1 << count) - 1)))
			continue;
		trial = *instruction;
		trial.uniform = RSX_NO_UNIFORM;
		memset(trial.constant, 0, sizeof(trial.constant));
		for (position = 0; position < 3; position++)
		{
			const struct operand *operand = &operands[position];
			struct rsx_source *source = &trial.source[position];

			if (position >= count || !fits)
			{
				*source = source_none();
				continue;
			}
			if (!((choices[pick] >> position) & 1))
			{
				*source = operand->source;
			}
			else if (!lanes)
			{
				fits = 0;
			}
			else if (all_numbers(operand, lanes))
			{
				fits = place_numbers(trial.constant, &used, source, operand->values, lanes);
			}
			else
			{
				fits = value_source(source, operand->values, lanes);
				if (fits && source->type == _rsx_register_constant)
				{
					fits = trial.uniform == RSX_NO_UNIFORM || trial.uniform == source->index;
					trial.uniform = (uint8_t)source->index;
				}
			}
			if (fits && source->type == _rsx_register_input)
			{
				fits = (input < 0 || input == source->index) && (source->index < _rsx_fp_input_texture0 ||
					trial.opcode == _rsx_fp_mov || trial.opcode == _rsx_fp_tex || trial.opcode == _rsx_fp_txp);
				input = source->index;
			}
		}
		if (!fits || (trial.uniform != RSX_NO_UNIFORM && used))
			continue;
		if (trial.uniform != RSX_NO_UNIFORM && !had && o->uniform_reads >= RSX_FP_MAXIMUM_UNIFORMS)
			continue;
		o->uniform_reads += (trial.uniform != RSX_NO_UNIFORM) - had;
		tidy(o->key, &trial);
		*instruction = trial;
		return 1;
	}
	return 0;
}

/* ---------- passes */

static int same_source(const struct rsx_source *a, const struct rsx_source *b)
{
	return a->type == b->type && a->negate == b->negate && a->absolute == b->absolute && a->index == b->index &&
		!memcmp(a->swizzle, b->swizzle, sizeof(a->swizzle));
}

static void name_of(struct name *name, const struct value *value)
{
	memset(name, 0, sizeof(*name));
	name->kind = value->kind;
	if (value->kind == _value_number)
	{
		memcpy(&name->bits, &value->number, sizeof(name->bits));
		return;
	}
	name->type = value->type;
	name->flags = (uint8_t)(value->negate | (value->absolute << 1));
	name->component = value->component;
	name->index = value->index;
	name->version = value->type == _rsx_register_temporary ? value->version : 0;
}

static void expression_of(struct expression *expression, const struct rsx_instruction *instruction,
	const struct operand *operands, int count, int lanes, int lane)
{
	int position, read;

	memset(expression, 0, sizeof(*expression));
	expression->opcode = instruction->opcode;
	expression->saturate = instruction->saturate;
	/* a lookup's components are the texel's */
	if (instruction->opcode == _rsx_fp_tex || instruction->opcode == _rsx_fp_txp)
	{
		expression->texture_unit = instruction->texture_unit;
		expression->component = (uint8_t)lane;
	}
	for (position = 0; position < count; position++)
	{
		if (per_component(instruction->opcode))
		{
			name_of(&expression->operands[position][0], &operands[position].values[lane]);
			continue;
		}
		for (read = 0; read < 4; read++)
		{
			if (lanes & (1 << read))
				name_of(&expression->operands[position][read], &operands[position].values[read]);
		}
	}
}

/* a component that holds the expression's value still */
static int find_expression(const struct optimizer *o, const struct expression *expression, int *reg, int *lane)
{
	for (*reg = 0; *reg < o->registers; (*reg)++)
	{
		for (*lane = 0; *lane < 4; (*lane)++)
		{
			const struct expression *made = &o->made[*reg][*lane];

			if (made->opcode == expression->opcode && o->made_versions[*reg][*lane] == o->versions[*reg][*lane] &&
				!memcmp(made, expression, sizeof(*made)))
				return 1;
		}
	}
	return 0;
}

static int same_instruction(const struct rsx_instruction *a, const struct rsx_instruction *b)
{
	int position;

	if (a->opcode != b->opcode || a->destination_type != b->destination_type || a->destination != b->destination ||
		a->mask != b->mask || a->saturate != b->saturate || a->condition_update != b->condition_update ||
		a->uniform != b->uniform || memcmp(a->constant, b->constant, sizeof(a->constant)))
		return 0;
	for (position = 0; position < 3; position++)
	{
		if (!same_source(&a->source[position], &b->source[position]))
			return 0;
	}
	return 1;
}

/* ---------- reading what made a value */

/* the value a name gives, if it is still to be read: a temporary at its version, an input, a uniform, a number */
static int named_value(const struct optimizer *o, const struct name *name, struct value *value)
{
	if (name->kind == _value_number)
	{
		float x;

		memcpy(&x, &name->bits, sizeof(x));
		*value = number_value(x);
		return 1;
	}
	if (name->type == _rsx_register_temporary)
	{
		int index = name->index & (TEMPORARIES - 1), component = name->component & 3;

		if (o->versions[index][component] != name->version)
			return 0;
		*value = held_value(o, index, component);
	}
	else
	{
		*value = register_value(name->type, name->index, name->component, 0);
		/* as resolve: the colours and the colour uniforms are 0..1 */
		if ((name->type == _rsx_register_input &&
			(name->index == _rsx_fp_input_color0 || name->index == _rsx_fp_input_color1)) ||
			(name->type == _rsx_register_constant && name->index < _rsx_uniform_fog))
		{
			value->low = 0.0f;
			value->high = 1.0f;
		}
	}
	*value = modified(*value, name->flags & 1, (name->flags >> 1) & 1);
	return 1;
}

/* what made a value, if it is a per-component instruction's own result, unsaturated, and still held */
static const struct expression *value_maker(const struct optimizer *o, const struct value *value)
{
	int index = value->index & (TEMPORARIES - 1), component = value->component & 3;
	const struct expression *made = &o->made[index][component];

	if (value->kind != _value_register || value->type != _rsx_register_temporary || value->absolute ||
		o->versions[index][component] != value->version || o->made_versions[index][component] != value->version ||
		made->saturate || !per_component(made->opcode))
		return NULL;
	return made;
}

/* the value a clamp to [-1, 1] took, if a clamp made this one (through its negate) and that value is still held */
static int clamped_base(const struct optimizer *o, const struct value *value, struct value *base)
{
	const struct expression *made = value_maker(o, value);

	if (!made || made->opcode != _op_clamp_signed || !named_value(o, &made->operands[0][0], base))
		return 0;
	*base = modified(*base, value->negate, 0);
	return 1;
}

/* reads through the clamps to [-1, 1] the instruction makes redundant: a saturated MOV; a saturated ADD whose other
operand is 0..1; a saturated MIN or MAX whose other is -1..1; MAX(t, 0..1) as MAX_SAT(x, 0..1) */
static void clamp_through(const struct optimizer *o, struct rsx_instruction *instruction, struct operand operands[3],
	int count)
{
	int lanes = instruction->mask, position, lane;

	for (position = 0; position < count && count <= 2; position++)
	{
		struct value bases[4];
		int through = 0, all = 1;

		for (lane = 0; lane < 4; lane++)
		{
			const struct value *other = &operands[1 - position].values[lane];
			int fits;

			if (!(lanes & (1 << lane)))
				continue;
			fits = clamped_base(o, &operands[position].values[lane], &bases[lane]);
			switch (instruction->opcode)
			{
			case _rsx_fp_mov:
				fits = fits && instruction->saturate;
				break;
			case _rsx_fp_add:
				fits = fits && instruction->saturate && other->low >= 0.0f && other->high <= 1.0f;
				break;
			case _rsx_fp_min:
				fits = fits && instruction->saturate && other->low >= -1.0f && other->high <= 1.0f;
				break;
			case _rsx_fp_max:
				fits = fits && (instruction->saturate ? other->low >= -1.0f && other->high <= 1.0f :
					other->low >= 0.0f && other->high <= 1.0f);
				break;
			default:
				fits = 0;
				break;
			}
			if (fits)
				through |= 1 << lane;
			else
				all = 0;
		}
		/* max(clamp(x), y) for y 0..1 is clamp(max(x, y), 0, 1): every component saturated then */
		if (!through || (instruction->opcode == _rsx_fp_max && !instruction->saturate && !all))
			continue;
		instruction->saturate = 1;
		for (lane = 0; lane < 4; lane++)
		{
			if (through & (1 << lane))
				operands[position].values[lane] = bases[lane];
		}
		operands[position].source = source_none();
	}
}

/* t's maker as x * m + n with numbers m and n: a MUL, ADD or MAD of numbers and one value */
static int linear_maker(const struct optimizer *o, const struct value *t, struct value *x, float *m, float *n)
{
	const struct expression *made = value_maker(o, t);
	struct value values[3];
	int position, count, variable = -1;

	if (!made || (made->opcode != _rsx_fp_mul && made->opcode != _rsx_fp_add && made->opcode != _rsx_fp_mad))
		return 0;
	count = source_count(made->opcode);
	for (position = 0; position < count; position++)
	{
		if (!named_value(o, &made->operands[position][0], &values[position]))
			return 0;
		if (values[position].kind != _value_number)
		{
			if (variable >= 0)
				return 0;
			variable = position;
		}
	}
	if (variable < 0)
		return 0;
	*x = values[variable];
	switch (made->opcode)
	{
	case _rsx_fp_mul:
		*m = values[1 - variable].number;
		*n = 0.0f;
		break;
	case _rsx_fp_add:
		*m = 1.0f;
		*n = values[1 - variable].number;
		break;
	default:
		if (variable == 2)
			return 0;
		*m = values[1 - variable].number;
		*n = values[2].number;
		break;
	}
	/* the reader's negate of t */
	if (t->negate)
	{
		*m = -*m;
		*n = -*n;
	}
	return 1;
}

static void numbers_operand(struct operand *operand, const float numbers[4])
{
	int lane;

	operand->source = source_none();
	for (lane = 0; lane < 4; lane++)
		operand->values[lane] = number_value(numbers[lane]);
}

/* the instruction from what made an operand: an ADD of a product as a MAD of its factors; numbers times or plus a
value numbers made from another as one MAD of that; (x + 1) * b as a MAD of x, b and b. 1 if it changed */
static int combine(const struct optimizer *o, struct rsx_instruction *instruction, struct operand operands[3], int *count)
{
	int lanes = instruction->mask, position, lane, fits;
	struct operand result[3];

	if (!lanes || (instruction->opcode != _rsx_fp_add && instruction->opcode != _rsx_fp_mul &&
		instruction->opcode != _rsx_fp_mad))
		return 0;
	memset(result, 0, sizeof(result));
	/* t + t is t * 2 */
	if (instruction->opcode == _rsx_fp_add)
	{
		for (lane = 0, fits = 1; lane < 4 && fits; lane++)
			fits = !(lanes & (1 << lane)) || same_value(&operands[0].values[lane], &operands[1].values[lane]);
		if (fits)
		{
			const float two[4] = { 2.0f, 2.0f, 2.0f, 2.0f };

			instruction->opcode = _rsx_fp_mul;
			numbers_operand(&operands[1], two);
			return 1;
		}
	}
	/* numbers and one value t = x * m + n: x * (m * p) + (n * p + q) */
	for (position = 0; position < *count; position++)
	{
		float multiplier[4], addend[4];
		int other;

		for (lane = 0, fits = 1; lane < 4 && fits; lane++)
		{
			float m, n, p = 1.0f, q = 0.0f;

			if (!(lanes & (1 << lane)))
			{
				multiplier[lane] = 1.0f;
				addend[lane] = 0.0f;
				result[0].values[lane] = number_value(0.0f);
				continue;
			}
			for (other = 0; other < *count && fits; other++)
				fits = other == position || operands[other].values[lane].kind == _value_number;
			if (!fits || !linear_maker(o, &operands[position].values[lane], &result[0].values[lane], &m, &n))
			{
				fits = 0;
				break;
			}
			switch (instruction->opcode)
			{
			case _rsx_fp_mul:
				p = operands[1 - position].values[lane].number;
				break;
			case _rsx_fp_add:
				q = operands[1 - position].values[lane].number;
				break;
			default:
				if (position == 2)
					q = operands[0].values[lane].number * operands[1].values[lane].number;
				else
				{
					p = operands[1 - position].values[lane].number;
					q = operands[2].values[lane].number;
				}
				break;
			}
			multiplier[lane] = m * p;
			addend[lane] = n * p + q;
		}
		if (!fits)
			continue;
		result[0].source = source_none();
		numbers_operand(&result[1], multiplier);
		numbers_operand(&result[2], addend);
		memcpy(operands, result, sizeof(result));
		instruction->opcode = _rsx_fp_mad;
		*count = 3;
		return 1;
	}
	/* (x + n) * b with n +-1: x * b + n * b */
	if (instruction->opcode == _rsx_fp_mul)
	{
		for (position = 0; position < 2; position++)
		{
			for (lane = 0, fits = 1; lane < 4 && fits; lane++)
			{
				float m, n;

				if (!(lanes & (1 << lane)))
					continue;
				fits = linear_maker(o, &operands[position].values[lane], &result[0].values[lane], &m, &n) &&
					(m == 1.0f || m == -1.0f) && (n == 1.0f || n == -1.0f);
				if (!fits)
					break;
				result[0].values[lane] = modified(result[0].values[lane], m < 0.0f, 0);
				result[1].values[lane] = operands[1 - position].values[lane];
				result[2].values[lane] = modified(operands[1 - position].values[lane], n < 0.0f, 0);
			}
			if (!fits)
				continue;
			for (lane = 0; lane < 3; lane++)
				result[lane].source = source_none();
			memcpy(operands, result, sizeof(result));
			instruction->opcode = _rsx_fp_mad;
			*count = 3;
			return 1;
		}
		return 0;
	}
	/* t + y with t a product: the factors' MAD */
	if (instruction->opcode == _rsx_fp_add)
	{
		for (position = 0; position < 2; position++)
		{
			for (lane = 0, fits = 1; lane < 4 && fits; lane++)
			{
				const struct value *t = &operands[position].values[lane];
				const struct expression *made;

				if (!(lanes & (1 << lane)))
					continue;
				made = value_maker(o, t);
				fits = made && made->opcode == _rsx_fp_mul &&
					named_value(o, &made->operands[0][0], &result[0].values[lane]) &&
					named_value(o, &made->operands[1][0], &result[1].values[lane]);
				if (!fits)
					break;
				result[0].values[lane] = modified(result[0].values[lane], t->negate, 0);
				result[2].values[lane] = operands[1 - position].values[lane];
			}
			if (!fits)
				continue;
			result[0].source = source_none();
			result[1].source = source_none();
			result[2].source = operands[1 - position].source;
			memcpy(operands, result, sizeof(result));
			instruction->opcode = _rsx_fp_mad;
			*count = 3;
			return 1;
		}
	}
	return 0;
}

/* one instruction with what is known before it: simpler, its sources read at their origin */
static int forward_instruction(struct optimizer *o, struct rsx_instruction *instruction)
{
	struct rsx_instruction result = *instruction;
	struct operand operands[3];
	struct value values[4];
	struct expression expressions[4];
	float low[4], high[4];
	int count = source_count(instruction->opcode), lanes = read_lanes(o->key, instruction), position, lane;
	int destination = instruction->destination & (TEMPORARIES - 1), copy, keyed, own = 0, saturated = 0, changed;

	if (!count)
		return 0;
	memset(operands, 0, sizeof(operands));
	memset(expressions, 0, sizeof(expressions));
	for (position = 0; position < count; position++)
	{
		operands[position].source = instruction->source[position];
		for (lane = 0; lane < 4; lane++)
		{
			if (lanes & (1 << lane))
				operands[position].values[lane] = resolve(o, instruction, &instruction->source[position], lane);
		}
	}
	if (per_component(result.opcode))
	{
		clamp_through(o, &result, operands, count);
		simplify(&result, operands, &count);
		if (combine(o, &result, operands, &count))
			simplify(&result, operands, &count);
	}
	for (lane = 0; lane < 4; lane++)
	{
		if (!(result.mask & (1 << lane)))
			continue;
		bounds(result.opcode, operands, lane, &low[lane], &high[lane]);
		saturated |= low[lane] < 0.0f || high[lane] > 1.0f;
	}
	/* no saturate where the result is 0..1 anyway */
	result.saturate = (uint8_t)(result.saturate && saturated);
	copy = result.opcode == _rsx_fp_mov && !result.saturate;
	keyed = !copy;
	for (lane = 0; lane < 4; lane++)
	{
		int reg, component;

		if (!(result.mask & (1 << lane)))
			continue;
		if (result.saturate)
		{
			low[lane] = clamp01(low[lane]);
			high[lane] = clamp01(high[lane]);
		}
		values[lane] = copy ? operands[0].values[lane] : register_value(_rsx_register_temporary, destination, lane, 0);
		values[lane].low = low[lane];
		values[lane].high = high[lane];
		if (copy)
			continue;
		/* computed before: a copy of that */
		expression_of(&expressions[lane], &result, operands, count, read_lanes(o->key, &result), lane);
		if (find_expression(o, &expressions[lane], &reg, &component))
			values[lane] = held_value(o, reg, component);
		else
			own |= 1 << lane;
	}
	if (keyed && !own)
	{
		result.opcode = _rsx_fp_mov;
		result.saturate = 0;
		count = 1;
		operands[0].source = fixed(0);
		memcpy(operands[0].values, values, sizeof(values));
		copy = 1;
	}
	/* components that would get what they hold already */
	if (copy && result.destination_type == _rsx_register_temporary && !result.condition_update)
	{
		for (lane = 0; lane < 4; lane++)
		{
			struct value now = held_value(o, destination, lane);

			if ((result.mask & (1 << lane)) && same_value(&now, &values[lane]))
				result.mask &= (uint8_t)~(1 << lane);
		}
		if (!result.mask)
		{
			/* eliminate() drops it */
			instruction->mask = 0;
			return 1;
		}
	}
	/* if nothing fits, the instruction as it was makes the same values */
	if (!encode(o, &result, operands, count))
		result = *instruction;
	if (result.destination_type == _rsx_register_temporary)
	{
		for (lane = 0; lane < 4; lane++)
		{
			if (!(result.mask & (1 << lane)))
				continue;
			o->versions[destination][lane]++;
			if (own & (1 << lane))
				values[lane].version = o->versions[destination][lane];
			o->held[destination][lane] = values[lane];
			o->made[destination][lane] = expressions[lane];
			o->made[destination][lane].opcode = keyed ? expressions[lane].opcode : _rsx_fp_nop;
			o->made_versions[destination][lane] = o->versions[destination][lane];
		}
	}
	changed = !same_instruction(&result, instruction);
	*instruction = result;
	return changed;
}

static int forward(struct optimizer *o)
{
	uint32_t index;
	int reg, lane, changed = 0;

	memset(o->made, 0, sizeof(o->made));
	memset(o->made_versions, 0, sizeof(o->made_versions));
	for (reg = 0; reg < TEMPORARIES; reg++)
	{
		for (lane = 0; lane < 4; lane++)
		{
			o->held[reg][lane] = register_value(_rsx_register_temporary, reg, lane, 0);
			o->versions[reg][lane] = 0;
		}
	}
	o->uniform_reads = 0;
	for (index = 0; index < o->count; index++)
	{
		o->uniform_reads += o->instructions[index].uniform != RSX_NO_UNIFORM &&
			rsx_fragment_reads_constant(&o->instructions[index]);
	}
	for (index = 0; index < o->count; index++)
		changed |= forward_instruction(o, &o->instructions[index]);
	return changed;
}

static void compact(struct optimizer *o)
{
	uint32_t index, kept = 0;

	for (index = 0; index < o->count; index++)
	{
		if (o->instructions[index].opcode != _rsx_fp_nop)
			o->instructions[kept++] = o->instructions[index];
	}
	o->count = kept;
}

/* backwards: the components something reads later; writes of others go, and the masks shrink */
static int eliminate(struct optimizer *o)
{
	uint8_t live[TEMPORARIES];
	int live_condition = 0, live_output = _rsx_xyzw, changed = 0;
	uint32_t index;

	memset(live, 0, sizeof(live));
	for (index = o->count; index-- > 0;)
	{
		struct rsx_instruction *instruction = &o->instructions[index];
		int destination = instruction->destination & (TEMPORARIES - 1), needed = 0, condition = 0, lanes;
		int position, lane;

		if (instruction->opcode == _rsx_fp_kil)
		{
			for (lane = 0; lane < 4; lane++)
				live_condition |= 1 << (instruction->condition_swizzle[lane] & 3);
			continue;
		}
		if (instruction->destination_type == _rsx_register_temporary)
			needed = instruction->mask & live[destination];
		else if (instruction->destination_type == _rsx_register_output)
			needed = instruction->mask & live_output;
		if (instruction->condition_update)
			condition = instruction->mask & live_condition;
		if (!(needed | condition))
		{
			instruction->opcode = _rsx_fp_nop;
			changed = 1;
			continue;
		}
		if (instruction->mask != (needed | condition) || (instruction->condition_update && !condition) ||
			(instruction->destination_type == _rsx_register_temporary && !needed))
			changed = 1;
		instruction->mask = (uint8_t)(needed | condition);
		instruction->condition_update = (uint8_t)(condition != 0);
		if (instruction->destination_type == _rsx_register_temporary && !needed)
		{
			instruction->destination_type = _rsx_register_none;
			instruction->destination = 0;
		}
		if (instruction->destination_type == _rsx_register_temporary)
			live[destination] &= (uint8_t)~instruction->mask;
		else if (instruction->destination_type == _rsx_register_output)
			live_output &= ~instruction->mask;
		if (instruction->condition_update)
			live_condition &= ~instruction->mask;
		lanes = read_lanes(o->key, instruction);
		for (position = 0; position < source_count(instruction->opcode); position++)
		{
			const struct rsx_source *source = &instruction->source[position];

			if (source->type != _rsx_register_temporary)
				continue;
			for (lane = 0; lane < 4; lane++)
			{
				if (lanes & (1 << lane))
					live[source->index & (TEMPORARIES - 1)] |= (uint8_t)(1 << (source->swizzle[lane] & 3));
			}
		}
	}
	compact(o);
	return changed;
}

static int last_writer(const struct optimizer *o, uint32_t before, int reg, int component)
{
	uint32_t index;

	for (index = before; index-- > 0;)
	{
		const struct rsx_instruction *instruction = &o->instructions[index];

		if (instruction->destination_type == _rsx_register_temporary &&
			(instruction->destination & (TEMPORARIES - 1)) == reg && (instruction->mask & (1 << component)))
			return (int)index;
	}
	return -1;
}

/* does anything but the reader read a component the maker writes */
static int read_elsewhere(const struct optimizer *o, uint32_t maker, uint32_t reader)
{
	int reg = o->instructions[maker].destination & (TEMPORARIES - 1), pending = o->instructions[maker].mask;
	uint32_t index;

	for (index = maker + 1; index < o->count && pending; index++)
	{
		const struct rsx_instruction *instruction = &o->instructions[index];
		int lanes = read_lanes(o->key, instruction), position, lane;

		for (position = 0; position < source_count(instruction->opcode) && index != reader; position++)
		{
			const struct rsx_source *source = &instruction->source[position];

			if (source->type != _rsx_register_temporary || (source->index & (TEMPORARIES - 1)) != reg)
				continue;
			for (lane = 0; lane < 4; lane++)
			{
				if ((lanes & (1 << lane)) && (pending & (1 << (source->swizzle[lane] & 3))))
					return 1;
			}
		}
		if (instruction->destination_type == _rsx_register_temporary &&
			(instruction->destination & (TEMPORARIES - 1)) == reg)
			pending &= ~instruction->mask;
	}
	return 0;
}

/* MOV_SAT of values nothing else reads: the instructions that make them saturate instead, and the MOV
becomes a copy */
static int absorb(struct optimizer *o)
{
	uint32_t index;
	int changed = 0;

	for (index = 0; index < o->count; index++)
	{
		struct rsx_instruction *instruction = &o->instructions[index];
		const struct rsx_source *source = &instruction->source[0];
		int makers[4], count = 0, lane, maker, fits = 1;

		if (instruction->opcode != _rsx_fp_mov || !instruction->saturate || instruction->condition_update ||
			source->type != _rsx_register_temporary || source->negate || source->absolute)
			continue;
		for (lane = 0; lane < 4 && fits; lane++)
		{
			int which;

			if (!(instruction->mask & (1 << lane)))
				continue;
			maker = last_writer(o, index, source->index & (TEMPORARIES - 1), source->swizzle[lane] & 3);
			fits = maker >= 0;
			for (which = 0; which < count && maker != makers[which]; which++)
				;
			if (fits && which == count)
				makers[count++] = maker;
		}
		for (maker = 0; maker < count && fits; maker++)
		{
			const struct rsx_instruction *made = &o->instructions[makers[maker]];

			fits = made->opcode != _rsx_fp_tex && made->opcode != _rsx_fp_txp && !made->condition_update &&
				!read_elsewhere(o, (uint32_t)makers[maker], index);
		}
		if (!fits || !count)
			continue;
		for (maker = 0; maker < count; maker++)
			o->instructions[makers[maker]].saturate = 1;
		instruction->saturate = 0;
		changed = 1;
	}
	return changed;
}

/* ---------- temporaries */

static int web_root(int *parent, int x)
{
	while (parent[x] != x)
	{
		parent[x] = parent[parent[x]];
		x = parent[x];
	}
	return x;
}

/* do two webs hold a component at the same time; a write where the other's last read is shares it */
static int webs_overlap(const int *start, const int *end, int a, int b)
{
	int lane;

	for (lane = 0; lane < 4; lane++)
	{
		if (end[a * 4 + lane] >= 0 && end[b * 4 + lane] >= 0 && start[a * 4 + lane] < end[b * 4 + lane] &&
			start[b * 4 + lane] < end[a * 4 + lane])
			return 1;
	}
	return 0;
}

/* a web is the writes a read joins, with their reads; each gets the lowest register free in its components
over its span. A copy whose source ends there joins it and goes. Unchanged on a read of nothing. */
static void allocate(struct optimizer *o)
{
	uint32_t count = o->count, index;
	int16_t writer[TEMPORARIES][4];
	/* (one translation at a time: fixed, not the heap's) */
	static int memory[RSX_FP_MAXIMUM_INSTRUCTIONS * 19];
	int *parent, *webs, *start, *end, *last, *physical, *removed;
	int output = -1, web_count = 0, highest = 0, position, lane, web;

	if (count > RSX_FP_MAXIMUM_INSTRUCTIONS)
		return;
	parent = memory;
	webs = parent + count;
	physical = webs + count;
	removed = physical + count;
	start = removed + count;
	end = start + count * 4;
	last = end + count * 4;
	/* reads[index * 3 + position]: a write the source reads */
	{
		int *reads = last + count * 4;

		memset(writer, 0xff, sizeof(writer));
		for (index = 0; index < count; index++)
		{
			parent[index] = (int)index;
			removed[index] = 0;
		}
		for (index = 0; index < count * 4; index++)
		{
			start[index] = 0x7fffffff;
			end[index] = -1;
			last[index] = -1;
		}
		for (index = 0; index < count; index++)
		{
			const struct rsx_instruction *instruction = &o->instructions[index];
			int lanes = read_lanes(o->key, instruction);

			for (position = 0; position < 3; position++)
			{
				const struct rsx_source *source = &instruction->source[position];
				int first = -1;

				reads[index * 3 + position] = -1;
				if (position >= source_count(instruction->opcode) || source->type != _rsx_register_temporary)
					continue;
				for (lane = 0; lane < 4; lane++)
				{
					int maker;

					if (!(lanes & (1 << lane)))
						continue;
					maker = writer[source->index & (TEMPORARIES - 1)][source->swizzle[lane] & 3];
					if (maker < 0)
						return;
					if (first < 0)
						first = maker;
					else
						parent[web_root(parent, maker)] = web_root(parent, first);
				}
				reads[index * 3 + position] = first;
			}
			if (instruction->destination_type == _rsx_register_temporary)
			{
				for (lane = 0; lane < 4; lane++)
				{
					if (instruction->mask & (1 << lane))
						writer[instruction->destination & (TEMPORARIES - 1)][lane] = (int16_t)index;
				}
			}
			else if (instruction->destination_type == _rsx_register_output)
			{
				if (output < 0)
					output = (int)index;
				parent[web_root(parent, (int)index)] = web_root(parent, output);
			}
		}

		/* each web's span per component: its first write to its last read */
		for (index = 0; index < count; index++)
		{
			const struct rsx_instruction *instruction = &o->instructions[index];
			int lanes = read_lanes(o->key, instruction);

			if (instruction->destination_type == _rsx_register_temporary ||
				instruction->destination_type == _rsx_register_output)
			{
				web = web_root(parent, (int)index);
				for (lane = 0; lane < 4; lane++)
				{
					if (!(instruction->mask & (1 << lane)))
						continue;
					start[web * 4 + lane] = (int)index < start[web * 4 + lane] ? (int)index : start[web * 4 + lane];
					end[web * 4 + lane] = (int)index > end[web * 4 + lane] ? (int)index : end[web * 4 + lane];
					last[web * 4 + lane] = (int)index;
				}
			}
			for (position = 0; position < 3; position++)
			{
				if (reads[index * 3 + position] < 0)
					continue;
				web = web_root(parent, reads[index * 3 + position]);
				for (lane = 0; lane < 4; lane++)
				{
					int component = o->instructions[index].source[position].swizzle[lane] & 3;

					if ((lanes & (1 << lane)) && (int)index > end[web * 4 + component])
						end[web * 4 + component] = (int)index;
				}
			}
		}
		/* the colour is read when the program ends */
		if (output >= 0)
		{
			web = web_root(parent, output);
			for (lane = 0; lane < 4; lane++)
			{
				if (end[web * 4 + lane] >= 0)
					end[web * 4 + lane] = (int)count;
			}
		}

		for (index = 0; index < count; index++)
		{
			const struct rsx_instruction *instruction = &o->instructions[index];
			const struct rsx_source *source = &instruction->source[0];
			int from, into;

			if (instruction->opcode != _rsx_fp_mov || instruction->saturate || instruction->condition_update ||
				reads[index * 3] < 0 || source->negate || source->absolute ||
				(instruction->destination_type != _rsx_register_temporary &&
				instruction->destination_type != _rsx_register_output))
				continue;
			for (lane = 0; lane < 4 && (!(instruction->mask & (1 << lane)) || source->swizzle[lane] == lane); lane++)
				;
			if (lane < 4)
				continue;
			from = web_root(parent, reads[index * 3]);
			into = web_root(parent, (int)index);
			if (from != into)
			{
				int clash = 0;

				for (lane = 0; lane < 4; lane++)
				{
					int a = from * 4 + lane, b = into * 4 + lane;

					if (end[a] < 0 || end[b] < 0)
						continue;
					/* both read after the copy: one value, while neither writes the component again */
					if ((instruction->mask & (1 << lane)) && end[a] > (int)index)
						clash |= last[a] > (int)index || start[b] != (int)index || last[b] != (int)index;
					else
						clash |= start[a] < end[b] && start[b] < end[a];
				}
				if (clash)
					continue;
				for (lane = 0; lane < 4; lane++)
				{
					int a = from * 4 + lane, b = into * 4 + lane;

					start[b] = start[a] < start[b] ? start[a] : start[b];
					end[b] = end[a] > end[b] ? end[a] : end[b];
					last[b] = last[a] > last[b] ? last[a] : last[b];
				}
				/* into keeps the root: the colour's web stays the colour's */
				parent[from] = into;
			}
			removed[index] = 1;
		}

		/* the webs by their first write, the colour's first in R0 */
		for (index = 0; index < count; index++)
		{
			const struct rsx_instruction *instruction = &o->instructions[index];
			int first = 0x7fffffff, slot;

			physical[index] = -1;
			if (removed[index] || (instruction->destination_type != _rsx_register_temporary &&
				instruction->destination_type != _rsx_register_output))
				continue;
			web = web_root(parent, (int)index);
			for (slot = 0; slot < web_count && webs[slot] != web; slot++)
				;
			if (slot < web_count)
				continue;
			for (lane = 0; lane < 4; lane++)
				first = start[web * 4 + lane] < first ? start[web * 4 + lane] : first;
			for (slot = web_count++; slot > 0; slot--)
			{
				int other = webs[slot - 1], other_first = 0x7fffffff;

				for (lane = 0; lane < 4; lane++)
					other_first = start[other * 4 + lane] < other_first ? start[other * 4 + lane] : other_first;
				if (other_first <= first)
					break;
				webs[slot] = other;
			}
			webs[slot] = web;
		}
		if (output >= 0)
			physical[web_root(parent, output)] = 0;
		for (index = 0; index < (uint32_t)web_count; index++)
		{
			int choice = 0, slot;

			web = webs[index];
			if (output >= 0 && web == web_root(parent, output))
				continue;
			for (slot = 0; slot < web_count; slot++)
			{
				int other = webs[slot];

				if (other != web && physical[other] == choice && webs_overlap(start, end, web, other))
				{
					choice++;
					slot = -1;
				}
			}
			physical[web] = choice;
			highest = choice > highest ? choice : highest;
		}
		if (highest >= TEMPORARIES)
			return;

		for (index = 0; index < count; index++)
		{
			struct rsx_instruction *instruction = &o->instructions[index];

			if (removed[index])
			{
				instruction->opcode = _rsx_fp_nop;
				continue;
			}
			if (instruction->destination_type == _rsx_register_temporary)
				instruction->destination = (uint8_t)physical[web_root(parent, (int)index)];
			for (position = 0; position < source_count(instruction->opcode); position++)
			{
				if (instruction->source[position].type == _rsx_register_temporary)
					instruction->source[position].index = (uint16_t)(reads[index * 3 + position] < 0 ? 0 :
						physical[web_root(parent, reads[index * 3 + position])]);
			}
		}
	}
	compact(o);
}

/* the program with fewer instructions and temporaries; as it was where it tests the condition outside KIL */
static uint32_t optimize(const struct rsx_fragment_key *key, struct rsx_instruction *instructions, uint32_t count)
{
	/* (one translation at a time: fixed, not the heap's) */
	static struct optimizer optimizer;
	struct optimizer *o = &optimizer;
	uint32_t index;
	int round;

	for (index = 0; index < count; index++)
	{
		if (instructions[index].opcode != _rsx_fp_kil && instructions[index].condition != _rsx_condition_always)
			return count;
	}
	memset(o, 0, sizeof(*o));
	o->key = key;
	o->instructions = instructions;
	o->count = count;
	for (index = 0; index < count; index++)
	{
		int position;

		if (instructions[index].destination_type == _rsx_register_temporary &&
			instructions[index].destination >= o->registers)
			o->registers = (instructions[index].destination & (TEMPORARIES - 1)) + 1;
		for (position = 0; position < 3; position++)
		{
			if (instructions[index].source[position].type == _rsx_register_temporary &&
				instructions[index].source[position].index >= o->registers)
				o->registers = (instructions[index].source[position].index & (TEMPORARIES - 1)) + 1;
		}
	}
	for (round = 0; round < 32; round++)
	{
		int changed = forward(o);

		changed |= eliminate(o);
		changed |= absorb(o);
		if (!changed)
			break;
	}
	allocate(o);
	return o->count;
}

/* ---------- half precision: colour-only temporaries as H registers (lookups, tests, floors, divides stay 32-bit).
Off: the PC check's 3000 keys took 2.96 R registers on average against 3.07; 13 missed the NV2A's by up to 7.5e-3 */

#define HALF_PRECISION 0
/* the largest magnitude a half temporary may hold: half's 11 bits keep 2^-12 near 1 */
#define HALF_RANGE 1024.0f

#if HALF_PRECISION
static struct value bounded_value(float low, float high)
{
	struct value value = register_value(_rsx_register_temporary, 0, 0, 0);

	value.low = low;
	value.high = high;
	return value;
}

/* a source's bounds in one lane: the registers' as the pass has them, inputs' and constants' as resolve's */
static struct value source_bounds(const struct rsx_instruction *instruction, const struct rsx_source *source,
	float low[TEMPORARIES][4], float high[TEMPORARIES][4], int lane)
{
	int component = source->swizzle[lane] & 3;
	struct value value;

	if (source->type == _rsx_register_temporary)
		value = bounded_value(low[source->index & (TEMPORARIES - 1)][component],
			high[source->index & (TEMPORARIES - 1)][component]);
	else if (source->type == _rsx_register_constant && instruction->uniform == RSX_NO_UNIFORM)
		value = number_value(instruction->constant[component]);
	else if ((source->type == _rsx_register_input &&
		(source->index == _rsx_fp_input_color0 || source->index == _rsx_fp_input_color1)) ||
		(source->type == _rsx_register_constant && instruction->uniform < _rsx_uniform_fog))
		value = bounded_value(0.0f, 1.0f);
	else
		value = bounded_value(-INFINITY, INFINITY);
	return modified(value, source->negate, source->absolute);
}

static int reads_exactly(int opcode)
{
	switch (opcode)
	{
	case _rsx_fp_tex:
	case _rsx_fp_txp:
	case _rsx_fp_slt:
	case _rsx_fp_sge:
	case _rsx_fp_seq:
	case _rsx_fp_frc:
	case _rsx_fp_flr:
	case _rsx_fp_rcp:
	case _rsx_fp_ex2:
	case _rsx_fp_lg2:
		return 1;
	default:
		return 0;
	}
}

static void half_precision(const struct rsx_fragment_key *key, struct rsx_instruction *instructions, uint32_t count)
{
	float low[TEMPORARIES][4], high[TEMPORARIES][4];
	uint8_t full[TEMPORARIES], used[TEMPORARIES], half_index[TEMPORARIES], changed = 1;
	int lane, reg, position, next_r = 1, odd = -1;
	uint32_t index;

	memset(full, 0, sizeof(full));
	memset(used, 0, sizeof(used));
	for (reg = 0; reg < TEMPORARIES; reg++)
	{
		for (lane = 0; lane < 4; lane++)
		{
			low[reg][lane] = -INFINITY;
			high[reg][lane] = INFINITY;
		}
	}
	/* each written value's bounds, in program order: outside half's range, its register stays 32-bit */
	for (index = 0; index < count; index++)
	{
		struct rsx_instruction *instruction = &instructions[index];
		int sources = source_count(instruction->opcode), lanes = read_lanes(key, instruction);
		int destination = instruction->destination & (TEMPORARIES - 1);
		struct operand operands[3];

		memset(operands, 0, sizeof(operands));
		for (position = 0; position < sources; position++)
		{
			if (instruction->source[position].type == _rsx_register_temporary)
				used[instruction->source[position].index & (TEMPORARIES - 1)] = 1;
			for (lane = 0; lane < 4; lane++)
			{
				if (lanes & (1 << lane))
					operands[position].values[lane] =
						source_bounds(instruction, &instruction->source[position], low, high, lane);
			}
		}
		if (instruction->destination_type != _rsx_register_temporary &&
			instruction->destination_type != _rsx_register_output)
			continue;
		used[destination] = 1;
		for (lane = 0; lane < 4; lane++)
		{
			float l, h;

			if (!(instruction->mask & (1 << lane)))
				continue;
			bounds(instruction->opcode, operands, lane, &l, &h);
			if (instruction->saturate)
			{
				l = clamp01(l);
				h = clamp01(h);
			}
			low[destination][lane] = l;
			high[destination][lane] = h;
			if (!(l >= -HALF_RANGE && h <= HALF_RANGE))
				full[destination] = 1;
		}
	}
	/* what an exact reader reads, a condition comes from, and all that makes a 32-bit value, is 32-bit */
	while (changed)
	{
		changed = 0;
		for (index = 0; index < count; index++)
		{
			const struct rsx_instruction *instruction = &instructions[index];
			int writes_full = (instruction->destination_type == _rsx_register_temporary ||
				instruction->destination_type == _rsx_register_output) &&
				full[instruction->destination & (TEMPORARIES - 1)];

			for (position = 0; position < source_count(instruction->opcode); position++)
			{
				const struct rsx_source *source = &instruction->source[position];

				reg = source->index & (TEMPORARIES - 1);
				if (source->type == _rsx_register_temporary && !full[reg] &&
					(writes_full || instruction->condition_update || reads_exactly(instruction->opcode)))
				{
					full[reg] = 1;
					changed = 1;
				}
			}
		}
	}
	/* R0 is the colour's: H0 and H1 when it is half; then the 32-bit registers; then the halves, two to an R */
	for (reg = 1; reg < TEMPORARIES; reg++)
	{
		if (used[reg] && full[reg])
			half_index[reg] = (uint8_t)next_r++;
	}
	if (!full[0])
		odd = 1;
	for (reg = 1; reg < TEMPORARIES; reg++)
	{
		if (!used[reg] || full[reg])
			continue;
		if (odd >= 0)
		{
			half_index[reg] = (uint8_t)odd;
			odd = -1;
		}
		else
		{
			half_index[reg] = (uint8_t)(2 * next_r);
			odd = 2 * next_r + 1;
			next_r++;
		}
	}
	half_index[0] = 0;
	for (index = 0; index < count; index++)
	{
		struct rsx_instruction *instruction = &instructions[index];

		if (instruction->destination_type == _rsx_register_temporary ||
			instruction->destination_type == _rsx_register_output)
		{
			reg = instruction->destination & (TEMPORARIES - 1);
			instruction->destination_half = (uint8_t)!full[reg];
			instruction->destination = half_index[reg];
		}
		for (position = 0; position < source_count(instruction->opcode); position++)
		{
			struct rsx_source *source = &instruction->source[position];

			if (source->type != _rsx_register_temporary)
				continue;
			reg = source->index & (TEMPORARIES - 1);
			source->half = (uint8_t)!full[reg];
			source->index = half_index[reg];
		}
	}
}
#endif

/* each clamp to [-1, 1] as an RSX MAX and MIN on its destination; a source the MAX cannot read beside its number (a
uniform, an input) is moved there first. 0 if they do not fit */
static int expand_clamps(struct rsx_instruction *instructions, uint32_t *count, uint32_t maximum)
{
	uint32_t index, extra = 0, out;

	for (index = 0; index < *count; index++)
	{
		if (instructions[index].opcode == _op_clamp_signed)
			extra += instructions[index].source[0].type == _rsx_register_temporary ? 1 : 2;
	}
	if (*count + extra > maximum)
		return 0;
	/* from the end, each in its place */
	out = *count + extra;
	for (index = *count; index-- > 0;)
	{
		struct rsx_instruction clamp = instructions[index], *instruction;
		struct rsx_source result = temporary(clamp.destination), limit = fixed(_k_one);

		if (clamp.opcode != _op_clamp_signed)
		{
			instructions[--out] = clamp;
			continue;
		}
		result.half = clamp.destination_half;
		/* the MIN, the MAX, and the MOV, made in place from the last */
		instruction = &instructions[--out];
		*instruction = clamp;
		instruction->opcode = _rsx_fp_min;
		instruction->uniform = RSX_NO_UNIFORM;
		instruction->source[0] = result;
		instruction->source[1] = limit;
		instruction->source[2] = source_none();
		memcpy(instruction->constant, fixed_constant, sizeof(instruction->constant));
		instruction = &instructions[--out];
		*instruction = clamp;
		instruction->opcode = _rsx_fp_max;
		instruction->saturate = 0;
		instruction->uniform = RSX_NO_UNIFORM;
		instruction->source[0] = clamp.source[0].type == _rsx_register_temporary ? clamp.source[0] : result;
		instruction->source[1] = negated(limit);
		instruction->source[2] = source_none();
		memcpy(instruction->constant, fixed_constant, sizeof(instruction->constant));
		if (clamp.source[0].type != _rsx_register_temporary)
		{
			instruction = &instructions[--out];
			*instruction = clamp;
			instruction->opcode = _rsx_fp_mov;
			instruction->saturate = 0;
		}
	}
	*count += extra;
	return 1;
}

/* the stages texture_stage gives a texel (a sampler, or a mode that needs none) */
static int stage_active(const struct rsx_fragment_key *key, int stage)
{
	int mode = stage_mode(key, stage);

	if (mode == _mode_passthru || mode == _mode_clipplane || mode == _mode_dot_product || mode == _mode_dot_zw)
		return 1;
	return mode != _mode_none && mode != _mode_brdf && mode <= _mode_dot_reflect_specular_constant &&
		key->sampler_type[stage] != _rsx_sampler_none;
}

static int stage_dot_mode(int mode)
{
	return mode == _mode_dot_product || (mode >= _mode_dot_st && mode <= _mode_dot_str_cube) ||
		mode == _mode_dot_reflect_specular_constant;
}

/* clears what the translation does not read (unused combiner stages, unused texture stages' states), so that the
leftovers of earlier draws give no new key: the same microcode (tools/ps3_rsx_shader_check.c) */
void rsx_fragment_key_clean(struct rsx_fragment_key *key)
{
	uint32_t combiner_count = key->combiner_count & 0xff, texture_modes = 0, dot_mapping = 0, input_texture = 0;
	uint32_t compare_mode = 0;
	int stage, mode, active, reads_dots, reads_input, sampled, diffuse_reflection;

	if (combiner_count > 8)
		combiner_count = 8;
	for (stage = (int)combiner_count; stage < 8; stage++)
	{
		key->rgb_inputs[stage] = key->rgb_outputs[stage] = 0;
		key->alpha_inputs[stage] = key->alpha_outputs[stage] = 0;
	}
	/* stage 2's diffuse reflection also takes stage 3's dot product, whatever stage 3's mode */
	diffuse_reflection = stage_active(key, 2) && stage_mode(key, 2) == _mode_dot_reflect_diffuse;
	for (stage = 0; stage < 4; stage++)
	{
		active = stage_active(key, stage);
		mode = active ? stage_mode(key, stage) : _mode_none;
		reads_dots = (active && stage_dot_mode(mode)) || (stage == 3 && diffuse_reflection);
		reads_input = reads_dots || mode == _mode_dependent_ar || mode == _mode_dependent_gb;
		sampled = active && mode != _mode_passthru && mode != _mode_clipplane && mode != _mode_dot_product &&
			mode != _mode_dot_zw;
		texture_modes |= (uint32_t)mode << (5 * stage);
		if (mode == _mode_clipplane)
			compare_mode |= key->compare_mode & (0xfu << (4 * stage));
		/* (stage 0 has no mapping: dot_input's shift would be negative, so its value stays whole) */
		if (reads_dots)
			dot_mapping |= stage ? key->dot_mapping & (7u << ((stage - 1) * 4)) : key->dot_mapping;
		if (reads_input && stage == 2)
			input_texture |= key->input_texture & (1u << 16);
		if (reads_input && stage == 3)
			input_texture |= key->input_texture & (3u << 20);
		if (!sampled)
		{
			key->sampler_type[stage] = _rsx_sampler_none;
			key->texture_scaled &= (uint8_t)~(1 << stage);
		}
		if (!active)
			key->alpha_kill[stage] = key->color_sign[stage] = 0;
	}
	key->texture_modes = texture_modes;
	key->dot_mapping = dot_mapping;
	key->input_texture = input_texture;
	key->compare_mode = compare_mode;
	if (!reads_fog(key, combiner_count))
		key->fog_enable = 0;
	if (!key->fog_enable || key->fog_table_mode > 3)
		key->fog_table_mode = 0;
	key->padding = 0;
}

/* the key's fields in the text's order: 39 words, then 15 bytes */
static void key_fields(const struct rsx_fragment_key *key, const uint32_t *words[39], const uint8_t *bytes[15])
{
	int index;

	for (index = 0; index < 8; index++)
	{
		words[index] = &key->rgb_inputs[index];
		words[8 + index] = &key->rgb_outputs[index];
		words[16 + index] = &key->alpha_inputs[index];
		words[24 + index] = &key->alpha_outputs[index];
	}
	words[32] = &key->final_inputs_abcd;
	words[33] = &key->final_inputs_efg;
	words[34] = &key->combiner_count;
	words[35] = &key->texture_modes;
	words[36] = &key->dot_mapping;
	words[37] = &key->input_texture;
	words[38] = &key->compare_mode;
	for (index = 0; index < 4; index++)
	{
		bytes[index] = &key->sampler_type[index];
		bytes[4 + index] = &key->alpha_kill[index];
		bytes[8 + index] = &key->color_sign[index];
	}
	bytes[12] = &key->fog_enable;
	bytes[13] = &key->fog_table_mode;
	bytes[14] = &key->texture_scaled;
}

static char *hex_write(char *text, uint32_t value, int digits)
{
	while (digits--)
		*text++ = "0123456789abcdef"[(value >> (4 * digits)) & 0xf];
	return text;
}

void rsx_fragment_key_text(const struct rsx_fragment_key *key, char text[RSX_FRAGMENT_KEY_TEXT])
{
	const uint32_t *words[39];
	const uint8_t *bytes[15];
	int index;

	key_fields(key, words, bytes);
	for (index = 0; index < 39; index++)
	{
		text = hex_write(text, *words[index], 8);
		*text++ = ' ';
	}
	for (index = 0; index < 15; index++)
	{
		text = hex_write(text, *bytes[index], 2);
		*text++ = index < 14 ? ' ' : 0;
	}
}

int rsx_fragment_key_parse(const char *text, struct rsx_fragment_key *key)
{
	const uint32_t *words[39];
	const uint8_t *bytes[15];
	int index, digits;

	memset(key, 0, sizeof(*key));
	key_fields(key, words, bytes);
	for (index = 0; index < 39 + 15; index++)
	{
		uint32_t value = 0;

		while (*text == ' ' || *text == '\t')
			text++;
		for (digits = 0; digits < (index < 39 ? 8 : 2); digits++, text++)
		{
			int digit = *text >= '0' && *text <= '9' ? *text - '0' : *text >= 'a' && *text <= 'f' ? *text - 'a' + 10 :
				*text >= 'A' && *text <= 'F' ? *text - 'A' + 10 : -1;

			if (digit < 0)
				return -1;
			value = (value << 4) | (uint32_t)digit;
		}
		if (index < 39)
			*(uint32_t *)words[index] = value;
		else
			*(uint8_t *)bytes[index - 39] = (uint8_t)value;
	}
	return 0;
}

int rsx_fragment_program_instructions(const struct rsx_fragment_key *key,
	struct rsx_instruction *instructions, uint32_t maximum, uint32_t *count)
{
	struct context context;
	uint32_t combiner_count = key->combiner_count & 0xff;
	struct rsx_instruction *instruction;
	int stage, result;

	memset(&context, 0, sizeof(context));
	context.key = key;
	context.instructions = instructions;
	context.maximum = maximum;
	if (combiner_count > 8)
		combiner_count = 8;

	/* a fragment program instruction reads at most one interpolated input */
	emit1(&context, _rsx_fp_mov, _register_v0, _rsx_xyzw, input(&context, _rsx_fp_input_color0));
	emit1(&context, _rsx_fp_mov, _register_v1, _rsx_xyzw, input(&context, _rsx_fp_input_color1));
	emit1(&context, _rsx_fp_mov, _register_dots, _rsx_xyzw, fixed(_k_zero));
	/* a reflection stage can read a later stage's texel, which is 0 then */
	for (stage = 0; stage < 4; stage++)
		emit1(&context, _rsx_fp_mov, _register_t0 + stage, _rsx_xyzw, fixed(_k_zero));
	for (stage = 0; stage < 4; stage++)
		texture_stage(&context, stage);
	if (reads_fog(key, combiner_count))
		fog(&context);
	emit1(&context, _rsx_fp_mov, _register_r0, _rsx_xyz, fixed(_k_zero));
	emit1(&context, _rsx_fp_mov, _register_r0, _rsx_w, replicated(temporary(_register_t0), 3));
	emit1(&context, _rsx_fp_mov, _register_r1, _rsx_xyzw, fixed(_k_zero));
	for (stage = 0; stage < (int)combiner_count; stage++)
		combiner_stage(&context, stage);
	result = final_combiner(&context);

	instruction = saturated(emit1(&context, _rsx_fp_mov, _register_color, _rsx_xyzw, temporary(result)));
	instruction->destination_type = _rsx_register_output;
	if (!context.overflow)
	{
		context.count = optimize(key, instructions, context.count);
		context.overflow = !expand_clamps(instructions, &context.count, maximum);
#if HALF_PRECISION
		if (!context.overflow)
			half_precision(key, instructions, context.count);
#endif
	}
	*count = context.count;
	return context.overflow ? -1 : (context.kill ? 1 : 0);
}

int rsx_fragment_program_translate(const struct rsx_fragment_key *key, struct rsx_fragment_program *program)
{
	/* (one translation at a time, on the thread that runs the Direct3D records: fixed, not the heap's) */
	static struct rsx_instruction instructions[RSX_FP_MAXIMUM_INSTRUCTIONS];
	uint32_t count = 0, index, word = 0;
	int result, half_output = 0;

	memset(program, 0, sizeof(*program));
	result = rsx_fragment_program_instructions(key, instructions, RSX_FP_MAXIMUM_INSTRUCTIONS, &count);
	if (result < 0 || !(program->microcode = malloc(count * 8 * sizeof(uint32_t))))
		return -1;
	program->word_count = rsx_fragment_encode(instructions, count, program->microcode);
	/* each uniform's place: the four words after its instruction */
	for (index = 0; index < count; index++)
	{
		word += 4;
		if (!rsx_fragment_reads_constant(&instructions[index]))
			continue;
		if (instructions[index].uniform != RSX_NO_UNIFORM && program->uniform_count < RSX_FP_MAXIMUM_UNIFORMS)
		{
			program->uniforms[program->uniform_count].word = (uint16_t)word;
			program->uniforms[program->uniform_count].uniform = instructions[index].uniform;
			program->uniform_count++;
		}
		word += 4;
	}
	for (index = 0; index < program->word_count; index++)
		program->microcode[index] = (program->microcode[index] << 16) | (program->microcode[index] >> 16);
	/* the texture coordinates read, as the interpolator set-up needs them; the temporaries in R registers (an H is
	half of one), two at least as cgcomp counts; the colour as R0 or H0 */
	program->register_count = 2;
	for (index = 0; index < count; index++)
	{
		const struct rsx_instruction *instruction = &instructions[index];
		uint32_t reg = instruction->destination_half ? instruction->destination / 2u : instruction->destination;
		int position;

		if ((instruction->destination_type == _rsx_register_temporary ||
			instruction->destination_type == _rsx_register_output) && reg >= program->register_count)
			program->register_count = reg + 1u;
		if (instruction->destination_type != _rsx_register_none && !instruction->destination && instruction->destination_half)
			half_output = 1;
		for (position = 0; position < 3; position++)
		{
			const struct rsx_source *source = &instruction->source[position];

			reg = source->half ? source->index / 2u : source->index;
			if (source->type == _rsx_register_input && source->index >= _rsx_fp_input_texture0)
				program->texcoord_mask |= 1u << (source->index - _rsx_fp_input_texture0);
			if (source->type == _rsx_register_temporary && reg >= program->register_count)
				program->register_count = reg + 1u;
		}
	}
	/* 0x40: R0 is exported (else H0); 0x80: the program kills */
	program->control = (half_output ? 0 : 0x40) | (result == 1 ? 0x80 : 0);
	return 0;
}

void rsx_fragment_program_free(struct rsx_fragment_program *program)
{
	free(program->microcode);
	memset(program, 0, sizeof(*program));
}
