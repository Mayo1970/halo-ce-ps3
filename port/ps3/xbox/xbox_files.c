/* The Win32 file API on lv2, after xbox_files.c: d:\ is the data root (maps/), the other drives are
folders of the save root on the HDD (z:\ the map cache); names match in any case, as on FATX. */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps3_timers.h"

#define SAVE_ROOT "/dev_hdd0/data/HaloCE"
#define HOST_PATH_SIZE 1024
/* transfers this large are logged with their time: the saves (16 MB a checkpoint) and the map copies */
#define TRANSFER_LOG_BYTES 0x100000UL
/* a read's lv2 calls at most this large: one 5 MB call read a structure BSP at 9.5 MB/s, the 128 KB calls of a
save game 48 MB/s */
#define READ_PIECE_BYTES 0x20000UL

/* ---------- paths */

static BOOL path_exists(const char *path)
{
	struct ps3_host_file_information information;

	return ps3_host_path_information(path, &information) == 0;
}

/* copies the on-disk spelling of name in directory, matched in any case; FALSE if none */
static BOOL find_entry_any_case(const char *directory, const char *name, char *result, unsigned long size)
{
	int32_t descriptor;
	char entry[256];
	BOOL found = FALSE;

	if (ps3_host_directory_open(directory, &descriptor) != 0)
		return FALSE;
	while (!found && ps3_host_directory_next(descriptor, entry, sizeof(entry)))
	{
		if (!_stricmp(entry, name))
		{
			snprintf(result, size, "%s", entry);
			found = TRUE;
		}
	}
	ps3_host_directory_close(descriptor);
	return found;
}

const char *platform_data_root(void)
{
	return ps3_host_data_root();
}

const char *platform_save_root(void)
{
	static BOOL made = FALSE;

	if (!made)
	{
		ps3_host_path_make_directory("/dev_hdd0/data");
		ps3_host_path_make_directory(SAVE_ROOT);
		made = TRUE;
	}
	return SAVE_ROOT;
}

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	char resolved[HOST_PATH_SIZE];
	const char *cursor = xbox_path;
	unsigned long length;

	snprintf(resolved, sizeof(resolved), "%s", platform_data_root());
	if (((cursor[0] >= 'a' && cursor[0] <= 'z') || (cursor[0] >= 'A' && cursor[0] <= 'Z')) && cursor[1] == ':')
	{
		char drive = (char)(cursor[0] | 0x20);

		if (drive != 'd')
		{
			snprintf(resolved, sizeof(resolved), "%s/%c", platform_save_root(), drive);
			/* every Xbox drive always exists */
			if (!path_exists(resolved))
				ps3_host_path_make_directory(resolved);
		}
		cursor += 2;
	}

	while (*cursor)
	{
		char component[256];
		char on_disk[256];
		unsigned long component_length = 0;

		while (*cursor == '\\' || *cursor == '/')
			cursor++;
		while (*cursor && *cursor != '\\' && *cursor != '/' && component_length + 1 < sizeof(component))
			component[component_length++] = *cursor++;
		component[component_length] = '\0';
		if (!component_length || !strcmp(component, "."))
			continue;

		length = strlen(resolved);
		snprintf(resolved + length, sizeof(resolved) - length, "/%s", component);
		/* the exact spelling first: one call, where a case search reads the folder */
		if (!path_exists(resolved))
		{
			resolved[length] = '\0';
			if (find_entry_any_case(resolved, component, on_disk, sizeof(on_disk)))
				snprintf(resolved + length, sizeof(resolved) - length, "/%s", on_disk);
			else
				snprintf(resolved + length, sizeof(resolved) - length, "/%s", component);
		}
	}
	snprintf(host_path, host_path_size, "%s", resolved);
}

/* ---------- file handles */

struct platform_file
{
	int32_t descriptor;
	/* positioned reads move the shared file position */
	struct ps3_host_lock *lock;
	char path[HOST_PATH_SIZE];
	/* since the open, reads [0] and writes [1], for the line at the close */
	__uint64 opened;
	__uint64 bytes[2];
	__uint64 microseconds[2];
	unsigned long calls[2];
	/* written or resized since the open: synced at the close */
	BOOL written;
};

static void file_destroy(struct platform_handle *handle)
{
	struct platform_file *file = handle->data;
	__uint64 start = ps3_host_microseconds(), synced;
	/* a save must be on the disk when the close returns: a power cut can follow */
	int result = file->written ? ps3_host_file_sync(file->descriptor) : 0;

	synced = ps3_host_microseconds();
	if (result)
		platform_log("%s: fsync failed (%d)", file->path, result);
	if (file->bytes[0] + file->bytes[1] >= TRANSFER_LOG_BYTES)
		platform_log("%s: closed after %llu ms: read %llu KB in %lu calls (%llu ms), wrote %llu KB in %lu calls "
			"(%llu ms), fsync %llu ms", file->path, (start - file->opened) / 1000, file->bytes[0] >> 10,
			file->calls[0], file->microseconds[0] / 1000, file->bytes[1] >> 10, file->calls[1],
			file->microseconds[1] / 1000, (synced - start) / 1000);
	ps3_host_file_close(file->descriptor);
	ps3_host_lock_destroy(file->lock);
	free(file);
}

static struct platform_file *file_from_handle(HANDLE handle)
{
	struct platform_handle *record = platform_handle_get(handle, _platform_handle_file);

	return record ? record->data : NULL;
}

HANDLE WINAPI CreateFileA(LPCSTR file_name, DWORD desired_access, DWORD share_mode,
	LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition,
	DWORD flags_and_attributes, HANDLE template_file)
{
	struct platform_file *file;
	struct platform_handle *handle;
	struct ps3_host_file_information information;
	char path[HOST_PATH_SIZE];
	int flags;
	int existed;
	int32_t descriptor;
	int result;

	(void)share_mode;
	(void)security_attributes;
	(void)flags_and_attributes;
	(void)template_file;

	platform_translate_path(file_name, path, sizeof(path));
	existed = ps3_host_path_information(path, &information) == 0;

	if ((desired_access & GENERIC_READ) && (desired_access & GENERIC_WRITE))
		flags = PS3_HOST_O_RDWR;
	else if (desired_access & GENERIC_WRITE)
		flags = PS3_HOST_O_WRONLY;
	else
		flags = PS3_HOST_O_RDONLY;

	switch (creation_disposition)
	{
	case CREATE_NEW:
		flags |= PS3_HOST_O_CREAT | PS3_HOST_O_EXCL;
		break;
	case CREATE_ALWAYS:
		flags |= PS3_HOST_O_CREAT | PS3_HOST_O_TRUNC;
		break;
	case OPEN_EXISTING:
		break;
	case OPEN_ALWAYS:
		flags |= PS3_HOST_O_CREAT;
		break;
	case TRUNCATE_EXISTING:
		flags |= PS3_HOST_O_TRUNC;
		break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}
	if ((flags & PS3_HOST_O_TRUNC) && (flags & 3) == PS3_HOST_O_RDONLY)
		flags = (flags & ~3) | PS3_HOST_O_RDWR;

	if (existed && information.directory)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}
	if (!existed && creation_disposition != OPEN_EXISTING && creation_disposition != TRUNCATE_EXISTING)
	{
		/* lv2 reports a missing folder as a missing file */
		char folder[HOST_PATH_SIZE];
		char *slash;

		snprintf(folder, sizeof(folder), "%s", path);
		slash = strrchr(folder, '/');
		if (slash && slash != folder)
		{
			*slash = '\0';
			if (!path_exists(folder))
			{
				SetLastError(ERROR_PATH_NOT_FOUND);
				return INVALID_HANDLE_VALUE;
			}
		}
	}

	result = ps3_host_file_open(path, flags, &descriptor);
	/* (G2: where a headless start-up has got to) */
	platform_log("open %s: %s", file_name, result ? "failed" : "ok");
	if (result != 0)
	{
		platform_set_last_error_from_errno(result);
		return INVALID_HANDLE_VALUE;
	}

	file = calloc(1, sizeof(*file));
	handle = file ? platform_handle_new(_platform_handle_file, file, file_destroy) : NULL;
	if (file)
		file->lock = ps3_host_lock_create(FALSE);
	if (!handle || !file->lock)
	{
		ps3_host_file_close(descriptor);
		if (file)
			ps3_host_lock_destroy(file->lock);
		free(file);
		free(handle);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	file->descriptor = descriptor;
	snprintf(file->path, sizeof(file->path), "%s", path);
	file->opened = ps3_host_microseconds();

	/* Win32 reports whether an OPEN_ALWAYS/CREATE_ALWAYS target existed */
	SetLastError((existed && (creation_disposition == OPEN_ALWAYS || creation_disposition == CREATE_ALWAYS)) ?
		ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return handle;
}

/* every transfer's bytes so far, for a large one's line: what the others moved while it ran */
static volatile __uint64 transfers_bytes;
/* the reads running now: the checkpoint's writer waits for them (game_state_xbox.c), a bsp's read has the disk alone */
static volatile int reads_running;

int xbox_files_reads_running(void)
{
	return reads_running;
}

/* (bsp_keep.c's reads wait while the game's move bytes) */
__uint64 xbox_files_transfer_bytes(void)
{
	return transfers_bytes;
}

static BOOL transfer(struct platform_file *file, void *buffer, DWORD count, LPDWORD done_count, BOOL positioned,
	__uint64 offset, BOOL write)
{
	__uint64 saved = 0, done = 0, start, elapsed, locked = ps3_host_microseconds(), seeked, at = offset;
	__uint64 others = transfers_bytes, slowest = 0;
	unsigned long pieces = 0, slowest_piece = 0;
	DWORD total = 0;
	int result = 0;
	int direction = write ? 1 : 0;

	if (!write)
		__sync_fetch_and_add(&reads_running, 1);
	ps3_host_lock_enter(file->lock);
	start = ps3_host_microseconds();
	if (positioned)
	{
		ps3_host_file_seek(file->descriptor, 0, 1, &saved);
		result = ps3_host_file_seek(file->descriptor, (__int64)offset, 0, NULL);
	}
	else if (count >= TRANSFER_LOG_BYTES)
		ps3_host_file_seek(file->descriptor, 0, 1, &at);
	seeked = ps3_host_microseconds();
	while (!result && total < count)
	{
		DWORD piece = count - total;
		__uint64 piece_start = ps3_host_microseconds(), piece_time;

		if (!write && piece > READ_PIECE_BYTES)
			piece = READ_PIECE_BYTES;
		result = write ?
			ps3_host_file_write(file->descriptor, (const char *)buffer + total, piece, &done) :
			ps3_host_file_read(file->descriptor, (char *)buffer + total, piece, &done);
		piece_time = ps3_host_microseconds() - piece_start;
		if (piece_time > slowest)
		{
			slowest = piece_time;
			slowest_piece = pieces;
		}
		pieces++;
		if (result || !done)
			break;
		__sync_fetch_and_add(&transfers_bytes, done);
		__sync_fetch_and_add(&ps3_spike_counts[_ps3_spike_disk_bytes], (uint32_t)done);
		total += (DWORD)done;
	}
	if (positioned)
		ps3_host_file_seek(file->descriptor, (__int64)saved, 0, NULL);
	elapsed = ps3_host_microseconds() - start;
	file->bytes[direction] += total;
	file->microseconds[direction] += elapsed;
	file->calls[direction]++;
	if (write && total)
		file->written = TRUE;
	ps3_host_lock_leave(file->lock);
	if (!write)
		__sync_fetch_and_sub(&reads_running, 1);
	/* (the pieces: where the time went; the others: another thread's reads moving the disk's head meanwhile) */
	if (total >= TRANSFER_LOG_BYTES)
		platform_log("%s: %s %lu KB in %llu ms (at %llu KB; the file's lock %llu ms, the seek %llu ms; %lu pieces, the "
			"slowest %llu ms (piece %lu); others' transfers meanwhile %llu KB)", file->path, write ? "wrote" : "read",
			(unsigned long)(total >> 10), elapsed / 1000, at >> 10, (start - locked) / 1000, (seeked - start) / 1000,
			pieces, slowest / 1000, slowest_piece, (transfers_bytes - others - total) >> 10);
	if (done_count)
		*done_count = total;
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

static __uint64 overlapped_offset(LPOVERLAPPED overlapped)
{
	return ((__uint64)overlapped->OffsetHigh << 32) | overlapped->Offset;
}

BOOL WINAPI ReadFile(HANDLE handle, LPVOID buffer, DWORD count, LPDWORD bytes_read, LPOVERLAPPED overlapped)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result;
	DWORD done = 0;

	if (!file)
		return FALSE;
	result = transfer(file, buffer, count, &done, overlapped != NULL, overlapped ? overlapped_offset(overlapped) : 0, FALSE);
	if (bytes_read)
		*bytes_read = done;
	if (overlapped)
	{
		overlapped->Internal = result ? 0 : GetLastError();
		overlapped->InternalHigh = done;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}
	return result;
}

BOOL WINAPI WriteFile(HANDLE handle, LPCVOID buffer, DWORD count, LPDWORD bytes_written, LPOVERLAPPED overlapped)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result;
	DWORD done = 0;

	if (!file)
		return FALSE;
	result = transfer(file, (void *)buffer, count, &done, overlapped != NULL, overlapped ? overlapped_offset(overlapped) : 0,
		TRUE);
	if (bytes_written)
		*bytes_written = done;
	if (overlapped)
	{
		overlapped->Internal = result ? 0 : GetLastError();
		overlapped->InternalHigh = done;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}
	return result;
}

/* ReadFileEx/WriteFileEx complete at once; as on Win32, the completion routine runs in the
issuing thread's next alertable wait */

static void file_completion_apc(void *routine, void *overlapped, void *unused)
{
	LPOVERLAPPED request = overlapped;

	(void)unused;
	((LPOVERLAPPED_COMPLETION_ROUTINE)routine)((DWORD)request->Internal, (DWORD)request->InternalHigh, request);
}

BOOL WINAPI ReadFileEx(HANDLE handle, LPVOID buffer, DWORD count, LPOVERLAPPED overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	struct platform_file *file = file_from_handle(handle);
	DWORD done = 0;
	BOOL result;

	if (!file || !overlapped)
	{
		if (file)
			SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	result = transfer(file, buffer, count, &done, TRUE, overlapped_offset(overlapped), FALSE);
	overlapped->Internal = result ? ERROR_SUCCESS : GetLastError();
	if (result && done == 0 && count > 0)
		overlapped->Internal = ERROR_HANDLE_EOF;
	overlapped->InternalHigh = done;
	platform_queue_apc(file_completion_apc, (void *)completion_routine, overlapped, NULL);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

BOOL WINAPI WriteFileEx(HANDLE handle, LPCVOID buffer, DWORD count, LPOVERLAPPED overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	struct platform_file *file = file_from_handle(handle);
	DWORD done = 0;
	BOOL result;

	if (!file || !overlapped)
	{
		if (file)
			SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	result = transfer(file, (void *)buffer, count, &done, TRUE, overlapped_offset(overlapped), TRUE);
	overlapped->Internal = result ? ERROR_SUCCESS : GetLastError();
	overlapped->InternalHigh = done;
	platform_queue_apc(file_completion_apc, (void *)completion_routine, overlapped, NULL);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE handle, LONG distance, PLONG distance_high, DWORD method)
{
	struct platform_file *file = file_from_handle(handle);
	__int64 offset;
	__uint64 position = 0;
	int whence, result;

	if (!file)
		return INVALID_SET_FILE_POINTER;
	switch (method)
	{
	case FILE_BEGIN: whence = 0; break;
	case FILE_CURRENT: whence = 1; break;
	case FILE_END: whence = 2; break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_SET_FILE_POINTER;
	}
	/* without a high part, distance is a signed 32-bit value */
	offset = distance_high ? (__int64)(((__uint64)(DWORD)*distance_high << 32) | (DWORD)distance) : (__int64)distance;
	ps3_host_lock_enter(file->lock);
	result = ps3_host_file_seek(file->descriptor, offset, whence, &position);
	ps3_host_lock_leave(file->lock);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return INVALID_SET_FILE_POINTER;
	}
	if (distance_high)
		*distance_high = (LONG)(position >> 32);
	SetLastError(ERROR_SUCCESS);
	return (DWORD)position;
}

DWORD WINAPI GetFileSize(HANDLE handle, LPDWORD size_high)
{
	struct platform_file *file = file_from_handle(handle);
	struct ps3_host_file_information information;
	int result;

	if (!file)
		return INVALID_FILE_SIZE;
	result = ps3_host_file_information(file->descriptor, &information);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return INVALID_FILE_SIZE;
	}
	if (size_high)
		*size_high = (DWORD)(information.size >> 32);
	SetLastError(ERROR_SUCCESS);
	return (DWORD)information.size;
}

BOOL WINAPI SetEndOfFile(HANDLE handle)
{
	struct platform_file *file = file_from_handle(handle);
	__uint64 position = 0;
	int result;

	if (!file)
		return FALSE;
	ps3_host_lock_enter(file->lock);
	result = ps3_host_file_seek(file->descriptor, 0, 1, &position);
	if (!result)
	{
		__uint64 start = ps3_host_microseconds();

		result = ps3_host_file_truncate(file->descriptor, position);
		if (!result)
			file->written = TRUE;
		/* the map cache's files (z:\cacheNNN.map) are made this way, hundreds of MB */
		if (position >= 0x100000)
			platform_log("%s: %llu MB in %llu ms (%d)", file->path, (__uint64)(position >> 20),
				(__uint64)((ps3_host_microseconds() - start) / 1000), result);
	}
	ps3_host_lock_leave(file->lock);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE handle, LPFILETIME creation_time, LPFILETIME last_access_time, LPFILETIME last_write_time)
{
	struct platform_file *file = file_from_handle(handle);
	struct ps3_host_file_information information;
	int result;

	if (!file)
		return FALSE;
	result = ps3_host_file_information(file->descriptor, &information);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	if (creation_time)
		platform_unix_time_to_filetime((unsigned long)information.creation_time, 0, creation_time);
	if (last_access_time)
		platform_unix_time_to_filetime((unsigned long)information.access_time, 0, last_access_time);
	if (last_write_time)
		platform_unix_time_to_filetime((unsigned long)information.modification_time, 0, last_write_time);
	return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE handle, CONST FILETIME *creation_time, CONST FILETIME *last_access_time,
	CONST FILETIME *last_write_time)
{
	struct platform_file *file = file_from_handle(handle);
	struct ps3_host_file_information information;
	unsigned long access_seconds, write_seconds, nanoseconds;
	int result;

	(void)creation_time;
	if (!file)
		return FALSE;
	result = ps3_host_file_information(file->descriptor, &information);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	access_seconds = (unsigned long)information.access_time;
	write_seconds = (unsigned long)information.modification_time;
	if (last_access_time)
		platform_filetime_to_unix_time(last_access_time, &access_seconds, &nanoseconds);
	if (last_write_time)
		platform_filetime_to_unix_time(last_write_time, &write_seconds, &nanoseconds);
	if (!last_access_time && !last_write_time)
		return TRUE;
	result = ps3_host_path_set_times(file->path, access_seconds, write_seconds);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

/* ---------- operations on paths */

static void fill_attribute_data(const struct ps3_host_file_information *information, WIN32_FILE_ATTRIBUTE_DATA *data)
{
	data->dwFileAttributes = 0;
	if (information->directory)
		data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
	if (information->read_only)
		data->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
	if (!data->dwFileAttributes)
		data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	platform_unix_time_to_filetime((unsigned long)information->creation_time, 0, &data->ftCreationTime);
	platform_unix_time_to_filetime((unsigned long)information->access_time, 0, &data->ftLastAccessTime);
	platform_unix_time_to_filetime((unsigned long)information->modification_time, 0, &data->ftLastWriteTime);
	data->nFileSizeHigh = (DWORD)(information->size >> 32);
	data->nFileSizeLow = (DWORD)information->size;
}

DWORD WINAPI GetFileAttributesA(LPCSTR file_name)
{
	struct ps3_host_file_information information;
	WIN32_FILE_ATTRIBUTE_DATA data;
	char path[HOST_PATH_SIZE];
	int result;

	platform_translate_path(file_name, path, sizeof(path));
	result = ps3_host_path_information(path, &information);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return (DWORD)-1;
	}
	fill_attribute_data(&information, &data);
	return data.dwFileAttributes;
}

BOOL WINAPI GetFileAttributesExA(LPCSTR file_name, GET_FILEEX_INFO_LEVELS level, LPVOID file_information)
{
	struct ps3_host_file_information information;
	char path[HOST_PATH_SIZE];
	int result;

	if (level != GetFileExInfoStandard)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	platform_translate_path(file_name, path, sizeof(path));
	result = ps3_host_path_information(path, &information);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	fill_attribute_data(&information, file_information);
	return TRUE;
}

BOOL WINAPI SetFileAttributesA(LPCSTR file_name, DWORD attributes)
{
	char path[HOST_PATH_SIZE];
	int result;

	platform_translate_path(file_name, path, sizeof(path));
	/* only the read-only bit has an lv2 equivalent */
	result = ps3_host_path_set_read_only(path, (attributes & FILE_ATTRIBUTE_READONLY) != 0);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI DeleteFileA(LPCSTR file_name)
{
	char path[HOST_PATH_SIZE];
	int result;

	platform_translate_path(file_name, path, sizeof(path));
	result = ps3_host_path_unlink(path);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI MoveFileA(LPCSTR existing_file_name, LPCSTR new_file_name)
{
	char from[HOST_PATH_SIZE], to[HOST_PATH_SIZE];
	int result;

	platform_translate_path(existing_file_name, from, sizeof(from));
	platform_translate_path(new_file_name, to, sizeof(to));
	if (path_exists(to))
	{
		SetLastError(ERROR_ALREADY_EXISTS);
		return FALSE;
	}
	result = ps3_host_path_rename(from, to);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI CopyFileA(LPCSTR existing_file_name, LPCSTR new_file_name, BOOL fail_if_exists)
{
	char from[HOST_PATH_SIZE], to[HOST_PATH_SIZE];
	static char buffer[65536];
	int32_t source, destination;
	__uint64 start = ps3_host_microseconds(), copied = 0;
	BOOL success = TRUE;
	int result;

	platform_translate_path(existing_file_name, from, sizeof(from));
	platform_translate_path(new_file_name, to, sizeof(to));
	result = ps3_host_file_open(from, PS3_HOST_O_RDONLY, &source);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	result = ps3_host_file_open(to, PS3_HOST_O_WRONLY | PS3_HOST_O_CREAT | (fail_if_exists ? PS3_HOST_O_EXCL : PS3_HOST_O_TRUNC),
		&destination);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		ps3_host_file_close(source);
		return FALSE;
	}
	for (;;)
	{
		__uint64 count = 0, written = 0;

		result = ps3_host_file_read(source, buffer, sizeof(buffer), &count);
		if (result || !count)
			break;
		result = ps3_host_file_write(destination, buffer, count, &written);
		copied += written;
		if (result || written != count)
			break;
	}
	/* the profile copies (blam.sav, savegame.bin) go to the disk as at a file's close */
	if (!result)
		result = ps3_host_file_sync(destination);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		success = FALSE;
	}
	ps3_host_file_close(source);
	ps3_host_file_close(destination);
	if (copied >= TRANSFER_LOG_BYTES)
		platform_log("copy %s to %s: %llu KB in %llu ms", from, to, copied >> 10,
			(ps3_host_microseconds() - start) / 1000);
	return success;
}

BOOL WINAPI CreateDirectoryA(LPCSTR path_name, LPSECURITY_ATTRIBUTES security_attributes)
{
	char path[HOST_PATH_SIZE];
	int result;

	(void)security_attributes;
	platform_translate_path(path_name, path, sizeof(path));
	result = ps3_host_path_make_directory(path);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR path_name)
{
	char path[HOST_PATH_SIZE];
	int result;

	platform_translate_path(path_name, path, sizeof(path));
	result = ps3_host_path_remove_directory(path);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR directory_name, PULARGE_INTEGER free_bytes_available,
	PULARGE_INTEGER total_bytes, PULARGE_INTEGER total_free_bytes)
{
	char path[HOST_PATH_SIZE];
	__uint64 free_bytes, total;
	int result;

	platform_translate_path(directory_name ? directory_name : "d:\\", path, sizeof(path));
	result = ps3_host_disk_space(path, &free_bytes, &total);
	if (result)
	{
		platform_log("free space of %s: failed (%d)", path, result);
		platform_set_last_error_from_errno(result);
		return FALSE;
	}
	platform_log("free space of %s: %llu MB", path, free_bytes >> 20);
	if (free_bytes_available)
		free_bytes_available->QuadPart = free_bytes;
	if (total_free_bytes)
		total_free_bytes->QuadPart = free_bytes;
	if (total_bytes)
		total_bytes->QuadPart = total;
	return TRUE;
}

/* ---------- directory enumeration */

struct platform_find
{
	int32_t directory;
	char directory_path[HOST_PATH_SIZE];
	char pattern[MAX_PATH];
};

static void find_destroy(struct platform_handle *handle)
{
	struct platform_find *find = handle->data;

	ps3_host_directory_close(find->directory);
	free(find);
}

/* Win32 wildcards: '*' any run, '?' one character, in any case; "*.*" also matches names
without a dot */
static BOOL wildcard_match(const char *pattern, const char *name)
{
	if (!strcmp(pattern, "*.*") || !strcmp(pattern, "*"))
		return TRUE;
	while (*pattern)
	{
		if (*pattern == '*')
		{
			while (*pattern == '*')
				pattern++;
			if (!*pattern)
				return TRUE;
			for (; *name; name++)
			{
				if (wildcard_match(pattern, name))
					return TRUE;
			}
			return FALSE;
		}
		if (!*name)
			return FALSE;
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name))
			return FALSE;
		pattern++;
		name++;
	}
	return *name == '\0';
}

static BOOL find_next_entry(struct platform_find *find, LPWIN32_FIND_DATAA data)
{
	char name[MAX_PATH];

	while (ps3_host_directory_next(find->directory, name, sizeof(name)))
	{
		char full_path[HOST_PATH_SIZE + MAX_PATH];
		struct ps3_host_file_information information;
		WIN32_FILE_ATTRIBUTE_DATA attributes;

		if (!wildcard_match(find->pattern, name))
			continue;
		snprintf(full_path, sizeof(full_path), "%s/%s", find->directory_path, name);
		if (ps3_host_path_information(full_path, &information) != 0)
			continue;
		fill_attribute_data(&information, &attributes);
		memset(data, 0, sizeof(*data));
		data->dwFileAttributes = attributes.dwFileAttributes;
		data->ftCreationTime = attributes.ftCreationTime;
		data->ftLastAccessTime = attributes.ftLastAccessTime;
		data->ftLastWriteTime = attributes.ftLastWriteTime;
		data->nFileSizeHigh = attributes.nFileSizeHigh;
		data->nFileSizeLow = attributes.nFileSizeLow;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", name);
		return TRUE;
	}
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

HANDLE WINAPI FindFirstFileA(LPCSTR file_name, LPWIN32_FIND_DATAA data)
{
	struct platform_find *find = calloc(1, sizeof(*find));
	struct platform_handle *handle;
	char xbox_directory[MAX_PATH];
	const char *separator = strrchr(file_name, '\\');
	int result;

	if (!find)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	if (!separator)
		separator = strrchr(file_name, '/');
	if (separator)
	{
		snprintf(xbox_directory, sizeof(xbox_directory), "%.*s", (int)(separator - file_name), file_name);
		snprintf(find->pattern, sizeof(find->pattern), "%s", separator + 1);
	}
	else
	{
		xbox_directory[0] = '\0';
		snprintf(find->pattern, sizeof(find->pattern), "%s", file_name);
	}
	platform_translate_path(xbox_directory, find->directory_path, sizeof(find->directory_path));
	result = ps3_host_directory_open(find->directory_path, &find->directory);
	if (result)
	{
		platform_set_last_error_from_errno(result);
		free(find);
		return INVALID_HANDLE_VALUE;
	}
	handle = platform_handle_new(_platform_handle_find, find, find_destroy);
	if (!handle)
	{
		ps3_host_directory_close(find->directory);
		free(find);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	if (!find_next_entry(find, data))
	{
		CloseHandle(handle);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}

BOOL WINAPI FindNextFileA(HANDLE find_file, LPWIN32_FIND_DATAA data)
{
	struct platform_handle *handle = platform_handle_get(find_file, _platform_handle_find);

	if (!handle)
		return FALSE;
	return find_next_entry(handle->data, data);
}

/* FindClose is an XDK macro for CloseHandle */
