/* The shader sweep's structures.c (in its object's place), with the release builds' assertions: c40's first frames
give an object's shadow a radius below 0 (structure_clusters_in_sphere), which the checked build stops at. */

#ifndef HALO_RELEASE
#define HALO_RELEASE
#define SWEEP_RELEASE_ASSERTIONS
#endif
#include "structures/structures.c"

#ifdef SWEEP_RELEASE_ASSERTIONS
/* (cseries.c has it in release builds only) noted in debug.txt, as a warning the game goes on after */
void release_assert_failed(char const *information, char const *file, long line, boolean fatal)
{
	(void)fatal;
	display_assert((char *)information, (char *)file, line, FALSE);
}
#endif
