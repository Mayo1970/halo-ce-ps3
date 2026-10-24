# Pixel shader key audit (PS3 RSX fragment programs)

Step 2 of shader.md (read-only audit). Line numbers are for the tree as of 2026-10-22 (HEAD f84f09ea plus the working tree changes).

## 1. How the key is made

- `prepare_draw_state` (port/ps3/xbox/d3d8_rsx.c:1880) is the only place that makes a key. It clears the key, then calls `fragment_key_build` (:1445), then `bind_textures` (:1202), then `rsx_fragment_key_clean` (port/ps3/src/ps3_rsx_fp.c:2916).
- `fragment_key_build` copies all 8 combiner stages, the final inputs, `PSCOMBINERCOUNT`, `PSTEXTUREMODES`, `PSDOTMAPPING`, `PSINPUTTEXTURE`, `PSCOMPAREMODE`, ALPHAKILL and COLORSIGN of stages 0-3, `FOGENABLE` and `FOGTABLEMODE`.
- `bind_textures` sets `sampler_type` per stage: 2D, 3D or cube from the bound RSX texture. It sets none when no texture is bound, when the mode is 0, 4 or 5, or when `texture_get` fails. Mode 0x11 (dot product) gets 2D, and the clean removes it.
- `bind_textures` sets `texture_scaled` bit n only for a render target that is linear and whose scale is not 1 (d3d8_rsx.c:1242). See section 2.4.
- `rsx_fragment_key_clean` does these things:
  - It clears the rgb/alpha inputs and outputs of the stages at and above `combiner_count & 0xff`.
  - It sets the mode of a stage to none when the stage is not active. A sampling mode with no texture is not active (`stage_active`, ps3_rsx_fp.c:2898).
  - It keeps `compare_mode` only for clip-plane stages, `dot_mapping` and `input_texture` only for stages that read them.
  - It clears ALPHAKILL and COLORSIGN only on stages that are not active.
  - It clears `fog_enable` when no combiner input and no final input reads the fog register (`reads_fog`, :767). It clears `fog_table_mode` when fog is off or the mode is above 3.
  - It does NOT clear: the 0x100 mux bit and the 0x1000 / 0x10000 unique-constant bits of `combiner_count`; the inputs and outputs of stages below the count; ALPHAKILL on active stages; the sampler type of an active stage.

## 2. Global state

### 2.1 Pixel shader upload

- `rasterizer_set_pixel_shader` (rasterizer_xbox.c:619). With fewer than 6 combiners (`MAXIMUM_INLINE_PIXEL_SHADER_COMBINERS`, :144) it writes the render states one by one (:639-686). It writes only the stages below the count. It never writes `PSCOMPAREMODE`.
- With 6 or more combiners it calls `IDirect3DDevice8_SetPixelShaderProgram` (:692). The PS3 version (d3d8_rsx.c:4236) copies all 8 stages and `PSCOMPAREMODE` from the definition.
- All game sites use one global scratch definition, `pixel_shader` (rasterizer_xbox.h:165, common symbol in port/linux/src/halo_linker_common.c:129). The progress bar has its own two (`blur_shader`, `regular_shader`).
- No game site uses texture mode 5 (clip plane). So the `PSCOMPAREMODE` left over never reaches a key: the clean removes it.

### 2.2 Fog and colour sign

| State | Where it is set | Value in every key |
|---|---|---|
| `D3DRS_FOGENABLE` | Once, at device init: rasterizer_xbox.c:3027 (TRUE). No other write in `source/`, `port/ps3/` or `port/linux/game/`. | 1 when a combiner reads fog, else cleaned to 0 |
| `D3DRS_FOGTABLEMODE` | Never set. The port's state array starts at 0. | 0 |
| `D3DTSS_COLORSIGN` | Never set by the game or the port. | 0 |

- There is no variable-index `SetRenderState` or `SetTextureStageState` in `source/`. `SetRenderStateSmart` (progress_bar.c:1457) and `SetTextureStageStateSmart` (progress_bar.c:1586) write through. Their numeric callers (motion_sensor.c:527-532: 0x3B, 0x3C, 0x3E, 0x3F, 0x4A, 0x7B) are blend, alpha-test and z states, not PS states.
- Result: fog adds no variants. It matters only as "this key reads fog" (enable 1, table mode 0). The shaders that read fog are:
  - environment.c:3210, specular spot light (final EFG 0x1D330D00);
  - environment_fog.c:1290, fog screen with `no_texture` (final EFG 0x3300);
  - models.c M1/M3 with `true_atmospheric_fog`;
  - water.c:802 and :968 with the water `atmospheric_fog` flag;
  - the debug vector mode index 23 (environment.c:413).

### 2.3 Texture binding and the sampler type

| Function | Line (rasterizer_xbox.c) | Type check | Sampler type in the key |
|---|---|---|---|
| `rasterizer_set_texture` | :1656 | Yes. A mismatch binds `default_textures[type]` | The requested type (0 = 2D, 1 = 3D, 2 = cube) |
| `rasterizer_set_texture_non_blocking` | :932 | Yes | The requested type. While the bitmap loads it binds nothing and returns TRUE: the previous texture stays |
| `rasterizer_set_texture_direct` | :1576 | No | The bitmap tag's own type |
| `rasterizer_set_texture_direct_non_blocking` | :1616 | No | The bitmap's type; nothing bound while it loads |
| `rasterizer_set_texture_bitmap_data` | :1547 | No | The bitmap's type |
| `rasterizer_set_target_as_texture` | :1015 (SetTexture :1120) | n/a | 2D render target |

- Cache full: `rasterizer_get_bitmap_default_hardware_format` (:3103) gives the default **2D** texture for a 3D bitmap (:3119). A project3d stage then gets a 2D sampler.
- PS3: when `texture_get` fails, the stage gets sampler none. The clean then sets that stage's mode to none. This is a different key for the same draw.
- `_rasterizer_frame_end` (:1950) binds NULL on stages 0-3 (:1965). The first draws of a frame see none on every stage they do not bind.

### 2.4 Render targets and `texture_scaled`

| Target | Format (Xbox) | PS3 `texture_scaled` |
|---|---|---|
| render_primary (back buffer), render_primary_copy | 0x00011229, LIN_A8R8G8B8, screen size | Set when the screen scale is not 1 |
| render_secondary | LIN_A8R8G8B8, 320x240 (:2784) | Set when the screen scale is not 1 |
| progress bar back-buffer texture | 0x11229, 640x480 | Set when the PS3 finds it as the screen target and the scale is not 1 |
| water bumpmap, shadow primary/secondary, sun glow primary/secondary | Swizzled | Never |

The screen scale is fixed for one video mode (`screen_mode_choose`, d3d8_rsx.c:343). 480p 4:3 gives scale 1. 480p wide, 576, 720 and 1080 do not. So each key that samples a linear target has 2 forms, and one run uses only one.

### 2.5 State set once and inherited

- Device init (rasterizer_xbox.c:3014-3034): `FOGENABLE` TRUE, `TEXCOORDINDEX` n = n.
- Every frame end: textures 0-3 set to NULL (:1965).
- `_rasterizer_frame_begin` (:1804), `_rasterizer_windows_begin` (:461), `_rasterizer_window_begin` (:1330), `_rasterizer_window_set_fog` (:483): no key state. Only fog constants, `FOGCOLOR`, stencil, target, fill mode.
- Per pass in environment.c: stage 3 normalization cube (:931); stages 2/3 at each pass begin (:1218/:1226, :1639/:1647, :1961/:1969); per light stage 1 (:1035, :1739, :3172); per lightmap (:971, :2043, :2265). The draws inherit these.

## 3. Sites per file

Column "AK/CS/Fog" gives ALPHAKILL, COLORSIGN, FOGENABLE/FOGTABLEMODE: "inh." means not set at the site and left over. COLORSIGN is always 0. Fog is as in 2.2.

### 3.1 source/rasterizer/xbox/rasterizer_xbox.c

| Item | :1935, `rasterizer_secondary_render_target_debug` (:1831) |
|---|---|
| Source | Built at run time (:1931-1934). memset; texture_modes 1, count 1, ABCD 0x08 (T0) |
| Tag fields | None |
| Runtime | Debug `secondary_render_target_debug_enabled`, window target render_primary (:1842) |
| Textures | s0 render_secondary target (:1849), 2D, scaled by video mode |
| AK/CS/Fog | AK s0 inh. (0 in practice). No fog read |
| Keys | 1 (x2 video mode). Debug only |

### 3.2 source/rasterizer/xbox/rasterizer_xbox_debug.c

All built at run time (memset, count 1, texture_modes 0). No tag field. No texture (all stages cleaned). AK inh. but cleaned. No fog read. Debug only.

| Site | Function | Shader | Key |
|---|---|---|---|
| :88 | `rasterizer_debug_drawing_begin` (:41), opaque | ABCD 4 (V0), EFG 0 | A |
| :117 | same, not opaque | ABCD 4, EFG 0x1400 | B |
| :173 | `_rasterizer_debug_immediate_begin` (:130) | ABCD 4, EFG 0 | A |
| :384 | `_rasterizer_debug_immediate_begin_screenspace` (:313) | ABCD 4, EFG 0 | A |

Keys: 2.

### 3.3 source/rasterizer/xbox/rasterizer_xbox_text.c

| Item | :374, `rasterizer_text_begin` (:141) |
|---|---|
| Source | Built at run time (:336-368). texture_modes `1 | (map[1]!=0)<<5 | (map[2]!=0)<<10`. Count 0x11102. Fixed inputs. ABCD 0x0C, EFG 0x1C00 |
| Tag fields | None. Font bitmap only |
| Runtime | `draw_dynamic_screen_geometry`, target 0 (:154). Callers (rasterizer_text.c:494, :624) set only map[0], so texture_modes is always 1 |
| Textures | s0 font bitmap, `_bitmap_data` (:284), 2D |
| AK/CS/Fog | AK s0 set to DISABLE at :380-384. No fog read |
| Keys | 1 (3 possible in code, 1 used) |

### 3.4 source/rasterizer/xbox/rasterizer_xbox_widgets.c

| Item | :383, `_rasterizer_widget_begin` (:291), occlusion test | :514, same, sprite |
|---|---|---|
| Source | Run time (memset). Count 1, texture_modes 0, ABCD (0,0,0,ONE) | Run time (memset, :450-513). s0 project2d, 3 fixed combiners, fixed final |
| Tag fields | None | Light volume `map`, lens flare `primary_map`: texture only |
| Runtime | Lens flare occlusion (rasterizer_lights.c:773) | Light volumes (light_volumes.c:276), lens flare sprites (rasterizer_lights.c:854) |
| Textures | None | s0 via `_rasterizer_widget_set_texture` (:529): `set_texture_non_blocking` type 0 (:542) or `direct_non_blocking` for the glow (:549). While loading nothing is bound. light_volumes.c:279 ignores this and draws with the previous s0 texture (any type) |
| AK/CS/Fog | Cleaned. No fog | AK s0 inh. No fog |
| Keys | 1 | 1 (rare extra from the leftover s0 type) |

### 3.5 source/rasterizer/xbox/rasterizer_xbox_motion_sensor.c

All run time (memset), texture_modes 1, count 1. No tag field: bitmaps are interface globals. Runs with debug `draw_hud_motion_sensor`, when textures are loaded (:125-126, :334-336). AK s0 inh. No fog read.

| Site | Function | Shader | s0 | Key |
|---|---|---|---|---|
| :229 | `_rasterizer_hud_motion_sensor_blip_begin` (:104) | ABCD 0x08040000 | Blip bitmaps (:130, :259), 2D. Draws into sun_glow_primary | M1 |
| :445 | `_rasterizer_hud_motion_sensor_blip_end` (:306), sweep | rgb 0x08040000/0xC0, alpha 0x18140000/0xC0, ABCD 0x0C, EFG 0x1C00 | Sweep bitmap (:338), 2D | M2 |
| :487 | same, sweep mask | ABCD 8, EFG 0x1800 | Sweep mask (:468), 2D | M3 |
| :569 | same, composite | same as :487 | sun_glow_primary target (:513), swizzled, not scaled | M3 |

Keys: 3.

### 3.6 source/rasterizer/xbox/rasterizer_xbox_plasma_energy.c

| Item | :321, `rasterizer_plasma_energy_draw` (:133) |
|---|---|
| Source | Run time (memset, :300-320). texture_modes 0x42 (s0, s1 project3d). Count 0x104. ABCD 0x0C0F0000, EFG 0x1C1C1400 |
| Tag fields | `shader_transparent_plasma` noise maps: texture only |
| Runtime | Debug `plasma_energy_enabled`. Called from the transparent pass |
| Textures | s0, s1 `rasterizer_set_texture` type 1 (:208, :215): 3D. Cache-full fallback gives 2D |
| AK/CS/Fog | AK s0/s1 inh. No fog |
| Keys | 1 (up to 4 with the 2D fallback) |

### 3.7 source/interface/progress_bar.c

| Item | :1899, `progress_bar_render` (:1835), blur | :1924, same, regular |
|---|---|---|
| Source | Built once: `blur_shader` in `progress_bar_make_stuff_ready` (:2003-2015). texture_modes 0x8421, count 3, ABCD 0x0C, EFG 0x1C00. `SetPixelShaderProgram` directly, so `PSCOMPAREMODE` = 0 | Built once: `regular_shader` (:2017-2025). texture_modes 0x21, count 1, ABCD 0x200C0000, EFG 0x1C00. `SetPixelShaderProgram` |
| Tag fields | None (`loading.tga`, :2089) | None |
| Runtime | Only while a map is copied to the cache (game.c:771, render.c:215) | Same |
| Textures | s0-s3 `back_buffer_texture` (:1909) over `GetBackBuffer(-1)` (:1822). 0x11229 linear 640x480, 2D; `texture_scaled` 0xF when found as the screen target and scale is not 1 | s0 `texture0` 320x240 LIN (:2087), s1 `texture1` 128x16 LIN (:1947), both 2D, not targets. All stages NULL at :1927 |
| AK/CS/Fog | AK inh. No fog | Same |
| Keys | 1 (x2 video mode) | 1 |

- progress_bar.c:1470 is only the `D3DRS_PSTEXTUREMODES` branch of the `SetRenderStateSmart` helper. No caller passes a PS state through it.

### 3.8 source/rasterizer/xbox/rasterizer_xbox_environment.c

All sites clear `pixel_shader` and build it at run time. No static shader table; only the debug `vector_modes[51]` (:389-441, final inputs only). No site sets AK, CS or fog states.

| Site | Function | Source | Tag fields | Runtime | Textures | Keys |
|---|---|---|---|---|---|---|
| :568 | `_rasterizer_environment_lightmap_draw`, debug vectors | :532-568. texture_modes `0x18C60 | (test ? 1 : 3)`. 3 or 1 combiners. ABCD from `vector_modes` | None | `drawing_mode == vectors`, `pad3` | `set_texture_direct` :462/:468/:480/:491/:502: s0 `test[3]` (tag type) or normalization; s1-s3 normalization (cube by tag) | ~10, debug |
| :877 | same, lightmap pass | No self-illum: 2 combiners (:684-698). With it: 0x00011106, 6 combiners, whole program (:758-797). texture_modes 0x18001/0x18021/0x18401/0x18421. ABCD changed at :805-831 | `self_illumination.map` NONE or not | `lightmap_missing` (:971-982); `lightmap_mode` (no_lightmap forced by structure_render.c:946-949); `lightmap_incident_radiosity_enabled`; debug `drawing_mode` (:833-875) | s0 bump, type 0 (:621), 2D. s1 self-illum, type 0 (:638). s2 lightmap (:971) 2D or `SetTexture(2, NULL)` (:981). s3 normalization cube (:931, inherited) | 8 max (2-4 typical); debug 12+ |
| :1266 | `_rasterizer_environment_diffuse_lights_begin` | Once per pass. texture_modes 0x18861 (2D, cube, 3D, cube), 4 combiners. :1106 and :1183 write constants only | Light `gel.map` (texture only) | `draw_environment_diffuse_lights`. Light count does not matter | s0 bump type 0 (:1145). s1 gel type 2 (:1035) cube. s2 `distance_attenuation` direct (:1218), 3D by tag. s3 normalization (:1226) | 1 |
| :1599 | `_rasterizer_environment_diffuse_texture_draw` | texture_modes 0x8421, 3 combiners | `environment.type` (3, :1477-1510), `detail_map_function` (3, :1512-1534), `micro_detail_map_function` (3, :1538-1561) | `drawing_mode` sets ABCD (:1564-1590) | s0-s3 base, detail, detail 2, micro, type 0 (:1378/:1390/:1402/:1414), 2D | 27; +54 debug |
| :1781 | `_rasterizer_environment_specular_light_begin`, point | :1747-1781. texture_modes 0x18C41, 0x00011006 (whole program). Edited by :1906 before each draw | `runtime_cosine_falloff_angle`, gel maps choose point or spot | Per light | s1 `distance_attenuation` (:1739) 3D. s2/s3 normalization (:1639/:1647) | 0 alone |
| :3211 | `rasterizer_environment_specular_spot_light_begin` | :3177-3211. texture_modes 0x18C61, 6 combiners. EFG 0x1D330D00 reads fog. Edited by :1906 | `gel.map` or `gel.secondary_map` | Per light | s1 gel type 2 (:3172), cube | 0 alone |
| :1906 | `_rasterizer_environment_specular_light_draw` | Edits the global from :1781/:3211 (:1858-1905): rgb_inputs[0..1], rgb_outputs[2], [4], count 6 or 8, alpha 6-7. All edited fields set in both branches | `bump_map_is_specular_mask`; `specular.flags` overbright, extra_shiny; skipped when brightness <= 0 | Point or spot base | s0 bump type 0 (:1825) | 16 |
| :2026 | `_rasterizer_environment_specular_lightmaps_begin` | :1991-2026. texture_modes 0x18C21, 6 combiners. Edited by :2173 | None | `lightmap_mode == normal` | s2/s3 normalization (:1961/:1969) | 0 alone |
| :2173 | `_rasterizer_environment_specular_lightmap_draw` | Edits :2026's global (:2125-2172) | Same as :1906; needs `specular_lightmap_bit` | Skipped when lightmap missing | s0 bump (:2094) 2D. s1 lightmap (:2043) 2D | 8 |
| :2245 | `_rasterizer_environment_reflection_lightmap_masks_begin` | Once per pass. texture_modes 1, 2 combiners. :2336 writes `PSCONSTANT1_0` only | Brightness fields gate the draw | `lightmap_mode == normal`, `draw_environment_reflection_lightmap_masks` | s0 lightmap (:2265) | 1 |
| :2589 | `_rasterizer_environment_reflection_mirror_draw` | :2483-2589. Modes (2D, cube, cube, 2D), 5 combiners, inline | `reflection.flags` dynamic_mirror; `bump_map` NONE (rgb_inputs[0]); `bump_map_is_specular_mask` (EFG) | `has_mirror`, target render_primary | s0 bump (:2409). s1/s2 normalization (:2421/:2429). s3 render_secondary target (:2437), scaled by video mode | 4 (x2 video mode) |
| :2900 | `_rasterizer_environment_reflection_draw` | :2771-2900. Bumped: (2D, dot product, dot_rflct_diff, dot_rflct_spec) with fixed dot mapping and input texture. Flat: (2D, cube, cube, cube). 5 combiners | `reflection.type`; flat when specular mask or no bump map (:2652-2664); `bump_map` NONE; specular mask (EFG) | `draw_environment_reflections` | s0 bump (:2683). s1/s2 normalization (:2695/:2703). s3 `cube_map` type 2 (:2711), cube | 5 |

- SetTexture at :981 binds NULL on s2 for the missing-lightmap case of :877 (mode 0 there).
- `_rasterizer_environment_transparent_geometry_submit` (:2940-3110) sets no shader. Its draws go through the transparent geometry file.
- Key subtotal: about 70 at most in normal play; about 80 more in debug modes.

### 3.9 source/rasterizer/xbox/rasterizer_xbox_environment_fog.c

Note: the enum at :97-99 has wrong names. Type 4 is shader_model (not chicago). Type 8 is water (not generic). The struct at :175-182 is the shader_model layout. So the test at :1331 means "shader_model without `not_alpha_tested`", and :1345 binds that model's `base_map`.

| Site | Function | Source | Tag fields | Runtime | Textures | AK/CS/Fog | Keys |
|---|---|---|---|---|---|---|---|
| :565 | `_rasterizer_environment_fog_begin` | Run time, fixed (:541-564). Modes 0x21, count 0x11002 | None (constants only) | `drawing_mode == normal`, `draw_environment_fog` (:477-478) | s0 `atmospheric_fog_density` direct (:508), s1 `planar_fog_density` direct (:517). Type by globals tag (2D) | AK s1 inh. (cleared at :1423 in the previous frame). No fog read | 1 |
| :1317 | `_rasterizer_environment_fog_screen_begin` | Run time (:1285-1316). (a) `no_texture`: modes 0, 1 combiner, EFG 0x3300 (reads fog). (b) textured: modes 1, alpha 0x28110820/0xC00, EFG 0x3C00. The model loop changes modes in place: `D3DDevice_SetRenderState_PSTextureModes(0x21)` at :1343 and `(1)` at :1385 | Fog `fog_screen.flags` `no_texture`; `no_environment_multipass`, `no_model_multipass` (:1197-1203); per model group `shader.type`, shader_model `not_alpha_tested` (:1331-1337), `base_map` (:1345) | Fog screen active (:673-735); `opaque_model_count > 0`. Runs for fog pass 0 and 1 | (b) s0 `planar_fog_density` direct (:1296). Alpha-tested model: s1 base_map, `set_texture` type 0 (:1345), 2D. (a) with modes 0x21: s0 is whatever an earlier draw left (any type, maybe a scaled target) | AK s1 ENABLE at :1344; DISABLE at :1386 (model, environment, water groups only) and :1423 after the loop. Other group types keep the previous AK and modes | (b) 2; (a) 1 + 2 x leftover s0 types (3-7) |
| :1678 | `_rasterizer_environment_fog_screen_end`, layers | Run time (:1566-1677). 4 combiners, unique c0/c1. Modes and some inputs depend on `layer_count` | `fog_screen.layer_count` (1-4, :651-671), `fog_screen.map` | Same as :1317 | s0..s(layers-1) `screen->map` direct (:1529), type by bitmap (2D in stock data) | inh. No fog | up to 4 |
| :1708 | same, second pass | Run time: memset, count 1, modes 0 | None | Fog pass 0 and `fog_screen_drawn` | None | Cleaned | 1 |

### 3.10 source/rasterizer/xbox/rasterizer_xbox_lights.c (sun glow)

All sites in `rasterizer_sun_glow_draw` (:87) or its helpers. They run with debug `lens_flare_sun_glow_enabled`, occlusion pixels, and a lens flare tag with `occlusion_radius == 50` or the `sun` flag (rasterizer_lights.c:1123-1136). All run-time built, fixed. No tag field selects them. No AK/CS/fog set; no fog read.

| Site | Function | Shader | Textures | Keys |
|---|---|---|---|---|
| :230 | sun glow, occlusion clear | modes 0, 1 combiner, EFG 0x1100 | None | 1 |
| :322 | sun glow, glow alpha | modes 1, alpha 0x18200000, EFG 0x1C00 | s0 `glow` direct (:256), 2D | 1 |
| :426 | sun glow, ray passes | modes 1, 2 combiners | s0 sun glow target (:368), swizzled | 1 |
| :822 | `rasterizer_sun_glow_copy_source` | modes 0x21, 1 combiner | s0 render_primary (:695), scaled by video mode; s1 `glow` (:722) | 1 (x2 video mode) |
| :984 / :1024 | `rasterizer_sun_glow_convolve` | modes 0x8421, 2 combiners; :1024 changes constant 0 only | s0-s3 sun glow targets (:993), swizzled | 1 |

### 3.11 source/rasterizer/xbox/rasterizer_xbox_shadows.c

All sites run when the target is render_primary and `draw_environment_shadows` is on. Run-time built, memset. No AK/CS/fog set; no fog read.

| Site | Function | Source | Tag fields | Runtime | Textures | Keys |
|---|---|---|---|---|---|---|
| :271 + :432/:436 | `_rasterizer_environment_shadow_begin` and `_rasterizer_environment_shadow_model_draw` | 1 combiner, ABCD 0x20, EFG 0x1800. Before each model draw a direct `D3DRS_PSTEXTUREMODES` write sets 0 (:432) or 1 (:436). The :271 modes are never drawn | shader_model (:282) `not_alpha_tested` (:430); `base_map` (:437) | `shadow_debug_enabled` (clear colour only) | Modes 1: s0 base_map, `set_texture` type 0, 2D | 2 |
| :755 | `_rasterizer_environment_shadow_draw` | modes 0x21, 4 combiners. Debug changes ABCD 0x2C to 0x0C (:744-750) | None | `shadow_debug_enabled`; `shadow_convolution_enabled` picks target 3 or 2 | s0 shadow target (:588), swizzled. s1 `linear_corner_fade` direct (:618). s2 normalization (:648), mode none, cleaned | 1 (2 with debug) |
| :985 | `rasterizer_shadow_convolve` (and port pass :1045-1089) | modes 0x8421, 1 combiner | None | `shadow_convolution_enabled` | s0-s3 shadow targets (:871, :1056), swizzled | 1 |

AK s0 at :432/:436 is inherited and kept under modes 1.

### 3.12 source/rasterizer/xbox/rasterizer_xbox_models.c

`set_environment_shader_pixel_shader` (:634-967) builds into the global `pixel_shader` with no memset. Local tables: `detail_mask_alpha_inputs[9]` (:664), `detail_function_alpha_inputs[3]` (:674), `detail_function_rgb_inputs[3]` (:690/:735). `local_pixel_shader_dirty_flag` (:443) picks a full set or a partial edit. Only `_rasterizer_models_begin` (:472) and the no_queue transparent path (:2547) set it TRUE.

| Item | M1: :902 full set (dirty TRUE) | M2: :907-958 partial edit (dirty FALSE) | M3: :1425-1487 `rasterizer_model_draw_environment_shader` |
|---|---|---|---|
| Source | Run time in the global. texture_modes 2D, 2D, 2D, CUBE (:805). Count 8 + unique c0/c1 (:810), so whole program (`SetPixelShaderProgram`). Writes rgb stages 0-7 and alpha stages 0-2. **Never writes alpha_inputs/outputs[3..7]**, dot_mapping, input_texture, compare_mode | Direct `SetRenderState`: `PSRGBINPUTS3` (:925), `PSRGBINPUTS6/OUTPUTS6` (:929/:933), `PSRGBINPUTS7/OUTPUTS7` (:937/:941), `PSFINALCOMBINERINPUTSABCD/EFG` (:953/:957), plus constants. Edits the bound M1 shader | Around one draw (:1454): `PSRGBINPUTS0` (:1429), `PSRGBOUTPUTS0` (:1437), EFG (:1445); restored at :1462/:1470/:1479 |
| Tag fields | shader_model `detail_function` (3), `detail_mask` (9), flag `detail_after_reflection`, flag `true_atmospheric_fog` (final reads fog). For an environment shader on a model: `diffuse.detail_map_function` (3), flag `true_atmospheric_fog`. Not in the key: two_sided, not_alpha_tested, alpha_blended_decal, cube map, multipurpose, self-illum | Same as M1 | shader_environment `detail_map_function` (3), `true_atmospheric_fog` (2) |
| Runtime | First model draw of each models phase: sky, objects, king hill, camouflage underlay. Alpha 3-7 come from the last writer of the global: environment.c :784-791, :1767-1776, :1893; dynavobgeom.c :427, :514-524, :551-580; transparent_geometry.c :2041, :2564. The camouflage underlay gets the last transparent shader's | Every later model draw in the phase | Model part with an environment shader and no effect |
| Textures | s0 base type 0 (:1741/:1085), s1 detail type 0 (:1758/:1102), s2 multipurpose type 0 (:1775) or bump (:1119), s3 cube type 2 (:1792/:1140). Always 2D, 2D, 2D, cube | Same | Same |
| AK/CS/Fog | AK s0-s3 inh. (all active, kept). Fog read only with `true_atmospheric_fog` | Same | Same |
| Keys | 3 x 9 x 2 x 2 = 108, times the alpha 3-7 leftovers (usually 1-3). Typical map: 5-20 | 0 new (if M1 stays bound) | 6, times the leftovers |

### 3.13 source/rasterizer/xbox/rasterizer_xbox_active_camouflage.c

| Item | C1: :348 `rasterizer_active_camouflage_cache_primary_render_target` | C2: :595 `rasterizer_active_camouflage_draw`, intensity 1 | C3: :849 same, distortion |
|---|---|---|---|
| Source | Run time (memset :336). Modes 2D. 1 combiner (zero). ABCD (0,0,0,T0) | Run time (memset :583). Modes 2D. 1 combiner. EFG (0,0,T0.a). Colour writes off | Run time (memset :797). Modes CUBE, DOTPRODUCT, DOT_ST, NONE. Dot mapping stages 1-2 MINUS1_TO_1_D3D. 2 combiners. ABCD (T2,R0,0,0), EFG (0,0,C0.a) |
| Tag fields | None | shader_model `base_map` (texture) | globals rasterizer data `active_camouflage_flags` `tint_edge_density` (rgb_inputs[0], 2 values); `active_camouflage_distortion` (s0) |
| Runtime | `active_camouflage_enabled`, a camouflaged model seen. Debug multipass repeats | intensity == 1.0, transparent phase (transparent_geometry.c:1400) | Every camouflaged group, intensity > 0 |
| Textures | s0 render_primary (:244), scaled by video mode | s0 `set_texture` type 0 (:482), 2D | s0 `set_texture_direct` (:646): bitmap's own type (should be cube). s2 render_secondary (:681), scaled by video mode |
| AK/CS/Fog | AK s0 inh. No fog | AK s0 inh. No fog | AK s0-s2 inh. No fog |
| Keys | 1 (x2 video mode) | 1 | 1 per map (x2 video mode) |

Partial cloak (intensity < 1, :630-640) goes to models.c M1 with the dirty flag set.

### 3.14 source/rasterizer/xbox/rasterizer_xbox_dynavobgeom.c

| Item | D1: :594 `_rasterizer_psuedo_dynamic_screen_quad_draw` (HUD, menu, cinematic, bink quads) |
|---|---|
| Source | Run time (memset :397 or :439). **Meter** (:385-436): modes 2D, 4 combiners 0x00011104, rgb_inputs[2] low byte from `flash_color_is_negative` (:419), ABCD 0x0C180000, EFG 0x1C00. **Maps** (:437-591): one 2D stage per map[0..2]. Stage 2 by `map0_to_1_blend_function` (switch :476: alpha_blend, multiply, double_multiply, add, reverse_subtract, min; min uses stages 2-5). Next stage by `map1_to_2_blend_function` (switch :536; plasma variant at :540). Unhandled values leave a zero stage that is still counted. Count `n | 0x11100` (:586); min gives 6-7, whole program. ABCD from `alpha_weighted` (:589) |
| Tag fields | HUD multitexture overlay `map_blending_function[0..1]` (hud_draw.c:744; CE remap hud_draw.c:581); overlay bitmaps (map count); overlay `framebuffer_blend_function` with CE tags sets `alpha_weighted` (hud_draw.c:783); HUD meter flash colour sign. ui_widget.c:1184-1213 always uses min + alpha_blend + plasma, `alpha_weighted` FALSE |
| Runtime | `draw_dynamic_screen_geometry`, target render_primary (:223-230) |
| Textures | `rasterizer_set_texture_bitmap_data(i, map[i])` (:340), no type check: bitmap's own type (2D in practice) |
| AK/CS/Fog | Meter sets AK s0 ENABLE (:391-395). Every quad sets AK s0 DISABLE (:610-614). Maps branch: s0 normally DISABLE; s1, s2 inh. No fog read |
| Keys | Meter 2. One map 2. Two maps up to 14. Three maps up to 86, plus 1 ui_widget plasma. About 65 can occur; typical map 5-15 |

### 3.15 source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.c

All sites in `rasterizer_transparent_geometry_group_draw` (:951-3478); the type switch is at :3391. No combiner here reads fog (the transparent "fog" is an extra stage that reads v0.a, v1.a, v1.rgb or c0). No clip-plane mode.

| Site | Use | Source | Tag fields | Runtime | Textures | AK | Keys |
|---|---|---|---|---|---|---|---|
| :1118 | Debug colour | Inline (:1062-1117): modes 0, count 1, ABCD 1 | None | `debug_transparent_geometry_enabled` (:978) | None | cleaned | 1 |
| :1226 | Cortana depth pre-pass | Inline (:1224-1225): count 1, all 0 | None | `effect_type == cortana` (:1198) | None | cleaned | 1 |
| :2837 | Glass tint | Inline (:2820-2836): modes 1, count 1. Camouflage adds alpha0 | `tint_map` or `tint_color` (:2751) | `effect_type == active_camouflage` (:2827) | s0 `set_texture` type 0 (:2758) | inh. | 2 |
| :3040 | Glass reflection | Inline (:2963-3039): count 0x11005. Bumped modes 0x62E21 + dot mapping 0x111; flat 0x18C61; mirror 0x8C61. Camouflage adds alpha1 (:3015) | `reflection_type`; bumped falls to flat with specular mask or no bump (:2741-2749); `reflection_bump_map` (rgb0, :2990); `bump_map_is_specular_mask` (EFG, :3036) | Mirror needs `has_mirror`, target 0 (:2737) | s0 bump type 0 (:2855). s1/s2 normalization direct (:2866/:2876), cube by tag. s3 render_secondary (:2890, scaled by video mode) or `reflection_map` type 2 (:2899) | inh. | ~18 |
| :3159 | Glass diffuse | Inline (:3144-3158): modes 0x421, count 3 | `diffuse_map` or `diffuse_detail_map` (:3044) | None | s0 diffuse (:3049), s1 detail (:3060), s2 lightmap `_bitmap_data` (:3088) or default 2D (:3097) | inh. | 1 |
| :3402 | Meter | Inline (:3356-3387): modes 1, count 0x11104. Debug override (:3389-3400): count 1, ABCD 0x08/0x18 | `flash_color_is_negative` (rgb2, :3376) | `debug_meter_shader_enabled && pad3` | s0 `set_texture` type 0 (:3277) | ENABLE :3357 (DISABLE :3393 debug); restored DISABLE :3404 | 2 (+2 debug) |
| :3419 a | shader_effect (particles, contrails) | Inline (:1654-1776). rgb combiners 3 (nonlinear tint) or 1; +1 secondary map; +1 fog stage by blend function. Count 1-5 | `uses_nonlinear_tint`; `secondary_map`, `secondary_map_anchor`; `framebuffer_blend_function` (8 values, 5 fog patterns) | Fog stage needs `draw_environment_fog` and no `no_fog_bit`. Z-sprite (modes 0x54421, input_texture 0x110000) needs `zsprite_enabled`, anchor zsprite, secondary map, not first-person (:1685-1689) | s0 `group->lightmap` `_bitmap_data` (:1432), bitmap's type; NULL keeps the old texture. s1 secondary 2D (:1456, :1715) | inh. | up to 36 |
| :3419 b | shader_transparent_generic | `shader_transparent_generic_create` (:2040) on every draw; fog stage patched at index max(min(stages,7),1) (:2044-2190) | Whole tag (see 3.16) | `draw_environment_fog`; sky with alpha_blend uses the planar fog combiner (rgb 0x310C1101). Extra layers drawn as own groups (:1795-1813) | Stages < min(maps,4): `set_texture` (:1903). s0 2D or cube by `first_map_type`; s1-s3 2D | inh. | ~1-2 per distinct tag structure |
| :3419 c | shader_transparent_chicago | `shader_transparent_chicago_create` (:2563); fog stage at PIN(maps,0,4) (:2567-2716) | Whole tag (see 3.17) | Same as generic | `set_texture` (:2414), as generic | inh. | ~1-2 per distinct tag structure |

Water (:2721), plasma (:3409) and model camouflage (:1400, :1276) go to their own files.

### 3.16 source/rasterizer/xbox/shader_transparent_generic_preprocessor.c

`shader_transparent_generic_create` (:487-607) runs at draw time. It clears the definition, then:
- texture_modes (:507-512): s0 none with no maps, cube if `first_map_type != 0`, else 2D. s1-s3 2D if `maps.count > n`.
- combiner_count (:540): `(max(min(stages,7),1) + 1) | 0x11100`. The +1 is the caller's fog stage.
- Inputs (:544-593): per stage `color_input_A..D`, `alpha_input_A..D` with mappings, through static tables (25-entry colour and alpha register tables, 8 mappings, `shader_stage_constant_input_registers` 5x8). No entry is the fog register.
- Outputs: `color_output_AB`, `_CD`, `_AB_CD_mux_sum`, output mapping, dot-product functions, `color_mux`, `alpha_mux`, `alpha_output_A..C`, alpha output mapping.
- No stages: default stage 0 (:597-600). Final ABCD 12, EFG 0x1C00.
- Bound: the number of distinct generic tags (layers included) in the map.

### 3.17 source/rasterizer/xbox/shader_transparent_chicago_preprocessor.c

`shader_transparent_chicago_create` (:129-211) runs at draw time. It clears the definition, then:
- combiner_count (:153): `(min(maps,4) + 1) | 0x11000` (no mux bit).
- texture_modes (:157-160): s0 2D or cube (`first_map_type != 0`); s1-s3 2D per map.
- Combiner m+1 (:179-184) for each map but the last: from the static `shader_transparent_chicago_combiner_table` (:94-118: 2x13 rgb words, 13 alpha words, 13 stage increments), indexed by `color_function`, `alpha_function` (0-12) and flag `alpha_replicate`.
- Last map: stage 0 (0x08200000 / 0x18200000). Outputs 0xC00. Final ABCD 12, EFG 0x1C00.
- Bound: the number of distinct chicago / chicago_extended tags in the map.

### 3.18 source/rasterizer/xbox/rasterizer_xbox_water.c

All run-time built (memset). No AK/CS/fog set. Fewer than 6 combiners.

| Site | Function | Source | Tag fields | Textures | Keys |
|---|---|---|---|---|---|
| :357 / :368 | `rasterizer_water_build_bumpmap` (:181) | :326-356: modes 0x8421, count 0x11004, final 0x310C0100. :368 per mip, constant only | ripples, ripple maps, mipmap levels and fade (constants only) | s0-s3 ripple maps `set_texture` type 0 (:234), 2D. Draws into the water bumpmap target | 1 |
| :613 | `rasterizer_water_draw` (:545), depth only | count 1, modes 0 | `draw_before_fog` flag | None | 1 |
| :738 | same, base alpha modulates reflection | modes 0x61, 2 combiners | `base_map_alpha_modulates_reflection`; base_map | s0 base_map type 0 (:628). s1 normalization direct (:659), cube by tag | 1 |
| :802 | same, base colour modulates background | modes 1, count 1, ABCD `(atmospheric_fog ? 0x13 : 0) << 24 | 0x200800` | `base_map_color_modulates_background`, `atmospheric_fog` (reads fog) | s0 base_map type 0 (:744) | 2 |
| :968 | same, reflection | modes 0x64621, dot mapping 0x111. Fog flag: 4 combiners, final 0x330C0000 (reads fog), or 2 combiners | `atmospheric_fog`; `reflection_map` | s0 water bumpmap target (:806), swizzled. s3 `reflection_map` type 2 (:835), cube | 2 |

### 3.19 source/rasterizer/xbox/rasterizer_xbox_screen_effect.c

`_rasterizer_screen_effect` (:462) runs with debug `screen_effects_enabled` and target render_primary. Pass count is even, so the `pass_count == 1` branches (:625-636, :818-831, :982-994) never run. The port's extra passes (scale-based) do not change keys. The effect reads render_primary and target 7 (render_primary_copy): both linear, scaled by video mode. No AK/CS/fog set; no fog read.

| Site | Function | Source | Tag fields | Textures | Keys |
|---|---|---|---|---|---|
| :808 | video effect | Pass 0: modes 1, 1 combiner, final 0x08. Pass 1: modes 0x421, 4 combiners, rgb_outputs[3] by overbright scale | `video_overbright_mode` (3), scanline map, noise map | Pass 0: s0 render_primary (:642). Pass 1: s0 target 7 (:594), s1 scanline and s2 noise `_bitmap_data` (:601, :614), bitmap's type | 4 |
| :980 | convolution, mask, filters | :814-979. Bases (A) no conv pass 0: modes 1. (B) no conv pass 1 + mask: modes 0x21, rgb 0x09200000, EFG 0x800. (C) no conv pass 1, no mask: modes 0x21, rgb 0x08200000. (D)/(E) conv with/without mask: modes 0x8421, 4 combiners. Last pass: light enhancement +2 stages, desaturation +1 (or +2 with light enhancement), additive or not, mask flags. Count `| 0x11000`, up to 7 (whole program) | `convolution_type`, `convolution_mask`, `convolution_extra_passes` (count only), `filter_light_enhancement_intensity > 0`, `filter_desaturation_intensity > 0`, `filter_desaturation_is_additive`, the two `uses_convolution_mask` flags | (A) s0 render_primary. (B) s0 mask, s1 target 7. (C) s0 target 7; **s1 not bound** (leftover). (D) s0 mask, s1-s3 target. (E) s0-s3 target. Mask `_bitmap_data` (:635, :651, :678). Targets :642, :653, :659, :680-684, :691-697 | up to 85 + 3; 1-3 per cinematic effect in practice |
| :1417 | `_rasterizer_screen_flash` (:1197) | 1 combiner, modes 0; stage 0 inputs by flash type (lighten/darken share) | Runtime `screen_flash.type`; debug `screen_flash_enabled` | None | 5 |

### 3.20 source/rasterizer/xbox/rasterizer_xbox_decals.c

| Item | :704, `_rasterizer_decals_draw` (:615); prepared in `_rasterizer_decals_begin` (:465) |
|---|---|
| Source | Edited in place. Begin (:588-605) clears the global, sets modes 1, outputs, final 0x0C/0x1C00; with the fog hack 3 combiners and alpha_inputs[2] 0x1C151115, else 2. Draw writes rgb_inputs[0], rgb_inputs[1] and, except for add / reverse_subtract / max, alpha_inputs[1]. Set at :704 when the blend function changes |
| Tag fields | Decal `framebuffer_blend_function` (8 values); decal map |
| Runtime | Fog hack: debug `filthy_decal_fog_hack_enabled` (default TRUE, rasterizer.c:676) and window fog `atmospheric_maximum_density == 1.0`. Alpha-tested layer never uses it. Decal order decides alpha_inputs[1] for add / reverse_subtract / max |
| Textures | s0 default 2D (:501), then decal map type 0 (:715). Always 2D |
| AK/CS/Fog | inh. No fog read (the hack reads v1 and c0) |
| Keys | up to 16; typical 3-6 |

### 3.21 source/rasterizer/xbox/rasterizer_xbox_detail_objects.c

| Item | :376, `_rasterizer_detail_objects_begin` (:295) |
|---|---|
| Source | Run time (:367-375): modes 1, 1 combiner, rgb 0x08040000, alpha 0x18140000 |
| Tag fields | None (collection map is texture only) |
| Runtime | Debug `draw_detail_objects`. Skipped in split screen (:302) |
| Textures | s0 collection map `set_texture` type 0 (:600), 2D |
| AK/CS/Fog | inh. No fog |
| Keys | 1 (maps with detail objects only) |

### 3.22 PS3-only code (port/ps3, port/linux/game)

- No file in port/ps3/xbox/, port/ps3/src/ or port/linux/game/ sets a pixel shader, calls `SetPixelShaderProgram`, or writes a `D3DRS_PS*`, fog or COLORSIGN state.
- hud_hires_rsx.c only swaps the texels of button and icon bitmaps. `bind_textures` gets a 2D RSX texture from it. It has no shader of its own.
- `ps3_gpu_present` uses no key.
- port/linux/game/tag_schema_render.c names pixel shaders only in comments.

## 4. Draw-order state that `rsx_fragment_key_clean` does not remove

1. **Model alpha stages 3-7 (largest effect).** models.c M1 (:902) never writes alpha_inputs/outputs[3..7] and uploads all 8 stages (whole program). The key holds whatever the last writer of the global `pixel_shader` left: environment lightmap/specular, dynavobgeom meter/min quads, transparent generic/chicago. The camouflage underlay inherits the last transparent shader.
2. **ALPHAKILL on active stages.** The clean keeps it. Setters: dynavobgeom.c:394/:613 (s0), text.c:380-384 (s0 DISABLE), environment_fog.c:1344/:1386/:1423 (s1), transparent_geometry.c:3357/:3393/:3404 (s0). All are paired, so the leftover is 0 in practice. Inside the fog screen model loop, groups that are not model, environment or water keep the previous group's s1 AK and modes.
3. **Fog screen `no_texture` with alpha-tested models.** environment_fog.c:1343 turns s0 to project2d over the texture an earlier pass left on s0. Its sampler type and `texture_scaled` go into the key.
4. **Screen effect case (C).** screen_effect.c:980 uses modes 0x21 but binds nothing on s1. The leftover s1 texture (or none) decides the key.
5. **Decals.** decals.c:704: add / reverse_subtract / max keep alpha_inputs[1] from the previous decal of the layer.
6. **Partial edits of the bound shader.** models.c M2 (:907-958) and M3 (:1425-1487) need M1 to be bound. environment.c :1906 and :2173 edit the global from :1781/:3211 and :2026. Fog screen :1343/:1385 and shadows :432/:436 write `PSTEXTUREMODES` over the bound shader. All are correct today because no other shader is set in between.
7. **Pending or missing textures.** `set_texture_non_blocking` / `_direct_non_blocking` leave the old texture while a bitmap loads (light_volumes.c:279 draws anyway). `_bitmap_data(stage, NULL)` binds nothing (transparent effect with no bitmap). A missing default texture leaves the old one.
8. **Texture failures on PS3.** `texture_get` failure gives sampler none, so the clean sets the mode to none: a new key.
9. **Cache-full fallback.** A 3D bitmap becomes the default 2D texture (rasterizer_xbox.c:3119): project3d stage with a 2D sampler (plasma, environment `distance_attenuation`).
10. **Stale transparent definition.** transparent_geometry.c:2040 / :2563 skip the create call when `success` is already FALSE, and the old global (plus the fog patch) is set at :3419. Failure path only.
11. **Tag data not range-checked.** models.c `detail_mask` / `detail_function` index local arrays with only `match_assert`. A generic or chicago tag with `first_map_type >= 4` gets the cube mode but a 2D sampler (transparent_geometry.c:1873, :2384).
12. **PSCOMPAREMODE** is left over at every inline set, but no site uses the clip-plane mode, so it never reaches a key.

## 5. Estimated number of distinct keys

### 5.1 Fixed sites (do not depend on the map's tags beyond "is this feature used")

| Area | Normal play | Debug only |
|---|---|---|
| Text, widgets, motion sensor, plasma | 7 | - |
| Progress bar (loading) | 2 | - |
| Debug draws, secondary target view | - | 3 |
| Environment (lightmap, diffuse, specular, reflections) | up to ~70 | ~80 |
| Fog, fog screen | 5-10 | - |
| Sun glow | 5 | - |
| Shadows | 4 | 1 |
| Camouflage | 3 | - |
| Transparent fixed (cortana, glass, meter) | ~24 | 3 |
| Water | up to 7 | - |
| Screen flash | 5 | - |
| Decals | up to 16 | - |
| Detail objects | 1 | - |

### 5.2 Tag-driven sites

| Area | Bound |
|---|---|
| Models M1 + M3 | up to 114, times 1-3 alpha 3-7 leftovers; typical 5-20 |
| HUD / menu quads (dynavobgeom) | up to ~87; typical 5-15 |
| shader_effect | up to 36; typical 10-20 |
| transparent_generic | 1-2 per distinct tag structure |
| transparent_chicago | 1-2 per distinct tag structure |
| Screen effects | up to ~92; 1-3 per effect used |

### 5.3 Per map type (estimates)

| Map type | What draws | Estimated distinct keys |
|---|---|---|
| ui.map (menus) | HUD/menu quads, text, the background scene (environment, sky models, some transparent) | about 40-80 |
| Multiplayer | Environment subset, models, generic/chicago, effects, HUD, sun glow, shadows, decals, some water | about 100-180 |
| Campaign | Same plus fog screens, cinematic screen effects, camouflage, cortana, glass, more tags | about 150-300 |
| Theoretical upper bound per map | Every fixed key plus every tag-driven combination | about 500 plus the generic/chicago tag count |

- `texture_scaled`: about 10 keys sample a linear target (render_primary, render_primary_copy, render_secondary, progress bar). Each has 2 forms, but one video mode uses only one. Precompute both forms, or one set per video mode class (scale 1 or not).
- Fog adds no variants (2.2). COLORSIGN adds none.
- The leftover cases in section 4 (items 1, 3, 4, 5, 7, 8, 9) can add keys that no offline pass over the tags can predict. An offline set should be backed by the run-time translation.
