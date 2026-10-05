/* FCNTL.H: host <fcntl.h> plus the MSVC text/binary mode flags (no-ops on Linux). */

#ifndef __HALO_LINUX_FCNTL_H
#define __HALO_LINUX_FCNTL_H

/* the host keeps its own long (the PS3 game's long is a 32-bit macro, halo_ps3_prefix.h) */
#pragma push_macro("long")
#undef long
#include_next <fcntl.h>
#pragma pop_macro("long")

#define O_BINARY 0
#define O_TEXT 0
#define _O_BINARY 0
#define _O_TEXT 0
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR O_RDWR
#define _O_CREAT O_CREAT
#define _O_TRUNC O_TRUNC
#define _O_APPEND O_APPEND
#define _O_EXCL O_EXCL

/* open() takes Xbox paths in game code; see stdio.h */
#ifndef HALO_LINUX_PLATFORM_LAYER
int halo_linux_open(const char *path, int flags, ...);
#define open halo_linux_open
#endif

#endif
