/* The shader sweep's rasterizer_xbox_transparent_geometry.c (in its object's place): while shader_sweep_transparent
is set, each kind of group a frame draws is drawn again with every shader tag of the map its draw takes (the same
type, the generic and chicago family, and on a model's or a BSP's geometry the glass, meter, plasma and water that
come on it), with and without the sky's and the no-fog flags. */

#define rasterizer_transparent_geometry_group_draw rasterizer_transparent_geometry_group_draw_original
#include "rasterizer/xbox/rasterizer_xbox_transparent_geometry.c"
#undef rasterizer_transparent_geometry_group_draw

#include "cache/cache_files.h"

extern const char *shader_sweep_site;
void platform_log(char const *format, ...);

boolean shader_sweep_transparent;
/* the groups drawn again, and the kinds of group seen */
long shader_sweep_transparent_draws;
static unsigned long transparent_kinds_done[4];

static short transparent_family(
	short type)
{
	switch (type)
	{
		case _shader_type_transparent_generic:
		case _shader_type_transparent_chicago:
			return _shader_type_transparent_generic;

		default:
			return type;
	}
}

/* the donor's shader family, its geometry's source and its lightmap: what the draw's set-up can depend on */
static long transparent_kind(
	struct transparent_geometry_group const *group)
{
	return transparent_family(group->shader->base.type) * 16 +
		(group->triangle_buffer ? 1 : 0) +
		(group->vertex_buffer ? 2 : 0) +
		(group->lightmap ? 4 : 0) +
		(group->node_matrix_count ? 8 : 0);
}

static real_rgb_color const neutral_colors[16] =
{
	{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
};
static real const neutral_values[16] =
{
	0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
};
static struct render_animation const neutral_animation = { neutral_colors, neutral_values };

static void transparent_sweep(
	struct transparent_geometry_group const *group)
{
	static unsigned long const flag_variants[] =
	{
		0,
		FLAG(_rasterizer_geometry_no_fog_bit),
		FLAG(_rasterizer_geometry_sky_bit),
	};
	struct transparent_geometry_group copy = *group;
	short family = transparent_family(group->shader->base.type);
	struct tag_iterator iterator;
	long tag_index;

	copy.previous_group_presorted_index = NONE;
	copy.next_group_presorted_index = NONE;
	copy.active_camouflage_transparent_source_object_index = 0;
	tag_iterator_new(&iterator, SHADER_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		struct shader *shader = shader_definition_get(tag_index);
		short variant;

		/* a model's or a BSP's generic group also takes the types that come on such geometry */
		if (transparent_family(shader->base.type) != family &&
			!(family == _shader_type_transparent_generic && group->node_matrix_count &&
				(shader->base.type == _shader_type_transparent_glass ||
				shader->base.type == _shader_type_transparent_meter ||
				shader->base.type == _shader_type_transparent_plasma)) &&
			!(family == _shader_type_transparent_generic && !group->node_matrix_count && group->vertex_buffer &&
				(shader->base.type == _shader_type_transparent_glass ||
				shader->base.type == _shader_type_transparent_water)))
		{
			continue;
		}
		copy.shader = shader;
		copy.shader_permutation_index = 0;
		/* another type reads the animation as its own (a meter's value): values in its range */
		copy.animation = transparent_family(shader->base.type) == family ? group->animation : &neutral_animation;
		for (variant = 0; variant < NUMBEROF(flag_variants); variant++)
		{
			copy.geometry_flags = (group->geometry_flags &
				~(FLAG(_rasterizer_geometry_no_fog_bit) | FLAG(_rasterizer_geometry_sky_bit))) | flag_variants[variant];
			rasterizer_transparent_geometry_group_draw_original(&copy, FALSE);
			shader_sweep_transparent_draws++;
		}
	}
}

void rasterizer_transparent_geometry_group_draw(
	struct transparent_geometry_group *group,
	boolean dirty)
{
	static boolean sweeping;

	rasterizer_transparent_geometry_group_draw_original(group, dirty);
	/* (a camouflage group's draw is the model's, and widgets have no shader) */
	if (shader_sweep_transparent && !sweeping && !dirty && group->shader &&
		group->shader->base.type != _shader_type_model)
	{
		long kind = transparent_kind(group);

		if (kind >= 0 && kind < 4 * LONG_BITS && !BIT_VECTOR_TEST_FLAG(transparent_kinds_done, kind))
		{
			char const *site = shader_sweep_site;

			BIT_VECTOR_SET_FLAG(transparent_kinds_done, kind, TRUE);
			platform_log("shader sweep: transparent kind %ld (shader type %d) at %s", kind,
				(int)group->shader->base.type, site ? site : "");
			sweeping = TRUE;
			shader_sweep_site = "transparent";
			transparent_sweep(group);
			shader_sweep_site = site;
			sweeping = FALSE;
		}
	}
}
