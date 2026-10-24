/* The shader sweep's Direct3D side (tools/ps3_shader_sweep.py): the PC build's draws make the PS3's pixel shader key
(port/ps3/xbox/d3d8_rsx.c: fragment_key_build, bind_textures), cleaned, before d3d8_gl.c's own (sweep_real_*). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xgpu.h"
#include "../../port/ps3/src/ps3_rsx_program.h"

#define TEXTURE_STAGES 4
#define MAXIMUM_KEYS 65536
#define MAXIMUM_TARGETS 256
#define MAXIMUM_LEFTOVERS 256
/* the model shader's set-up (rasterizer_xbox_models.c, its dirty flag): its texture modes and its 8 combiners, of
which it never writes alpha stages 3 to 7 */
#define MODEL_TEXTURE_MODES 0x18421
#define MODEL_COMBINERS 8

void WINAPI sweep_real_D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count);
void WINAPI sweep_real_D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count,
	CONST WORD *index_data);
void WINAPI sweep_real_D3DDevice_End(void);
void WINAPI sweep_real_D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture);
void WINAPI sweep_real_D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil);
void WINAPI sweep_real_D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result);
void WINAPI sweep_real_D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2);

/* sweep_game.c: called at each frame's end; the label of the keys made now */
void shader_sweep_frame(void);
extern const char *shader_sweep_site;

/* the game's pixel shader definition (rasterizer_xbox_pixel_shader.h), which every set-up but the progress bar's
writes into */
struct pixel_shader_definition
{
	unsigned long alpha_inputs[8];
	unsigned long final_combiner_inputs_abcd;
	unsigned long final_combiner_inputs_efg;
	unsigned long constant_0[8];
	unsigned long constant_1[8];
	unsigned long alpha_outputs[8];
	unsigned long rgb_inputs[8];
};

extern struct pixel_shader_definition pixel_shader;

/* what the definition held in alpha stages 3 to 7 at a draw: the model shader's set-up leaves it there */
struct leftover
{
	unsigned long inputs[5];
	unsigned long outputs[5];
};

static struct leftover leftovers[MAXIMUM_LEFTOVERS];
static unsigned long leftover_count;

struct key_entry
{
	struct rsx_fragment_key key;
	const char *site;
};

static struct key_entry keys[MAXIMUM_KEYS];
static unsigned long key_count, draw_count;
static D3DBaseTexture *textures[TEXTURE_STAGES];
static DWORD targets[MAXIMUM_TARGETS];
static unsigned long target_count;
static DWORD current_target;

static void target_add(DWORD data)
{
	unsigned long index;

	if (!data)
		return;
	for (index = 0; index < target_count; index++)
	{
		if (targets[index] == data)
			return;
	}
	if (target_count < MAXIMUM_TARGETS)
		targets[target_count++] = data;
}

static BOOL target_find(DWORD data)
{
	unsigned long index;

	for (index = 0; index < target_count; index++)
	{
		if (targets[index] == data)
			return TRUE;
	}
	return FALSE;
}

static void key_add(struct rsx_fragment_key *key)
{
	unsigned long index;

	rsx_fragment_key_clean(key);
	for (index = 0; index < key_count; index++)
	{
		if (!memcmp(&keys[index].key, key, sizeof(*key)))
			return;
	}
	if (key_count < MAXIMUM_KEYS)
	{
		keys[key_count].key = *key;
		keys[key_count].site = shader_sweep_site ? shader_sweep_site : "frame";
		key_count++;
	}
}

/* as d3d8_rsx.c's fragment_key_build and bind_textures; a linear target's stage both at the screen's scale and not
(the scale is the video mode's) */
static void draw_key(void)
{
	DWORD *rs = D3D__RenderState;
	struct rsx_fragment_key key;
	uint8_t scaled = 0;
	int index;

	draw_count++;
	target_add(current_target);
	{
		struct leftover now;

		memcpy(now.inputs, &pixel_shader.alpha_inputs[3], sizeof(now.inputs));
		memcpy(now.outputs, &pixel_shader.alpha_outputs[3], sizeof(now.outputs));
		for (index = 0; index < (int)leftover_count && memcmp(&leftovers[index], &now, sizeof(now)); index++)
			;
		if (index == (int)leftover_count && leftover_count < MAXIMUM_LEFTOVERS)
			leftovers[leftover_count++] = now;
	}
	memset(&key, 0, sizeof(key));
	for (index = 0; index < 8; index++)
	{
		key.rgb_inputs[index] = rs[D3DRS_PSRGBINPUTS0 + index];
		key.rgb_outputs[index] = rs[D3DRS_PSRGBOUTPUTS0 + index];
		key.alpha_inputs[index] = rs[D3DRS_PSALPHAINPUTS0 + index];
		key.alpha_outputs[index] = rs[D3DRS_PSALPHAOUTPUTS0 + index];
	}
	key.final_inputs_abcd = rs[D3DRS_PSFINALCOMBINERINPUTSABCD];
	key.final_inputs_efg = rs[D3DRS_PSFINALCOMBINERINPUTSEFG];
	key.combiner_count = rs[D3DRS_PSCOMBINERCOUNT];
	key.texture_modes = rs[D3DRS_PSTEXTUREMODES];
	key.dot_mapping = rs[D3DRS_PSDOTMAPPING];
	key.input_texture = rs[D3DRS_PSINPUTTEXTURE];
	key.compare_mode = rs[D3DRS_PSCOMPAREMODE];
	for (index = 0; index < TEXTURE_STAGES; index++)
	{
		key.alpha_kill[index] = D3D__TextureState[index][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
		key.color_sign[index] = (unsigned char)((D3D__TextureState[index][D3DTSS_COLORSIGN] >> 28) & 0xf);
	}
	key.fog_enable = rs[D3DRS_FOGENABLE] != 0;
	key.fog_table_mode = (unsigned char)rs[D3DRS_FOGTABLEMODE];
	for (index = 0; index < TEXTURE_STAGES; index++)
	{
		D3DBaseTexture *texture = textures[index];
		unsigned long mode = (rs[D3DRS_PSTEXTUREMODES] >> (5 * index)) & 0x1f;
		struct xgpu_texture_description description;

		key.sampler_type[index] = mode == 0x11 ? _rsx_sampler_2d : _rsx_sampler_none;
		if (!texture || !texture->Data || mode == 0 || mode == 0x04 || mode == 0x05 || mode == 0x11)
			continue;
		xgpu_texture_describe(texture->Format, texture->Size, &description);
		if (target_find(texture->Data))
		{
			key.sampler_type[index] = _rsx_sampler_2d;
			if (description.linear)
				scaled |= (uint8_t)(1 << index);
		}
		else
		{
			key.sampler_type[index] = description.cube_map ? _rsx_sampler_cube :
				description.depth > 1 ? _rsx_sampler_3d : _rsx_sampler_2d;
		}
	}
	key_add(&key);
	if (scaled)
	{
		key.texture_scaled = scaled;
		key_add(&key);
	}
}

/* each model shader's key again with every alpha stage 3 to 7 an earlier set-up left in the definition */
static void model_leftovers(void)
{
	unsigned long count = key_count, index, leftover;

	shader_sweep_site = "model leftovers";
	for (index = 0; index < count; index++)
	{
		struct rsx_fragment_key key = keys[index].key;

		if (key.texture_modes != MODEL_TEXTURE_MODES || (key.combiner_count & 0xff) != MODEL_COMBINERS)
			continue;
		for (leftover = 0; leftover < leftover_count; leftover++)
		{
			memcpy(&key.alpha_inputs[3], leftovers[leftover].inputs, sizeof(leftovers[leftover].inputs));
			memcpy(&key.alpha_outputs[3], leftovers[leftover].outputs, sizeof(leftovers[leftover].outputs));
			key_add(&key);
		}
	}
	platform_log("shader sweep: %lu leftovers of alpha stages 3 to 7, %lu keys more with them", leftover_count,
		key_count - count);
}

/* rasterizer_xbox_decals.c's set-up: each decal key with the alpha stage 1 an earlier blend function of its layer
leaves (add, subtract and max write none), and with the fog hack's third combiner (a window whose atmospheric fog
is whole) and without it */
static void decal_leftovers(void)
{
	static const unsigned long alpha_inputs[] = { 0, 0x341c1420, 0x341c14a0, 0x34180000 };
	unsigned long count = key_count, index, variant;

	shader_sweep_site = "decal leftovers";
	for (index = 0; index < count; index++)
	{
		struct rsx_fragment_key key = keys[index].key;
		unsigned long combiners = key.combiner_count & 0xff, alpha;

		if (key.texture_modes != 1 || key.final_inputs_abcd != 0xc || key.final_inputs_efg != 0x1c00 ||
			key.rgb_outputs[0] != 0xc00 || key.rgb_outputs[1] != 0xc00 || key.alpha_outputs[1] != 0xc00 ||
			(combiners != 2 && combiners != 3) || (key.rgb_inputs[0] != 0x08040000 &&
			key.rgb_inputs[0] != 0x28240820 && key.rgb_inputs[0] != 0xa8240820))
		{
			continue;
		}
		for (variant = 0; variant < 2; variant++)
		{
			key.combiner_count = variant ? 3 : 2;
			key.alpha_inputs[2] = variant ? 0x1c151115 : 0;
			key.alpha_outputs[2] = variant ? 0xc00 : 0;
			if (key.rgb_inputs[0] != 0x08040000 || key.rgb_inputs[1] != 0x340c0000)
			{
				key_add(&key);
				continue;
			}
			for (alpha = 0; alpha < sizeof(alpha_inputs) / sizeof(alpha_inputs[0]); alpha++)
			{
				key.alpha_inputs[1] = alpha_inputs[alpha];
				key_add(&key);
			}
		}
	}
	platform_log("shader sweep: %lu keys more with the decals' leftovers", key_count - count);
}

/* the keys, one shader_keys.txt line each with its first site; the file named by HALO_SHADER_SWEEP_OUT */
void shader_sweep_write(const char *map_name)
{
	const char *path = getenv("HALO_SHADER_SWEEP_OUT");
	FILE *file;
	unsigned long index;

	model_leftovers();
	decal_leftovers();
	file = path ? fopen(path, "w") : NULL;
	if (!file)
	{
		platform_log("shader sweep: cannot write %s", path ? path : "(HALO_SHADER_SWEEP_OUT not set)");
		return;
	}
	for (index = 0; index < key_count; index++)
	{
		char text[RSX_FRAGMENT_KEY_TEXT];

		rsx_fragment_key_text(&keys[index].key, text);
		fprintf(file, "%s # %s %s\n", text, map_name, keys[index].site);
	}
	fclose(file);
	platform_log("shader sweep: %lu keys from %lu draws written to %s", key_count, draw_count, path);
}

unsigned long shader_sweep_key_count(void)
{
	return key_count;
}

void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT vertex_count)
{
	if (vertex_count)
		draw_key();
	sweep_real_D3DDevice_DrawVertices(primitive_type, start_vertex, vertex_count);
}

void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE primitive_type, UINT vertex_count, CONST WORD *index_data)
{
	if (vertex_count && index_data)
		draw_key();
	sweep_real_D3DDevice_DrawIndexedVertices(primitive_type, vertex_count, index_data);
}

void WINAPI D3DDevice_End(void)
{
	draw_key();
	sweep_real_D3DDevice_End();
}

void WINAPI D3DDevice_SetTexture(DWORD stage, D3DBaseTexture *texture)
{
	if (stage < TEXTURE_STAGES)
		textures[stage] = texture;
	sweep_real_D3DDevice_SetTexture(stage, texture);
}

void WINAPI D3DDevice_SetRenderTarget(D3DSurface *render_target, D3DSurface *depth_stencil)
{
	if (render_target)
		current_target = render_target->Data;
	sweep_real_D3DDevice_SetRenderTarget(render_target, depth_stencil);
}

void WINAPI D3DDevice_GetBackBuffer(INT back_buffer, D3DBACKBUFFER_TYPE type, D3DSurface **result)
{
	sweep_real_D3DDevice_GetBackBuffer(back_buffer, type, result);
	if (*result)
		target_add((*result)->Data);
}

void WINAPI D3DDevice_Present(CONST RECT *source_rectangle, CONST RECT *destination_rectangle,
	void *unused, void *unused2)
{
	sweep_real_D3DDevice_Present(source_rectangle, destination_rectangle, unused, unused2);
	shader_sweep_frame();
}
