/* NV2A pixel shaders (texture stages, register combiners, the final combiner) as RSX fragment programs,
with the semantics of port/linux/src/nv2a_psh.c. Per-draw constants are inline constants, patched per draw. */

#include "ps3_rsx_program.h"

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
	_register_count = 23,
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
	struct rsx_instruction *instruction = emit1(context, opcode, _register_t0 + stage, _rsx_xyzw, coordinates);

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

/* the output mapping, then the clamp to [-1, 1] the registers hold */
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
	emit2(context, _rsx_fp_max, reg, mask, value, negated(fixed(_k_one)));
	emit2(context, _rsx_fp_min, reg, mask, value, fixed(_k_one));
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
	int portion;

	combiner_input(context, (rgb_inputs >> 24) & 0xff, 0, stage, _register_a);
	combiner_input(context, (rgb_inputs >> 16) & 0xff, 0, stage, _register_b);
	combiner_input(context, (rgb_inputs >> 8) & 0xff, 0, stage, _register_c);
	combiner_input(context, rgb_inputs & 0xff, 0, stage, _register_d);
	combiner_input(context, (alpha_inputs >> 24) & 0xff, 1, stage, _register_a);
	combiner_input(context, (alpha_inputs >> 16) & 0xff, 1, stage, _register_b);
	combiner_input(context, (alpha_inputs >> 8) & 0xff, 1, stage, _register_c);
	combiner_input(context, alpha_inputs & 0xff, 1, stage, _register_d);

	emit2(context, (rgb_flags & 0x02) ? _rsx_fp_dp3 : _rsx_fp_mul, _register_ab, _rsx_xyz,
		temporary(_register_a), temporary(_register_b));
	emit2(context, _rsx_fp_mul, _register_ab, _rsx_w, temporary(_register_a), temporary(_register_b));
	emit2(context, (rgb_flags & 0x01) ? _rsx_fp_dp3 : _rsx_fp_mul, _register_cd, _rsx_xyz,
		temporary(_register_c), temporary(_register_d));
	emit2(context, _rsx_fp_mul, _register_cd, _rsx_w, temporary(_register_c), temporary(_register_d));
	if ((rgb_flags | alpha_flags) & 0x04)
		mux_selector(context);
	for (portion = 0; portion < 2; portion++)
	{
		int mask = portion ? _rsx_w : _rsx_xyz;
		uint32_t flags = portion ? alpha_flags : rgb_flags;

		if (flags & 0x04)
		{
			/* sum = selector ? cd : ab */
			emit2(context, _rsx_fp_add, _register_sum, mask, temporary(_register_cd), negated(temporary(_register_ab)));
			emit(context, _rsx_fp_mad, _register_sum, mask, temporary(_register_sum),
				replicated(temporary(_register_scratch2), 0), temporary(_register_ab));
		}
		else
		{
			emit2(context, _rsx_fp_add, _register_sum, mask, temporary(_register_ab), temporary(_register_cd));
		}
		output_mapping(context, _register_ab, mask, flags);
		output_mapping(context, _register_cd, mask, flags);
		output_mapping(context, _register_sum, mask, flags);
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
			emit1(context, _rsx_fp_mov, ab, mask, temporary(_register_ab));
			if (!portion && (flags & 0x80))
				emit1(context, _rsx_fp_mov, ab, _rsx_w, replicated(temporary(_register_ab), 2));
		}
		if (cd >= 0)
		{
			emit1(context, _rsx_fp_mov, cd, mask, temporary(_register_cd));
			if (!portion && (flags & 0x40))
				emit1(context, _rsx_fp_mov, cd, _rsx_w, replicated(temporary(_register_cd), 2));
		}
		if (sum >= 0)
			emit1(context, _rsx_fp_mov, sum, mask, temporary(_register_sum));
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
	*count = context.count;
	return context.overflow ? -1 : (context.kill ? 1 : 0);
}

int rsx_fragment_program_translate(const struct rsx_fragment_key *key, struct rsx_fragment_program *program)
{
	struct rsx_instruction *instructions = malloc(RSX_FP_MAXIMUM_INSTRUCTIONS * sizeof(*instructions));
	uint32_t count = 0, index, word = 0;
	int result;

	memset(program, 0, sizeof(*program));
	if (!instructions)
		return -1;
	result = rsx_fragment_program_instructions(key, instructions, RSX_FP_MAXIMUM_INSTRUCTIONS, &count);
	if (result < 0 || !(program->microcode = malloc(count * 8 * sizeof(uint32_t))))
	{
		free(instructions);
		return -1;
	}
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
	/* the texture coordinates read, as the interpolator set-up needs them */
	for (index = 0; index < count; index++)
	{
		int position;

		for (position = 0; position < 3; position++)
		{
			const struct rsx_source *source = &instructions[index].source[position];

			if (source->type == _rsx_register_input && source->index >= _rsx_fp_input_texture0)
				program->texcoord_mask |= 1u << (source->index - _rsx_fp_input_texture0);
		}
	}
	/* 0x40: R0 is exported at 32 bits; 0x80: the program kills */
	program->control = 0x40 | (result == 1 ? 0x80 : 0);
	program->register_count = _register_count;
	free(instructions);
	return 0;
}

void rsx_fragment_program_free(struct rsx_fragment_program *program)
{
	free(program->microcode);
	memset(program, 0, sizeof(*program));
}
