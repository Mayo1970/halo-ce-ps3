/* XPhysicalAlloc on lv2: the mmapper reserves the Xbox window at 0x80000000 (the maps point into it)
and maps each 1 MB page while a block uses it; protections are only recorded. */

#include <string.h>

#define PAGE_SIZE_BYTES 0x1000UL
#define CONTIGUOUS_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / PAGE_SIZE_BYTES)
#define CHUNK_COUNT (PLATFORM_CONTIGUOUS_SIZE / PS3_HOST_WINDOW_PAGE)
#define PAGES_PER_CHUNK (PS3_HOST_WINDOW_PAGE / PAGE_SIZE_BYTES)

/* per page: 0 free, else the protection of its block (PAGE_*); the first page of a block
also records the block's length */
static DWORD page_protection[CONTIGUOUS_PAGE_COUNT];
static unsigned long block_page_count[CONTIGUOUS_PAGE_COUNT];
/* per 1 MB chunk: the pages in use and the lv2 memory mapped there */
static unsigned long chunk_pages[CHUNK_COUNT];
static uint32_t chunk_memory[CHUNK_COUNT];
/* the chunks mapped now and at most, for the [mem] heartbeat (ps3_main.c) */
static uint32_t mapped_chunks, mapped_chunks_peak;
static BOOL arena_reserved = FALSE;
static struct ps3_host_lock *arena_lock;

/* before the game runs (ps3_main.c) */
BOOL platform_memory_initialize(void)
{
	int result;

	arena_lock = ps3_host_lock_create(FALSE);
	result = ps3_host_window_reserve(PLATFORM_CONTIGUOUS_BASE);
	arena_reserved = arena_lock && result == 0;
	platform_log("Xbox memory window at 0x%08lx: %s (%d)", PLATFORM_CONTIGUOUS_BASE,
		arena_reserved ? "reserved" : "NOT available", result);
	return arena_reserved;
}

BOOL platform_is_contiguous(const void *address)
{
	__uint64 value = (__uint64)address;

	return value >= PLATFORM_CONTIGUOUS_BASE && value - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static BOOL pages_free(unsigned long first, unsigned long count)
{
	unsigned long page;

	if (first + count > CONTIGUOUS_PAGE_COUNT)
		return FALSE;
	for (page = first; page < first + count; page++)
	{
		if (page_protection[page])
			return FALSE;
	}
	return TRUE;
}

static unsigned long chunk_address(unsigned long chunk)
{
	return PLATFORM_CONTIGUOUS_BASE + chunk * PS3_HOST_WINDOW_PAGE;
}

/* adds count pages from first to the use counts of their chunks, mapping the chunks that
had none; FALSE (and nothing changed) if lv2 has no memory */
static BOOL chunks_use(unsigned long first, unsigned long count)
{
	unsigned long first_chunk = first / PAGES_PER_CHUNK;
	unsigned long last_chunk = (first + count - 1) / PAGES_PER_CHUNK;
	unsigned long chunk;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++)
	{
		if (!chunk_pages[chunk] && ps3_host_window_map(chunk_address(chunk), PS3_HOST_WINDOW_PAGE, &chunk_memory[chunk]) != 0)
		{
			while (chunk-- > first_chunk)
			{
				if (!chunk_pages[chunk])
				{
					ps3_host_window_unmap(chunk_address(chunk), chunk_memory[chunk]);
					mapped_chunks--;
				}
			}
			return FALSE;
		}
		if (!chunk_pages[chunk] && ++mapped_chunks > mapped_chunks_peak)
			mapped_chunks_peak = mapped_chunks;
	}
	for (chunk = first_chunk; chunk <= last_chunk; chunk++)
	{
		unsigned long start = first > chunk * PAGES_PER_CHUNK ? first : chunk * PAGES_PER_CHUNK;
		unsigned long end = first + count < (chunk + 1) * PAGES_PER_CHUNK ? first + count : (chunk + 1) * PAGES_PER_CHUNK;

		chunk_pages[chunk] += end - start;
	}
	return TRUE;
}

static void chunks_release(unsigned long first, unsigned long count)
{
	unsigned long first_chunk = first / PAGES_PER_CHUNK;
	unsigned long last_chunk = (first + count - 1) / PAGES_PER_CHUNK;
	unsigned long chunk;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++)
	{
		unsigned long start = first > chunk * PAGES_PER_CHUNK ? first : chunk * PAGES_PER_CHUNK;
		unsigned long end = first + count < (chunk + 1) * PAGES_PER_CHUNK ? first + count : (chunk + 1) * PAGES_PER_CHUNK;

		chunk_pages[chunk] -= end - start;
		if (!chunk_pages[chunk])
		{
			ps3_host_window_unmap(chunk_address(chunk), chunk_memory[chunk]);
			mapped_chunks--;
		}
	}
}

/* chunks are 1 MB (PS3_HOST_WINDOW_PAGE); read without the lock, as a log line can be a little late */
void platform_memory_usage(uint32_t *mapped_mb, uint32_t *peak_mb)
{
	*mapped_mb = mapped_chunks;
	*peak_mb = mapped_chunks_peak;
}

void *platform_contiguous_alloc(unsigned long size, unsigned long alignment,
	unsigned long physical_address, DWORD protect)
{
	unsigned long count = (size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
	unsigned long alignment_pages = alignment > PAGE_SIZE_BYTES ? alignment / PAGE_SIZE_BYTES : 1;
	unsigned long first = CONTIGUOUS_PAGE_COUNT;
	unsigned long page;
	void *address;

	if (!count)
		count = 1;
	protect &= ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
	if (!protect)
		protect = PAGE_READWRITE;

	if (!arena_reserved)
		return NULL;
	ps3_host_lock_enter(arena_lock);
	if (physical_address != PLATFORM_ANY_PHYSICAL_ADDRESS)
	{
		unsigned long wanted = (physical_address & ~PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;

		if (pages_free(wanted, count))
			first = wanted;
	}
	else if (count <= CONTIGUOUS_PAGE_COUNT)
	{
		/* top-down first fit, like the Xbox contiguous allocator */
		unsigned long candidate = CONTIGUOUS_PAGE_COUNT - count;

		for (;;)
		{
			candidate -= candidate % alignment_pages;
			if (pages_free(candidate, count))
			{
				first = candidate;
				break;
			}
			if (candidate == 0)
				break;
			candidate--;
		}
	}
	if (first == CONTIGUOUS_PAGE_COUNT || !chunks_use(first, count))
	{
		ps3_host_lock_leave(arena_lock);
		return NULL;
	}
	for (page = first; page < first + count; page++)
		page_protection[page] = protect;
	block_page_count[first] = count;
	ps3_host_lock_leave(arena_lock);

	address = (void *)(PLATFORM_CONTIGUOUS_BASE + first * PAGE_SIZE_BYTES);
	/* fresh memory reads as zero, as Linux's new pages do */
	memset(address, 0, count * PAGE_SIZE_BYTES);
	return address;
}

void platform_contiguous_free(void *address)
{
	unsigned long first, count, page;

	if (!platform_is_contiguous(address))
		return;
	first = ((unsigned long)(__uint64)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
	ps3_host_lock_enter(arena_lock);
	count = block_page_count[first];
	if (count)
	{
		for (page = first; page < first + count; page++)
			page_protection[page] = 0;
		block_page_count[first] = 0;
		chunks_release(first, count);
	}
	ps3_host_lock_leave(arena_lock);
}

/* ---------- XAPI */

LPVOID WINAPI XPhysicalAlloc(SIZE_T size, ULONG_PTR physical_address, ULONG_PTR alignment, DWORD protect)
{
	/* an address inside the window places the block there: the game state and tag cache */
	void *result = platform_contiguous_alloc(size, alignment,
		physical_address < PLATFORM_CONTIGUOUS_SIZE ? physical_address : PLATFORM_ANY_PHYSICAL_ADDRESS,
		protect);

	platform_log("XPhysicalAlloc %lu KB (address 0x%08lx): %p", (unsigned long)size >> 10,
		(unsigned long)physical_address, result);
	if (!result)
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return result;
}

VOID WINAPI XPhysicalFree(LPVOID address)
{
	platform_contiguous_free(address);
}

BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD new_protect, PDWORD old_protect)
{
	unsigned long start = (unsigned long)(__uint64)address & ~(PAGE_SIZE_BYTES - 1);
	unsigned long end = ((unsigned long)(__uint64)address + size + PAGE_SIZE_BYTES - 1) & ~(PAGE_SIZE_BYTES - 1);

	if (old_protect)
		*old_protect = platform_is_contiguous(address) ?
			page_protection[(start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES] : PAGE_READWRITE;
	if (platform_is_contiguous(address))
	{
		unsigned long page;

		ps3_host_lock_enter(arena_lock);
		for (page = (start - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES;
			page < (end - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES && page < CONTIGUOUS_PAGE_COUNT;
			page++)
		{
			if (page_protection[page])
				page_protection[page] = new_protect & ~(PAGE_WRITECOMBINE | PAGE_NOCACHE);
		}
		ps3_host_lock_leave(arena_lock);
	}
	return TRUE;
}

VOID WINAPI XPhysicalProtect(LPVOID address, SIZE_T size, DWORD new_protect)
{
	VirtualProtect(address, size, new_protect, NULL);
}

DWORD WINAPI XQueryMemoryProtect(LPVOID address)
{
	DWORD protect = PAGE_READWRITE;

	if (platform_is_contiguous(address))
	{
		ps3_host_lock_enter(arena_lock);
		protect = page_protection[((unsigned long)(__uint64)address - PLATFORM_CONTIGUOUS_BASE) / PAGE_SIZE_BYTES];
		ps3_host_lock_leave(arena_lock);
		if (!protect)
			protect = PAGE_NOACCESS;
	}
	return protect;
}
