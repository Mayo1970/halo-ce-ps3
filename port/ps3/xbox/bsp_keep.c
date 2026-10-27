/* The level's structure BSPs kept in RSX memory (cache_files.c's hooks): a BSP loaded again is copied back by the RSX,
not read from the disk; a thread reads the level's other BSPs there while the game's disk is idle. */

#include <string.h>

#include "ps3_gpu.h"

#define KEEP_ENTRIES 32
/* RSX memory left free for textures and targets once a BSP is kept */
#define KEEP_RESERVE_BYTES (32u << 20)
#define PIECE_BYTES 0x20000u
/* the game's disk transfers stop this long before the reader's next piece */
#define QUIET_MICROSECONDS 100000
#define POLL_MICROSECONDS 20000
/* cache_files_windows.c's reads are whole sectors */
#define SECTOR_BYTES 512u

enum
{
	_kept_none,
	/* as read from the cache file, not swapped */
	_kept_read,
	/* swapped and through tag_validate_structure_bsp: the bytes its checks and corrections made */
	_kept_checked
};

/* the game's scenario_structure_bsp_reference (scenario_definitions.h) */
struct bsp_reference
{
	int32_t file_offset;
	int32_t file_size;
	uint32_t base_address;
	uint32_t unused;
	uint32_t structure_bsp[4];
};

typedef char bsp_reference_size_check[sizeof(struct bsp_reference) == 0x20 ? 1 : -1];

struct kept_bsp
{
	int32_t file_offset;
	int32_t file_size;
	uint32_t base_address;
	uint32_t bytes;
	/* its RSX memory, the size it was allocated with */
	void *memory;
	uint32_t memory_bytes;
	uint32_t offset;
	int state;
	/* a load or the reader copies it: not freed meanwhile; drop: freed after (another map loaded) */
	BOOL busy;
	BOOL drop;
	uint64_t sums[2];
};

static struct
{
	struct ps3_host_lock *lock;
	struct ps3_host_condition *wake;
	BOOL started;
	/* the loaded map is the kept BSPs' map (not the menu's or a Custom Edition map) */
	BOOL active;
	char map[32];
	int32_t tag_data_offset;
	int32_t tag_data_size;
	/* a map load's: the reader's piece of an older one is not kept */
	uint32_t generation;
	/* the level's BSPs (at its first BSP load), the loaded one, its cache file */
	long count;
	long current;
	char path[64];
	struct kept_bsp bsps[KEEP_ENTRIES];
	/* a BSP load runs; no room for more */
	BOOL loading;
	BOOL full;
} keep;

BOOL d3d8_bsp_copies_available(void);
void *d3d8_bsp_memory_allocate(uint32_t bytes, uint32_t *offset);
void d3d8_bsp_memory_release(void *memory, uint32_t bytes);
BOOL d3d8_bsp_copy_to_main(uint32_t source_offset, void *destination, uint32_t bytes);
/* (the game's boolean) */
unsigned char cache_file_open_map_path(char *path);
int xbox_files_reads_running(void);
__uint64 xbox_files_transfer_bytes(void);

/* Fletcher's sums over 64-bit words (8-aligned): a copy is checked against its source. A line at a time, the line 1 KB
ahead touched: the RSX's copy is in no cache, and a miss at each line took 8 ms for 5.4 MB */
static void sums_add(uint64_t sums[2], const void *data, uint32_t bytes)
{
	const uint64_t *word = data;
	uint64_t a = sums[0], b = sums[1];
	uint32_t count = bytes / 8;

	for (; count >= 16; count -= 16, word += 16)
	{
		int index;

		__asm__ volatile("dcbt 0,%0" : : "r"(word + 128));
		for (index = 0; index < 16; index++)
		{
			a += word[index];
			b += a;
		}
	}
	while (count--)
	{
		a += *word++;
		b += a;
	}
	sums[0] = a;
	sums[1] = b;
}

/* into RSX memory, which is cache-inhibited: aligned doubleword stores only (both 8-aligned), the sums on the way */
static void local_copy(void *destination, const void *source, uint32_t bytes, uint64_t sums[2])
{
	volatile uint64_t *to = destination;
	const uint64_t *from = source;
	uint64_t a = sums[0], b = sums[1];
	uint32_t count = bytes / 8;

	while (count--)
	{
		uint64_t word = *from++;

		*to++ = word;
		a += word;
		b += a;
	}
	sums[0] = a;
	sums[1] = b;
	__asm__ volatile("sync" : : : "memory");
}

/* the PPE's lines of the range written back and dropped: the RSX's copy there is then read from memory */
static void lines_flush(const void *address, uint32_t bytes)
{
	uintptr_t line = (uintptr_t)address & ~(uintptr_t)127, end = (uintptr_t)address + bytes;

	for (; line < end; line += 128)
		__asm__ volatile("dcbf 0,%0" : : "r"(line) : "memory");
	__asm__ volatile("sync" : : : "memory");
}

static double milliseconds(uint64_t start, uint64_t end)
{
	return (double)(end - start) / 1000.0;
}

static uint32_t kept_megabytes_tenths(void)
{
	uint64_t bytes = 0;
	long index;

	for (index = 0; index < KEEP_ENTRIES; index++)
	{
		if (keep.bsps[index].state != _kept_none)
			bytes += keep.bsps[index].bytes;
	}
	return (uint32_t)(bytes * 10 / 1048576);
}

/* (the lock held) */
static void entry_free(struct kept_bsp *bsp)
{
	if (bsp->memory)
		d3d8_bsp_memory_release(bsp->memory, bsp->memory_bytes);
	bsp->memory = NULL;
	bsp->state = _kept_none;
	bsp->drop = FALSE;
}

/* (the lock held) its RSX memory, when the textures keep their room */
static BOOL entry_allocate(struct kept_bsp *bsp)
{
	struct ps3_gpu_memory_usage usage;

	if (keep.full)
		return FALSE;
	ps3_gpu_memory_usage(&usage);
	if (usage.heap < usage.display + usage.used + bsp->bytes + KEEP_RESERVE_BYTES ||
		!(bsp->memory = d3d8_bsp_memory_allocate(bsp->bytes, &bsp->offset)))
	{
		keep.full = TRUE;
		platform_log("kept BSPs: no more kept (%lu KB asked, %lu KB of RSX memory free)",
			(unsigned long)(bsp->bytes >> 10),
			(unsigned long)((usage.heap - usage.display - usage.used) >> 10));
		return FALSE;
	}
	bsp->memory_bytes = bsp->bytes;
	return TRUE;
}

/* (the lock held) the entry that holds this BSP's copy */
static long entry_find(long file_offset, long file_size, const void *address)
{
	long index;

	for (index = 0; index < KEEP_ENTRIES; index++)
	{
		const struct kept_bsp *bsp = &keep.bsps[index];

		if (bsp->state != _kept_none && bsp->file_offset == file_offset && bsp->file_size == file_size &&
			bsp->base_address == (uint32_t)(uintptr_t)address)
			return index;
	}
	return -1;
}

/* (ps3_gpu.c, an allocation without room, its caller holding the RSX heap) the kept BSPs go first */
static int memory_short(uint32_t size)
{
	unsigned long dropped = 0;
	long index;

	if (!ps3_host_lock_try_enter(keep.lock))
		return 0;
	for (index = 0; index < KEEP_ENTRIES; index++)
	{
		struct kept_bsp *bsp = &keep.bsps[index];

		if (bsp->memory && !bsp->busy)
		{
			ps3_gpu_memory_release(bsp->memory, bsp->memory_bytes, _ps3_gpu_memory_bsp);
			bsp->memory = NULL;
			bsp->state = _kept_none;
			dropped++;
		}
	}
	keep.full = TRUE;
	ps3_host_lock_leave(keep.lock);
	if (dropped)
		platform_log("kept BSPs: %lu dropped, RSX memory short (%lu KB asked)", dropped, (unsigned long)(size >> 10));
	return dropped != 0;
}

/* (the lock held) the next BSP to read: after the loaded one first, as a level goes on; -1 none */
static long reader_pick(void)
{
	long step;

	if (!keep.active || keep.count < 2 || keep.full || keep.loading || !keep.path[0] || !d3d8_bsp_copies_available())
		return -1;
	for (step = 1; step <= keep.count; step++)
	{
		/* (the ones after it, then those before it from the nearest) */
		long after = keep.count - 1 - keep.current;
		long index = step <= after ? keep.current + step : keep.current - (step - after);
		struct kept_bsp *bsp;

		if (index < 0 || index >= keep.count)
			continue;
		bsp = &keep.bsps[index];
		if (bsp->state != _kept_none || bsp->busy)
			continue;
		if (!bsp->memory && !entry_allocate(bsp))
			return -1;
		return index;
	}
	return -1;
}

/* until the game has moved no bytes on the disk for a while; FALSE when its map changed */
static BOOL reader_quiet(uint32_t generation, __uint64 *last, uint64_t *quiet_since, uint64_t *waited)
{
	for (;;)
	{
		uint64_t now = ps3_host_microseconds();
		__uint64 bytes = xbox_files_transfer_bytes();

		if (keep.generation != generation)
			return FALSE;
		if (bytes != *last || xbox_files_reads_running() || keep.loading)
		{
			*last = bytes;
			*quiet_since = now;
		}
		else if (now - *quiet_since >= QUIET_MICROSECONDS)
		{
			return TRUE;
		}
		ps3_host_sleep_microseconds(POLL_MICROSECONDS);
		*waited += POLL_MICROSECONDS;
	}
}

static void reader_thread(void *unused)
{
	unsigned char *block = malloc(PIECE_BYTES + 127), *buffer;
	HANDLE file = INVALID_HANDLE_VALUE;
	uint32_t file_generation = 0;

	(void)unused;
	if (!block)
	{
		platform_log("kept BSPs: no memory for the reader's buffer");
		return;
	}
	buffer = (unsigned char *)(((uintptr_t)block + 127) & ~(uintptr_t)127);
	for (;;)
	{
		struct kept_bsp *bsp;
		uint64_t start, quiet_since, waited = 0, sums[2] = { 0, 0 };
		__uint64 last;
		uint32_t generation, done = 0, bytes, pieces = 0;
		int32_t file_offset;
		unsigned char *memory;
		char path[64];
		BOOL ok = TRUE;
		long index;

		ps3_host_lock_enter(keep.lock);
		while ((index = reader_pick()) < 0)
			ps3_host_condition_wait(keep.wake, 500000);
		bsp = &keep.bsps[index];
		bsp->busy = TRUE;
		generation = keep.generation;
		file_offset = bsp->file_offset;
		bytes = bsp->bytes;
		memory = bsp->memory;
		memcpy(path, keep.path, sizeof(path));
		ps3_host_lock_leave(keep.lock);

		if (file == INVALID_HANDLE_VALUE || file_generation != generation)
		{
			if (file != INVALID_HANDLE_VALUE)
				CloseHandle(file);
			file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
			file_generation = generation;
		}
		ok = file != INVALID_HANDLE_VALUE;
		start = ps3_host_microseconds();
		last = xbox_files_transfer_bytes();
		quiet_since = start;
		while (ok && done < bytes)
		{
			uint32_t piece = bytes - done < PIECE_BYTES ? bytes - done : PIECE_BYTES;
			OVERLAPPED overlapped;
			DWORD got = 0;

			if (!reader_quiet(generation, &last, &quiet_since, &waited))
			{
				ok = FALSE;
				break;
			}
			memset(&overlapped, 0, sizeof(overlapped));
			overlapped.Offset = (DWORD)file_offset + done;
			if (!ReadFile(file, buffer, piece, &got, &overlapped) || got != piece)
			{
				ok = FALSE;
				break;
			}
			/* (its own bytes: the game's are those after) */
			last = xbox_files_transfer_bytes();
			local_copy(memory + done, buffer, piece, sums);
			done += piece;
			pieces++;
		}

		ps3_host_lock_enter(keep.lock);
		bsp->busy = FALSE;
		if (bsp->drop)
		{
			entry_free(bsp);
		}
		else if (ok && keep.generation == generation && bsp->state == _kept_none)
		{
			bsp->state = _kept_read;
			bsp->sums[0] = sums[0];
			bsp->sums[1] = sums[1];
			platform_log("kept BSPs: BSP %ld (%lu KB) read ahead in %.1f s (%lu pieces; %.1f s waited for the game's "
				"disk); %.1f MB kept", index, (unsigned long)(bytes >> 10),
				milliseconds(start, ps3_host_microseconds()) / 1000.0, (unsigned long)pieces, (double)waited / 1e6,
				kept_megabytes_tenths() / 10.0);
		}
		else if (!ok && keep.generation == generation)
		{
			/* (a read that fails is not tried again for this map) */
			platform_log("kept BSPs: BSP %ld not read ahead (%s at %lu KB)", index, path, (unsigned long)(done >> 10));
			entry_free(bsp);
			keep.full = TRUE;
		}
		ps3_host_lock_leave(keep.lock);
	}
}

static BOOL keep_start(void)
{
	if (keep.started)
		return TRUE;
	keep.lock = ps3_host_lock_create(FALSE);
	keep.wake = keep.lock ? ps3_host_condition_create(keep.lock) : NULL;
	if (!keep.wake)
		return FALSE;
	keep.current = -1;
	ps3_gpu_set_memory_hook(memory_short);
	/* (below the game's threads: it reads while they wait) */
	if (ps3_host_thread_create(reader_thread, NULL, 0x20000, PS3_HOST_PRIORITY_NORMAL + 1, "bsp_keep") != 0)
		platform_log("kept BSPs: no reader thread, only the loaded BSPs are kept");
	keep.started = TRUE;
	return TRUE;
}

/* cache_files.c: a map loads (name NULL: a Custom Edition map); another's kept BSPs go, the menu's keeps them */
void bsp_keep_map(char const *name, long tag_data_offset, long tag_data_size)
{
	BOOL menu = name && !strncmp(name, "ui", sizeof(keep.map));
	BOOL same;
	unsigned long dropped = 0;
	long index;

	if (!keep_start())
		return;
	ps3_host_lock_enter(keep.lock);
	keep.generation++;
	keep.count = 0;
	keep.current = -1;
	keep.path[0] = 0;
	keep.full = FALSE;
	same = name && keep.map[0] && !strncmp(keep.map, name, sizeof(keep.map)) &&
		keep.tag_data_offset == tag_data_offset && keep.tag_data_size == tag_data_size;
	if (!same && !menu)
	{
		for (index = 0; index < KEEP_ENTRIES; index++)
		{
			struct kept_bsp *bsp = &keep.bsps[index];

			if (bsp->state != _kept_none)
				dropped++;
			if (bsp->busy)
				bsp->drop = TRUE;
			else
				entry_free(bsp);
			bsp->file_offset = bsp->file_size = 0;
			bsp->base_address = 0;
		}
		memset(keep.map, 0, sizeof(keep.map));
		if (name)
			strncpy(keep.map, name, sizeof(keep.map) - 1);
		keep.tag_data_offset = tag_data_offset;
		keep.tag_data_size = tag_data_size;
	}
	keep.active = name && !menu;
	ps3_host_lock_leave(keep.lock);
	if (dropped)
		platform_log("kept BSPs: %lu dropped (%s loads)", dropped, name ? name : "a Custom Edition map");
}

/* cache_files.c, before a BSP's read: its kept copy copied in place by the RSX; _kept_checked (no swap, no checks),
_kept_read (the caller swaps and checks it) or _kept_none (read it) */
int bsp_keep_restore(long file_offset, long file_size, void *address)
{
	struct kept_bsp *bsp;
	uint64_t start, copied_at, sums[2] = { 0, 0 };
	BOOL copied;
	long index;
	int state;

	if (!keep.started)
		return _kept_none;
	ps3_host_lock_enter(keep.lock);
	keep.loading = TRUE;
	index = keep.active && d3d8_bsp_copies_available() ? entry_find(file_offset, file_size, address) : -1;
	bsp = index >= 0 ? &keep.bsps[index] : NULL;
	if (!bsp || bsp->busy || !bsp->memory)
	{
		ps3_host_lock_leave(keep.lock);
		return _kept_none;
	}
	bsp->busy = TRUE;
	state = bsp->state;
	ps3_host_lock_leave(keep.lock);

	start = ps3_host_microseconds();
	lines_flush(address, bsp->bytes);
	copied = d3d8_bsp_copy_to_main(bsp->offset, address, bsp->bytes);
	copied_at = ps3_host_microseconds();
	if (copied)
		sums_add(sums, address, bsp->bytes);

	ps3_host_lock_enter(keep.lock);
	bsp->busy = FALSE;
	if (!copied || sums[0] != bsp->sums[0] || sums[1] != bsp->sums[1])
	{
		platform_log("kept BSPs: BSP %ld (%lu KB) %s: read from the disk", index, (unsigned long)(bsp->bytes >> 10),
			copied ? "copied back wrong" : "not copied back");
		entry_free(bsp);
		state = _kept_none;
	}
	else
	{
		platform_log("kept BSPs: BSP %ld (%lu KB) copied back in %.1f ms (the RSX's copy %.1f, its check %.1f)%s", index,
			(unsigned long)(bsp->bytes >> 10), milliseconds(start, ps3_host_microseconds()),
			milliseconds(start, copied_at), milliseconds(copied_at, ps3_host_microseconds()),
			state == _kept_read ? ", to be swapped and checked" : "");
	}
	ps3_host_lock_leave(keep.lock);
	return state;
}

/* (the lock held) the level's entry of this BSP; -1 none */
static long level_find(long file_offset, long file_size, const void *address)
{
	long index;

	for (index = 0; index < keep.count; index++)
	{
		const struct kept_bsp *bsp = &keep.bsps[index];

		if (bsp->file_offset == file_offset && bsp->file_size == file_size &&
			bsp->base_address == (uint32_t)(uintptr_t)address)
			return index;
	}
	return -1;
}

/* cache_files.c, after the BSP's read and swap: the load's end for the reader; the scenario's BSPs, which it reads
ahead */
void bsp_keep_loaded(long file_offset, long file_size, void *address, const void *references, long count)
{
	const struct bsp_reference *reference = references;
	struct kept_bsp *bsp;
	long index;

	if (!keep.started)
		return;
	ps3_host_lock_enter(keep.lock);
	keep.loading = FALSE;
	if (keep.active && !keep.count && reference && count > 1 && count <= KEEP_ENTRIES)
	{
		for (index = 0; index < count; index++)
		{
			bsp = &keep.bsps[index];
			if (bsp->file_offset != reference[index].file_offset || bsp->file_size != reference[index].file_size ||
				bsp->base_address != reference[index].base_address)
			{
				if (bsp->busy)
					bsp->drop = TRUE;
				else
					entry_free(bsp);
				bsp->file_offset = reference[index].file_offset;
				bsp->file_size = reference[index].file_size;
				bsp->base_address = reference[index].base_address;
			}
			bsp->bytes = ((uint32_t)reference[index].file_size + SECTOR_BYTES - 1) & ~(SECTOR_BYTES - 1);
		}
		keep.count = count;
		if (!cache_file_open_map_path(keep.path))
			keep.path[0] = 0;
	}
	index = level_find(file_offset, file_size, address);
	if (index >= 0)
		keep.current = index;
	ps3_host_condition_broadcast(keep.wake);
	ps3_host_lock_leave(keep.lock);
}

/* cache_files.c, after the BSP's checks passed: its checked bytes kept (but those the checks were skipped for) */
void bsp_keep_checked(long file_offset, long file_size, void *address, int restored)
{
	struct kept_bsp *bsp;
	uint64_t start, sums[2] = { 0, 0 };
	uint32_t generation;
	long index;

	if (!keep.started || restored == _kept_checked)
		return;
	ps3_host_lock_enter(keep.lock);
	index = keep.active ? level_find(file_offset, file_size, address) : -1;
	bsp = index >= 0 ? &keep.bsps[index] : NULL;
	if (!bsp || bsp->busy || !d3d8_bsp_copies_available() || (!bsp->memory && !entry_allocate(bsp)))
	{
		ps3_host_lock_leave(keep.lock);
		return;
	}
	bsp->busy = TRUE;
	generation = keep.generation;
	ps3_host_lock_leave(keep.lock);

	start = ps3_host_microseconds();
	local_copy(bsp->memory, address, bsp->bytes, sums);

	ps3_host_lock_enter(keep.lock);
	bsp->busy = FALSE;
	if (bsp->drop || keep.generation != generation)
	{
		entry_free(bsp);
	}
	else
	{
		bsp->state = _kept_checked;
		bsp->sums[0] = sums[0];
		bsp->sums[1] = sums[1];
		platform_log("kept BSPs: BSP %ld (%lu KB) kept in %.1f ms; %.1f MB kept", index,
			(unsigned long)(bsp->bytes >> 10), milliseconds(start, ps3_host_microseconds()),
			kept_megabytes_tenths() / 10.0);
	}
	ps3_host_condition_broadcast(keep.wake);
	ps3_host_lock_leave(keep.lock);
}
