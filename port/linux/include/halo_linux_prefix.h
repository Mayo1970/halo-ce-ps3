/* HALO_LINUX_PREFIX.H: force-included ahead of each unit of the Linux, Android and PS3
builds. It gives the MSVC/XDK environment of the game, so the source stays as MSVC compiles it. */

#ifndef __HALO_LINUX_PREFIX_H
#define __HALO_LINUX_PREFIX_H

#if !defined(__i386__) && !defined(HALO_ARM64_GUEST) && !defined(__wasm32__) && !defined(HALO_PS3)
#error the Linux port targets 32-bit x86: game data structures assume 32-bit pointers
#endif

/* a pointer that the game's data keeps in 4 bytes; ILP32 pointers are 4 bytes
already, and the PS3 prefix sets it for LP64 */
#ifndef HALO_PTR32
#define HALO_PTR32
#endif

/* MSVC's unsigned 64-bit integer; the PS3 prefix makes it a typedef, as its __int64 is one */
#ifndef HALO_PS3
#define __uint64 unsigned __int64
#endif

/* ---------- WebAssembly: no return addresses: Emscripten makes one up from a JavaScript stack trace, which datum lookups
and the HUD's stack checks asked for thousands of times a frame; it would name nothing a log could use, so it is 0 */
#ifdef __wasm32__
#define __builtin_return_address(level) ((void *)0)
#endif

/* ---------- XDK architecture selection (MSVC predefines these) */

#define _X86_ 1
#define _M_IX86 600
#define _STDCALL_SUPPORTED 1
#define _INTEGRAL_MAX_BITS 64
#define _WCHAR_T_DEFINED
#define _USE_MATH_DEFINES
/* the XDK's COM headers decorate methods with __export when _WIN32 is unset */
#define __export

/* ---------- MSVC inline semantics: MSVC gives C __inline functions COMDAT linkage,
which ELF C lacks, so each unit gets a private copy (clang only warns about `static static`) */

#define __inline static __inline__
#define _inline static __inline__
#define __forceinline static __inline__ __attribute__((always_inline))

/* An inline function that also has an ordinary prototype keeps external linkage;
halo_msvc_semantics.h makes each inline name `#pragma weak`, pick-any like a COMDAT. */

/* glibc spells its own extern-inline helpers with __inline; keep it from
emitting them so the redefinition above cannot reach them. */
#define __NO_INLINE__ 1

/* ---------- __declspec(selectany) data (XDK D3DCONST tables) */

#define DECLSPEC_SELECTANY __attribute__((weak))

/* ---------- MSVC intrinsics: clang predeclares the _Interlocked* builtins with
prototypes that conflict with the XDK's; the platform layer implements these names */

#define _InterlockedCompareExchange halo_linux_InterlockedCompareExchange
#define _InterlockedDecrement halo_linux_InterlockedDecrement
#define _InterlockedExchange halo_linux_InterlockedExchange
#define _InterlockedExchangeAdd halo_linux_InterlockedExchangeAdd
#define _InterlockedIncrement halo_linux_InterlockedIncrement

/* ---------- structured exception handling: only main()'s crash handler uses SEH; the
platform layer installs signal handlers, so the guarded block always runs */

#define __try if (1)
#define __except(filter) else if (0)
#define __finally
#define __leave

/* ---------- multiplayer session limits of the native builds */

#include "halo_port_limits.h"

/* the Xbox Winsock headers' fd_set in game units (platform units see glibc's,
which is larger) */
#ifndef HALO_LINUX_PLATFORM_LAYER
#define FD_SETSIZE HALO_PORT_FD_SETSIZE
#endif

/* ---------- Winsock: game code sees the XDK's Winsock under private names
(halo_linux_winsock_names.h); the platform layer includes the XDK headers itself */

#ifndef HALO_LINUX_PLATFORM_LAYER
#include "halo_linux_winsock_names.h"
#include "halo_linux_source_fixups.h"
#endif

/* ---------- MSVC built-in types */

#include <stddef.h>

#endif /* __HALO_LINUX_PREFIX_H */
