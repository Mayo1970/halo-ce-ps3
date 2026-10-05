# Roadmap

This file is the handoff of the Halo CE PS3 port from one session to the next.

- At the start of a session, read this file. Then read the part of `GOALS.md`
  for the current goal.
- At the end of a session, update "Now", the goal table and "Log".
- Do not use files from the scratchpad of an earlier session. That folder
  belongs to its session.

## Project

- The port: the Halo CE decompilation (`halo-ce-universal`) on the PS3, with
  PSL1GHT.
- This repository: `E:\Users\Matteo\Desktop\Halo\PS3`. It is a copy of
  `halo-ce-universal` at commit `e3389991` ("Online split screen").
- Upstream: `E:\Users\Matteo\Desktop\Halo\halo-ce-universal`. It is the
  read-only reference. Do not change it.
- Git: on 2026-10-05 at 00:42, something other than Claude replaced `.git`
  with an empty `git init`. Matt then committed G1 to G3 (`4b4874c`,
  `68a0df9`). The `.gitignore` rule `xbox/` (upstream's, for the Xbox SDK)
  also ignores `port/ps3/xbox/`: those files are in no commit. To see the
  PS3 changes, compare with upstream and ignore CRLF (upstream is CRLF, this
  copy is LF):
  `diff -rq --strip-trailing-cr -x .git -x build -x __pycache__ ../halo-ce-universal .`
  (Git Bash, approximately 25 s).
- The other documents:
  - `GOALS.md`: the goal stack, the G1 step details, the blocker analysis.
  - `port/ps3/README.md`: build, install, game data, log, bring-up test.

## Rules

1. Do only what Matt asks. Take no initiative. Reply short and direct. No
   emojis.
2. Build only with `./build.sh`. It runs `make` in the Docker image
   `ps3dev-latest:latest` (ppu-gcc 7.2, PSL1GHT, LP64).
3. Matt tests on his PS3. Never use RPCS3.
4. Each goal ends with a test on the PS3. Start the next goal only after
   Matt reports SUCCESS.
5. Do `./build.sh check` before each hardware test.
6. Before Matt boots, look at UDP port 18194 again (see "Hardware test").
7. Comments: only for logic that is not obvious. At most 2 lines each, and
   keep the "why". When you edit a file, shorten its longer comments too.
8. Commits and PRs: no co-author line, no AI attribution.

## Now (2026-10-05, end of session)

- G0 to G3 are done: hardware SUCCESS. G2 ran the game headless to the open
  of `d:\maps\ui.map`; the Xbox window is at 0x80000000, so the map pointers
  stay valid. G3 (run 9) ran the main menu headless at 30 frames per second
  on the swapped `ui.map`. See "G3 summary".
- G4 is done: hardware SUCCESS on run 5 (2026-10-05). The main menu draws
  on the RSX, 4:3 in the middle of the display, at 30 frames per second:
  about 120 draws a frame, 0 skipped, 13 vertex programs and 15 pixel
  shaders sent a frame. Matt chose runtime microcode. See "G4 summary" and
  GOALS.md ("G4 progress").
- G5 is done: the pads work (run 1, hardware SUCCESS, Matt). The menu
  moves with the pad; a profile was made and a10 loaded and ran, at about
  9 to 10 frames per second in the level. See GOALS.md ("G5 progress").
- G6 started (Matt, "G6. Go for the sound."). Run 1: the sound works
  (hardware SUCCESS, Matt): DirectSound on the PS3 audio port
  (`port/ps3/xbox/dsound_ps3.c`), in the menu and in a10, with 0 underruns.
  See GOALS.md ("G6 progress").
- Diagnostics in the build, from G3 (ask Matt before removing them):
  - `PS3_MAIN_STEP(n)` sets the main loop's place: `main.c` (`main_loop`
    1-70, `main_menu_load` 100-106, `main_game_render` 501-503 and
    540-549, `set_window_camera_values` 560-568 and 5611-5613),
    `sound_manager.c` 510-519, `render.c` 520-530, `player_effects.c`
    570-576.
  - One log line each at the first call of `set_window_camera_values` and
    `player_effect_get_camera_effect_matrix` (`ps3: ...`).
  - Every 10 s (`ps3_main.c` display thread, `platform_heartbeat` in
    `xbox_kernel.c`): the frames, the main loop pass and step, each
    thread's last wait site; `[host] thread ... blocked` for lock and
    condition waits over 2 s (`ps3_host.c`). At the first stall:
    `[ps3] main thread: priority ...` (an error code if lv2 ended it).
  - From G5: `XInput: port M: <buttons>` at each change of the buttons that
    the game gets (`xinput_ps3.c`).
  - From G6: `DirectSound:` every 10 s (`dsound_ps3.c`): the voices that
    play, the packets decoded, the mixer's time per chunk, the port blocks
    played silent or partial.

## Goal stack

| Goal | Status |
| --- | --- |
| G0 Scaffolding and the platform layer: video, pads, audio, files, UDP log, XMB exit, memory probe | Done 2026-10-05 |
| G1 Data model: the game compiles and links for LP64 with the 32-bit layouts of its data | Done in G2 (Matt, 2026-10-05) |
| G2 The Xbox platform layer on the PS3 (kernel, XAPI, files, threads, memory, from `port/linux/src`). The game runs headless to its first map load. | Done 2026-10-05 (hardware SUCCESS, third run) |
| G3 Map data on a big-endian CPU: tag data swapped and its pointers relocated, checked headless | Done 2026-10-05 (hardware SUCCESS, run 9, after the audit fixes) |
| G4 Direct3D 8 on the RSX: the main menu shows | Done 2026-10-05 (hardware SUCCESS, run 5) |
| G5 XInput on the pads and DirectSound on the audio port: the menu operates, with sound | Done 2026-10-05: the pads (run 1, hardware SUCCESS); Matt moved the sound to G6 |
| G6 First playable level (a10) | In progress: the sound works (run 1, hardware SUCCESS) |
| G7 Performance and hardening: memory budget, saved games on the HDD, network | Not started |

## G4 summary

`GOALS.md` ("G4 progress") has the details.

- Shaders: the game's NV2A vertex programs and pixel shaders become RSX
  microcode at run time (`port/ps3/src/ps3_rsx_vp.c`, `ps3_rsx_fp.c`,
  `ps3_rsx_asm.c`), cached per vertex shader and per pixel shader key. The
  encoding is cgcomp's, word for word: `tools/ps3_rsx_shader_check.sh`
  compares with `cgcomp -a` and with the NV2A's results (0 failures).
- The RSX back end (`port/ps3/src/ps3_gpu.c`, API `ps3_gpu.h`): the game's
  thread takes the RSX from the display thread at device creation (the
  display thread keeps the heartbeat; at a halt it takes the RSX back for
  the red screen). A 2 x 16 MB scratch in RSX memory holds each frame's
  vertices, indices and fragment program copies; backend label 64 fences
  it. Present draws the 640x480 back buffer 4:3 into the display buffer and
  flips; it also does the XMB exit.
- The device (`port/ps3/xbox/d3d8_rsx.c`, from `d3d8_gl.c`): render targets
  are linear A8R8G8B8 and Z24S8 RSX surfaces; textures are RSX copies (DXT
  as they are, the rest A8R8G8B8 in the same texel order), checked once a
  frame by a hash of their texels; vertices are converted to float4 per
  draw; the RSX's winding is Direct3D's (the picture is not mirrored).
- G4 hardware runs 1 to 4 (2026-10-05): black. Diagnostic builds (test
  shaders, pixels read back) proved the present, the geometry, the
  textures (DXT included) and the translated pixel shaders. Two faults:
  the front face was inverted (every visible triangle culled), and a depth
  clear of 1.0 wrote 0 (1.0 * 16777215 + 0.5 is 2^24 in a float). Both are
  fixed; run 5: SUCCESS.
- Not yet: visibility tests (0), render targets sampled by mip level (level
  0 only), point sprites, a cache of converted vertex buffers.

## G3 summary

`GOALS.md` ("G3 progress") has the details.

- The swap: `port/ps3/xbox/xbox_tag_swap.c` swaps in place, on each read:
  the map header (`cache_file_header_verify`, only if it is little-endian,
  so a second call does nothing), the tag data (`scenario_tags_load`) and
  each structure BSP (`scenario_structure_bsp_load`). The cache file on the
  HDD keeps little-endian data and gets a big-endian header.
- The layouts: `tools/ps3_tag_swap.py` writes
  `port/ps3/xbox/xbox_tag_swap_tables.h` from Invader's JSON definitions
  (361 structs, 82 groups). The tool has the Xbox overrides: the kind of
  each data field, MCC-only fields as padding, fields that the game reads
  narrower (`FIELD_UNITS`). Special data: model vertices and strips (Xbox
  model parts), BSP vertices, script nodes, animations (also compressed),
  recorded animation streams, UTF-16 strings, PVS bits.
- The PC check: `tools/ps3_tag_swap_check.sh <maps folder>` builds the same
  C code with the image's host GCC and swaps every map and BSP. All 24 maps
  pass: 0 errors, no byte swapped twice; the bytes not reached are names
  and padding.
- Game fixes for the big-endian CPU: `struct data_array` (`HALO_PTR32`,
  0x38), the HaloScript value views (`hs.h` `HS_LOW_*`/`HS_SLOT_*`: a
  boolean or short in a long slot is its low bytes), `hs_stack_frame`
  (0x10), the string external global, the recorded animation header
  bitfield and debug table, `ui_widget_event_handler_functions.c` (a word
  count), `material_effects.c`, and 4 narrow views of tag fields (fog
  flags twice, a runtime decal index, vehicle flags).
- The G3 stop (`HALO_PS3_STOP_AFTER_MAIN_MENU`) and the G2 stop are gone.
- Hardware fixes (runs 7 and 8): `global_screenshot_count` is 8-aligned on
  the PS3 (`main.c`); the text parameters' copy in
  `rasterizer_xbox_text.c` has `HALO_PTR32` pointers.

## Audit fixes (2026-10-05)

- Layouts: `hs_function_definition` is 0x1C again (`HALO_PTR32`), so
  script functions with 2+ parameters read their types (all scripts,
  `ring_loop` on the menu). `single_player_level_entry.map_name`,
  `plasma_runtime_parameters` and the camera and game start request copies
  match their creators. `zlib/trees.c` indexes `static_tree_desc`.
- Misaligned floats: the script stack keeps its frames and slots 4-aligned
  on the PS3 (worst shipped script: 457 of 512 bytes). Compressed
  animation translations and scales are copied to aligned locals.
- Byte order: CPU texel reads and writes use little-endian access
  (`TEXEL16`/`TEXEL32`), so all texture memory keeps the Xbox order (G4
  converts it on upload); vertex data stays big-endian. Also fixed: the AI
  vocalization short, contrail alpha, tag text, Winsock order, the network
  message size, the message header bitfield, the loading TGA.
- Runtime: the game's `_vsnprintf`/`_snprintf` return -1 when the text does
  not fit (MSVC); `fast_ftol_C` gives x87's 0x80000000 out of range;
  `menu_tags.c` scans `%d`; the texture cache debug list sorts pointers.
- Threads: the vblank thread runs at 1000 and the XMB monitor at 1000
  (above the game's 1001); empty spins call `SwitchToThread`; cache request
  and completion flags have barriers; thread names and ids match.

## G2 summary

`GOALS.md` ("G2 progress") has the details.

- The G1 items are done: the prototypes that cut a returned pointer are
  added, and the 8 HaloScript string casts zero-extend. The other int to
  pointer casts were checked: an unsigned source, or a small index.
- Two compile classes: the host layer (`port/ps3/src`, LP64 newlib) and the
  Xbox layer (`port/ps3/xbox` and 8 units of `port/linux/src`, the game's
  ABI). `port/ps3/src/ps3_host.h` is the boundary, fixed-width types only.
- The Xbox window at 0x80000000 comes from lv2's mmapper. The first run
  proved it on hardware.
- Headless: Direct3D, DirectSound, XInput and Winsock do nothing yet. A
  display thread flips the RSX for the XMB: blue running, green at the G2
  stop, red at a halt.
- The G2 stop was `HALO_PS3_STOP_AT_FIRST_MAP` (`xbox_files.c`). G3
  removed it.

## Found for later goals

- G4 pixel data and G5 sound data stay little-endian in the cache file:
  the texture and sound caches read them by file offset. CPU-written texels
  are little-endian too (trap 27). Not converted yet (tool or later code):
  `bitmap_utilities.c`, `bitmap_drawing.c`, `bitmap_extract.c`, the TIFF,
  TGA and quantize importers, `text_hires.c` (a stub on the PS3), a real
  Bink decoder. `string_to_tag` (`cseries.c`, no callers) reads a byte view.
- G3 run 9 logged at 2 s: `local player 0, weapon (0x0), deleted
  unexpectedly` (`first_person_weapons.c`; it resets the slot; not checked
  against the Xbox).
- G7: `network_distributed.c` `distributed_address_text` assumes a
  little-endian network order; it prints right today only because the PS3
  `p2p_peer_endpoint_address` stub is an identity. TEA in
  `message_encryption.c` runs on native-order words (no callers).
- zlib's `z_stream` keeps 8-byte pointers (`zlib.h` stays as zlib wrote it).
  So the layout asserts of the map decompressor's runtime state are off on
  the PS3 (`#ifndef HALO_PS3` in `cache_files_decompress_windows.c`).
- From the G2 audits (2026-10-05):
  - G7: `data_encoding.c` swaps every packet, so the PS3 wire order is not
    the PC's.
  - Winsock: `XNetGetTitleXnAddr` gives `XNET_GET_XNADDR_NONE`, so
    `transport_initialize` fails, and a `telnet_console = true` asserts.

## Decisions

- LP64 stays. `-m32` code is not an option: lv2 runs processes in 64-bit
  mode, where `-m32` code computes carries and address wrap-around
  incorrectly.
- Matt (2026-10-05, "Do 1, then 2"): pointer members first, with
  `HALO_PTR32`. Then `long` as a 32-bit type, with a macro in the prefix
  header, not a source rewrite.
- Matt (2026-10-04): controllers, file reading, RSX init and the other
  platform parts are not separate goals. Copy them from the sibling ports
  (see "Reference ports").
- Matt (2026-10-05), G3: swap the map data at load time on the PS3 (the
  Xbox maps stay as they are), with Invader's tag definitions. Invader is
  GPL-3.0-only, so `xbox_tag_swap_tables.h` and the EBOOT are GPLv3
  (upstream is CC0). The maps of his disc are in
  `E:\Users\Matteo\Downloads\[XBOX] HALO Combat Evolved\HALO_Combat_Evolved\maps`.
- Matt (2026-10-05), G4: runtime microcode. C emitters translate the NV2A
  vertex programs and the combiner state to RSX microcode at run time,
  cached per key. The encoders follow PSL1GHT's cgcomp source
  (`E:\Users\Matteo\Desktop\Dreamcast\PSL1GHT\tools\cgcomp`); the PC check
  compares with `cgcomp -a`.

## How the game units compile

- `./build.sh game` compiles the units in `port/linux/port.json` (the list of
  the Linux build). `tools/ps3_game_mk.py` writes `build/ps3/game.mk` from
  it. They compile with `-mminimal-toc`: they need 137 KB of TOC.
- Per-unit flags are target-specific variables in the `Makefile`
  (`GAME_UNIT_FLAGS`): `shell_xbox.c` renames `main` to `halo_xbox_main`,
  and libtiff's units get `port/ps3/include/halo_ps3_libtiff.h`.
- Each unit gets two forced includes:
  - `port/ps3/include/halo_ps3_prefix.h`.
  - `build/ps3/halo_msvc_semantics.h`, from `tools/ps3_msvc_semantics.py`.
    It wraps `tools/linux_msvc_semantics.py` and removes the names of static
    inline functions from `#pragma weak`.
- `halo_ps3_prefix.h`:
  - It includes `stddef.h`, `stdarg.h`, `stdint.h`, `sys/types.h` and
    `time.h` first. They must come before `#define long int`, and
    `stddef.h` must come before `_WCHAR_T_DEFINED`.
  - `#define long int`: `long` is 32 bits in game code.
  - `#define u_long halo_u_long`: the 64-bit `u_long` of newlib conflicts
    with Winsock and libtiff.
  - `__int64` and `__uint64` are `mode(DI)` typedefs. Write `__uint64`, not
    `unsigned __int64`.
  - `HALO_PTR32` is `__attribute__((mode(SI)))`, a 4-byte pointer member.
    Loads zero-extend it. It is empty on the other ports.
  - `__stdcall`, `__cdecl` and `__fastcall` are empty. `__declspec(x)` maps
    to GCC attributes.
  - Then it includes `port/linux/include/halo_linux_prefix.h`.
- The C runtime wrappers in `port/linux/include/` (`stdio.h`, `stdlib.h`,
  `string.h`, `math.h`, `fcntl.h`, `float.h`, `io.h`, `StdDef.h`) and
  `port/ps3/include/limits.h` push the `long` macro, undefine it,
  `#include_next` the newlib header, and pop the macro. `limits.h` gives
  `LONG_MAX`, `LONG_MIN` and `ULONG_MAX` the 32-bit values.
- The flags (`GAME_CFLAGS` in `Makefile`) are the GCC versions of the MSVC
  ABI flags of the Linux build, and `-fsigned-char` (`char` is unsigned on
  PowerPC).
- `HALO_PTR32` is on: the pointer members of the tag types
  (`source/tag_files/tag_groups.h`), the cache file structures, each struct
  with a layout assert, the types that they hold by value, the tag headers
  (`*_definitions.*`, `*_group.h`), and 31 members of
  `port/include/xdk/xdk_pdb.h` (written by `tools/xdk_headers.py`). To list
  the files: `grep -rl HALO_PTR32 source port`.

## Traps

1. Never give the address of a `HALO_PTR32` member where a full pointer is
   written (`T **`). The callee writes 8 bytes into 4. Write a full-size
   local, then copy it to the member. GCC shows this as an incompatible-pointer
   warning whose two types print the same.
2. A cast from a 32-bit integer to a pointer sign-extends. Addresses at or
   above 0x80000000 break.
3. An implicit declaration returns `int`. A pointer result loses its top 32
   bits.
4. Do not put `mode(DI)` in a macro. `int __attribute__((mode(DI))) *p` makes
   a 64-bit pointer to a 32-bit `int`, with no error. Use the typedefs.
5. A C runtime header that a game unit includes after the prefix sees `long`
   as `int`. Include it in the prefix before the macro, or give it a wrapper.
6. The Docker bind mount ignores case: `StdDef.h` matches `stddef.h`. A
   wrapper must use `#include_next`, or it includes itself.
7. The decompiled files have `#line` directives, so GCC gives incorrect line
   numbers. For a warning build, remove them in a copy (see "Commands").
8. Use `-O2` only. `-O3` broke boot on this toolchain (ioQuake3-PS3).
9. A TOC over 64 KB makes `r2off` stubs, and such an EBOOT hangs at boot.
   `-Wl,--no-multi-toc` makes it a link error, and `check` looks for them.
10. The hardware-confirmed PSL1GHT traps of the sibling ports are already in
    `port/ps3/src`: `sin_len` on UDP addresses, no `netDeinitialize`, a
    64-bit `setjmp` (newlib's overflows `jmp_buf`), a monitor thread that
    pumps `sysUtilCheckCallback`, the free memory from the container probe
    (never malloc until failure), WaitFlip before SetFlip. The full lists are
    in the `AGENTS.md` files of those ports.
11. `long long` does not compile after `#define long int` (`int int`).
    Write `__int64` or `__uint64` in the game and in the Xbox layer.
12. A `HALO_PTR32` member that holds `INVALID_HANDLE_VALUE` loads back as
    0xFFFFFFFF, not -1. A comparison with `INVALID_HANDLE_VALUE` fails.
    `platform_handle_get` rejects such handles.
13. A struct with a pointer that lives in fixed bytes (a `file_reference`
    holds a `file_reference_info`) overflows with 8-byte pointers. Give the
    pointer `HALO_PTR32`, and add a size check.
14. The game's strict C89 (`__STRICT_ANSI__`) hides newlib's C99 float
    functions. Undeclared, GCC calls `fabsf` as an `int` function. Declare
    them (`port/linux/include/math.h`).
15. A container memory probe (`ps3_memory_free_mb`) reserves memory while
    it runs. Another thread that allocates then can hang lv2. Probe only
    when no other thread allocates.
16. No TLS in PSL1GHT's runtime, and newlib has one `errno` for all threads.
    The Xbox layer keeps a table of thread states, and uses lv2 file calls.
17. A union of a 64-bit value and two 32-bit halves (`LARGE_INTEGER`) needs
    the high half first on the PS3. `tools/xdk_headers.py` writes
    `xdk_pdb.h` that way (`WORD_ORDER_UNIONS`).
18. The XDK headers are a system folder (`-idirafter`): the depfiles leave
    them out. The `Makefile` makes every game and Xbox layer object depend
    on them.
19. Decompiled code can use a runtime struct with raw offsets or as a `long`
    array (`stack_memory_pool.c`: the 0x1C block header, `pool_data[13]`).
    Every upstream build is ILP32, so it works there. Give the pointers of
    that struct (and of its copies in other files) `HALO_PTR32`, and add a
    layout check.
20. A script value is a `long`. Where the game views its boolean or short
    through a union, a struct or `*(short *)&`, a big-endian CPU gives the
    high bytes. Use `HS_LOW_*` and `HS_SLOT_*` (`hs.h`): the low bytes, as
    on the Xbox. The map's script nodes swap their values as longs.
21. Some decompiled tag structs read a field narrower than the tag defines
    it (a `byte` of long flags). After the swap, that view gets the high
    bytes. Either move the narrow member to the field's last bytes under
    `__BIG_ENDIAN__` (`fog_definitions.h`), or swap the field in the
    narrower units (`FIELD_UNITS` in `tools/ps3_tag_swap.py`).
22. After a change of the tag layouts or the swap, run
    `tools/ps3_tag_swap_check.sh` on the maps before a hardware test.
23. The PPE faults on a misaligned float load or store, and lv2 then ends
    only that thread. The other threads run on: the display and the
    heartbeat continue, and the main loop step stops at a harmless line.
    Misaligned integer accesses work. A `#pragma pack(push, 1)` global can
    start at an odd address: give it `__declspec(align(8))` (its layout
    stays). To list the data symbols that are not 4-aligned (Git Bash):
    `MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W):/src" -w /src ps3dev-latest:latest ppu-nm -S build/ps3/halo_ps3.elf | awk 'NF==4 && $3 ~ /^[bBdDrR]$/ && strtonum("0x"$1) % 4 && strtonum("0x"$2) >= 4'`.
    In this build, only `global_screenshot_count` held floats.
24. A decompiled file can read a caller's struct through its own copy of
    the struct, with plain pointers (`rasterizer_xbox_text.c`). On LP64 its
    offsets differ from the caller's `HALO_PTR32` struct. Give the copy
    `HALO_PTR32`, and add a size check.
25. lv2's debug calls (`sys/dbg.h`: the exception handler, a thread's
    context and status) give 0x8001042c on Matt's console. Do not read
    another thread's stack: when lv2 ends a thread, its stack is unmapped,
    and the read crashed the console. Use the main loop steps and the
    heartbeat. The game's `__try`/`__except` is compiled out too, so a CPU
    fault on any thread is silent.
26. A struct whose next part continues past its end (`hs_function_definition`
    and its `with_N_parameters` arrays), or a byte array sized for Xbox
    entries (`single_player_level_data[0x50]`), breaks when 8-byte pointers
    add tail padding or size. Give the pointers `HALO_PTR32` and check the
    size and the offset of the continuation.
27. Texture memory keeps the Xbox's little-endian texel order: the map's
    texels and the ones the CPU writes. CPU code reads and writes 16/32-bit
    texels and palette entries with `TEXEL16`/`TEXEL32` (a swap on the PS3).
    Vertex data is big-endian (the map swap converts it).
28. A script stack frame's data was at +14 on the Xbox, so reals sat at 2
    mod 4 (trap 23). On the PS3, frames start 4-aligned with data at +16, and
    `hs_stack_allocate` rounds to 4 (2 for shorts). Map data can also hold
    floats at 2 mod 4 (compressed animations): copy them before use.
29. lv2 does not share a hardware thread between equal priorities the way
    the Xbox scheduler does. A thread that spins on another thread's flag
    calls `SwitchToThread` on the PS3. Threads that must run promptly
    (vblank, XMB monitor) sit above the game's 1001.
30. MSVC's `_vsnprintf`/`_snprintf` return -1 when the text does not fit;
    newlib returns the full length. Game code gets the MSVC return on the
    PS3 (`port/linux/include/stdio.h`).
31. The RSX's winding is the picture's as shown: with ps3gl's viewport
    (scale y negative, rows from the top) nothing is mirrored, so
    Direct3D's front face passes through. An inverted one culled every
    visible triangle (G4 runs 1 to 4: a black screen, no error).
32. A float holds 24 bits: `1.0f * 16777215 + 0.5f` is 2^24, which a
    `<< 8` into a Z24S8 clear value shifts out to 0. Clamp to 0xFFFFFF.
33. To find a black RSX picture, read pixels back after `rsxFinish`
    (`gcmGetConfiguration` gives the CPU address of RSX memory) and swap
    in one-instruction pixel shaders. G4's diagnostic builds did this.

## Commands

Use Git Bash in the repository folder.

| Command | Result |
| --- | --- |
| `./build.sh -j12` | `build/ps3/halo_ps3.elf` (the game) |
| `./build.sh pkg` | `build/ps3/halo_ps3.pkg`, the package to install |
| `./build.sh self` | `build/ps3/EBOOT.BIN`, to copy over an installed package |
| `./build.sh check` | The link checks |
| `./build.sh symbols ADDRESSES="..."` | The functions of a call stack from the log |
| `./build.sh -j12 game` | Compiles the 476 game units only |
| `./build.sh -j12 bringup-self` | `build/ps3/bringup/EBOOT.BIN`, the G0 bring-up test |
| `./build.sh game GAME_WARN=` | The same, with the default GCC warnings |
| `./build.sh clean` | Deletes `build/ps3` |
| `python3 tools/ps3_tag_swap.py --invader <clone>` | Rewrites `port/ps3/xbox/xbox_tag_swap_tables.h` from a clone of Invader (`git clone --depth 1 https://github.com/SnowyMouse/invader`) |
| `tools/ps3_tag_swap_check.sh "<maps folder>" [names]` | The swap of each map and BSP on the PC, with its errors and the bytes swapped twice or never |
| `KEYS=3000 SEED=1 tools/ps3_rsx_shader_check.sh` | The RSX microcode on the PC: the 67 vertex shaders and `KEYS` random pixel shaders against `cgcomp -a` and the NV2A's results (`VERBOSE=1` prints each program) |

The warning build of the G1 open items. It copies the tree, removes the
`#line` directives in the copy, and writes `warn.txt` to `OUT`. `OUT` is a
Windows path with forward slashes:

```sh
OUT=C:/path/to/a/folder
MSYS_NO_PATHCONV=1 docker run --rm -v "E:/Users/Matteo/Desktop/Halo/PS3:/src:ro" -v "$OUT:/out" ps3dev-latest:latest bash -c '
mkdir -p /tmp/w && cd /src && tar cf - --exclude=./build --exclude=./.git . | (cd /tmp/w && tar xf -)
cd /tmp/w && find source port/linux/game \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 sed -i "s/^#line .*$//"
make -k -j12 game GAME_EXTRA=-fsyntax-only GAME_WARN="-Wimplicit-function-declaration -Wincompatible-pointer-types -Wint-to-pointer-cast -Wno-multichar" > /out/warn.txt 2>&1'
```

## Hardware test

1. Build: `./build.sh pkg` for a first install. For an update,
   `./build.sh self`, then copy `build/ps3/EBOOT.BIN` with FTP to
   `/dev_hdd0/game/HALOCE000/USRDIR/EBOOT.BIN`.
2. Do `./build.sh check`.
3. Right before Matt boots, find the owner of UDP port 18194:
   `Get-NetUDPEndpoint -LocalPort 18194` (PowerShell), then the command line
   of that process. Matt's other sessions (for example flycastPS3) use the
   same port.
   - If the listener of another session has the port: do not start a
     second listener, and do not send test datagrams. Read its log file.
   - If the port is free: start the listener below, and stop it after the
     test.
4. Matt boots and reports SUCCESS or FAILURE.

The listener. Save it in the scratchpad of the session, and run it in the
background with
`C:\Users\Matteo\AppData\Local\Programs\Python\Python311\python.exe udplog.py <log file>`:

```python
# udplog.py: appends each datagram from UDP 18194 to the file in argv[1]
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
s.bind(("0.0.0.0", 18194))
with open(sys.argv[1], "ab", buffering=0) as f:
    while True:
        f.write(s.recv(65535))
```

The PS3 sends to `192.168.178.99`. For a different PC, do `./build.sh clean`,
then `./build.sh pkg LOG_IP=<address>`.

## Reference ports

- ioQuake3-PS3: `E:\Users\Matteo\Desktop\quake3\Ioquake3-PS3\ioQuake3-PS3`
  (`code\sys`, `code\input`, `code\audio`, `code\gl`, `AGENTS.md`).
- xashPS3: `E:\Users\Matteo\Desktop\HL1\PS3\xashPS3` (`engine\platform\ps3`,
  `3rdparty\ps3gl` for OpenGL 1.1 on the RSX, `AGENTS.md`).
- Read their `AGENTS.md` before you write new platform code.

## File map

| Path | Contents |
| --- | --- |
| `Makefile`, `build.sh` | The PS3 build |
| `port/ps3/src/` | The host layer: the entry point (`ps3_main.c`), `ps3_host.c` (lv2 for the Xbox layer), video, pads, audio, log |
| `port/ps3/xbox/` | The Xbox layer on the PS3: kernel, memory window, files, D3D on the RSX, XInput on the pads (`xinput_ps3.c`), DirectSound on the audio port (`dsound_ps3.c`), headless network |
| `port/ps3/src/ps3_gpu.c`, `ps3_gpu.h` | The RSX back end of D3D (G4) |
| `port/ps3/src/ps3_rsx_*.c`, `ps3_rsx_program.h` | RSX microcode at run time from NV2A vertex programs and combiners (G4) |
| `port/ps3/xbox/d3d8_rsx.c` | The D3D 8 device on the RSX (G4), from `d3d8_gl.c` |
| `tools/ps3_rsx_shader_check.c`, `.sh` | The PC check of the RSX microcode |
| `port/ps3/bringup/` | The G0 bring-up test |
| `port/ps3/include/` | `halo_ps3_prefix.h`, `halo_ps3_libtiff.h`, `limits.h`, `setjmp.h` |
| `port/linux/include/` | The C runtime wrappers, shared with the Linux build |
| `port/linux/src/` | The Xbox platform layer of the Linux build, the base for G2 |
| `port/linux/src/d3d8_gl.c` | The Direct3D 8 renderer of the Linux build (GLSL at run time), the reference for G4 |
| `port/linux/port.json` | The list of game units |
| `port/ps3/xbox/xbox_tag_swap.c`, `xbox_tag_swap.h` | The map swap (G3), and the G3 stop |
| `port/ps3/xbox/xbox_tag_swap_tables.h` | The swap layouts, written by `tools/ps3_tag_swap.py` from Invader (GPLv3) |
| `tools/ps3_tag_swap_check.c`, `.sh` | The PC check of the map swap |
| `tools/ps3_game_mk.py`, `tools/ps3_msvc_semantics.py` | They write `build/ps3/game.mk` and `build/ps3/halo_msvc_semantics.h` |
| `source/` | The decompiled game |
| `source/cache/physical_memory_map.c` | The fixed Xbox addresses (`GOALS.md`, blocker 3) |

## Log

- 2026-10-04: The scaffold: `Makefile`, `build.sh`, `port/ps3/` (the
  platform layer from ioQuake3-PS3 and xashPS3), `GOALS.md`.
- 2026-10-05: G0 hardware SUCCESS: 60 fps with paced flips, pad buttons and
  sticks, the 440 Hz tone with 0 partial blocks, a clean XMB exit. The boot
  lines were lost: no listener ran at boot.
- 2026-10-05: G1 step 1 (`HALO_PTR32`) and step 2 (32-bit `long`) done. 13
  places that gave the address of a `HALO_PTR32` member were fixed. The
  comments longer than 2 lines in the 116 changed files were shortened.
- 2026-10-05: This file.
- 2026-10-05: Matt chose G2 without a G1 hardware test. G2 implemented: the
  game links (0 `r2off`, `.got` 0x27e0) and runs headless to its first map
  file.
- 2026-10-05: First G2 run: the window at 0x80000000 works; a halt in
  `cache_copy_initialize` (the `LARGE_INTEGER` word order). Fixed. Waiting
  for the second run.
- 2026-10-05: Second G2 run: a halt in `stack_memory_pool_reset` (trap 19).
  Fixed. Audits of the path to the first map open. Waiting for the third
  run.
- 2026-10-05: Third G2 run: hardware SUCCESS. The game runs headless to the
  open of `d:\maps\ui.map`. G2 done.
- 2026-10-05: G3 started. Matt chose the swap at load time with Invader's
  definitions. The swap, its PC check (all 24 maps pass), the big-endian
  game fixes and the G3 stop are built. Waiting for the first G3 run.
- 2026-10-05: G3 hardware runs 1 to 8. The swap works on the PS3 (the
  counts of the PC check, 0 errors). Fixed: a misaligned float store that
  ended the main thread (run 7, `global_screenshot_count`), and the text
  parameters' copy with 8-byte pointers (run 8). Run 9 is built, not
  booted.
- 2026-10-05: Mismatch audit of the tree (10 classes, adversarially
  verified): 43 issues. Matt: fix all. All fixed and reviewed (see "Audit
  fixes", traps 26-30). Run 9 rebuilt with the fixes.
- 2026-10-05: Run 9: hardware SUCCESS. 1800 frames of the main menu on the
  swapped `ui.map`, green screen. G3 done.
- 2026-10-05: G4 started. Matt chose runtime microcode. The vertex program
  and combiner translators and their PC check (67 shaders, 3000 random
  pixel shaders, 0 failures), the RSX back end (`ps3_gpu.c`) and the
  device (`d3d8_rsx.c`) are built; `check` passes (0 `r2off`, `.got`
  0x29c8). The G3 stop is removed. Waiting for the first G4 run.
- 2026-10-05: G4 runs 1 to 4: black picture, no hang. Diagnostic builds
  isolated it: an inverted front face and a depth clear of 1.0 that wrote
  0. Both fixed; the diagnostics are removed. Run 5 is built.
- 2026-10-05: G4 run 5: hardware SUCCESS. The main menu draws on the RSX at
  30 frames per second. G4 done.
- 2026-10-05: G5 started with the gamepad (Matt). XInput on the PS3 pads
  (`xinput_ps3.c`, `ps3_input_rumble`) is built; `check` passes (0
  `r2off`, `.got` 0x29c8). Waiting for G5 run 1.
- 2026-10-05: G5 run 1: the pads work (Matt). From the menu, a new profile
  and a10: the level loads and runs (about 9 to 10 frames per second).
- 2026-10-05: G6 started (Matt, "G6. Go for the sound."). DirectSound on
  the PS3 audio port (`dsound_ps3.c`, replaces `dsound_null.c`) is built;
  a PC check of its decoder and mixer passes; `check` passes (0 `r2off`,
  `.got` 0x2c00).
- 2026-10-05: G6 run 1: the sound works (Matt). Menu, a10, menu: 0 silent
  and 0 partial port blocks; the mixer used 40 to 114 us a 5333 us chunk
  (at most 249 us), with up to 3 streams.
