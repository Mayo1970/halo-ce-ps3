/* Force-included in the game's libtiff units (Makefile): the game functions some call without a
prototype, whose implicit int result would cut a 64-bit pointer. As the units' own (int clear). */

#ifndef __HALO_PS3_LIBTIFF_H
#define __HALO_PS3_LIBTIFF_H

void *debug_malloc(unsigned int size, int clear, const char *file, long line);
void *debug_realloc(void *pointer, unsigned int size, const char *file, long line);
void debug_free(void *pointer, const char *file, long line);
void *csmemcpy(void *destination, const void *source, unsigned long size);
void *csmemset(void *buffer, long c, unsigned long size);
char *csstrcpy(char *destination, const char *source);
unsigned long csstrlen(const char *s1);

#endif
