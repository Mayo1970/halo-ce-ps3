# PS3 port goals

This repository is a copy of `halo-ce-universal` at commit `e3389991`
("Online split screen"). The upstream folder
`E:\Users\Matteo\Desktop\Halo\halo-ce-universal` is the read-only reference.
The PS3 files are `Makefile`, `build.sh`, this file and `port/ps3/`
(refer to `port/ps3/README.md`). G1 and G2 also changed shared files
(`source/`, `port/linux/src`, `port/linux/include`, `port/include`,
`tools/`); the comments longer than two lines in each changed file were
shortened, and the decomp files' symbol tables now point to the upstream
copy.

Rules:

- Build only with `./build.sh` (Docker image `ps3dev-latest:latest`).
- Each goal ends with a test on a PS3. The next goal starts after the test
  is a SUCCESS.
- Do `./build.sh check` before each hardware test.

## Goal stack (proposed)

| Goal | Status |
| --- | --- |
| G0 Scaffolding and the platform layer: video, pads, audio, files, UDP log, XMB exit, memory probe | Done. Hardware SUCCESS 2026-10-05: 60 fps with paced flips, pad buttons and sticks, the 440 Hz tone with 0 partial blocks, a clean XMB exit. No listener ran at boot, so the free memory, the resolution and the map list are not measured yet. |
| G1 Data model: the game compiles and links for LP64 with the 32-bit layouts of its data | Done in G2. Steps 1 (pointer fields) and 2 (32-bit `long`) done 2026-10-05. Matt (2026-10-05): no G1 hardware test; G2 fixes the open items and links the game. |
| G2 The Xbox platform layer on PS3 (kernel, XAPI, files, threads, memory, from `port/linux/src`); the game runs headless to its first map load | Done. Hardware SUCCESS 2026-10-05 on the third run: the game runs headless to the open of `d:\maps\ui.map`, and the XMB exit is clean. See "G2 progress". |
| G3 Map data on a big-endian CPU: tag data swapped and its pointers relocated, checked headless | Not started |
| G4 Direct3D 8 on the RSX: the main menu shows | Not started |
| G5 XInput on the pads and DirectSound on the audio port: the menu operates, with sound | Not started |
| G6 First playable level (a10) | Not started |
| G7 Performance and hardening: memory budget, saved games on the HDD, network | Not started |

## G1 progress

Step 1, pointer fields (done 2026-10-05):

- `HALO_PTR32` (`__attribute__((mode(SI)))` in
  `port/ps3/include/halo_ps3_prefix.h`, empty on the other ports) keeps a
  pointer member in 4 bytes. It is on the pointer members of the tag types
  (`tag_groups.h`), the cache file structures, each struct with a layout
  assert, the types that they hold by value, and the tag headers
  (`*_definitions.*`, `*_group.h`): 393 members in 90 files. It is also on
  31 members of `port/include/xdk/xdk_pdb.h`, which `tools/xdk_headers.py`
  now writes.
- Never pass the address of a `HALO_PTR32` member where a full pointer is
  written (`T **`). 13 places did. They now write a full-size local and copy
  it (`ui_widget.c`, `object_types.c`, the Direct3D and DirectSound create
  and lock calls, `create_thread`, `create_mutex`). GCC shows such a place
  as an incompatible-pointer warning whose two types print the same.
- One exception: zlib's `z_stream` keeps 8-byte pointers (`zlib.h` stays as
  zlib wrote it), so the layout asserts of the map decompressor's runtime
  state are off on the PS3.

Step 2, 32-bit `long` (done 2026-10-05):

- `#define long int` in the PS3 prefix, after the system headers that game
  units include directly. The C runtime wrappers (`port/linux/include`,
  `port/ps3/include/limits.h`) undefine it while they include newlib, and
  `LONG_MAX`, `LONG_MIN` and `ULONG_MAX` get the 32-bit values.
- `__int64` and `__uint64` are `mode(DI)` typedefs. `unsigned __int64` is
  spelled `__uint64` (12 places, and `tools/pdb200_types.py`), and the
  `long long` members of `main.c` are `__int64`.
- `errors.c` gave a `long` to `time()`, which writes a 64-bit `time_t`. It
  is a `time_t` now.

Build: `./build.sh -j12 game` compiles all 476 units with no errors. The
flags are the GCC equivalents of the Linux build's ABI flags, and
`-fsigned-char` (`char` is unsigned on PowerPC).

The G1 open items, done in G2:

- Implicit declarations that cut a returned pointer: libtiff's units get
  the prototypes from `port/ps3/include/halo_ps3_libtiff.h` (forced, so
  libtiff's licensed headers stay as they are); `_wfdopen` and `_wfreopen`
  are declared in `port/linux/include/wchar.h`; `playlist_profile_get_options`
  in `ui_widget_event_handler_functions.c`; `fabsf` (a float function that
  newlib hides from the game's strict C89) in `port/linux/include/math.h`.
- Casts from a 32-bit integer to a pointer: only a signed source
  sign-extends. The 8 HaloScript string values (`hs.c`, `hs_runtime.c`)
  now go through `unsigned long`. The others have an unsigned source
  (`game_state_xbox.c`, `sound_dsound_xbox.c`, `stack_walk_windows.c`), or
  hold a small index or size (`cache_files_decompress_windows.c`,
  `lruv_cache.c`, `tif_packbits.c`), or are never used
  (`byte_swapping.c`'s external definitions).
- `stack_walk_windows.c` walks the PPC64 back chain on the PS3.
- The link: G2.

Found for later goals, not changed: byte order in `cache_files.c` (the tag
signature bytes), `render_contrails.c` (the alpha byte) and
`ui_widget_event_handler_functions.c` (a `word` written where a `long`
is read: `playlist_profiles_enumerate_available_to_local_player_index`).

## G2 progress

Done 2026-10-05: hardware SUCCESS on the third run.

- The link: all 476 game units, the PS3 Xbox layer (`port/ps3/xbox`), the
  Linux units that need no host change (`bink_null.c`, `d3d8_resources.c`,
  `halo_linker_common.c`, `msvc_crt.c`, `msvc_wide.c`, `port_config.c`,
  `xbdm.c`, `xbox_xapi.c`), tomlc17 and musl's maths. `check`: 0 `r2off`
  stubs, `.got` 0x27e0. The game units need 137 KB of TOC, so they compile
  with `-mminimal-toc`. Loaded segments: 6.5 MB of code, 9.9 MB of data and
  `.bss`.
- Two compile classes. The host layer (`port/ps3/src`) is LP64 with
  newlib's own `long`. The Xbox layer has the game's ABI and the XDK
  declarations, with `port/ps3/xbox/ps3_platform.h` in place of
  `port/linux/src/platform.h`. Between them, `port/ps3/src/ps3_host.h`
  has only fixed-width types.
- Memory: lv2's mmapper reserves the Xbox window at `0x80000000` (an
  alignment of `0x80000000`, else 256 MB areas until one lands there), and
  maps each 1 MB page while a block uses it. The game state and the tag
  cache get their Xbox addresses. If the hardware confirms the window, the
  pointers in the tag data stay valid, and G3 only swaps byte order
  (blocker 3).
- Kernel: lv2 mutexes and condition variables (one kernel lock for all
  waits), detached PPU threads (512 KB stacks at least), APCs for
  `ReadFileEx`, a table of thread states in place of TLS (PSL1GHT sets up
  no TLS). PSL1GHT's `libpthread` is not used: it needs `pthread_init`.
- Files: lv2 file calls (thread-safe, unlike newlib's single `errno`).
  `d:\` is the data root, the other drives are folders of
  `/dev_hdd0/data/HaloCE`.
- Headless: Direct3D creates its resources in the window but draws
  nothing; DirectSound gives each packet back as played; XInput has no
  devices; Winsock has no network. A thread keeps the RSX flipping for the
  XMB, in a colour for the state: blue running, green at the G2 stop, red
  at a halt (`halt_and_catch_fire`).
- The G2 stop: the first map file the game opens (`d:\maps\ui.map`, from
  `cache_files_precache_map_begin`). The log gets its size, its header
  bytes, the free memory, then the game waits for the XMB exit
  (`HALO_PS3_STOP_AT_FIRST_MAP` in the Makefile; G3 removes it).
- `file_reference_info.file_handle` (`files.h`) is `HALO_PTR32`: with an
  8-byte pointer the info overflowed the 268 bytes of a `file_reference`.
- First run on the PS3 (2026-10-05): lv2 gave the window at `0x80000000`
  at the first try (blocker 3 solved), with 188 MB free at boot and 179 MB
  after the RSX init. The map cache files are sparse (5 to 35 ms each).
  The game halted in `cache_copy_initialize`: `LARGE_INTEGER`'s halves had
  the little-endian order. `xdk_pdb.h` now puts the high half first on a
  big-endian target. A halt now waits on the red screen for the XMB exit,
  where it asserted again and exited before.
- Second run on the PS3 (2026-10-05): the game passed the cache files, the
  saved game file, XInput and DirectSound. It halted in
  `stack_memory_pool_reset`, which reads the widget pool as a `long` array
  with the Xbox layout. The pool and block pointers are now `HALO_PTR32`
  (`stack_memory_pool.c`, `ui_widget.c`), with layout checks.
- Third run on the PS3 (2026-10-05): SUCCESS. The game opened
  `d:\maps\ui.map` (14145536 bytes, version 5, build "01.10.12.2276") and
  stopped on the green screen. 94 MB were free after the game's start-up
  (179 MB after the RSX init). The XMB exit was clean.
- Still a risk: the game state pools may outgrow 15.75 MB with 8-byte
  pointers (an assert in `game_state_malloc`). The audit found the budget
  is 0xFC0000, against 0x305000 on the Xbox.

## Blockers found in the analysis

1. **ILP32 data on an LP64 compiler.** The game's data (tags, cache files,
   saved games, Direct3D resources) holds 32-bit pointers, and its code uses
   `long` as 32 bits. Linux and Windows build 32-bit x86 code; Android builds
   ILP32 code (`arm64_32`). PSL1GHT's ppu-gcc is LP64 only: `void*` and
   `long` are 8 bytes. `-m32` code is not an option: lv2 runs processes in
   64-bit mode, where `-m32` code computes carries and address wrap-around
   incorrectly. Decision (Matt, 2026-10-05): keep LP64, pointer fields
   first (`mode(SI)`), then `long` as a 32-bit type with a prefix-header
   macro (see "G1 progress"). Size of the work in `source/` (806 files):
   14672 `long`, 592 `sizeof` layout asserts (the check list), 321
   `address` field uses, 1358 `TAG_BLOCK_GET_ELEMENT` uses (one macro).
   - Pointer fields: ppu-gcc 7.2 accepts `__attribute__((mode(SI)))` on a
     pointer type (tested 2026-10-05). The field is 4 bytes, `lwz` loads it
     zero-extended, `stw` stores it, with no warnings with `-Wall`. Thus
     only the declarations change; the code that uses the fields stays.
   - `long` with a macro (`#define long int` after the C runtime headers):
     the sources have 7 `long long`, 0 `long double`, 0 `long int`, 153
     integer constants with an `L` suffix (64-bit constants: examine
     each), and 108 `%l` formats (only the output changes).
2. **Little-endian map data.** Cache files, tag data, vertex and index
   data, bitmaps and sounds are little-endian. Only 7 files in `source/`
   define tag fields or byte-swap codes, so a swap needs external tag
   definitions. Choice for G3: convert the maps once on the PC, or swap
   them at load time.
3. **Fixed Xbox addresses.** The tag cache is at `0x803A6000` and the game
   state at `0x81A00000` (`source/cache/physical_memory_map.c`). Solved on
   the PS3 (2026-10-05): lv2's mmapper gives the window at `0x80000000`,
   so the pointers in the tag data stay valid (see "G2 progress").
4. **Runtime shaders.** The Linux renderer (`port/linux/src/d3d8_gl.c`)
   translates the NV2A vertex shaders and register combiners to GLSL at run
   time. The PS3 has no shader compiler at run time: G4 must generate RSX
   microcode directly, or use shaders compiled offline with `cgcomp`.
5. **Memory.** The Xbox window is 128 MB on Linux (tag cache 22 MB, game
   state 16 MB, texture cache 22 MB, sound cache 4 MB, and more). The free
   main memory on the PS3 is approximately 145 to 200 MB, by firmware and
   EBOOT size; G0 measures it. The texture cache can go in the 256 MB of
   RSX memory.
