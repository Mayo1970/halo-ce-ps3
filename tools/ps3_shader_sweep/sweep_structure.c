/* The shader sweep's structure_render.c (in its object's place): while shader_sweep_all_surfaces is set, each window
draws every surface of the BSP, so that its passes set up every material's shader with its lightmap. */

#define structure_render_preprocess structure_render_preprocess_original
#include "structures/structure_render.c"
#undef structure_render_preprocess

boolean shader_sweep_all_surfaces;

void structure_render_preprocess(
	void)
{
	if (shader_sweep_all_surfaces)
	{
		struct structure_bsp *structure_bsp = global_structure_bsp_get();
		long count = structure_bsp->surfaces.count;
		long index;

		if (count > MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
			count = MAXIMUM_RENDERED_ENVIRONMENT_SURFACES;
		csmemset(render.environment_surface_flags, 0, sizeof(render.environment_surface_flags));
		for (index = 0; index < count; index++)
			SET_FLAG(render.environment_surface_flags[index / LONG_BITS], index % LONG_BITS, TRUE);
		render.environment_surface_count = count;
	}
	structure_render_preprocess_original();
}
