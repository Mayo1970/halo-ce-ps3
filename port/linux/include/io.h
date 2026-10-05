/* IO.H: MSVC low-level I/O for game code, mapped onto POSIX. */

#ifndef __HALO_LINUX_IO_H
#define __HALO_LINUX_IO_H

#include <stddef.h>
/* the host keeps its own long (the PS3 game's long is a 32-bit macro, halo_ps3_prefix.h) */
#pragma push_macro("long")
#undef long
#include <unistd.h>
#pragma pop_macro("long")
#include <fcntl.h>

#define _open open
#define _close close
#define _read read
#define _write write
#define _lseek lseek
#define _unlink unlink
#define _access access

long _filelength(int handle);
int _chsize(int handle, long size);

#endif
