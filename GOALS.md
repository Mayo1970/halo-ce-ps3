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
| G3 Map data on a big-endian CPU: tag data swapped and its pointers relocated, checked headless | Done. Hardware SUCCESS 2026-10-05 on run 9: 1800 frames of the main menu on the swapped `ui.map`, after the fixes of 2 halts and of the 43 audit mismatches. See "G3 progress". |
| G4 Direct3D 8 on the RSX: the main menu shows | Done. Hardware SUCCESS 2026-10-05 on run 5: the main menu at 30 frames per second, after the fixes of an inverted front face and a depth clear of 0. See "G4 progress". |
| G5 XInput on the pads and DirectSound on the audio port: the menu operates, with sound | Done. The pads (XInput) work: hardware SUCCESS 2026-10-05 on run 1. Matt moved the sound to G6. See "G5 progress". |
| G6 First playable level (a10) | In progress. Matt (2026-10-05): "G6. Go for the sound." The sound works: hardware SUCCESS 2026-10-05 on run 1. See "G6 progress". |
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
- The game state fits (audit, 2026-10-05): the PS3 needs 0xDA0C3C (13.63
  MB) of the 0xFC0000 budget; ILP32 needs 13.60 MB. Only 33 KB grow with
  8-byte pointers (mostly `object_header_datum`, 12 to 16 bytes).

## G3 progress

Matt (2026-10-05): swap the map data at load time on the PS3, with Invader's
tag definitions (GPL-3.0-only, so the tables and the EBOOT are GPLv3). The
PS3 keeps the Xbox maps as they are.

- No pointer relocation: the Xbox window is at `0x80000000`, so the map's
  pointers stay valid (blocker 3).
- The swap (`port/ps3/xbox/xbox_tag_swap.c`), in place, when the game reads:
  - The map header, in `cache_file_header_verify` (each header the game
    reads goes there), only if it is little-endian. The cache copy writes
    the swapped header to `z:\cacheNNN.map`; the rest of that file stays
    little-endian.
  - The tag data, in `scenario_tags_load`, before the `tags` check: the
    tag header, the tag instances, the D3D vertex and index buffer headers,
    then each tag by its group's layout, through its blocks.
  - Each structure BSP, in `scenario_structure_bsp_load`: its header, its
    D3D vertex buffer headers, then the `sbsp` layout.
  - Special data: Xbox model parts (32-byte compressed vertices behind the
    part's D3D vertex buffer, and the triangle strip), BSP lightmap vertices
    (by the material's counts), script nodes (a `data_array` of 20-byte
    nodes; values swap as longs), animation frame info, default data and
    frames (by the node flags; compressed animations by the sections of
    their header), recorded animation streams (version 4, by event),
    UTF-16 strings, PVS bit vectors. Sound samples and bitmap pixels are not
    in the tag data: the caches read them by file offset (G4, G5).
- The layouts: `tools/ps3_tag_swap.py` reads Invader's JSON definitions and
  writes `port/ps3/xbox/xbox_tag_swap_tables.h`. It checks each struct's
  size against Invader's. Xbox specifics: the data field kinds, MCC-only
  fields as padding, the 'external' word of a data field is not used.
- Checked on the PC: `tools/ps3_tag_swap_check.sh` runs the same C file with
  the host GCC of the Docker image on all 24 maps of the NTSC disc
  (01.10.12.2276) and their 82 BSPs: 0 errors, no byte reached twice, only
  names and alignment bytes not reached. Python checks of the same maps:
  the 94 compressed animations of a10 and c40 have consistent keyframes, and
  all 349 recorded animation streams end exactly at their end event.
- The game's views of tag data, compared with Invader's units through the
  DWARF of a `-g` build (244 struct pairs): 5 fields that the game reads
  narrower than the tag defines them. Fixed: the UI widget text box flags
  (swapped as words), the fog flags (a byte and a word view), a runtime
  decal's palette index, the vehicle flags that AI perception reads.
- Big-endian fixes in the game: `struct data_array` (`HALO_PTR32` data,
  0x38 bytes, as the map's script nodes); the HaloScript values
  (`HS_LOW_*`, `HS_SLOT_*` in `hs.h`: the argument views, the results, the
  global reconcile, the casts, `sleep_until`, `and`/`or`, `=`/`!=`);
  `hs_stack_frame` (0x10); the string external global (it wrote 8 bytes in
  a 4-byte value); the recorded animation header bitfield; the recorded
  animation debug table (0x10 entries); a word count passed as a long
  (`ui_widget_event_handler_functions.c`); the material type read of
  `material_effects.c`.
- The G3 stop: after the first map's tag data is swapped, 1800 presented
  frames (30 s at 60 Hz) end the run on the green screen
  (`HALO_PS3_STOP_AFTER_MAIN_MENU`). G4 removed it.
- Hardware runs 1 to 8 (2026-10-05):
  - The swap of `ui.map` on the PS3 gives the counts of the PC check and 0
    errors (8 to 9 ms for the tag data).
  - Runs 1 to 7: the main thread stopped on its first main loop pass, with
    no wait and no lock; the display thread and the heartbeat ran on. Finer
    main loop steps in each run placed it in `set_window_camera_values`,
    then at its first float store. Cause: `global_screenshot_count` (a
    `#pragma pack(push, 1)` struct, 0x38B bytes) was at 0x103D217F. The PPE
    faults on a misaligned float store, and lv2 ends only that thread.
    Fixed: `__declspec(align(8))` on the PS3. No other data symbol with
    floats is misaligned.
  - Run 8: the main menu's text halted in `rasterizer_text_begin`
    (`parameters->map[0]`, called from `render_ui_widgets`).
    `rasterizer_xbox_text.c` read the caller's
    `rasterizer_dynamic_screen_geometry_parameters` through its own copy
    with 8-byte pointers. Fixed: `HALO_PTR32` in the copy, a 0x8C size
    check. `rasterizer_xbox_plasma_energy.c` had the same bug (fixed in the
    audit below).
  - lv2's debug calls (`sys/dbg.h`) give 0x8001042c on Matt's console, so
    the build has no exception handler.
- Mismatch audit (2026-10-05). Why G2 and G3 went one bug per boot: each
  fault ends the run where it happens, a fault is silent (lv2 ends only that
  thread; no debug calls; `__try` compiled out), and the bug classes exist
  on no upstream build (all ILP32 and little-endian). A static audit of 10
  classes, each finding checked by an adversarial verifier, found 43 issues;
  Matt asked to fix all. Each fix was reviewed in the compiled code:
  - `hs_function_definition` (5 pointers, 56 bytes on LP64) put tail
    padding before its `with_N_parameters` arrays, so every script function
    with 2+ parameters read types 2..N as 0 ("needs to be recompiled" and
    garbage arguments; `ring_loop` on the menu, 1793 calls in a10). Now
    `HALO_PTR32`, 0x1C, with offset checks.
  - Script reals sat at 2 mod 4 on the script stack (frame data at +14):
    a silent main thread death early in a10. Frames and slots are now
    4-aligned on the PS3; a simulation over the 24 maps gives 0 misaligned
    reals and a worst case of 457 of 512 bytes (453 on the Xbox).
  - Compressed animations: 89 of 186 have float sections at 2 mod 4 in the
    Xbox maps (a10 intro): now copied to aligned locals before use.
  - `single_player_level_data[0x50]` held 16-byte entries (10 x 16): the
    level list overwrote `build_number_string`. `map_name` is `HALO_PTR32`.
  - Also fixed: `plasma_runtime_parameters`, the camera and game start
    request copies, the AI vocalization short, CPU texels in the Xbox byte
    order, contrail alpha, Winsock order, the network message size union,
    the message header bitfield, MSVC `_vsnprintf` returns (lines of 256+
    characters halted), `fast_ftol_C`, `menu_tags.c` `%ld`, `trees.c`,
    the texture cache debug sort, the loading TGA, thread priorities and
    spins, cache flag barriers, thread names.
- Run 9 (2026-10-05, the audit fixes): SUCCESS. The main loop ran at 30
  frames per second to `G3: 1800 frames and 1799 game ticks on the swapped
  map data` and the green screen. Free memory: 188 MB at boot, 179 MB after
  the RSX init.

## G4 progress

Matt (2026-10-05): runtime microcode. C emitters translate the NV2A vertex
programs and the combiner state to RSX microcode at run time, cached per
key (blocker 4). The encoders follow PSL1GHT's cgcomp
(`compilervp.cpp`, `compilerfp.cpp`).

- Vertex programs (`port/ps3/src/ps3_rsx_vp.c`): the RSX's vertex unit has
  the NV2A's opcodes, registers and operand fields. Each NV2A instruction
  (its MAC and ILU halves) becomes 1 to 4 RSX instructions, in an order
  that keeps the NV2A's reads before writes. r12 is the position. The
  program first sets the NV2A's output defaults (0 0 0 1, fog 1) and zeroes
  the temporaries it reads before it writes them (56 of the 67 shaders):
  the RSX does not clear them.
- The position: the game's programs end in screen space (the XDK epilogue
  with c[-38] and c[-37]). The translation maps screen space to the RSX's
  clip space with c[192] and c[193], which the device computes for the
  bound target, with Direct3D's integer pixel centres moved to the RSX's
  halves. Where the program takes `rcc` of the clip position, that position
  is kept, so there is no division by w.
- Relative constants (`c[A0.x + n]`, 23 shaders): the `INDEX_CONST` bit of
  nouveau's NV40 encoding (cgcomp drops it). An ARL from a computed value
  adds 0.001 first, as `nv2a_vsh.c`.
- Pixel shaders (`ps3_rsx_fp.c`): the texture stages (every mode of
  `nv2a_psh.c`), the general combiners (input and output mappings, dot
  products, the mux, the [-1, 1] clamp), the final combiner, fog and alpha
  kill, with `nv2a_psh.c`'s semantics. The per-draw constants are inline
  constants: a copy of the program with the values patched in goes to the
  frame's scratch when they change.
- The PC check (`tools/ps3_rsx_shader_check.sh`, the image's host GCC and
  `cgcomp`): the 67 vertex shaders and 3000 random pixel shader keys, word
  for word against `cgcomp -a` (the relative bit aside), and their results
  against an NV2A interpreter and a combiner reference, with garbage in the
  RSX's temporaries. 0 failures.
- The RSX back end (`port/ps3/src/ps3_gpu.c`): the game's thread takes the
  RSX from the display thread when it creates the device; at a halt the
  display thread takes it back for the red screen. Per frame: a 16 MB half
  of a 32 MB scratch in RSX memory (vertices, indices, fragment programs),
  fenced with backend label 64 (ioQuake3-PS3's pattern); freed RSX memory
  waits for the frame that last used it. State, programs and textures are
  sent only when they change, and all again at each frame. Present draws
  the 640x480 back buffer 4:3 into the display buffer (bilinear) and
  flips.
- The device (`port/ps3/xbox/d3d8_rsx.c`, from `d3d8_gl.c`):
  - Render targets: linear A8R8G8B8 and Z24S8 RSX surfaces in place of the
    memory their `Data` names. A texture over a rendered target samples
    its surface (linear, its level 0).
  - Textures: RSX copies. DXT stays as it is (blocks in order, as on the
    Xbox); every other format becomes A8R8G8B8 in the same texel order
    (the RSX's swizzle is the NV2A's); linear textures keep texel
    coordinates (`UNRM`). A hash of the texels, once a frame per texture,
    finds the ones the CPU changed.
  - Vertices: each draw converts its vertex range to float4 attributes,
    interleaved, in one pass (vertex data is big-endian after G3). Inputs
    without data get a stride-0 constant (ps3gl's pattern).
  - Facing: the picture is not mirrored (rows from the top, as ps3gl's),
    so the RSX's winding is Direct3D's: the front face passes through.
  - The vertical blank emulation and the flip counter stay as in G2.
- Not yet: visibility tests (always 0, so lens flares stay hidden), render
  targets sampled by mip level, point sprites. The cache of converted
  vertex buffers came on 2026-10-06 (ROADMAP.md, "Now").
- Hardware runs (2026-10-05):
  - Runs 1 and 2: a black picture at 30 frames per second, 0 skipped draws,
    no hang.
  - Diagnostic runs 3 and 4 (grey bars, a test square through the present
    programs, one-instruction test shaders in timed phases, pixels read
    back): the present, the geometry, the vertex colours, the textures (DXT
    included) and the full translated pixel shaders work. With the game's
    own render state, nothing showed.
  - Causes: the front face was inverted, so every visible triangle was
    culled; and the depth clear value overflowed (1.0 * 16777215 + 0.5
    rounds to 2^24 in a float, which shifted out to 0), so every depth test
    failed. Both are fixed.
  - Run 5: SUCCESS. The main menu shows at 30 frames per second: about
    120 draws a frame, 0 skipped, 0 dropped.
- Not proven on hardware yet: the relative constant bit, `UNRM` textures,
  stencil.

## G5 progress

Matt (2026-10-05): start G5 with the gamepad ("Wire that Gamepad in!").
DirectSound is not started.

- `port/ps3/xbox/xinput_ps3.c` replaces `xinput_null.c`: XInput on the pads
  of `port/ps3/src/ps3_input.c`.
- Ports: a pad takes the lowest free Xbox port (0 to 3) when it connects,
  and keeps it until it disconnects. A lone pad is port 0 on any PS3 port,
  and the ports of the other players do not move. If a port loses its pad
  and gets a new one in the same poll, the game sees a removal and an
  insertion.
- Polling: the game calls `XGetDeviceChanges` for the gamepads once a frame
  on its thread (`input_frame_begin`). The pads are polled there, so all
  pad calls run on one thread. `XInputSetState` comes from the game's input
  thread: it only keeps the motor speeds, and the next poll sends them.
- Mapping (as `xinput_sdl.c`):

  | DS3 | Xbox |
  | --- | --- |
  | CROSS, CIRCLE, SQUARE, TRIANGLE | A, B, X, Y |
  | L1, R1 | White, Black |
  | L2, R2 | Left trigger, right trigger |
  | D-pad, left stick, right stick | D-pad, left stick, right stick |
  | L3, R3 | Left stick click, right stick click |
  | START, SELECT | Start, Back |

- Analog buttons: 255 while held, else 0. The game reads them only through
  its press thresholds (`input_xbox.c`), so the pressures are not used.
- Sticks: 0 to 255 (128 at the center) become -32768 to 32767, y up. The
  game applies its own dead zone (9000).
- Rumble: the left motor's speed / 256 drives the large motor. The small
  motor (only on or off) runs from half of the right motor's speed. Sent
  only on a change. Not tested: the menu does not rumble (G6).
- The log: `XInput: PS3 pad N is port M`, `port M open`, and
  `XInput: port M: <buttons>` at each change of the buttons the game gets.
- Run 1 (2026-10-05): hardware SUCCESS for the pads (Matt). The D-pad, A,
  B and Start reached the game; a new profile, then a10 from the menu. In
  a10: the tag data and the structure BSPs swap with 0 errors, and the
  level runs at about 9 to 10 frames per second (the heartbeat). For G6:
  `checksum failed on persistent storage` once at the level start.

## G6 progress

Matt (2026-10-05): "G6. Go for the sound."

- `port/ps3/xbox/dsound_ps3.c` replaces `dsound_null.c`: DirectSound on
  the audio port of `port/ps3/src/ps3_audio.c` (G0, hardware-tested), after
  `port/linux/src/dsound_sdl.c`.
- What the game plays: 81 streams, all Xbox ADPCM (`sound_preferences.c`:
  mono 22 kHz 2D and 3D, stereo 22 kHz, stereo 44 kHz), each with up to 4
  packets of about 186 ms; the inanity buffer (silent, no front mix bin);
  the loading screen's 4 looping ADPCM tones (`progress_bar.c`), which
  `dsound_sdl.c` leaves silent.
- Decoding: each packet becomes floats when the game gives it
  (`Process`, on the game's thread), so the mixer converts nothing. The
  IMA decoder runs on float tables (the PPE converts an integer to a float
  through memory); it equals `dsound_sdl.c`'s integer decoder bit for bit.
  16-bit PCM is read little-endian, as the map keeps it.
- Mixing: a thread at priority 1000 (above the game's 1001, as the vblank
  thread) keeps 2048 frames (43 ms) in the port's ring, in chunks of 256
  frames (one port block, 5.3 ms), and sleeps 4 ms between passes. Per
  voice: linear resampling to 48 kHz (`SetFrequency`; a buffer's
  `SetPitch` is 1/4096 octave from 48 kHz), the volume, the front mix
  bins, and for 3D voices the distance, pan and I3DL2 direct gain of
  `dsound_sdl.c`. Gains ramp over a chunk. A soft limit above 0.8.
- Completion: the mixer only marks packets played; the game's callbacks
  run in `DirectSoundDoWork` and `Flush` on the game's thread, as in
  `dsound_sdl.c`.
- `ps3_host_exit` stops the audio port before the RSX (as the bring-up
  test).
- `audio.volume` and `audio.enabled` (`config.toml`) work as on Linux.
- The log: `DirectSound: the PS3 audio port, 48000 Hz stereo, mixed 43 ms
  ahead` at the start, then every 10 s the streams and buffers that play,
  the packets decoded, the mixer's time per chunk, and the port blocks
  played silent or partial (underruns).
- PC check (a harness in the session's scratchpad, not in the tree): the
  decoder against `dsound_sdl.c`'s on 4000 random packets, 4 PCM packets
  (frame count, 1 kHz pitch, level, callbacks), the resampler against the
  per-frame double cursor of `dsound_sdl.c` at 7 frequencies (largest
  difference 1e-4), a flush (1 played, 3 flushed), a 3D pan, the loading
  tone's loop. All passed.
- `check`: 0 `r2off`, `.got` 0x2c00.
- Run 1 (2026-10-05): hardware SUCCESS (Matt, "It works!"). The menu,
  a10 for about 40 s, then the menu again. Every 10 s line: 0 silent and 0
  partial port blocks; the mixer took 40 to 114 us on average for a 5333 us
  chunk (at most 249 us), so about 1 to 2 percent of one PPE thread; up to
  3 streams played at once. `### ERROR no DirectSound for bink` at the
  start comes from `bink_null.c` (`BinkSetSoundSystem` fails; no Bink
  decoder yet), not from DirectSound.

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
   definitions. Decision (Matt, 2026-10-05): swap at load time with
   Invader's definitions (see "G3 progress").
3. **Fixed Xbox addresses.** The tag cache is at `0x803A6000` and the game
   state at `0x81A00000` (`source/cache/physical_memory_map.c`). Solved on
   the PS3 (2026-10-05): lv2's mmapper gives the window at `0x80000000`,
   so the pointers in the tag data stay valid (see "G2 progress").
4. **Runtime shaders.** The Linux renderer (`port/linux/src/d3d8_gl.c`)
   translates the NV2A vertex shaders and register combiners to GLSL at run
   time. The PS3 has no shader compiler at run time. Decision (Matt,
   2026-10-05): G4 generates RSX microcode directly (see "G4 progress").
5. **Memory.** The Xbox window is 128 MB on Linux (tag cache 22 MB, game
   state 16 MB, texture cache 22 MB, sound cache 4 MB, and more). The free
   main memory on the PS3 is approximately 145 to 200 MB, by firmware and
   EBOOT size; G0 measures it. The texture cache can go in the 256 MB of
   RSX memory.
