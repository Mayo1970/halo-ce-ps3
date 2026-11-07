/* HALO_PS3_PREFIX.H: force-included first in each PS3 game and Xbox layer unit, from its LP64 copy (`long` spelled
`int`: tools/ps3_lp64_tree.py). The game's 64-bit pointer mode (HALO_64BIT), big-endian, on newlib. */

#ifndef __HALO_PS3_PREFIX_H
#define __HALO_PS3_PREFIX_H

#if !defined(__PPU__) || !defined(__LP64__) || !defined(HALO_64BIT)
#error the PS3 port targets the LP64 PPU code of PSL1GHT, with the 64-bit pointer mode (HALO_64BIT)
#endif

#define HALO_PS3 1
/* the console's capabilities (the game's halo_console_features.h) and the desktop extras left out */
#define HALO_PS3_CONSOLE 1
#define HALO_BIG_ENDIAN 1
#define HALO_CONSOLE 1
/* Custom Edition maps' tag cache: lv2 keeps 0x40000000 for the RSX, so it is above the Xbox window, in its area;
their tags' pointers are moved there as they are swapped (port/ps3/xbox/xbox_tag_swap.c) */
#define HALO_CE_TAG_CACHE_BASE 0x88440000U

/* ---------- the C library, read first: its own __inline helpers, before the MSVC semantics below. wchar_t is
16 bits (-fshort-wchar), wint_t with it; <limits.h> is left out (cseries.h has LONG_MAX as an enumerator) */

#define _WINT_T 1
#define __wint_t_defined 1
typedef unsigned short wint_t;
#include <stdint.h>
#include <stddef.h>
/* the XDK's Winsock declares its own 32-bit u_long */
#define u_long halo_host_u_long
#include <sys/types.h>
#undef u_long
/* newlib's fd_set name; the game has Winsock's (halo_linux_winsock_names.h) */
#undef fd_set
#include <stdarg.h>
#include <float.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
/* the game's own (cseries.h) */
#undef assert
/* C99 float functions that newlib's math.h hides from strict ISO C (-std=gnu89 -D__STRICT_ANSI__) */
float fabsf(float x);
float sqrtf(float x);
float nextafterf(float x, float y);
/* the classification functions as functions: the game declares them itself (hs.c) */
#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef isblank
#undef isascii
#undef toascii
#undef tolower
#undef toupper

/* ---------- pointers inside Xbox data: lv2 puts every user address below 4 GB, and xbox_memory.c maps the Xbox's
window at its own addresses (0x80000000 up), so an Xbox address is the PS3's */

#define XBOX_ADDRESS_SPACE_BASE 0ULL
#include "../../../source/cseries/xbox_address.h"

/* ---------- XDK architecture selection: the declarations' x86 names; _M_IX86 stays undefined (the game's assembly) */

#define _X86_ 1
#define _STDCALL_SUPPORTED 1
#define _INTEGRAL_MAX_BITS 64
#define _WCHAR_T_DEFINED
#define _USE_MATH_DEFINES
#define __export

/* ---------- MSVC keywords that GCC knows only on Windows targets */

#undef __stdcall
#undef __cdecl
#undef __fastcall
#define __stdcall
#define __cdecl
#define _cdecl
#define __fastcall
#define __int64 long long
#define __uint64 uint64_t
#define __int32 int
#define __int16 short
#define __int8 char
#define __w64
#define __unaligned
#define __ptr32
#define __declspec(attribute) __declspec_##attribute
#define __declspec_align(bytes) __attribute__((aligned(bytes)))
#define __declspec_dllexport
#define __declspec_dllimport
#define __declspec_noreturn __attribute__((noreturn))
#define __declspec_noinline __attribute__((noinline))
#define __declspec_naked __attribute__((naked))
#define __declspec_novtable
#define __declspec_selectany __attribute__((weak))
#define __declspec_thread __thread

/* ---------- MSVC inline semantics: gnu89 inline functions, whose external copies halo_msvc_semantics.h makes weak
(COMDAT-like); GCC rejects the `static static` of the desktop's static __inline */

#define __inline __inline__
#define _inline __inline__
#define __forceinline __inline__ __attribute__((always_inline))

#define DECLSPEC_SELECTANY __attribute__((weak))

/* ---------- MSVC intrinsics: the platform layer's */

#define _InterlockedCompareExchange halo_linux_InterlockedCompareExchange
#define _InterlockedDecrement halo_linux_InterlockedDecrement
#define _InterlockedExchange halo_linux_InterlockedExchange
#define _InterlockedExchangeAdd halo_linux_InterlockedExchangeAdd
#define _InterlockedIncrement halo_linux_InterlockedIncrement

/* ---------- structured exception handling: the guarded block always runs */

#define __try if (1)
#define __except(filter) else if (0)
#define __finally
#define __leave

/* ---------- multiplayer session limits */

#include "halo_port_limits.h"

#ifndef HALO_LINUX_PLATFORM_LAYER
#undef FD_SETSIZE
#define FD_SETSIZE HALO_PORT_FD_SETSIZE
#endif

/* ---------- Winsock under private names, and the port's functions the game's sources call */

#ifndef HALO_LINUX_PLATFORM_LAYER
#include "halo_linux_winsock_names.h"
#include "halo_linux_source_fixups.h"
#endif

#endif
