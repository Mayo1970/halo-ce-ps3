/* ZLIB_PREFIXED.H: the port's zlib 1.3.2 (inflate only, for others' data: maps, PNGs, updates), its names prefixed z_ beside
the game's 1.1.3 (source/memory/zlib); include this, not zlib.h. Its other files are zlib.net's release, unchanged. */

#ifndef ZLIB_PREFIXED_H
#define ZLIB_PREFIXED_H

#ifndef Z_PREFIX
#define Z_PREFIX
#endif
#ifdef HALO_PS3
/* the host's long, as the Makefile builds zlib's objects: the game's is a 32-bit macro (halo_ps3_prefix.h) */
#pragma push_macro("long")
#undef long
#endif
#include "zlib.h"
#ifdef HALO_PS3
#pragma pop_macro("long")
#endif

#endif
