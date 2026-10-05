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
  with an empty `git init`. There are no commits. To see the PS3 changes,
  compare with upstream and ignore CRLF (upstream is CRLF, this copy is LF):
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

## Now (2026-10-05)

- G0 is done: hardware SUCCESS.
- G1 is done in G2. Matt (2026-10-05) chose "Start G2 now": no separate G1
  hardware test; G2 fixes the G1 open items and links the game.
- G2 is done: hardware SUCCESS on the third run (2026-10-05). The EBOOT
  links the whole game with the PS3 Xbox layer (`port/ps3/xbox`) and runs
  it headless to the first map it opens (`d:\maps\ui.map`), where it stops.
  `check`: 0 `r2off` stubs, `.got` 0x27e8.
- First G2 run on the PS3 (2026-10-05), two boots:
  - lv2 gave the Xbox window at 0x80000000 at the first try. The game
    state is at 0x81A00000, the tag cache at 0x803A6000, as on the Xbox.
    G3 needs no pointer relocation.
  - Free memory: 188 MB at boot, 188 MB after the window, 179 MB after
    the RSX init. 1280x720.
  - The map cache files (730 MB) take 5 to 35 ms each (sparse).
  - The game halted in `cache_copy_initialize`: `LARGE_INTEGER` had the
    little-endian word order, so `u.HighPart` read the low half. Fixed
    (trap 17). The halt then asserted again and exited to the XMB: a halt
    now waits on the red screen for the XMB exit.
  - The PPC64 stack walk works: `./build.sh symbols` names the frames.
- Second G2 run (2026-10-05): the game passed the cache files, the saved
  game file, XInput and DirectSound, then halted in
  `ui_widgets_initialize` > `stack_memory_pool_reset` (trap 19). Fixed.
  Three audits of the rest of the path to the first map open found no other
  defect there. Their later finds are in "Found for later goals".
- Third G2 run (2026-10-05): SUCCESS. The game opened `d:\maps\ui.map`
  and stopped on the green screen; the XMB exit was clean.
  - `ui.map`: 14145536 bytes, header bytes `64 61 65 68` (`head` in
    little-endian order), version 5, length 33582080 after decompression,
    build "01.10.12.2276".
  - Free memory: 179 MB after the RSX init, 94 MB after the game's
    start-up (64 MB of it in the Xbox window).
- Next: G3. Waiting for Matt to confirm it. Still open for G3: convert the
  maps once on the PC, or swap them at load time (see "Decisions"). The
  first G3 fix is known: `struct data_array` ("Found for later goals").

## Goal stack

| Goal | Status |
| --- | --- |
| G0 Scaffolding and the platform layer: video, pads, audio, files, UDP log, XMB exit, memory probe | Done 2026-10-05 |
| G1 Data model: the game compiles and links for LP64 with the 32-bit layouts of its data | Done in G2 (Matt, 2026-10-05) |
| G2 The Xbox platform layer on the PS3 (kernel, XAPI, files, threads, memory, from `port/linux/src`). The game runs headless to its first map load. | Done 2026-10-05 (hardware SUCCESS, third run) |
| G3 Map data on a big-endian CPU: tag data swapped and its pointers relocated, checked headless | Not started |
| G4 Direct3D 8 on the RSX: the main menu shows | Not started |
| G5 XInput on the pads and DirectSound on the audio port: the menu operates, with sound | Not started |
| G6 First playable level (a10) | Not started |
| G7 Performance and hardening: memory budget, saved games on the HDD, network | Not started |

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
- The G2 stop is `HALO_PS3_STOP_AT_FIRST_MAP` in the Makefile
  (`port/ps3/xbox/xbox_files.c`). G3 removes it.

## Found for later goals

- Byte order (G3): `cache_files.c` (the tag signature bytes),
  `render_contrails.c` (the alpha byte), `ui_widget_event_handler_functions.c`
  (a `word` given as `int *`).
- `stack_walk_windows.c` is an x86 stack walker. It needs a PS3 version or a
  stub.
- zlib's `z_stream` keeps 8-byte pointers (`zlib.h` stays as zlib wrote it).
  So the layout asserts of the map decompressor's runtime state are off on
  the PS3 (`#ifndef HALO_PS3` in `cache_files_decompress_windows.c`).
- From the G2 audits (2026-10-05), all after the G2 stop:
  - G3, a certain halt at each map load: `struct data_array` (`data.h`) is
    0x40 bytes, the map's script nodes have a 0x38 header. `hs_allocate`
    then frees tag memory. Fix: `HALO_PTR32` on `data`, a 0x38 check.
  - Scripts: `union hs_evaluation_argument` (`hs.c`) has an 8-byte pointer,
    and `short_value`/`boolean_value` read the high half of a 4-byte slot.
    `struct hs_stack_frame` grows from 16 to 32 bytes in the 0x200 stack.
  - `recorded_animations.c`: the debug table is 0x400 bytes for 64 entries
    of 24 bytes (16 on the Xbox).
  - G7: `union network_game_message_size` (`network_messages.c`) reads the
    high half: each message asserts. `data_encoding.c` swaps every packet,
    so the PS3 wire order is not the PC's.
  - Winsock: `XNetGetTitleXnAddr` gives `XNET_GET_XNADDR_NONE`, so
    `transport_initialize` fails, and a `telnet_console = true` asserts.
  - `zlib/trees.c` `_tr_init`: the offsets 0x14 and 0x28 are ILP32 sizes of
    `static_tree_desc`. Only `bitmap_extract.c` compresses.
  - `tag_to_string` (`cseries.c`) swaps the bytes: assert texts only.

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
- Still open: G3, convert the maps once on the PC or swap them at load time.
  G4, generate RSX microcode directly or use shaders compiled offline with
  `cgcomp` (the PS3 has no shader compiler at run time).

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
| `port/ps3/xbox/` | The Xbox layer on the PS3: kernel, memory window, files, headless D3D, DirectSound, XInput, network |
| `port/ps3/bringup/` | The G0 bring-up test |
| `port/ps3/include/` | `halo_ps3_prefix.h`, `halo_ps3_libtiff.h`, `limits.h`, `setjmp.h` |
| `port/linux/include/` | The C runtime wrappers, shared with the Linux build |
| `port/linux/src/` | The Xbox platform layer of the Linux build, the base for G2 |
| `port/linux/src/d3d8_gl.c` | The Direct3D 8 renderer of the Linux build (GLSL at run time), the reference for G4 |
| `port/linux/port.json` | The list of game units |
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
