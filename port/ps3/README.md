# PS3

The PS3 port builds with PSL1GHT (ppu-gcc 7.2) in the Docker image
`ps3dev-latest:latest`. The EBOOT links the whole game. It loads the first
map, swaps the map data to the PS3's byte order, and draws the game with the
RSX (G4 in `GOALS.md`). It has no sound and no pads yet (G5).
`GOALS.md` in the root folder gives the plan and the blockers.

The code is in three parts:

| Folder | Contents | Compiled as |
| --- | --- | --- |
| `src/` | The host layer: lv2 and newlib. The entry point, the UDP log, video, pads, audio, and `ps3_host.c`, the lv2 services of the Xbox layer. | LP64, with newlib's own 64-bit `long` |
| `xbox/` | The Xbox layer on the PS3: kernel, memory, files, Direct3D on the RSX, and a headless DirectSound, XInput and network. The Linux build's portable units (`port/linux/src`) join it. | The game's ABI: 32-bit `long`, the XDK declarations |
| `bringup/` | The G0 bring-up test, without the game | LP64 |

`src/ps3_host.h`, `src/ps3_gpu.h` and `src/ps3_rsx_program.h` are the
boundary between the first two parts. They have only fixed-width types,
because `long` has a different size on each side.

| File | Contents |
| --- | --- |
| `src/ps3_main.c` | The entry point and `SYS_PROCESS_PARAM` (1 MB main stack, where the game runs). It reserves the Xbox memory window, starts the display thread, and calls the game's `main()` (`shell_xbox.c`, compiled as `halo_xbox_main`). |
| `src/ps3_host.c` | lv2 mutexes and condition variables, PPU threads, the timebase, the mmapper calls of the memory window, the lv2 file calls. |
| `src/ps3_system.c` | The UDP log, the XMB exit, the free memory, the time and the data root. |
| `src/ps3_video.c` | RSX init, three buffers with vsync flips, 720p if the TV has it. |
| `src/ps3_gpu.c` | The RSX for Direct3D: RSX memory, a per-frame scratch in two halves with a backend label fence, render state, programs, textures, draws, and the present (the 640x480 picture 4:3 in the middle of the display). The game's thread takes the RSX from the display thread when it creates its device. |
| `src/ps3_rsx_vp.c`, `ps3_rsx_fp.c`, `ps3_rsx_asm.c`, `ps3_rsx_program.h` | RSX microcode made at run time: the game's NV2A vertex programs, and its pixel shaders (texture stages and register combiners), encoded as `cgcomp` encodes them. |
| `src/ps3_input.c` | DualShock 3 and Sixaxis pads on all 7 ports: buttons, sticks and pressures. |
| `src/ps3_audio.c` | One 48 kHz stereo float audio port, fed from a ring by a thread. |
| `src/ps3_posix.c` | The part of `port/linux/src/posix.h` that `msvc_crt.c` calls. |
| `src/ps3_setjmp.S`, `include/setjmp.h` | A 64-bit `setjmp`/`longjmp` in place of the newlib one, which overflows `jmp_buf`. |
| `xbox/ps3_platform.h` | Force-included in the Xbox layer, in place of `port/linux/src/platform.h`. |
| `xbox/xbox_kernel.c` | Handles, events, mutexes, threads, waits, APCs, time, `GlobalAlloc`. One lv2 lock and condition variable for all waits. |
| `xbox/xbox_memory.c` | `XPhysicalAlloc`. The Xbox window at `0x80000000`, as on the Xbox. |
| `xbox/xbox_files.c` | The Win32 file API and the Xbox paths. |
| `xbox/xbox_tag_swap.c` | The swap of the map data (header, tag data, structure BSPs) to the PS3's byte order when the game reads it. Its layouts are `xbox/xbox_tag_swap_tables.h`, which `tools/ps3_tag_swap.py` writes from Invader's tag definitions (GPLv3). |
| `xbox/d3d8_rsx.c` | The Direct3D 8 device on `src/ps3_gpu.c`, from `port/linux/src/d3d8_gl.c`: state, vertex shaders, render targets as RSX surfaces, a texture cache in RSX memory (converted again when the texels change), vertices converted to floats per draw. |
| `xbox/xbox_texture_layout.c` | The layout of Xbox textures, and their texels for the RSX: A8R8G8B8 in the same order (the RSX swizzles as the NV2A does), DXT blocks as they are. |
| `xbox/dsound_null.c`, `xinput_null.c`, `xnet_null.c` | Headless devices until G5, no network until G7. |
| `xbox/port_stubs.c` | The Linux build's services that the PS3 does not have yet: internet play, the PC menus, the high-res HUD and text. |

## Build

Use a shell that runs `build.sh` (Git Bash on Windows) in the root folder:

| Command | Result |
| --- | --- |
| `./build.sh -j12` | `build/ps3/halo_ps3.elf`, the game |
| `./build.sh -j12 pkg` | `build/ps3/halo_ps3.pkg`, the package to install |
| `./build.sh -j12 self` | `build/ps3/EBOOT.BIN`, fake-signed, to copy over an installed package |
| `./build.sh check` | The link checks |
| `./build.sh -j12 bringup-self` | `build/ps3/bringup/EBOOT.BIN`, the G0 bring-up test |
| `./build.sh -j12 game` | Compiles the game's 476 units only |
| `./build.sh clean` | Deletes `build/ps3` |
| `tools/ps3_tag_swap_check.sh "<maps folder>"` | Runs the map swap on the PC over each map and BSP of an Xbox `maps` folder |
| `KEYS=3000 SEED=1 tools/ps3_rsx_shader_check.sh` | Checks the RSX microcode on the PC: the game's 67 vertex shaders and `KEYS` random pixel shaders, word for word against `cgcomp -a`, and their results against the NV2A's |

The game units use `include/halo_ps3_prefix.h` (32-bit `long`, `HALO_PTR32`,
the MSVC keywords) and the GCC equivalents of the Linux build's ABI flags,
with `-mminimal-toc`: they need 137 KB of TOC, and the one TOC holds 64 KB.
`./build.sh game GAME_WARN=` shows their warnings, which are off by default
(`-w`), as on Linux.

Do `./build.sh check` before each hardware test. It stops with an error if:

- The ELF has `r2off` stubs. A TOC over 64 KB makes the linker split it, and
  such an EBOOT hangs at boot. `-Wl,--no-multi-toc` makes this a link error.
- `.got` is 0x10000 bytes or more.

It also shows the loaded segments. Their size comes out of the main memory.

The log goes to `192.168.178.99`. For a different computer, give `LOG_IP`:
`./build.sh clean` and then `./build.sh pkg LOG_IP=192.168.1.10`. Make does
not rebuild for a changed flag.

## Install

1. Copy `build/ps3/halo_ps3.pkg` to a USB drive.
2. On the PS3 (CFW or PS3HEN), select Package Manager > Install Package
   Files, and install it.

The title ID is `HALOCE000`. To update only the program, copy
`build/ps3/EBOOT.BIN` (`./build.sh self`) to
`/dev_hdd0/game/HALOCE000/USRDIR/EBOOT.BIN` with FTP.

## Game data

The data root is `/dev_hdd0/data/HaloCE`. It is the Xbox `d:\`. Put the
`maps` folder of an Xbox disc image in it, unchanged (the game swaps the map
data when it loads it), for example with FTP:
`/dev_hdd0/data/HaloCE/maps/a10.map`. If the HDD has no `maps` folder, the
port looks for `HaloCE/maps` on the USB drives (`/dev_usb000` to
`/dev_usb006`). At the first start without data, it makes the HDD folder.

The game writes these files:

| File | Contents |
| --- | --- |
| `/dev_hdd0/data/HaloCE/config.toml` | The settings, with the defaults at the first start |
| `/dev_hdd0/data/HaloCE/z/` | The Xbox `z:\`: the map cache (`cache000.map` to `cache005.map`, about 730 MB) and `savegame.bin` |
| `/dev_hdd0/data/HaloCE/u/` | The Xbox `u:\`: the user data |
| `debug.txt` in the data root | The game's log |

## Log

The PS3 sends its log as UDP datagrams to port 18194 of `LOG_IP`. To see it,
enter `nc -ul 18194` on that computer. `printf`, `puts` and `putchar` also
go to the log (`-Wl,--wrap`), and so do the lines of the game's
`debug.txt`.

## The EBOOT (G4)

| Part | In the log | On the PS3 |
| --- | --- | --- |
| System | `[ps3]` lines: the build time, `sizeof` of `void*`, `long` and `wchar_t`, the byte order | |
| Memory | `[mem]` lines: the free memory at boot, after the memory window and after the RSX init; the mmapper results | |
| Display | `[vid]` lines; `[hb]` every 10 s | A slow blue pulse until the game creates its Direct3D device |
| Game | `[halo]` lines: each memory window block, each file the game opens, each thread; the game's own `debug.txt` lines | |
| Map data | `tag data swapped in N ms: ...` and `structure BSP swapped ...`, with the counts and the errors | |
| Drawing | `[gpu] ready`; `Direct3D:` lines: each vertex shader translated, the first pixel shaders, and every 600 frames the draws, the skips and the texture uploads; `[gpu] frame N` every 600 frames: the programs and the scratch use | The game's picture, 4:3 in the middle of the display |
| Exit | `[ps3] exit 0` | PS button > Quit Game goes back to the XMB |
| Failure | `EXCEPTION halt` and the call stack: `./build.sh symbols ADDRESSES="..."` names its functions | A red pulse until PS button > Quit Game |

## Bring-up test

`./build.sh bringup-self` builds the G0 test of the host layer
(`bringup/ps3_bringup.c`) as `build/ps3/bringup/EBOOT.BIN`. To run it, copy
that file over the installed EBOOT with FTP.

| Part | In the log | On the PS3 |
| --- | --- | --- |
| System | `[ps3]` lines: the build time, `sizeof` of `void*`, `long` and `wchar_t`, the byte order | |
| Memory | `[mem]` lines: the free memory at boot and after each init | |
| Files | `[fs]` lines: the data root and each file in `maps` with its size | |
| Video | `[vid]` lines: `rsxInit`, the resolution, the three display buffers | A slow blue pulse |
| Input | `[pad]` lines: each pad that connects, each button down and up | CROSS, CIRCLE, TRIANGLE, SQUARE change the colour |
| Audio | `[aud]` lines: the audio port | Hold CROSS for a 440 Hz tone |
| Timing | `[hb]` each second: frames, flips, audio blocks, the state of each pad | |
| Exit | `[ps3] XMB exit`, then `shut down` lines | PS button > Quit Game goes back to the XMB |
