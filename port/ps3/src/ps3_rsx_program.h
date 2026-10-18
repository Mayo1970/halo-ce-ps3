/* PS3_RSX_PROGRAM.H: RSX program microcode made at run time from NV2A vertex programs and combiners.
Fixed-width types only: the Xbox layer calls it, and tools/ps3_rsx_shader_check.c builds it on the PC. */

#ifndef __HALO_PS3_RSX_PROGRAM_H
#define __HALO_PS3_RSX_PROGRAM_H

#include <stdint.h>

/* ---------- instructions (the encodings: cgcomp's compilervp.cpp and compilerfp.cpp) */

enum
{
	_rsx_register_none,
	_rsx_register_temporary,
	_rsx_register_input,
	_rsx_register_constant,
	_rsx_register_output,
	_rsx_register_address,
};

/* component masks, x in bit 0 */
enum
{
	_rsx_x = 1,
	_rsx_y = 2,
	_rsx_z = 4,
	_rsx_w = 8,
	_rsx_xyz = 7,
	_rsx_xyzw = 15,
};

/* vertex program opcodes: the vector unit's, and the scalar unit's from _rsx_vp_scalar */
enum
{
	_rsx_vp_nop = 0x00,
	_rsx_vp_mov = 0x01,
	_rsx_vp_mul = 0x02,
	_rsx_vp_add = 0x03,
	_rsx_vp_mad = 0x04,
	_rsx_vp_dp3 = 0x05,
	_rsx_vp_dph = 0x06,
	_rsx_vp_dp4 = 0x07,
	_rsx_vp_dst = 0x08,
	_rsx_vp_min = 0x09,
	_rsx_vp_max = 0x0a,
	_rsx_vp_slt = 0x0b,
	_rsx_vp_sge = 0x0c,
	_rsx_vp_arl = 0x0d,

	_rsx_vp_scalar = 0x80,
	_rsx_vp_scalar_mov = 0x81,
	_rsx_vp_rcp = 0x82,
	_rsx_vp_rcc = 0x83,
	_rsx_vp_rsq = 0x84,
	_rsx_vp_exp = 0x85,
	_rsx_vp_log = 0x86,
	_rsx_vp_lit = 0x87,
};

/* vertex program outputs */
enum
{
	_rsx_vp_output_position = 0,
	_rsx_vp_output_color0 = 1,
	_rsx_vp_output_color1 = 2,
	_rsx_vp_output_back_color0 = 3,
	_rsx_vp_output_back_color1 = 4,
	_rsx_vp_output_fog = 5,
	_rsx_vp_output_point_size = 6,
	_rsx_vp_output_texture0 = 7,
};

/* fragment program opcodes */
enum
{
	_rsx_fp_nop = 0x00,
	_rsx_fp_mov = 0x01,
	_rsx_fp_mul = 0x02,
	_rsx_fp_add = 0x03,
	_rsx_fp_mad = 0x04,
	_rsx_fp_dp3 = 0x05,
	_rsx_fp_dp4 = 0x06,
	_rsx_fp_min = 0x08,
	_rsx_fp_max = 0x09,
	_rsx_fp_slt = 0x0a,
	_rsx_fp_sge = 0x0b,
	_rsx_fp_seq = 0x0f,
	_rsx_fp_frc = 0x10,
	_rsx_fp_flr = 0x11,
	_rsx_fp_kil = 0x12,
	_rsx_fp_tex = 0x17,
	_rsx_fp_txp = 0x18,
	_rsx_fp_rcp = 0x1a,
	_rsx_fp_ex2 = 0x1c,
	_rsx_fp_lg2 = 0x1d,
};

/* fragment program inputs */
enum
{
	_rsx_fp_input_color0 = 1,
	_rsx_fp_input_color1 = 2,
	_rsx_fp_input_fog = 3,
	_rsx_fp_input_texture0 = 4,
};

/* condition codes (_rsx_condition_always: no test) */
enum
{
	_rsx_condition_false,
	_rsx_condition_less,
	_rsx_condition_equal,
	_rsx_condition_less_equal,
	_rsx_condition_greater,
	_rsx_condition_not_equal,
	_rsx_condition_greater_equal,
	_rsx_condition_always,
};

struct rsx_source
{
	uint8_t type;
	uint8_t negate;
	uint8_t absolute;
	/* vertex programs: c[A0.x + index] */
	uint8_t relative;
	uint16_t index;
	uint8_t swizzle[4];
	/* fragment programs: a temporary read as the half-precision register H index */
	uint8_t half;
};

struct rsx_instruction
{
	uint8_t opcode;
	uint8_t destination_type;
	uint8_t destination;
	/* fragment programs: the destination is the half-precision register H destination (the colour: H0) */
	uint8_t destination_half;
	uint8_t mask;
	uint8_t saturate;
	/* fragment programs: the condition register update, the test, and the texture unit */
	uint8_t condition_update;
	uint8_t condition;
	uint8_t condition_swizzle[4];
	uint8_t texture_unit;
	/* fragment programs: the inline constant the constant sources read, and its per-draw value */
	uint8_t uniform;
	struct rsx_source source[3];
	float constant[4];
};

#define RSX_NO_UNIFORM 0xff

/* the vertex program words of an instruction; last marks the program's end */
void rsx_vertex_encode(const struct rsx_instruction *instruction, int last, uint32_t words[4]);
/* the fragment program words (not yet half-swapped): 4 per instruction, 4 more after one that
reads its constant. Returns the word count. */
uint32_t rsx_fragment_encode(const struct rsx_instruction *instructions, uint32_t count, uint32_t *words);
int rsx_fragment_reads_constant(const struct rsx_instruction *instruction);

/* ---------- NV2A vertex programs (ps3_rsx_vp.c) */

#define RSX_VP_MAXIMUM_INSTRUCTIONS 512

/* constants past the NV2A's 192: the clip-space conversion and fixed values */
enum
{
	_rsx_vp_constant_viewport_scale = 58, /* Direct3D's c[-38] */
	_rsx_vp_constant_viewport_offset = 59, /* c[-37] */
	_rsx_vp_constant_screen_scale = 192,
	_rsx_vp_constant_screen_offset = 193,
	_rsx_vp_constant_default = 194, /* 0 0 0 1 */
	_rsx_vp_constant_one = 195, /* 1 1 1 1 */
	_rsx_vp_constant_address_bias = 196, /* 0.001 0 0 0 */
	_rsx_vp_constant_zero = 197,
	_rsx_vp_constant_count = 198,
};

struct rsx_vertex_program
{
	uint32_t *microcode;
	uint32_t instruction_count;
	uint32_t input_mask;
	uint32_t output_mask;
	uint32_t register_count;
};

/* nv2a: the instruction words after the program header, 4 per instruction. 0 on success; the
program's microcode is malloc'd. */
int rsx_vertex_program_translate(const uint32_t *nv2a, uint32_t nv2a_count, struct rsx_vertex_program *program);
void rsx_vertex_program_free(struct rsx_vertex_program *program);
/* the translation as instructions, before encoding (the PC check) */
int rsx_vertex_program_instructions(const uint32_t *nv2a, uint32_t nv2a_count,
	struct rsx_instruction *instructions, uint32_t maximum, uint32_t *count);

/* ---------- NV2A register combiners (ps3_rsx_fp.c) */

#define RSX_FP_MAXIMUM_INSTRUCTIONS 1024

enum
{
	_rsx_sampler_none,
	_rsx_sampler_2d,
	_rsx_sampler_3d,
	_rsx_sampler_cube,
};

/* everything a translated pixel shader depends on but its constants; programs are cached by it */
struct rsx_fragment_key
{
	uint32_t rgb_inputs[8];
	uint32_t rgb_outputs[8];
	uint32_t alpha_inputs[8];
	uint32_t alpha_outputs[8];
	uint32_t final_inputs_abcd;
	uint32_t final_inputs_efg;
	uint32_t combiner_count;
	uint32_t texture_modes;
	uint32_t dot_mapping;
	uint32_t input_texture;
	uint32_t compare_mode;
	uint8_t sampler_type[4];
	uint8_t alpha_kill[4];
	/* D3DTSS_COLORSIGN >> 28: the channels (alpha, red, green, blue from bit 0) that hold signed data */
	uint8_t color_sign[4];
	uint8_t fog_enable;
	/* D3DFOG_*: 0 none (the vertex's fog), 1 exp, 2 exp2, 3 linear */
	uint8_t fog_table_mode;
	/* the stages (bit n) whose texel coordinates are scaled before the lookup: a linear target drawn at the
	screen's scale */
	uint8_t texture_scaled;
	uint8_t padding;
};

/* the per-draw constants: colours as 0..1 floats, the fog's start, end and density, per stage the bump
matrix (00 01 10 11) and its luminance scale and offset, and the texel coordinates' scale (s t 1 1) */
enum
{
	_rsx_uniform_c0 = 0,
	_rsx_uniform_c1 = 8,
	_rsx_uniform_final_c0 = 16,
	_rsx_uniform_final_c1 = 17,
	_rsx_uniform_fog_color = 18,
	_rsx_uniform_fog = 19,
	_rsx_uniform_bump_matrix = 20,
	_rsx_uniform_bump_luminance = 24,
	_rsx_uniform_texture_scale = 28,
	_rsx_uniform_count = 32,
};

#define RSX_FP_MAXIMUM_UNIFORMS 32

struct rsx_fragment_program
{
	/* half-swapped as the RSX reads them, with each uniform's constant zero */
	uint32_t *microcode;
	uint32_t word_count;
	/* the FP_CONTROL bits: 32-bit colour, KIL; and the temporaries */
	uint32_t control;
	uint32_t register_count;
	/* the texture coordinates it reads */
	uint32_t texcoord_mask;
	uint32_t uniform_count;
	struct
	{
		uint16_t word;
		uint8_t uniform;
	} uniforms[RSX_FP_MAXIMUM_UNIFORMS];
};

int rsx_fragment_program_translate(const struct rsx_fragment_key *key, struct rsx_fragment_program *program);
void rsx_fragment_program_free(struct rsx_fragment_program *program);
int rsx_fragment_program_instructions(const struct rsx_fragment_key *key,
	struct rsx_instruction *instructions, uint32_t maximum, uint32_t *count);
/* writes a uniform's value into a copy of the microcode */
void rsx_fragment_program_set_uniform(uint32_t *microcode, uint32_t word, const float value[4]);

#endif
