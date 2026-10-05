/* HALO_PS3_PREFIX.H: force-included first in each PS3 game unit. It adds the PS3 parts
to halo_linux_prefix.h, which the Linux, Android and PS3 builds share. */

#ifndef __HALO_PS3_PREFIX_H
#define __HALO_PS3_PREFIX_H

#if !defined(__PPU__) || !defined(__LP64__)
#error the PS3 port targets the LP64 PPU code of PSL1GHT
#endif

#define HALO_PS3 1

/* the system headers that game units include directly, parsed with newlib's own 64-bit long;
stddef.h also comes before _WCHAR_T_DEFINED, which makes GCC's stddef.h skip wchar_t */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* The game's long is 32-bit, as MSVC's (GOALS.md, blocker 1). The C runtime wrappers
(port/linux/include, port/ps3/include/limits.h) undefine it while they include newlib. */
#define long int
/* newlib's BSD u_long is 64-bit; the Winsock and libtiff ones are the game's 32-bit u_long */
#define u_long halo_u_long

/* MSVC's 64-bit integers without the keyword long; `unsigned __int64` is spelled __uint64 */
typedef int __int64 __attribute__((mode(DI)));
typedef unsigned int __uint64 __attribute__((mode(DI)));

/* a pointer that the game's data keeps in 4 bytes: lv2 puts every user
address below 4 GB, and loads zero-extend it (tested on ppu-gcc 7.2) */
#define HALO_PTR32 __attribute__((mode(SI)))

/* ---------- MSVC keywords that GCC knows only on Windows targets */

#define __stdcall
#define __cdecl
#define _cdecl
#define __fastcall
#define __declspec(attribute) __declspec_##attribute
#define __declspec_align(bytes) __attribute__((aligned(bytes)))
#define __declspec_dllexport
#define __declspec_dllimport
#define __declspec_noreturn __attribute__((noreturn))
#define __declspec_selectany __attribute__((weak))

#include "halo_linux_prefix.h"

/* GCC rejects the `static static` that halo_linux_prefix.h makes of `static __inline`: gnu89
inline functions instead, whose external copies halo_msvc_semantics.h makes weak (COMDAT-like) */
#undef __inline
#undef _inline
#undef __forceinline
#define __inline __inline__
#define _inline __inline__
#define __forceinline __inline__ __attribute__((always_inline))

#endif
