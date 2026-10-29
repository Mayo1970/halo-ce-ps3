/* GAME_STATE_XBOX.C. Its Xbox symbols and their addresses are listed in the
upstream halo-ce-universal copy of source/saved games/game_state_xbox.c. */

/* ---------- headers */

#include "cache/physical_memory_map.h"
#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "interface/player_ui.h"
#include "memory/crc.h"
#include "saved games/game_state.h"
#include "sound/sound_manager.h"
#include <xtl.h>
#ifdef HALO_PS3
#include "ps3_spu_game.h" /* port: port/ps3/src/ps3_spu.c */
#include "ps3_vmx.h" /* port: port/ps3/src/ps3_vmx.c */
#endif

/* ---------- constants */

enum
{
	CPU_PAGE_SIZE = 0x1000,
	/* the native builds' larger game state (halo_port_capacity.h); the saved
	game files only need to hold it */
	GAME_STATE_SIZE = HALO_PORT_GAME_STATE_SIZE,
	GAME_STATE_FILE_SIZE = HALO_PORT_GAME_STATE_SIZE,
	/* port: a checkpoint's write in parts, the game's reads of its files served between them; its copy on a cache
	line, as the state (PS3) */
	CHECKPOINT_WRITE_PART = 256*1024,
	CHECKPOINT_LINE = 128
};

/* ---------- structures */

struct xbox_game_state_globals_prefix
{
	boolean buffer_allocated;
	byte reserved001[3];
	void *HALO_PTR32 buffer;
	long buffer_size;
	boolean file_open;
	boolean file_valid_for_read;
	byte reserved00E[2];
	HANDLE HALO_PTR32 handle;
};

typedef char verify_xbox_game_state_buffer_offset[
	offsetof(struct xbox_game_state_globals_prefix, buffer) == 0x4 ? 1 : -1];
typedef char verify_xbox_game_state_file_open_offset[
	offsetof(struct xbox_game_state_globals_prefix, file_open) == 0xC ? 1 : -1];
typedef char verify_xbox_game_state_handle_offset[
	offsetof(struct xbox_game_state_globals_prefix, handle) == 0x10 ? 1 : -1];
typedef char verify_xbox_game_state_globals_prefix_size[
	sizeof(struct xbox_game_state_globals_prefix) == 0x14 ? 1 : -1];

#ifdef HALO_PS3
/* port: a checkpoint's state copied in main memory, which a thread of its own writes to the file: the 20 MB write held
the game about 1.4 s on the PS3's disk; a revert takes the copy, the bytes the file holds */
struct game_state_checkpoint
{
	/* the used parts (game_state_port_used_sizes): the CPU part's, then the GPU part's at gpu_offset; the rest is zeroes.
	The image is on a CHECKPOINT_LINE in the block allocated */
	byte *block;
	byte *image;
	long image_size;
	long cpu_size;
	long gpu_size;
	long gpu_offset;
	boolean image_valid;
	boolean write_failed;
	HANDLE writer;
	DWORD write_error;
};
#endif

/* ---------- prototypes */

static void game_state_header_terminate(
	void *header,
	long header_size);
static HANDLE game_state_open_persistent_storage(
	const char *directory);
#ifdef HALO_PS3
static boolean game_state_checkpoint_save(
	void);
static void game_state_checkpoint_wait(
	void);
static long game_state_checkpoint_spu_save(
	byte *buffer,
	long cpu_size,
	long gpu_size,
	long gap_size,
	long gpu_offset);
static boolean game_state_checkpoint_spu_revert(
	byte *buffer,
	long cpu_size,
	long gpu_size,
	long gap_size,
	long gpu_offset);
#endif

/* ---------- globals */

static struct xbox_game_state_globals_prefix xbox_game_state_globals = { 0 };
#ifdef HALO_PS3
static struct game_state_checkpoint game_state_checkpoint;
#endif

/* ---------- public code */

void *game_state_allocate_buffer(
	unsigned long address,
	unsigned long cpu_size,
	unsigned long gpu_size)
{
	void *result;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		46,
		!xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		48,
		address);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		49,
		cpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		50,
		gpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		53,
		!(cpu_size&(CPU_PAGE_SIZE-1)));
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		54,
		!(gpu_size&(CPU_PAGE_SIZE-1)));

	result = physical_memory_get_game_state_base_address();
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		58,
		result);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		59,
		(unsigned long)result==address);

	XPhysicalProtect(
		(void *)(address+cpu_size),
		gpu_size,
		PAGE_READWRITE|PAGE_WRITECOMBINE);

	xbox_game_state_globals.buffer_allocated = TRUE;
	xbox_game_state_globals.buffer = (void *)address;
	xbox_game_state_globals.buffer_size = cpu_size+gpu_size;

	return result;
}

void game_state_free_buffer(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		75,
		xbox_game_state_globals.buffer_allocated);
	XPhysicalFree(xbox_game_state_globals.buffer);
	xbox_game_state_globals.buffer_allocated = FALSE;

	return;
}

void game_state_create_or_open_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		86,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		87,
		!xbox_game_state_globals.file_open);

	xbox_game_state_globals.handle = CreateFileA("z:\\savegame.bin",
		GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
		FILE_FLAG_NO_BUFFERING|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (xbox_game_state_globals.handle != INVALID_HANDLE_VALUE &&
		SetFilePointer(xbox_game_state_globals.handle, GAME_STATE_FILE_SIZE, NULL,
			FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		SetEndOfFile(xbox_game_state_globals.handle))
	{
		xbox_game_state_globals.file_open = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			97,
			FALSE,
			csprintf(temporary, "couldn't open or create saved game file (#%d)",
				GetLastError()));
	}

	return;
}

void game_state_close_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		106,
		xbox_game_state_globals.file_open);
#ifdef HALO_PS3
	game_state_checkpoint_wait(); /* port: (game_state_checkpoint_save) */
#endif
	CloseHandle(xbox_game_state_globals.handle);
	xbox_game_state_globals.file_open = FALSE;

	return;
}

boolean game_state_write_to_file(
	void)
{
	unsigned long bytes_written;
	boolean result = FALSE;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		120,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		121,
		xbox_game_state_globals.file_open);

#ifdef HALO_PS3
	/* port: copied, then written by a thread of its own (game_state_checkpoint_save); with no memory for the copy,
	written here as before */
	if (game_state_checkpoint_save())
	{
		xbox_game_state_globals.file_valid_for_read = TRUE;
		return TRUE;
	}
#endif
	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		WriteFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
		bytes_written == xbox_game_state_globals.buffer_size)
	{
		xbox_game_state_globals.file_valid_for_read = TRUE;
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			132,
			FALSE,
			csprintf(temporary, "couldn't write saved game file (#%d)", GetLastError()));
	}

	return result;
}

boolean game_state_read_from_file(
	void)
{
	unsigned long bytes_read;
	boolean result = FALSE;
	void *image;
	void *target; /* port: (the image, or the game state) */

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		144,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		145,
		xbox_game_state_globals.file_open);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		146,
		xbox_game_state_globals.file_valid_for_read || recover_saved_games_hack);

#ifdef HALO_PS3
	/* port: the last checkpoint's copy, the bytes its file holds (game_state_checkpoint_save), put over the game state
	and checked there (as the image is checked before it is taken; a refusal resets the map) */
	if (game_state_checkpoint.image_valid)
	{
		byte *buffer = (byte *)xbox_game_state_globals.buffer;
		long cpu_size = game_state_checkpoint.cpu_size;
		long gpu_size = game_state_checkpoint.gpu_size;
		long gap_size = xbox_game_state_globals.buffer_size - cpu_size - gpu_size;
		long gpu_offset = game_state_checkpoint.gpu_offset;
		boolean spu_copied;
		LARGE_INTEGER start, copied;
		char line[128];

		QueryPerformanceCounter(&start);
		spu_copied = game_state_checkpoint_spu_revert(buffer, cpu_size, gpu_size, gap_size, gpu_offset);
		if (!spu_copied)
		{
			ps3_vmx_copy(buffer, game_state_checkpoint.image, cpu_size);
			ps3_vmx_zero(buffer + cpu_size, gap_size);
			ps3_vmx_copy(buffer + xbox_game_state_globals.buffer_size - gpu_size, game_state_checkpoint.image + gpu_offset,
				gpu_size);
		}
		QueryPerformanceCounter(&copied);
		sprintf(line, "[halo] checkpoint: its copy put back by %s in %.1f ms\n", spu_copied ? "an SPU" : "the PPE",
			(copied.u.LowPart - start.u.LowPart) / 1000.0);
		OutputDebugStringA(line);
		result = game_state_image_accept(buffer, xbox_game_state_globals.buffer_size);
		if (!result)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				156,
				FALSE,
				csprintf(temporary, "couldn't read saved game file (#%d)", GetLastError()));
		}
		return result;
	}
#endif
	/* port: read beside the game state, and taken once checked */
	image = malloc(xbox_game_state_globals.buffer_size);
	target = image;
#ifdef HALO_PS3
	/* port: with no memory for the copy (after a long session), into the game state: a refusal resets the map, as the
	checkpoint copy's above */
	if (!target)
		target = xbox_game_state_globals.buffer;
#endif
	if (target &&
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		ReadFile(xbox_game_state_globals.handle, target,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		bytes_read == xbox_game_state_globals.buffer_size &&
		game_state_image_accept(target, xbox_game_state_globals.buffer_size))
	{
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			156,
			FALSE,
			csprintf(temporary, "couldn't read saved game file (#%d)", GetLastError()));
	}
	/* port: NULL when the read went into the game state; debug_free halts on NULL */
	if (image)
		free(image);

	return result;
}

boolean game_state_write_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean result = FALSE;

	CreateDirectoryA("d:\\core", NULL);
	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (WriteFile(file, buffer, buffer_size, &bytes_written, NULL) &&
			bytes_written == buffer_size)
		{
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

boolean game_state_read_core_header(
	const char *name,
	void *header,
	long header_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;
	boolean result = FALSE;

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (ReadFile(file, header, header_size, &bytes_read, NULL) &&
			bytes_read == header_size)
		{
			game_state_header_terminate(header, header_size);
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

void game_state_read_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;

	void *image = malloc(buffer_size);
#ifdef HALO_PS3
	/* port: with no memory for the copy, into the game state (a refused one halts below, as before) */
	void *target = image ? image : buffer;
#else
	void *target = image;
#endif

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	/* port: read beside the game state, and taken once checked */
	match_vassert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		226,
		target && file != INVALID_HANDLE_VALUE &&
			ReadFile(file, target, buffer_size, &bytes_read, NULL) &&
			bytes_read == buffer_size &&
			game_state_image_accept(target, buffer_size),
		"game state has been corrupted (thank you, come again)");
	(void)buffer;
	CloseHandle(file);
	/* port: NULL when the read went into the game state; debug_free halts on NULL */
	if (image)
		free(image);

	return;
}

const char *game_state_get_persistent_storage_filename(
	void)
{
	return "savegame.bin";
}

static __inline boolean game_state_get_persistent_storage_path(
	char *path)
{
	boolean result;

	result = player_ui_get_path_to_local_player_profile_directory(0, path) != FALSE;

	return result;
}

static void delete_persistent_storage(
	void)
{
	char path[256];

	if (game_state_get_persistent_storage_path(path))
		DeleteFileA(path);

	return;
}

static HANDLE game_state_open_persistent_storage(
	const char *directory)
{
	char path[256];
	byte zeroes[16*1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean success = FALSE;

	if (directory || game_state_get_persistent_storage_path(path))
	{
		if (directory)
		{
			strcpy(path, directory);
		}
		else
		{
			game_state_get_persistent_storage_path(path);
		}
		strcat(path, "savegame.bin");

		file = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0,
			NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			success = TRUE;
			if (GetFileSize(file, NULL) != GAME_STATE_FILE_SIZE)
			{
				memset(zeroes, 0, sizeof(zeroes));
				if (!WriteFile(file, zeroes, sizeof(zeroes), &bytes_written, NULL) ||
					bytes_written != sizeof(zeroes) ||
					SetFilePointer(file, GAME_STATE_FILE_SIZE, NULL, FILE_BEGIN) ==
						INVALID_SET_FILE_POINTER ||
					!SetEndOfFile(file))
				{
					match_vassert(
						"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
						491,
						FALSE,
						csprintf(temporary,
							"couldn't resize persistent storage \"%s\"", path));
					delete_persistent_storage();
					CloseHandle(file);
					success = FALSE;
				}
			}
		}
		else
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				498,
				FALSE,
				csprintf(temporary, "couldn't open or create persistent storage \"%s\"",
					path));
		}
	}

	return success ? file : INVALID_HANDLE_VALUE;
}

/* port: the strings of a header read from a file end in it (the file is
anyone's) */
static void game_state_header_terminate(
	void *header,
	long header_size)
{
	if (header_size == sizeof(struct game_state_header))
	{
		struct game_state_header *game_state_header = header;

		game_state_header->map_name[NUMBEROF(game_state_header->map_name) - 1] = 0;
		game_state_header->build_number[NUMBEROF(game_state_header->build_number) - 1] = 0;
	}
}

boolean game_state_read_header_from_persistent_storage(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted)
{
	byte buffer[128*1024];
	HANDLE file;
	unsigned long checksum;
	unsigned long stored_checksum;
	unsigned long bytes_read;
	long remaining_size;
	long read_size;
	boolean result;

	file = game_state_open_persistent_storage(NULL);
	result = FALSE;
	if (corrupted)
		*corrupted = FALSE;

	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, header, header_size, &bytes_read, NULL) ||
			bytes_read != header_size)
		{
			game_state_header_terminate(header, header_size);
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				298,
				FALSE,
				csprintf(temporary,
					"couldn't read header from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		else
		{
			game_state_header_terminate(header, header_size);
			stored_checksum = *header_checksum;

			crc_new(&checksum);
			*header_checksum = 0;
			crc_checksum_buffer(&checksum, header, header_size);

			remaining_size = buffer_size-header_size;
			while (remaining_size>0)
			{
				read_size = MIN(sizeof(buffer), remaining_size);
				if (ReadFile(file, buffer, read_size, &bytes_read, NULL) &&
					bytes_read == read_size)
				{
					crc_checksum_buffer(&checksum, buffer, read_size);
				}
				sound_idle();
				remaining_size -= read_size;
			}

			if (checksum == stored_checksum)
			{
				result = TRUE;
			}
			else
			{
				if (corrupted && stored_checksum)
					*corrupted = TRUE;
				error(_error_silent, "checksum failed on persistent storage");
			}
		}
		CloseHandle(file);
	}

	return result;
}

void game_state_write_to_persistent_storage(
	void *buffer,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size)
{
	byte saved_header[2048];
	HANDLE file;
	unsigned long checksum;
	unsigned long bytes_written;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		*header_checksum = 0;

		crc_new(&checksum);
		crc_checksum_buffer(&checksum, buffer, GAME_STATE_SIZE);
		*header_checksum = checksum;

		match_assert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			333,
			header_size<sizeof(saved_header));

		memcpy(saved_header, buffer, header_size);
		memset(buffer, 0, header_size);

		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, buffer, buffer_size, &bytes_written, NULL) ||
			bytes_written != buffer_size ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, saved_header, header_size, &bytes_written, NULL) ||
			bytes_written != header_size)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				351,
				FALSE,
				csprintf(temporary, "failed to write to persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}

		memcpy(buffer, saved_header, header_size);
		CloseHandle(file);
	}

	return;
}

void game_state_read_from_persistent_storage(
	void *buffer,
	long buffer_size)
{
	HANDLE file;
	unsigned long bytes_read;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		/* port: read beside the game state, and taken once checked */
		void *image = malloc(buffer_size);
#ifdef HALO_PS3
		/* port: with no memory for the copy (after a long session), into the game state: the file's checksum already
		passed (game_state_read_header_from_persistent_storage) */
		void *target = image ? image : buffer;
#else
		void *target = image;
#endif

		if (!target ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, target, buffer_size, &bytes_read, NULL) ||
			bytes_read != buffer_size ||
			!game_state_image_accept(target, buffer_size))
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				383,
				FALSE,
				csprintf(temporary, "failed to read from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		(void)buffer;
		/* port: NULL when the read went into the game state; debug_free halts on NULL */
		if (image)
			free(image);
		CloseHandle(file);
	}

	return;
}

void game_state_create_persistent_storage(
	const char *path)
{
	HANDLE handle;

	handle = game_state_open_persistent_storage(path);
	if (handle != INVALID_HANDLE_VALUE)
		CloseHandle(handle);

	return;
}

/* ---------- private code */

#ifdef HALO_PS3
/* port: zeroes, the game state's unused middle as the file holds it (never written) */
static byte game_state_checkpoint_zeroes[64*1024];
/* port: the writer's waits for the reads (port/ps3/xbox/xbox_files.c) in this write; FALSE: the game thread writes */
static DWORD game_state_checkpoint_paused;
static boolean game_state_checkpoint_yields;

int xbox_files_reads_running(void); /* port: port/ps3/xbox/xbox_files.c */

/* port: the reads first (a bsp's, the game thread's), each part after them; at most 2 s a part */
static void game_state_checkpoint_yield(
	void)
{
	DWORD start = GetTickCount();

	while (game_state_checkpoint_yields && xbox_files_reads_running() > 0 && GetTickCount() - start < 2000)
		Sleep(1);
	game_state_checkpoint_paused += GetTickCount() - start;
}

/* port: size bytes from data (NULL: zeroes) to the file at its pointer, in parts (CHECKPOINT_WRITE_PART) */
static boolean game_state_checkpoint_write_range(
	byte const *data,
	long size)
{
	long offset = 0;
	unsigned long bytes_written;

	while (offset < size)
	{
		long part = MIN(data ? CHECKPOINT_WRITE_PART : (long)sizeof(game_state_checkpoint_zeroes), size - offset);

		game_state_checkpoint_yield();
		if (!WriteFile(xbox_game_state_globals.handle, data ? data + offset : game_state_checkpoint_zeroes, part,
			&bytes_written, NULL) || bytes_written != (unsigned long)part)
		{
			return FALSE;
		}
		offset += part;
	}

	return TRUE;
}

/* port: the copy to the file, the whole state's bytes (the used parts, zeroes between), from the writer's thread or
the game's */
static DWORD WINAPI game_state_checkpoint_writer(
	LPVOID parameter)
{
	DWORD start = GetTickCount();
	long cpu_size = game_state_checkpoint.cpu_size;
	long gpu_size = game_state_checkpoint.gpu_size;
	char line[128];

	game_state_checkpoint_yields = parameter != NULL;
	game_state_checkpoint_paused = 0;
	game_state_checkpoint.write_failed =
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
		!game_state_checkpoint_write_range(game_state_checkpoint.image, cpu_size) ||
		!game_state_checkpoint_write_range(NULL, xbox_game_state_globals.buffer_size - cpu_size - gpu_size) ||
		!game_state_checkpoint_write_range(game_state_checkpoint.image + game_state_checkpoint.gpu_offset, gpu_size);
	game_state_checkpoint.write_error = game_state_checkpoint.write_failed ? GetLastError() : 0;
	sprintf(line, "[halo] checkpoint: %ld KB written by the writer in %lu ms (%lu waiting for reads)%s\n",
		xbox_game_state_globals.buffer_size >> 10, (unsigned long)(GetTickCount() - start),
		(unsigned long)game_state_checkpoint_paused, game_state_checkpoint.write_failed ? ", failed" : "");
	OutputDebugStringA(line);

	return 0;
}

/* port: the last checkpoint's write done; a failed one reported as game_state_write_to_file did */
static void game_state_checkpoint_wait(
	void)
{
	if (game_state_checkpoint.writer)
	{
		WaitForSingleObject(game_state_checkpoint.writer, INFINITE);
		CloseHandle(game_state_checkpoint.writer);
		game_state_checkpoint.writer = NULL;
	}
	if (game_state_checkpoint.write_failed)
	{
		game_state_checkpoint.write_failed = FALSE;
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			132,
			FALSE,
			csprintf(temporary, "couldn't write saved game file (#%d)", game_state_checkpoint.write_error));
	}

	return;
}

/* port: the copies by an SPU's DMA (port/ps3/spu/spu_game.c): the middle's check, the used parts; the answer of the
check is the SPU's own */
static spu_game_answer game_state_checkpoint_answer;

/* port: the copy's GPU part after the CPU part's 16-byte lines, at its source's alignment: the SPU's DMA takes two
addresses alike in their low 4 bits */
static long game_state_checkpoint_gpu_offset(
	long cpu_size,
	long gpu_size)
{
	return ((cpu_size + 15) & ~15) + (-gpu_size & 15);
}

/* port: the 16-byte lines of the state's parts are the SPU's, the bytes at their edges (under 16 each) the PPE's: the
CPU part's last, the middle's first and last, the GPU part's first (allocations are 4-byte multiples) */
struct game_state_checkpoint_edges
{
	long cpu_body;
	long gap_head;
	long gap_body;
	long gap_tail;
	long gpu_head;
};

static void game_state_checkpoint_edges_make(
	long cpu_size,
	long gpu_size,
	long gap_size,
	struct game_state_checkpoint_edges *edges)
{
	edges->cpu_body = cpu_size & ~15;
	edges->gap_head = -cpu_size & 15;
	edges->gap_tail = -gpu_size & 15;
	edges->gap_body = gap_size - edges->gap_head - edges->gap_tail;
	if (edges->gap_body < 0)
	{
		edges->gap_head = gap_size;
		edges->gap_tail = 0;
		edges->gap_body = 0;
	}
	edges->gpu_head = gpu_size & 15;
}

/* port: 1 when the SPU copied the used parts and found the middle zeroes, 0 when not zeroes, NONE when the SPU did not
(the PPE does it all) */
static long game_state_checkpoint_spu_save(
	byte *buffer,
	long cpu_size,
	long gpu_size,
	long gap_size,
	long gpu_offset)
{
	byte *image = game_state_checkpoint.image;
	byte *gap = buffer + cpu_size;
	byte *gpu = buffer + xbox_game_state_globals.buffer_size - gpu_size;
	struct game_state_checkpoint_edges edges;
	uint32_t ticket;
	long index;

	if (!ps3_spu_game_running() ||
		((PS3_SPU_EA(buffer) | PS3_SPU_EA(image) | (uint32_t)xbox_game_state_globals.buffer_size) & 15))
	{
		return NONE;
	}
	game_state_checkpoint_edges_make(cpu_size, gpu_size, gap_size, &edges);
	game_state_checkpoint_answer.status = 0;
	ps3_spu_game_submit(SPU_GAME_ZEROES, (uint32_t)edges.gap_body,
		PS3_SPU_EA(edges.gap_body ? gap + edges.gap_head : buffer), PS3_SPU_EA(&game_state_checkpoint_answer));
	ps3_spu_game_submit(SPU_GAME_COPY, (uint32_t)edges.cpu_body, PS3_SPU_EA(buffer), PS3_SPU_EA(image));
	ticket = ps3_spu_game_submit(SPU_GAME_COPY, (uint32_t)(gpu_size - edges.gpu_head), PS3_SPU_EA(gpu + edges.gpu_head),
		PS3_SPU_EA(image + gpu_offset + edges.gpu_head));
	if (!ps3_spu_game_wait(ticket) || game_state_checkpoint_answer.status != SPU_GAME_DONE)
	{
		return NONE;
	}
	memcpy(image + edges.cpu_body, buffer + edges.cpu_body, cpu_size - edges.cpu_body);
	memcpy(image + gpu_offset, gpu, edges.gpu_head);
	if (!game_state_checkpoint_answer.value)
	{
		return 0;
	}
	for (index = 0; index < edges.gap_head; index++)
	{
		if (gap[index])
			return 0;
	}
	for (index = gap_size - edges.gap_tail; index < gap_size; index++)
	{
		if (gap[index])
			return 0;
	}

	return 1;
}

/* port: the checkpoint's copy put over the state by the SPU's DMA, the middle zeroed, the edges here; FALSE when the
SPU did not */
static boolean game_state_checkpoint_spu_revert(
	byte *buffer,
	long cpu_size,
	long gpu_size,
	long gap_size,
	long gpu_offset)
{
	byte *image = game_state_checkpoint.image;
	byte *gap = buffer + cpu_size;
	byte *gpu = buffer + xbox_game_state_globals.buffer_size - gpu_size;
	struct game_state_checkpoint_edges edges;
	uint32_t ticket;

	if (!ps3_spu_game_running() ||
		((PS3_SPU_EA(buffer) | PS3_SPU_EA(image) | (uint32_t)xbox_game_state_globals.buffer_size) & 15))
	{
		return FALSE;
	}
	game_state_checkpoint_edges_make(cpu_size, gpu_size, gap_size, &edges);
	ps3_spu_game_submit(SPU_GAME_COPY, (uint32_t)edges.cpu_body, PS3_SPU_EA(image), PS3_SPU_EA(buffer));
	ps3_spu_game_submit(SPU_GAME_ZERO, (uint32_t)edges.gap_body, 0,
		PS3_SPU_EA(edges.gap_body ? gap + edges.gap_head : buffer));
	ticket = ps3_spu_game_submit(SPU_GAME_COPY, (uint32_t)(gpu_size - edges.gpu_head),
		PS3_SPU_EA(image + gpu_offset + edges.gpu_head), PS3_SPU_EA(gpu + edges.gpu_head));
	if (!ps3_spu_game_wait(ticket))
	{
		return FALSE;
	}
	memcpy(buffer + edges.cpu_body, image + edges.cpu_body, cpu_size - edges.cpu_body);
	memset(gap, 0, edges.gap_head);
	memset(gap + gap_size - edges.gap_tail, 0, edges.gap_tail);
	memcpy(gpu, image + gpu_offset, edges.gpu_head);

	return TRUE;
}

/* port: the state's used parts copied (once the last write is done), then written by a thread of its own, or here if
none starts; FALSE (written as before) with no memory for the copy, or an unused middle that is not zeroes */
static boolean game_state_checkpoint_save(
	void)
{
	byte *buffer = (byte *)xbox_game_state_globals.buffer;
	long cpu_size;
	long gpu_size;
	long gap_size;
	long gpu_offset;
	long spu_copied;
	LARGE_INTEGER start, checked, copied;
	char line[256];

	game_state_checkpoint_wait();
	game_state_checkpoint.image_valid = FALSE;
	game_state_port_used_sizes(&cpu_size, &gpu_size);
	gap_size = xbox_game_state_globals.buffer_size - cpu_size - gpu_size;
	if (cpu_size < 0 || gpu_size < 0 || gap_size < 0)
		return FALSE;
	gpu_offset = game_state_checkpoint_gpu_offset(cpu_size, gpu_size);
	if (!game_state_checkpoint.block || game_state_checkpoint.image_size != gpu_offset + gpu_size)
	{
		/* (the game's free halts on NULL: debug_memory.c) */
		if (game_state_checkpoint.block)
			free(game_state_checkpoint.block);
		game_state_checkpoint.image_size = gpu_offset + gpu_size;
		game_state_checkpoint.block = malloc(game_state_checkpoint.image_size + CHECKPOINT_LINE);
		game_state_checkpoint.image = game_state_checkpoint.block ? (byte *)(((uintptr_t)game_state_checkpoint.block +
			CHECKPOINT_LINE - 1) & ~(uintptr_t)(CHECKPOINT_LINE - 1)) : NULL;
	}
	if (!game_state_checkpoint.image)
		return FALSE;

	QueryPerformanceCounter(&start);
	spu_copied = game_state_checkpoint_spu_save(buffer, cpu_size, gpu_size, gap_size, gpu_offset);
	if (!spu_copied)
		return FALSE;
	if (spu_copied == NONE)
	{
		if (!ps3_vmx_zeroes(buffer + cpu_size, gap_size))
			return FALSE;
		QueryPerformanceCounter(&checked);
		ps3_vmx_copy(game_state_checkpoint.image, buffer, cpu_size);
		ps3_vmx_copy(game_state_checkpoint.image + gpu_offset, buffer + xbox_game_state_globals.buffer_size - gpu_size,
			gpu_size);
	}
	QueryPerformanceCounter(&copied);
	/* (the SPU checks the middle as it copies) */
	if (spu_copied != NONE)
		checked = copied;
	game_state_checkpoint.cpu_size = cpu_size;
	game_state_checkpoint.gpu_size = gpu_size;
	game_state_checkpoint.gpu_offset = gpu_offset;
	game_state_checkpoint.image_valid = TRUE;
	/* (microseconds: QueryPerformanceFrequency) */
	sprintf(line, "[halo] checkpoint: %ld KB of the state's %ld KB copied by %s in %.1f ms (its unused %ld KB checked to "
		"be zeroes in %.1f ms; the CPU part's %ld KB, the GPU part's %ld KB)\n", (cpu_size + gpu_size) >> 10,
		xbox_game_state_globals.buffer_size >> 10, spu_copied != NONE ? "an SPU" : "the PPE",
		(copied.u.LowPart - start.u.LowPart) / 1000.0, gap_size >> 10, (checked.u.LowPart - start.u.LowPart) / 1000.0,
		cpu_size >> 10, gpu_size >> 10);
	OutputDebugStringA(line);
	game_state_checkpoint.writer = CreateThread(NULL, 0, game_state_checkpoint_writer, (LPVOID)&game_state_checkpoint, 0,
		NULL);
	if (!game_state_checkpoint.writer)
	{
		game_state_checkpoint_writer(NULL);
		game_state_checkpoint_wait();
	}

	return TRUE;
}
#endif
