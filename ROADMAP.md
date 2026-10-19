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
- Upstream merges (2026-10-16): `port/ps3/xbox/` is tracked now (`git add -f`,
  `ec01a30`). The remote `upstream` is OpenCE's GitHub repository; the local
  graft `git replace --graft 4b4874c e3389991` joins the two histories.
  OpenCE `main` at `8102e711` is merged (`63949dd`). The next merge: in a
  worktree, `git -c core.autocrlf=false merge upstream/main` (the system
  `autocrlf=true` would write CRLF), then a PS3 build and test.
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

## Now (2026-10-19)

- Handoff (2026-10-19, end of the session; Matt: "Update ROADMAP and
  GOALS. I'll start a new session."): G8 is in progress. The last PKG is
  11:40 in `build/ps3_g8/` (run on hardware: a clean exit, no sound
  fault). Not committed since `efa94804` ("Widescreen + 60 FPS"): step 5
  (the Direct3D thread), the sound off the game thread, the obstruction
  cache of 4 ticks and all the timers (33 files in `git status`). The
  listener on UDP 18194 is stopped. Next, Matt's choice: 1. the HUD timed
  inside an element (`hud_draw_static_element`, `hud_draw_meter`,
  `hud_draw_numbers`: 40 to 140 us each outside the draw); 2. the rays'
  objects part (1.07 / 2.91 ms a frame): the objects out of a ray's way
  passed over on a packed copy of their bounding spheres (exact, as
  `object_bounds_cache.c`), or exact VMX; 3. the first SPE task, the
  DirectSound mixer, on flycastPS3's SPU set-up; 4. the obscurer ray
  (`rasterizer_models_begin`) once a tick. The SPEs and VMX: "G8 analysis"
  ("SPEs and VMX") and GOALS.md ("SPEs and VMX (candidates)").
- 2026-10-19: G8, the fixes (Matt: "Just go with the fixes. We'll measure
  from top-down then."; WIDESCREEN and FRAME RATE in the video settings,
  "Inside SETTINGS", defaults widescreen and 60): built, not tested. 60
  frames per second (interpolation), the game at 1280x720 (16:9) or 960x720
  (4:3), the Direct3D layer's CPU cuts, tiled targets with Z compression and
  ZCull, native 8- and 16-bit textures, resident vertex programs, RSX
  timestamps; half-precision shaders built and left off (the PC check). PKG
  of 00:13 in `build/ps3_g8/`. See "G8 analysis" ("The fixes").
  Hardware FAILURE (Matt: "There's a blue screen."): the RSX did not finish
  the game's first frame (frame label 0); the display thread's blue stays.
  Diagnostic PKG of 00:50 in `build/ps3_g8/` (`RSX_CHECKPOINTS` in
  `ps3_gpu.c`, frame labels in `ps3_video.c`): the timestamp report (type
  0x10) stops the RSX. Fixed (type 1, trap 44), the diagnostic still on:
  PKG of 01:00. Hardware SUCCESS (Matt: "It runs!"): menus 59.9 frames
  per second; a30 54 to 60 calm, 33 to 37 in heavy combat (game thread
  about 28 ms: ticks 11 to 16 ms each, render 15 to 20; RSX 14 to 18 ms).
  No change of WIDESCREEN or FRAME RATE in the log. Clean PKG (diagnostic
  off) of 01:14 (committed by Matt, "Widescreen + 60 FPS"). Step 5 (Matt:
  "Let's go with Step 5."): the Direct3D thread. Hardware: runs, a30 heavy
  combat 38 to 47 (Matt: "The framerate still feels sluggish."). Not to
  the SPEs or VMX (network results must match the PC's; VMX later, for
  loops that only draw). Built, not tested (Matt: "Do the measurement.
  Also build the audio on tick frames."): the tick's timers and the sound
  at the ticks' rate, PKG of 02:25: tested (the tick's parts below, Log).
  Built, not tested (Matt: "update the sound then expand the measurements
  for the objects_update, and the render's objects and effects passes."):
  `game_sound_update` at the ticks' rate, DirectSound's settings without
  the mixer's lock, the timers of `objects_update` and of the two passes;
  PKG of 02:50: tested (Log). Built, not tested (Matt: "Make the immediate
  mode cheaper."): immediate mode keeps each vertex's written registers
  only; PKG of 03:16: tested, the HUD's time the same (Log). Built, not
  tested (Matt: "Prepare the measurements for 1, 2 and 3, so we can
  investigate all 3 issues and have a median."): the timers in the sound,
  the units' update and the interface pass; medians of the play windows
  after the run. PKG of 03:42 in `build/ps3_g8/`: tested, medians in the
  Log. Next: Matt's choice of three steps (Matt: "I'll get back to it
  tomorrow morning."). See "G8 analysis" ("The measurements after step
  5"). Matt chose all three (his answer to the choice). 1: the
  collision tests' time by user and by kind; 2: the stream packets
  decoded on the mixer's thread, and the mixer's lock held only to take
  and give back a pass's packets (not for a whole chunk); 3: the screen
  quads' parts and the interface pass's Direct3D calls. PKG of 10:59 in
  `build/ps3_g8/`: run on hardware (Matt: "Done."), a30, no halt, a clean
  exit, 0 silent or partial port blocks; `sound_render` 0.30 / 0.70 ms a
  frame (before 1.10 / 2.15); the collision tests 3.63 / 5.89 ms a frame,
  of it the sounds' obstruction rays 1.60 / 1.94; the screen quads only
  0.36 of the HUD's 1.83 ms. Medians in the Log. No sound fault (Matt: "I
  didn't hear any sound faults."). Next steps 1 to 3 (Matt: "Go with 1-3.
  Be aware that we can also delegate tasks to the SPEs."). 1: a source
  that has not moved keeps its obstruction up to 4 ticks; 2: the rays
  timed inside (each logged collision function, the bsp nodes, the
  objects tested); 3: the HUD's parts timed with their quads. PKG of
  11:40 in `build/ps3_g8/`: run on hardware (Matt: "Done."), a clean
  exit; the sounds' rays 0.58 / 0.93 ms a frame (before 1.60 / 1.94); the
  rays' objects part 1.07 / 2.91 ms a frame (1235 / 5655 objects tested);
  each HUD element 40 to 140 us outside its quad's draw. Medians in the
  Log. Matt: "Write the SPEs parts and the vmx inside GOALS and ROADMAPS
  in case. Those are extremely worth checking out.": see "G8 analysis"
  ("SPEs and VMX") and GOALS.md. Next: Matt's choice. See "G8 analysis"
  ("Steps 1 to 3 of 2026-10-19" and "The next steps 1 to 3").
- 2026-10-18: Invite codes (Matt: "Yes. Let's go with this."): hardware
  SUCCESS (Matt: "it works as intended"). 8 digits that stand for an invite link, sealed on the MQTT brokers
  for 30 minutes. INTERNET PLAY: Y (ENTER CODE) types one; the PS3 host's
  pregame screen shows its own. `port/ps3/invite/index.html` (for GitHub
  Pages) converts a link to a code and back. PKG of 22:02 in
  `build/ps3_code/`. See "Log".
- 2026-10-16: OpenCE upstream merged (620 commits, to `8102e711`): hardware
  SUCCESS (Matt: "We're in again! Perfect."). Menu, a level, saves written
  and loaded. New upstream features that report "none" on the PS3: voice
  chat, touch, Custom Edition maps, p2p passwords, per-pixel lighting,
  anti-aliasing. See the Log.
- 2026-10-16: The new network messages' byte swap (Matt: "Let's work on the
  connectivity now! Byte-swap them."): built, not tested. The merge brought
  23 distributed message kinds (network version 11 to 29). Their 19 kinds
  with multi-byte fields are swapped as the others are (trap 38): the AI's
  actor states and damage, co-op's structure BSP, presentation, events,
  device groups and states, object names, transforms, looks and screen
  effect, the damage animations, voice chat's 3 and map sharing's 4. The
  skip vote and the votekick messages are bytes. The older messages' structs
  changed only in byte fields. PKG of 01:04 in `build/ps3_swap/`. See "Log".
- 2026-10-16: Internet play (Matt: "Add 1, you can look at my Quake 3 port
  for reference to that. 2. For the screen, use the small screen in the Xbox
  menus that list the browser's games and joins one. 3. Sure, implement that
  one too."): `port/linux/src/p2p*.c`, KCP and Mbed TLS (WebRTC's DTLS, for
  browsers) compile for the PS3. The Xbox menus' multiplayer list has
  INTERNET PLAY: a copy of the System Link game list with the server
  browser's games. Discord: none (the PS3 has no Discord client). The log of
  Matt's run of 03:08 (PKG of 03:03): 4 public games joined. See "Log".
- 2026-10-16: LAN PLAY (Matt: "Can you replace the menu voice of "SYSTEM
  LINK PLAY" with a "LAN PLAY" browser? So we get both of the worlds.";
  CREATE GAME kept, his answer): built, not tested. In SYSTEM LINK PLAY's
  place, a copy of the System Link game list without the START-to-join
  screens, with the LAN's games only. PKG of 03:41 in `build/ps3_p2p/`.
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
- The vertex cache (Matt, 2026-10-06, for the low frame rate in a10):
  hardware SUCCESS (Matt: "Everything looks fine"). a10 runs at 24 to 30
  frames per second, from 9 to 10. `d3d8_rsx.c` converts the map's vertex
  buffers (models, the BSP: the tag cache) once into a 48 MB RSX block,
  which each map or BSP load empties (`cache_files.c` calls
  `d3d8_tag_cache_loaded`). Other vertex data is converted at each draw, as
  before. See "Found for later goals" (G7, a10).
- The texture hash fix (Matt, 2026-10-06): hardware SUCCESS (Matt: "the
  textures look right"). `d3d8_rsx.c` checks the texels of a texture in the
  texture cache's memory only after the cache has loaded it again (trap
  34). Other textures (the text cache, the loading screen) are still
  checked every frame. The check fell from 9 to 15 ms a frame to 0.0; a10
  runs at 29 to 30 frames per second in calm scenes, and the RSX limits the
  heavy ones (24 to 26). See "Found for later goals" (G7, a10).
- The pixel shader optimizer (Matt, 2026-10-06: "Yes, make the shader
  optimizer."): hardware SUCCESS (Matt). a10 runs at 30 frames per second
  with 0.0 ms of RSX wait in every timer window, at up to 565 draws a
  frame (see "Found for later goals", G7, a10).
  `ps3_rsx_fp.c` optimizes each translated pixel shader before the
  encoding: it folds the numbers and the clamps that the value ranges
  prove, reads copies at their source, reuses equal computations, drops
  what nothing reads, and packs the temporaries (trap 35). On the PC:
  random keys 249 instructions on average before, 26 after; the
  reflection pass 205 to 47 instructions and 23 to 5 temporaries; the
  simplest shader 60 to 3. The probe frames are off; the three other
  `[time]` lines stay.
- G7 started (Matt, 2026-10-06: "We will now work on G7."). The analysis is
  done: see "G7 analysis". Step 1, the measurement build (Matt: "Do the
  measurement build."): run 2026-10-06 with the PKG of 21:03, results in
  "G7 analysis" ("Measurement run").
- The a30 red screen (Matt, 2026-10-06): a halt at the first recorded
  animation (`recorded_animations.c,#354`), in every map. Fixed (trap 36);
  the dump scan found no other fault. Hardware SUCCESS with the PKG of
  19:53 (Matt: "Perfect, it worked!"; no listener ran). That PKG also holds
  the measurement build.
- G7 step 2, the known save items (Matt, 2026-10-06: "Do 1 then."): built,
  PKG of 21:03, with the measurement build. `GetDiskFreeSpaceExA` gives the
  real free space of the HDD: `ps3_host_disk_space` loads the sys_fs module
  at its first call and asks `sysFsGetFreeSize` for the mount point
  (`/dev_hdd0`). Each written file gets `sysLv2FsFsync` at its close
  (`file_destroy` in `xbox_files.c`), and so does a `CopyFileA` copy.
  `z:\savegame.bin` (the checkpoints) stays open during the game, so it
  gets none. The measurement run (Matt: "Done", 2026-10-06 21:15) shows
  both work: see "G7 analysis" ("Measurement run"). Hardware SUCCESS
  (Matt: "Yes, it did!": the resume returned to the quit point).
- The map scan (Matt, 2026-10-06): the largest loads of the 24 maps, and
  the maps for the next measurement run (d40, a10 to its end, c20; then
  d20). See "G7 analysis" ("Map scan").
- The vertex cache growth fix (Matt, 2026-10-06: "Yes, thanks."): the
  three draws of map vertex buffers (`rasterizer_xbox_draw_primitives.c`:
  BSP materials, model parts) give the device their vertex count
  (`d3d8_draw_vertex_count`, under `HALO_PS3`), which the Xbox's
  `DrawIndexedPrimitive` wrapper drops. `d3d8_rsx.c` sizes each element
  copy to the whole buffer at its first draw, so no copy grows or leaves
  an old copy: the arena holds at most the scan's data (24.3 MB, a10g).
  The two units compile; no PKG yet (see "Log").
- The mux fix (Matt, 2026-10-06: the NV2A mismatches of the shader check):
  the combiner mux in `ps3_rsx_fp.c` computed `(cd - ab) * s + ab`, which
  can miss `cd` by one ulp. A later mux that reads such an r0.a (exactly
  0.5 or -0.5 in the reference) then selected the other side (trap 37).
  The mux is now exact. PC: 0 failures in 720000 random keys (240 seeds of
  3000 keys; 420000 keys also without the optimizer), against 13 in 60000
  keys before; 26 instructions on average, as before. Not tested on the
  PS3: the PKG of 21:54 also holds part of another session's network
  edits (21:48 to 21:59), so do not install it.
- G7 step 3, system link on the LAN with the PC builds (Matt, 2026-10-06:
  "Can you do this?"): built, not tested. Winsock and XNet on the PSL1GHT
  sockets (`port/ps3/src/ps3_net.c` and `.h`, `port/ps3/xbox/xnet_ps3.c`;
  `xnet_null.c` is gone), and the PC's byte order on the wire (trap 38).
  See "G7 analysis" ("System link"). PKG of 22:52 in `build/ps3_net/` (a
  build folder of its own, see "Log"); it also holds the vertex cache
  growth fix and the mux fix above, both untested on the PS3.
- The audit of the code that a10 does not run (Matt, 2026-10-06): no halt
  and no thread death found. 6 PS3-only faults change the behaviour
  (doors, controls, AI, a d40 picture). Fixed (Matt: "Yes, you can fix
  them."): the changed units compile; no PKG yet, not tested on the PS3.
  See "Audit of the code a10 does not run".
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
  - The timer build (Matt, 2026-10-06): three `[time]` lines every 300
    frames (`port/ps3/src/ps3_timers.c`, `port/ps3/include/ps3_timers.h`),
    in ms a frame on the game's thread: the game ticks (and ms a tick), the
    render, the throttle wait, the present, the rest; in the render the
    sound and the parts of each window (`render.c`); the Direct3D draws
    (state, textures, texture hash, uploads, colours, vertices, indices);
    the waits (vblank, flip, RSX).
  - The RSX timestamps (G8, 2026-10-19, in place of the RSX probe of
    2026-10-06): a fourth `[time]` line, `[time] RSX in N frames`: the RSX's
    ms a frame and by part. At the frame's start, at each window's start and
    parts (`render.c`), after the render and at the present, the RSX writes
    its time to a report (`ps3_timers_rsx_wait`, trap 44), read two frames
    later; a part is the time from the split before to its own. No stall.
  - G8 lines: `screen: WxH drawn at PxQ` at the start and at each WIDESCREEN
    change; `[gpu] target ... tiled (region N, pitch P)` (`, ZCull` on the
    screen's depth); `[vid] display buffer N tiled`; `[gpu] frame N`: the
    vertex programs uploaded and switched; `menus: ... SETTINGS has VIDEO
    SETTINGS`.
  - G8 steps 1 to 3 (2026-10-19): `[time] collision` (the outermost
    collision tests' ms a frame by kind: vector, sphere, pill, point, move;
    the tests a frame) and `[time] collision by user` (ms a frame and tests
    a frame for each of the game's 22 users); `[time] screen quads` (the
    quads a frame, the parts of a quad, the interface pass's Direct3D draws
    and constants); in `[time] sound`, the game thread's waits for
    DirectSound's lock; in `DirectSound:`, the mixer's decoding and its
    lock holds (us a chunk, the longest hold). Then `[time] collision by
    function` (each logged function at any depth, the BSP nodes, the
    objects tested), `[time] HUD parts` (with their quads), and the
    obstruction calls and computations in `[time] sound`.
  - The G7 measurement build (Matt, 2026-10-06):
    - Two `[mem]` lines every 10 s (`ps3_main.c` `memory_heartbeat`). The
      first: the free main memory from lv2 syscall 352 (`ps3_memory_user`,
      no reservation) and its lowest, which the display thread samples at
      each pass from the game's start; the Xbox window's mapped MB and
      peak (`xbox_memory.c`); newlib's heap (`mallinfo`). The second: the
      RSX local memory by kind, with its peak (`ps3_gpu_memory_usage`;
      `ps3_gpu_memory_allocate` and `_free` take the kind and the size).
    - The boot `[mem]` lines give the probe and lv2's count side by side.
    - `xbox_files.c`: each transfer of 1 MB or more with its time; at the
      close of a file with 1 MB or more of transfers, the totals.
    - `Direct3D: frame` lines: the textures in RSX memory, and the KB that
      no draw used in 600 frames.
  - The save items (G7 step 2): `free space of <path>: N MB` at each save
    check of the game; the `fsync` time in the close line of a file with 1
    MB or more of transfers; `fsync failed (N)` on an error.
  - System link (G7 step 3): `Winsock: PSL1GHT sockets, this PS3 at
    a.b.c.d` at the start; `[net] <call>: lv2 error N` for the first 32
    socket errors but would-block (`ps3_net.c`); `Winsock: SO_RCVBUF of N
    bytes refused` with the size lv2 took (`xnet_ps3.c`).

## Goal stack

| Goal | Status |
| --- | --- |
| G0 Scaffolding and the platform layer: video, pads, audio, files, UDP log, XMB exit, memory probe | Done 2026-10-05 |
| G1 Data model: the game compiles and links for LP64 with the 32-bit layouts of its data | Done in G2 (Matt, 2026-10-05) |
| G2 The Xbox platform layer on the PS3 (kernel, XAPI, files, threads, memory, from `port/linux/src`). The game runs headless to its first map load. | Done 2026-10-05 (hardware SUCCESS, third run) |
| G3 Map data on a big-endian CPU: tag data swapped and its pointers relocated, checked headless | Done 2026-10-05 (hardware SUCCESS, run 9, after the audit fixes) |
| G4 Direct3D 8 on the RSX: the main menu shows | Done 2026-10-05 (hardware SUCCESS, run 5) |
| G5 XInput on the pads and DirectSound on the audio port: the menu operates, with sound | Done 2026-10-05: the pads (run 1, hardware SUCCESS); Matt moved the sound to G6 |
| G6 First playable level (a10) | Closed 2026-10-06 (Matt started G7): the sound works (run 1, hardware SUCCESS); a10 at 30 frames per second |
| G7 Performance and hardening: memory budget, saved games on the HDD, network | In progress: analysis done 2026-10-06 (see "G7 analysis"); step 1 (the measurement run) and the save items of step 2: hardware SUCCESS 2026-10-06 (a30; the larger maps not covered); map scan 2026-10-06 (see "G7 analysis"); step 3 (system link with the PC builds) built 2026-10-06, not tested; the swap of the messages the upstream merge brought built 2026-10-16, not tested; internet play and the Xbox menus' INTERNET PLAY screen built 2026-10-16, 4 public games joined in Matt's run (the log); LAN PLAY in SYSTEM LINK PLAY's place built 2026-10-16, not tested |
| G8 60 frames per second and 720p: the frames between the 30 Hz ticks, each frame in 16.7 ms on the game thread and the RSX; the game drawn at 720p (Matt, 2026-10-18) | Analysis done 2026-10-18; the fixes run on hardware 2026-10-19 (menus 60, a30 heavy combat 33 to 37); step 5 (the Direct3D thread) runs on hardware 2026-10-19 (a30 heavy combat 38 to 47); the measurements after it done 2026-10-19; Matt chose its three next steps (collision time by user, the sound's decode off the game thread, the screen quads' timers): run on hardware 2026-10-19 (`sound_render` 1.10 / 2.15 to 0.30 / 0.70 ms a frame; the collision tests 3.63 / 5.89 ms a frame); the next steps 1 to 3 (the obstruction kept up to 4 ticks, the rays and the HUD timed inside) run on hardware 2026-10-19 (the sounds' rays 1.60 / 1.94 to 0.58 / 0.93 ms a frame; the rays' objects part 1.07 / 2.91); the SPEs and VMX candidates written down (see "G8 analysis", "SPEs and VMX"); the next step is Matt's choice |

## G8 analysis (2026-10-18)

Matt: "The next step is to get this to 60 FPS and 720p. Analyze what can
achieve this." A read-only analysis of the code, the disassembly and the
logged data. No code change.

- Now (a10, the runs of 2026-10-06):
  - The display is 1280x720 with 3 buffers and vsync flips (`ps3_video.c`).
    The game draws 640x480; Present scales it to 960x720, 4:3 in the middle
    (`ps3_gpu.c` `blit_draw`). `halo_interpolation_enabled` returns 0
    (`port_stubs.c`): 30 frames per second.
  - Game thread: calm scenes 10 to 12 ms a frame with a tick (render 7.0 to
    7.8, tick 3.3 to 3.8); heavy scenes 18 to 21 ms (render 13 to 15, tick
    4 to 8; 450 to 565 draws).
  - RSX: under 33 ms (wait 0.0 at the cap). Not measured since the shader
    optimizer; about 40 ms in the heavy scene before it.
- The budget at 60: 16.7 ms a frame on the game thread and on the RSX. A
  tick runs every second frame: 33.3 ms hold one tick and two renders.
  Calm: 17 to 19 ms of 33.3 (fits). Heavy: 30 to 38 ms (up to 5 ms over),
  plus the interpolation and the per-frame updates (director, observer,
  player control), not measured.
- 60 frames per second, the code:
  - `render_interpolation.c` is in the build. `halo_interpolation_enabled`
    reads `display.interpolation`, as `sdl_platform.c` does.
  - `D3DDevice_Present` skips its wait for 2 emulated flips when it
    interpolates and counts the flip, as `d3d8_gl.c` does. The real flips
    (`ps3_video_begin_frame`, 3 buffers) then cap at the display's rate.
  - Memory: the interpolation's records, about 0.6 MB (8192 objects) and
    about 200 bytes a node. Main memory's lowest: 36 MB (2026-10-16).
- 720p, the code (upstream's model: `d3d8_gl.c` `screen_mode_choose`,
  `render_target_get`, `bind_textures`):
  - The game keeps its 480 lines. `halo_screen_width` gives 852 for 16:9.
    The targets the size of the screen and the secondary target (320x240)
    get 1280x720 pixels a screen (scale 1.502 x 1.5); viewports, scissors
    and clears scale; a linear target's texel coordinates scale (`UNRM`
    textures); `halo_screen_ui_offset` centres the menus (106);
    `halo_screen_scale` gives 1.5 (screen effects). `d3d8_rsx.c` gives 640
    and 1 now.
  - Present copies 1:1 (no 4:3 box, no clear).
  - The shapes:

    | Shape | Pixels | Times 640x480 | View across |
    | --- | --- | --- | --- |
    | 1280x720, 16:9 | 921600 | 3.0 | 86 degrees (stock 70) |
    | 960x720, 4:3 | 691200 | 2.25 | 70 degrees, as now |
    | 1024x576, 16:9, scaled to 720p | 589824 | 1.92 | 86 degrees |

    The wider view draws more objects and BSP (CPU and RSX).
  - RSX memory: about 10 MB more (peak 131.5 of 234 MB).
- The RSX at 720p:
  - A heavy frame at 1280x720 in 16.7 ms is about 5.5 ms at 640x480 for
    the pixel work.
  - The RSX has about 3.5 to 6 times the NV2A's rates: fill (8 ROPs at
    500 MHz, the NV2A 4 at 233), bandwidth (22.4 GB/s, the NV2A 6.4
    shared), texels. 1280x720 at 60 is 6 times the pixels a second of the
    Xbox's 640x480 at 30; 960x720 at 60 is 4.5 times. Scenes with many
    blended effects can stay over 16.7 ms at 1280x720.
  - Cuts (none is proven on hardware in the sibling ports):
    1. Tiled colour and depth surfaces with Z compression
       (`gcmSetTileInfo`, `gcmBindTile`, `GCM_COMPMODE_Z32_SEPSTENCIL`).
       Every surface is linear now (`ps3_gpu.c`, `ps3_video.c`). 1280 x 4
       = 0x1400, a tiled pitch.
    2. ZCull on the main depth buffer (`gcmBindZcull`): no shading of
       occluded fragments, in every depth-tested pass.
    3. Half-precision temporaries in the translated pixel shaders: fewer
       registers, more fragments in flight. The encoder has none; the PC
       check needs an fp16 model.
    4. Native 16-bit and 8-bit textures. Every non-DXT texture becomes
       A8R8G8B8 now (`xbox_texture_layout.c`).
    5. Vertex programs kept in the RSX's 512 slots: about 40 loads a frame
       now.
- CPU cuts (game thread):
  1. `d3d8_rsx.c` per draw (the timer run: 450 to 520 draws):
     - Colours 1.0 to 2.0 ms: 76 int-to-float conversions through memory
       (load-hit-store) and 76 divides a draw (the disassembly of
       `prepare_draw_state`). A table, and a rebuild only when the pixel
       shader constants change.
     - Indices 0.5 to 1.8 ms: two passes over each index. A draw whose
       vertices are all in the vertex cache can bind the arrays at the base
       vertex and copy the indices as they are. The models' index buffers
       are in the tag cache: cache them as the vertices. The BSP's are made
       each frame (`dynamic_triangles`).
     - Vertices at draws 0.4 to 1.6 ms: float elements copied as they are.
     - Estimate: 2 to 4 ms a frame in heavy scenes.
  2. Only if heavy scenes still miss: the Direct3D layer on the PPE's
     second hardware thread (a recorded command list). A large change: the
     texture cache, the dynamic vertices and the text cache then need
     fences (the game sees the RSX done at once now, `D3DDevice_IsBusy`).
     The gain is less than the work moved: the two threads share one core.
  3. SPUs: no easy candidate. The costs are the game's own render code and
     the per-draw state work.
  4. Fused multiply-adds: not in the simulation, which must match the PC
     builds (`port/linux/README.md`). Possible in the units that only draw
     (`source/rasterizer`, `source/render`); gain not known.
- Found: `rsxSetTimeStamp` in the image's librsx writes its data to word 2
  of its 2-word command (disassembled; PSL1GHT's source has the same
  fault). The RSX would read a stale word. Its type, 0x10, stops the RSX
  (hardware, 2026-10-19): use `rsxSetReport(context, GCM_ZPASS_PIXEL_CNT,
  index)` (trap 44).
- Out of reach of this plan: the large online games (44 players: 10.8
  frames per second, 0.82 ticks a frame, 2026-10-16). There the tick costs.
- Proposed steps:
  1. A measurement build, no change of the picture: interpolation on (60
     at 640x480), Present's flip wait as above, RSX timestamps at the
     frame's parts in place of the probe (no stall), the timer lines. The
     run: a10, a calm part and the heavy scene.
  2. The CPU cuts of item 1. Measure.
  3. 720p: the screen scale, in the shape Matt chooses (or a setting).
     Measure the RSX.
  4. The RSX cuts, in the order the timestamps give.
  5. The second hardware thread, only if heavy scenes still miss.
- The fixes (2026-10-19, Matt: "Just go with the fixes. We'll measure from
  top-down then. ... make the widescreen an option in the video settings.
  ... And a framerate toggle between 30 and 60."; step 5 only if heavy
  scenes still miss). Built, not tested on the PS3:
  - 60 frames per second: `halo_interpolation_enabled` reads
    `display.interpolation` (`port_stubs.c`); `D3DDevice_Present` counts the
    flip without the emulated wait when it interpolates.
  - 720p: `d3d8_rsx.c` has d3d8_gl.c's screen model: `display.screen_width`
    (0: 852 units for 16:9; 640: 4:3), targets of the screen's size and the
    320x240 secondary target at `screen_scale` pixels a unit (1280x720 or
    960x720), scissors and clears in pixels, the menus centred
    (`ui_offset` in the screen constants and the clear rectangles),
    `halo_screen_commit` (a change between frames: the surfaces resized,
    the targets evicted), `halo_screen_scale`. A linear target's texel
    coordinates are scaled in the pixel shader (the key's `texture_scaled`,
    uniform `_rsx_uniform_texture_scale`). Present copies the back buffer
    pixel for pixel in the middle (`blit_draw`), clearing only around a 4:3
    picture.
  - Menu: SETTINGS opens a new screen (a copy of the multiplayer menu's) of
    PLAYER PROFILES and VIDEO SETTINGS; VIDEO SETTINGS is a copy of ADVANCED
    CONTROLS with WIDESCREEN (ON/OFF) and FRAME RATE (60/30), bound to the
    two settings ("port setting load", "port settings save"); headers in
    the large font, the header pictures' blue. `menu_tags.c`
    (`xbox_settings_find`, `_define`, `_wire`, `_open`), in the same table
    growth as LAN PLAY (trap 48).
  - CPU: the pixel shader constants are converted with a table and only
    when their states change; a map buffer that the vertex cache holds
    whole draws with its indices as they are (one pass, a bound check by
    their OR), the models' index buffers copied once into the arena
    (`index_cache_offset`); `bind_targets` keeps its lookups while the
    surfaces stay. Vertices at draws: unchanged (the float path copies).
  - RSX: the screen's targets tiled (regions 3 to 14), the depth with Z
    compression and ZCull (`ps3_gpu_target_allocate`, trap 47); the display
    buffers tiled (regions 0 to 2, `ps3_video.c`); 8- and 16-bit textures
    native with a remap (`xbox_texture_layout.c`); vertex programs resident
    in the 512 slots, dropped at each system overlay
    (`ps3_system_overlay_events`); timestamps at the frame's splits in
    place of the probe (`ps3_timers_rsx_wait`, `[time] RSX`, trap 44).
    Each has a switch at the top of its file (`RSX_TILED_TARGETS`,
    `TILED_DISPLAY_BUFFERS`, `RSX_NATIVE_TEXELS`, `RSX_RESIDENT_PROGRAMS`).
  - Half precision (`ps3_rsx_fp.c` `half_precision`, `HALF_PRECISION` 0):
    built and off. The PC check, 3000 keys: 2.96 R registers on average
    against 3.07, 879 colours in H0, 13 results off the NV2A's by up to
    7.5e-3 (tests, divides, lookups and their sources must stay 32-bit,
    which leaves little).
  - Checks: `KEYS=3000` with `SEED=1` and `SEED=7`: 0 failures (the scaled
    stages included; the checker models H registers and scaled lookups);
    the changed units compile with GCC's warnings (none new); `check`: 0
    `r2off`, `.got` 0x7f80. PKG of 00:13 in `build/ps3_g8/`.
- Step 5, the Direct3D thread (2026-10-19, Matt: "Let's go with Step 5."):
  a30's heavy combat at 33 to 37 frames per second, the game thread about
  28 ms (Direct3D draws 6 to 8 of it). On hardware 2026-10-19: it runs,
  a30's heavy combat 38 to 47 frames per second (Matt: "The framerate
  still feels sluggish."):
  - `d3d8_rsx.c`: the game's thread records each call in a 4 MB ring
    (`record_begin`, `record_commit`); a thread of priority 1000
    (`direct3d_thread`, the PPE's other hardware thread) runs the records
    with the old draw code, one frame behind (`D3DDevice_Present` waits
    for the frame before). The game's thread keeps its own state
    (`front`); the draw's prologue records the render and texture stage
    states changed since the last record (a diff: the XDK's inline setters
    write `D3D__RenderState`), the vertex registers, the targets' and the
    textures' words. Records: states, attributes, texture, vertex shader,
    constants, constant mode, viewport, render target, menu offset, clear,
    draw, indexed draw, immediate draw, RSX timestamp, present.
  - Data: vertices and indices out of the tag cache are copied into the
    record (as the Xbox put indices in its push buffer; the dynamic
    buffers are written again each frame); the tag cache's are read where
    they are, and `d3d8_tag_cache_loading` (`cache_files.c`, before each
    load) waits for the draws. A texture's `Lock` holds its last draw:
    `D3DResource_IsBusy` and `BlockUntilNotBusy` (`d3d8_resources.c`)
    follow the Direct3D thread, so the texture cache evicts a texture only
    after its draws (trap 34, 49); other resources wait for every record.
  - A record larger than 1 MB, or every record when the thread does not
    start (`DIRECT3D_THREAD` 0), runs on the game's thread. The screen's
    change and the tag cache's loads drain the ring first. The exit and
    the halt stop the thread (`ps3_gpu_stop_drawing`).
  - `[time] Direct3D thread`: its busy time, and the game thread's waits
    for it; the draw timers are its own.
  - `check`: 0 `r2off`, `.got` 0x7fa0. PKG of 01:58 in `build/ps3_g8/`.
- The measurements after step 5 (2026-10-19, the PKGs of 02:25, 02:50,
  03:16 and 03:42; the numbers in the Log). Medians of a30's 300-frame
  windows of play, all of them / those under 45 frames per second:
  - The game thread sets the rate: the frame 19.9 / 27.1 ms, the RSX 9.9.
  - The sound, ms a frame: `game_sound_update` 1.50 / 1.92, nearly all of
    it the looping sounds' loop; the likely cost (not timed yet) is
    `compute_sound_obstruction`'s ray test through the BSP and the objects
    for each audible sound. `sound_render` 1.10 / 2.15: `begin_scene`
    (`DirectSoundDoWork`: the mixer's lock, the stream packets' decode on
    the game thread), refresh, channels. The mixer holds its lock for a
    whole chunk (0.14 to 0.54 ms, 2.0 at most).
  - The units, ms a tick: the biped's moving (`biped_update_physics`) 0.56
    / 1.32, the vehicle 0.42 / 1.70, the unit part 0.28 / 0.42.
  - The interface 1.9 ms in every frame of play: the weapon HUD 0.92, the
    unit HUD 0.80. The cheaper immediate mode did not change it.
  - Also: AI 1.91 / 4.38 ms a tick, the particles (update 1.32 / 1.96,
    draw 0.92 / 1.28 ms a frame), the models 2.24 / 1.91.
  - Not to the SPEs or VMX for the simulation: network results must match
    the PC's (Matt: "We'll consider the vmx approach later.").
  - Next, open for Matt's choice: 1. the collision tests' time by user, from
    the game's collision-user stack (sounds, bipeds, vehicles, AI,
    projectiles); 2. the sound: the stream packets' decode off the game
    thread (the mixer's thread or an SPU), and the mixer's lock not held
    for a whole chunk; 3. the HUD's own draws and state calls timed in the
    interface pass.
- Steps 1 to 3 of 2026-10-19 (Matt chose all three). Built, not tested on
  the PS3:
  - 1, collision by user: `collision_usage.c` times the outermost
    collision test (a depth count: a test inside another is the outer
    one's time) and gives it to the user on top of the game's
    collision-user stack at its end (`collision_move_pill` and
    `collision_fix_pill` push "biped" for every caller). The tests: the
    logged ones (`collision_log_start_time` and `_end_time`: vector and
    sphere tests of the structure, the objects and the models), and
    `collision_test_vector`, `_test_point`, `_test_sphere`, `_test_pill`,
    `_test_pill_new`, `_move_point`, `_move_pill`, `_fix_pill`
    (`collisions.c`), `collision_model_test_point` and `_test_pill`
    (`collision_models.c`). Two `mftb` reads a test: about 0.1 ms a frame
    more in heavy scenes (estimate).
  - 2, the sound (`dsound_ps3.c`): `Process` only allocates the packet's
    float buffer; the mixer decodes it when it first mixes it. A pass
    takes the streams' unfinished packets with the lock (a copy of each),
    decodes and mixes without it, and gives the decodes and the finished
    packets back with it (the buffers then); a flush voids the pass's
    results for its stream (a count), and the buffers of the packets it
    takes from a pass are freed after the pass. A release waits for the
    pass's end. The play state (position, gains) is the mixer's; a flush
    or a dry stream's new packet asks for a restart. GetStatus, the voice
    status and the reference counts take no lock; `DirectSoundDoWork`
    takes it only when the mixer finished a packet. The mixer thread keeps
    its priority (1000). A PC check (Docker, gcc 13; the session's
    scratchpad): the game's use of 24 streams (4 packets, refills from the
    callback, flushes) against the version before, one thread, 40000
    chunks: the same output bit for bit and the same completions; with the
    mixer thread, 317075 packets with AddressSanitizer: 0 errors, every
    packet completed once and in order; ThreadSanitizer: no race on the
    streams or the buffers (left: the statistics counters and the
    finished count's lock-free read, as meant).
  - 3, the screen quads (`rasterizer_xbox_dynavobgeom.c`
    `_rasterizer_psuedo_dynamic_screen_quad_draw`, the HUD's draw): their
    count, and the time of their render states, vertex constants,
    textures (the texture cache's lookup and the stage states), shaders
    (vertex shader, the pixel shader's states) and draw (immediate mode);
    `render.c`: the interface pass's share of the game thread's Direct3D
    draws and constants.
  - The changed files' comments over 2 lines are shortened; the symbol
    tables of `collision_usage.c`, `collisions.c` and `render.c` point to
    upstream's copy (as `sound_manager.c`'s); the code without comments
    differs only by the timers.
  - `check`: 0 `r2off`, `.got` 0x8048. PKG of 10:59 in `build/ps3_g8/`.
  - The run (Matt: "Done."; the numbers in the Log), medians of a30's
    windows of play, all / under 45 frames per second:
    - The sound off the game thread works: `sound_render` 0.30 / 0.70 ms a
      frame (before 1.10 / 2.15), the game thread's lock waits 0.00, no
      underrun. The mixer thread: 484 us a chunk of 5333 (decoding 202,
      about 0.49 ms a packet), the lock held 50 us a chunk.
    - The collision tests: 3.63 / 5.89 ms a frame, nearly all rays
      (vector 3.24 / 5.04). By user: the sounds' obstruction rays 1.60 /
      1.94 (19 / 27 rays a frame, about 70 us each: nearly all of
      `game_sound_update`); AI line of sight 0.68 / 0.64 (6 rays, about
      106 us each: about half of the AI's tick); the particles' point
      physics 0.47 / 0.65 (213 / 364 short rays, 2 us each); the bipeds'
      moves 0.33 / 0.16 (most of `biped_update_physics`); vehicles 0.15
      / 0.59 (about two thirds of their type update); "debug" 0.20 / 0.22:
      `rasterizer_models_begin`'s obscurer ray, 10000 units along the view,
      every frame.
    - The HUD: 19 screen quads a frame take 0.36 ms (shaders 0.14, draw
      0.15), and the interface pass's Direct3D draws 0.17 / 0.21: the
      rest of the HUD's 1.83 / 1.91 ms (about 1.45) is its own code
      outside the draws.
  - Next, open for Matt's choice: 1. the sounds' obstruction ray reused
    for a few ticks (audio only, not the simulation); 2. inside the rays:
    the structure BSP's part and the objects' part, to make the common
    path faster with the same results (sounds, AI, vehicles); 3. the
    HUD's own code timed (crosshairs, numbers, meters, motion sensor); 4.
    the obscurer ray once a tick, not every frame.
- The next steps 1 to 3 (2026-10-19, Matt: "Go with 1-3. Be aware that we
  can also delegate tasks to the SPEs."). Built, not tested on the PS3:
  - 1, `game_sound.c` (`HALO_PS3`): the obstruction cache (keyed by the
    source's exact position and the local player) keeps a result up to 4
    ticks, not 1: each of its 512 slots (was 128) renews at its own tick of
    the 4, so the rays spread over the ticks. A moving source still casts
    its ray each tick. `[time] sound` counts the calls and those computed.
    Audio only: the simulation does not read the obstruction.
  - 2, the rays timed inside (`collision_usage.c`): each logged collision
    function's time and calls at any depth (vector-structure, the
    structure BSP part of `collision_test_vector`; vector-objects, its
    objects part; model-vector; the object and structure BSP vector and
    sphere tests), the BSP nodes each test entered (`collision_bsp.c`), and
    the objects the rays tested (`collisions.c`). Faster code with the same
    results comes after these numbers. About two more `mftb` reads a test.
  - 3, the HUD's parts (`hud_weapon.c`: state, crosshairs, panel, sounds,
    grenades; `hud_unit.c`: setup, background, shield, health, motion
    sensor elements, motion sensor with its update and render in
    `motion_sensor.c`, auxiliary), each with the screen quads it draws.
  - The SPEs: see "SPEs and VMX" below.
  - The changed files' comments over 2 lines are shortened (the symbol
    tables of `game_sound.c`, `hud_weapon.c`, `hud_unit.c` and
    `motion_sensor.c` point to upstream's copy); the code without comments
    differs only by these changes. `check`: 0 `r2off`, `.got` 0x80b8. PKG
    of 11:40 in `build/ps3_g8/`.
  - The run (Matt: "Done."; the numbers in the Log), a30 with a vehicle
    (heavier than the run before: the frames are not comparable), medians
    all / under 45 frames per second:
    - 1 works: 78 / 74 % of the obstruction calls come from the cache;
      the sounds' rays 7.3 / 11.6 a frame (before 19.3 / 26.9), 0.58 /
      0.93 ms (before 1.60 / 1.94); `game_sound_update` 0.72 / 0.77 ms a
      frame (before 1.52 / 1.66).
    - 2: the rays' objects part (vector-objects) is their main cost: 1.07
      / 2.91 ms a frame for 30 / 60 calls, which test 1235 / 5655 objects
      (about 0.5 to 0.9 us each, nearly all out of the ray's way); the
      structure part (vector-structure) 0.35 / 0.83; every structure BSP
      vector test 0.43 / 0.83 ms for 77 / 185 tests, 1881 / 4284 nodes
      (about 24 a test, 0.2 us a node); the model tests 0.23 / 0.27;
      features-in-sphere 0.40 / 1.07. By user: particles' point physics
      0.56 / 1.15 (291 / 638 rays), vehicles 0.33 / 2.39, sounds 0.58 /
      0.93, AI line of sight 0.36 / 0.40, bipeds 0.23 / 0.19, the obscurer
      ray 0.21 / 0.22.
    - 3: each HUD element costs 60 to 160 us, of it the quad's draw about
      19 (the 19 quads 0.36 ms): weapon panel 0.49 / 0.56 (8 quads),
      grenades 0.24 / 0.27 (4), crosshairs 0.16 / 0.21 (1), shield 0.18 (2),
      health 0.18 / 0.20 (2), motion sensor elements 0.17 (2), the motion
      sensor's blips 0.17 / 0.25 (its update 0.01). So 40 to 140 us an
      element are the HUD's own code outside the draw.
  - Next, open for Matt's choice: 1. the HUD: time inside an element
    (`hud_draw_static_element`, `hud_draw_meter`, `hud_draw_numbers`); 2.
    the rays' objects part: pass over the objects out of a ray's way on a
    packed copy of their bounding spheres (as `object_bounds_cache.c` does
    for the sphere tests: the same test on the same values, so the same
    results), or with exact VMX; 3. the first SPE task, the DirectSound
    mixer (see "SPEs and VMX"); 4. the obscurer ray once a tick.
- SPEs and VMX (2026-10-19, Matt: "Write the SPEs parts and the vmx inside
  GOALS and ROADMAPS in case. Those are extremely worth checking out."):
  - What must match the PC builds: the simulation (the game ticks:
    objects, units, vehicles, AI, projectiles, damage, scripts, and the
    collision tests they make) must give the PC builds' floats (x86 SSE,
    round to nearest, no fused multiply-add: `-ffp-contract=off`, musl's
    maths) for network play. Free to differ: the sound (mixing, decoding,
    obstruction), the drawing (render, the Direct3D layer, the RSX
    microcode), the HUD, the particles and contrails (updated each frame
    with the frame's time: visual only), loads and conversions.
  - SPEs (PSL1GHT): 6 for the game, 256 KB local store each, DMA to main
    memory; `spu-gcc` 7.2 is in the image (`$PS3DEV/spu/bin`), with
    PSL1GHT's SPU libraries (`libsputhread`, `libspudma`, `simdmath`); SPU
    threads with `sysSpuThreadGroupCreate`, images with
    `sysSpuImageImport`. Single precision rounds toward zero, has no
    denormals, NaN or infinity: not the PC's results. Double precision is
    IEEE (round to nearest) but slow on this Cell (not pipelined).
  - VMX (AltiVec on the PPE): 4 floats a register; every unit builds with
    `-mno-altivec`, so a VMX unit needs `-maltivec` of its own. Exact for
    the simulation only so: `vaddfp` and `vsubfp` round to nearest as SSE
    does; a multiply is `vmaddfp` with -0.0 added (one rounding, exact); a
    fused multiply-add rounds once, unlike the PC's multiply then add;
    division and square root are only estimates (`vrefp`, `vrsqrtefp`),
    so they stay on the scalar FPU; VSCR's NJ bit must be 0 (it flushes
    denormals). The scalar code's order of operations kept in each lane:
    four of the same test at once.
  - Reference: flycastPS3 (`E:\Users\Matteo\Desktop\Dreamcast\flycastPS3`):
    SPU programs in `ps3/spu/` (audio `spe_aica`, texture decode and hash
    `spe_tex`, vertex build `spe_vb`, pixels `spe_pix`), their PPE side
    (`ps3/ps3_spe.cpp`, `ps3_spevb.cpp`, `ps3_spetex.cpp`), the Makefile's
    rules (`spu-gcc`, `bin2s`, `-lsputhread`), a PC simulator of the SPU
    image (`misc/spu_sim/`), and its trap: an SPU thread's `main` gets 3
    arguments, the 4th arrives as 0 (pack extra buffers into one struct).
  - SPE candidates, by measured cost (a30, all / under 45 frames per
    second):
    1. The DirectSound mixer (decode and mix): 484 us of each 5333 us
       chunk on the PPE, about 9 % of the hardware thread that the game
       thread and the Direct3D thread share. Self-contained (packets in,
       samples out to the audio ring), no exactness needed; flycastPS3's
       `spe_aica` is the model. The first SPE task: small, and a fault is
       heard at once.
    2. The Direct3D thread's conversions (of its 10 to 12 ms a frame:
       vertices at draws 3.45, colours 0.5, indices 0.3): copies and
       conversions into the RSX scratch as SPU jobs (flycastPS3's
       `spe_vb`); the command buffer stays on the PPE.
    3. The particles (visual): their update 1.33 / 2.57 ms a frame, and
       their point physics rays 0.56 / 1.15 (291 / 638 a frame). An SPU
       job needs the collision BSP and the objects by DMA: a large port.
    4. The sounds' obstruction rays (audio): 0.58 / 0.93 ms a frame after
       the 4-tick cache; the same needs as 3.
    5. Texture conversion and hashing at loads (flycastPS3's `spe_tex`):
       load times, not frame times.
  - VMX candidates:
    1. The Direct3D layer's conversions on the PPE (as SPE 2): float4
       vertices, D3DCOLOR to float4, index copies.
    2. The mixer's loop (4 samples at once), while it stays on the PPE.
    3. The render's CPU math, free to differ: the models (2.84 / 2.19 ms
       a frame: node matrices to vertex constants), the visibility tests,
       the particles' and contrails' quads (0.92 / 1.87).
    4. The simulation, exactly (adds, subtracts and unfused multiplies,
       NJ 0, no estimates): the rays' objects part (1.07 / 2.91 ms a frame,
       bounding sphere tests four at once), node matrices (1.15 ms a tick),
       the BSP traversal (0.43 / 0.83 ms). Each with a PC check bit for bit
       against the scalar code, as the shader check does.
  - Proposed order: the SPU set-up from flycastPS3 (Makefile rules, an
    SPU job runner) and the mixer on an SPU; measure; then the Direct3D
    conversions (SPU jobs or VMX); then exact VMX where the measurements
    point, each with its PC check.

## G7 analysis (2026-10-06)

Three read-only audits of the code. No code change.

- Memory:
  - Main memory: 188 MB free at boot, 179 MB after the RSX init, 94 MB
    after the game's start-up (G2). The Xbox window maps about 72 MB (tag
    cache 22, game state 16, texture cache 22, sound cache 4, Direct3D
    blocks); nothing in it is freed before the exit. Outside the window: the
    RSX host buffer 8 MB, the AI debug arrays 9.9 MB and the rasterizer
    debug arrays 1.4 MB (always allocated, `ai_debug.c`,
    `rasterizer_debug.c`), DirectSound's float packets (up to about 7.6 MB,
    freed when played), the shader caches (never freed).
  - RSX memory: about 98 MB fixed (display buffers and depth 14 MB at 720p,
    scratch 32 MB, vertex arena 48 MB, render targets about 4 MB). Texture
    copies have no cap: a copy stays up to 1800 idle frames after the
    texture cache drops it. A failed RSX allocation logs and skips; nothing
    halts.
  - Nothing measures memory during the game: the `[mem]` lines are at boot
    only. lv2 syscall 352 (`sys_memory_get_user_memory_size`, no PSL1GHT
    wrapper, `lv2/syscalls.h`) gives the free user memory without a
    reservation (trap 15 is about the container probe).
- Saved games (files in `/dev_hdd0/data/HaloCE/u` and `z`):
  - The game state is 16 MB on the PS3 (`HALO_PORT_GAME_STATE_SIZE`; 3.3
    MB on the Xbox). Each checkpoint writes all of it to `z:\savegame.bin`
    on the game thread; a death, "restart at checkpoint" and a cinematic
    skip read it back. Each level start reads the profile's `savegame.bin`
    (the header, then a CRC over 16 MB); pause > quit writes it.
  - `checksum failed on persistent storage` at a level start is normal
    until the profile has a save: a new file of zeroes has a stored
    checksum of 0 (the Xbox does the same).
  - The files hold big-endian data. The PS3 reads only its own files (an
    Xbox or PC profile fails its signature).
  - `GetDiskFreeSpaceExA` gives a fixed 8 GB (`ps3_host_disk_space`;
    `sysFsGetFreeSize` needs the sys_fs module). Written files get no
    `sysLv2FsFsync`. Both fixed in step 2 (2026-10-06, see "Now").
  - Not tested on hardware: the revert at a death, pause > quit and the
    resume, a profile after a PS3 restart.
- Network (`xbox/xnet_null.c` now: no network):
  - System link uses the game's transport (`source/bungie_net`, UDP
    broadcast to ports 5150 and 5151, TCP). The Linux build implements
    Winsock and XNet on POSIX in `port/linux/src/xnet.c` and `posix_net.c`
    (Linux-only parts: getifaddrs, accept4, MSG_NOSIGNAL, `__thread`,
    pthread). Internet play (`p2p*.c`: MQTT signalling, STUN, a KCP tunnel)
    also needs threads, DNS and LP64 fixes (`long long`, 4-byte copies of
    an `unsigned long`).
  - The PC builds have their own wire format (message version 2, network
    version 11). To play with a PC, the PS3 must send the PC's bytes: the
    message header (`message_header.c`) and `data_encoding.c` always swap,
    which a big-endian CPU must not do; the advertisement, the game
    settings and the distributed netcode (`port/linux/game/network_*.c`)
    are raw little-endian structs.
  - PSL1GHT: socket descriptors have bit 30 set (no `select` with `FD_*`;
    `poll` strips the bit); `sockaddr_in` starts with `sin_len` and a
    1-byte family; netctl gives the local address and netmask.
- Proposed steps:
  1. A measurement build: the free main memory (syscall 352) and the RSX
     memory in use, every 10 s, with peaks; the time of each file transfer
     of 1 MB or more. The run: a10 to a checkpoint, a death, pause > quit,
     resume, a PS3 restart and resume, then larger maps. Built 2026-10-06
     (Matt: "Do the measurement build.").
  2. Fixes from that data, and the known save items (the real free space,
     `fsync` after the save writes). The save items are built (2026-10-06,
     Matt: "Do 1 then.").
  3. System link on the LAN with the PC builds: Winsock and XNet on the
     PSL1GHT sockets (from `xnet.c`), and the PC's byte order on the wire.
  4. Later: internet play (`p2p*.c`).
- Measurement run 2026-10-06 (PKG of 21:03; a30 and the menu; the
  listener started at t=20 s of the first boot):
  - Main memory: 93.1 MB free at the lowest (94.1 in the menu); the Xbox
    window 71 MB (peak 71); newlib's heap 21 to 22 MB. Stable.
  - RSX memory: 99 MB in the menu, peak 131.5 of 234 MB, 0 refused. The
    textures grew from 21.0 to 34.7 MB when a30 loaded a second time in
    the same boot (the old copies stay for up to 1800 idle frames).
  - On the game thread: each checkpoint writes 16 MB to `z:\savegame.bin`
    in 1.12 to 1.20 s; each revert (a death) reads it in 1.0 s; pause >
    quit 2.1 s (the revert, then 16 MB to the profile in 0.96 s and the
    `fsync` in 39 ms); each check of the profile's save (menu, level
    start) 0.7 s; the resume read 0.75 s. The Xbox's game state is 3.3 MB.
  - The save items: `free space of /dev_hdd0/data/HaloCE/u: 67529 MB`
    after the restart (the game checks only at the first menu of a boot
    and at a new save, not after pause > quit); no `fsync failed`. The
    resume after the PS3 restart read the save and ran a30 at 30 frames
    per second.
  - Not covered: the larger maps.
- Map scan 2026-10-06 (Matt: "Scan the 24 maps for their largest loads"):
  a PC script (the session's scratchpad) walks the tag data and the BSPs
  of each map with the swap tables (0 errors). No code change. Sizes in
  MB:
  - Vertex cache (one 48 MB block, emptied at each map or BSP load): each
    vertex once, as a float4 array for each element that the game's
    vertex shaders read (BSP 5 + 2 lightmap, models 7). The largest BSP:
    a10g 10.4 (then a10f 10.2, c20_3 9.6, a10b 9.6, a10e 9.3, d40c 9.2,
    d40b 9.1, d20_muster 9.0). All the models of a map (all levels of
    detail): 8 to 17 (a30 17.2). Both: at most 24.3 (a10g). A BSP array
    grows by doubling when more of it is visible, and the old copy stays
    until the next load: up to 4 times its data. In that worst case 8
    BSPs pass 48 (a10g 55.5, a10f, a10b, d40c, a10e, d40b, d20_muster,
    d20_hangar). The a10 run of 2026-10-06 measured 10.2.
  - Textures as RSX copies (DXT as they are, the rest 4 bytes a texel):
    all the bitmaps of a map: d40 94.6, a10 82.6, b40 79.9, c40 78.5, c10
    75.2, c20 73.2 (a30 52.2; the run measured 21.0). The set of one BSP
    (its shaders, lightmaps, fog, weather, lens flares): at most 25.4
    (c20_3). With all the other bitmaps of its map: at most 54.6 (d40b).
  - Sounds: the largest permutation (one block of the 4 MB sound cache) is
    372 KB (a10, a50, d20). The sound cache is the Xbox's.
  - Multiplayer maps: at most 10.7 of vertex data and 35.7 of textures.
  - For the next measurement run: d40 (d40b, d40c), a10 to its end (a10f,
    a10g), c20 (c20_1 at the start has 7.6 of BSP vertex data); then d20.
- System link, step 3 (2026-10-06, Matt: "Can you do this?"). Built, not
  tested on the PS3:
  - Transport: `port/ps3/src/ps3_net.c` calls libnet's `netSocket` and
    co. (their error is each thread's `net_errno`; trap 39), sets
    `sin_len`, makes sockets non-blocking with `SO_NBIO`, and selects with
    `netPoll`, Winsock's way (a failed connect is in the error set, by
    `SO_ERROR`). The PS3's address comes from netctl (`SYSMODULE_NETCTL`,
    asked once a second); random numbers from `sysGetRandomNumber`.
    `port/ps3/xbox/xnet_ps3.c` is `port/linux/src/xnet.c` without internet
    play and the latency test: Winsock's `sockaddr_in` to lv2's,
    `network.address` and `network.broadcast` from `config.toml`, the
    XNADDR with the PS3's address (pending while netctl gets one, which
    `transport_initialize` waits up to 10 s for), `WSAGetLastError` as the
    thread's last error. A refused 1 MB socket buffer falls back by halves.
  - Wire order (trap 38): message headers and the typed packet fields are
    big-endian on the wire, the PS3's own order, so `message_header.c` and
    `data_encoding.c` no longer swap on a big-endian host. The PC's
    little-endian memory is swapped on the PS3 when it goes out and when it
    comes in: the advertisement (`game_advertisement_byte_swap`, both
    handlers), the settings record (`network_game_byte_swap`, at its two
    sends and before a complete record is applied), and the distributed
    messages (`network_distributed_byte_swap`: at the five send functions
    of `network_game_globals.c` and `network_server_message_handler.c`, and
    once in `network_distributed_handle_message`, batches included; the
    entries in `network_distributed.c`, `network_objects.c`,
    `network_damage.c`, the game types' states in `game_engine*.c`). The
    receive buffers are 4-aligned: handlers read entries in place, floats
    too (trap 23).
  - Checks on the PC: the walkers of the unit states and relayed actions
    (the variable entries) against their writers, 200000 random states, 0
    failures (a planted fault fails). A review of every swap list against
    its struct and of the packet field tables: no defect.
  - The test: a PC build of the same source (network version 11; 29 since
    the merge of 2026-10-16) on the LAN, and its firewall open for UDP 5150
    and 5151 and TCP 5150. The PC
    hosts a system link game and the PS3 joins; then the PS3 hosts and the
    PC joins; a few minutes each (moves, shots, kills, scores on both).
    Games that do not show: `network.broadcast` in both `config.toml`.

## Audit of the code a10 does not run (2026-10-06)

Matt: "Audit the code that a10 does not run (cutscenes, vehicles, the Flood,
the scripts of the other levels)". Six read-only audits and four checks of
the whole tree. No code change.

- The scope, from the maps (a PC script in the session's scratchpad): the
  88 script functions and 3 external globals that only a30 to d40 use; the
  tag groups that a10 does not have (`ctrl`, `dobc`, `elec`, `glw!`,
  `lifi`, `rain`, `swat`, `wind`); the vehicles, characters, weapons and
  effects of each map.
- No halt and no thread death found in: the recorded animations (all 349
  of the 10 maps simulated: no assert can fail), cinematics, cameras,
  titles, the c10 video effect, custom and scenery animations; vehicles,
  seats, vehicle physics, vehicle scripts and AI; the Flood, Sentinels,
  the Monitor, Hunters, Jackals, Engineers, stealth elites, the new
  weapons, projectiles and powerups; the AI scripts (swarms, spawns,
  migration, conversations); the other scripts, devices, level changes
  and the d40 end (the credits are Bink: the null player returns to the
  menu, as at boot); the PS3 layers (texture formats of all maps, draws,
  shaders, render targets, sound formats, waits).
- Checks of the whole tree:
  - The dump scan of trap 36, with a pattern that also finds typedef names
    (`size_t`, `uintptr_t`), then the 64-bit types only. 494 units: only
    the 6 known hits. The pattern:
    `grep -rn -E "\([A-Za-z_][A-Za-z_0-9 ]*\) \(int\) " --include=*.original build`
  - Map data: every 4-byte field of the tag data and the BSPs of the 10
    campaign maps and `ui.map` is 4-aligned (trap 23).
  - The game's views of tag data (DWARF of the 476 game units, 310 views)
    against the swap layouts: the narrow views 1, 2 and 5 below. The 3083
    game fields on bytes that the swap leaves (padding to Invader) are 0
    in all maps, or the game does not read them.
- The faults (PS3 only, none halts; fixed, see "Fixes" below):
  1. Doors lose their flags. `scenario_machine_datum.flags`
     (`device_machines.h`) is a byte at +0x30 of a 32-bit field (trap 21):
     the PS3 reads the high byte, always 0. No door gets "does not operate
     automatically", "one sided", "never appears locked" or "opened by
     melee attack". Doors with flags: a10 3, a50 142, b30 1, b40 44, c10
     25, c20 12, c40 14, d20 120, d40 69. A locked door opens for any biped
     near it when it has power, a one-sided door from both sides; a melee
     door does not open.
  2. Controls lose "usable from both sides". `scenario_control_datum.flags`
     (`device_controls.c`) is a word at +0x30 of a 32-bit field. The action
     prompt then shows only at the front of the panel. Controls with the
     flag: a30 1, b30 2, b40 14, c10 5, c40 14. A panel whose front the
     player cannot reach blocks the level (not checked).
  3. `machine_update` (`device_machines.c:251`) reads the byte at unit
     +0xB6, the high byte of the word `damage_flags` on the PS3: bit 10
     (`_object_cannot_hold_weapon_bit`), not bit 2 (`_object_dead_bit`).
     Corpses keep automatic doors open; units without their weapon arm
     (the Flood) cannot open them. All maps.
  4. `actor_perception.c:1236` reads the same byte (struct at line 417):
     dead targets count as alive, and units without their weapon arm as
     dead (unacknowledged and orphan targets). The AI then picks other
     dialogue. All maps.
  5. `extra_flags` of the chicago shader
     (`rasterizer_xbox_transparent_geometry.c:357`) is a word at +0x60 of a
     32-bit field. The d40 self-destruct screens show wrong digits; the
     first-person weapon screens fade with active camouflage.
  6. `encounters.c:741` and `:808`: `FLAG(squad_index)` in a 32-bit mask.
     The PPC gives 0 for squads 32 to 63, x86 uses the low 5 bits. b40
     `ext_b_area_a_cov` patrols with such squads: its active area can
     differ. Minor.
  - Fixes (2026-10-06, Matt: "Yes, you can fix them."): 1, 2 and 5 put
    the narrow member at the field's last bytes under `__BIG_ENDIAN__`
    (trap 21; the offset asserts of 5 too); 3 reads
    `unit->object.damage_flags` with `_object_dead_bit`; 4 moves its view
    to +0xB7 under `__BIG_ENDIAN__`; 6 uses `squad_index & 31` (x86's
    result). The compiled code reads +0x33, +0x32, the word at +0xB6, +0xB7
    and +0x62, and masks the shift. Not tested on the PS3.
- Sound and picture of the PS3 layers: no reverb, no Doppler, no sound
  cones (`dsound_ps3.c`: `SetI3DL2Listener`, `SetVelocity` and the cone
  calls do nothing). The texture copies have no cap (see "Map scan").

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
  0 only), point sprites. The cache of converted vertex buffers came on
  2026-10-06 (see "Now").

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
- G7, a10 at 9 to 10 frames per second (investigation 2026-10-05: code,
  disassembly and `a10.map` data; no timers on the PS3 yet):
  - Most likely main cost: `d3d8_rsx.c` `setup_vertices`. Each draw
    converts its vertices on the CPU into the RSX scratch, again for each
    pass and each frame (no cache), over the whole index range of an
    indexed draw. Each packed normal and normalized short component is a
    load-hit-store (`std`/`lfd`) and an `fdivs`: up to 14 a vertex in the
    lightmap pass. `a10a` has 67329 vertices in 181 materials. Estimate:
    20 to 40 ms a frame. `port/linux/src/d3d8_gl.c` uploads each page once.
  - Then: `texture_get` hashes the texels of each bound texture once a
    frame (the `a10a` shaders: 25 bitmaps, 5.0 MB); `prepare_draw` converts
    19 colours a draw (76 conversions and divides).
  - Above 33 ms a frame, `main_update_time` gives up to 2 ticks a frame,
    so the render cost also adds tick cost.
  - To confirm: per-phase timers, or the `[gpu] frame` scratch peak and the
    `Direct3D: frame` texture uploads of an a10 log.
  - 2026-10-06 (Matt): the vertex cache for the first cost is built (see
    "Now"). Its log line: `Direct3D: frame N: vertex elements converted:
    ... into the cache, ... at draws; vertex cache ... arrays, ... of
    49152 KB, ... refused`. The texture hash and the colour conversions
    are not changed.
  - Hardware run 2026-10-06 (a10, 5400 frames): 238 to 298 frames each
    10 s; the cache held 635 arrays, 10394 KB, 0 refused; after the warm-up
    0 elements a window went into it, and about 2300 a frame were converted
    at draws. At 30 frames per second the game thread waits in
    `main_rasterizer_throttle`. In the slower windows it never reaches that
    wait: the frame is over 33 ms (more draws, 427 to 483 a frame). The
    heartbeat then shows `_sound_cache_sound_request` as the last wait, but
    only with 2 to 6 yields in 5 to 45 s: it is not the cost. The sound
    mixer uses 69 to 136 us of each 5333 us chunk (1 to 3%).
  - Next, not measured yet: the rest of the frame (game ticks, the game's
    render code, the texture hash, the colour conversions). Timers per
    phase would show it.
  - Timer build run 2026-10-06 (a10, about 7500 frames, no fight): 18.9 to
    29.9 frames per second, 33 to 53 ms a frame. In ms a frame: the texture
    hash 8.8 to 15.3 (the largest part); the game ticks 2.8 to 4.7 (3 to 4
    ms a tick); the colours 1.0 to 2.0; vertices 0.4 to 1.6; indices 0.5
    to 1.8; the game's own render code about 6 to 9; the RSX wait 0.2 to
    12.1 (in the 18.9 fps window the RSX needed about 50 ms a frame);
    vblank and flip waits 0.
  - Next by that data: the texture hash (skip it for the texture cache's
    textures, which only disk reads fill), then the RSX time of heavy
    scenes.
  - 2026-10-06 (Matt: "Yes, do it"): the texture hash fix is built (see
    "Now" and trap 34). Hardware run (a10, about 7000 frames): the hash
    0.0 ms, uploads normal (12 in 600 frames after the load). Calm scenes:
    29 to 30 frames per second, the game thread 12 to 16 ms a frame. Heavy
    scene: 24 to 26 frames per second, 38.5 to 41.6 ms a frame; the game
    thread 16 to 19 ms (ticks 3.8 to 4.4, render 12.1 to 15.3), then 17 to
    23 ms of RSX wait: the RSX needs about 40 ms a frame there. Each frame:
    450 to 520 draws, 110 to 117 pixel shader copies, about 40 vertex
    program loads.
  - Next by that data: the RSX. 2026-10-06 (Matt: "Yes, do it"): the RSX
    probe is built (see "Now", diagnostics).
  - RSX probe run 2026-10-06 (a10 heavy scene, 21 to 27 frames per second):
    RSX ms in a probe frame: specular part (specular lightmaps,
    reflections, mirrors, transparent level geometry, fog) 13.5 to 17.9,
    effects 0.3 to 9.1, diffuse 4.3 to 5.7, lightmaps 3.6 to 4.6, lights
    2.8 to 4.5, objects 0.9 to 2.5, the rest under 1. At a probe frame's
    start the RSX still had 35 to 42 ms of earlier frames to do. Matt saw
    the geometry jump in that build only: the probe frames (about 70 ms).
  - The cause, found on the PC with `ps3_rsx_fp.c` (2026-10-06): the
    pixel shader translation is not optimized. The simplest shader (one
    texture times the vertex colour, 1 combiner) becomes 60 RSX
    instructions and 38 inline constants with 23 temporaries; the
    reflection pass's (`rasterizer_xbox_environment.c`, bumped cube map,
    5 combiners) 205 instructions with 23 temporaries. Every input gets a
    clamp, every combiner computes all its outputs and clamps them, and no
    register is reused. The fix: an optimizing pass before the encoding
    (dead code, constants, copies, register allocation), checked with
    `tools/ps3_rsx_shader_check.sh`.
  - 2026-10-06 (Matt: "Yes, make the shader optimizer."): the pass is built
    (see "Now", trap 35). PC: random keys 249 to 26 instructions on
    average, 3.1 temporaries on average (9 at most); the reflection pass
    205 to 47 instructions (1332 to 264 words), 5 temporaries; the simplest
    shader 60 to 3. With `KEYS=3000` the check shows 1 to 3 mismatches
    against the NV2A reference on some seeds (`SEED=1`: key 1729). The
    translation without the optimizer gives the same ones. Fixed
    2026-10-06: the mux was not exact (see "Now", trap 37).
  - Optimizer run 2026-10-06 (Matt: SUCCESS, a10, then the menu; the
    listener started about 200 s after the boot): 11 timer windows. 8 at
    29.7 to 30.0 frames per second, the RSX wait 0.0 ms in all 11. The
    game thread 10 to 21 ms a frame (ticks 3.3 to 8.0, render 7.0 to
    14.7), the rest the 30 fps throttle wait. Two windows at 26.9 frames
    per second: 4.0 ms a frame outside the timed phases ("rest", about 1.2
    s each), with texture uploads, new pixel shaders and vertex cache
    fills (loads, not the RSX). The last window (21.6) is the return to the
    menu. Each frame: 211 to 565 draws, 60 to 113 pixel shader copies;
    scratch peak 601 to 722 KB (1005 KB before). 143 pixel shaders
    translated, 0 skipped draws.
- Widescreen and 60 frames per second (Matt asked, 2026-10-06): both are
  possible, not started. Recommended order: the G7 performance work, then
  widescreen, then 60 frames per second.
  - Widescreen: the game code already takes a wider screen from
    `halo_screen_width()` (HUD inset, menus, cinematics, viewport; the FOV
    follows the viewport shape in `render_cameras.c`). On the PS3 it returns
    640 (`d3d8_rsx.c`), and Present draws 640x480 4:3 in the middle. To do:
    854 on a 16:9 TV, an 854x480 back buffer, Present to the full display.
    Cost: about 33% more pixels for the RSX.
  - 60 frames per second (G8 from 2026-10-06, Matt: the last goal): the
    game ticks at 30 Hz. `port/linux/game/render_interpolation.c` draws
    the frames between ticks and is already in the PS3 build;
    `halo_interpolation_enabled` in `port/ps3/xbox/port_stubs.c` returns
    0. The vsync flips (three buffers) give the cap at 60. Each frame must
    take 16.7 ms or less, and a tick runs every second frame. The game
    thread after the optimizer (run of 2026-10-06): calm scenes render 7.0
    to 7.8 ms and tick 3.3 to 3.8 ms (10 to 12 ms with a tick: fits);
    heavy scenes render 13 to 15 ms and tick 4 to 8 ms (18 to 21 ms with a
    tick: about 5 ms too much). The RSX is under 33 ms a frame (wait 0.0);
    under 16.7 ms is not measured. Interpolation adds CPU work each frame,
    not measured. First step: a measurement build, interpolation and
    timers on.

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
    narrower units (`FIELD_UNITS` in `tools/ps3_tag_swap.py`). Private
    structs in `.c` files and raw byte reads of a runtime word (unit +0xB6)
    have the same fault (see "Audit of the code a10 does not run").
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
34. A texture's `Lock` holds the number of its last draw, as the Xbox
    runtime's does (`d3d8_rsx.c` `draw_prologue`): `D3DResource_IsBusy`
    compares it with the Direct3D thread's progress (trap 49). A draw that
    finds `Lock` 0 checks the texels again: `xbox_texture_cache.c` puts it
    to 0 for each texture it loads, and texels of the texture cache are not
    checked again otherwise. Code that writes texels in that memory with
    the CPU (Bink, once it decodes) must put `Lock` to 0 after each write.
35. The pixel shader optimizer (`ps3_rsx_fp.c`) takes these values as 0 to
    1: the texels (every RSX texture is A8R8G8B8 or DXT), the colour inputs
    (`ps3_rsx_vp.c` saturates the colour outputs) and the colour uniforms
    (c0, c1, the final combiner's, the fog colour). A signed or float
    texture, or an unsaturated colour output, needs a change in `resolve`
    and `bounds`. Only MOV, TEX and TXP read a texture coordinate input:
    cgcomp's MUL, DP3 and DP4 read it without perspective correction.
36. GCC sign-extends a `HALO_PTR32` pointer when it makes it a 64-bit
    integer: in a difference with a full pointer, and in a cast to a 64-bit
    integer type (`extsw`). An Xbox window address 0x8xxxxxxx becomes
    0xFFFFFFFF8xxxxxxx. Cast the member to a full pointer first
    (`p - (byte *)x->start`): that conversion zero-extends. Loads,
    assignments, comparisons, pointer + integer and a difference of two
    `HALO_PTR32` pointers are correct. GCC gives no warning: use the dump
    scan (see "Commands"). A hit is a fault only if the `(int)` operand is a
    pointer and the 64-bit result is used as it is (not cut to 32 bits).
37. A combiner mux must give `cd` or `ab` exactly. `(cd - ab) * s + ab`
    can miss `cd` by one ulp. When the exact value is 0.5 or -0.5, a later
    mux that reads it in r0.a (the parity of r0.a * 255 + 0.5, or r0.a >=
    0.5) then selects the other side: wrong colours. `ps3_rsx_fp.c` uses
    `ab - ab * s + cd * s`, exact for s 0 or 1. Keep a rewrite of the mux,
    or of the optimizer, exact.
38. The network wire is the PC builds' order. Message headers and typed
    packet fields (`data_encoding.c`) are big-endian: the PS3 sends them
    as they are, the PC swaps them. Raw packet fields, the settings record
    and the distributed messages are the PC's little-endian memory: a new
    or changed multi-byte field in them needs its `BYTE_SWAP_*` line in the
    matching `*_byte_swap` function (see "G7 analysis", "System link"), and
    a new message kind its case in `distributed_message_entries_byte_swap`
    (`network_distributed.c`). The host takes voice chat's and map sharing's
    messages before `network_distributed_handle_message` (which swaps the
    others): `network_server_message_handler.c` swaps them there. In a swap,
    write `sizeof(*pointer)` for a struct that `port/linux/network_format.txt`
    does not list: `tools/network_format.py` takes any struct named in a
    `sizeof(struct distributed_...)` for one sent whole, and then fails.
39. PSL1GHT's libnet wrappers (`socket`, `bind`, `poll`, ...) put their
    error in newlib's one `errno`, which all threads share. `ps3_net.c`
    calls `netSocket` and co. and reads `net_errno`, each thread's own.
40. In the Xbox layer `long` is `int` (`#define long int`), so `long long`
    is `int int`: an error. Write `int64_t` and `uint64_t` there (KCP's own:
    `include/halo_ps3_kcp.h`, force-included in `p2p.o` and `ikcp.o`).
41. newlib's `pthread.h` is empty (no `_POSIX_THREADS`). The Xbox layer's
    `xbox/include/pthread.h` gives `p2p*.c` mutexes and threads on the host
    layer (`xbox/ps3_pthread.c`).
42. The portlibs' Mbed TLS 2.28 headers are on `CFLAGS`' include path. A unit
    of the vendored Mbed TLS 3.6 needs `MBEDTLS_FLAGS` before `CFLAGS`, else
    it gets the 2.28 headers (`mbedtls_ms_time_t` unknown).
43. New files in `port/ps3/xbox/` are ignored (`.gitignore` rule `xbox/`):
    add each one with `git add -f`.
44. `rsxSetTimeStamp` of the image's librsx (and PSL1GHT's source) writes
    its data to word 2 of its 2-word command: the RSX reads a stale word.
    Its report type, 0x10, stops the RSX at once (hardware, 2026-10-19: the
    blue screen). For the RSX's time use `rsxSetReport(context,
    GCM_ZPASS_PIXEL_CNT, index)`: each report holds the time (nouveau's NV40
    timestamps use this type).
45. Every make target (`check`, `pkg`, `self`) builds the ELF first. Run it
    only with your own `BUILD` folder: `./build.sh BUILD=<another session's
    folder> check` recompiles that folder with your tree.
46. `d3d8_rsx.c` keeps the game's units (852x480 or 640x480) and the RSX's
    pixels apart: `device.target_width` is in units (the screen constants),
    `target_scale` and `target_pixel_*` give the pixels (scissors, clears,
    the surface). A linear target that the game samples with texel
    coordinates needs its stage in the key's `texture_scaled`.
47. ZCull (one region, the screen's depth): a depth write with GREATER,
    NOTEQUAL, GEQUAL or ALWAYS, or a depth clear of part of it, makes its
    data stale (`zcull_invalidate`); a clear of all of it starts it again.
    A target with ZCull or a tiled region is freed only with the RSX idle.
48. The menus' tags for LAN PLAY, INTERNET PLAY and SETTINGS come from one
    `instances_grow` a load (`xbox_multiplayer_patch`): a second would take
    the first's tags as the map's (`original_count`).
49. Two threads draw (`d3d8_rsx.c`): the game's thread records the Direct3D
    calls (`front`), the Direct3D thread runs the records (`device`,
    `render_state`, `ps3_gpu.c`). A new call that changes what a draw uses
    needs a record, or the draw prologue's diff. The Direct3D thread reads
    game memory only where it stays: the tag cache (`d3d8_tag_cache_loading`
    waits before a load) and textures (`Lock`, trap 34); other vertices and
    indices are copied into the record. Game-thread code that uses the
    Direct3D thread's state or `ps3_gpu.c` calls `direct3d_drain` first.

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

The dump scan of trap 36. It compiles the game and the Xbox layer of a copy
with `-fdump-tree-original` and writes each widened 32-bit value to
`ptr32.txt` in `OUT`:

```sh
OUT=C:/path/to/a/folder
MSYS_NO_PATHCONV=1 docker run --rm -v "E:/Users/Matteo/Desktop/Halo/PS3:/src:ro" -v "$OUT:/out" ps3dev-latest:latest bash -c '
mkdir -p /tmp/w && cd /src && tar cf - --exclude=./build --exclude=./.git . | (cd /tmp/w && tar xf -)
cd /tmp/w && make -k -j12 game GAME_EXTRA=-fdump-tree-original > /out/make.txt 2>&1
make -k -j12 $(ls port/ps3/xbox/*.c port/linux/src/{bink_null,d3d8_resources,halo_linker_common,msvc_crt,msvc_wide,port_config,xbdm,xbox_xapi}.c | sed "s#^#build/ps3/obj/#; s#\.c\$#.o#") XBOX_DEFINES=-fdump-tree-original >> /out/make.txt 2>&1
grep -rn -E "\((long( unsigned)? int|__u?int64|sizetype)\) \(int\) " --include=*.original build > /out/ptr32.txt'
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
| `port/ps3/src/` | The host layer: the entry point (`ps3_main.c`), `ps3_host.c` (lv2 for the Xbox layer), video, pads, audio, log, sockets (`ps3_net.c`) |
| `port/ps3/xbox/` | The Xbox layer on the PS3: kernel, memory window, files, D3D on the RSX, XInput on the pads (`xinput_ps3.c`), DirectSound on the audio port (`dsound_ps3.c`), Winsock and XNet on the PSL1GHT sockets (`xnet_ps3.c`) |
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
- 2026-10-05: Investigation of the low frame rate in a10 (Matt). No code
  change. Results: "Found for later goals" (G7, a10).
- 2026-10-06: The vertex cache (Matt: "Let's go with the Vertex cache
  fix"): `d3d8_rsx.c`, `ps3_gpu.c` (`ps3_gpu_wait_idle` on backend label
  66, the RSX vertex cache flushed after new copies), `cache_files.c`.
  `check` passes (0 `r2off`, `.got` 0x2c20). Waiting for the hardware
  test in a10.
- 2026-10-06: Vertex cache hardware SUCCESS (Matt, PKG of 00:26): a10 at
  24 to 30 frames per second, no visible errors. The listener on UDP 18194
  is stopped. Matt asked about the sound on an SPE (flycastPS3's way); the
  log shows the sound is not the cost (see "Found for later goals").
- 2026-10-06: The timer build (Matt: "Timer build"): `ps3_timers.c` and
  `.h`, spans in `main.c`, `render.c`, `d3d8_rsx.c` and `ps3_gpu.c`.
  `check` passes (0 `r2off`, `.got` 0x2c78). PKG of 00:47. Waiting for an
  a10 run with its `[time]` lines.
- 2026-10-06: Timer build run (Matt, a10, "There was no fight"): results
  in "Found for later goals" (G7, a10). The listener is stopped.
- 2026-10-06: Matt asked about widescreen and 60 frames per second. Both
  are possible. The notes are in "Found for later goals". No code change.
- 2026-10-06: The texture hash fix (Matt: "Yes, do it"): `d3d8_rsx.c`
  (`texture_get`, `texture_cache_holds`, trap 34). The timers stay in.
  `check` passes (0 `r2off`, `.got` 0x2c78). PKG of 01:02. Waiting for the
  hardware test in a10.
- 2026-10-06: Texture hash fix hardware SUCCESS (Matt, PKG of 01:02): the
  textures look right, also in new areas. Results in "Found for later
  goals" (G7, a10). The listener is stopped.
- 2026-10-06: The RSX probe (Matt: "Yes, do it"): `ps3_timers.c` and `.h`,
  `ps3_gpu.c` (`ps3_timers_rsx_wait`), `render.c`, `main.c`. `check`
  passes (0 `r2off`, `.got` 0x2cb8). PKG of 01:20. Waiting for a run in
  the heavy scene of a10.
- 2026-10-06: RSX probe run (Matt, heavy scene): the pixel shaders cost
  the RSX time; their translation is not optimized (see "Found for later
  goals", G7, a10). The listener is stopped.
- 2026-10-06: The pixel shader optimizer (Matt: "Yes, make the shader
  optimizer."): `ps3_rsx_fp.c` (the pass before the encoding; the
  temporaries count from the program), trap 35. The probe frames are off
  (`ps3_timers.c`). PC: `tools/ps3_rsx_shader_check.sh` 0 failures (300
  keys); with `KEYS=3000` only the old NV2A mismatches; 40000 random keys
  give the same results with and without the pass. `check` passes (0
  `r2off`, `.got` 0x2d68). PKG of 02:51. Waiting for the hardware test in
  a10.
- 2026-10-06: Optimizer hardware SUCCESS (Matt, PKG of 02:51): a10 at 30
  frames per second, the RSX wait 0.0 ms. Results in "Found for later
  goals" (G7, a10). The listener is stopped.
- 2026-10-06: Matt asked if 60 frames per second is possible now: partly
  (calm scenes fit, heavy scenes need about 5 ms less on the game thread;
  the RSX not measured). Matt: "Add it to the goals as last goal": G8 in
  the goal stacks (here and GOALS.md). No code change.
- 2026-10-06: G7 started (Matt: "We will now work on G7."). Analysis of the
  memory, the saved games and the network: "G7 analysis". No code change.
- 2026-10-06: G7 step 1, the measurement build (Matt: "Do the measurement
  build."): `ps3_system.c` and `.h`, `ps3_main.c`, `ps3_gpu.c` and `.h`,
  `ps3_video.c` and `.h`, `xbox_memory.c`, `d3d8_rsx.c`, `xbox_files.c`;
  `port/ps3/README.md` (the new lines, `init.txt`). `check` passes (0
  `r2off`, `.got` 0x2e08). PKG of 09:26. The listener is stopped (Matt:
  "Stop the listener for now. I'll tell you when I'm going to run the
  test."). Waiting for Matt's word before the run.
- 2026-10-06: Red pulsing screen in a30 (Matt, the 02:51 build): the halt
  `recorded_animations.c,#354` at the intro's first recording. Cause: trap
  36 (`event_stream_start` sign-extended in a difference with a full
  pointer). Fixed with a cast (Matt: "Do these fixes."). The dump scan (476
  game units, 18 Xbox layer units) found no other fault: the hits in
  `hs_compile.c` (3, the result is cut to 32 bits) and
  `cache_files_decompress_windows.c` (2, two `HALO_PTR32` pointers) are
  correct. `check` passes (0 `r2off`, `.got` 0x2e08). PKG of 19:53, which
  also holds the G7 measurement build. The listener is stopped.
- 2026-10-06: a30 fix hardware SUCCESS (Matt: "Perfect, it worked!"). No
  listener ran, so there is no log of the run.
- 2026-10-06: Matt asked what can be done on the PC before a hardware test,
  and chose the known save items of G7 step 2 (Matt: "Do 1 then."):
  `ps3_host.c` and `.h` (`ps3_host_disk_space` with `sysFsGetFreeSize`,
  `ps3_host_file_sync`), `xbox_files.c` (`fsync` at the close of a written
  file and after `CopyFileA`; the free space and the `fsync` time in the
  log), `Makefile` (`-lsysfs`), `port/ps3/README.md`. `check` passes (0
  `r2off`, `.got` 0x2e20). PKG of 21:03, with the measurement build.
  Waiting for the measurement run.
- 2026-10-06: The measurement run (Matt: "Done"): a30 to checkpoints, two
  deaths, pause > quit, resume, an exit and a new boot, resume. The free
  space and the `fsync` work; the data is in "G7 analysis" ("Measurement
  run"). The listener is stopped. Hardware SUCCESS (Matt: "Yes, it did!":
  the resume returned to the quit point).
- 2026-10-06: The map scan (Matt: "Scan the 24 maps for their largest
  loads"): the vertex data, textures and sounds of the 24 maps, with a PC
  script. Results and the maps for the next measurement run in "G7
  analysis" ("Map scan"). No code change.
- 2026-10-06: The vertex cache growth fix (Matt: "Yes, thanks."):
  `rasterizer_xbox_draw_primitives.c` (3 calls under `HALO_PS3`),
  `d3d8_rsx.c` (`d3d8_draw_vertex_count`, the extent in `vertex_cache_get`).
  Both units compile. No PKG: the tree also holds another session's
  uncommitted network work (edits to 22:00). Waiting for Matt's word.
- 2026-10-06: The NV2A mismatches of the shader check (Matt: "Do this"):
  all 13 failures in 60000 random keys came from the combiner mux
  (`ps3_rsx_fp.c`, trap 37). Fixed. `tools/ps3_rsx_shader_check.sh`: 0
  failures (`KEYS=3000 SEED=1`, and the default 300 keys); a sweep of
  720000 keys: 0 failures. The PKG of 21:54 also holds part of another
  session's network edits; its `check` was stopped. No clean PKG yet, not
  tested on the PS3.
- 2026-10-06: G7 step 3, system link (Matt: "Can you do this?"):
  `port/ps3/src/ps3_net.c` and `.h`, `port/ps3/xbox/xnet_ps3.c` (in place
  of `xnet_null.c`), the byte order in `byte_swapping.c` and `.h`
  (`byte_swap_parts`, `BYTE_SWAP_*`), `data_encoding.c`,
  `message_header.c`, `network_game_manager.c` and `.h`,
  `network_server_message_handler.c`, `network_client_message_handler.c`,
  `network_game_globals.c`, `network_distributed.c` and `.h`,
  `network_objects.c`, `network_damage.c`, `game_engine.c` and the five
  game types; `port/ps3/README.md` ("System link"). The comments longer
  than 2 lines in the changed files are shortened (code unchanged, checked
  without comments). `check` passes (0 `r2off`, `.got` 0x2f38). PKG of
  22:52 in `build/ps3_net/` (`./build.sh -j12 BUILD=build/ps3_net pkg`):
  other sessions build in `build/ps3`. It holds the whole tree, the vertex
  cache growth fix and the mux fix too. Waiting for the test with a PC
  build (see "G7 analysis", "System link").
- 2026-10-06: The audit of the code that a10 does not run (Matt: "Audit
  the code that a10 does not run (cutscenes, vehicles, the Flood, the
  scripts of the other levels)"): six read-only audits and four checks of
  the whole tree. No halt and no thread death found; 6 PS3-only faults
  change the behaviour. Results in "Audit of the code a10 does not run".
  No code change.
- 2026-10-06: The 6 audit faults fixed (Matt: "Yes, you can fix them."):
  `device_machines.h`, `device_machines.c`, `device_controls.c`,
  `actor_perception.c`, `encounters.c`,
  `rasterizer_xbox_transparent_geometry.c`; their comments longer than 2
  lines are shortened. The 8 units that use them compile
  (`./build.sh BUILD=build/ps3_audit`, a build folder of its own). No PKG:
  the tree also holds other sessions' uncommitted work. Waiting for Matt's
  word.
- 2026-10-16: The OpenCE upstream merge (Matt: "Can you do these steps for
  me?"), in the worktree `../PS3-merge`, branch `upstream-merge`, then
  fast-forwarded into `main`. 47 conflicting files: upstream's code with the
  PS3's byte swaps, `HALO_PTR32`, `HS_LOW_*`, steps and timers kept; the
  frame wait keeps `SwitchToThread`. Build: `-iquote port/linux/game`, folder
  excludes in `tools/ps3_game_mk.py`, the port's zlib (`zlib_prefixed.h`
  keeps the host's `long`), Monocypher and stb_vorbis as host code, 32-bit
  reads in `tag_validate.c`, the new platform hooks as "none"
  (`port_stubs.c`, `d3d8_rsx.c`, `xinput_ps3.c`, `dsound_ps3.c`,
  `xnet_ps3.c`). Run 1 halted at boot: upstream's 256 MB texture cache;
  `halo_port_capacity.h` gives the PS3 the Xbox's 22 MB and 4 MB (`28070c4`).
  Run 2: hardware SUCCESS. Open: the new network messages (co-op BSP,
  votekick, voice, map share) have no byte swap, so PS3 to PC play of them
  does not work; the game state is 4 MB larger, so saves of older builds do
  not load; lowest free main memory 36 MB (t=52 s).
- 2026-10-16: The new messages' byte swap (Matt: "Let's work on the
  connectivity now! Byte-swap them."): `*_byte_swap` functions in
  `network_actors.c`, `network_coop.c`, `network_voice.c`, `map_share.c`
  and `network_objects.c` (the damage animations); `network_distributed.c`
  and `.h` (the structure BSP, the new kinds' cases);
  `network_server_message_handler.c` (the host's voice chat and map sharing
  paths, trap 38); `port/ps3/README.md` (network version 29). The other swap
  lists stay: the merge changed only byte fields of their structs
  (`structure_bsp_index`, `forced_shader_permutation_index`,
  `cooperative_flags`), and not the game's packet definitions. Checks: a PC
  script (the session's scratchpad) computes each struct's layout from the
  sources: in all 30 swap lists every multi-byte field is swapped once at
  its size and no byte field is swapped (the message header is the
  connection's); `tools/network_format.py`: the recorded format; the code of
  the 8 changed sources without comments differs from HEAD only by the
  additions (their comments over 2 lines are shortened). The changed units
  compile with GCC's warnings: none in them. `check` passes (0 `r2off`,
  `.got` 0x3498). PKG of 01:04 in `build/ps3_swap/`, which holds only this
  session's edits. Waiting for the test with a PC build of network version
  29: the tests of "G7 analysis" ("System link"), and a co-op level.
- 2026-10-16: Internet play (Matt: "Add 1, you can look at my Quake 3 port
  for reference to that. 2. For the screen, use the small screen in the Xbox
  menus that list the browser's games and joins one. 3. Sure, implement that
  one too."). The Xbox layer compiles `port/linux/src/p2p.c`, `p2p_crypto.c`,
  `p2p_discord.c`, `p2p_lobby.c`, `p2p_signal.c`, `p2p_webrtc.c` and KCP
  (traps 40, 41; in `p2p.c` and `p2p_signal.c` the network order is the
  PS3's own). The host layer compiles `port/linux/src/posix_dtls.c` with
  the vendored Mbed TLS 3.6 (`include/ps3_mbedtls_config.h`,
  `src/ps3_mbedtls.c`: lv2's random numbers and time; trap 42). New:
  `src/ps3_upnp.c` (a UPnP client for the tunnel's port), `ps3_net_resolve`
  (DNS with `netGetHostByName`, as the Quake 3 port's `code/sys/ps3_net.h`
  does with `gethostbyname`) and `ps3_net_secure_random` in `src/ps3_net.c`,
  `xbox/posix_ps3.c` (`posix.h` on `src/ps3_net.c`; Discord: -1),
  `xbox/ps3_pthread.c`. `xbox/xnet_ps3.c` has `xnet.c`'s internet play
  hooks, and writes `brokers.txt` beside `config.toml` at each start
  (`port/assets/network/brokers.txt`, as `$(BUILD)/brokers_txt.h`). The
  Xbox menus (`HALO_PS3`): `menu_tags.c` (`xbox_internet_patch`) puts
  INTERNET PLAY after SYSTEM LINK PLAY (its description is the fifth) and
  makes copies of the System Link game list, its description and message,
  named under `connected\internet\`, so that the stock region check
  applies; `menu_functions.c` ("xbox internet ...") fills them from the
  server browser's listings (9 rows that scroll at their ends), joins as
  the server browser does (`lobby_browser_join`) and opens the Xbox's
  pregame screen; `ui_widget_event_handler_functions.c` lets those tags run
  the port's functions. Refused: games with a password, closed games.
  Not done: Discord (no client on the PS3); a game the PS3 hosts is not
  listed (its invite is in the log). Checks: the changed game units compile
  with GCC's warnings (none new); `tools/network_format.py`: the recorded
  format; the comments longer than 2 lines in the changed files are
  shortened (the code without comments is the same). `check` passes (0
  `r2off`, `.got` 0x7ee8). PKG of 03:03 in `build/ps3_p2p/`. The new files
  in `port/ps3/xbox/` need `git add -f` (trap 43). Matt's run of 03:08 (the
  log, before his report): STUN gave the public address; 4 public games
  joined from INTERNET PLAY (ratrace; hangemhigh, 44 players, in progress;
  wizard; damnation, in progress, 30 fps). In the 44-player game the PS3 ran
  at 10.8 fps (0.82 game ticks a frame) and its client ended after 10 s.
- 2026-10-16: LAN PLAY (Matt: "Can you replace the menu voice of "SYSTEM
  LINK PLAY" with a "LAN PLAY" browser? So we get both of the worlds.").
  Asked whether LAN PLAY keeps CREATE GAME; Matt: "Keep CREATE GAME". The
  patch (now `xbox_multiplayer_patch`) puts LAN PLAY in SYSTEM LINK PLAY's
  place (its description in the same place) and INTERNET PLAY after it. LAN
  PLAY's screen is a copy of the System Link game list (`connected\lan\`,
  with CREATE GAME and the stock description and message). Its functions
  ("xbox lan ...", `menu_functions.c`): the client's games, the open ones
  first, without internet play's hosts (`game_from_peer`); A or START joins
  the focused game as player 1 (`multiplayer_player`, `ui_widget_port_join`,
  the pregame screen); Y, or A or START while it finds none, hosts
  (`ui_widget_port_host`, then the map select), as System Link's list does.
  The description panel's update is shared with INTERNET PLAY
  (`xbox_describe`, with the score limit for LAN games). The changed units
  compile with GCC's warnings (none); `check` passes (0 `r2off`, `.got`
  0x7ee8). PKG of 03:41 in `build/ps3_p2p/`. Not tested on hardware.
- 2026-10-18: Invite codes (Matt: "what if we made a shortener (tied to
  Github pages) ... convert the URL into a few digits", then "Yes. Let's go
  with this."). A stateless conversion cannot be short (an invite is 256
  random bits), so a code names a retained message on the MQTT brokers:
  topic `hceu/3/code/<HMAC of the code's key>`, the invite sealed with the
  key (HMAC-SHA256 stream and tag), the key PBKDF2-HMAC-SHA256 of the 8
  digits (100000 rounds, salt "hceu invite code 1"), MQTT 5 expiry 30
  minutes. `p2p_crypto.c` (`p2p_code_*`), `p2p.c` (`p2p_invite_code`: the
  host's code, made on the first call while it hosts and kept for the
  token; `p2p_look_up_code`, `p2p_code_found`, `p2p_stop_code_lookup`),
  `p2p_signal.c` (the code retained on each broker, published again on
  each connection and every 10 minutes, cleared when it changes; the
  looked-up topic subscribed). Menus (`HALO_PS3`): INTERNET PLAY's Y runs
  "xbox internet code" (the Xbox keyboard, its caption the port's:
  `virtual_keyboard_port_caption`, a `port:` change in
  `source/interface/virtual_keyboard.c`; Done takes the text without the
  file name checks); the code found is joined as a listing is; the status
  shows the lookup and what went wrong for 5 s. CREATE GAME's key copy says
  "= ENTER CODE" on INTERNET PLAY. The pregame screen has a new text child
  ("xbox invite code": "Code: 1234 5678", at 82/376, right-aligned). The
  page `port/ps3/invite/index.html` (one file, its own MQTT 5 client over
  WebSockets, as `port/web/site/net.js`): link to code and code to link.
  Checks: the C key, topic and payload equal a Python reference and the
  page's (node, 3 codes); the page made a code and found it on EMQX, HiveMQ
  and Mosquitto, then cleared it (HiveMQ answers in about 8 s, the others in
  under 1 s); a host x86 PBKDF2 takes 54 ms. The changed units compile with
  GCC's warnings (none new); `check` passes (0 `r2off`, `.got` 0x7ee8). The
  network version is unchanged (no message between machines changed). The
  comments over 2 lines in `p2p.h`, `p2p_internal.h` and
  `virtual_keyboard.c` are shortened. PKG of 22:02 in `build/ps3_code/`.
  Not tested on hardware.
- 2026-10-18: Invite codes on hardware: SUCCESS (Matt: "I tested it and it
  works as intended!"). PKG of 22:02 in `build/ps3_code/`.
- 2026-10-18: G8 analysis, 60 frames per second and 720p (Matt: "The next
  step is to get this to 60 FPS and 720p. Analyze what can achieve this."):
  "G8 analysis". The game thread misses 16.7 ms only in heavy scenes (up to
  5 ms); the RSX is not measured since the shader optimizer; 1280x720 is 3
  times the pixels. `rsxSetTimeStamp` of the image is faulty (use
  `rsxSetReport` with type 0x10). No code change.
- 2026-10-19: G8, the fixes (Matt: "Just go with the fixes. We'll measure
  from top-down then."; the video settings "Inside SETTINGS", defaults
  widescreen and 60): `port_stubs.c`, `d3d8_rsx.c`,
  `xbox_texture_layout.c`, `ps3_platform.h`, `ps3_gpu.c` and `.h`,
  `ps3_video.c` and `.h`, `ps3_system.c` and `.h`, `ps3_timers.c` and `.h`,
  `ps3_rsx_fp.c`, `ps3_rsx_asm.c`, `ps3_rsx_program.h`, `menu_tags.c`,
  `source/main/main.c` and `source/render/render.c` (their `port:` timer
  comments), `tools/ps3_rsx_shader_check.c`, `port/ps3/README.md` ("Video
  settings"), traps 44 to 48. See "G8 analysis" ("The fixes"). `check`: 0
  `r2off`, `.got` 0x7f80. PKG of 00:13 in `build/ps3_g8/`. A `check` with
  `BUILD=build/ps3_code` (by mistake, trap 45) recompiled 82 of that
  folder's objects before it was stopped: they are deleted, its ELF and PKG
  of 22:02 are unchanged. Waiting for the hardware test.
- 2026-10-19: G8 fixes on hardware: FAILURE (Matt: "There's a blue
  screen."). Log: "[gpu] frame 4: the RSX did not finish frame 2 (label 0)"
  at each frame; the game runs a frame each 2 s (the fence timeout); the
  display thread's dark blue stays; the boot lines are missing. Diagnostic,
  no fix: `RSX_CHECKPOINTS` in `ps3_gpu.c` (a command label and a back-end
  label after each kind of command; at a fence timeout, the last ones the
  RSX took and finished), frame labels in `ps3_video.c` (end, flip,
  finished; also in the `[hb]` heartbeat). `check`: 0 `r2off`, `.got`
  0x8030. PKG of 00:50 in `build/ps3_g8/`.
- 2026-10-19: The diagnostic on hardware (Matt: "There."): at each fence
  timeout, "the front end took 12 (the timestamp report; next the frame
  label), the back end finished 11 (the present blit)": the RSX stops at
  the game's first `rsxSetReport` with type 0x10 (the first report it
  gets). The display thread's 4 frames all finished. Fix: `REPORT_TIMESTAMP`
  is `GCM_ZPASS_PIXEL_CNT` (1); trap 44 corrected. `RSX_CHECKPOINTS` stays 1
  for the next run (ZCull, Z compression, native textures and resident
  programs have not run on the RSX yet). `check`: 0 `r2off`, `.got`
  0x8030. PKG of 01:00 in `build/ps3_g8/`.
- 2026-10-19: PKG of 01:00 on hardware: SUCCESS (Matt: "It runs!"), menus
  and a30 for about 5 minutes, no RSX stop, a clean exit. Tiled targets, Z
  compression, ZCull, native textures and resident vertex programs ran.
  The `[time]` lines (diagnostic on): menus 59.9 frames per second, 16.7
  ms (render 2.2, RSX 4 to 6 ms). a30: calm 54 to 60 (ticks 8 ms each,
  render 9 to 13, RSX 7 to 13 ms); heavy combat 33 to 37 (ticks 11 to 16
  ms each at 0.85 to 0.9 a frame; render 15 to 20, of it Direct3D draws 6
  to 8 and sound 1.3 to 2.7; RSX 14 to 18 ms, once 25). No `screen:` line
  and no 30 frames per second window: WIDESCREEN and FRAME RATE not seen
  changed. `RSX_CHECKPOINTS` 0 (its names only with it): clean PKG of
  01:14 in `build/ps3_g8/`, `check` 0 `r2off`, `.got` 0x7f98. Heavy scenes
  miss 16.7 ms: Matt's condition for step 5 (the second hardware thread).
- 2026-10-19: G8 step 5, the Direct3D thread (Matt: "Yes, the picture
  looked right. I'll try the widescreen/30 FPS changes in future. Let's go
  with Step 5."): `d3d8_rsx.c` (the records, the ring, `direct3d_thread`),
  `ps3_gpu.c` and `.h` (`ps3_gpu_timestamp`, the stop hook),
  `ps3_main.c` (the exit and the halt stop the thread), `ps3_timers.c` and
  `.h` (`[time] Direct3D thread`), `d3d8_resources.c` (`IsBusy`,
  `BlockUntilNotBusy` and a release follow the thread under `HALO_PS3`),
  `source/cache/cache_files.c` (`d3d8_tag_cache_loading` before each
  load), `port/ps3/README.md`, traps 34 and 49. See "G8 analysis" ("Step
  5"). `check`: 0 `r2off`, `.got` 0x7fa0. PKG of 01:58 in
  `build/ps3_g8/`. Not tested on hardware.
- 2026-10-19: PKG of 01:58 (the Direct3D thread) on hardware: runs, no
  error, no draw skipped (Matt: "The framerate still feels sluggish.").
  The `[time]` lines: menus 59.9; a30 calm 57 to 60, heavy combat 38 to 47
  (before: 33 to 37), once 23 (1.3 ticks a frame of 19 ms, sound 5.7 ms).
  The game thread's render 9 to 18 ms (before 15 to 21); it never waits
  for the Direct3D thread in a level. Left: the ticks, 9 to 19 ms each
  (they hold `game_frame`, which runs every frame: particles, widgets,
  sound), every second frame, so frames alternate about 25 and 12 ms; the
  RSX 8 to 19 ms. Main memory 44 to 46 MB free. Matt asked about the SPEs
  and VMX: not for the tick (network results must match the PC builds:
  SPU single precision and VMX's fused multiply-add round otherwise; the
  tick's code is branches and pointers); VMX later, for loops that only
  draw.
- 2026-10-19: The tick's measurement and the sound at the ticks' rate
  (Matt: "Do the measurement. Also build the audio on tick frames."):
  `game.c` (`PS3_GAME_SPLIT`: units, AI, players, effects, first person,
  scripts, objects, HUD, other; `game_frame`: particles, widgets, sound,
  other), `game_time.c` (the interpolation's and the netcode's step of a
  tick), `ps3_timers.c` and `.h` (`[time] tick, ms each`, `game frame, ms
  a frame`), `main.c` (`sound_render` with the frames between ticks only
  on a tick's frame, else every second frame). `check`: 0 `r2off`, `.got`
  0x7fa8. PKG of 02:25 in `build/ps3_g8/`. Not tested on hardware.
- 2026-10-19: PKG of 02:25 on hardware (Matt: "Done. Check the ticks."): a30
  heavy combat, a frame of 28.5 ms (35 frames per second): the game's
  render 16.8 (objects 5.7, effects 3.0, `sound_render` 2.7, interface 2.0,
  lights 1.1, diffuse 1.1), the ticks 7.2 a frame (`objects_update` 4.4, AI
  1.6, interpolation 0.6), `game_frame` 3.9 (particles 2.0, game sound 1.9).
  A tick: `objects_update` 2.4 to 7.9 ms, AI up to 6.0, interpolation 0.7,
  effects 0.4, scripts 0.2, the rest under 0.1 each. The sound, 2.5 to 5 ms
  a frame (`game_sound_update` 1.7 to 2.7 at every frame). No sound fault
  reported with `sound_render` at the ticks' rate.
- 2026-10-19: The sound, and the next measurements (Matt: "Yes, update the
  sound then expand the measurements for the objects_update, and the
  render's objects and effects passes."): `game.c` (`game_sound_update`
  at the ticks' rate, as `sound_render`, with the frames' time added; its
  `dt` is unused today), `dsound_ps3.c` (the voices' and the listener's
  settings without the mixer's lock: words the mixer reads once a chunk;
  the orientation normalized before it is stored), `objects.c` (the type
  updates by kind: units, items, projectiles, others; damage, node
  matrices, lights, values; activation, garbage collection), `render.c`
  (`PS3_TIMER_PART`: the CPU's parts of a split without an RSX stamp: sky,
  first person, lights; weather, particles, contrails, transparent,
  decals, detail objects), `render_objects.c` (find, first person,
  lighting, models), `ps3_timers.c` and `.h` (`[time] objects_update`,
  `[time] objects pass`; the render line's objects and effects stay whole
  passes). `check`: 0 `r2off`, `.got` 0x7fb8. PKG of 02:50 in
  `build/ps3_g8/`. Not tested on hardware.
- 2026-10-19: PKG of 02:50 on hardware (the first run was the PKG of 02:25
  again: no new lines; the second the right one). No sound fault reported.
  `game_sound_update` about 1.3 ms a frame in calm play (before 1.8): about
  2.5 ms a call. A tick's `objects_update`: units' type updates 0.6 to 1.2
  ms calm, 4.8 to 6.4 in heavy combat; node matrices 0.7 to 1.7; items and
  projectiles up to 0.9; the rest about 0.8. The render's `render_objects`
  2.2 to 8.0 ms a frame, of it the models 1.4 to 6.7 (find, first person
  and lighting each under 0.5); the sky about 0.25. Effects: particles 0.3
  to 1.8, transparent 0.2 to 1.5, contrails up to 0.7 (and the particles'
  update in `game_frame` 0.5 to 3.2). The interface (HUD) about 2 ms in
  every frame of play.
- 2026-10-19: Immediate mode, cheaper (Matt: "Make the immediate mode
  cheaper."): `d3d8_rsx.c` stores each vertex's registers of a layout (the
  ones written up to the first vertex) in place of all 16 (256 bytes);
  `End` copies the block once. A register first written after the first
  vertex widens the layout, with its value before for the vertices before
  (`immediate_widen`), as each vertex had all 16. The Direct3D thread's
  side is the same. `check`: 0 `r2off`, `.got` 0x7fb8. PKG of 03:16 in
  `build/ps3_g8/`. Not tested on hardware.
- 2026-10-19: PKG of 03:16 on hardware (Matt: "Done"): the interface 1.5 to
  2.1 ms a frame in play, as before (about 2): no measurable change.
- 2026-10-19: The measurements for 1, 2 and 3 (Matt: "Prepare the
  measurements for 1, 2 and 3, so we can investigate all 3 issues and have
  a median."): `sound_manager.c` (`sound_render`: listener, with
  `begin_scene` and the class fades; looping sounds, refresh, prioritize,
  channels; cache, with `end_scene`), `game_sound.c` (`game_sound_update`:
  environment, looping sounds), `object_types.c` (a type update's parts:
  unit, for bipeds and vehicles; biped, vehicle, item for all items,
  projectile, other), `bipeds.c` (`biped_update`: turning, moving, dead or
  airborne or landing or slipping, animation, the rest), `render.c` (the
  interface pass: fog, flares and antialiasing; `interface_draw_screen`;
  flash; widgets), `interface.c` and `hud.c` (screen effect, players,
  motion sensor, weapon, unit, navigation, messages, game engine),
  `d3d8_rsx.c` (the game thread's time in the four draw calls and in
  `SetVertexShaderConstant`, in the whole frame, without its waits for
  ring room), `ps3_timers.c` and `.h` (`[time] unit parts`, `[time] sound`,
  `[time] interface pass`; the render line's interface stays the whole
  pass). The medians come from the play windows' lines. `check`: 0
  `r2off`, `.got` 0x7fd0. PKG of 03:42 in `build/ps3_g8/`. Not tested on
  hardware.
- 2026-10-19: PKG of 03:42 on hardware (Matt: "Done."), a30, 20 windows of
  play; medians of all of them, then of the 8 under 45 frames per second.
  The frame 19.9 / 27.1 ms, the RSX 9.9 (the game thread sets the rate).
  The sound, ms a frame: `game_sound_update` 1.50 / 1.92, nearly all of it
  the looping sounds' loop (each audible one's `sound_refresh_looping`, with
  `compute_sound_obstruction`'s ray test through the BSP and the objects);
  `sound_render` 1.10 / 2.15: listener 0.60 / 0.78 (with `begin_scene`:
  `DirectSoundDoWork`, the mixer's lock and the stream packets' decode on
  the game thread), refresh 0.30 / 0.59, channels 0.15 / 0.58 (worst 2.1),
  the rest under 0.1. The mixer holds its lock for a whole chunk: 0.14 to
  0.54 ms on average, 2.0 at most, with up to 32 streams. The units, ms a
  tick: `objects_update`'s units 1.73 / 2.92; biped 0.60 / 1.54, of it
  moving 0.56 / 1.32 (`biped_update_physics`), the other steps under 0.1
  each; vehicle 0.42 / 1.70 (4.1 in the worst window); unit 0.28 / 0.42.
  The interface, ms a frame: 1.90 / 2.00, the same in calm play and in
  combat: weapon 0.92 / 0.95 (crosshairs, weapon and grenade HUD), unit
  0.80 / 0.83, the rest 0.1 or less. The game thread's Direct3D calls in
  the whole frame: draws 1.75 / 1.73 (158 to 435 draws a frame), constants
  0.16 / 0.15. Also: AI 1.91 / 4.38 ms a tick, the particles' update 1.32
  / 1.96 and draw 0.92 / 1.28 ms a frame, the models 2.24 / 1.91.
- 2026-10-19: Steps 1 to 3 (Matt chose all three: "3. HUD draw timers, 2.
  Sound off game thread, 1. Collision by user"): `collision_usage.c` and
  `.h`, `collisions.c`, `collision_models.c` (the collision tests by user
  and kind), `dsound_ps3.c` (the decode on the mixer's thread, the lock
  held only to take and give back a pass's packets),
  `rasterizer_xbox_dynavobgeom.c` and `render.c` (the screen quads, the
  interface pass's Direct3D calls), `ps3_timers.c` and `.h` (`[time]
  collision`, `[time] collision by user`, `[time] screen quads`, the
  sound's lock waits), `port/ps3/README.md`. See "G8 analysis" ("Steps 1
  to 3 of 2026-10-19"). The PC check of `dsound_ps3.c` passes. `check`: 0
  `r2off`, `.got` 0x8048. PKG of 10:59 in `build/ps3_g8/`. Not tested on
  hardware.
- 2026-10-19: PKG of 10:59 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, a clean exit (`[ps3] exit 0`), no halt; every
  `DirectSound:` line 0 silent and 0 partial port blocks. 31 timer
  windows, 26 of play (game ticks of 1 ms or more each), 9 under 45
  frames per second; medians all / under 45 (the same method as the run
  of 03:42). The frame 18.05 / 23.80 ms (55.5 / 41.9 frames per second;
  0.53 / 0.70 ticks a frame), game ticks 7.75 / 11.10, render 9.30 /
  13.30; the RSX 9.2 / 12.5; the Direct3D thread busy 10.5 / 10.2. The
  sound, ms a frame: `sound_render` 0.30 / 0.70 (listener 0.03 / 0.04,
  before 0.60 / 0.78; channels 0.03 / 0.04, before 0.15 / 0.58; refresh
  0.22 / 0.58), `game_sound_update`'s looping sounds 1.51 / 1.65, the
  game thread's waits for DirectSound's lock 0.00 / 0.00. The mixer
  thread (15 lines of play): 484 us a chunk (216 to 838; max 2.5 to 5.2
  ms), decoding 202 (85 to 350; about 0.49 ms a packet), the lock held
  50 us a chunk (max 0.13 to 0.82 ms), 725 packets in 10 s. The collision
  tests, ms a frame (tests a frame): total 3.63 / 5.89 (270 / 554);
  vector 3.24 / 5.04, sphere 0.09 / 0.37, move 0.33 / 0.16, pill and
  point 0.00; sound 1.60 / 1.94 (19.3 / 26.9; 72 / 68 us a test), ai-los
  0.68 / 0.64 (5.95 / 5.8; 106 us), pt-phys 0.47 / 0.65 (213 / 364; 1.9
  us), biped 0.33 / 0.16 (0.85 / 0.8; 84 / 100 us), veh 0.15 / 0.59 (21
  / 50; 7 / 13 us), debug 0.20 / 0.22 (1.0; about 200 us: the obscurer
  ray of `rasterizer_models_begin`), unknown 0.00 / 0.11 (0 / 36), light
  0.04 / 0.05, the others 0.00. The interface: screen 1.83 / 1.91 (weapon
  0.94 / 0.98, unit 0.82 / 0.85); 19 screen quads a frame: states 0.01,
  constants 0.03, textures 0.03, shaders 0.14, draw 0.15; the interface
  pass's Direct3D draws 0.17 / 0.21 and constants 0.02 (the whole frame's
  1.68 / 2.19 and 0.16 / 0.26). Also: AI 2.08 / 2.07 ms a tick,
  `objects_update` 4.48 / 4.62 ms a tick (units 1.77 / 2.44), the unit
  parts (ms a tick): biped 0.85 / 0.58 (moving 0.62 / 0.32), vehicle 0.44
  / 1.32; particles' update 1.09 / 1.59 ms a frame. No sound fault (Matt:
  "I didn't hear any sound faults.").
- 2026-10-19: The next steps 1 to 3 (Matt: "Go with 1-3. Be aware that we
  can also delegate tasks to the SPEs."): `game_sound.c` (the obstruction
  kept up to 4 ticks, 512 slots, counted), `collision_usage.c` and `.h`,
  `collision_bsp.c`, `collisions.c` (each logged collision function at any
  depth, the BSP nodes, the objects tested), `hud_weapon.c`, `hud_unit.c`,
  `motion_sensor.c` (the HUD's parts and their quads), `ps3_timers.c` and
  `.h` (`[time] collision by function`, `[time] HUD parts`, the
  obstruction counts in `[time] sound`), `port/ps3/README.md`. See "G8
  analysis" ("The next steps 1 to 3"). The changed units compile with
  GCC's warnings (none new). `check`: 0 `r2off`, `.got` 0x80b8. PKG of
  11:40 in `build/ps3_g8/`. Not tested on hardware.
- 2026-10-19: PKG of 11:40 on hardware (Matt: "Done."; our listener):
  menus, a30 with a vehicle, `[ps3] exit 0`, no halt; `DirectSound:` 0
  partial blocks, 1 silent block at the boot only (before any sound). 21
  timer windows, 20 of play, 5 under 45 frames per second; medians all /
  under 45. The frame 18.80 / 27.30 ms (53.2 / 36.7 frames per second;
  0.54 / 0.70 ticks a frame), game ticks 6.20 / 11.40, render 10.50 /
  11.50; the RSX 9.85 / 11.60; the Direct3D thread busy 11.8 / 8.2. The
  sound: obstruction calls 36.4 / 49.8 a frame, computed 8.2 / 13.1;
  `game_sound_update` 0.72 / 0.77 ms a frame (looping 0.71 / 0.76);
  `sound_render` 0.20 / 0.50 (refresh 0.10 / 0.32); lock waits 0.00. The
  mixer thread (12 lines of play): 469 us a chunk (172 to 1049; max 2.4 to
  6.5 ms), decoding 206 (62 to 438), the lock held 50 us a chunk (max 0.16
  to 0.77 ms). The collision tests 2.68 / 6.37
  ms a frame (390 / 833 tests): vector 2.33 / 5.28, sphere 0.20 / 0.99,
  move 0.23 / 0.19. By user (ms, tests a frame): pt-phys 0.56 / 1.15 (291
  / 638), sound 0.58 / 0.93 (7.3 / 11.6), veh 0.33 / 2.39 (36 / 78),
  ai-los 0.36 / 0.40 (3.1 / 4.4), biped 0.23 / 0.19, debug 0.21 / 0.22
  (1), unknown 0.08 / 0.13 (25 / 41). By function (ms, calls a frame, at
  any depth): vector-structure 0.35 / 0.83 (48 / 143), vector-objects 1.07
  / 2.91 (30 / 60), features-in-sphere 0.40 / 1.07 (21 / 39), model-vector
  0.23 / 0.27 (26 / 30), object-bsp-vector 0.17 / 0.20 (68 / 82),
  structure-bsp-vector 0.43 / 0.83 (77 / 185), object-bsp-sphere 0.04 /
  0.08, structure-bsp-sphere 0.09 / 0.16 (21 / 39); BSP nodes a frame:
  object vector 620 / 703, structure vector 1881 / 4284, structure sphere
  473 / 785; objects tested a frame 1235 / 5655. The HUD (ms a frame,
  quads): interface screen 1.75 / 2.04 (weapon 0.91 / 1.06, unit 0.80 /
  0.88); crosshairs 0.16 / 0.21 (1), weapon panel 0.49 / 0.56 (8),
  grenades 0.24 / 0.27 (4), state and sounds 0.00; unit setup 0.02,
  background 0.00, shield 0.18 / 0.17 (2), health 0.18 / 0.20 (2), motion
  sensor elements 0.17 / 0.18 (2), motion sensor 0.18 / 0.26 (0 quads;
  update 0.01, render 0.17 / 0.25), auxiliary 0.00; 19 screen quads a
  frame. Also: AI 1.88 / 1.83 ms a tick, `objects_update` 3.83 / 7.26 ms
  a tick (units 1.84 / 4.81, node matrices 1.15 / 1.10), unit parts
  vehicle 1.07 / 3.41 ms a tick, the particles' update 1.33 / 2.57 ms a
  frame. Matt: "Write the SPEs parts and the vmx inside GOALS and
  ROADMAPS in case. Those are extremely worth checking out.": "G8
  analysis" ("SPEs and VMX") and GOALS.md ("SPEs and VMX").
- 2026-10-19: The handoff (Matt: "Update ROADMAP and GOALS. I'll start a
  new session."): "Now", the goal table and GOALS.md updated. No code
  change after the PKG of 11:40.
