/* HALO_WINDOWS_PREFIX.H: force-included ahead of each game unit and Xbox-facing platform unit
of the Windows build (never the win32_*.c units); the Microsoft target gives most of MSVC already. */

#ifndef __HALO_WINDOWS_PREFIX_H
#define __HALO_WINDOWS_PREFIX_H

#if !defined(_M_IX86)
#error the Windows port targets 32-bit x86: game data structures assume 32-bit pointers
#endif

#define HALO_WINDOWS 1

/* a pointer that the game's data keeps in 4 bytes (32-bit on the PS3's LP64 build) */
#define HALO_PTR32
/* MSVC's unsigned 64-bit integer; the PS3 prefix makes it a typedef, as its __int64 is one */
#define __uint64 unsigned __int64

/* ---------- XDK architecture selection */

#define _X86_ 1
#define _STDCALL_SUPPORTED 1
#define _USE_MATH_DEFINES

/* ---------- MSVC inline semantics: clang's Microsoft target has COMDAT __inline already; copies
it inlines away come from port/linux/game/msvc_comdat.c and tools/windows_build.py's wrappers */

/* ---------- MSVC intrinsics: clang predeclares the _Interlocked* builtins with prototypes that
conflict with the XDK's; route them to the platform layer (port/linux/src/xbox_kernel.c) */

#define _InterlockedCompareExchange halo_linux_InterlockedCompareExchange
#define _InterlockedDecrement halo_linux_InterlockedDecrement
#define _InterlockedExchange halo_linux_InterlockedExchange
#define _InterlockedExchangeAdd halo_linux_InterlockedExchangeAdd
#define _InterlockedIncrement halo_linux_InterlockedIncrement

/* ---------- structured exception handling: clang does not catch a fault in the __try's own
function, so the block always runs; win32_memory_watch.c reports crashes instead */

#define __try if (1)
#define __except(filter) else if (0)
#define __finally
#define __leave

/* ---------- multiplayer session limits of the native builds */

#include "../../linux/include/halo_port_limits.h"

/* the Xbox Winsock headers' fd_set, for game units and the platform's XNet
alike (the Windows-facing units never see this header) */
#define FD_SETSIZE HALO_PORT_FD_SETSIZE

/* ---------- Xbox functions named like Windows functions */

#include "halo_windows_api_names.h"

/* ---------- Winsock and source fixups shared with the Linux build */

#ifndef HALO_LINUX_PLATFORM_LAYER
/* the game's own strnlen (source/cseries/cseries.c), which the C runtime
also defines (see crt/string.h) */
#define strnlen halo_game_strnlen
#include "../../linux/include/halo_linux_winsock_names.h"
#include "../../linux/include/halo_linux_source_fixups.h"
#endif

#include <stddef.h>

#endif /* __HALO_WINDOWS_PREFIX_H */
