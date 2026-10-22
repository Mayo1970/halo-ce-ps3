# PS3

The PS3 port builds with PSL1GHT (ppu-gcc 7.2) in the Docker image
`ps3dev-latest:latest`. The EBOOT links the whole game. It loads the first
map, swaps the map data to the PS3's byte order, and draws the game with the
RSX (G4 in `GOALS.md`). The PS3 pads are its Xbox gamepads (G5). Its
DirectSound plays on the PS3 audio port (G6). Its Winsock and XNet use the
PSL1GHT sockets, for system link with the PC builds on the LAN and for
internet play (G7).
`GOALS.md` in the root folder gives the plan and the blockers.

The code is in four parts:

| Folder | Contents | Compiled as |
| --- | --- | --- |
| `src/` | The host layer: lv2 and newlib. The entry point, the UDP log, video, pads, audio, the SPU threads, and `ps3_host.c`, the lv2 services of the Xbox layer. | LP64, with newlib's own 64-bit `long` |
| `xbox/` | The Xbox layer on the PS3: kernel, memory, files, Direct3D on the RSX, XInput on the pads, DirectSound on the audio port, Winsock and XNet on the PSL1GHT sockets. The Linux build's portable units (`port/linux/src`) join it. | The game's ABI: 32-bit `long`, the XDK declarations |
| `spu/` | The SPU programs. The EBOOT holds each one as data (`bin2s`), which `src/ps3_spu.c` starts on an SPU thread. | `spu-gcc` 7.2 |
| `bringup/` | The G0 bring-up test, without the game | LP64 |

`src/ps3_host.h`, `src/ps3_gpu.h`, `src/ps3_rsx_program.h`,
`src/ps3_input.h`, `src/ps3_net.h`, `src/ps3_spu.h`, `include/ps3_vmx.h`, `include/ps3_spu_game.h`, `spu/spu_mixer.h`, `spu/spu_vertices.h` and `spu/spu_game.h` are the boundary
between the parts. They have only fixed-width types, because `long` has a different size on each side.

| File | Contents |
| --- | --- |
| `src/ps3_main.c` | The entry point and `SYS_PROCESS_PARAM` (1 MB main stack, where the game runs). It reserves the Xbox memory window, starts the display thread, and calls the game's `main()` (`shell_xbox.c`, compiled as `halo_xbox_main`). |
| `src/ps3_host.c` | lv2 mutexes and condition variables, PPU threads, the timebase, the mmapper calls of the memory window, the lv2 file calls. |
| `src/ps3_system.c` | The UDP log, the XMB exit, the free memory, the time and the data root. |
| `src/ps3_video.c` | RSX init, three tiled buffers with vsync flips, 720p if the TV has it, and the display's shape (16:9 or 4:3). |
| `src/ps3_gpu.c` | The RSX for Direct3D: RSX memory, a per-frame scratch in two halves with a backend label fence, render state, programs, textures, draws, and the present (the back buffer pixel for pixel in the middle of the display). The screen's targets are tiled, their depth Z-compressed with ZCull; vertex programs stay in the RSX's 512 slots; RSX timestamps time each part of a frame. The game's thread takes the RSX from the display thread when it creates its device; then the Direct3D thread (`xbox/d3d8_rsx.c`) calls it. Its own context callback runs before the system's, which sends each full segment of the command buffer to the RSX: the SPU's conversions first. |
| `src/ps3_rsx_vp.c`, `ps3_rsx_fp.c`, `ps3_rsx_asm.c`, `ps3_rsx_program.h` | RSX microcode made at run time: the game's NV2A vertex programs, and its pixel shaders (texture stages and register combiners), encoded as `cgcomp` encodes them. An optimizing pass shrinks each pixel shader before the encoding: numbers folded, copies read at their source, what nothing reads dropped, the registers packed; a combiner write's clamp to [-1, 1] is one instruction to it, read through where the reader makes it redundant (a saturate, a `MAX` with 0..1) and else an RSX `MAX` and `MIN`; each stage keeps its own ab, cd and sum until the packing; an `ADD` of a product becomes a `MAD`, steps of numbers on one value one `MAD`, `(x + 1) * b` `MAD(x, b, b)`. The translation's temporary buffers are fixed (one translation at a time), not the heap's. |
| `src/ps3_input.c` | DualShock 3 and Sixaxis pads on all 7 ports: buttons, sticks and pressures, and the motors. |
| `src/ps3_audio.c` | One 48 kHz stereo float audio port, fed from a ring by a thread. `xbox/dsound_ps3.c` opens it, and `ps3_host_exit` stops it. |
| `src/ps3_spu.c` | The SPU threads: `sysSpuInitialize` (6 SPUs), the DirectSound mixer's SPU thread group and its jobs. A job's bell is a store to a 128-byte line that the SPU holds a reservation of; the SPU's event (port 16) ends the mixer thread's wait on an event queue, which also looks at the SPU's echo each 1 ms. A job that is not done in 100 ms stops the SPU, and the PPE mixes from then on. The Direct3D thread's conversions are a second SPU thread group with a ring of 1024 tasks: a task's store to the bell, and a wait (a short spin, then sleeps) until the SPU's echo shows the tasks done. A wait of 100 ms stops that SPU: the PPE does the tasks left and converts from then on. The game thread's jobs (`spu/spu_game.c`) are a third SPU thread group with a ring of 64 jobs, started before the game: a job's store to the bell, a ticket, and a wait (a short spin, then sleeps) until the SPU's echo passes it. A wait of 100 ms stops that SPU: each caller then does its job on the PPE (`ps3_spu_game_wait` gives 0). The render's jobs are a fourth SPU thread group, the same program with a ring of its own (`ps3_spu_render_*`). `ps3_host_exit` stops the four thread groups. |
| `spu/spu_game.c`, `spu/spu_game.h`, `include/ps3_spu_game.h` | The game thread's jobs on two SPUs: the game SPU's (the checkpoint's copies, the particles' rays) and the render SPU's (the rest). Exact: the checkpoint's copies (`game_state_xbox.c`: the state's used parts into the copy, the unused middle checked to be zeroes, a revert's copy back and zeroes; 16 KB DMAs in four buffers, each fenced in its tag; the parts' 16-byte lines on the SPU, the bytes at their edges on the PPE, the copy's GPU part at its source's alignment). Render only (the SPU's floats round toward zero; its float constants too): an object's shadow's surfaces (`structure_visibility_build_surfaces`, sorted, with their triangles; started when the shadow begins and taken before its triangles: `render_objects.c`, `structure_render.c`; one in 64 searched by the PPE too, as a check), the window's weather (the particles' update, visibility and sprites into the sprite groups' vertices, started at the window's start and drawn where `weather_particle_systems_render` was: `weather_particle_systems.c`, `render.c`), the effects' free particles' rays (`point_physics_update`'s three rays through `collision_test_vector`'s structure and water tests, gathered by `particles_update` after their forces, a job each 64 sent while it gathers the next, and taken with their contacts at the next frame's `particles_update`, a frame late, or dropped before a BSP switch, a map's end or a game state's load: `particles.c`, `point_physics.c`, `collisions.c`, `game_state.c`); the window's visible surfaces (the subclusters' walk after the portals, the surfaces ascending and their triangles, taken by `structure_render_preprocess`: `structure_visibility.c`, `structure_render.c`), the dynamic lights' surfaces (a job for each lights pass from after `lights_preprocess_scene`, each light's search, sort and triangles: `object_lights.c`, `structure_render.c`), the effects' particles' sprites (`render_particles`' groups but the first person's, their size cuts, fades and `build_sprite`: `render_particles.c`, `render_sprite.c`) and the contrails' strips (`render_contrails.c`), each from after the lights' part to its draw. The structure BSP's data is read through a 64 KB cache of 128-byte lines (2 ways), kept while the BSP is the same (`scenario_structure_bsp_generation`). A job the SPU cannot take (sizes past its room) is refused, and the PPE does it. |
| `spu/spu_vertices.c`, `spu/spu_vertices.h` | The SPU's part of `xbox/d3d8_rsx.c`: a draw's vertex elements to float4s (every Xbox element type, as `vertex_element_convert` gives them, bit for bit: a division by 2^n - 1 in integers, from the quotient's repeating binary digits) and its indices less a base, from the record ring or the tag cache by DMA, into RSX local memory (the scratch, the vertex cache); and a pixel shader's copy with its constants (`src/ps3_gpu.c`'s `ps3_gpu_set_fragment_program`: up to 11 values and 16 KB, else the PPE copies it). One fetch brings a stream's elements; the next fetch comes while one converts, four vertices at a time; a task's output goes out while the next task runs, its echo fenced behind it. |
| `src/ps3_vmx.c`, `include/ps3_vmx.h` | Exact VMX for the simulation, the one unit built with `-maltivec`: each lane runs the scalar code's float operations in its order (a product is a multiply-add of -0), with VSCR's NJ off. The first call compares VMX with the FPU on denormals; if they differ, the callers use the scalar code. `fast_vector_intersects_sphere` for four spheres at once, for `port/linux/game/object_bounds_cache.c` (the rays' object loop in `collisions.c`, which reads each cluster's objects and spheres from a block kept while the lists and the spheres do not change); `matrix4x3_multiply` (`matrix_math.c` calls it): a row in each vector, no loop, no store read back at once. And memory streams (`ps3_vmx_copy`, `ps3_vmx_zero`, `ps3_vmx_zeroes`) for the checkpoint's copy and its revert: each source line touched 1 KB ahead (`dcbt`), each destination line written whole zeroed in the cache first (`dcbz`, not read from memory) when the first call finds that `dcbz` zeroes exactly the 128-byte line (`[vmx] dcbz zeroes N bytes`), else plain stores. |
| `spu/spu_mixer.c`, `spu/spu_mixer.h` | The SPU's part of `xbox/dsound_ps3.c`: each job decodes the packets a 256-frame chunk needs (DMA from the game's sound cache, floats into the packets' buffers) and mixes the streams' voices into the chunk, with the PPE mixer's float operations in its order (no fused multiply-adds). |
| `src/ps3_posix.c` | The part of `port/linux/src/posix.h` that `msvc_crt.c` calls. |
| `src/ps3_net.c` | The sockets of `xbox/xnet_ps3.c`: libnet's own calls (their error is each thread's, not newlib's one `errno`), `sin_len` set, `SO_NBIO` for non-blocking sockets, select on `netPoll` with Winsock's readiness (a failed connect in the error set), the PS3's address from netctl (asked once a second), random numbers from lv2, DNS (`netGetHostByName`, one name at a time). Errors come back as Winsock codes. |
| `src/ps3_upnp.c` | The UPnP client of internet play (`posix.h`): it asks the router to forward the tunnel's UDP port, and removes the forwards that no copy of the game uses. |
| `src/ps3_mbedtls.c`, `include/ps3_mbedtls_config.h` | Mbed TLS 3.6 (`port/third_party/mbedtls`) for the DTLS of `port/linux/src/posix_dtls.c` (internet play with browsers): lv2's random numbers and time, no files or sockets. |
| `src/ps3_setjmp.S`, `include/setjmp.h` | A 64-bit `setjmp`/`longjmp` in place of the newlib one, which overflows `jmp_buf`. |
| `src/ps3_timers.c`, `include/ps3_timers.h` | The timer build: the game thread's time by phase, which the game, the Direct3D device and `src/ps3_gpu.c` add up, the Direct3D thread's time, and the RSX's time by part from its timestamps, logged every 300 frames. |
| `src/ps3_heap.c` | The heap log of the `[mem]` heartbeat: the Makefile's `--wrap` sends `malloc`, `calloc`, `realloc`, `memalign` and newlib's `_sbrk_r` here; each growth of the heap and each allocation of 64 KB or more is kept (under newlib's malloc lock, which a request holds around the call) with the request and its return addresses. |
| `xbox/ps3_platform.h` | Force-included in the Xbox layer, in place of `port/linux/src/platform.h`. |
| `xbox/xbox_kernel.c` | Handles, events, mutexes, threads, waits, APCs, time, `GlobalAlloc`. One lv2 lock and condition variable for all waits. |
| `xbox/xbox_memory.c` | `XPhysicalAlloc`. The Xbox window at `0x80000000`, as on the Xbox. |
| `xbox/xbox_files.c` | The Win32 file API and the Xbox paths. |
| `xbox/xbox_tag_swap.c` | The swap of the map data (header, tag data, structure BSPs) to the PS3's byte order when the game reads it. Its layouts are `xbox/xbox_tag_swap_tables.h`, which `tools/ps3_tag_swap.py` writes from Invader's tag definitions (GPLv3). |
| `xbox/d3d8_rsx.c` | The Direct3D 8 device on `src/ps3_gpu.c`, from `port/linux/src/d3d8_gl.c`: state, vertex shaders, render targets as RSX surfaces (the screen's at the display's resolution, EXTRA OPTIONS), a texture cache in RSX memory (converted again when the texels change; the button icons' textures in their bitmaps' places, `xbox/hud_hires_rsx.c`; the texels of the game's texture cache are checked only after it loads them again, see trap 34 in ROADMAP.md), vertices converted to floats: the map's once, into a vertex cache in RSX memory that each map or BSP load empties, the models' indices with them; the rest at each draw. The game's thread records the calls in a 4 MB ring (state changes, draws with the vertices and indices that are not in the tag cache, the present); the Direct3D thread (priority 1000, the PPE's other hardware thread) runs them one frame behind. A texture's `Lock` holds its last draw (`IsBusy`, the texture cache's evictions). The vertex conversions, the vertex cache's and the index copies are SPU tasks (`spu/spu_vertices.c`): each flush of the RSX's commands (the present, a wait for the RSX, a full segment of the command buffer) comes after them, and a record's ring room comes back once its tasks are done (a list of such records, checked while the game's thread waits for room; all of them when the Direct3D thread goes idle). |
| `xbox/xbox_texture_layout.c` | The layout of Xbox textures, and their texels for the RSX: 8- and 16-bit texels at their size where the RSX has the format (with a remap to the Xbox's channels), the rest as A8R8G8B8, in the same order (the RSX swizzles as the NV2A does); DXT blocks as they are. |
| `xbox/xinput_ps3.c` | XInput on `src/ps3_input.c`: each pad takes the lowest free Xbox port (0 to 3) and keeps it until it disconnects. The game's polls read the pads once a frame. |
| `xbox/dsound_ps3.c` | DirectSound on `src/ps3_audio.c`, after `port/linux/src/dsound_sdl.c`: a mixer thread (priority 1000) keeps the port's ring 43 ms ahead. Each mixer pass takes the streams' unfinished packets with the lock held, decodes (Xbox ADPCM or PCM, little-endian, to floats into a 128-aligned buffer `Process` takes: 16, 32 or 64 KB, cut from blocks of its own, a first of 4 MB and then 1 MB more, never freed; a buffer given back is kept for the next packets) and mixes them without it, and gives the results back with it. The decode and the mix are an SPU job (`spu/spu_mixer.c`); the mixer thread computes the gains and waits for the job, or does the work itself when the SPU is off; a flush during a pass voids the pass's results for that stream, and its buffers are given back after the pass. The packets complete on the game's thread, in `DirectSoundDoWork` and `Flush`. Buffers loop (the loading screen's tones), mixed with the lock held. A voice's settings (volume, pitch, position) are stored without the mixer's lock: the mixer reads them once a chunk. 3D voices get the Linux build's distance and pan. |
| `xbox/xnet_ps3.c` | Winsock and XNet on `src/ps3_net.c`, from `port/linux/src/xnet.c` without the latency test: Winsock's addresses, options and select, `network.address` and `network.broadcast` from `config.toml` (as on Linux), an XNADDR with the PS3's address, `WSAGetLastError` as the thread's last error (as on Windows), internet play's peers through their stand-ins. It writes `brokers.txt` at each start. |
| `xbox/posix_ps3.c` | The sockets, random numbers and DNS of `port/linux/src/posix.h` on `src/ps3_net.c`, for internet play (`port/linux/src/p2p*.c`). |
| `xbox/ps3_pthread.c`, `xbox/include/pthread.h` | The mutexes and threads of `port/linux/src/p2p*.c` on the host layer: newlib has no pthreads. |
| `include/halo_ps3_kcp.h` | KCP's 64-bit integers as `int64_t` (in the Xbox layer, `long long` is `int int`). |
| `xbox/port_stubs.c` | `display.interpolation` (FRAME RATE), and the Linux build's services that the PS3 does not have yet: the PC menus, the high-res HUD and text. |

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

`init.txt` in the data root holds console commands that the game runs at
start-up, one a line. For example, `map_name levels\b30\b30` starts that
campaign level without the menu.

The game writes these files:

| File | Contents |
| --- | --- |
| `/dev_hdd0/data/HaloCE/config.toml` | The settings, with the defaults at the first start |
| `/dev_hdd0/data/HaloCE/brokers.txt` | The MQTT brokers of internet play, written again at each start (`port/assets/network/brokers.txt`) |
| `/dev_hdd0/data/HaloCE/z/` | The Xbox `z:\`: the map cache (`cache000.map` to `cache005.map`, about 730 MB) and `savegame.bin` (a checkpoint's state: its used parts copied into main memory at the checkpoint, the unused rest zeroes, written by a thread of its own, a revert taken from the copy: `game_state_xbox.c`; the copy and its check of the zeroes with `ps3_vmx.c`'s streams) |
| `/dev_hdd0/data/HaloCE/u/` | The Xbox `u:\`: the user data |
| `debug.txt` in the data root | The game's log |

## Log

The PS3 sends its log as UDP datagrams to port 18194 of `LOG_IP`. To see it,
enter `nc -ul 18194` on that computer. `printf`, `puts` and `putchar` also
go to the log (`-Wl,--wrap`), and so do the lines of the game's
`debug.txt`.

## The EBOOT (G7)

| Part | In the log | On the PS3 |
| --- | --- | --- |
| System | `[ps3]` lines: the build time, `sizeof` of `void*`, `long` and `wchar_t`, the byte order | |
| Memory | `[mem]` lines: the free memory at boot, after the memory window and after the RSX init (the container probe, and lv2's count from syscall 352); the mmapper results. Every 10 s (G7): the free main memory from lv2 and its lowest since the game started, the Xbox window's mapped MB, newlib's heap, and the RSX's local memory by use (display, textures, targets, vertices, scratch, programs) with its peak. `[heap]` lines with them (`src/ps3_heap.c`, the Makefile's `--wrap` of `malloc`, `calloc`, `realloc`, `memalign` and `_sbrk_r`): each growth of newlib's heap and each allocation of 64 KB or more since the last, with the request's size and five return addresses (`ppu-addr2line` on the ELF names them; the game's allocations pass through `debug_malloc`, `system_malloc` and `GlobalAlloc`) | |
| Display | `[vid]` lines; `[hb]` every 10 s | A slow blue pulse until the game creates its Direct3D device |
| Game | `[halo]` lines: each memory window block, each file the game opens, each thread; the game's own `debug.txt` lines. Each file transfer of 1 MB or more with its time, and at the close of a file with 1 MB or more of transfers, their totals and the `fsync` time (G7: the saves). A written file gets an `fsync` at its close (`fsync failed` on an error). `free space of ...: N MB` at each save check of the game (sys_fs) | |
| Map data | `tag data swapped in N ms: ...` and `structure BSP swapped ...`, with the counts and the errors | |
| Pads | `[pad] port N connected`; `XInput:` lines: the Xbox port of each pad, the game's open and close, and the buttons the game gets at each change | The D-pad and the left stick move in the menus, CROSS chooses (A), CIRCLE goes back (B) |
| Drawing | `[gpu] ready` and the RSX local memory; `[gpu] the command buffer's segment` (its start, end and size, the system's callback); `screen: 852x480 drawn at 1280x720` (or `640x480 drawn at 960x720`) at the start and at each change of WIDESCREEN; `[gpu] target ... tiled` for the screen's targets (`ZCull` on its depth); `Direct3D:` lines: `vertex and index conversions on an SPU` (or `on the PPE`) and `[spu] vertices up` at the start, each vertex shader translated, the first pixel shaders, and every 600 frames the draws, the skips, the texture uploads, the textures in RSX memory (and the part no draw used in 600 frames), the vertex cache (the vertex elements converted into it and at draws, its use) and the conversions' SPU (its tasks, elements and indices, its time a frame with its waits for input and output, the waits that found tasks not done and their time a frame), the elements converted by type (the XDK's `D3DVSDT_` values: 0x32 FLOAT3, 0x22 FLOAT2, 0x40 D3DCOLOR, 0x16 NORMPACKED3, 0x21 NORMSHORT2, ...); `[spu] vertices: ... SPU off` if a task is not done in 100 ms; `[spu] the game thread's jobs run on an SPU` (or `the PPE`) and `[spu] game up`, `[spu] the render's jobs run on an SPU` and `[spu] render up` at the start, `[spu] game: ... SPU off` (or `render:`) if a job is not done in 100 ms, `[halo] checkpoint: ... copied by an SPU` (or `the PPE`) at each checkpoint; `[gpu] frame N` every 600 frames: the vertex programs uploaded and switched, the fragment programs (and of them the SPU's copies) and the scratch use | The game's picture at the display's lines: 16:9 over the whole display, or 4:3 in its middle |
| Timers | `[time]` lines every 300 frames (`src/ps3_timers.c`): the game thread's ms a frame by phase (game ticks, render and its parts), the ticks' parts in ms a tick (`game.c`, `objects_update`'s in `objects.c`), `[time] node matrices` (`object_compute_node_matrices`'s steps at every call: the animation's orientations, the overlays and blends, the matrices (the first node, the validity checks, the other nodes, the bounding sphere); the postprocess; its calls and nodes a tick), `[time] particles` (the game frame's particles part: `particles_update` with its particles' physics and counts, `contrails_update`, `particle_systems_update`), `[time] point physics` (`point_physics_update` at every call: its wind and its rays, of them those that test no surface (the weather's), its calls and rays a frame), `[time] ai` (`ai_update`'s conversations, encounters and actors; each actor's update by step: `actors.c`; the actors a tick), `[time] ai parts` (the control and movement steps by call: `actors.c`; `actor_perception_update`'s refresh, danger zone and props loop, with its props and their position and status refreshes a tick: `actor_perception.c`), `[time] ai line of sight` (`ai_test_line_of_sight`: the first ray, the fog, the mode's rays, the first rays' length: `ai.c`; its rays' structure and objects parts with their bsp nodes, leaves, clusters and objects tested: `collision_usage.h`, `collisions.c`), `[time] ai line of sight objects` (the rays' loop over a cluster's objects: the copies' iterator, the tests, the objects walked, read from kept blocks, passed and tested: `collisions.c`, `object_bounds_cache.c`), `[time] action control` (`actor_action_control` by action: `actions.c`), `[time] ai move` (`actor_move_update`'s parts, and `actor_path_refresh` wherever it runs, with its path builds: `actor_moving.c`), `[time] ai paths` (a path build's setup, search with its nodes, and path made; the flying and cached builds), `[time] ai avoidance` (the flying vector avoidance's setup with the objects it found, its rays' structure tests and object tests), `[time] ai decisions` (the decisions step by call, the decision loop's decide and perform: `actors.c`), `[time] ai path search` (the search's reset, begin, traverse with its pops, edges with the surfaces, edge loops and closest points, the debug copy; the path made's smoothing and obstacles: `path.c`), `[time] ai path smoothing` (its pill tests, turning points and surfaces: `path_smoothing.c`), `[time] vehicles` (`vehicle_update`'s physics, effects, stopping and animation: `vehicles.c`), `[time] physics` (`physics_update`'s forces with the mass points, movement, units' collisions, old physics: `physics.c`), `[time] old physics` (`physics_update_old`'s location with the mass points, ground plane, water depth, antigravity probes with their count, the move: `physics.c`), `[time] ground planes` (`compute_ground_plane`'s features in the sphere with the features found, and the point test, for the new and old physics), `[time] ground planes' features in the sphere` (in those queries alone: the bsp's leaves with the calls and the leaves a call, the structure's features, the leaves' clusters' objects with the clusters and the objects walked and read from kept blocks, their tests: `collisions.c`), the game's part of each frame (`game_frame`), the render's objects and effects passes by part, `[time] render_model but the shadows` (`render_model`'s node matrices with the nodes a model, begin, parts and end, each with the game thread's Direct3D draws and constants in it, the models a frame: `models.c`; the objects' widgets and `render_objects`' Direct3D: `render_objects.c`), `[time] render_model's parts' draws` (in the parts' draws: the solid draws of model and environment shaders, the decals' draws, the transparent submits, each with its count and Direct3D, and the loops: `models.c`), `[time] lights and diffuse passes` (their parts with their Direct3D: the lens flares' occlusion tests, the objects' shadows, the lights' diffuse part, the light and alpha-tested decals; the structure's diffuse textures, the primary and secondary decals: `render.c`), `[time] objects' shadows` (the pass's begin and end, the objects' checks and lighting, each shadow's begin, model, volume, structure (the surfaces found, their triangles, their draws: `structure_render.c`) and end, each with its Direct3D: `render_objects.c`), `[time] weather` (the weather's update with its particles, in it each particle's acceleration and `point_physics_update`; visibility tests, sprites built with their count, their draw: `weather_particle_systems.c`), `[time] transparent geometry` (the sort with the groups, in it the qsort and the port's model order, the groups' begin and end, each group drawn by its shader's type with the groups: `rasterizer_transparent_geometry.c`), `[time] unit parts` (each part of a type update, ms a tick: `object_types.c`; `biped_update`'s steps: `bipeds.c`), `[time] sound` (`sound_render`'s steps: `sound_manager.c`; `game_sound_update`'s: `game_sound.c`; the game thread's waits for DirectSound's lock; the sounds' obstruction calls and those computed, not from the cache), `[time] collision` and `[time] collision by user` (the outermost collision tests' ms a frame by kind and by the user on top of the game's collision-user stack, with the tests a frame: `collision_usage.c`, `collisions.c`, `collision_models.c`), `[time] collision by function` (each logged collision function's ms and calls a frame at any depth, the bsp nodes its tests entered: `collision_bsp.c`; the objects the rays tested themselves, after the copies of their spheres, `object_bounds_cache.c`; `[vmx] exact` or `not exact` at the first test), `[time] interface pass` (its parts: `render.c`; the HUD's: `interface.c`, `hud.c`; the game thread's time in the Direct3D draws and vertex constants, without its waits for the ring), `[time] screen quads` (the quads a frame and the parts of `_rasterizer_psuedo_dynamic_screen_quad_draw`; the interface pass's Direct3D draws and constants), `[time] HUD parts` (the weapon HUD's and the unit HUD's parts with their quads: `hud_weapon.c`, `hud_unit.c`, `motion_sensor.c`), the Direct3D thread's (busy, the game thread's waits for it, its draws and waits, the SPU's conversions among them), `[time] game SPU, render SPU` (the game thread's SPU jobs by kind: copies, shadows, weather, particles, lights, visibility, sprites, contrails; jobs a frame, the SPU's microseconds a job and its DMA waits, the game thread's waits in ms a frame; the tag data cache's lines found and fetched, the jobs refused; the shadows checked against the PPE's search, their PPE time, those apart: `ps3_spu.c`, `structure_render.c`), and `[time] RSX`: the RSX's ms a frame and by part, from timestamps the RSX writes at each split (no wait); `[time] specular part` (the specular part's splits: `render.c`'s lights, specular lightmaps, reflection masks, mirrors, reflections, transparent and fog, on the game thread and on the RSX with the RSX's draws a frame); `[time] specular part's Direct3D draws and constants` (each split's share of the game thread's Direct3D draws and constants); `[time] specular part's pixel shaders on the RSX` (in one frame of eight the RSX stamps each of the part's draws: the eight pixel shaders with the most RSX time, ms and draws a stamped frame, the split, the program's 16-byte slots (instructions and inline constants) and registers, the combiners, the texture modes and the last draw's textures: `d3d8_rsx.c`, `ps3_gpu.c`) | |
| Sound | `[aud] port N` at the start; `[spu] mixer up` and `DirectSound: the streams decode and mix on an SPU` (or `on the PPE`); `DirectSound:` every 10 s: the streams and buffers that play, the packets decoded, the mixer thread's time per 5333 us chunk (with its wait for the SPU; of it its own decoding, and the lock held), the port blocks played silent or partial (underruns), and the SPU's time per chunk, its decoding, the mixer thread's wait, how the waits ended (the SPU's event, the 1 ms timeout, an error) and the events lv2 refused the SPU; `[spu] mixer: ... SPU off` if a job is not done in 100 ms; `[spu] mixer down`, `[spu] vertices down`, `[spu] game down` and `[spu] render down` at the exit | The game's sound on the PS3 audio output |
| Network | `Winsock: PSL1GHT sockets, this PS3 at a.b.c.d` at the start (or `no network`); `[net] <call>: lv2 error N` for the first 32 socket errors that are not would-block; `Winsock: SO_RCVBUF of N bytes refused` with the size lv2 took; the game's own `network` and `transport` lines; `Internet play:` lines (the brokers, STUN, UPnP, joins); `menus: the multiplayer menu has LAN PLAY and INTERNET PLAY; SETTINGS has EXTRA OPTIONS` | The PC builds' games on the LAN in LAN PLAY's list; public internet games in INTERNET PLAY's list |
| Exit | `[ps3] exit 0` | PS button > Quit Game goes back to the XMB |
| Failure | `EXCEPTION halt` and the call stack: `./build.sh symbols ADDRESSES="..."` names its functions | A red pulse until PS button > Quit Game |

## Extra options (G8)

SETTINGS in the main menu opens a screen of two items (`menu_tags.c`,
`xbox_settings_wire`): PLAYER PROFILES, the Xbox's profile screens, and
EXTRA OPTIONS. EXTRA OPTIONS has three rows, at the places of ADVANCED
CONTROLS' first three. A or START keeps the changes in `config.toml`, and
they apply from the next frame; B goes back without them.

| Row | Values | Setting |
| --- | --- | --- |
| WIDESCREEN | ON (the default): 852x480 of the game, drawn at 1280x720 over the whole display. OFF: the Xbox's 640x480, drawn at 960x720 in the middle | `display.screen_width`: 0 for the display's shape, 640 for 4:3 |
| FRAME RATE | 60 (the default): a frame at each refresh, blended between the game's 30 ticks a second (`port/linux/game/render_interpolation.c`). 30: a frame a tick, as on the Xbox | `display.interpolation`: true or false |
| BUTTON ICONS | XBOX: the maps' own buttons. STYLE A (the default): the PS3's, colored circles with white symbols. STYLE B: black circles with colored symbols | `display.button_icons`: `"xbox"`, `"style_a"` or `"style_b"` |

The button icons (`xbox/hud_hires_rsx.c`) stand for the menus' button
bitmaps (`a_butn`, `b_butn`, `x_butn`, `y_butn` and the small A and B) and
for the A, B, X and Y sprites of the message icons (`hud_msg_icons_sm`, the
"Press A" icons of the menus' and the HUD's text): CROSS for A, CIRCLE for
B, SQUARE for X, TRIANGLE for Y. The message icons are stencils, white with
the symbol cut out, which the game tints as the Xbox's; both styles share
them. The other message icons (the triggers, the sticks, the black and
white buttons) stay the maps'. The art is `ps3_buttons/svg` (made by
`ps3_buttons/make.py`); `tools/ps3_button_assets.py build` draws it at 4
times the bitmaps' size into `port/assets/buttons/ps3`, and the Makefile
embeds those (`embed`: swizzled A8R8G8B8 with every mip level). A bitmap
whose pixels are not the English maps' (its CRC) keeps its own.

At 60 frames per second the sounds update at the ticks' rate, as on the Xbox
(`main.c`, `game.c`). A sound source that has not moved keeps its
obstruction, a ray from the camera, for up to 4 ticks (`game_sound.c`).

The game keeps its 480 lines of units, as the other ports do: the targets
the size of the screen (and the reflections' and active camouflage's
secondary target) get the display's pixels for each unit, and the menus stay
640 units wide in the middle. A TV without 720p gets its own mode, at its
lines; a 4:3 mode has no widescreen.

## System link (G7)

The PS3 plays system link with the PC builds of the same source on the LAN.
Both must have the same network version (`HALO_PORT_NETWORK_VERSION` in
`port/linux/include/halo_port_limits.h`, 29 now) and the same maps. The game
uses UDP ports 5150 (host) and 5151 (clients), with broadcasts to find games,
and TCP port 5150.

On the wire, message headers and the typed packet fields are big-endian: the
PS3's own order, which the PC swaps to. The rest is the PC's little-endian
memory: the game advertisement, the game settings record, the distributed
netcode's messages (co-op's, the AI's, voice chat's and map sharing's too)
and the game type's state in them. The PS3 swaps those when it sends and
when it receives (the `*_byte_swap` functions, under `__BIG_ENDIAN__`). A new
or changed field in them, or a new message kind, needs its swap line.

When the broadcasts do not reach the other machine, list its address, or the
LAN's broadcast address, in `network.broadcast` in `config.toml`, on both
machines (for example `network.broadcast = "192.168.178.255"`).

In the multiplayer menu, LAN PLAY is in the place of SYSTEM LINK PLAY
(`port/linux/game/menu_tags.c`, `xbox_multiplayer_patch`). It opens a copy of
the System Link game list without the "press START to join" screens. The
list shows only the games of the LAN: the hosts that internet play reaches
are in INTERNET PLAY's list. A on a game joins it. Y, or A when the list has
no games, hosts a game (CREATE GAME). Only player 1 plays, with player 1's
profile (the active profile, else the last used, else the first): the
"press START to join" screens of the other pads are not on this path.

## Internet play (G7)

Internet play is the PC builds' (`port/linux/src/p2p*.c`; see
`port/linux/README.md`, "Internet play"). In the multiplayer menu, INTERNET
PLAY is after LAN PLAY. It opens a copy of the System Link game list, which
shows the public games of the server browser, 9 rows at a time (the rows
scroll at their ends). A on a game joins it: the PS3 reaches the host, then
the pregame screen opens. Player 1's profile plays, as in LAN PLAY.

- `network.online = false` turns internet play off, and
  `network.public_lobby = false` the game list.
- The game asks the router to forward the tunnel's port (UPnP,
  `network.allow_upnp`).
- Players in a browser connect through WebRTC (DTLS with Mbed TLS).
- The list refuses games with a password (no screen types it) and closed
  games.
- A game that the PS3 hosts is not in the server browser. Its invite link is
  in the log (`Internet play: hosting.`).
- Discord does not operate: the PS3 has no Discord client.

### Invite codes

An invite code is 8 digits that stand for an invite link. The PS3 cannot
paste a link, so it uses codes. A code is kept on the MQTT brokers of
internet play for 30 minutes. The brokers do not see the link: it is
encrypted with a key that comes from the code (`p2p_code_*` in
`port/linux/src/p2p_crypto.c`).

- To join a game with a code: in INTERNET PLAY, press Y (ENTER CODE), type
  the 8 digits on the keyboard, and select Done. The PS3 finds the link on
  the brokers and joins the game, as for a game in the list.
- When the PS3 hosts a game, the pregame screen shows its code ("Code: 1234
  5678", right of "Choose teams using D-pad"). The code stays on the brokers
  while the PS3 hosts. The log has it too (`Internet play: this game's invite
  code is`).
- The converter page, `port/ps3/invite/index.html`, makes a code from a PC's
  link, and finds the link of a PS3's code. It is one file with no other
  files: publish it with GitHub Pages, or open it from the disk.

Anyone who has a code can join the game while the code lasts.
- The game does not run its `atexit` functions when it stops: a hosted
  game's listing stays at the brokers until it expires, and a UPnP forward
  until its hour ends.

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
