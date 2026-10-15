/* Force-included in the Xbox layer's units: it stands in for port/linux/src/platform.h and xgpu.h,
whose guards it sets, so their pthread and OpenGL declarations never load. */

#ifndef __HALO_PS3_PLATFORM_H
#define __HALO_PS3_PLATFORM_H

#define __HALO_LINUX_PLATFORM_H
#define __HALO_LINUX_XGPU_H

/* as platform.h does: the reused units rely on it (_MAX_PATH, malloc) */
#include <stdlib.h>

/* as cseries_windows.h does, so xtl.h also declares the debug keyboard */
#define DEBUG_KEYBOARD
/* newlib's sys/types.h (from the prefix) names its own fd_set; the Xbox layer has Winsock's */
#undef fd_set
/* the game units' Winsock fd_set size (halo_linux_prefix.h), not newlib's 1024: one halo_ws_fd_set layout */
#undef FD_SETSIZE
#define FD_SETSIZE HALO_PORT_FD_SETSIZE
#include "halo_linux_winsock_names.h"
#include <xtl.h>
#include <xbdm.h>
#include <d3d8perf.h>
#define HALO_LINUX_WINSOCK_NAMES_UNDEFINE
#include "halo_linux_winsock_names.h"
#undef HALO_LINUX_WINSOCK_NAMES_UNDEFINE

#include "ps3_host.h"

/* ---------- logging */

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void platform_unimplemented(const char *name);
#define PLATFORM_UNIMPLEMENTED() platform_unimplemented(__func__)

/* ---------- errors */

DWORD platform_set_last_error_from_errno(int error_number);

/* ---------- handles */

enum platform_handle_type
{
	_platform_handle_file = 1,
	_platform_handle_event,
	_platform_handle_mutex,
	_platform_handle_thread,
	_platform_handle_find,
	_platform_handle_signature,
	_platform_handle_other,
};

/* Every HANDLE the layer returns points at one of these. The waitable state is guarded by the
one kernel lock of xbox_kernel.c, not by a lock of its own. */
struct platform_handle
{
	unsigned long signature;
	long type;
	BOOL signaled;
	BOOL manual_reset;
	/* mutexes */
	__uint64 owner;
	long recursion;
	void *data;
	void (*destroy)(struct platform_handle *handle);
};

struct platform_handle *platform_handle_new(long type, void *data,
	void (*destroy)(struct platform_handle *handle));
/* returns NULL (and sets ERROR_INVALID_HANDLE) unless handle has this type */
struct platform_handle *platform_handle_get(HANDLE handle, long type);
void platform_handle_signal(struct platform_handle *handle);

/* ---------- asynchronous procedure calls (ReadFileEx completions) */

typedef void (*platform_apc_routine)(void *context0, void *context1, void *context2);
void platform_queue_apc(platform_apc_routine routine, void *context0, void *context1, void *context2);
long platform_run_apcs(void);

/* ---------- paths */

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);
const char *platform_data_root(void);
const char *platform_save_root(void);

/* ---------- contiguous ("physical") memory: the Xbox window at 0x80000000 (xbox_memory.c) */

#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x08000000UL
#define PLATFORM_ANY_PHYSICAL_ADDRESS 0xffffffffUL

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect);
void platform_contiguous_free(void *address);
BOOL platform_is_contiguous(const void *address);
/* (unsigned long is 32-bit here: the results zero-extend) */
#define PLATFORM_PHYSICAL_TO_VIRTUAL(physical) ((void *)((unsigned long)(physical) | PLATFORM_CONTIGUOUS_BASE))
#define PLATFORM_VIRTUAL_TO_PHYSICAL(address) ((unsigned long)(__uint64)(address) & ~PLATFORM_CONTIGUOUS_BASE)

/* ---------- time */

void platform_unix_time_to_filetime(unsigned long seconds, unsigned long nanoseconds, FILETIME *file_time);
void platform_filetime_to_unix_time(const FILETIME *file_time, unsigned long *seconds, unsigned long *nanoseconds);

/* ---------- textures (xgpu.h): the layout of an Xbox texture in memory */

struct xgpu_texture_description
{
	DWORD format;
	unsigned long width, height, depth, levels;
	BOOL cube_map;
	BOOL linear;
	BOOL compressed;
	unsigned long pitch;
	BOOL hires;
	BOOL hires_coverage;
};

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description);
unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description);
unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level);
unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level);

/* the RSX's copy of a level (d3d8_rsx.c): DXT blocks as they are, other texels as A8R8G8B8 in the
same order, linear rows xgpu_texture_rsx_pitch apart; palette: 256 ARGB values */
unsigned long xgpu_texture_rsx_pitch(const struct xgpu_texture_description *description, unsigned long level);
unsigned long xgpu_texture_rsx_level_bytes(const struct xgpu_texture_description *description, unsigned long level);
void xgpu_texture_rsx_convert_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const DWORD *palette, void *destination);

#endif
