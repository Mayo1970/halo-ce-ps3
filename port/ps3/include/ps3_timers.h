/* PS3_TIMERS.H: the timer build: the game thread's time by phase, logged every 300 frames (ps3_timers.c).
Every layer includes it, so fixed-width types only; the game's thread and the Direct3D thread add to the timers. */

#ifndef __HALO_PS3_TIMERS_H
#define __HALO_PS3_TIMERS_H

#include <stdint.h>

enum
{
	/* the main loop (main.c) */
	_ps3_timer_ticks,
	_ps3_timer_render,
	_ps3_timer_throttle,
	_ps3_timer_present,
	/* in the render: the sound (main.c), then each window's parts in order (render.c) */
	_ps3_timer_sound,
	_ps3_timer_window_visibility,
	_ps3_timer_window_objects,
	_ps3_timer_window_lightmaps,
	_ps3_timer_window_lights,
	_ps3_timer_window_diffuse,
	_ps3_timer_window_specular,
	_ps3_timer_window_effects,
	_ps3_timer_window_interface,
	_ps3_timer_window_end,
	/* the specular part's splits in order (render.c); the window's specular part is their sum */
	_ps3_timer_specular_lights,
	_ps3_timer_specular_lightmaps,
	_ps3_timer_specular_masks,
	_ps3_timer_specular_mirrors,
	_ps3_timer_specular_reflections,
	_ps3_timer_specular_transparent,
	_ps3_timer_specular_fog,
	/* the game ticks (game.c, game_time.c): each part, logged in ms a tick */
	_ps3_timer_tick_units,
	_ps3_timer_tick_ai,
	_ps3_timer_tick_players,
	_ps3_timer_tick_effects,
	_ps3_timer_tick_first_person,
	_ps3_timer_tick_scripts,
	_ps3_timer_tick_objects,
	_ps3_timer_tick_hud,
	_ps3_timer_tick_other,
	_ps3_timer_tick_interpolation,
	_ps3_timer_tick_network,
	/* the game's part of every frame, in the ticks' time (game_frame) */
	_ps3_timer_frame_particles,
	_ps3_timer_frame_widgets,
	_ps3_timer_frame_sound,
	_ps3_timer_frame_other,
	/* the particles' part of it: particles_update (its particles' physics, with the particles and those with physics:
	ps3_timer_counts; particles.c), the SPU's update taken (its events), contrails_update, particle_systems_update */
	_ps3_timer_particles_update,
	_ps3_timer_particles_physics,
	_ps3_timer_particles_finish,
	_ps3_timer_contrails_update,
	_ps3_timer_particle_systems_update,
	/* point_physics_update at every call (point_physics.c), ms a frame: its whole time, its wind, its rays; its calls
	and rays (ps3_timer_counts) */
	_ps3_timer_point_total,
	_ps3_timer_point_wind,
	_ps3_timer_point_rays,
	/* of the rays, those that test no surface (_point_physics_force_no_collisions_bit: the weather's), with their calls */
	_ps3_timer_point_free_rays,
	/* ai_update's parts (ai.c), ms a tick; an actor's update by step, the decisions and control by call (actors.c),
	with the actors (ps3_timer_counts) */
	_ps3_timer_ai_conversations,
	_ps3_timer_ai_encounters,
	_ps3_timer_ai_actors,
	_ps3_timer_ai_other,
	_ps3_timer_actor_general,
	_ps3_timer_actor_perception,
	_ps3_timer_actor_situation,
	_ps3_timer_actor_emotion,
	_ps3_timer_actor_orders,
	_ps3_timer_actor_decision_loop,
	_ps3_timer_actor_actions,
	_ps3_timer_actor_clear_output,
	_ps3_timer_actor_action_control,
	_ps3_timer_actor_communication,
	_ps3_timer_actor_conversation,
	_ps3_timer_actor_destination,
	_ps3_timer_actor_look_movement,
	_ps3_timer_actor_move,
	_ps3_timer_actor_look_combat,
	_ps3_timer_actor_unit,
	/* actor_perception_update's parts (actor_perception.c): the refresh, the danger zone, the props' loop with its
	props (ps3_timer_counts) and, in it, the props' position and status refreshes with their count */
	_ps3_timer_perception_refresh,
	_ps3_timer_perception_danger,
	_ps3_timer_perception_props,
	_ps3_timer_perception_prop_refresh,
	/* ai_test_line_of_sight (ai.c): its whole time and calls, the first ray and its calls, the fog, the mode's rays;
	the first rays' length in hundredths of a world unit (a count only) */
	_ps3_timer_los_total,
	_ps3_timer_los_first_ray,
	_ps3_timer_los_fog,
	_ps3_timer_los_mode_rays,
	_ps3_timer_los_length,
	/* actor_action_control by the actor's action, with its calls (actions.c), in the order of the game's actions */
	_ps3_timer_action_none,
	_ps3_timer_action_sleep,
	_ps3_timer_action_alert,
	_ps3_timer_action_fight,
	_ps3_timer_action_flee,
	_ps3_timer_action_uncover,
	_ps3_timer_action_guard,
	_ps3_timer_action_search,
	_ps3_timer_action_wait,
	_ps3_timer_action_vehicle,
	_ps3_timer_action_charge,
	_ps3_timer_action_obey,
	_ps3_timer_action_converse,
	_ps3_timer_action_avoid,
	/* actor_move_update's parts (actor_moving.c): the flying vector avoidance, the movement (in it the pathfinding
	location and the line test), the firing positions; actor_path_refresh wherever it runs, its path builds; calls */
	_ps3_timer_move_avoidance,
	_ps3_timer_move_movement,
	_ps3_timer_move_location,
	_ps3_timer_move_line,
	_ps3_timer_move_firing_positions,
	_ps3_timer_path_refresh,
	_ps3_timer_path_build,
	/* a path build's parts (actor_moving.c): the setup (input, state, debug storage), the search (path_state_find) and
	the nodes it made (a count only), the path made from it; a flying path, a cached state's path; calls */
	_ps3_timer_path_setup,
	_ps3_timer_path_search,
	_ps3_timer_path_nodes,
	_ps3_timer_path_make,
	_ps3_timer_path_flying,
	_ps3_timer_path_cached,
	/* the flying vector avoidance's parts (actor_moving.c): the setup with the objects it found as its count, the
	rays' structure tests and their tests against the objects' pills, with their counts */
	_ps3_timer_avoid_setup,
	_ps3_timer_avoid_structure,
	_ps3_timer_avoid_objects,
	/* path_state_find's parts (path.c): the reset, the begin, the traverse (in it the heap's pops, the surfaces' edges
	with the surfaces as their count, the edges' loop, the closest points in it), the debug copy */
	_ps3_timer_path_reset,
	_ps3_timer_path_begin,
	_ps3_timer_path_traverse,
	_ps3_timer_path_pop,
	_ps3_timer_path_edges,
	_ps3_timer_path_edge_loop,
	_ps3_timer_path_closest,
	_ps3_timer_path_debug_copy,
	/* path_state_build_path's parts (path.c): the smoothing, the obstacle avoidance */
	_ps3_timer_path_smooth,
	_ps3_timer_path_obstacles,
	/* path_smooth's parts (path_smoothing.c): the pill tests to the raw steps, the turning points, the surface found
	and the point projected; with their counts */
	_ps3_timer_smooth_pills,
	_ps3_timer_smooth_turning,
	_ps3_timer_smooth_surface,
	/* physics_update's parts (physics.c): the forces (with the mass points as their count), the movement, the units'
	collisions, the old physics; with their calls */
	_ps3_timer_physics_compute,
	_ps3_timer_physics_mass_points,
	_ps3_timer_physics_move,
	_ps3_timer_physics_unit_collisions,
	_ps3_timer_physics_old,
	/* physics_update_old's parts (physics.c): each mass point's location (the mass points as its count), ground plane,
	water depth and antigravity probe (with the probes), then the object's move */
	_ps3_timer_old_location,
	_ps3_timer_old_ground,
	_ps3_timer_old_water,
	_ps3_timer_old_antigrav,
	_ps3_timer_old_translate,
	/* compute_ground_plane's parts (physics.c, the new physics' and the old's): the features in the sphere with the
	calls and the features found (a count only), the point's test against them with the calls */
	_ps3_timer_ground_features,
	_ps3_timer_ground_feature_count,
	_ps3_timer_ground_point_test,
	/* in those features in the sphere (collisions.c): the bsp's leaves with the calls and the leaves (a count), the
	structure's features, the clusters' objects (the clusters; the objects walked, read from kept blocks), their tests */
	_ps3_timer_ground_leaves,
	_ps3_timer_ground_leaf_count,
	_ps3_timer_ground_structure,
	_ps3_timer_ground_objects,
	_ps3_timer_ground_objects_walked,
	_ps3_timer_ground_objects_read,
	_ps3_timer_ground_object_tests,
	/* vehicle_update's parts (vehicles.c): the physics with the vehicles simulated as its count, the effects after it
	(slipping, suspension, crashing, airborne), the stopping, the animation with the vehicles as its count */
	_ps3_timer_vehicle_physics,
	_ps3_timer_vehicle_effects,
	_ps3_timer_vehicle_stopping,
	_ps3_timer_vehicle_animation,
	/* actor_decision_loop's parts (actors.c): the decisions and the actions performed, with their counts */
	_ps3_timer_decision_decide,
	_ps3_timer_decision_perform,
	/* objects_update (objects.c), in ms a tick: the type updates by kind, then the other steps of an object */
	_ps3_timer_objects_units,
	_ps3_timer_objects_items,
	_ps3_timer_objects_projectiles,
	_ps3_timer_objects_others,
	_ps3_timer_objects_damage,
	_ps3_timer_objects_nodes,
	_ps3_timer_objects_lights,
	_ps3_timer_objects_values,
	_ps3_timer_objects_activation,
	_ps3_timer_objects_cleanup,
	/* object_compute_node_matrices's steps at every call (objects.c), in ms a tick, with its calls and nodes
	(ps3_timer_counts); the matrices: the first node's, the validity checks, the other nodes', the bounding sphere */
	_ps3_timer_nodes_orientations,
	_ps3_timer_nodes_blends,
	_ps3_timer_nodes_root,
	_ps3_timer_nodes_checks,
	_ps3_timer_nodes_children,
	_ps3_timer_nodes_bounds,
	_ps3_timer_nodes_postprocess,
	/* the overlays and blends' parts (_ps3_timer_nodes_blends is the rest): the overlays, the type's preprocess, the
	interpolation from the last animation; with their counts */
	_ps3_timer_nodes_overlays,
	_ps3_timer_nodes_preprocess,
	_ps3_timer_nodes_interpolate,
	/* the render interpolation (render_interpolation.c): a tick's objects kept (the nodes: a count only), then a frame's
	blends: the snap tests (objects), the snapshots' rotations (nodes), the blends (nodes) */
	_ps3_timer_interpolation_objects,
	_ps3_timer_interpolation_nodes,
	_ps3_timer_interpolation_snap,
	_ps3_timer_interpolation_rotations,
	_ps3_timer_interpolation_blend,
	/* the render's objects pass before render_objects (render.c), and in it (render_objects.c) */
	_ps3_timer_render_sky,
	_ps3_timer_render_first_person,
	_ps3_timer_render_lights,
	_ps3_timer_render_find,
	_ps3_timer_render_first_person_draw,
	_ps3_timer_render_lighting,
	_ps3_timer_render_models,
	/* render_model's parts at every call but the shadows' (models.c): the node matrices relative to the model's (the
	nodes as the count), the begin, the parts' draws (the models), the end; the objects' widgets (render_objects.c) */
	_ps3_timer_model_nodes,
	_ps3_timer_model_begin,
	_ps3_timer_model_parts,
	_ps3_timer_model_end,
	_ps3_timer_render_widgets,
	/* the objects' widgets by type, in the game's order (widgets.c), with their count */
	_ps3_timer_widget_flag,
	_ps3_timer_widget_antenna,
	_ps3_timer_widget_glow,
	_ps3_timer_widget_light_volume,
	_ps3_timer_widget_lightning,
	/* in the parts' draws (render_model_parts): the solid draws of model and environment shaders, the decals' draws, the
	transparent parts' centroids and submits, each with its count; the rest: the loops over regions and parts, the links */
	_ps3_timer_parts_model,
	_ps3_timer_parts_environment,
	_ps3_timer_parts_decals,
	_ps3_timer_parts_transparent,
	_ps3_timer_parts_rest,
	/* a model shader's draw (rasterizer_xbox_models.c): render states, textures, colours, vertex shader, constants, fog,
	pixel shader, the draws (their count), the fog screen's submit */
	_ps3_timer_model_draw_states,
	_ps3_timer_model_draw_textures,
	_ps3_timer_model_draw_colours,
	_ps3_timer_model_draw_vertex_shader,
	_ps3_timer_model_draw_constants,
	_ps3_timer_model_draw_fog,
	_ps3_timer_model_draw_pixel_shader,
	_ps3_timer_model_draw_draws,
	_ps3_timer_model_draw_fog_screen,
	/* the lights pass's parts (render.c): the lens flares' occlusion tests, the objects' shadows, the lights' diffuse
	part, the light and alpha-tested decals; the diffuse pass's: the structure's diffuse textures, the decals */
	_ps3_timer_lights_flares,
	_ps3_timer_lights_shadows,
	_ps3_timer_lights_diffuse,
	_ps3_timer_lights_decals,
	_ps3_timer_diffuse_structure,
	_ps3_timer_diffuse_decals,
	/* counts: the decals' batched draws and their vertices (d3d8_rsx.c's d3d8_draw_decals_begin) */
	_ps3_timer_decal_draws,
	_ps3_timer_decal_vertices,
	/* the objects' shadows (render_objects.c): the pass's begin and end, the objects' checks and lighting (the objects),
	a shadow's begin (the calls), its model (the shadows), volume, structure, end; the structure's parts below */
	_ps3_timer_shadow_pass,
	_ps3_timer_shadow_objects,
	_ps3_timer_shadow_begin,
	_ps3_timer_shadow_model,
	_ps3_timer_shadow_volume,
	_ps3_timer_shadow_structure,
	_ps3_timer_shadow_end,
	/* in the structure (structure_render.c): the surfaces found (their count), their triangles built, their draws */
	_ps3_timer_shadow_surfaces,
	_ps3_timer_shadow_triangles,
	_ps3_timer_shadow_draws,
	/* counts: the shadows' models, those whose node matrices the objects' draws kept (models.c) */
	_ps3_timer_shadow_models,
	_ps3_timer_shadow_models_kept,
	/* the render's effects pass (render.c) */
	_ps3_timer_effects_weather,
	_ps3_timer_effects_particles,
	_ps3_timer_effects_contrails,
	_ps3_timer_effects_transparent,
	_ps3_timer_effects_decals,
	_ps3_timer_effects_detail,
	/* the weather's render (weather_particle_systems.c): the particles' update with the particles as its count, the
	sprites built with their count, their draw; the rest is the visibility tests */
	_ps3_timer_weather_update,
	_ps3_timer_weather_sprites,
	_ps3_timer_weather_draw,
	/* in the update, each particle's acceleration and its point_physics_update */
	_ps3_timer_weather_acceleration,
	_ps3_timer_weather_point,
	/* the transparent geometry's draw (rasterizer_transparent_geometry.c): the sort with the groups as its count (in
	it the qsort and the port's model order), the groups' begin and end, each group by its shader's type, with the groups */
	_ps3_timer_transparent_sort,
	_ps3_timer_transparent_qsort,
	_ps3_timer_transparent_order_models,
	/* in the models' order (rasterizer_xbox_transparent_geometry.c): the glass scan, the chains' roots, the models' bounds
	(their calls), the planes' tests, the order written */
	_ps3_timer_order_scan,
	_ps3_timer_order_roots,
	_ps3_timer_order_bounds,
	_ps3_timer_order_planes,
	_ps3_timer_order_write,
	_ps3_timer_transparent_begin_end,
	_ps3_timer_transparent_effect,
	_ps3_timer_transparent_model,
	_ps3_timer_transparent_generic,
	_ps3_timer_transparent_chicago,
	_ps3_timer_transparent_water,
	_ps3_timer_transparent_glass,
	_ps3_timer_transparent_meter,
	_ps3_timer_transparent_plasma,
	_ps3_timer_transparent_other,
	/* counts: the generic shaders' definitions kept from a draw before, and made (shader_transparent_generic_create) */
	_ps3_timer_generic_kept,
	_ps3_timer_generic_made,
	/* counts: the models' bounds of the sort's model order by VMX, of them checked by the scalar loop, and found apart
	(rasterizer_xbox_transparent_geometry.c) */
	_ps3_timer_bounds_vmx,
	_ps3_timer_bounds_checked,
	_ps3_timer_bounds_apart,
	/* sound_render's steps (sound_manager.c), game_sound_update's (game_sound.c), the waits for the mixer's lock */
	_ps3_timer_sound_listener,
	_ps3_timer_sound_looping,
	_ps3_timer_sound_refresh,
	_ps3_timer_sound_prioritize,
	_ps3_timer_sound_channels,
	_ps3_timer_sound_cache,
	_ps3_timer_game_sound_environment,
	_ps3_timer_game_sound_looping,
	_ps3_timer_sound_lock_wait,
	/* a type update's parts (object_types.c), in ms a tick; biped_update's steps (bipeds.c) */
	_ps3_timer_part_unit,
	_ps3_timer_part_biped,
	_ps3_timer_part_vehicle,
	_ps3_timer_part_item,
	_ps3_timer_part_projectile,
	_ps3_timer_part_other,
	_ps3_timer_biped_turning,
	_ps3_timer_biped_moving,
	_ps3_timer_biped_state,
	_ps3_timer_biped_animation,
	_ps3_timer_biped_rest,
	/* the render's interface pass (render.c), interface_draw_screen in it (interface.c, hud.c) */
	_ps3_timer_hud_fog_flares,
	_ps3_timer_hud_screen,
	_ps3_timer_hud_screen_effect,
	_ps3_timer_hud_players,
	_ps3_timer_hud_motion_sensor,
	_ps3_timer_hud_weapon,
	_ps3_timer_hud_unit,
	_ps3_timer_hud_navigation,
	_ps3_timer_hud_messages,
	_ps3_timer_hud_game_engine,
	_ps3_timer_hud_flash,
	_ps3_timer_hud_widgets,
	/* the screen quads (rasterizer_xbox_dynavobgeom.c): render states, constants, textures, shaders, the draw;
	the game thread's Direct3D draws and constants in the interface pass (render.c) */
	_ps3_timer_quad_states,
	_ps3_timer_quad_constants,
	_ps3_timer_quad_textures,
	_ps3_timer_quad_shaders,
	_ps3_timer_quad_draw,
	_ps3_timer_interface_draws,
	_ps3_timer_interface_constants,
	/* the weapon HUD's parts (hud_weapon.c) and the unit HUD's (hud_unit.c; the motion sensor's update and render
	in its part: motion_sensor.c), each with the screen quads it draws (ps3_timer_counts) */
	_ps3_timer_hud_weapon_state,
	_ps3_timer_hud_crosshairs,
	_ps3_timer_hud_weapon_panel,
	_ps3_timer_hud_weapon_sounds,
	_ps3_timer_hud_grenades,
	_ps3_timer_hud_unit_setup,
	_ps3_timer_hud_unit_background,
	_ps3_timer_hud_shield,
	_ps3_timer_hud_health,
	_ps3_timer_hud_sensor_elements,
	_ps3_timer_hud_sensor,
	_ps3_timer_hud_sensor_update,
	_ps3_timer_hud_sensor_render,
	_ps3_timer_hud_auxiliary,
	/* the game thread's Direct3D calls (d3d8_rsx.c): the draws with their prologue (the bytes they copy as the count), the
	vertex constants; no ring waits */
	_ps3_timer_front_draws,
	_ps3_timer_front_constants,
	/* in the draws: the prologue's render states, attributes, targets and textures recorded (the rest: the draw's record) */
	_ps3_timer_front_states,
	_ps3_timer_front_attributes,
	_ps3_timer_front_targets,
	_ps3_timer_front_textures,
	/* the Direct3D thread's time running records, and the game thread's waits for it (d3d8_rsx.c) */
	_ps3_timer_thread_busy,
	_ps3_timer_thread_wait,
	/* the Direct3D thread's records by type, in d3d8_rsx.c's order from _record_wrap, with their count */
	_ps3_timer_record_wrap,
	_ps3_timer_record_states,
	_ps3_timer_record_attributes,
	_ps3_timer_record_texture,
	_ps3_timer_record_vertex_shader,
	_ps3_timer_record_constants,
	_ps3_timer_record_constant_mode,
	_ps3_timer_record_viewport,
	_ps3_timer_record_render_target,
	_ps3_timer_record_ui_offset,
	_ps3_timer_record_clear,
	_ps3_timer_record_draw,
	_ps3_timer_record_draw_indexed,
	_ps3_timer_record_immediate,
	_ps3_timer_record_elements,
	_ps3_timer_record_timestamp,
	_ps3_timer_record_draw_stamps,
	_ps3_timer_record_present,
	/* the Direct3D device's draws (d3d8_rsx.c): the state includes the textures (the stages bound again as the count) and
	the colours */
	_ps3_timer_draw_state,
	_ps3_timer_draw_textures,
	_ps3_timer_texture_hash,
	_ps3_timer_texture_upload,
	_ps3_timer_draw_colours,
	_ps3_timer_draw_vertices,
	_ps3_timer_draw_indices,
	/* the state's other parts: the vertex program found, targets, raster state, screen constants, the fragment key, the
	fragment program found (looked up again as the count), the uniforms (in the colours), vertex program set, constants */
	_ps3_timer_draw_program,
	_ps3_timer_draw_targets,
	_ps3_timer_draw_raster,
	_ps3_timer_draw_screen,
	_ps3_timer_draw_key,
	_ps3_timer_draw_fragment,
	_ps3_timer_draw_uniforms,
	_ps3_timer_draw_vertex_program,
	_ps3_timer_draw_constants,
	/* waits in the present: two flips pending (d3d8_rsx.c), the display flips and the RSX (ps3_gpu.c); the
	conversions' SPU (ps3_spu.c) */
	_ps3_timer_vblank_wait,
	_ps3_timer_flip_wait,
	_ps3_timer_rsx_wait,
	_ps3_timer_spu_wait,
	/* the game thread's waits for its SPU jobs by kind (ps3_spu.c); the shadows' checks against the PPE's search
	(structure_render.c: their time and count), those apart (a count only) */
	_ps3_timer_spu_game_copy_wait,
	_ps3_timer_spu_game_shadow_wait,
	_ps3_timer_spu_game_weather_wait,
	_ps3_timer_spu_game_particles_wait,
	_ps3_timer_spu_game_lights_wait,
	_ps3_timer_spu_game_visibility_wait,
	_ps3_timer_spu_game_sprites_wait,
	_ps3_timer_spu_game_contrails_wait,
	_ps3_timer_spu_game_interpolation_wait,
	_ps3_timer_spu_shadow_check,
	_ps3_timer_spu_shadow_apart,
	/* RSX splits only (ps3_rsx_timers): the frame's start, before each window, after the render */
	_ps3_timer_rsx_previous,
	_ps3_timer_rsx_before,
	_ps3_timer_rsx_after,
	_ps3_timer_count
};

/* the outermost collision test's kind (collisions.c, collision_models.c, collision_usage.c) */
enum
{
	_ps3_collision_vector,
	_ps3_collision_sphere,
	_ps3_collision_pill,
	_ps3_collision_point,
	_ps3_collision_move,
	_ps3_collision_kinds
};

/* the draw stamps' parts (ps3_timers_draw_stamps): the specular part's splits in order, then the effects pass's;
a transparent group's by its maker (ps3_transparent_origin), else by its shader's type as the transparent timers */
enum
{
	_ps3_stamp_specular_lights,
	_ps3_stamp_specular_fog = _ps3_stamp_specular_lights + 6,
	_ps3_stamp_effects_rest,
	_ps3_stamp_weather,
	_ps3_stamp_particles,
	_ps3_stamp_particle_systems,
	_ps3_stamp_contrails,
	_ps3_stamp_effect,
	_ps3_stamp_model,
	_ps3_stamp_generic,
	_ps3_stamp_chicago,
	_ps3_stamp_water,
	_ps3_stamp_glass,
	_ps3_stamp_meter,
	_ps3_stamp_plasma,
	_ps3_stamp_other,
	_ps3_stamp_decals,
	_ps3_stamp_detail,
	_ps3_stamp_count
};

/* the game's collision users and logged collision functions (collision_usage.h) */
#define PS3_COLLISION_USERS 22
#define PS3_COLLISION_FUNCTIONS 8

/* timebase ticks */
extern uint64_t ps3_timers[_ps3_timer_count];
/* a count a timer's part makes (the HUD's parts: screen quads) */
extern uint32_t ps3_timer_counts[_ps3_timer_count];
/* the game ticks run, for the time of one */
extern uint32_t ps3_timer_game_ticks;
/* the screen quads drawn */
extern uint32_t ps3_timer_quads;
/* set while compute_ground_plane's sphere query runs (physics.c), for the timers of its parts (collisions.c) */
extern uint32_t ps3_timer_ground_query;
/* the game thread's Direct3D draws and constants in a timer's spans: render.c's splits and parts, render_model's */
extern uint64_t ps3_render_d3d[_ps3_timer_count];
/* the collision tests' time and count by the user on top of the game's stack, and their time by kind */
extern uint64_t ps3_collision_timers[PS3_COLLISION_USERS];
extern uint32_t ps3_collision_tests[PS3_COLLISION_USERS];
extern uint64_t ps3_collision_kind_timers[_ps3_collision_kinds];
/* each logged collision function's time and calls at any depth, the bsp nodes its tests entered (collision_bsp.c);
the objects the rays tested (collisions.c) */
extern uint64_t ps3_collision_function_timers[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_collision_function_calls[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_collision_nodes[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_collision_objects;
/* the same for the AI's line-of-sight rays alone (collision_usage.h), with their leaves and the clusters whose
objects they walked (collisions.c) */
extern uint64_t ps3_los_function_timers[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_los_function_calls[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_los_nodes[PS3_COLLISION_FUNCTIONS];
extern uint32_t ps3_los_objects;
extern uint32_t ps3_los_leaves;
extern uint32_t ps3_los_clusters;
/* their loop over a cluster's objects (collisions.c): its time, its marks' and object tests' time, the objects the
copies passed, the objects the copies' iterator walked; ps3_bounds_walked counts every ray's (object_bounds_cache.c) */
extern uint64_t ps3_los_loop_time;
extern uint64_t ps3_los_test_time;
extern uint32_t ps3_los_candidates;
extern uint32_t ps3_los_walked;
extern uint32_t ps3_bounds_walked;
/* the objects the iterator read from a cluster's kept block (object_bounds_cache.c): every ray's, the AI's rays' */
extern uint32_t ps3_bounds_read;
extern uint32_t ps3_los_read;
/* the sounds' obstruction (game_sound.c): the calls, and those computed (not from the cache) */
extern uint32_t ps3_obstruction_calls;
extern uint32_t ps3_obstruction_computed;
/* the RSX's nanoseconds from each split to the next, from its timestamps (ps3_gpu.c), and per frame; the draws in
each split's part */
extern uint64_t ps3_rsx_timers[_ps3_timer_count];
extern uint64_t ps3_rsx_frame_nanoseconds;
extern uint32_t ps3_rsx_frames;
extern uint32_t ps3_rsx_draws[_ps3_timer_count];
/* the stamped draws' RSX nanoseconds and draws by the Direct3D device's key for their pixel shader, in the specular
part [0] and the effects pass [1], and by part; a stamp is the key and part << PS3_DRAW_STAMP_PART_SHIFT */
#define PS3_DRAW_STAMP_KEYS 1024
#define PS3_DRAW_STAMP_PART_SHIFT 10
extern uint64_t ps3_draw_stamp_nanoseconds[2][PS3_DRAW_STAMP_KEYS];
extern uint32_t ps3_draw_stamp_draws[2][PS3_DRAW_STAMP_KEYS];
extern uint64_t ps3_draw_stamp_part_nanoseconds[_ps3_stamp_count];
extern uint32_t ps3_draw_stamp_part_draws[_ps3_stamp_count];
/* the frames stamped, the draws past a frame's stamps */
extern uint32_t ps3_draw_stamp_frames;
extern uint32_t ps3_draw_stamp_dropped;
/* the game thread's stamp part (ps3_timers_draw_stamps, -1 none); the part of the transparent groups made now
(render.c sets it around the particles' and contrails' submits; 0: by the shader's type) */
extern int ps3_draw_stamps_part;
extern int ps3_transparent_origin;
/* the Direct3D device's lines of the stamped pixel shaders, at each log (d3d8_rsx.c); NULL: none */
extern void (*ps3_timers_shaders_log)(void);
/* the Direct3D SPU's timers into the Direct3D thread's, at each window's end (d3d8_rsx.c); NULL: none */
extern void (*ps3_timers_direct3d_collect)(void);

/* the timebase, read again while its low word is 0 (PSL1GHT's __gettime: a Cell erratum) */
static inline uint64_t ps3_timer_now(void)
{
	uint64_t ticks;

	__asm__ volatile("1: mftb %0; cmpwi 7,%0,0; beq- 7,1b" : "=r"(ticks) : : "cr7");
	return ticks;
}

/* adds the time since start to the timer; returns the time now, the start of a next span */
static inline uint64_t ps3_timer_add(int timer, uint64_t start)
{
	uint64_t now = ps3_timer_now();

	ps3_timers[timer] += now - start;
	return now;
}

/* adds the time and the screen quads since a part's start to the timer, and starts the next part there */
static inline void ps3_timer_quad_part(int timer, uint64_t *span, uint32_t *quads)
{
	*span = ps3_timer_add(timer, *span);
	ps3_timer_counts[timer] += ps3_timer_quads - *quads;
	*quads = ps3_timer_quads;
}

/* the main loop's top: one call a frame; every 300 frames it logs the times and starts again */
void ps3_timers_frame(void);
/* a split of the RSX's frame: a timestamp report in the Direct3D thread's order (d3d8_rsx.c), which ps3_gpu.c
reads two frames on (no stall); returns the time now, the start of a next span */
uint64_t ps3_timers_rsx_wait(int part);
/* the draws from here stamped on the RSX in a part (_ps3_stamp_*) or not (-1), in the Direct3D thread's order
(d3d8_rsx.c), in one frame of eight: each draw's time by its pixel shader and part */
void ps3_timers_draw_stamps(int part);

#endif
