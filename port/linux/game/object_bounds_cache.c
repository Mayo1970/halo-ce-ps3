/* OBJECT_BOUNDS_CACHE.C: the objects' bounding spheres by absolute index from their only writer (node matrices), out of
the game state; collision's object loops (collisions.c) test these copies first: the same tests on the same values */

#ifdef HALO_PS3
#include "ps3_timers.h"
#include "ps3_vmx.h"
#endif
#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#ifdef HALO_PS3
#include "structures/cluster_partitions.h"
#include "structures/structures.h"
#endif
#include "object_bounds_cache.h"

/* the sphere first, in 32 bytes: one aligned quadword for VMX (ps3_vmx.c). Valid for its datum index until the game
state is next replaced (game_state.c) */
struct object_bounds
{
	real_point3d center;
	real radius;
	long object_index;
	long epoch;
	long pad[2];
};

#ifdef HALO_PS3
static struct object_bounds object_bounds[MAXIMUM_OBJECTS_PER_MAP] __attribute__((aligned(16)));
#else
static struct object_bounds object_bounds[MAXIMUM_OBJECTS_PER_MAP];
#endif
static long object_bounds_epoch = 1;

#ifdef HALO_PS3
/* each cluster's collideable objects in its list's order with their copies' spheres, kept while the lists (their
port_modification_count) and the copies (object_bounds_writes) don't change: a ray reads one block, not the list */
enum { OBJECT_BOUNDS_KEPT_ENTRIES = 4096 };
static struct
{
	float spheres[OBJECT_BOUNDS_KEPT_ENTRIES][4] __attribute__((aligned(16)));
	long objects[OBJECT_BOUNDS_KEPT_ENTRIES];
	boolean known[OBJECT_BOUNDS_KEPT_ENTRIES];
	unsigned long generation;
	long entry_count;
	unsigned long cluster_generations[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	long cluster_first_entries[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	long cluster_entry_counts[MAXIMUM_CLUSTERS_PER_STRUCTURE];
} object_bounds_kept __attribute__((aligned(16)));
static unsigned long object_bounds_writes;
#endif

void object_bounds_cache_update(
	long object_index,
	real_point3d const *center,
	real radius)
{
	struct object_bounds *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	bounds->object_index = object_index;
	bounds->epoch = object_bounds_epoch;
	bounds->center = *center;
	bounds->radius = radius;
#ifdef HALO_PS3
	object_bounds_writes++;
#endif
}

void object_bounds_cache_invalidate(
	void)
{
	object_bounds_epoch++;
#ifdef HALO_PS3
	object_bounds_writes++;
#endif
}

/* whether the object's bounding sphere is known to be out of the reach of
the point (collision's own test on the copy); FALSE when it is not known */
boolean object_bounds_cache_out_of_reach(
	long object_index,
	real_point3d const *point,
	real radius)
{
	struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	return bounds->object_index == object_index && bounds->epoch == object_bounds_epoch &&
		!point_in_sphere(point, &bounds->center, bounds->radius + radius);
}

static boolean object_bounds_known(
	struct object_bounds const *bounds,
	long object_index)
{
	return bounds->object_index == object_index && bounds->epoch == object_bounds_epoch;
}

/* whether the segment misses the object's copy; FALSE when it is not known */
static boolean vector_missed(
	long object_index,
	real_point3d const *point,
	real_vector3d const *vector)
{
	struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];

	return object_bounds_known(bounds, object_index) &&
		!fast_vector_intersects_sphere(point, vector, &bounds->center, bounds->radius);
}

/* FALSE once the iterator is full: its objects are then the cluster's list again (count NONE) */
static boolean vector_candidate_add(
	struct object_bounds_vector_iterator *iterator,
	long object_index)
{
	if (iterator->count == OBJECT_BOUNDS_VECTOR_OBJECTS)
	{
		iterator->count = NONE;
		return FALSE;
	}
	iterator->objects[iterator->count++] = object_index;
	return TRUE;
}

/* the cluster's list from object_index on, past the objects the segment misses */
static long vector_list_next(
	struct object_bounds_vector_iterator *iterator,
	long object_index)
{
	while (object_index != NONE && vector_missed(object_index, iterator->point, iterator->vector))
		object_index = cluster_get_next_collideable_object(&iterator->reference_index);
	return object_index;
}

#ifdef HALO_PS3
/* the list's objects in order, the known copies' spheres among them for VMX four at a time */
struct vector_batch
{
	long objects[32];
	boolean known[32];
	float const *spheres[32];
	long count, known_count;
};

static boolean vector_batch_flush(
	struct vector_batch *batch,
	struct object_bounds_vector_iterator *iterator)
{
	uint32_t misses = batch->known_count ?
		ps3_vmx_segment_misses_spheres(batch->spheres, (uint32_t)batch->known_count, &iterator->point->x,
			&iterator->vector->i) : 0;
	long index, known = 0;
	boolean room = TRUE;

	for (index = 0; index < batch->count && room; index++)
	{
		if (!batch->known[index] || !((misses >> known++) & 1))
			room = vector_candidate_add(iterator, batch->objects[index]);
	}
	batch->count = batch->known_count = 0;
	return room;
}

/* the cluster's block (its first entry and count), made from its list when this generation has none; NONE: not kept
(an index out of the clusters, or more objects than fit), its list is walked */
static long object_bounds_kept_cluster(
	short cluster_index,
	long *entry_count)
{
	unsigned long generation = 1 + collideable_object_cluster_partition.port_modification_count + object_bounds_writes;
	long reference_index;
	long object_index;
	long first;

	if (cluster_index < 0 || cluster_index >= MAXIMUM_CLUSTERS_PER_STRUCTURE)
		return NONE;
	if (object_bounds_kept.generation != generation)
	{
		object_bounds_kept.generation = generation;
		object_bounds_kept.entry_count = 0;
	}
	if (object_bounds_kept.cluster_generations[cluster_index] != generation)
	{
		first = object_bounds_kept.entry_count;
		for (object_index = cluster_get_first_collideable_object(&reference_index, cluster_index);
			object_index != NONE;
			object_index = cluster_get_next_collideable_object(&reference_index))
		{
			struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];
			long entry = object_bounds_kept.entry_count;

			ps3_bounds_walked++; /* the timer build's (ps3_timers.h) */
			if (entry >= OBJECT_BOUNDS_KEPT_ENTRIES)
			{
				object_bounds_kept.entry_count = first;
				first = NONE;
				break;
			}
			object_bounds_kept.entry_count++;
			object_bounds_kept.objects[entry] = object_index;
			object_bounds_kept.known[entry] = object_bounds_known(bounds, object_index);
			object_bounds_kept.spheres[entry][0] = bounds->center.x;
			object_bounds_kept.spheres[entry][1] = bounds->center.y;
			object_bounds_kept.spheres[entry][2] = bounds->center.z;
			object_bounds_kept.spheres[entry][3] = bounds->radius;
		}
		object_bounds_kept.cluster_generations[cluster_index] = generation;
		object_bounds_kept.cluster_first_entries[cluster_index] = first;
		object_bounds_kept.cluster_entry_counts[cluster_index] = first == NONE ? 0 : object_bounds_kept.entry_count - first;
	}
	*entry_count = object_bounds_kept.cluster_entry_counts[cluster_index];
	return object_bounds_kept.cluster_first_entries[cluster_index];
}
#endif

/* the copies' test over the whole list first: the objects it does not rule out, in order, while the iterator holds them */
long object_bounds_cache_first_vector(
	struct object_bounds_vector_iterator *iterator,
	short cluster_index,
	real_point3d const *point,
	real_vector3d const *vector)
{
	long object_index;
	boolean room = TRUE;
#ifdef HALO_PS3
	boolean vmx = ps3_vmx_exact();
	struct vector_batch batch;

	batch.count = batch.known_count = 0;
#endif

	iterator->count = 0;
	iterator->next = 0;
	iterator->point = point;
	iterator->vector = vector;
#ifdef HALO_PS3
	/* the cluster's kept block, in the list's order: the same objects, copies and batches as the list's walk below */
	if (vmx)
	{
		long count;
		long entry = object_bounds_kept_cluster(cluster_index, &count);

		if (entry != NONE)
		{
			long end = entry + count;

			ps3_bounds_read += (uint32_t)count; /* the timer build's (ps3_timers.h) */
			for (; entry < end && room; entry++)
			{
				batch.objects[batch.count] = object_bounds_kept.objects[entry];
				batch.known[batch.count++] = object_bounds_kept.known[entry];
				if (object_bounds_kept.known[entry])
					batch.spheres[batch.known_count++] = object_bounds_kept.spheres[entry];
				if (batch.count == NUMBEROF(batch.objects))
					room = vector_batch_flush(&batch, iterator);
			}
			if (batch.count && room)
				room = vector_batch_flush(&batch, iterator);
			if (!room)
				return vector_list_next(iterator, cluster_get_first_collideable_object(&iterator->reference_index, cluster_index));
			return object_bounds_cache_next_vector(iterator);
		}
	}
#endif
	for (object_index = cluster_get_first_collideable_object(&iterator->reference_index, cluster_index);
		object_index != NONE && room;
		object_index = cluster_get_next_collideable_object(&iterator->reference_index))
	{
#ifdef HALO_PS3
		ps3_bounds_walked++; /* the timer build's (ps3_timers.h) */
		if (vmx)
		{
			struct object_bounds const *bounds = &object_bounds[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];
			boolean known = object_bounds_known(bounds, object_index);

			batch.objects[batch.count] = object_index;
			batch.known[batch.count++] = known;
			if (known)
				batch.spheres[batch.known_count++] = &bounds->center.x;
			if (batch.count == NUMBEROF(batch.objects))
				room = vector_batch_flush(&batch, iterator);
			continue;
		}
#endif
		if (!vector_missed(object_index, point, vector))
			room = vector_candidate_add(iterator, object_index);
	}
#ifdef HALO_PS3
	if (batch.count && room)
		room = vector_batch_flush(&batch, iterator);
#endif
	if (!room)
		return vector_list_next(iterator, cluster_get_first_collideable_object(&iterator->reference_index, cluster_index));
	return object_bounds_cache_next_vector(iterator);
}

long object_bounds_cache_next_vector(
	struct object_bounds_vector_iterator *iterator)
{
	if (iterator->count == NONE)
		return vector_list_next(iterator, cluster_get_next_collideable_object(&iterator->reference_index));
	return iterator->next < iterator->count ? iterator->objects[iterator->next++] : NONE;
}
