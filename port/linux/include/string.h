/* STRING.H: host <string.h> plus MSVC-named functions. */

#ifndef __HALO_LINUX_STRING_H
#define __HALO_LINUX_STRING_H

/* the host keeps its own long (the PS3 game's long is a 32-bit macro, halo_ps3_prefix.h) */
#pragma push_macro("long")
#undef long
#include_next <string.h>
#pragma pop_macro("long")

int _stricmp(const char *string1, const char *string2);
int _strnicmp(const char *string1, const char *string2, size_t count);
char *_strdup(const char *string);
char *_strlwr(char *string);
char *_strupr(char *string);

/* (newlib has its own, onto strcasecmp) */
#undef stricmp
#undef strcmpi
#undef strnicmp
#define stricmp _stricmp
#define strcmpi _stricmp
#define _strcmpi _stricmp
#define strnicmp _strnicmp
#define strdup _strdup

#endif
