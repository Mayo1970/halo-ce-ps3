/* OBJECT_BOUNDS_CACHE.H: the copies of the objects' bounding spheres (object_bounds_cache.c) for collision's object
loops (collisions.c) */

#ifndef OBJECT_BOUNDS_CACHE_H
#define OBJECT_BOUNDS_CACHE_H

enum { OBJECT_BOUNDS_VECTOR_OBJECTS = 64 };

/* a cluster's collideable objects a segment may hit, in the cluster's order; count NONE: more than objects holds, so the
cluster's list is walked again from reference_index */
struct object_bounds_vector_iterator
{
	long objects[OBJECT_BOUNDS_VECTOR_OBJECTS];
	long count;
	long next;
	long reference_index;
	real_point3d const *point;
	real_vector3d const *vector;
};

void object_bounds_cache_update(long object_index, real_point3d const *center, real radius);
void object_bounds_cache_invalidate(void);
/* whether the object's sphere is known to be out of the point's reach (collision's own test on the copy) */
boolean object_bounds_cache_out_of_reach(long object_index, real_point3d const *point, real radius);
/* the cluster's objects but those whose copy the segment misses (fast_vector_intersects_sphere on the copies, four at
once with exact VMX), which fail their own test the same way; NONE at the end */
long object_bounds_cache_first_vector(struct object_bounds_vector_iterator *iterator, short cluster_index,
	real_point3d const *point, real_vector3d const *vector);
long object_bounds_cache_next_vector(struct object_bounds_vector_iterator *iterator);

/* a cluster's collideable objects but those whose copy is out of a sphere query's reach, in the cluster's order; entry
NONE: the list is walked */
struct object_bounds_sphere_iterator
{
	long entry;
	long end;
	long reference_index;
	real_point3d const *point;
	real radius;
};

/* the same objects as object_bounds_cache_out_of_reach passes; the kept block from a cluster's second ask in a
generation (a vehicle's mass points ask in a row); NONE at the end */
long object_bounds_cache_first_sphere(struct object_bounds_sphere_iterator *iterator, short cluster_index,
	real_point3d const *point, real radius);
long object_bounds_cache_next_sphere(struct object_bounds_sphere_iterator *iterator);

#endif
