/* SPU_GAME.H: the game thread's jobs on two SPUs (spu_game.c): the checkpoint's copies and the particles' rays; the
render's shadows, weather, lights, visibility, sprites, contrails. The SPU, ps3_spu.c and the game's units share it. */

#ifndef __HALO_PS3_SPU_GAME_H
#define __HALO_PS3_SPU_GAME_H

#include <stdint.h>

/* jobs, a power of two */
#define SPU_GAME_RING 64

#define SPU_GAME_RUNNING 1
#define SPU_GAME_EXITED 2

/* spu_game_job.kind */
#define SPU_GAME_COPY 1
#define SPU_GAME_ZEROES 2
#define SPU_GAME_ZERO 3
#define SPU_GAME_SHADOW 4
#define SPU_GAME_WEATHER 5
#define SPU_GAME_PARTICLES 6
#define SPU_GAME_LIGHTS 7
#define SPU_GAME_VISIBILITY 8
#define SPU_GAME_SPRITES 9
#define SPU_GAME_CONTRAILS 10
#define SPU_GAME_KINDS 11

/* a job's answer: done, or refused (the PPE does that job) */
#define SPU_GAME_DONE 1
#define SPU_GAME_REFUSED 2

/* COPY source to target, ZEROES (all zero: the answer at target), ZERO target: size bytes, all 16-aligned. The
others: size bytes of parameters at source (16-aligned), with their answers */
typedef struct
{
	uint32_t kind;
	uint32_t size;
	uint32_t source;
	uint32_t target;
} spu_game_job;

typedef struct
{
	volatile uint32_t status;
	volatile uint32_t value;
	uint32_t pad[2];
} __attribute__((aligned(16))) spu_game_answer;

/* read once at the start */
typedef struct
{
	uint32_t ring;
	uint32_t pad[31];
} __attribute__((aligned(128))) spu_game_setup;

/* the PPE writes it; its store ends the SPU's reservation of the line, which wakes the SPU */
typedef struct
{
	volatile uint32_t head;
	volatile uint32_t quit;
	uint32_t pad[30];
} __attribute__((aligned(128))) spu_game_bell;

/* the SPU puts it after each job, behind the job's output; the PPE only reads it */
typedef struct
{
	volatile uint32_t state;
	volatile uint32_t tail;
	volatile uint32_t ls_address;
	volatile uint32_t stage;
	volatile uint32_t wakes;
	volatile uint32_t timeouts;
	/* the tag data cache's reads: found, fetched; the jobs refused */
	volatile uint32_t cache_hits;
	volatile uint32_t cache_misses;
	volatile uint32_t refused;
	uint32_t pad0[3];
	/* by kind: the jobs, their timebase ticks (the SPU's decrementer), and of those the waits for DMA */
	volatile uint32_t jobs[SPU_GAME_KINDS + 1];
	volatile uint32_t busy_ticks[SPU_GAME_KINDS + 1];
	volatile uint32_t dma_ticks[SPU_GAME_KINDS + 1];
	uint32_t pad1[8];
} __attribute__((aligned(128))) spu_game_echo;

/* ---------- the structure bsp's blocks (tag data, constant while the bsp is loaded) */

/* the SPU keeps tag data it read while generation is the same */
typedef struct
{
	uint32_t generation;
	/* structure_bsp->nodes: byte rectangles (6 bytes) */
	uint32_t nodes;
	int32_t node_count;
	/* the collision bsp's bsp3d nodes (12 bytes) and planes (16) */
	uint32_t bsp3d_nodes;
	int32_t bsp3d_node_count;
	uint32_t planes;
	int32_t plane_count;
	/* structure_bsp->leaves (16 bytes), surface_references (8), clusters (cluster_size bytes) */
	uint32_t leaves;
	int32_t leaf_count;
	uint32_t surface_references;
	int32_t surface_reference_count;
	uint32_t clusters;
	int32_t cluster_count;
	int32_t cluster_size;
	/* where a cluster has its subclusters' block, and a subcluster (subcluster_size bytes) its surface indices' */
	int32_t cluster_subclusters_offset;
	int32_t subcluster_size;
	int32_t subcluster_surfaces_offset;
	int32_t surface_count;
	float world_bounds[6];
	/* structure_bsp->surfaces: 3 int16 vertex indices (6 bytes) each */
	uint32_t surfaces;
	uint32_t pad[3];
} spu_game_structure;

/* ---------- SHADOW: structure_visibility_build_surfaces for an object's shadow */

#define SPU_GAME_SHADOW_PLANES 6
#define SPU_GAME_SHADOW_CLUSTERS 512

typedef struct
{
	spu_game_structure structure;
	/* render.environment_surface_flags */
	uint32_t environment_flags;
	/* the indices found (int32, maximum_count of them) and the answer (value: their count) */
	uint32_t surfaces;
	uint32_t answer;
	int32_t maximum_count;
	float center[3];
	float radius;
	float bounds[6];
	int32_t plane_count;
	int32_t cluster_count;
	float planes[SPU_GAME_SHADOW_PLANES][4];
	/* cluster_count NONE: the nodes from the root; else these clusters' subclusters */
	int16_t clusters[SPU_GAME_SHADOW_CLUSTERS];
	/* 0: the indices in the walk's order; else ascending, their triangles here (3 int16 each), as
	structure_render_build_dynamic_triangles makes them */
	uint32_t triangles;
	uint32_t pad[3];
} spu_game_shadow;

/* ---------- WEATHER: a window's weather particles: their update, their visibility, their sprites */

#define SPU_GAME_WEATHER_TYPES 8
#define SPU_GAME_WEATHER_GROUPS 8
#define SPU_GAME_WEATHER_BOXES 26
#define SPU_GAME_WEATHER_POLYHEDRA 8
#define SPU_GAME_WEATHER_SEQUENCES 64
#define SPU_GAME_WEATHER_SPRITES 256

/* a bitmap group's sprites, kept by the PPE for the map */
typedef struct
{
	float bounds[4];
	float registration[2];
	/* the sprite's bitmap among the group's (SPU_GAME_WEATHER_GROUPS), NONE: no sprite drawn */
	int32_t slot;
	uint32_t pad;
} spu_game_weather_sprite;

typedef struct
{
	int32_t sequence_count;
	int32_t sprite_total;
	uint32_t pad[2];
	/* the slots' bitmaps' widths (a slot whose vertices the window has none of draws nothing) */
	float widths[SPU_GAME_WEATHER_GROUPS];
	int16_t first_sprite[SPU_GAME_WEATHER_SEQUENCES];
	/* NONE: a sequence with no first bitmap (no sprite of it drawn) */
	int16_t sprite_count[SPU_GAME_WEATHER_SEQUENCES];
	spu_game_weather_sprite sprites[SPU_GAME_WEATHER_SPRITES];
} spu_game_weather_sprites;

typedef struct
{
	/* the type's list in the particles' data array */
	int32_t first_particle_index;
	int32_t particle_count;
	float box_width;
	/* the definition's update */
	float acceleration_bounds[2];
	float turning_rate;
	float change_rate;
	/* its point physics */
	uint32_t physics_flags;
	float mass_over_radius_cubed;
	float water_buoyancy_scale;
	float air_buoyancy_scale;
	float air_friction;
	float water_friction;
	/* its render: fades, sprite mode and direction source, the shader's fade mode, alpha from the fade alone */
	float distance_fade[4];
	float one_over_sprite_width;
	int16_t render_mode;
	int16_t direction_source;
	int16_t fade_mode;
	int16_t fade_alpha;
	/* spu_game_weather_sprites */
	uint32_t sprites;
	uint32_t sprites_size;
	/* each slot's locked vertices (0: none), and its sprites built (answer) */
	uint32_t vertices[SPU_GAME_WEATHER_GROUPS];
	int32_t slot_sprites[SPU_GAME_WEATHER_GROUPS];
	/* the window's visibility: clip planes, boxes, the polyhedra's planes */
	float clip_planes[5][4];
	int32_t box_count;
	int32_t polyhedron_count;
	uint32_t pad0[2];
	float boxes[SPU_GAME_WEATHER_BOXES][4];
	float box_distances[SPU_GAME_WEATHER_BOXES][8];
	uint32_t polyhedron_planes[SPU_GAME_WEATHER_POLYHEDRA];
	int32_t polyhedron_plane_counts[SPU_GAME_WEATHER_POLYHEDRA];
	/* answer: the sprites built and their origins' sum (build_sprite_data's) */
	int32_t sprite_count;
	float centroid[3];
	uint32_t pad1[2];
} spu_game_weather_type;

typedef struct
{
	/* the particles' data array: its datums, their size, how many */
	uint32_t particles;
	int32_t particle_size;
	int32_t particle_maximum;
	int32_t type_count;
	/* the frame: its time, the update's ticks, the random seed, the water flag */
	float dt;
	int32_t update_ticks;
	uint32_t seed;
	int32_t under_water;
	/* the random directions' table (12 bytes each) */
	uint32_t directions;
	int32_t direction_count;
	/* the weather palette entry's wind (scenario_get_current_from_weather_palette): valid, its velocity, the
	definition's variation weight, rate and damping, the variance table (3 x 64 x 12 bytes) and its time */
	int32_t wind_valid;
	float wind_velocity;
	float wind_velocity3d[3];
	float wind_variation_weight;
	float wind_variation_rate;
	float wind_damping;
	float wind_time;
	uint32_t wind_variance;
	/* the point physics' globals */
	float gravity;
	float air_mass;
	float water_mass;
	/* the camera and frustum (projection: projection_matrix[0][0], [2][0], [1][1], [2][1]) */
	float camera_position[3];
	float camera_forward[3];
	/* real_matrix4x3: scale, forward, left, up, position */
	float world_to_view[16];
	float projection[4];
	float projection_world_to_screen_x;
	float viewer_up[3];
	float viewer_forward[3];
	/* build_sprite_globals' big sprites so far (answer: after this job) and the coverage it adds (answer) */
	int32_t big_sprite_count;
	float coverage;
	uint32_t pad[2];
	spu_game_weather_type types[SPU_GAME_WEATHER_TYPES];
} spu_game_weather;

/* ---------- PARTICLES: the effects' particles' rays (point_physics_update's loop after the forces) */

#define SPU_GAME_PARTICLES_MAXIMUM 1024

/* a particle in, its answer out over it */
typedef struct
{
	float position[3];
	float dt;
	float velocity[3];
	float radius;
	/* in: point_physics' collision flags (_collision_test_*); out: its result flags (_point_physics_*) */
	uint32_t flags;
	int32_t leaf_index;
	int16_t cluster_index;
	int16_t material_type;
	float contact_friction;
	float collision_normal[3];
	float elasticity;
} spu_game_particle;

/* a cluster's water (collision_cluster_fog_plane's): the plane less the fog's distance; NONE: no water test */
typedef struct
{
	float plane[4];
	int32_t material_type;
	int32_t valid;
	uint32_t pad[2];
} spu_game_cluster_water;

typedef struct
{
	spu_game_structure structure;
	/* the collision bsp's leaves (8), bsp2d references (8), bsp2d nodes (20), surfaces (12), edges (24), vertices
	(16): EAs and counts */
	uint32_t collision_leaves;
	int32_t collision_leaf_count;
	uint32_t bsp2d_references;
	int32_t bsp2d_reference_count;
	uint32_t bsp2d_nodes;
	int32_t bsp2d_node_count;
	uint32_t surfaces;
	int32_t surface_count;
	uint32_t edges;
	int32_t edge_count;
	uint32_t vertices;
	int32_t vertex_count;
	/* the breakable surfaces' flags (bits), the clusters' water (spu_game_cluster_water each), the collision
	materials' types (int16 each) */
	uint32_t breakable_flags;
	int32_t breakable_count;
	uint32_t cluster_water;
	uint32_t material_types;
	int32_t material_count;
	/* _material_water */
	int32_t water_material_type;
	/* the particles (spu_game_particle each) and the answer */
	uint32_t particles;
	int32_t particle_count;
	uint32_t answer;
	uint32_t pad[3];
} spu_game_particles;

/* ---------- LIGHTS: a window pass's dynamic lights (structure_render_diffuse_light, _specular_light): each light's
structure_visibility_build_surfaces, then structure_render_build_dynamic_triangles' sort and triangles */

#define SPU_GAME_LIGHTS_MAXIMUM 128
#define SPU_GAME_LIGHT_SURFACES 4096
#define SPU_GAME_LIGHT_CLUSTERS 512

/* a light's search, its answer over it */
typedef struct
{
	float center[3];
	float radius;
	float bounds[6];
	/* NONE: the nodes from the root; else these clusters' subclusters (int16 each) */
	int32_t cluster_count;
	uint32_t clusters;
	/* answer: SPU_GAME_DONE with count surfaces at first in the arena, or SPU_GAME_REFUSED */
	int32_t status;
	int32_t count;
	int32_t first;
	uint32_t pad;
} spu_game_light;

typedef struct
{
	spu_game_structure structure;
	/* render.environment_surface_flags */
	uint32_t environment_flags;
	uint32_t lights;
	int32_t light_count;
	/* the arena, arena_count surfaces: their indices ascending (int32 each), their triangles (3 int16 each) */
	uint32_t indices;
	uint32_t triangles;
	int32_t arena_count;
	uint32_t answer;
	uint32_t pad;
} spu_game_lights;

/* ---------- VISIBILITY: structure_visibility_traverse_subclusters for the window's rendered clusters, then the
surfaces it marked, ascending, and their triangles (structure_render_preprocess) */

#define SPU_GAME_VISIBILITY_CLUSTERS 128

typedef struct
{
	int32_t cluster_index;
	/* render_frustum_cube_visible's: the frustum's world bounds, its four side planes */
	float world_bounds[6];
	float planes[4][4];
	uint32_t pad;
} spu_game_visibility_cluster;

typedef struct
{
	spu_game_structure structure;
	/* out: render.environment_surface_flags and _indices, the surfaces' triangles (3 int16 each) */
	uint32_t flags;
	uint32_t indices;
	uint32_t triangles;
	int32_t maximum_count;
	uint32_t answer;
	int32_t cluster_count;
	uint32_t pad[2];
	spu_game_visibility_cluster clusters[SPU_GAME_VISIBILITY_CLUSTERS];
} spu_game_visibility;

/* ---------- SPRITES: render_particles' sprites (build_sprite of render_sprite.c) for its groups the PPE gathered */

#define SPU_GAME_SPRITE_SLOTS SPU_GAME_WEATHER_GROUPS
#define SPU_GAME_SPRITE_U_MIRROR 1u
#define SPU_GAME_SPRITE_V_MIRROR 2u
/* out: the particle passed its size cut (it is rendered) */
#define SPU_GAME_SPRITE_BUILT 0x80000000u

typedef struct
{
	/* world space, the node's matrix applied */
	float position[3];
	float radius;
	float direction[3];
	float rotation;
	float color[4];
	float age;
	float lifespan;
	int16_t sequence_index;
	int16_t sprite_index;
	uint32_t flags;
} spu_game_sprite_particle;

typedef struct
{
	/* the particle definition's: its size cut, scale and fades; its orientation; its shader's fade mode, alpha from
	the fade alone */
	float lod_cutoff;
	float minimum_pixels;
	float one_over_width;
	float fade_in_time;
	float fade_out_time;
	int16_t mode;
	int16_t fade_mode;
	int32_t fade_alpha;
	/* its bitmap group's spu_game_weather_sprites */
	uint32_t sprites;
	uint32_t sprites_size;
	/* each slot's locked vertices (0: none) */
	uint32_t vertices[SPU_GAME_SPRITE_SLOTS];
	int32_t first_particle;
	int32_t particle_count;
	/* answer: each slot's sprites, the order of the slot's first use (NONE: none), the sprites, their origins' sum */
	int32_t slot_sprites[SPU_GAME_SPRITE_SLOTS];
	int32_t slot_order[SPU_GAME_SPRITE_SLOTS];
	int32_t sprite_count;
	float centroid[3];
	uint32_t pad;
} spu_game_sprite_group;

typedef struct
{
	/* the window, as the weather's */
	float world_to_view[16];
	float projection[4];
	float projection_world_to_screen[2];
	/* build_sprite_globals' big sprites so far (answer: after the job), the coverage the job adds (answer) */
	int32_t big_sprite_count;
	float coverage;
	float viewer_up[3];
	float viewer_forward[3];
	uint32_t groups;
	int32_t group_count;
	uint32_t particles;
	int32_t particle_count;
	uint32_t answer;
	uint32_t pad;
} spu_game_sprites;

/* ---------- CONTRAILS: render_contrail's strips (render_contrails.c) for the contrails the PPE gathered */

#define SPU_GAME_CONTRAIL_POINTS 1024
/* spu_game_contrail.flags */
#define SPU_GAME_CONTRAIL_FADES_SLOWLY 1u
#define SPU_GAME_CONTRAIL_FIRST_UNFADED 2u
#define SPU_GAME_CONTRAIL_LAST_UNFADED 4u
#define SPU_GAME_CONTRAIL_TRANSITIONING 2u

typedef struct
{
	float position[3];
	float density;
	float time;
	int32_t state_index;
	/* the point's flags (SPU_GAME_CONTRAIL_TRANSITIONING) */
	uint32_t flags;
	uint32_t pad;
} spu_game_contrail_point;

typedef struct
{
	/* its points (spu_game_contrail_point each), its locked vertices (2 a point) and triangles (2 a segment) */
	uint32_t points;
	int32_t point_count;
	uint32_t vertices;
	uint32_t triangles;
	/* the definition's states (state_size bytes each); in a state, its width, colours and flags at width_offset; the
	flags' bits for the colour and the width scaled by the density */
	uint32_t states;
	int32_t state_count;
	int32_t state_size;
	int32_t width_offset;
	uint32_t color_mask;
	uint32_t width_mask;
	int16_t render_type;
	int16_t fade_mode;
	uint32_t flags;
	/* the attachment's change colour */
	int32_t tinted;
	float tint[3];
	float texture_u;
	float texture_u_step;
	float texture_offset_v;
	float texture_v_far;
	/* answer: SPU_GAME_DONE or SPU_GAME_REFUSED */
	int32_t status;
	uint32_t pad[3];
} spu_game_contrail;

typedef struct
{
	uint32_t contrails;
	int32_t contrail_count;
	/* transition_function_evaluate's table for _transition_function 2 (its size in bytes) */
	uint32_t transition_table;
	int32_t transition_size;
	float camera[3];
	uint32_t answer;
	/* scenario_structure_bsp_generation: the tag data the SPU kept is the map's */
	uint32_t generation;
	uint32_t pad[3];
} spu_game_contrails;

/* the SPU fetches and puts them whole */
typedef char spu_game_layout_check[(sizeof(spu_game_structure) % 16 == 0 && sizeof(spu_game_shadow) % 16 == 0 &&
	sizeof(spu_game_weather_sprites) % 16 == 0 && sizeof(spu_game_weather_type) % 16 == 0 &&
	sizeof(spu_game_weather) % 16 == 0 && sizeof(spu_game_weather) <= 16384 && sizeof(spu_game_particle) == 64 &&
	sizeof(spu_game_cluster_water) == 32 && sizeof(spu_game_particles) % 16 == 0 && sizeof(spu_game_light) == 64 &&
	sizeof(spu_game_lights) % 16 == 0 && sizeof(spu_game_visibility_cluster) == 96 &&
	sizeof(spu_game_visibility) % 16 == 0 && sizeof(spu_game_visibility) <= 16384 &&
	sizeof(spu_game_sprite_particle) == 64 && sizeof(spu_game_sprite_group) == 160 &&
	sizeof(spu_game_sprites) % 16 == 0 && sizeof(spu_game_contrail_point) == 32 && sizeof(spu_game_contrail) == 96 &&
	sizeof(spu_game_contrails) % 16 == 0) ? 1 : -1];

#endif
