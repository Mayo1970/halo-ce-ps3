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
7. Comments: only for logic that is not obvious. Our own (port/ps3,
   tools/ps3_*, HALO_PS3 blocks, our port: comments) at most 2 lines, and
   keep the "why"; upstream's comments stay as they are (fewer merge
   conflicts: tools/ps3_upstream_check.py --merge takes them).
8. Commits and PRs: no co-author line, no AI attribution.

## Now (2026-10-22)

- 2026-10-24: the shader pack on hardware (b40, PKG of 17:12 in
  `build/ps3_shader_check/`): `0 programs differ`, 1 pixel shader of the
  whole run translated at a draw; Matt: the stutter only at the start.
  Then (Matt: "do the hash and the conversion on a SPU"; and the BUTTON
  ICONS style not in game): texture uploads as tasks of the conversions'
  SPU (`spu_vertices.c`: TEXTURE, HASH; `xbox_texture_layout.c`:
  `xgpu_texture_rsx_task`); the texture cache's textures upload at a load
  there without a hash, the others' hash (a new one, 16 lanes of FNV-1a)
  on the SPU from 256 KB. PC check (spusim, scratchpad): 3,000 textures and
  300 hashes, 0 apart; 11 of 12 faults found (the 12th changes nothing).
  The HUD's button prompts (`hud_hires_tags.c`, `hud_hires_button_draw`)
  take the PS3's icons on the PS3 (XBOX: the map's). `check` passed; PKG of
  17:58 in `build/ps3_texture/` (the tree of `dfc65142` and this). Not
  tested on the PS3.
- 2026-10-24: OpenCE upstream merge 2 (Matt: "Can you do both?": the merge
  and an automatic check). Worktree `../PS3-merge`, branch
  `upstream-merge-2`: `0fbd0a3f` merges `5e27f3a2` (376 commits, network
  version 38, 30 conflicts) into `f84f09ea`; `1b3cb0d8` adds
  `tools/ps3_upstream_check.py` (report: conflicts, network version, new
  messages, hooks, pointer structs; `--build`: merge and PS3 build in
  `../PS3-upstream-check`). Not in `main` yet. `check` passed; PKG of 13:42
  in `../PS3-merge/build/ps3_merge/`. Run on hardware (Matt: "Seems like it
  works"): a30 played, its saved game loaded, `[ps3] exit 0`; lowest free main
  memory 17.6 MB (t=196 s); 28 `ps3_net_peer_name: lv2 error 57` at the
  end (the network screens). Then an internet game joined (Matt: "all 3"
  work: lobby, a game, a join in progress): host of version 38 at 12:13:01,
  "successfully joined", a game, then "Next map: Wizard"; lowest free main
  memory 36.6 MB; `[ps3] exit 0`. Then (Matt: "You can make them in the
  meantime"): `16574b91` swaps messages 90 and 91 and Infection's state (the
  chat's messages and the settings record's new bytes are bytes only), and
  the check reports `source/rasterizer` changes (a new `shader_keys.txt`
  sweep). `check` passed; PKG of 14:14 in `../PS3-merge/build/ps3_merge/`.
  Not tested on the PS3. After the shader pack's commit (`431af528`), main
  was merged into the branch (`dfc65142`, no conflict) and `main`
  fast-forwarded to it. `check` passed (pack: 357 pixel, 67 vertex
  shaders; `.got` 0x87c8); PKG of 17:29 in `../PS3-merge/build/ps3_merge/`.
  Not tested on the PS3. The merge changed 13 files of `source/rasterizer`:
  `shader_keys.txt` needs a new sweep. Open: begin game's 16-bit
  time is the long's high half on the PS3 (older too; the join in progress
  worked).
  `port/ps3/xbox/{include/pthread.h,posix_ps3.c,ps3_pthread.c,hud_hires_rsx.c}`
  are in no commit (`.gitignore`'s `xbox/`): copied into the worktree.

- 2026-10-22 to 10-24: Matt: "Work on shader.md" (the shader pack). Steps 1,
  2, 4 and 5 done (shader.md, "Progress"; the audit: `shader_audit.md`);
  step 3 (Matt chose the static sweep): `tools/ps3_shader_sweep.py` on the
  Windows build of the game (built in this tree: `python configure.py
  --pgo=off --lto=off`, Visual Studio's ninja), 357 keys from the 24 maps.
  `check` passed. PKG of 17:12 in `build/ps3_shader_check/`
  (`SHADER_PACK_CHECK=1`; the tree of `f84f09ea`, this work and the
  buttons session's uncommitted PNGs). Not tested on the PS3. New files:
  `tools/ps3_shader_pack.c`, `tools/ps3_shader_sweep.py`,
  `tools/ps3_shader_sweep/`, `port/ps3/shader_keys.txt`, `shader_audit.md`.

- 2026-10-22: Matt: "Change video settings into "Extra options". Implement
  the button changes." SETTINGS' VIDEO SETTINGS is EXTRA OPTIONS, with a
  third row BUTTON ICONS: XBOX, STYLE A (the default), STYLE B
  (`display.button_icons`). The PS3 icons stand for the menus' button
  bitmaps and the message icons' A, B, X and Y (`xbox/hud_hires_rsx.c`,
  `tools/ps3_button_assets.py`; port/ps3/README.md, "Extra options").
  `check` passed. PKG of 18:35 in `build/ps3_buttons/` (the tree of
  `96f6f6d5` and this change only). Not tested on the PS3. The new file
  `port/ps3/xbox/hud_hires_rsx.c` needs `git add -f` (`.gitignore`'s
  `xbox/`).

- 2026-10-22: G9 started (Matt: "Work on Goal 9."). The analysis is done:
  see "G9 analysis". Then (Matt: "Work on step 2, 3, 4 and 5 together.
  Enough timers."): the render SPU (a fourth SPU thread group: the
  shadows, the weather and four new jobs: the lights' surfaces, the
  window's visible surfaces, the particles' sprites, the contrails'
  strips), and the window's list walked once for its passes. PC check
  passed (16.5 M checks, 0 apart; 17 injected faults, each found). PKG of
  17:18 in `build/ps3_g9/` (the tree of `8bee7864` and this change only).
  Run on hardware (b40; Matt: "Run's done."; no fault reported): a clean
  exit, no `SPU off`; render 7.8 ms a frame on median (the 15:18 run 9.0);
  the SPU waits 0. See the Log. (Matt committed it as `4a313dde`.) Then
  (Matt, on the BSP switch's stutter: "let's go with option 1", the decals
  and the shadows' sort): file reads in lv2 calls of 128 KB; the decals'
  batch in one call; the shadows' sort and triangles on the render SPU.
  PC check passed. Built, not tested on the PS3. PKG of 18:00 in
  `build/ps3_g9/` (the tree of `4a313dde` and this change only).
- 2026-10-22: G8, the SPU offload (Matt chose all four candidates: 1 the
  checkpoint's copy, 2 the weather, 3 the effects' particles, 4 the
  shadows' surfaces): a third SPU thread group, the game thread's jobs
  (`spu/spu_game.c`). Built, not tested on the PS3. PKG of 14:12 in
  `build/ps3_spe/` (the tree of `b54ebca8` and this change only). PC check
  passed (the real SPU image in flycastPS3's simulator against
  transcriptions of the game's functions, on the 81 BSPs of the 23 Xbox
  maps; six faults each fail it). See the Log. Run on hardware (Matt:
  "Done."): a clean exit; the shadows work (6 us a shadow on the game
  thread, before 103; 358 checks, 0 apart); the particles give nothing
  (the wait); the checkpoint went to the PPE (sizes not 16-byte
  multiples); the weather did not run (no weather in the run). See the
  Log. Then (Matt: "Do 1 and 2 then. For 2, pick a."): the checkpoint's
  edges on the PPE, the particles in jobs of 64. Built, not tested on the
  PS3. PKG of 14:33 in `build/ps3_spe/`. Run on hardware (b40; Matt:
  "Done"; no fault reported): `[ps3] exit 0`; the checkpoint copied by an
  SPU in 3.1 ms (the PPE 12.9 to 15.1); the particles 3.8 to 2.9 us a
  particle; the weather in the snow on the SPU (the game thread 1.2 to
  0.05 ms a frame); no revert ran. See the Log. Then (Matt: "Work on these
  2"): the pixel shaders' copies with their constants on the conversions'
  SPU, and the particles' rays taken at the next frame's start. Built, not
  tested on the PS3. PKG of 15:18 in `build/ps3_spe/`. Run on hardware
  (b40 with a death; Matt: "Done."; no fault reported): `[ps3] exit 0`;
  every pixel shader copy by the SPU, but `colours` only 3.61 to 3.50 us
  a draw; the particles' wait gone (2.89 to 2.57 us a particle); the
  revert's copy put back by an SPU in 2.8 ms. See the Log.
- 2026-10-22: the next goal is G9 (GOALS.md; Matt: "Save that as the next
  goal. Maybe we can delegate more tasks to the SPE for the Game thread's
  render."): in heavy scenes the game thread sets the rate (12 windows
  under 50 fps: ticks 5.15 + render 14.65 of a 21.6 ms frame; the
  Direct3D thread is not waited for), so more of the game thread's render
  goes to the SPEs. Render's parts in those windows: objects 3.2, effects
  2.3, specular 2.1, lights 1.65, diffuse 1.3, interface 1.25, lightmaps
  1.05, visibility 0.4 ms a frame. Not started.
- Handoff (2026-10-22, end of the session; Matt: "Save these in memory.
  I'll start a new session soon."): G8 is in progress. The last PKG is
  11:54 in `build/ps3_spu/` (run on hardware: a clean exit, no fault
  reported). Not committed since `0966e23c` ("Further optimization"):
  everything of 2026-10-20 and 2026-10-22 but the kept boxes (taken out
  after two crashes): the shorter pixel shader translation, the
  translator's fixed buffers, the weather's rays without surfaces, the
  stream buffers' blocks, the sphere query on the kept blocks, the step
  marks in `render_player_frame` and `render_window`, and the timers (22
  files in `git status`). No listener runs on UDP 18194. Next, Matt's
  choice among the SPU candidates of the Log's last entry (recommended:
  1, the checkpoint's copy by SPU DMA; then 2, the weather).
- 2026-10-22: the 01:54 PKG on hardware (b40): the game thread crashed at
  about t=33 s in `render_player_frame` (step 523); lv2 ended it (see the
  Log). Then (Matt: "Do 1"): finer `PS3_MAIN_STEP` marks in
  `render_player_frame` and `render_window`. Built, not tested on the PS3.
  PKG of 10:57 in `build/ps3_spu/` (the 01:54 tree and this). Run on
  hardware: the same crash at step 627
  (`rasterizer_transparent_geometry_draw(TRUE)`: the transparent sort's
  models' order, the 01:54 tree's step 2). See the Log. Then (Matt: "Go
  with option 2."; the gain at best about 0.25 ms a frame in heavy
  scenes): the kept boxes taken out, the models' order exact as in the
  00:37 build. Built, not tested on the PS3. PKG of 11:16 in
  `build/ps3_spu/`. Run on hardware (Matt: "Done"; no fault reported):
  `[ps3] exit 0`, no crash; the weather's update in the snow 0.86 ms a
  frame (00:37: 1.27). See the Log for the medians and the next steps.
  Then (Matt: "Do all 4."): the stream buffers from blocks of their own;
  the ground planes' sphere query on the kept blocks (the same answer: PC
  check); `render_model_parts` and the objects' shadows timed inside.
  Built, not tested on the PS3. PKG of 11:54 in `build/ps3_spu/`. Run on
  hardware (Matt: "Done."; no fault reported): `[ps3] exit 0`; the heap
  no longer fragments (22.3 MB free at the end, before 19.3); the sphere
  query 10.3 us a call (before 13.3). With it (Matt: "check what we can
  place to the SPU"): the SPU candidates in the Log.
- Handoff (2026-10-20, end of the session; Matt: "Can you save this
  somewhere? I'll work on it next time."): G8 is in progress. The last
  PKG is 01:54 in `build/ps3_spu/` (the 5 steps of the entry below;
  built, not run on the PS3). Not committed since `0966e23c` ("Further
  optimization"): the shorter pixel shader translation, the sound
  streams' buffers kept (a pool of 64), the translator's fixed buffers,
  the weather's rays without surfaces moved without the collision call,
  the transparent model order from kept boxes, and the timers of both
  rounds (19 files in `git status`). No listener runs on UDP 18194.
  Next: Matt installs the 01:54 PKG; at his "Start the listener." check
  who holds UDP 18194, then log the run. Report the medians (all / under
  45) of the new lines (`[time] ground planes' features in the sphere`,
  `[time] render_model but the shadows`, `[time] lights and diffuse
  passes`, `[time] specular part's Direct3D draws and constants`), the
  weather's update and the transparent sort's model order against the
  00:37 run (the Log), the heap's 64 KB stream allocations; then the
  numbered next steps.
- 2026-10-19: G8, the SPE tasks (Matt: "Let's work on the SPEs tasks."):
  the first in the proposed order ("SPEs and VMX"): the SPU set-up and the
  DirectSound mixer's decode and mix on an SPU. PKG of 12:52 in
  `build/ps3_spu/` (the tree of `8f833512` and this change only). Run on
  hardware (Matt: "Done."), a30: the SPU mixed the whole run, 0 silent or
  partial port blocks, the SPU's events worked, `[ps3] exit 0`; the mixer
  thread's own PPE time 107 us a chunk (before 469), the SPU 183 us a
  chunk. Hardware SUCCESS (Matt: "No audible faults."). See the Log.
  The second SPE task (Matt: "Go with the second SPE's task!"): the
  Direct3D thread's vertex conversions (at draws and into the vertex
  cache) and index copies as tasks of a second SPU thread. Built, not
  tested on the PS3. PKG of 13:47 in `build/ps3_spu/` (the first SPE task
  and this). Run on hardware (Matt: "Done."): no `SPU off`, a clean exit;
  the Direct3D thread's vertices 3.3 to 0.5 ms a frame, but it waits 1.1
  ms a frame for the SPU (about once a task). Hardware SUCCESS (Matt:
  "Yes, it looked correctly."). See the Log. Then (Matt: "Let's go with
  option 3."): the ring's room given back without a wait (the records
  with SPU tasks in a list), and the SPU's fetches ahead of its work, its
  outputs out while the next task runs. Built, not tested on the PS3. PKG
  of 14:06 in `build/ps3_spu/`. Run on hardware (Matt: "Done"): a clean
  exit, but the Direct3D thread still waits for the SPU about once a task
  (0.9 ms a frame): `ps3_gpu_commands_check` fires at every record (the
  context's end is a segment's, not the buffer's). Hardware SUCCESS
  (Matt: "Yes, the picture looked correct."). See the Log. Then (Matt:
  "Go with option 1."): the SPU wait only before a segment's flush: our
  own context callback waits for the SPU, then calls the system's;
  `ps3_gpu_commands_check` removed. Built, not tested on the PS3. PKG of
  14:29 in `build/ps3_spu/`. Run on hardware (Matt: "Done."): a clean
  exit; the segment is 28 KB (the cause confirmed); the waits 0.6 to 2 a
  frame (before about one a task), but each of 340 to 740 us: the SPU
  waits 0.60 / 0.70 ms a frame (before 0.90 / 0.95). Hardware SUCCESS
  (Matt: "The picture looked correct."). See the Log. Then
  (Matt: "Do 2 and 3."): the conversions' SPU faster (one fetch a
  stream, four vertices at a time, the divisions in integers; its DMA
  waits and the elements by type logged), and exact VMX in the rays'
  objects part (each cluster's objects first against the copies of
  their bounding spheres, four at once). Built, not tested on the PS3.
  PKG of 15:26 in `build/ps3_spu/`. Run on hardware (Matt: "Done."): a
  clean exit, `[vmx] exact`; the SPU 22 to 26 cycles an element (before
  about 89), the Direct3D thread's SPU waits 0.10 / 0.10 ms a frame
  (before 0.60 / 0.70); the rays' own object tests 1.8 / 2.2 a ray
  (before 65 / 93), but each ray only 5 to 18% faster: the loop's mark
  reads each object before the copies' answer. Hardware SUCCESS (Matt:
  "Yup", to "Was the picture correct this run?"). See the Log. Then
  (Matt: "1-2"): the copies' answer before the mark, and exact VMX in
  `matrix4x3_multiply` (every node's matrix, and its other callers).
  Built, not tested on the PS3. PKG of 15:50 in `build/ps3_spu/`. Run on
  hardware (Matt: "Done."): a clean exit, `[vmx] exact`; a ray's
  objects part 29.8 us (before 37.2), node matrices 1.00 ms a tick
  (before 1.08). Hardware SUCCESS (Matt: "Picture was correct"). See the
  Log. Then (Matt: "Go with 1-2."): each cluster's list walked once (an
  iterator gives the objects the copies do not rule out), and timers
  inside the node matrices. Built, not tested on the PS3. PKG of 16:22 in
  `build/ps3_spu/`. Run on hardware (Matt: "Done"): a clean exit; a ray's
  objects part 23.3 us (before 29.8); node matrices: the matrices 0.45
  ms a tick (2.0 us a call), the orientations 0.21, the overlays and
  blends 0.18, the postprocess 0.06; 231 calls and 569 nodes a tick.
  Hardware SUCCESS (Matt: "Yeah, it was"). See the Log. Then (Matt: "All
  3."): `valid_real` without a store read back (PS3), the matrices step
  timed inside, and the particles timed inside (the next target: 1.11 /
  1.96 ms a frame, above the AI's 1.21 / 1.72 ms a tick). Built, not
  tested on the PS3. PKG of 16:58 in `build/ps3_spu/`. Run on hardware
  (Matt: "Done."): a clean exit; the matrices step 0.50 ms a tick (first
  node 0.18, checks 0.15, other nodes 0.15, bounds 0.05), no gain seen
  from `valid_real`; particles_update 0.80 / 1.72 ms a frame, 85% its
  particles' physics (about 4 us a particle). Hardware SUCCESS (Matt:
  "Yes, the picture was still correct. I will tell you if the picture is
  wrong from now on."). See the Log. Then (Matt: "All 3."): the
  particles' point physics timed inside (its rays test the structure and
  water only), the AI timed inside, and why the validity checks cost
  (each test's branch waits for its whole float chain on the in-order
  PPE). Built, not tested on the PS3. PKG of 17:31 in `build/ps3_spu/`.
  Run on hardware (Matt: "Done."; no fault reported): a clean exit; the
  AI 2.21 ms a tick for 7 actors (313 us an actor: perception 128,
  control 42, movement 41, decisions 35); point physics 3.5 us a call
  (wind 0.9, the ray 2.2). See the Log. Then (Matt: "Do all 4 then."):
  the AI's perception, control and movement timed inside, why the wind
  lookup costs 0.9 us (each particle finds its cluster's palette and
  definitions again; 10 store-and-read stalls a call), and the validity
  checks' terms first, then the tests without a branch each (the same
  answer). Built, not tested on the PS3. PKG of 18:05 in
  `build/ps3_spu/`. Run on hardware (Matt: "Done."; no fault reported):
  a clean exit; the validity checks 0.15 to 0.09 ms a tick (0.67 to 0.39
  us a call); perception is the props' refreshes (77 us each, 86% of it
  the AI's line-of-sight rays: 57 us a ray), control is action control
  (43 us an actor), movement is the move (29 us an actor). See the Log.
  Then (Matt: "Do all 4 next steps."): the AI's line-of-sight rays timed
  inside, action control by action, the move's parts and the path
  searches (`actor_path_refresh` runs in action control and destination,
  not in the move), and the wind's lookups kept for the tick, its keys
  without the stalls (the same answer: PC check, 29,101,907 calls, 0
  differ). Built, not tested on the PS3. PKG of 18:58 in
  `build/ps3_spu/`. Run on hardware (Matt: "Done"; no fault reported): a
  clean exit; the wind 0.88 to 0.60 us a call; action control is the
  guard action's path builds (`actor_path_refresh`: 2 to 3.6 ms each, one
  every 2 or 3 ticks in combat); the move is the flying vehicles' pilots'
  vector avoidance (334 us a call: 25 rays, each up to 2 structure bsp
  tests);
  a line-of-sight ray 59 us (its objects part 38). See the Log. Then
  (Matt: "Do 'em"): a path build timed inside (setup, search with its
  nodes, path made), the vector avoidance timed inside (setup, the rays'
  structure tests, their object tests), and the line-of-sight rays' loop
  over a cluster's objects (the copies' iterator against the tests; the
  objects walked, passed, tested). Built, not tested on the PS3. PKG of
  19:38 in `build/ps3_spu/`. Run on hardware (Matt: "Done."; no fault
  reported): a clean exit; a path search 1.1 ms and the path made from
  it 1.5 ms (each up to about 2.5); the vector avoidance 310 us a call
  (its 50 structure tests 150); a line-of-sight ray's objects loop 44.5
  us (the iterator 19.5: 122 objects walked at 158 ns each). The
  heaviest combat window: 38.8 frames per second, 19.0 ms a tick, 0.77
  ticks a frame (vehicles 3.1 ms a tick, AI 4.6, decisions 1.7 of it).
  See the Log. Before it (Matt: "are we close at getting 60 stable
  FPS?"): not yet; close in light play. Then (Matt: "Do it along all
  the steps you mentioned, so we concretize the amount of things we
  test."): the checkpoint's state copied into main memory and written
  by a thread of its own (a revert from the copy); each cluster's
  objects and spheres kept in a block for the rays' iterator (the same
  answer: PC check); and timers inside the path search, the path made,
  the vehicles and the AI's decisions. Built, not tested on the PS3.
  PKG of 21:17 in `build/ps3_spu/`. Run on hardware (Matt: "Done. The
  fix for the stutter worked, though"): a clean exit; a checkpoint holds
  the game thread 29 to 30 ms (the copy), the writer's thread writes it
  in 1.6 to 1.8 s; a revert took the copy. Main memory: 45.6 MB free
  before the first checkpoint, 23.6 after, the lowest 18.6. The rays'
  iterator 9.3 us a ray (before 19.5), the lists all but unwalked. A
  path made is its smoothing (1.27 ms); a search copies 225 us into the
  debug storage; a simulated vehicle's physics 400 us. 36 windows, none
  under 45 frames per second. See the Log. Then (Matt: "Yeah, do all
  those. We're doing great!"): timers inside the path smoothing and the
  vehicles' physics; the search's debug copy only while a debug view
  draws it; the surfaces' edges kept for the searches (the same answer:
  PC check); the checkpoint's copy of the state's used parts only (the
  rest zeroes, checked). Built, not tested on the PS3. PKG of 22:08 in
  `build/ps3_spu/`. Run on hardware (b40, Matt: "Got a red screen."): the
  first checkpoint halted the game: the checkpoint's `free` of the copy
  not yet made (the game's `free` halts on NULL, `debug_memory.c`). Fixed
  (`free` only of a copy made). PKG of 22:24 in `build/ps3_spu/`. See the
  Log. Run on hardware (b40; Matt: "Run's done!"; no fault reported): a
  clean exit; three checkpoints, each 38 to 40 ms on the game thread
  (17,942 KB copied), written by the writer in 1.5 to 2.0 s; a revert
  took the copy. Main memory 42.3 MB free before the first checkpoint,
  25.3 after it, the lowest 18.3 (the heap grows in play, its use not).
  35 windows, 59.9 frames per second on median, two under 45 (the load's
  32.4, the last 37.5). The debug copy gone; the kept edges no faster (a
  kept surface 1.1 us, a built one 0.9); the RSX's specular pass 4.8 ms a
  frame on median (a30: 0.7), up to 9.0. See the Log. Then (Matt: "Go for
  it! Also, maybe we can delegate the specular pass to one SPU."): the
  kept edges taken out; the checkpoint's copy and check with VMX streams
  (the same bytes: PC check); the specular part split in seven on the
  game thread and the RSX, its draws timed on the RSX by pixel shader in
  one frame of eight; the weather, the transparent geometry and the old
  vehicle physics timed inside; the heap's growths and large allocations
  logged. The SPU: not for the RSX's time (see the Log). Built, not
  tested on the PS3. PKG of 23:33 in `build/ps3_spu/`. Run on hardware
  (b40; Matt: "Done."; no fault reported): a clean exit; a checkpoint
  holds the game thread 10.4 to 11.9 ms (before 38 to 40); a revert took
  the VMX copy and play went on. The specular part's RSX time is its
  pixel shaders: 96 instructions for 6 combiners (lights), 66 for 5
  (reflections), 36 for 2 (fog). The heap grows with the sound streams'
  64 KB buffers (10 to 25 a second) and the shader translator's
  temporary buffers: 16.5 MB free at t=200 s. See the Log. Then (Matt: "Go
  with steps 1-6."): the pixel shader translation shorter (b40's
  specular lights 384 to 232 words, the reflections 240 to 188, the fog
  144 to 116; the same results as before: PC check); the sound streams'
  packet buffers kept and given back; the translator's buffers fixed; the
  weather's point physics, the ground planes and the transparent sort
  timed inside. Built, not tested on the PS3. PKG of 00:37 in
  `build/ps3_spu/`. (Matt committed the state before as `0966e23c`
  "Further optimization".) Run on hardware (b40; the flycastPS3
  session's listener held UDP 18194 and logged it; Matt: "Check that
  log."; no fault reported): a clean exit; 59.0 frames per second on
  median (5 windows under 45); the RSX's specular part 4.4 ms a frame on
  median (5.6 before); the heap's growth stops at t=100 s (19.3 MB free
  to the end). See the Log.
  Then (Matt: "Ok, do those 5 steps."): the weather's rays that test
  no surface moved without the collision call (the same positions: PC
  check); the transparent sort's model order from boxes kept for each
  part's nodes, the exact bounds only where those cannot tell a side
  (the same order: PC check); the ground planes' sphere query timed
  inside; the sound streams' pool 16 to 64 buffers; `render_model`, the
  lights pass and the diffuse pass timed inside, with each part's
  Direct3D calls. Built, not tested on the PS3. PKG of 01:54 in
  `build/ps3_spu/`. See the Log.
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
    `sound_manager.c` 510-519, `render.c` 520-530 (`render_frame`),
    580-585 (`render_player_frame`), 600-637 (`render_window`, the primary
    target's; 700-737 the mirror's), `player_effects.c` 570-576.
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
  - The SPE tasks (2026-10-19): `[spu] mixer up` and `DirectSound: the
    streams decode and mix on an SPU` at the start; the SPU's part of the
    `DirectSound:` line (its time a chunk and its decoding, the mixer
    thread's wait, how the waits ended, the events lv2 refused); `[spu]
    mixer: ... SPU off` with the SPU's own echo when a job is not done in
    100 ms; `[spu] mixer down` at the exit. The second: `Direct3D: vertex
    and index conversions on an SPU` and `[spu] vertices up` at the start;
    every 600 frames `Direct3D: frame N: the conversions' SPU on: ...`
    (tasks, elements, indices, its time a frame, the waits that found
    tasks not done and their time a frame); `SPU` among the Direct3D
    thread's waits in `[time]`; `[spu] vertices: ... SPU off` when a task
    is not done in 100 ms; `[spu] vertices down` at the exit.
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
| G8 60 frames per second and 720p: the frames between the 30 Hz ticks, each frame in 16.7 ms on the game thread and the RSX; the game drawn at 720p (Matt, 2026-10-18) | Analysis done 2026-10-18; the fixes run on hardware 2026-10-19 (menus 60, a30 heavy combat 33 to 37); step 5 (the Direct3D thread) runs on hardware 2026-10-19 (a30 heavy combat 38 to 47); the measurements after it done 2026-10-19; Matt chose its three next steps (collision time by user, the sound's decode off the game thread, the screen quads' timers): run on hardware 2026-10-19 (`sound_render` 1.10 / 2.15 to 0.30 / 0.70 ms a frame; the collision tests 3.63 / 5.89 ms a frame); the next steps 1 to 3 (the obstruction kept up to 4 ticks, the rays and the HUD timed inside) run on hardware 2026-10-19 (the sounds' rays 1.60 / 1.94 to 0.58 / 0.93 ms a frame; the rays' objects part 1.07 / 2.91); the SPEs and VMX candidates written down (see "G8 analysis", "SPEs and VMX"); the first SPE task (the SPU set-up, the DirectSound mixer's decode and mix on an SPU) run on hardware 2026-10-19 (the mixer thread's PPE time 469 to 107 us a chunk; SUCCESS); the second (the Direct3D thread's vertex conversions and index copies on an SPU) run on hardware 2026-10-19 (its vertices 3.3 to 0.5 ms a frame; SUCCESS), then its waits for the SPU at a command buffer segment's flush only (run on hardware: 0.6 to 2 waits a frame, before one a task); the conversions faster, exact VMX (the rays' objects part, `matrix4x3_multiply`) and each cluster's object list walked once run on hardware 2026-10-19 (SPU 22 to 26 cycles an element; a ray's objects part 37.2 to 23.3 us; SUCCESS); the node matrices, particles, point physics and AI timed inside, run on hardware 2026-10-19; the AI's perception, control and movement timed inside, and the validity checks without a branch each, run on hardware 2026-10-19 (the checks 0.15 to 0.09 ms a tick; the AI's line-of-sight rays 57 us each); the line of sight, action control and the move timed inside, and the wind's lookups kept for the tick, run on hardware 2026-10-19 (the wind 0.88 to 0.60 us a call; a guard's path build 1.8 to 3.6 ms; a flying vehicle pilot's vector avoidance 334 us); the path builds, the vector avoidance and the line-of-sight objects loop timed inside, run on hardware 2026-10-19 (a path search 1.1 ms and its path made 1.5; the heaviest combat 19.0 ms a tick); the checkpoint written by a thread of its own, the clusters' kept blocks, and timers inside the path search, the vehicles and the decisions, run on hardware 2026-10-19 (the checkpoint stutter gone: 30 ms on the game thread; the rays' iterator 19.5 to 9.3 us a ray; SUCCESS); the smoothing and physics timed inside, the search's debug copy skipped, the surfaces' edges kept, the checkpoint's used parts only, run on hardware 2026-10-19 on b40 after a fix (the first checkpoint's `free` of NULL) (59.9 frames per second on median; the kept edges no faster; the RSX's specular pass up to 9.0 ms a frame; SUCCESS); the kept edges out, the checkpoint's VMX copy, the specular part by split and by pixel shader, the weather, transparent geometry and old physics timed, the heap log, run on hardware 2026-10-19 (the checkpoint 38-40 to 10.4-11.9 ms; the specular part's shaders 36 to 96 RSX instructions; the heap's growth from the sound streams' buffers; SUCCESS); the shorter pixel shader translation, the sound streams' buffers kept, the translator's buffers fixed, the weather's point physics, ground planes and transparent sort timed, run on hardware 2026-10-20 (the RSX's specular part 5.6 to 4.4 ms a frame on median; the heap's growth stops; SUCCESS); the weather's rays without surfaces moved without the collision call and the transparent model order from kept boxes (the same results: PC checks), the ground planes' query, `render_model` and the lights and diffuse passes timed inside, the streams' pool of 64: built 2026-10-20; on hardware 2026-10-22 the kept boxes crashed the game thread twice (step 627, the transparent sort; cause not found): taken out (Matt: option 2), then a clean run (the weather in the snow 1.27 to 0.86 ms a frame); the stream buffers' blocks (no heap fragments: 22.3 MB free at the end, before 19.3), the sphere query on the kept blocks (13.3 to 10.3 us a call; the same answer: PC check), `render_model_parts` and the shadows timed inside: run on hardware 2026-10-22 (SUCCESS); the SPU candidates written down (the Log); the game thread's jobs on a third SPU (the checkpoint's copies, the shadows' surfaces, the weather, the particles' rays; PC check on the 23 maps): built 2026-10-22, run on hardware 2026-10-22 (the checkpoint 12.9-15.1 to 3.1 ms; the shadows' surfaces 103 to 4.7 us a shadow; the weather 1.2 to 0.05 ms a frame; the particles 3.8 to 2.9 us each); the pixel shaders' copies on the conversions' SPU and the particles' rays taken a frame late: built 2026-10-22 |
| G9 The game thread's render on the SPEs (Matt, 2026-10-22) | Analysis done 2026-10-22, steps proposed (see "G9 analysis"); steps 2 to 5 (the render SPU: the lights' surfaces, the window's visible surfaces, the particles' sprites, the contrails' strips): run on hardware 2026-10-22 (a clean exit; render 9.0 to 7.8 ms a frame on median; the SPU waits 0); file reads in 128 KB calls (the BSP switch's stutter), the decals' batch in one call, the shadows' sort and triangles on the render SPU: built 2026-10-22 |

## G9 analysis (2026-10-22)

Matt: "Work on Goal 9." The game thread's render (14.65 ms a frame in the
windows under 50 fps) read part by part for work that an SPU can take:
render-only, a batch a frame or a window, compact data by DMA. Steps of
`render_window` as in the Log (`PS3_WINDOW_STEP`).

SPU candidates, the largest expected first:

1. The dynamic lights' surfaces (`object_lights.c` `lights_render_diffuse`
   and `lights_render_specular`; `structure_render.c`
   `structure_render_diffuse_light` and `_specular_light`). Each dynamic
   light searches the BSP for its surfaces twice a frame (the diffuse
   sphere, then the specular one: `structure_visibility_build_surfaces`),
   and twice sorts and gathers their triangles
   (`structure_render_build_dynamic_triangles`). It is the shadows' search
   (the PPE 103 us a search and 24 us the triangles; with the SPU 4.7 to
   8.3 us on the game thread). The spheres and cluster lists are final
   after `lights_preprocess_scene` (step 6); the passes use them at steps
   12 and 16. The SHADOW job serves the search as it is (no planes, the
   sphere's box, the light's clusters; the walk from the root under radius
   2.0). Not timed: the lights' searches have no timer, and the dynamic
   lights a frame are not logged.
2. The window's visible surfaces (`structure_visibility.c`
   `traverse_subclusters`, `structure_render.c`
   `structure_render_preprocess`): the subclusters' frustum tests and the
   surface flags, then the ascending surface list and its triangles (up to
   32,768 surfaces), and each material's chunk (first triangle, count).
   The portal walk stays on the PPE (`render_objects` and the lights need
   its clusters at once). On the PPE nothing reads the flags before step 8;
   the shadows' jobs read them on the same SPU, after. Submitted at the end
   of `structure_visibility_compute`, taken at step 8: it overlaps the sky,
   first person, lights and `render_objects`. With the chunks, the 10
   passes on the window's list (`structure_render_pass`: lightmaps,
   diffuse, specular lightmaps, masks, mirrors, reflections, transparent,
   fog, the fog screen twice) stop walking every visible surface each. Its
   time is inside the visibility split (0.4 ms with the portals and
   `rasterizer_window_begin`) and the lightmaps split (1.05 with the
   lightmaps' draws): not split yet.
3. The effects' particles' sprites (`render_particles.c`): the gather,
   sort and groups, the attached particles, the first person groups and
   the write-backs (`last_rendered_frame_index`, `particle_delete`, the
   shader's radius) stay on the PPE; each group's sprites built as the
   weather's job builds them (`weather_sprite`, `spu_game.c`, with the
   size, distance and fade cuts and the u/v mirrors added) into vertex
   buffers locked first. Earlier runs: the particles' render 0.3 to 1.8 ms
   a frame (with `particle_systems_render`).
4. The contrails' strips (`render_contrails.c` `render_contrail`): two
   vertices a point (colour, width, fade), no write-back. Up to 0.7 ms a
   frame. Every contrail is built each frame (no visibility test).
5. The render's node matrices (`render_interpolation.c`, `models.c`
   1004-1022, `rasterizer_set_model_skinning`): the blend of the two ticks,
   the matrices relative to the model's and the skinning constants for all
   the window's objects at once; the shadows' pass uses them again. 3.5 us
   a model for the relative matrices: about 0.05 to 0.3 ms a frame.

Not for an SPU (state set-up a draw, the texture cache, tag reads): a model
part's draw (21.7 us, its Direct3D 5.8: 4 texture cache lookups, about 50
small calls, periodic functions, the fog's colours again each part), the
structure passes' draws, the transparent groups' draws, the decals, the
detail objects (built only when the camera enters a new cell), the HUD.

Found on the game thread, not SPU work:

- The lens flares' occlusion tests draw their quads, but
  `GetVisibilityTestResult` gives 0 on the PS3, so no flare is drawn (0.06
  ms a frame and the RSX's quads for nothing).
- `particle_systems_render` makes a vertex buffer and a transparent group
  (a full group draw later) for each particle of a particle system.
- `draw_prologue` compares all render and texture stage states (144 + 4 x
  32 words) at every draw; a dynamic index list is copied into the ring at
  every draw (the window's list once a pass).
- 3 of the 6 SPUs are free. The game's jobs share one SPU and one ring (the
  shadows' jobs already wait behind the particles').

Proposed steps (Matt's choice):

1. Timers, one run: the lights' searches and triangles (diffuse, specular,
   the dynamic lights a frame), the visibility split's parts (portals,
   subclusters, `rasterizer_window_begin`), `structure_render_preprocess`
   alone, the passes' walks, `render_particles`' sprites,
   `particle_systems_render`, the contrails' build, the render's node
   matrices.
2. The lights' surfaces and triangles on the SPU (candidate 1).
3. The window's visible surfaces, triangles and chunks on the SPU
   (candidate 2).
4. The particles' and the contrails' sprites on the SPU (candidates 3, 4).
5. A second game SPU for the render's jobs (a ring of its own).

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
50. In the game's sources `free` is `match_free` (`cseries.h`): it halts the
    game on NULL ("Attempted an operation with pointer at 0x0",
    `debug_memory.c`), where the C library's `free` does nothing. Free only
    what was allocated.

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
- 2026-10-19: The first SPE task (Matt: "Let's work on the SPEs tasks."):
  the SPU set-up and the DirectSound mixer on an SPU. Files:
  - `port/ps3/spu/spu_mixer.c`, `.h` (new, `spu-gcc`): an SPU thread that
    sleeps on a reservation of the bell's 128-byte line (flycastPS3's
    model). A job is one 256-frame chunk: the voices in 32-voice DMA
    batches, each packet not decoded yet decoded (16 KB batches from the
    sound cache, floats put into its buffer), then `mix_run` on windows of
    the floats (at most 8 KB a DMA), the same float operations in the same
    order as `dsound_ps3.c`. Then the chunk, the voices' positions and
    flags, the echo (fenced behind them) and `spu_thread_send_event` (port
    16). 73 KB of the 256 KB local store.
  - `port/ps3/src/ps3_spu.c`, `.h` (new): `sysSpuInitialize(6, 0)`, the
    thread group (priority 100), an event queue connected to the SPU
    thread's port 16. A job: the header, a drain of the queue, `lwsync`,
    the bell's store; the mixer thread then sleeps on the queue (1 ms
    timeouts that look at the echo). A job not done in 100 ms: the SPU's
    own echo read from its LS, the group terminated, `SPU off`, the PPE
    mixes from then on. `ps3_spu_stop` at both exits (`ps3_main.c`).
  - `dsound_ps3.c`: a pass computes each taken stream's step and gains
    (`mix_begin`, kept in the stream), then `spu_mix` fills the job, runs
    it, copies the chunk and gives each stream the SPU's position and
    packet flags; `mix_stream` (the PPE) only for the streams past 256 or
    with the SPU off. `Process` allocates the floats 128-aligned with the
    size rounded up to 128 bytes (`ps3_spu_buffer_allocate`; `free()` frees
    them). The `DirectSound:` line ends with the SPU's part.
  - `Makefile`: the SPU rules (`spu-gcc -O2 -ffp-contract=off`, `bin2s`
    into `spu_mixer_elf[]`), `-I port/ps3/spu`; the bring-up leaves out
    `ps3_spu.o`. `port/ps3/README.md`: the `spu/` folder, the files, the
    log lines.
  - The PC check (Docker, gcc 13; the session's scratchpad): flycastPS3's
    SPU simulator (`misc/spu_sim/spusim.c`, a copy with lv2's side of
    `spu_thread_send_event`: channel 30 is an event, channel 29 its answer)
    runs the real SPU image (its `.text` the same as the PKG's).
    `dsound_ps3.c` itself, with stand-ins of the XDK types and the host
    layer, plays the game's part: 8 to 64 streams of 6 formats (ADPCM mono
    and stereo at 22 and 44 kHz, PCM), 2 to 12 packets, refills from the
    callback, odd packet sizes, flushes, stream releases, frequencies from
    1 kHz to 30 times the rate, 3D, I3DL2, mix bins, a looping buffer. Two
    runs of a seed, the SPU's and the PPE's, both in the SPU's arithmetic
    (round toward zero, no denormals): their chunks and packet completions
    equal byte for byte. 5 seeds, 38000 chunks, about 49000 packets, 6600
    flushes; no DMA outside its block (each block its own mapping). The
    simulator's SPU time a chunk: 8 streams 110 us, 24 streams 308 to 381,
    40 streams 587, 64 streams 920 (max 3.4 ms); the decode about half. The
    first version took 433 us (24 streams): `adpcm_expand` as a call and
    the clamps as branches (an SPU branch without a hint costs 18 cycles);
    now inline with selects.
  - `check`: 0 `r2off`, `.got` 0x81e8. PKG of 12:52 in `build/ps3_spu/`
    (a copy of `build/ps3_g8`, whose objects are the commit's). Not tested
    on hardware.
- 2026-10-19: PKG of 12:52 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, `[ps3] exit 0` and `[spu] mixer down`, no halt.
  `[spu] mixer up` and `DirectSound: the streams decode and mix on an
  SPU`; the SPU mixed to the exit (no `SPU off`). Every `DirectSound:` line
  0 silent and 0 partial port blocks. The 10 `DirectSound:` lines of a30's
  play (4 to 22 streams, median 12; 672 packets in 10 s), medians (range):
  the mixer thread 329 us a chunk (159 to 578; max 1.5 to 2.4 ms), of it
  its wait for the SPU 223 (132 to 430), so its own PPE time 107 (20 to
  148; the lock held 50 of it, as before) against 469 in the run of 11:40;
  the SPU 183 us a chunk (95 to 389), decoding 94 (43 to 162). The waits:
  about 1875 a 10 s line ended by the SPU's event (one a job), 4 to 111 by
  the 1 ms timeout (jobs over 1 ms), 0 errors, 0 events refused. 17 timer
  windows of play, 3 under 45 frames per second (one of them the load);
  medians all / under 45: the frame 19.80 / 26.40 ms (50.6 / 37.9 frames
  per second; 0.54 / 0.54 ticks a frame), game ticks 5.40 / 4.20, render
  10.90 / 10.60; the RSX 10.5 / 10.9; the Direct3D thread busy 13.0 / 13.8
  (vertices 3.3, state 3.2, colours 0.7, indices 0.4 / 0.2). The play
  differs from the runs before: the frames are not comparable. Hardware
  SUCCESS (Matt: "No audible faults.").
- 2026-10-19: The second SPE task (Matt: "Go with the second SPE's
  task!"): the Direct3D thread's conversions on an SPU. The log of 12:52:
  12 to 25 million vertex elements converted at draws a 600 frames (20 to
  40 thousand a frame: the dynamic vertices), the vertex cache's 10 to 20
  thousand after the first frames; "colours" (the pixel shader's constants
  and its copy into the scratch, about 1 us a draw) stays on the PPE.
  Files:
  - `port/ps3/spu/spu_vertices.c`, `.h` (new): an SPU thread on a ring of
    1024 tasks of 256 bytes (the bell's reservation as the mixer's). A
    CONVERT task: up to 15 elements of a draw (source, stride, type, size),
    each pass of up to 16 KB of float4s put while the next converts; each
    element's bytes fetched as the 16-byte lines that hold them (a line
    never crosses a page: no read past the last element's line). Every
    Xbox type as `vertex_element_convert`: floats copied, bytes through
    d3d8_rsx.c's `b / 255` table (DMA at the start), shorts by `csflt`,
    the divisions of NORMSHORT and NORMPACKED3 as the double product by the
    reciprocal rounded once (a whole number over an odd divisor is never
    within 2^-39 of a rounding point: the float division's result). An
    INDICES task: 16-bit indices less a base, 8 a quadword. The echo after
    each task, behind its output. 60 KB of the local store.
  - `port/ps3/src/ps3_spu.c`, `.h`: `ps3_spu_vertices_start` (in
    `Direct3D_CreateDevice`, after the table), `_task` (a ring slot, after
    the SPU's room), `_commit` (`lwsync`, the bell), `_wait` (until the
    echo's tail: a spin of about 60 us with `db16cyc`, then 20 us sleeps;
    100 ms: the SPU's own echo logged, the group terminated, the tasks left
    run on the PPE by d3d8_rsx.c's fallback, the PPE converts from then
    on). `ps3_spu_stop` stops both groups; the SPU finishes its tasks
    before a quit.
  - `port/ps3/src/ps3_gpu.c`, `.h`: the wait before every flush
    (`ps3_gpu_wait_idle`, `finish`, the present's `ps3_video_end_frame`),
    and `ps3_gpu_commands_check`: less than 64 KB left in the command
    buffer (its wrap flushes) waits first; `record_execute` calls it
    before each record (one record's commands are under 20 KB).
  - `port/ps3/xbox/d3d8_rsx.c`: `vertices_convert` and `indices_convert`
    queue a task (else convert as before) in `setup_vertices`,
    `vertex_cache_get`, `index_cache_offset`, `index_copy` (its index OR
    first, then the copy; the scratch rounded up to 16 bytes) and
    `draw_indexed_run`'s rebase. The Direct3D thread gives the ring's room
    back after the SPU's tasks (they read the records' vertices): when it
    goes idle, or at each record while the game's thread waits; an inline
    record waits for them (its memory is the next one's). `byte_floats`
    128-aligned. `[time]`: `_ps3_timer_spu_wait`.
  - `Makefile`: `spu_vertices` among the SPU programs.
    `port/ps3/README.md`: the files, the log lines.
  - The PC check (the session's scratchpad; flycastPS3's simulator as for
    the mixer): the real image (its `.text` the same as the build's) runs
    random tasks of all 19 types and 2 unknown ones, 1 to 15 elements,
    strides 0 to 20000, any alignment, floats with NaN, infinity and
    denormals, shorts at their ends, the ring wrapping, a quit; against
    `vertex_element_convert` extracted from `d3d8_rsx.c` (its aligned fast
    path removed: the x86 order) and the index rebase. 11 seeds, about
    9300 tasks, 17.7 million elements and 4.7 million indices: equal byte
    for byte (the indices' padding aside), each source block unchanged, no
    DMA outside a block (each block its own mapping, its last line only).
    The simulator's SPU time: about 70 cycles an element or index over all
    types, 31 with FLOAT3, FLOAT2 and D3DCOLOR only (the dynamic vertices:
    20 to 40 thousand elements a frame are about 0.2 to 0.4 ms of the SPU,
    against 3.3 ms of the PPE).
  - flycastPS3's hardware runs: SPU puts into RSX local memory work (3.3
    GB/s); its `spe_vb` orders them as here (the puts' tags, then the echo).
  - `check`: 0 `r2off`, `.got` 0x8300. PKG of 13:47 in `build/ps3_spu/`.
    Not tested on hardware.
- 2026-10-19: PKG of 13:47 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[ps3] exit 0`,
  `[spu] mixer down` and `[spu] vertices down`. The conversions' SPU every
  600 frames of play: 21 to 50 thousand tasks, 12 to 29 million elements
  and 4 to 23 million indices (about 45 thousand elements a frame); its
  time 0.6 to 1.6 ms a frame (about 100 cycles an element on hardware, 31
  in the simulator: the fetches wait for their DMA). The waits that found
  tasks not done: about one a task (19 to 59 thousand a 600 frames), 0.6
  to 1.6 ms a frame: while the game's thread waits for the Direct3D thread
  (its present's wait for the frame before), the Direct3D thread waits for
  the SPU after every record. 27 timer windows of play, 4 under 45 frames
  per second (one the load); medians all / under 45: the frame 17.00 /
  27.50 ms (58.7 / 36.9 frames per second), game ticks 6.30 / 8.50, render
  9.60 / 12.20; the RSX 8.4 / 12.1; the Direct3D thread: vertices 0.50 /
  0.55 ms a frame (before 3.3), indices 0.20 / 0.25 (before 0.4 / 0.2),
  the SPU waits 1.10 / 1.05, state 2.70 / 3.15, colours 0.50 / 0.65; the
  game thread's waits for it 1.00 / 0.65. The play differs from the run
  before: the frames are not comparable. Hardware SUCCESS (Matt: "Yes, it
  looked correctly.").
- 2026-10-19: Option 3 of the next steps (Matt: "Let's go with option
  3."): both.
  - The ring's room without a wait (`d3d8_rsx.c`): the records that
    committed SPU tasks go in a list of 1024 (their start, the task head
    after them; a full list waits for the SPU). After each record the room
    comes back up to the first record whose tasks are not done
    (`ps3_spu_vertices_done`, a test of the echo's tail, no wait): at once
    while nothing is pending, and while the game's thread waits for room or
    a position; when the thread goes idle, all of it after a wait. The
    waits left: the present's flush, a wrap of the command buffer, an idle
    thread, a full list or ring. `ps3_spu.c`: `ps3_spu_vertices_head` and
    `_done`.
  - The SPU (`spu_vertices.c`): two input buffers, each slot's (or index
    batch's) fetch started while the one before converts. A task's outputs
    are put while the next task runs: each output buffer's puts and an echo
    copy on its own tag; the echo fenced (`mfc_putf`) behind the task's last
    buffer, after a wait for its other buffer and the echo before (so the
    PPE never sees a tail before its output, or an older tail after a
    newer one). A buffer is waited for when it is taken again. 76 KB of the
    local store.
  - The PC check (as before): 5 seeds equal byte for byte (about 2300
    tasks, 4.3 million elements, 1.2 million indices); the simulator's SPU
    time 54 to 63 cycles an element over all types (before 66 to 76), 27
    with the common types (before 31); its DMA has no RSX bandwidth (3.3
    GB/s on hardware), so the output puts that now run beside the work are
    not in these numbers. The build's image has the checked `.text`.
  - `check`: 0 `r2off`, `.got` 0x8300. PKG of 14:06 in `build/ps3_spu/`.
    Not tested on hardware.
- 2026-10-19: PKG of 14:06 on hardware (Matt: "Done"; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[ps3] exit 0`,
  `[spu] mixer down` and `[spu] vertices down`. The conversions' SPU every
  600 frames of play: 30 to 90 thousand tasks, 11 to 25 million elements
  and 8 to 22 million indices; its time 0.64 to 1.29 ms a frame (about 89
  cycles an element and an eighth of the indices, before about 100). The
  waits that found tasks not done: still about one a task (0.75 to 0.94),
  0.57 to 1.31 ms a frame, the same as the SPU's time. The cause:
  `ps3_gpu_commands_check` before every record takes `context->end` as the
  end of the 1 MB buffer, but the system's default FIFO ("traditional"
  mode, `gcm_sys.h`) ends the context at the end of a segment; the
  callback flushes and starts the next segment. A segment under 64 KB
  makes the check wait at every record (the segment's size is not in the
  log; at the buffer's end the check would wait at about 6% of the
  records). 17 timer windows of play, 4 under 45 frames per second;
  medians all / under 45: the frame 17.60 / 27.60 ms (56.7 / 37.1 frames
  per second), game ticks 4.60 / 7.95, render 10.40 / 11.80; the RSX 9.1 /
  11.8; the Direct3D thread: vertices 0.60 / 0.55, indices 0.40 / 0.25,
  the SPU waits 0.90 / 0.95 (before 1.10 / 1.05), state 3.20 / 3.10,
  colours 0.70 / 0.60; the game thread's waits for it 0.80 / 0.25 (before
  1.00 / 0.65). The play differs from the run before: the frames are not
  comparable. Hardware SUCCESS (Matt: "Yes, the picture looked correct.").
- 2026-10-19: Option 1 of the fixes (Matt: "Go with option 1."): the SPU
  wait at the segment's flush, not before every record.
  - `port/ps3/src/ps3_gpu.c`: `ps3_gpu_initialize` keeps the context's
    callback (the system's, a 32-bit descriptor) and puts its own
    (`segment_flush`, its descriptor from `__get_opd32`; once, so it never
    calls itself). `segment_flush` waits for the SPU, puts the system's
    callback back in the context, calls librsx's `rsxContextCallback` (the
    call through the context's 32-bit descriptor), then puts its own back.
    A log line: `[gpu] the command buffer's segment`, its start, end, size,
    the current position and the system's callback. `COMMAND_ROOM` and
    `ps3_gpu_commands_check` removed (`ps3_gpu.h`, `d3d8_rsx.c`'s
    `record_execute`). The waits left: the segment's flush, the present,
    `ps3_gpu_wait_idle`, `finish`, an idle Direct3D thread, a full list or
    ring.
  - The ELF: the descriptor's 32-bit words (code 0x11060, TOC 0x9749d8)
    as the flip handler's (TOC 0x9749d8), which works on hardware.
    `segment_flush`'s code: the wait, a 32-bit store of the system's
    callback at the context's offset 12, `rsxContextCallback`, its own
    back, the result returned.
  - `port/ps3/README.md`: the `ps3_gpu.c` and `d3d8_rsx.c` rows, the log.
  - `check`: 0 `r2off`, `.got` 0x8320. PKG of 14:29 in `build/ps3_spu/`.
    Not tested on hardware.
- 2026-10-19: PKG of 14:29 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[ps3] exit 0`,
  `[spu] mixer down` and `[spu] vertices down`. `[gpu] the command
  buffer's segment: 0x50101000 to 0x50107ffc (28668 bytes)`: the first
  segment starts 4 KB into the IO buffer and ends 4 bytes before its 32
  KB; the cause of the waits at every record confirmed. The conversions'
  SPU every 600 frames of play: 28 to 95 thousand tasks, 9 to 24 million
  elements, its time 0.52 to 1.23 ms a frame (about 89 cycles an element,
  as before). The waits that found tasks not done: 344 to 1180 a 600
  frames (0.6 to 2 a frame; before 28 to 53 thousand), 0.20 to 1.03 ms a
  frame, 340 to 740 us each: 35 to 83% of the SPU's time. Each wait now
  waits for many tasks; the log does not show which waits they are (a
  segment's flush, the present or the idle Direct3D thread). 16 timer
  windows of play, 3 under 45 frames per second; medians all / under 45:
  the frame 17.40 / 26.00 ms (57.6 / 38.4 frames per second), game ticks
  5.05 / 14.60, render 10.25 / 11.10; the RSX 8.9 / 9.9; the Direct3D
  thread: the SPU waits 0.60 / 0.70 (before 0.90 / 0.95), vertices 0.60 /
  0.50, indices 0.40 / 0.20, state 3.25 / 2.90, colours 0.65 / 0.50; the
  game thread's waits for it 1.35 / 0.00 (the frames under 45 do not wait
  for the Direct3D thread). The play differs from the run before: the
  frames are not comparable. Hardware SUCCESS (Matt: "The picture looked
  correct.").
- 2026-10-19: Steps 2 and 3 of the next steps (Matt: "Do 2 and 3."): 3,
  the conversions' SPU faster; 2, exact VMX where the measurements point
  (the rays' objects part: 0.79 / 3.52 ms a frame, the largest).
  - 3, `spu_vertices.c`: the slots of one stream (the same stride, within
    2 KB of a vertex) come in one fetch, the next fetch while one
    converts; each type's loop does four vertices at a time. NORMSHORT,
    NORMPACKED3, D3DCOLOR and PBYTE without double precision or the byte
    table: x / (2^n - 1) has |x|'s n bits repeating as its binary digits,
    so a window of |x|'s copies, normalized, gives the 24 bits and the
    rounding bit (the digits after are never all 0: no ties), in
    integers. Checked on the PC for every input (256 bytes, 65536 shorts,
    2048 and 1024 packed fields) against the float division: equal. The
    SPU's waits for its input and output DMA are timed (the echo), and
    `d3d8_rsx.c` counts the elements by type: both in the 600-frame
    lines. The byte table's DMA is gone (`ps3_spu_vertices_start`).
  - The PC check of 3 (flycastPS3's simulator, as before; now slots that
    share streams too): 14 seeds, 63 million elements, 15 million
    indices: equal byte for byte, no DMA outside a block, the sources
    unchanged. The simulator's SPU 24 to 28 cycles an element or index
    over all types (before 67 to 75), 11 with the dynamic vertices'
    layout (FLOAT3, D3DCOLOR, FLOAT2 in 24 bytes; before 27). 80 KB of
    the local store. The build's image has the checked `.text`.
  - 2, `collisions.c`: the rays' loop tests each cluster's objects first
    against the copies of their bounding spheres
    (`port/linux/game/object_bounds_cache.c`: `fast_vector_intersects_sphere`
    on the copies); an object that the segment misses there fails its own
    test the same way, so the loop skips it (only root objects are in
    the clusters, and their next object is none). Each answer carries its
    test's serial: a test between another's and its answers leaves those
    unknown. An entry is 32 bytes, the sphere first, 16-byte aligned on
    the PS3. The other builds: the same test, scalar.
  - The VMX (`port/ps3/src/ps3_vmx.c`, `include/ps3_vmx.h`, the one unit
    with `-maltivec`): four spheres at once, each lane the scalar
    operations in the same order (a product is a multiply-add of -0, no
    estimates), VSCR's NJ off. The first call compares VMX with the FPU
    (denormal products and sums, signed zeros) and logs `[vmx] exact`;
    else the scalar code. The PPU code: the operations in the source's
    order, no other fused multiply-add.
  - The PC checks of 2: `ps3_vmx.c`'s own source on x86 (each VMX
    operation emulated per lane, the multiply-add fused) against
    `real_math.c`'s `fast_vector_intersects_sphere`: 49.5 million spheres
    (random, near the segments, denormals, zeros, infinities, NaNs): no
    difference. `tools/harness/tests/test_object_bounds_cache.py`: 5
    vector cases and 3 negative controls more, 14 passed (a container
    with a 32-bit runtime). The rest of the harness: 144 failures in
    other tests (sound, transparent geometry, keyboard, elevator), which
    do not compile on `HALO_PTR32` in the game's headers.
  - `[time] collision by function`: its objects tested are now only those
    tested themselves.
  - `check`: 0 `r2off`, `.got` 0x8360. PKG of 15:26 in `build/ps3_spu/`.
    Not tested on hardware.
- 2026-10-19: PKG of 15:26 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[vmx] exact:
  VSCR's NJ off, the denormals as the FPU's` (the PPE's VMX keeps
  denormals: the VMX test runs), `[ps3] exit 0`, `[spu] mixer down` and
  `[spu] vertices down`.
  - The elements converted by type, a 600 frames of play: PBYTE3 (0x34)
    5 to 14 million and SHORT1 (0x15) 2.5 to 7 million (two PBYTE3 and a
    SHORT1 a vertex: declaration 0x20 of
    `rasterizer_xbox_vertex_shaders_initialize.c`), FLOAT3, FLOAT2 and
    D3DCOLOR 0.5 to 1.5 million each, the rest under 0.1 million. The
    PBYTE3s went through the byte table before.
  - The conversions' SPU every 600 frames of play: 28 to 73 thousand
    tasks, 9 to 25 million elements, its time 121 to 319 us a frame: 22 to
    26 cycles an element and an eighth of the indices (before about 89),
    of it its waits for input 22 to 52 and for output 27 to 75 us a
    frame. The waits that found tasks not done: 0.4 to 2.3 a frame, 13 to
    182 us a frame (before 0.20 to 1.03 ms).
  - The rays' objects part: each ray tests 1.8 / 2.2 objects itself
    (before 65 / 93): the copies rule out 97%. But a ray takes 37.2 /
    43.0 us (before 45.4 / 46.5): `object_mark_function`, before the
    copies' answer, reads each object (its marker), so the cache misses
    stay. vector-objects 0.82 / 2.36 ms a frame (before 0.79 / 3.52;
    37.2 / 69.2 rays a frame, before 27.1 / 76.3).
  - 18 timer windows of play, 5 under 45 frames per second; medians all /
    under 45: the frame 18.30 / 24.50 ms (54.7 / 40.8 frames per second),
    game ticks 5.65 / 11.00, render 11.05 / 11.70; the RSX 9.1 / 11.1; the
    collision tests 2.25 / 5.64, rays 2.00 / 4.61; the Direct3D thread:
    the SPU waits 0.10 / 0.10 (before 0.60 / 0.70), vertices 0.70 / 0.50,
    indices 0.35 / 0.30, state 3.40 / 3.00; the game thread's waits for it
    0.45 / 0.00. The play differs from the run before: the frames are not
    comparable.
  - Hardware SUCCESS (Matt: "Yup", to "Was the picture correct this
    run?").
- 2026-10-19: Next steps 1 and 2 (Matt: "1-2").
  - 1, `collisions.c`: the rays' object loop asks the copies' answer
    before `object_mark_function`, which reads the object (its marker):
    an object the segment misses is neither marked nor read. The same
    results: its mark only stops a second test, in another cluster, that
    fails too (the same copy, the same segment).
  - 2, exact VMX in the node matrices: `object_compute_node_matrices`
    makes each node's matrix with `matrix4x3_from_orientation` (scalar:
    it divides) and `matrix4x3_multiply`, whose PS3 code is a loop of 12
    elements into a stack array that is read back at once (each read
    waits for its store). `ps3_vmx_matrix4x3_multiply`: a row of the
    product in each vector, the scalar code's operations in each lane
    ((b[r][0] * forward + b[r][1] * left) + b[r][2] * up; the position's
    row times a's scale, plus a's position), unaligned loads from the
    quadwords that hold the floats only, word stores (no byte outside the
    matrix), result as a or b. `matrix_math.c` calls it when
    `ps3_vmx_exact` (so every caller gets it: units, physics, models).
    VSCR's NJ is now set by an `mtvscr` with a memory clobber before
    each call's loads; `ps3_vmx.c` builds with `-ffp-contract=off` too.
    `matrix_math.c`'s symbol table header points to upstream's copy.
  - The PC checks: `ps3_vmx.c`'s own source on x86 (the VMX operations
    emulated per lane, the loads, permutes and word stores as VMX does
    them) against `matrix_math.c`'s `matrix4x3_multiply`: 4 million
    products (every 4-byte alignment, result as a, as b or apart, random
    and special values): equal, but 158 where both give a NaN with other
    bits (the x86 emulation's fused multiply-add and SSE make different
    NaNs; the PC builds and the PPE already differ there); no byte
    written outside. The sphere test's check again: 49.5 million, equal.
    The PPU code: `mtvscr` before the loads, each product a multiply-add
    of -0, nothing else fused, 13 word stores.
  - `check`: 0 `r2off`, `.got` 0x8360. PKG of 15:50 in `build/ps3_spu/`.
    Not tested on hardware.
- 2026-10-19: PKG of 15:50 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[vmx]
  exact`, `[ps3] exit 0`, `[spu] mixer down` and `[spu] vertices down`.
  18 timer windows of play, 1 under 45 frames per second (the "under 45"
  values are that one window); medians all / under 45:
  - The rays' objects part: 29.8 / 36.4 us a ray (before 37.2 / 43.0),
    28.8 / 84.5 rays a frame; vector-objects 0.60 / 3.08 ms a frame; 60 /
    260 objects tested themselves a frame. The rest of a ray's time: its
    clusters' object lists, walked twice (the copies' test, then the
    loop), and the copies and answers read by absolute index.
  - Node matrices 1.00 / 1.40 ms a tick (before 1.08 / 1.15): the
    multiply is a small part of the node time.
  - The frame 17.45 / 28.10 ms (57.4 / 35.5 frames per second), game
    ticks 4.70 / 16.30, render 10.40 / 11.30; the RSX 8.7 / 9.1; the
    collision tests 1.71 / 7.51, rays 1.44 / 5.51; the Direct3D thread's
    SPU waits 0.10 / 0.10. The play differs from the run before: the
    frames are not comparable.
  - Hardware SUCCESS (Matt: "Picture was correct").
- 2026-10-19: Next steps 1 and 2 (Matt: "Go with 1-2.").
  - 1, each cluster's object list walked once: `object_bounds_cache.h`
    (new) holds an iterator of 64 objects; `object_bounds_cache_first_vector`
    runs the copies' test over the whole list (four at once with VMX) and
    keeps, in the list's order, the objects it does not rule out;
    `_next_vector` gives them. With more than 64, the list is walked again
    and each object's copy tested as it comes (the scalar test: the same
    answers). `collisions.c`'s loop takes the iterator; the answers by
    absolute index and their serial are gone. The same objects marked and
    tested, in the same order.
  - `tools/harness`: the vector cases check the iterator (the objects
    given in the cluster's order, each left out missing itself, and none
    given missing when every copy is current; the list walk of a full
    iterator too), and 3 negative controls (the epoch not checked, nothing
    left out, the objects past 64 dropped): 13 passed.
  - 2, the node matrices timed (`[time] node matrices`, ms a tick):
    `object_compute_node_matrices` at every call, its steps: the
    animation's orientations (`animation_get_node_orientations` or the
    model's), the overlays and blends (overlays, scale, the type's
    preprocess, interpolation), the matrices (the node loop, the bounding
    sphere); `object_postprocess_node_matrices`; its calls and nodes a
    tick.
  - `objects.c`'s comments over 2 lines shortened (19; its code without
    comments differs only by the timers). `check`: 0 `r2off`, `.got`
    0x8368. PKG of 16:22 in `build/ps3_spu/`. Not tested on hardware.
- 2026-10-19: PKG of 16:22 on hardware (Matt: "Done"; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[vmx]
  exact`, `[ps3] exit 0`, `[spu] mixer down` and `[spu] vertices down`.
  21 timer windows of play, 4 under 45 frames per second; medians all /
  under 45:
  - The rays' objects part: 23.3 / 25.3 us a ray (before 29.8 / 36.4; at
    the start of the work 45.4 / 46.5), 33.1 / 43.5 rays a frame;
    vector-objects 0.52 / 1.05 ms a frame.
  - Node matrices (objects_update's) 1.01 / 1.06 ms a tick. In
    `object_compute_node_matrices` at every call: the matrices 0.45 /
    0.45 ms a tick, the orientations 0.21 / 0.20, the overlays and blends
    0.18 / 0.26; the postprocess 0.06 / 0.05; 231 / 231 calls and 569 /
    506 nodes a tick: 2.5 nodes a call. A call's matrices take 2.0 us
    (0.86 us a node), its orientations 0.92, its blends 0.78: most calls
    are an object of one or two nodes, so the root's work counts most
    (the object's translation and rotation, its offset and center of
    mass, 2 to 4 multiplies, `valid_real_matrix4x3` twice, the bounding
    sphere), and the reads of the object's and its tags' data.
    `valid_real` reads a float's bits through memory: on the PPE a store
    read back at once, about 10 in each `valid_real_matrix4x3`.
  - The frame 18.20 / 24.65 ms (55.0 / 40.7 frames per second), game
    ticks 5.10 / 8.20, render 10.30 / 10.55; the RSX 8.8 / 10.8; the
    collision tests 1.87 / 3.36, rays 1.61 / 2.44. The play differs from
    the run before: the frames are not comparable.
  - Hardware SUCCESS (Matt: "Yeah, it was").
- 2026-10-19: Next steps 1 to 3 (Matt: "All 3.").
  - 1, `real_math.h`'s `valid_real` on the PS3: `n - n == 0.0f` (0 for
    every finite n, NaN for an infinity or a NaN) in place of the float's
    bits read through memory (on the PPE a store read back at once). The
    same answer for every float: all 2^32 checked on the PC. The PPU code
    of `object_compute_node_matrices`: 30 `fsubs n,n,n`, no exponent mask.
    Every `valid_real*` check gains (the matrix checks: about 20 a call).
  - 2, the matrices step timed inside (`[time] node matrices`): the first
    node (its matrices and multiplies), the validity checks (the parent
    node's, and the first node's twice), the other nodes, the bounding
    sphere (with the copy's update and the objects without a model).
  - 3, the next target, the particles (1.11 / 1.96 ms a frame in the last
    run, above the AI's 1.21 / 1.72 ms a tick, about 1.0 a frame at 0.6
    ticks a frame), timed inside as the node matrices were (`[time]
    particles`, ms a frame): `particles_update` (its particles' physics,
    the particles and those with physics), `contrails_update`,
    `particle_systems_update` (`game.c`, `particles.c`).
  - Comments over 2 lines shortened: `real_math.h` (3; a function in a
    comment is now `#if 0`), `particles.c` (3) and `game.c` (7; their
    symbol tables point to upstream's copy). Without comments the four
    game files differ from git only by these changes. `check`: 0 `r2off`,
    `.got` 0x8370. PKG of 16:58 in `build/ps3_spu/`. Not tested on
    hardware.
- 2026-10-19: PKG of 16:58 on hardware (Matt: "Done."; our listener, from
  the boot): menus, a30, `[spu] vertices up`, no `SPU off`, `[vmx]
  exact`, `[ps3] exit 0`, `[spu] mixer down` and `[spu] vertices down`.
  22 timer windows of play, 5 under 45 frames per second; medians all /
  under 45:
  - Node matrices 1.06 / 1.47 ms a tick: orientations 0.23 / 0.26,
    overlays and blends 0.18 / 0.49, matrices 0.50 / 0.55 (first node
    0.18 / 0.18, checks 0.15 / 0.15, other nodes 0.15 / 0.18, bounds 0.05
    / 0.05), postprocess 0.06 / 0.06; 569 / 687 nodes a tick. A call:
    first node 0.78 us, checks 0.65, bounds 0.21; another node 0.47 us.
    The matrices step was 0.45 / 0.45 before with 4 timer reads fewer a
    call: no gain from `valid_real` shows.
  - Particles, ms a frame: particles_update 0.80 / 1.72, of it their
    physics 0.69 / 1.48 (196 / 392 particles a frame, nearly all with
    physics: 4.5 us a particle, 3.9 in its physics); the particles' rays
    (pt-phys) 0.43 / 0.89 of the physics; contrails 0.13 / 0.17 (0.4 to
    0.9 in 8 of the windows); particle systems 0.00 / 0.00 (up to 0.32).
  - AI 1.32 / 4.08 ms a tick: in the frames under 45 the largest tick
    part.
  - A ray's objects part 25.0 / 26.0 us. The frame 16.95 / 23.70 ms (59.1
    / 42.1 frames per second), game ticks 5.50 / 11.70, render 9.45 /
    9.70. The play differs from the run before: the frames are not
    comparable.
  - Hardware SUCCESS (Matt: "Yes, the picture was still correct. I will
    tell you if the picture is wrong from now on.").
- 2026-10-19: Next steps 1 to 3 (Matt: "All 3.").
  - 1, the particles' physics: `point_physics_update` timed at every call
    (`[time] point physics`, ms a frame: its time, its wind lookup
    `scenario_get_current`, its rays loop; its calls and rays a frame).
    What a particle's ray tests: `collision_test_vector` with front-facing
    surfaces, the structure if its physics has structure collisions, water
    (media) if it has water collisions, never objects; up to 3 rays an
    update (each bounce goes on along the rest of the frame's motion). So
    the objects' copies do not shorten these rays: their cost is the
    structure BSP's.
  - 2, the AI timed (`[time] ai`, ms a tick): `ai_update`'s conversations,
    encounters, actors and the rest; each actor's update by step (general
    and input, perception, situation to decisions, actions, control and
    communication, movement, look and combat, unit control); the actors a
    tick.
  - 3, why the validity checks cost 0.65 us a call: the PPU code of
    `valid_real_matrix4x3` (inline, no call) is ten tests, each a chain of
    3 to 6 dependent float operations (about 10 cycles each on the PPE)
    then a compare and a branch; the PPE issues in order, so each test
    waits for its whole chain before the next starts: about 600 to 700
    cycles a matrix, 2 to 3 matrices a call. The fix to offer: every term
    first, then the tests (the same answer; `valid_realcmp(y)` is
    `fabs(y) < 0.001f`, false for a NaN or an infinity, without its
    `valid_real`).
  - Comments over 2 lines shortened: `point_physics.c` (1), `ai.c` (2) and
    `actors.c` (5; their symbol tables point to upstream's copy). Without
    comments the three files differ from git only by the timers. `check`:
    0 `r2off`, `.got` 0x8380. PKG of 17:31 in `build/ps3_spu/`. Not tested
    on hardware.
- 2026-10-19: PKG of 17:31 on hardware (Matt: "Done."; no fault reported;
  our listener, from the boot): menus, a30, `[spu] vertices up`, no `SPU
  off`, `[vmx] exact`, `[ps3] exit 0`, `[spu] mixer down` and `[spu]
  vertices down`. 28 timer windows of play, 2 under 45 frames per second
  (both RSX-bound: the RSX 15.3 ms a frame); medians of all:
  - The AI, in the 21 windows with actors (the level's first windows have
    none): actors 2.21 ms a tick for 7 actors, up to 4.58 for 14: 313 us
    an actor, of it perception 128 (0.96 ms a tick), control (action
    control, communication, conversation) 42 (0.52), movement
    (destination, look's effect, move) 41 (0.46), decisions (situation,
    emotion, orders, decision loop) 35 (0.37), general 0.11, look and
    combat 0.10, unit control 0.02, actions 0.00 ms a tick; conversations
    0.00, encounters 0.01, the rest 0.04.
  - Point physics 0.69 ms a frame, 196 calls (one ray each): 3.5 us a
    call, of it the wind lookup (`scenario_get_current`) 0.9 us (0.18 ms
    a frame) and the ray 2.2 us (0.42). Particles: particles_update 0.83,
    their physics 0.72, 181 particles a frame.
  - Node matrices as before (matrices 0.52 ms a tick: first node 0.18,
    checks 0.15, other nodes 0.15, bounds 0.05).
  - The frame 16.70 ms (59.9 frames per second), game ticks 5.55, render
    7.75, the RSX 8.35. The play differs from the run before: the frames
    are not comparable.
- 2026-10-19: Next steps 1 to 4 (Matt: "Do all 4 then.").
  - 1, perception timed (`[time] ai parts`, ms a tick, `actor_perception.c`):
    `actor_perception_update`'s refresh (timeslice), danger zone, and props
    loop, with the props' position and status refreshes inside it; the
    props and the refreshes a tick.
  - 2, control and movement timed by call (`actors.c`): clear output,
    action control (the swarm's control too), communication, conversation;
    destination, look's effect on movement, move. `[time] ai` gives their
    sums as before.
  - 3, why the wind lookup costs 0.9 us a call (`scenario_get_current`,
    `wind.c`, `scenario.c`; PPU code seen): for each particle it finds
    again what its cluster gives: the BSP 3 times (`global_structure_bsp_get`,
    a call with its assert), the cluster twice, the fog region
    (`scenario_get_fog_region_index`: the cluster's fog plane, its region's
    fog, `fog_definition_get`, the plane test), the fog palette's fog
    again, the weather palette and the wind (`wind_definition_get`): 2 to
    3 `tag_get` calls (each `cache_get_tag_instance` and the group tests).
    `wind_variance_get` moves a float's bits through memory 3 times an axis
    (9 store-and-read stalls), and the tick count to a float once more. The
    fix to offer: each cluster's palette and definitions kept for the tick
    (the fog plane test still at each call), and the variance's axes
    without the stalls (the same answer).
  - 4, the validity checks (`real_math.h`, PS3): `valid_real_matrix4x3`
    computes its six terms first, then tests all ten with `&`. PPU code:
    no branch (the terms in parallel, then the compares, `mfocrf`, `and`).
    PC check (gcc, `-ffp-contract=off`): 50,000,000 matrices (near
    orthonormal, with NaNs, infinities, denormals and random bits;
    37,019,466 valid): 0 differ from the `&&` form with the float bits.
  - Without comments the changed files differ from git only by the timers
    and the checks. `check`: 0 `r2off`, `.got` 0x8388. PKG of 18:05 in
    `build/ps3_spu/`. Not tested on hardware.
- 2026-10-19: PKG of 18:05 on hardware (Matt: "Done."; no fault
  reported). The boot's first lines are not in the log: our listener's
  first start failed (a Windows path given to MSYS's python), the second
  started after the boot. From the menus on: a30, both SPUs on (no `SPU
  off`), `[ps3] exit 0`, `[spu] mixer down` and `[spu] vertices down`.
  29 timer windows of play, 1 under 45 frames per second (the level's
  start, no actors); medians of all:
  - The validity checks 0.09 ms a tick (before 0.15), 0.39 us a call
    (before 0.67) for 232 calls a tick; the matrices step 0.48 ms a tick
    (before 0.52): first node 0.19, other nodes 0.15, bounds 0.05.
  - The AI, in the 22 windows with actors: actors 2.20 ms a tick for 7
    actors (up to 14): 315 us an actor, of it perception 122, control 44,
    decisions 37, movement 33, general 14, look and combat 13.
  - Perception: the timeslice refresh 16 us an actor, the danger zone 0,
    the props loop 105: its props' position and status refreshes 0.79 ms
    a tick, 8.8 a tick, 77 us each (47 to 108); the rest 1.5 us a prop
    (23.6 props a tick). The AI's line-of-sight rays (`ai_test_line_of_sight`,
    `ai-los`) 0.67 ms a tick, 8.2 rays a tick, 57 us a ray (44 to 118):
    86% of the refreshes' time.
  - Control: action control 43 us an actor (0 in the windows with 3
    actors, 31 to 134 in combat with 13 and 14); clear output,
    communication and conversation 0 to 1. Movement: the move 29 us an
    actor (157 to 262 in the windows with 3 actors, 6 to 9 with 7 to 12);
    destination 1, look's effect 0.
  - Point physics: the wind 0.19 ms a frame, 0.88 us a call (211 calls),
    the rays 0.47; particles_update 0.89, their physics 0.76.
  - The frame 17.00 ms (58.8 frames per second), game ticks 5.70, render
    8.70, the RSX 8.5. The play differs from the run before: the frames
    are not comparable.
- 2026-10-19: Next steps 1 to 4 (Matt: "Do all 4 next steps.").
  - 1, the AI's line of sight timed (`[time] ai line of sight`, ms a
    tick): `ai_test_line_of_sight`'s calls and those with a ray (the
    others the PVS rules out), the first ray, the fog
    (`scenario_fog_at_point`), the mode's rays, the rest; its rays a tick
    and the first rays' length. Its rays' parts (the user `ai-los` on top
    of the collision stack, `collision_usage.h`): the structure part with
    its bsp and bsp nodes, the objects part with the models, the object
    bsps and their nodes; the leaves, the clusters whose objects the rays
    walked and the objects tested.
  - 2, action control by action (`[time] action control`, `actions.c`):
    each of the 14 actions' time and calls a tick.
  - 3, the move (`[time] ai move`, `actor_moving.c`): `actor_move_update`
    has no path search; its parts: the flying vector avoidance, the
    movement (`actor_move_calculate_movement`: in it the pathfinding
    location and the line test), the firing positions, the rest. The path
    searches are `actor_path_refresh` (from action control's
    `actor_move_to_*`, `actor_move_halt*`, and `actor_destination_update`):
    timed wherever it runs, with its path builds (`path_state_find` and
    the build).
  - 4, the wind (`wind.c`, PS3): `scenario_get_current`'s lookups for a
    cluster (its weather palette entry; its fog region's, with the fog's
    water flag; the fog plane and the water's distance) and each weather
    palette entry's wind definition, kept for one tick (`wind_update`'s
    time), structure bsp and map, 32 clusters direct mapped; the fog
    plane's test still at each call; a cluster or entry the structure bsp
    does not have takes the code as it was (its error report at each
    call). `wind_variance_get`: the three keys made, then stored and read
    once, `fabs` for the sign mask, the time as a real once a tick, the
    axis scales in the program's data (PPU code: three stores, then the
    loads; before, 10 store-and-read stalls a call). PC check (gcc,
    `-ffp-contract=off`, both forms taken from the sources): 29,101,907
    calls with ticks, structure bsp switches (one at the same address with
    new data) and map changes between them, NaNs, infinities, denormals,
    bad indices: 0 differ in the wind's bits, the water flag or the block
    errors; 11,774,506 calls found their cluster's lookup kept. Six
    faults put in the PS3 code (no bsp index test, no water distance, no
    `fabs`, no water flag, a wrong cluster kept, a stale time) each
    differ.
  - Comments over 2 lines shortened: `actions.c` (3), `actor_moving.c`
    (3), `wind.c` (1; the symbol tables point to upstream's copy).
    Without comments the files differ from git only by the timers and the
    wind. Warnings (`-Wall`) in the changed files: none in the new code.
    `check`: 0 `r2off`, `.got` 0x83f8. PKG of 18:58 in `build/ps3_spu/`.
    Not tested on hardware.
- 2026-10-19: PKG of 18:58 on hardware (Matt: "Done"; no fault reported;
  our listener, from the boot): menus, a30, `[spu] vertices up`, `[spu]
  mixer up`, `[vmx] exact`, no `SPU off`, `[ps3] exit 0`, `[spu] mixer
  down` and `[spu] vertices down`. 25 timer windows of play, 2 under 45
  frames per second (no actors in them: the level's start, 40.4 ms a
  frame, 20.1 of it outside the phases; and an RSX-bound one, the RSX
  13.3 ms); medians of all:
  - The wind 0.60 us a call (before 0.88), 0.12 ms a frame for 220 calls
    (before 0.19 for 211); point physics 3.0 us a call (before 3.5).
  - The AI, in the 18 windows with actors: 2.00 ms a tick for 5.2 actors
    (up to 14): 326 us an actor, of it perception 120, movement 40,
    decisions 35, control 25.
  - Action control is the guard action (90 us a call; up to 1.66 ms a
    tick with 14 actors); the other actions about 0. It is the path
    builds: `actor_path_refresh` 0.36 ms a tick, its builds 0.36 (in the
    windows with 7 to 14 actors 0.73 to 1.78 ms a tick, 0.3 to 0.6
    builds a tick: 1.8 to 3.6 ms a build, in one tick). The 3-actor
    windows' builds cost about 0.
  - The move is the flying vector avoidance (the flying vehicles'
    pilots): 0.36 ms a tick, 334 us a call, 1 to 2 calls a tick; each
    call 25 rays (9
    sense rays, 8 directions of 2), each up to 2 structure bsp tests and
    the pills of the objects near. The movement 0.03, the rest 0.02.
  - The line of sight 0.58 ms a tick (up to 1.24 with 14 actors), 4.8
    calls a tick, all with a ray (none ruled out by the PVS), 7.9 rays (the
    mode's extra rays 0.16 ms); 59 us a ray: the objects part 38 (models
    19, of it object bsps 14; the rest, 19, the loop over the clusters'
    objects), the structure part 16 (45 bsp nodes); a ray 38 world units
    long on median, 5.4 leaves, 1.3 clusters, 3.9 objects tested. The fog
    0.00.
  - Node matrices: the checks 0.08 ms a tick, the matrices 0.45.
  - The frame 17.00 ms (58.9 frames per second), game ticks 5.40, render
    9.40, the RSX 8.8. The play differs from the run before: the frames
    are not comparable.
- 2026-10-19: Next steps 1 to 3 (Matt: "Do 'em").
  - 1, a path build timed inside (`[time] ai paths`, `actor_moving.c`):
    the setup (`actor_path_input_new`, `path_state_new` with the debug
    storage `ai_debug_get_path_storage` gives every build, the
    destination), the search (`path_state_find`) with the nodes it made
    (`node_count`), the path made from it (`path_state_build_path`); the
    flying builds (`path_3d_build_path`) and the cached state's apart. A
    timed call keeps its result (`PS3_MOVE_TIMED`, a GCC statement
    expression; the call alone on the other builds).
  - 2, the flying vector avoidance timed inside (`[time] ai avoidance`):
    `actor_move_avoidance_setup` with the objects it found, the rays'
    structure bsp tests (`collision_bsp_test_vector`, up to 2 a ray) and
    their pill tests against those objects, with their counts; the rest.
  - 3, the line-of-sight rays' loop over a cluster's objects (`[time] ai
    line of sight objects`, `collisions.c`): the loop's time, its marks'
    and object tests' time (the iterator: the difference); the objects
    the copies' iterator walked (`ps3_bounds_walked`,
    `object_bounds_cache.c`), those it passed, those tested.
  - `check`: 0 `r2off`, `.got` 0x8430. Warnings (`-Wall`) in the changed
    files: none in the new code. PKG of 19:38 in `build/ps3_spu/`. Not
    tested on hardware.
- 2026-10-19: Matt: "are we close at getting 60 stable FPS?". The answer,
  from the PKG of 18:58's run: not yet; close in light play. 11 of 25
  windows held 60, 7 were under 55. Two limits: a combat tick (13 to 16
  ms; a tick and two renders must fit in 33.3 ms, so with renders of 8
  to 10 ms a tick must stay under about 13 to 15 ms), and the RSX in
  some views (13 to 17 ms a frame without actors). Only a30 measured;
  the windows hide single slow frames.
- 2026-10-19: PKG of 19:38 on hardware (Matt: "Done."; no fault reported;
  our listener, from the boot): menus, a30, `[spu] vertices up`, `[spu]
  mixer up`, `[vmx] exact`, no `SPU off`, `[ps3] exit 0`, `[spu] mixer
  down` and `[spu] vertices down`. 30 timer windows of play, 3 under 45
  frames per second; medians of all / under 45:
  - The frame 16.80 / 25.70 ms (59.6 / 38.8 frames per second), game
    ticks 5.65 / 4.10, render 9.00 / 11.70, the RSX 8.7 / 12.7. The
    three under 45: the level's start (32.9), a view the RSX holds (40.9:
    the RSX 17.8 ms a frame) and the heaviest combat (38.8: 19.0 ms a
    tick, 0.77 ticks a frame as the game catches up; the objects 8.25 ms
    a tick, of it vehicles 3.10, bipeds 1.74, node matrices 1.29; the AI
    4.58 for 11.7 actors, of it decisions 1.66, perception 1.41;
    collision 7.02 ms a frame for 958 tests; particles_update 2.21 ms a
    frame for 492 particles). A window at 45.2 is the same combat (18.8
    ms a tick).
  - Path builds, in the 15 windows with searches: the setup 50 us, the
    search (`path_state_find`) 1133 us (167 to 2300), the path made from
    it (`path_state_build_path`) 1500 us (200 to 2800); 494 nodes a
    search (11 to 977, of the 1024 a state holds); 0.2 builds a tick.
  - The vector avoidance, in the 15 windows with calls: 310 us a call, 2
    calls a tick: the 50 structure bsp tests 150 us (3.0 us each), the
    rest 90 (the weights, the debug arrays it fills, the timers), the
    setup 40 (1 object found on median, up to 9.7), the 25 pill tests 5.
  - A line-of-sight ray, in the 24 windows with actors (11.1 rays a
    tick): 61.7 us, its objects part 46.0, structure 14.0. The objects
    loop 44.5 us a ray: the iterator 19.5 (122 objects walked a ray, 158
    ns each: the cluster's list and the copies), the marks and object
    tests 25.7 (5.2 objects tested a ray; 5.3 passed by the copies).
  - The wind 0.54 us a call (0.14 ms a frame).
- 2026-10-19: the checkpoint stutter (Matt: "There where also stutters when
  it reached a "checkpoint". It happens at every level start."). Cause,
  from every log of the day: `game_state_save` (game_state.c) writes the
  whole game state, 20 MB (the native builds' larger state:
  `HALO_PORT_GAME_STATE_SIZE`; the Xbox's was 3.3), to `z:\savegame.bin`
  on the game thread (`game_state_write_to_file`, one `WriteFile`; the
  game's clock stopped meanwhile): 1.30 to 1.50 s each (once 4.39 s),
  about 15 MB/s, two in each a30 run (the level's start and its first
  checkpoint). A revert reads the file back: 0.32 to 0.38 s. The fix to
  offer: the state copied into a 20 MB buffer in main memory at the
  checkpoint (a few ms), the file written from it by a thread of its
  own, and a revert from the copy. Its cost: 20 MB of main memory (the
  lowest free 41.7 MB in this run, 36 MB on 2026-10-16); the used part
  of the state alone, if it is smaller.
- 2026-10-19: Steps 1 to 5 (Matt: "Do it along all the steps you
  mentioned, so we concretize the amount of things we test.").
  - 5, the checkpoint (`game_state_xbox.c`, PS3): `game_state_write_to_file`
    copies the state (20 MB) into a buffer of main memory (allocated at
    the first checkpoint, kept) and returns; a thread of its own
    (`CreateThread`) writes the file from the copy in parts of 256 KB (the
    game's reads of its files between them) and logs `[halo] checkpoint:
    ... written by the writer in N ms`; the copy logs `... copied in N
    ms`. The next checkpoint and the file's close wait for the last write
    first. A revert (`game_state_read_from_file`) takes the copy
    (`game_state_image_accept` on it: the bytes the file holds; no 20 MB
    read nor its 20 MB buffer). With no memory for the copy, the write is
    as before; with no thread, the copy is written at once. A failed
    write is reported at the next wait (as before, `match_vassert`), and
    a revert still has the copy.
  - 4, each cluster's kept block (`object_bounds_cache.c`, PS3): its
    collideable objects in its list's order with their copies' spheres
    (and whether each copy is known), 16 bytes a sphere, made at the
    first ray of a generation (1 + the collideable partition's
    `port_modification_count` + `object_bounds_writes`, the copies'
    writes and invalidations; as the lights' kept lists,
    `object_lights.c`), 4096 entries for all clusters; the VMX batches
    read the block, not the list and the scattered copies. A cluster out
    of range or past the entries: the list walked as before. PC check
    (gcc, both forms from the sources: the plain walk with
    `vector_missed`, the PS3 one with a scalar stand-in for the VMX test):
    2,789,659 segments with VMX and 930,108 without, while objects move,
    leave and join clusters, change datums (unknown copies) and the
    copies are invalidated: 0 differ in the objects given and their
    order; four faults (no list counter, no copy counter, known always,
    a radius halved) each differ.
  - 1, a path search timed inside (`[time] ai path search`, `path.c`): the
    reset, the begin, the traverse (its heap pops, the surfaces' edges
    (`build_path_edges_for_surface`, with the surfaces), the edges' loops
    with their heap inserts, the closest points in them), the copy of the
    state into the debug storage; the path made: `path_smooth`,
    `path_avoid_obstacles`.
  - 2, the vehicles (`[time] vehicles`, `vehicles.c`): `vehicle_update`'s
    physics (`update_*_physics`, with the vehicles simulated), the effects
    after it (slipping, suspension, crashing, airborne), the stopping, the
    animation (with the vehicles).
  - 3, the decisions by call (`[time] ai decisions`, `actors.c`):
    `actor_situation_update`, `actor_emotion_update`, `actor_clear_orders`,
    `actor_decision_loop` (its `actor_type_decide_action` and
    `actor_action_perform`, with their calls); `[time] ai` gives their sum.
  - Comments over 2 lines shortened: `path.c` (4), `vehicles.c` (7; the
    symbol tables point to upstream's copy). Warnings (`-Wall`) in the
    changed files: none in the new code. `check`: 0 `r2off`, `.got`
    0x8450. PKG of 21:17 in `build/ps3_spu/`. Not tested on hardware.
- 2026-10-19: PKG of 21:17 on hardware (Matt: "Done. The fix for the
  stutter worked, though"; no fault reported; our listener, from the
  boot): menus, a30, `[spu]` and `[vmx]` as before, `[ps3] exit 0`. 36
  timer windows of play, none under 45 frames per second; medians of
  all:
  - The checkpoints (the level's start and the first one): `[halo]
    checkpoint: 20480 KB copied in 30 ms` and `29 ms` (the game thread),
    `written by the writer in 1848 ms` and `1558 ms` (its own thread; the
    whole write was 1.30 to 1.50 s on the game thread before). A revert
    ("the saved game is taken") took the copy. Main memory: 45.6 MB free
    before the first checkpoint, 23.6 MB after it (the copy kept), the
    lowest 18.6 MB (t=225 s); the heap 46.8 to 73.5 MB.
  - The rays' iterator, the AI's line-of-sight rays (29 windows with
    actors): 9.3 us a ray (before 19.5), 93.2 objects a ray read from
    kept blocks, 0.1 walked in the lists; the objects part 32.3 us a ray
    (before 46.0), a ray 55.0 us (before 61.7).
  - Path builds (11 windows with searches): a search 1367 us: the
    traverse 1325 (pops 200, the surfaces' edges 467 for 576 surfaces,
    the edges' loops 600, the closest points 0), the debug copy 225, the
    reset and begin 0; 499 nodes. The path made 1550 us: its smoothing
    (`path_smooth`) 1267, the obstacle avoidance 300.
  - Vehicles (33 windows with vehicles): the physics 0.82 ms a tick (up
    to 3.01), 2 simulated a tick, 400 us each (up to 1122); effects,
    stopping and animation about 0.
  - The decisions (lighter AI than before: 3 actors on median, up to 14):
    the decision loop 21 us an actor (perform 11, decide 7), situation
    3, emotion 3, orders 0; with 12 to 14 actors the loop 0.27 to 0.62 ms
    a tick, most of it perform (0.16 to 0.49).
  - The frame 16.80 ms (59.5 frames per second), game ticks 5.25, render
    8.55, the RSX 8.1. The slowest: 46.7 and 47.4 (a tick 14.8 to 14.9
    ms: the objects 6.86 with vehicles 3.12; the AI 3.48 for 13 actors)
    and 48.5 and 49.7 (the RSX 16.5 to 17.8 ms, no actors).
- 2026-10-19: Steps 1 to 5 (Matt: "Yeah, do all those. We're doing
  great!").
  - 1, the smoothing timed inside (`[time] ai path smoothing`,
    `path_smoothing.c`): the pill tests from the current point to each
    raw step (`structure_test_pill2d`), the turning points
    (`find_turning_point`, both ways), the surface found and the point
    projected; with their calls.
  - 2, the search's debug copy (`path.c`, PS3): the 70 KB
    `state->debug->path_state = *state` only while `ai_debug.path`,
    `render_paths` or `render_paths_failed` is on: the only readers are
    `ai_debug.c`'s `ai_debug_render_path_storage` (by those three) and
    `ai_debug_render_paths_failed`; the rest of the debug storage as
    before. (Paths searched while those were off show no search when one
    is turned on, until searched again.)
  - 3, the vehicles' physics timed inside (`[time] physics`,
    `physics.c`): `physics_compute_new` (the forces, with the mass points
    as their count), `physics_update_new` (the movement),
    `physics_compute_unit_collisions`, `physics_update_old` (a physics
    with a radius).
  - 4, the surfaces' edges kept (`path.c`, PS3): `path_edges_kept_get`
    keeps `build_path_edges_for_surface`'s edges by surface (1024 slots by
    the surface's index, up to 8 edges: a ring has at most
    `MAXIMUM_EDGES_PER_COLLISION_SURFACE`, 8), 270 KB; read only from the
    structure bsp's tag data, they are kept while the structure bsp
    (pointer and index) is the same, forgotten at a map's start and end
    (`paths_initialize_for_new_map`, `paths_dispose_from_old_map`).
    `[time] ai path search` gives the surfaces whose edges were kept. PC
    check (gcc, both from the sources): 19,992,036 lookups (9,984,793
    kept) over three random structure bsps, switched, reloaded at the
    same place under another index, and new maps: 0 differ; faults (no
    index test, no forgetting, a wrong slot test) each differ (with no
    pointer test, none: an index is always the same data).
  - 5, the checkpoint's copy (`game_state_xbox.c`, `game_state.c`, PS3):
    only the state's used parts (`game_state_port_used_sizes`: the CPU
    part's allocations from its start, the GPU part's from its end), once
    the unused middle is checked to be zeroes (else the write as before);
    the writer writes the used parts and zeroes between them (the same
    bytes). A revert puts the parts over the game state, clears the
    middle and checks it there (`game_state_image_accept` on the game
    state itself, which no longer copies an image onto itself); a refusal
    resets the map, as before. The log gives the parts' sizes.
  - Comments over 2 lines shortened: `path_smoothing.c` (4),
    `physics.c` (5), `game_state.c` (5), `game_state.h` (1). Warnings
    (`-Wall`) in the changed files: none in the new code. `check`: 0
    `r2off`, `.got` 0x8460. PKG of 22:08 in `build/ps3_spu/`. Not tested
    on hardware.
- 2026-10-19: PKG of 22:08 on hardware (Matt asked "Should I try a bigger
  level than "Halo"?"; b40 proposed). The first load of b40 copied
  `d:\maps\b40.map` (182,494 KB) into the map cache (`z:\cache001.map`)
  in 43 s (read 6.2 s, the writes about 11 MB/s), the game's main loop
  waiting and the picture still (Matt: "...Did the game freeze? Or is it
  loading?"). Then at the level's first checkpoint: "EXCEPTION halt in
  c:\halo\SOURCE\cseries\debug_memory.c,#165: Attempted an operation with
  pointer at 0x0, outside of the valid pointer range. (source/saved
  games/game_state_xbox.c:799)" (Matt: "Got a red screen."). Cause: the
  compact checkpoint's `free(game_state_checkpoint.image)` before the
  first copy was made: the game's `free` is `match_free`, which halts on
  NULL. Fix: `free` only of a copy made. PKG of 22:24 in `build/ps3_spu/`.
  Memory before the halt: 42.3 MB free at the lowest (b40, t=107 s).
- 2026-10-19: PKG of 22:24 on hardware (b40; Matt: "Run's done!"; no
  fault reported; our listener, from the boot): menus, b40 from the map
  cache (no copy), `[ps3] exit 0`. 35 timer windows of play, 2 under 45
  frames per second (the load's, 32.4; the last, 37.5). Medians of all /
  under 45, and sums of the run for the costs of a call (each window's
  ms a tick times its ticks):
  - The frame 16.70 / 28.75 ms (59.9 / 35.0 frames per second), game
    ticks 2.60 / 4.20 ms a frame (5.2 / 5.5 ms each), render 6.50 /
    11.85, the RSX 11.80 / 14.15 (a30: 8.1).
  - The checkpoints (the level's start and two more): `17942 KB of the
    state's 20480 KB copied in 38 ms` (then 40, 39; the CPU part's 17,782
    KB, the GPU part's 160 KB) on the game thread; a30's whole copy took
    29 to 30. Written by the writer in 1785, 2033 and 1513 ms. A revert
    ("the saved game is taken") took the copy. Main memory: 42.3 MB free
    before the first checkpoint, 25.3 after it, the lowest 18.3 (t=218
    s). The heap grows in play, its use not: 66.7 to 73.7 MB, in use
    64.8 to 66.6 (a30: 68.8 to 73.5, in use 67.2 to 69.0).
  - Path searches: 101,066 surfaces, their edges kept for 91%. A
    surface's edges 1.07 us (a30, built: 0.82), the edges' loops 0.97
    (1.05), the pops 0.24 (0.34); the debug copy 0 (a30: 0.45 us a
    surface, 225 us a search). Windows with 96 to 99% kept: 0.81 to 1.24
    us a surface; a least-squares fit over 25 windows: a kept surface
    1.09 us, a built one 0.92. The kept copies are not faster than the
    builds (cache misses in the 270 KB table as in the tag data). Most of
    b40's search time is not `actor_path_refresh`'s (its 852 searches: 51
    ms of the traverses' 253): `actor_firing_position.c` and
    `action_flee.c` call `path_state_find` too.
  - The smoothing 165 us a path made or cached (a30: 1158): its pill
    tests (`structure_test_pill2d`) 10.1 us a call, 13.6 a path; the
    turning points 1.2 us a call, 3.5 a path; the surfaces 0.
  - Vehicles (18 windows with vehicles): 487 us a simulated vehicle (a30:
    400), up to 0.73 ms a tick. Most of it the old physics
    (`physics_update_old`): 364 us a call, 2,167 calls. The new (310
    calls): the forces 150 us a call (5.5 mass points), the movement 105,
    the units' collisions 5.
  - The AI (5 actors a tick on median, up to 10): actors 0.86 / 0.73 ms a
    tick; the heaviest 2.01 ms a tick (8.3 actors).
  - The RSX by pass: the specular pass 4.80 / 2.45 ms a frame (a30:
    0.70), 7.2 to 9.0 in the windows of 46.9 to 56.2 frames per second,
    with the lights 1.6 to 6.7; the lightmaps 1.70 / 1.35, the diffuse
    1.00 / 0.95.
  - The slowest window (37.5): a tick 9.5 ms at 0.80 a frame (objects
    5.10: node matrices 1.53, units 1.64; the AI 1.53 for 10 actors); the
    game thread's render 18.4 ms a frame (effects 5.1: weather 2.30,
    transparent 2.04; objects 3.5: models 2.72; its Direct3D draws 3.57);
    the RSX 17.7. A window of 51.7: the particles 2.05 ms a frame (392
    particles, 388 with physics: 1.71).
- 2026-10-19: Steps 1 to 6 (Matt: "Go for it! Also, maybe we can delegate
  the specular pass to one SPU.").
  - 1, the kept edges taken out (`path.c`): a search builds a surface's
    edges again (`build_path_edges_for_surface`, timed as before); 270 KB
    back. `[time] ai path search` gives the surfaces only.
  - 2, the checkpoint's copy (`game_state_xbox.c`, `ps3_vmx.c`): the copy
    on a 128-byte line in its block; the unused middle checked and the
    used parts copied with `ps3_vmx_zeroes` and `ps3_vmx_copy` (each
    source line touched 1 KB ahead; each destination line zeroed in the
    cache by `dcbz`, not read, then written whole with VMX, when the first
    call finds that `dcbz` zeroes exactly the 128-byte line, else plain
    stores); a revert with `ps3_vmx_copy` and `ps3_vmx_zero`. The log
    line gives the copy's ms and the check's. PC check (gcc, the section
    of `ps3_vmx.c` with stand-ins: `vec_ld` and `vec_st` drop an
    address's low 4 bits, `dcbz` zeroes its block): 4,000 rounds each with
    a `dcbz` of 128, 32 and 64 bytes (sizes to 3 MB, any alignment, guard
    bytes around): 0 differ; faults (the head to 64 bytes, no source
    alignment test, the zero test's head or last check dropped, `dcbz`
    used whatever it zeroes) each differ.
  - 3, the specular part (`render.c`) split in seven: `lights_render_specular`,
    the specular lightmaps, the reflection masks, the mirrors, the
    reflections, the transparent structure, the fog; each a split on the
    game thread and the RSX, with the RSX's draws (`ps3_gpu.c` counts the
    draws between splits). The window's specular part is their sum
    (`[time] specular part`). In one frame of eight the RSX stamps each
    draw of the part (`ps3_timers_draw_stamps`: a record for the Direct3D
    thread; report slots 512 to 991); `d3d8_rsx.c` logs the eight pixel
    shaders with the most RSX time, with their split, instructions,
    registers, combiners, texture modes and the last draw's textures.
    Timestamps a frame 32 to 48, their parts 16-bit.
  - The SPU (Matt's question): the specular part's RSX time (4.8 ms a
    frame on median on b40, up to 9.0) is the RSX's own pixel work. An
    SPU cannot write the RSX's local memory (no DMA there) and is far
    slower at pixels; it could take only the game thread's part (1.2 ms
    a frame on median), the game's own loops and Direct3D calls. Step 3
    names the split and the pixel shaders; the fix is then on the RSX
    (the program, the textures, the pixels drawn).
  - 4, the weather (`weather_particle_systems.c`: the update with its
    particles, the sprites built with their count, their draw; the
    visibility tests are the rest) and the transparent geometry
    (`rasterizer_transparent_geometry.c`: the sort with the groups, the
    groups' begin and end, each group by its shader's type, with the
    groups).
  - 5, the heap log (`ps3_heap.c`; the Makefile's `--wrap` of `malloc`,
    `calloc`, `realloc`, `memalign`, `_sbrk_r`): each growth of newlib's
    heap and each allocation of 64 KB or more, with the request and five
    return addresses, at the `[mem]` heartbeat (`[heap]` lines). A request
    holds newlib's (recursive) malloc lock around the call. The game's
    allocations reach `calloc` through `debug_malloc`, `system_malloc`
    and `GlobalAlloc` (`ppu-objdump`: 49 calls to `__wrap_malloc`;
    `GlobalAlloc` calls `__wrap_calloc`, `_malloc_r` `__wrap__sbrk_r`).
  - 6, the old physics (`physics.c`, `physics_update_old`): each mass
    point's location (`scenario_location_from_point`), ground plane
    (`compute_ground_plane`), water depth and antigravity probe
    (`collision_test_vector`), then the object's move
    (`scenario_location_from_line`, `object_translate`).
  - Comments over 2 lines shortened: `weather_particle_systems.c` (4; the
    symbol table points to upstream's copy), the Makefile (1). Warnings
    (`-Wall`) in the changed files: none in the new code. `check`: 0
    `r2off`, `.got` 0x85a8. PKG of 23:33 in `build/ps3_spu/`. Not tested
    on hardware.
- 2026-10-19: PKG of 23:33 on hardware (b40; Matt: "Done."; no fault
  reported; our listener, from the boot, after Matt freed UDP 18194 from
  the flycastPS3 session's listener): `[ps3] exit 0`. 33 timer windows
  of play, 7 under 45 frames per second (the run went further into the
  snow than the last). Medians of all / under 45, and sums of the run
  for the costs of a call:
  - The frame 17.80 / 23.70 ms (56.0 / 42.2 frames per second), game
    ticks 3.70 / 6.50 ms a frame (7.0 / 8.7 each), render 11.70 / 16.50,
    the RSX 13.50 / 17.00.
  - The checkpoints: `[vmx] dcbz zeroes 128 bytes: whole lines written
    with it`; three copies of 17,942 KB in 10.4, 11.5 and 11.9 ms (the
    last run's 38 to 40), the unused 2,537 KB checked in 0.6 to 0.9 ms;
    written by the writer in 1.7 to 2.3 s. A revert ("the saved game is
    taken") took the copy (`ps3_vmx_copy`, `ps3_vmx_zero`) and a timer
    window of play followed.
  - The specular part on the RSX, ms a frame: reflections 1.79 / 1.53
    (17.6 / 24.9 draws), lights 1.26 / 1.26 (11.9), fog 0.85 / 0.95,
    reflection masks 0.61 / 0.63, transparent 0.05 / 0.05 (up to 2.07 in a
    window), specular lightmaps and mirrors 0. On the game thread:
    lights 0.39, reflections 0.37, masks 0.22, transparent 0.20, fog
    0.19.
  - Its pixel shaders over 1,237 stamped frames (ms and draws a stamped
    frame): 61 (lights) 1.67, 13.8 draws: 96 instructions, 6 registers,
    6 combiners; a 512x512 A8R8G8B8 bump map, a 32x32 3D texture, two
    64x64 cube maps. 17 (reflections) 1.50, 16.4 draws: 66 instructions,
    5 registers, 5 combiners; a 512x512 A8R8G8B8 map, two 64x64 cube
    maps. 66 (fog) 0.60, 15.3 draws: 36 instructions for 2 combiners. 15
    (reflection masks) 0.56, 19.0 draws: 14 instructions.
  - The weather in the snow (under 45): its update 1.39 ms a frame for
    364 particles (3.8 us each; their `point_physics_update` 0.86 ms for
    365 calls, its rays 0.54), visibility 0.45, sprites 0.33 (113, 2.9 us
    each), their draw 0 (queued: the transparent geometry draws them).
  - The transparent geometry: the sort 0.17 / 0.36 ms (19.5 / 38.4
    groups, 9 us a group), generic 0.18 / 0.31 (5.5 / 10.1 groups),
    glass 0.15 / 0.27 (4.0 / 7.6), effect 0.11 / 0.22 (6.8 / 14.2).
  - The old physics (the tank, 3,580 calls): 339 us a call; its ground
    planes 257 (18.9 mass points, 13.6 us each), the locations 33, water
    11, the move 10, the rest 28; no antigravity probes.
  - The heap: 25.0 MB free at t=50 s (after the first copy), 16.5 at
    t=200 s; the heap 66.5 to 75.4 MB, in use 65.2 to 65.6. Its large
    allocations in play (`ppu-objdump`'s function starts name the
    addresses): the sound streams' packet buffers, 64 KB each, 10 to 25
    a second (`ps3_spu_buffer_allocate` from `stream_process`,
    `dsound_ps3.c`: a `memalign` at each packet, freed once mixed); the
    pixel shader translator's temporary buffers at each new shader (68
    KB in `rsx_fragment_program_translate`, 45 KB in
    `rsx_fragment_program_instructions`). Of the growths logged: the
    streams' buffers 2.7 MB, the translator's 0.8 MB.
- 2026-10-20: Steps 1 to 6 (Matt: "Go with steps 1-6."). Matt committed
  the state before as `0966e23c` ("Further optimization").
  - 1, the pixel shader translation (`ps3_rsx_fp.c`). The hardware log's
    "instructions" were 16-byte slots: an instruction, or the inline
    constant after one (61: 60 instructions and 36 constants). The
    game's three keys, from `rasterizer_xbox_environment.c` (the specular
    light, the bumped reflection) and `rasterizer_xbox_environment_fog.c`,
    translated on the PC: the clamps to [-1, 1] of each combiner write
    (a `MAX` and a `MIN` each), chains of steps with numbers ((2z - 1) *
    2 * 4), and `MUL`s added after. Now: a write's clamp is one
    instruction for the optimizer (`_op_clamp_signed`), at the write and
    not in place, so the value before it stays; each stage has its own
    ab, cd and sum until the registers are packed; a reader the clamp
    does not change reads the value before it (`clamp_through`: a
    saturated `MOV`; a saturated `ADD` with an operand 0..1; a saturated
    `MIN` or `MAX` with -1..1; `MAX(t, 0..1)` as `MAX_SAT`); `combine`:
    `t + t` as `t * 2`, steps of numbers on one value as one `MAD`, `(x +
    1) * b` as `MAD(x, b, b)`, an `ADD` of a product as a `MAD`; the
    clamps left become a `MAX` and a `MIN` (`expand_clamps`). The three
    keys: 61 (lights) 60 instructions and 36 constants (384 words) to 39
    and 19 (232), 6 to 7 registers; 17 (reflections) 44 and 16 (240) to
    37 and 10 (188), 4 registers; 66 (fog) 22 and 14 (144) to 17 and 12
    (116), 4. Random keys: 27 instructions to 26 on average, 3.08 to
    3.21 registers. Checks: `tools/ps3_rsx_shader_check.c` (cgcomp's
    encoding, the NV2A's results) 0 failures with 3,000 keys on seeds 1
    and 7 and 5,000 on 3, 11 and 23, and the game's keys; the translation
    before (git's) against now, 10,000 keys of 50 inputs each: 0 results
    apart by over 1e-4 (the largest 1.4e-6), 0 kills apart; faults (an
    unsaturated `ADD` read through, a step's addend dropped, `(x - 1) * b`
    as `(x + 1) * b`, a fused product's negate dropped, the expanded clamp
    at 0) each fail. (A `MAX` read through with an operand down to -1 is
    not caught: the translation's `MAX`es take 0.)
  - 2, the sound streams' packet buffers (`dsound_ps3.c`): a packet
    takes a kept buffer of its size to twice that (`samples_take`), else
    allocates one; the completed or retired buffers go back to a pool of
    16 (`samples_give_back`), else are freed. Under the mixer's lock.
  - 3, the translator's buffers fixed (`ps3_rsx_fp.c`): the instructions
    (68 KB), the optimizer (45 KB) and the allocator's memory (77 KB) are
    static (one translation at a time: the thread that runs the Direct3D
    records).
  - 4, the weather (`weather_particle_systems.c`, `point_physics.c`):
    each particle's acceleration and `point_physics_update` timed in the
    update; the rays that test no surface (the weather's
    `_point_physics_force_no_collisions_bit`) apart in `[time] point
    physics`.
  - 5, the ground planes (`physics.c` `compute_ground_plane`, the new
    physics' and the old's): the features in the sphere with the
    features found, and the point test (`[time] ground planes`).
  - 6, the transparent sort (`rasterizer_transparent_geometry.c`): the
    qsort and the port's `rasterizer_transparent_geometry_order_models`.
  - The shader log gives slots, not instructions. Comments over 2 lines
    shortened: `ps3_rsx_fp.c` (1). Warnings (`-Wall`) in the changed
    files: none in the new code. `check`: 0 `r2off`, `.got` 0x85d8. PKG
    of 00:37 in `build/ps3_spu/`. Not tested on hardware.
- 2026-10-20: PKG of 00:37 on hardware (b40; no fault reported). The
  flycastPS3 session's listener held UDP 18194 (our listener could not
  start; the permission system refused to stop another session's
  process), so its log took the run (`ps3log_g17_stageD_run1.txt`, lines
  18400 to 21139; Matt: "Check that log."). `[ps3] exit 0`. 33 timer
  windows of play, 5 under 45 frames per second. Medians of all / under
  45, and sums of the run for the costs of a call:
  - The frame 16.90 / 23.60 ms (59.0 / 42.5 frames per second; the last
    run 56.0 / 42.2 on another path), game ticks 3.20 / 6.20 ms a frame
    (6.4 / 8.8 each), render 9.20 / 16.70, the RSX 11.50 / 15.90.
  - The checkpoints: 10.5, 11.4 and 11.2 ms; written in 1.6 to 2.1 s. A
    revert took the copy; the BSP swap and the main loop went on.
  - The specular part on the RSX: 4.40 / 4.20 ms a frame (5.60 / 4.50
    before): reflections 1.54 / 1.68, lights 0.94 / 0.18, fog 0.78 /
    0.95, reflection masks 0.61 / 0.82. Its pixel shaders (ms and draws
    a stamped frame; slots, registers): 17 (reflections) 1.34, 16.1; 50,
    4 (1.50, 16.4 before). 61 (lights) 1.31, 12.0; 58, 7 (1.67, 13.8).
    66 (fog) 0.48, 13.4; 29, 4 (0.60, 15.3). 15 (reflection masks, not
    changed) 0.64, 18.8; 10, 2 (0.56, 19.0). A draw of 61 9% cheaper, of
    17 9%, of 66 8%, of the unchanged 15 16% dearer: the scenes differ.
  - The heap: 24.8 MB free at t=50 s (after the first copy), 19.3 at
    t=100 s and to the end (t=200 s); the heap 66.7 to 72.2 MB by t=100,
    then 72.1 to 72.3. The sound streams still take a new 64 KB buffer
    about once a second (136 logged from t=45 s: up to 50 streams play,
    more packets than the pool's 16).
  - The weather in the snow (under 45): its update 1.27 ms a frame for
    364 particles: the acceleration 0.02, `point_physics_update` 0.84
    (of it the rays without surfaces 0.50: 364 calls, 1.4 us each), the
    rest 0.41.
  - The transparent sort (under 45): 0.33 ms a frame, of it the qsort
    0.03 and the port's `rasterizer_transparent_geometry_order_models`
    0.29.
  - The ground planes (`compute_ground_plane`, 66,824 calls): the
    features in the sphere 13.4 us a call, 0.06 features found a call;
    the point tests (1,592) about 0. The old physics: 341 us a call, its
    ground planes 253 (19 mass points).
  - The game thread's render under 45: objects 2.7, lights 2.3,
    diffuse 2.1, specular 2.0, effects 3.9 (weather 2.0, transparent
    1.4); its Direct3D calls 3.0 of the 16.7.
- 2026-10-20: The 5 steps (Matt: "Ok, do those 5 steps.").
  - 1, the weather (`point_physics.c`): a ray that tests no surface
    (`_point_physics_force_no_collisions_bit`, the weather's) moves the
    point by the sum `collision_test_vector` made (`add_vectors3d`),
    without the call; its look-up of a location set only the weather's
    shared one, which is set again before it is read. PC check
    (`weathercheck.c`): 20,000,000 moves of random floats (signs, 1e-30
    to 1e30, denormals, zeros, infinities), the call's path against the
    sum: 0 apart.
  - 2, the transparent sort's model order
    (`rasterizer_xbox_transparent_geometry.c`): each model part's local
    vertices boxed by node (the nodes a vertex's weight puts on it), the
    exact path's weight and node checks, and 14 sample vertices (the
    furthest along the axes and diagonals), kept for the map: 64 sets of
    2 parts, 16 nodes a part (more: the exact path); forgotten at a
    map's load and end (`rasterizer_common.c`). A frame's bounds: the
    boxes moved by their node matrices, widened by a thousandth of
    their size and 0.001 (the rounding of both paths, the weights'
    1e-5). A surface's side is taken from them only past its band (the
    exact bounds lie inside them, and rounding keeps the order); a
    plane that the sample vertices, skinned as the draw skins them,
    pass on both sides past the band crosses the exact bounds too; else
    the unit's exact bounds as before (each vertex skinned). PC check
    (`ordercheck`: HEAD's code and the tree's, taken from the sources):
    400,000 random scenes (planes; models of 1 to 64 nodes, compressed
    and uncompressed vertices, chains of parts; bad vertices, NaN and
    far matrices, far scenes; map changes): 0 orders and 0 units'
    validity or gaps apart; the parts bounded exactly 3.68 M before,
    now 3.67 M from kept boxes and 0.80 M exactly (the scenes put half
    the objects within 4 units of a plane). Faults, each fails (scenes
    apart of 20,000): no reach (5,939), no band (3,953), a crossing on
    one side (3,954), the crossing's band at +epsilon (3), half the
    boxes (1,385), one node of two weights (149), valid with a bad
    vertex (8,144), no margin (2), no range check of a box (1,580), no
    forget at a map change (1,019).
  - 3, the ground planes' sphere query (`collisions.c`,
    `collision_usage.h`; `physics.c` marks `compute_ground_plane`'s
    query): the bsp's leaves with the leaves a call, the structure's
    features, the leaves' clusters' objects (the clusters, the objects
    walked), their tests: `[time] ground planes' features in the
    sphere`.
  - 4, the sound streams' pool (`dsound_ps3.c`): 16 to 64 buffers.
  - 5, the render (`render.c`, `models.c`, `render_objects.c`): the
    lights pass's parts (the lens flares' occlusion tests, the objects'
    shadows, the lights' diffuse part, the light and alpha-tested
    decals), the diffuse pass's (the structure's diffuse textures, the
    decals), `render_model`'s but the shadows' (the node matrices with
    the nodes, the begin, the parts' draws with the models, the end),
    the objects' widgets; each split's and part's share of the game
    thread's Direct3D draws and constants (`ps3_render_d3d`). New lines:
    `[time] render_model but the shadows`, `[time] lights and diffuse
    passes`, `[time] specular part's Direct3D draws and constants`.
  - Comments over 2 lines shortened:
    `rasterizer_xbox_transparent_geometry.c` (2),
    `rasterizer_transparent_geometry.h` (1), `rasterizer_common.c` (1),
    `render_objects.c` (1), `models.c` (6). Warnings (`-Wall`) in the
    changed files: none in the new code. `check`: 0 `r2off`, `.got`
    0x8608. PKG of 01:54 in `build/ps3_spu/`. Not tested on hardware.
- 2026-10-22: PKG of 01:54 on hardware (b40; our listener). The game
  thread crashed at about t=33 s (frame 1448, 4 s after the 1200-frame
  window): from t=40 s the heartbeat shows 1444 frames, main loop pass
  1446 at step 523 (`render_player_frame`), and at the first stall
  `[ps3] main thread: priority -1 (0x80010005)` (ESRCH: lv2 ended the
  thread). No `[host] ... blocked` line; the Direct3D thread had finished
  frame 1448; the display thread and the sound went on. Matt left by the
  XMB (`[ps3] no shutdown 5 s after the XMB exit: ending the process`).
  The log has no address of the fault. The new code of the 01:54 tree in
  that path: the transparent sort's kept boxes, the render timers
  (`render.c`, `models.c`, `render_objects.c`). The kept boxes' loops are
  bounded and their node indices checked: no cause found by reading.
- 2026-10-22: Finer marks of the main loop's place (Matt: "Do 1"):
  `render.c`'s `PS3_MAIN_STEP` defined before `render_window`;
  `render_player_frame` 580 (the camera's cluster), 581 (the fog), 582
  (the frustums), 583 (the mirror's search), 584 (the mirror's window),
  585 (the player's window); `render_window` 600 + n in the primary
  target's window, 700 + n in the mirror's (`PS3_WINDOW_STEP`): 0 the
  begin, 1 visibility, 2 the screen flash, 3 `rasterizer_window_begin`, 4
  the sky, 5 the first person weapon, 6 `lights_preprocess_scene`, 7
  `render_objects`, 8 `structure_render_preprocess`, 9 the lightmaps, 10
  the lens flares' occlusion tests, 11 the objects' shadows, 12 the
  lights' diffuse part, 13 the light and alpha-tested decals, 14 the
  structure's diffuse textures, 15 the primary and secondary decals, 16
  to 22 the specular part (lights, specular lightmaps, reflection masks,
  mirrors, reflections, transparent structure, fog), 23
  `game_engine_post_rasterize_objects`, 24 the weather, 25 the particles,
  26 the contrails, 27 `rasterizer_transparent_geometry_draw(TRUE)`, 28
  the water decals, 29 the detail objects, 30
  `rasterizer_transparent_geometry_draw(FALSE)`, 31
  `rasterizer_transparent_geometry_stop`, 32 the screen fog, lens flares
  and antialiasing, 33 `interface_draw_screen`, 34 the screen flash, 35
  the UI widgets, 36 bink and the debug draws, 37
  `rasterizer_window_end`. `check`: 0 `r2off`, `.got` 0x8608. PKG of
  10:57 in `build/ps3_spu/`. Not tested on hardware.
- 2026-10-22: PKG of 10:57 on hardware (b40; our listener; Matt: "It
  crashed again!"). The same crash, earlier: from t=30 s the heartbeat
  shows 1163 frames, main loop pass 1165 at step 627
  (`rasterizer_transparent_geometry_draw(TRUE)` in the primary window:
  the transparent sort with the models' order, then the draws); at the
  first stall `[ps3] main thread: priority -1 (0x80010005)`. The 01:54
  tree changed only the models' order there (the kept boxes, their
  samples, the exact path as pass 1) and its timers. Trap 23 (a
  misaligned float access ends only that thread) fits the signs; the
  kept boxes' static array is at 0x10931620 (aligned, 472 bytes an
  entry). The cause is not found yet.
- 2026-10-22: The kept boxes taken out (Matt: "Go with option 2."): the
  gain at best about 0.25 ms a frame (the models' order 0.29 ms a frame
  under 45 frames per second in the 00:37 run; the PC check's exact
  bounds 78% fewer), not worth a debug round.
  `rasterizer_xbox_transparent_geometry.c`, `rasterizer_common.c` and
  `rasterizer_transparent_geometry.h` back to `0966e23c` (the 00:37
  build's code), but their shortened comments; the 01:54 code is in this
  session's scratchpad only. The ELF has no `transparent_model_boxes`
  symbols. `check`: 0 `r2off`, `.got` 0x8608. PKG of 11:16 in
  `build/ps3_spu/`. Not tested on hardware.
- 2026-10-22: PKG of 11:16 on hardware (b40; our listener; Matt: "Done";
  no fault reported): `[ps3] exit 0`, no crash. 35 timer windows of play
  (the menu's left out), 3 under 45 frames per second: the load (34.1),
  the snow's start (39.9), the last with a revert (43.9); the heavy play
  of this run is at 45 to 53. Medians of all / under 45 (a window: the
  lines from `[time] N frames` to the next), and sums of the run for the
  costs of a call:
  - The frame 17.90 / 25.10 ms (55.9 / 39.9 frames per second; 00:37
    59.0 / 42.5 on another path), game ticks 3.90 / 2.80 ms a frame
    (7.1 / 5.6 each; more combat than 00:37's 3.20), render 11.10 /
    11.70, the RSX 12.00 / 14.60 (specular 5.30 / 4.80, its lights 1.89
    for 19.2 draws). Checkpoints 10.5, 11.8, 11.4 ms; written in 1.7 to
    3.1 s; two reverts took the copy.
  - The weather in the snow (6 windows of 364 particles): its update
    0.86 ms a frame (00:37: 1.27): acceleration 0.02, point physics 0.39
    (0.84), of it the rays without surfaces 0.02 for 364 (0.50), the
    rest 0.45 (0.41).
  - The transparent sort (exact again): 0.17 / 0.10 ms a frame, the
    models' order 0.16 / 0.09.
  - The ground planes' sphere query (70,148 calls): 13.3 us a call
    (00:37: 13.4): the bsp's leaves 4.5 (1.8 leaves), the leaves'
    clusters' objects 7.7 (1.0 cluster, 15.7 objects walked; in it their
    tests 1.8 for 1.6 objects), the structure 0.05, the rest 1.1. The old
    physics (3,621 calls) 336 us a call, its ground planes 252 (18.9 mass
    points).
  - `render_model` but the shadows: 13.2 models a frame, 84 us a model:
    the parts 75.9 (of it the Direct3D draws and constants 19.0), the
    begin 4.3, the node matrices 3.5 (10.9 nodes). Medians: the parts
    0.93 / 0.53 ms a frame.
  - The lights pass (ms a frame; its Direct3D): the objects' shadows 1.05
    / 0.36 (0.24), the lights' diffuse part 0.36 / 0.23 (0.10), the lens
    flares 0.06; the diffuse pass: the structure 0.34 / 0.37 (0.16), the
    decals 0.41 / 0.24 (0.11).
  - The specular part's Direct3D draws and constants: lights 0.14,
    reflection masks 0.14, reflections 0.14, fog 0.13, transparent 0.05
    (about 0.6 ms a frame of the game thread's 1.6).
  - The heap: the streams' new 64 KB buffers from t=45 s 59 (00:37: 136,
    the pool of 16), but their 32 KB buffers (under the log's 64 KB) grew
    the heap 111 times (2.9 MB) from t=45 s; the arena 67.5 MB at t=70 s
    to 73.1, in use flat (65.8 to 66.9): fragments of newlib's heap. Main
    memory free 22.3 MB at t=100 s, the lowest 17.8 at t=160, 19.3 at the
    end (00:37: 19.3 from t=100).
- 2026-10-22: The 4 steps (Matt: "Do all 4.").
  - 1, the stream packets' buffers (`dsound_ps3.c`): sizes of 16, 32 and
    64 KB (a packet: up to 8,192 frames of 2 channels; the game's packets
    are 2,304 or 8,192 bytes, times 2 for 44 kHz and for stereo), cut
    from blocks of their own, a first of 4 MB, then 1 MB more when those
    are used up (the last block's rest goes to the smaller sizes), never
    freed; a buffer given back goes on its size's free list (its first
    bytes link the next); over 64 KB (none of the game's) the heap's.
    `DirectSound:` adds `packet buffers N KB (peak P) of B KB in K
    blocks`. The pool of 64 is gone.
  - 2, the ground planes' sphere query (`collisions.c`,
    `object_bounds_cache.c`): the clusters' objects from
    `object_bounds_cache_first_sphere`/`next_sphere`: the objects
    `object_bounds_cache_out_of_reach` passes, in the list's order; a
    cluster's second ask in a generation (the lists' and the copies'
    changes; a vehicle's mass points ask in a row) reads the rays' kept
    block (made then: one ask alone walks the list, which costs less);
    the mark only for those (an object out of reach stays out of reach in
    the query, so the same objects are tested in the same order).
    `[time] ground planes' features in the sphere` adds the objects read
    from kept blocks. PC check (`spherecheck.c`: the file with stand-ins
    for the game's headers; host gcc 13, `-ffp-contract=off`, also
    ASan/UBSan): 717,994 queries (random clusters of up to 512 objects,
    shared and repeated objects, copies with edge values and unknown
    ones, writes, invalidations, list and index changes, blocks over
    4,096 entries; the rays' iterator too) against the old loop: 0 apart.
    Faults, each fails: no `known` test (67,213 of 95,134 apart), the
    reach without the query's radius (6,838), the list's walk without the
    reach (13,720), no write count at an update (5,835), one entry past a
    block (33,339).
  - 3, `render_model_parts` (`models.c`) timed inside, but the shadows':
    the solid draws of model shaders and of environment shaders, the
    decals' draws, the transparent parts' centroids and submits (each
    with its count and the game thread's Direct3D in it), the loops
    (`[time] render_model's parts' draws`).
  - 4, the objects' shadows (`render_objects.c`, `structure_render.c`):
    the pass's begin and end, the objects' checks and lighting, each
    shadow's begin, model, volume, structure (the surfaces found with
    their count, their triangles built, their draws) and end, each with
    its Direct3D (`[time] objects' shadows`).
  - Comments over 2 lines shortened: `structure_render.c` (2; the symbol
    table points to upstream's copy). Warnings (`-Wall`) in the changed
    files: none in the new code. `check`: 0 `r2off`, `.got` 0x8618. PKG
    of 11:54 in `build/ps3_spu/`. Not tested on hardware.
- 2026-10-22: PKG of 11:54 on hardware (b40; our listener; Matt: "Done.";
  no fault reported): `[ps3] exit 0`, 0 silent or partial port blocks.
  35 timer windows of play, 4 under 45 frames per second (the load 33.9,
  a revert 42.1, combat 40.6, the last 35.0). Medians of all / under 45,
  and sums of the run for the costs of a call:
  - The frame 16.90 / 26.65 ms (59.0 / 37.8 frames per second), game
    ticks 3.60 / 4.40 ms a frame, render 9.70 / 11.90, the RSX 11.30 /
    13.25; the Direct3D thread busy 15.0 (its flip waits 8.9), the game
    thread's waits for it 3.10 / 4.15. Checkpoints 10.7 to 12.0 ms.
  - 1, the stream buffers: in use up to 3,040 KB, 5,120 KB in 2 blocks;
    the heap's arena and use equal (70.0 / 70.0 MB at the end; before
    73.1 / 66.7); main memory free 22.3 MB at the end, the lowest 21.8
    (before 19.3 and 17.8). The heap's other growths from t=45 s: 104,
    about 4 KB each (the Direct3D thread's).
  - 2, the ground planes' sphere query (72,261 calls): 10.3 us a call
    (before 13.3); its objects part 4.1 (before 7.7): 20.0 objects read
    from kept blocks and 2.4 walked a call (before 15.7 walked). The old
    physics 277 us a call (before 336), its ground planes 191 (252).
  - 3, `render_model_parts`: 14.7 models a frame, 68.7 us a model; a
    model shader's draw 21.7 us (its Direct3D 5.8; 27.8 a frame), an
    environment shader's 15.7 (5.6; 8.3), a transparent submit 15.4
    (4.3; 12.8), the loops 4.6 us a model. Medians: model shaders 0.55 /
    0.83 ms a frame.
  - 4, the objects' shadows: 0.77 / 0.91 ms a frame, 3.2 shadows a
    frame (11.0 objects checked); a shadow 281 us: its model 78 (Direct3D
    29), the structure 203: the surfaces found 103 (184 surfaces), their
    triangles 24, their draws 75 (Direct3D 41).
  - The effects' particles: `particles_update` 0.50 / 0.46 ms a frame,
    118 particles with physics, 3.9 us each. The weather under 45: 0.57
    ms a frame (266 particles), visibility 0.35, sprites 0.29.
  - The SPU (Matt: "check what we can place to the SPU"). Not the
    simulation (ticks: AI, objects, physics): the SPU's single precision
    rounds toward zero and flushes denormals, so its results differ from
    the PPE's and the PC builds' (the machines must compute the same).
    Candidates, exact or render-only, on compact data: (1) the
    checkpoint's copy of 17.9 MB (bytes: exact) by SPU DMA, the game
    thread's 10.7 to 12.0 ms hold a checkpoint to an estimated 2 to 3;
    (2) the weather (update, visibility, sprites; 1.2 ms a frame in the
    snow); (3) the effects' particles (0.5 ms a frame; their rays need
    the structure by DMA); (4) the shadows' surface search (0.33 ms a
    frame; a bsp walk by DMA); (5) on the Direct3D thread the fragment
    programs' constants (`colours`, 1.2 ms a frame; that thread waits on
    flips most of the frame). Not a fit: `render_model_parts`' setup and
    the transparent sort (short, branchy, in the Direct3D calls' order).
- 2026-10-22: The SPU offload (Matt: "It's time to work on some SPU
  offload."; he chose all four candidates). A third SPU thread group,
  `spu/spu_game.c` (`spu_game.h`, `include/ps3_spu_game.h`), started
  before the game: a ring of 64 jobs, a ticket each, the game thread's
  wait a short spin then sleeps; a job not done in 100 ms stops the SPU,
  and each caller then does its job on the PPE. The BSP's data through a
  64 KB cache of 128-byte lines (2 ways), kept while
  `scenario_structure_bsp_generation` (a new counter, `scenario.c`) is the
  same. A job past the SPU's room is refused (the PPE does it).
  - 1, the checkpoint (`game_state_xbox.c`): the unused middle's zero test
    and the two used parts' copies as three jobs (16 KB DMAs in four
    buffers, each get and put fenced in its buffer's tag); a revert's
    copies and zeroes the same. The log line says `copied by an SPU`.
  - 4, the shadows (`render_objects.c`, `structure_render.c`,
    `structure_visibility.c`): the shadow's volume computed when it
    begins, its surfaces' search started then (the clusters in the sphere
    found on the PPE; the subclusters, or the nodes from the root, the
    SPU's: `traverse_node`'s recursion on a stack of its own, in the same
    order) and taken in `structure_render_shadow` after the shadow's model
    draws. One shadow in 64 is searched by the PPE too, as a check
    (`[time] game SPU`: checked, apart).
  - 2, the weather (`weather_particle_systems.c`, `render.c`,
    `render_sprite.c`, `wind.c`, `random_math.c`, `point_physics.c`): at
    the window's start (after `build_sprite_prepare_for_window`) the
    system, its time and the particles' counts on the PPE, the sprite
    groups made for every bitmap of a type's sprites (a table of each
    bitmap group's sprites, kept for the map), then one job: the particles'
    update (its own random seed, the random directions' table by DMA, the
    wind), visibility, polyhedra and sprites into the groups' vertices;
    `weather_particle_systems_render` draws them (`build_sprites_end`).
    The big sprites' count goes on from the window's start. The update and
    render were split into functions for this, and for the PPE's fallback.
  - 3, the particles (`particles.c`, `point_physics.c`, `collisions.c`,
    `game.c`): `point_physics_update` in two parts (forces, rays; the same
    statements). `particles_update` does a free particle's forces and
    gathers it; the job makes the rays (`collision_test_vector`'s structure
    and water tests: the bsp3d traversal on a stack of its own, the leaf,
    bsp2d and surface tests, the clusters' water and the materials' types
    from tables made once a BSP); `particles_update_end` at the end of
    `game_frame` (after widgets, sound, scenario) takes them and makes the
    contacts (effects, deaths, rest). New particles of those contacts wait
    for the next frame.
  - PC check (scratchpad; flycastPS3's `spusim.c`, the image with its
    symbols; the host in the SPU's rounding, toward zero with no
    denormals; spu-gcc converts float constants toward zero too, so the
    transcriptions use those constants): copies, zero tests and zeroes
    (180 jobs, sizes to 3 MB, guard bytes); the weather (300 windows,
    7,821 sprites, random tables, boxes, polyhedra, vertex buffers at
    every alignment): particles, sprite counts and vertices 0 apart; on
    the 81 BSPs of the 23 Xbox maps (swapped as the PS3 swaps them),
    32,400 shadow searches (nodes and clusters, random volumes, planes and
    flags, small rooms) 0 apart, and 81,000 particles (anywhere with long
    rays, and in effects' spots; 7,226 collided) 0 apart. Faults,
    each fails: the traversal's children swapped, a ray's far side first,
    the polyhedra untested, a copy 16 bytes short, the water plane's sign,
    a leaf's last reference dropped. The simulator's times (3.2 GHz,
    500-cycle DMA): a shadow 46 us (the PPE 103), a weather window 171 us,
    a particle 2.6 us in effects' spots (warm cache), 4.2 anywhere.
  - Comments over 2 lines shortened in the changed files (the symbol
    tables of `random_math.c`, `render_sprite.c`, `scenario.c` and
    `structure_visibility.c` point to upstream's copy). Warnings (`-Wall`)
    in the changed files: none in the new code. `check`: 0 `r2off`, `.got`
    0x8778. PKG of 14:12 in `build/ps3_spe/`. Not tested on hardware.
- 2026-10-22: PKG of 14:12 on hardware (b40; our listener; Matt: "Done.";
  no fault reported): `[spu] game up`, `[ps3] exit 0`, `[spu] game down`;
  no `SPU off`, 0 jobs refused. 24 timer windows, 22 of play, none under
  45 frames per second but the load. Medians of the play windows:
  - The frame 16.7 ms, game ticks 2.85, render 7.85, the RSX 10.6.
  - 4, the shadows work: the game thread's surfaces found 6 us a shadow
    (before 103), 2.8 shadows a frame; the SPU 36.5 us a job (DMA 1.9),
    the game thread's waits 0.006 ms a frame. 358 shadows checked against
    the PPE's search: 0 apart. The cache: 26.1 M lines found, 0.13 M
    fetched.
  - 3, the particles give nothing: particles_update with its end 3.8 us a
    particle (12 windows over 50 particles, median 194), as before (3.9 to
    4.5); the SPU 1.9 us a particle, 272 us a job, and the game thread
    waits for it 0.12 ms a frame (0.81 at 426 particles): the rest of
    `game_frame` is too short to hide it.
  - 1, the checkpoint was copied by the PPE both times (12.9 and 15.1 ms):
    the used parts' sizes are 4-byte multiples (`game_state_malloc`), so
    the 16-byte test sent it to the PPE; the revert too.
  - 2, the weather was not tested: no weather system ran (`[time]
    weather` 0 particles in every window).
  - Next, for Matt's choice: (1) the checkpoint: the 16-byte body of each
    part on the SPU, the few edge bytes on the PPE, the GPU part put in the
    copy at its source's alignment; (2) the particles: (a) jobs of 64
    particles sent while `particles_update` gathers, so the SPU works
    during the gather (the same order of events), or (b) the rays taken at
    the next frame's start (no wait; the particles move one frame late),
    or (c) back to the PPE; (3) a run in b40's snow for the weather.
- 2026-10-22: Steps 1 and 2a (Matt: "Do 1 and 2 then. For 2, pick a.").
  - 1, the checkpoint (`game_state_xbox.c`): the state's allocations are
    4-byte multiples, so each part's 16-byte lines go to the SPU and the
    bytes at their edges (under 16: the CPU part's last, the middle's first
    and last, the GPU part's first) are the PPE's, after the SPU's jobs;
    the copy keeps its GPU part at `gpu_offset` (after the CPU part's
    lines, at its source's alignment: DMA wants two addresses alike in
    their low 4 bits); the writer and the revert read it there. A revert
    logs `checkpoint: its copy put back by an SPU` (or `the PPE`) with its
    ms. PC check (the file's own functions with an SPU stand-in that fails
    on DMA rules, ASan/UBSan): 20,000 rounds of random 4-byte sizes, a
    stray byte in the middle (at its edges or anywhere), guard bytes
    around the copy and the state: 0 failed. Faults, each fails: the CPU
    part's last bytes dropped (15,069), the middle's last bytes unchecked
    (984), the GPU part's offset without its alignment (14,911), a
    revert's middle head not zeroed (4,951), a revert's GPU head dropped
    (5,033).
  - 2a, the particles (`particles.c`): a job each 64 particles gathered,
    sent at once (the SPU makes their rays while `particles_update`
    gathers the next), the rest at the loop's end; `particles_update_end`
    waits for the last (the jobs end in order) and takes each particle by
    its job's answer (one the SPU did not end, or refused, is done on the
    PPE). Up to 16 jobs a frame.
  - `check`: 0 `r2off`, `.got` 0x8778. Warnings (`-Wall`) in the changed
    files: none in the new code. PKG of 14:33 in `build/ps3_spe/`. Not
    tested on hardware.
- 2026-10-22: The 14:33 PKG on hardware (b40, from its start into the
  snow; Matt: "Done"; no fault reported). `[ps3] exit 0`, `[spu] game
  down` at the end, no `SPU off`, 0 jobs refused.
  - Medians of 58 play windows (all / under 45 fps, 3 windows): the frame
    16.9 / 22.9 ms, game ticks 3.3 / 6.8, render 9.45 / 15.6, the RSX
    11.15 / 14.4. The snow (10 windows): 51.3 fps, the frame 19.5 ms,
    ticks 5.7, render 12.2, the RSX 11.95.
  - 1, the checkpoint: 7 saves, each "17942 KB copied by an SPU in 3.1
    ms" (the CPU part 17782 KB, the GPU part 160 KB, the middle's 2537 KB
    checked to be zeroes in the same 3.1 ms); before 12.9 to 15.1 ms on
    the PPE (10.4 to 11.9 in earlier runs). The SPU 1027 us a copy job.
    The writer 1.7 to 5.2 s, as before. No revert ran (no death after a
    checkpoint): `put back by` is not in the log.
  - 2a, the particles (32 windows over 50 particles, median 158):
    `particles_update` with its end 2.9 us a particle (before 3.8; 3.9 to
    4.5 on the PPE); 2.8 jobs a frame, the SPU 118 us a job (2.0 us a
    particle); the wait at `game_frame`'s end 0.03 ms a frame (before
    0.12; 0.22 at most, at 332 particles).
  - 2, the weather (b40's snow, 364 particles; first run on hardware):
    one job a window, the SPU 526 us a job (DMA 16.7 us), the game
    thread's wait 0.000 ms a frame. The game thread's weather 0.05 ms a
    frame (its job's set-up and the draws); before 1.2 to 1.5 (update
    0.86, visibility 0.35, sprites 0.29).
  - 4, the shadows (46 windows, 3.6 shadows a frame): the surfaces found
    4.7 us a shadow on the game thread (the PPE 103); the SPU 38 us a job;
    823 checks against the PPE, 0 apart.
  - Next, for Matt's choice: (1) a run with a death after a checkpoint
    (the revert by the SPU is not yet run on hardware); (2) candidate 5,
    the Direct3D thread's fragment-program constants (1.2 ms a frame);
    (3) the particles' rays taken at the next frame's start (no wait;
    the particles move one frame late).
- 2026-10-22: Matt chose steps 2 and 3 ("Work on these 2"); he tests the
  death in the next run. Matt asked whether the 14:33 run felt choppier:
  its log does not show it (the median 59.05 fps as 11:54's 59.0; the
  slowest tenth of windows 48.0 against 42.1); the snow was slower with
  more combat (10.3 actors a tick against 8.5; the ticks 5.7 against 4.3
  ms a frame) while its effects fell 2.8 to 1.7 ms; the SPU's waits are
  small (particles 0.03 ms a frame, at most 0.22). The windows are
  averages of 300 frames: a short stutter does not show.
  - 2, the pixel shaders' copies (`ps3_gpu.c`, `spu/spu_vertices.c`,
    `.h`, `d3d8_rsx.c`): a copy (the program's microcode with its
    constants, into the scratch) is a FRAGMENT task of the conversions'
    SPU, in the ring of the vertex tasks: the microcode by DMA (at any
    address), each constant's quadword its value's with the 16-bit halves
    swapped (`spu_rl`), put into RSX memory; the flushes already wait for
    the ring, so the RSX reads it after. A task holds 32 constants and 11
    values (the program's distinct uniforms) and 16,352 bytes; a larger
    one, or the SPU off, is the PPE's as before. The PPE still builds the
    values and compares them (`colours`). A task the SPU leaves is done
    by the fallback (`ps3_gpu_fragment_task_run`). `[gpu] frame N` counts
    the SPU's copies. PC check (the real `spu_vertices` image in
    flycastPS3's simulator, fed by `ps3_gpu.c`'s own functions, against
    the copy's definition; INDICES tasks between): 20,000 rounds (random
    programs to 2,048 slots, sources at any 4-byte address, 1 to 32
    uniforms), 17,524 copies by the SPU (5,455 cycles a task), 658 by the
    PPE, 17,524 fallbacks, 4,552 index tasks: 0 failed. Faults, each
    fails: no half swap, a 3-bit value index, an aligned source assumed,
    the last quad not put, a constant's place off by a slot, the values
    not deduplicated, one value past the task allowed.
  - 3, the particles (`particles.c`, `game.c`, `game_state.c`): the rays
    of the particles a frame gathers are taken by the next frame's
    `particles_update` first (no wait: the SPU had the render and the
    ticks between; the particles move a frame late). Rays not yet taken
    are dropped once the SPU is done with them before the BSP switches
    (`particles_disconnect_from_structure_bsp`), the map ends
    (`particles_dispose_from_old_map`) or a game state loads (a
    before-load proc): their particles keep their place a frame.
    `game_frame`'s call at its end is gone.
  - `check`: 0 `r2off`, `.got` 0x8778. No warnings in the changed files.
    PKG of 15:18 in `build/ps3_spe/`. Not tested on hardware.
- 2026-10-22: The 15:18 PKG on hardware (b40 into the snow, a death after
  the third checkpoint; Matt: "Done."; no fault reported). `[ps3] exit 0`,
  no `SPU off`, 0 jobs refused.
  - Medians of 35 play windows (all / under 45 fps, 2 windows: a load and
    the fight before the death): 59.6 fps; the frame 16.8 / 26.0 ms, game
    ticks 3.2 / 3.95, render 9.0 / 10.5, the RSX 10.5 / 13.8; the
    slowest tenth of windows 47.6 fps. The snow (14 windows): 58.25 fps,
    the frame 17.15 ms.
  - 2, the pixel shaders' copies: every copy by the SPU (`[gpu] frame N`:
    15 to 130 a frame, all "copied by the SPU"); the conversions' SPU 116
    to 194 us a frame (163 to 307 tasks a frame). The PPE's `colours`
    (29 and 17 spans of 600 frames, by the draws of each) 3.61 to 3.50 us
    a draw: about 0.04 ms a frame. The copy was not its cost: the values'
    build and compare stay on the PPE, 3.5 us a draw (`state` 12.2).
  - 3, the particles a frame late: their wait 0.03 (at most 0.22) to 0.00
    ms a frame; `particles_update` 2.89 to 2.57 us a particle (21 windows
    over 50 particles, median 129); the SPU 102 us a job. The shadows' jobs
    now wait behind the particles' on the SPU during the render: the
    surfaces found 4.7 to 8.3 us a shadow (the PPE 103), their wait 0.007
    to 0.019 ms a frame (at most 0.068).
  - 1, the checkpoint: 3 saves, 3.1 ms each by the SPU. The revert after
    the death: "its copy put back by an SPU in 2.8 ms", "the saved game is
    taken", the BSP swapped in 23 ms (the exit came seconds later).
  - The weather (14 windows of snow): the SPU 538 us a job, the wait 0.000
    ms a frame. The shadows' check: 566, 0 apart.
  - Next, for Matt's choice: (1) `colours` timed inside (the values'
    build, the compare, the task) to find its 3.5 us a draw; (2) the
    copies kept in RSX memory by program and values across frames (no copy
    when the same one comes again); (3) the shadows' jobs before the
    particles' (the particles' on another SPU, or a queue of their own).
- 2026-10-22: G9 steps 2 to 5 (Matt: "Work on step 2, 3, 4 and 5 together.
  Enough timers. Let's get to action."). No new timers but the SPU jobs'
  own (the `[time] game SPU` line has the new kinds and the render SPU).
  - 5, the render SPU (`ps3_spu.c`): the same `spu_game` image on a fourth
    SPU thread group (`halo_render`), a ring of its own
    (`ps3_spu_render_*`). The shadows and the weather moved there; the game
    SPU keeps the checkpoint's copies and the particles' rays. Its jobs run
    in order, so each reads what the ones before it wrote.
  - 3, VISIBILITY (`structure_visibility.c`, `structure_render.c`): after
    the portal walk `structure_visibility_compute` sends the rendered
    clusters with their frustums (`render_frustum_cube_visible`'s bounds
    and four planes); the SPU walks the subclusters (the same order, the
    cap of 32,768), puts the flags, then the surfaces ascending and their
    triangles (16 KB fetches of the surfaces block).
    `structure_render_preprocess` takes it (the PPE walks if the job did
    not end) and copies the triangles into dynamic triangles. The window's
    list is then walked once (`structure_render_build_chunks`): the passes
    on it (lightmaps, diffuse, specular lightmaps, masks, mirrors,
    reflections, transparent, fog, fog screen, the supersized flashlight)
    go through the kept chunks with the same calls in the same order.
  - 2, LIGHTS (`object_lights.c`, `structure_render.c`): after
    `lights_preprocess_scene` a job for the diffuse pass and one for the
    specular, each light's sphere, box and clusters (as
    `lights_render_diffuse` and `_specular` find them); the SPU searches
    each (the SHADOW job's walks), sorts (the found flags, in order) and
    gathers the triangles into an arena (12,288 surfaces a pass). Each
    pass takes its light's entry if the light, sphere and clusters are the
    same and the window's flags were final (else the PPE searches).
  - 4, SPRITES (`render_particles.c`, `render_sprite.c`): after the lights'
    part the gather, sort and groups as `render_particles` makes them;
    each group not first person, with a sprite table (a bitmap group's,
    kept for the map, 192 KB) and room (2,048 particles), placed on the
    PPE (node matrices, deletions as before) and its build_sprite groups
    made for the slots its particles use; the SPU does
    `render_particles`' size cut, scale and fades and build_sprite (the
    weather's builder, shared, with the mirrors). `render_particles` takes
    each group in order: the counts, the groups in their first sprites'
    order, `last_rendered_frame_index` and the shader's radius on the PPE;
    a group the SPU did not build is built on the PPE then.
  - 4, CONTRAILS (`render_contrails.c`): after the lights' part each
    normal contrail's points gathered (with their average), its buffers
    made and locked; the SPU builds the strips (render types vertical,
    horizontal, viewer; `contrail_fade` with the transition table;
    `real_argb_color_to_pixel32`'s rint exactly).
    `render_contrails_normal` draws them in order; the others by
    `render_contrail` as before.
  - PC check (scratchpad; flycastPS3's `spusim.c`, the images with their
    symbols, the new one's `.text` the PKG's; the host in the SPU's
    rounding): 700 rounds of random structures, lights, particles and
    contrails, the new jobs against transcriptions of the game's functions
    (the lights' nodes walk against the SHADOW job), and the weather,
    shadow and particles jobs of the new image against the old image's:
    16.5 M and 22.2 M checks, 0 apart. Faults, each found: a corner's test
    sign, the frustum's bounds untested, the cap, a window place off by
    one, the arena place not rounded, the flags kept between lights, a
    triangle's vertices swapped, no u mirror, the slot order after the
    vertices, the depth's row, the fade out unscaled, the last point's
    fade, rint's ties, the transition's index, the horizontal side's sign,
    the tint's channels, the weather's sprite limit.
  - Memory: 1.38 MB more `.bss` (the jobs' buffers and arenas). The SPU
    image 54.5 KB of code (before 38.7), 33 KB of LS left for its stack.
  - Comments over 2 lines shortened in the changed files (the symbol
    tables of `periodic_functions.c` and `render_particles.c` point to
    upstream's copy). Warnings (`-Wall`) in the changed files: none in the
    new code. `check`: 0 `r2off`, `.got` 0x8710. PKG of 17:18 in
    `build/ps3_g9/`. Not tested on hardware.
- 2026-10-22: The 17:18 PKG on hardware (b40 into the snow, a death after a
  checkpoint; Matt: "Run's done."; no fault reported). `[spu] render up`,
  `[ps3] exit 0`, the four SPU groups down, no `SPU off`, 0 jobs refused,
  503 shadows checked, 0 apart; the revert put back by an SPU in 2.8 ms.
  - Medians of 29 play windows (loads left out) / the 7 under 56 fps (1
    under 50: this run had less heavy combat than the G9 evidence's 12):
    58.8 / 52.6 fps; the frame 17.0 / 19.0 ms, ticks 3.5 / 4.6, render
    7.8 / 12.8 (the 15:18 run: 9.0 all; the evidence under 50: 14.65), the
    RSX 11.2 / 15.3.
  - The render's parts under 56 fps (the evidence under 50): visibility
    0.1 (0.4), objects 2.7 (3.2), lightmaps 0.4 (1.05), lights 2.2 (1.65),
    diffuse 1.8 (1.3), specular 1.5 (2.1), effects 2.0 (2.3), interface
    1.2 (1.25). The lights part holds the lights' jobs' set-up now and the
    shadows (1.09, 5.1 shadows a frame); the diffuse part's decals 1.37
    (immediate mode, 3 calls a vertex).
  - The render SPU, under 56 fps: lights 2.0 jobs a frame, 342 us a job;
    visibility 274 us a window; sprites 202 us; contrails 26 us; weather
    543 us; shadows 48 us each. The game thread's waits for all of them
    0.000 ms a frame but the shadows' 0.027.
  - Main memory: 21.1 MB free at t=150 s, 19.1 at the end (the lowest).
- 2026-10-22: The BSP switch's stutter (Matt: "Sometimes when loading a new
  chunk of the level would stutter the game."): b40's switch at t=125 s
  held the game thread about 0.8 s: the 5.4 MB BSP read from
  `z:\cache001.map` in 564 ms (9.5 MB/s; the same at the level's start),
  its swap 58 ms, the rest of the switch about 0.2 s (not timed: the tag
  cache's 0xCD fill, the Direct3D thread's drain, the tag checks, the
  game's switch). Other reads of the run: the 2.1 MB BSP 36 MB/s, the tag
  data 20 MB/s, the save game in 128 KB calls 48 MB/s. The Xbox's game
  waits for the read too (`scenario_structure_bsp_load`). Then (Matt:
  "let's go with option 1."; with the decals and the shadows' sort):
  - `xbox_files.c`: a read's lv2 calls 128 KB at most (each read: the
    BSPs, the tag data, the caches' reads); the log's line gives each read
    of 1 MB or more with its time, as before.
  - The decals (`rasterizer_xbox_decals.c`, `d3d8_rsx.c`): a batch's
    vertices made in one loop (registers 0, 4, 9: as
    `SetVertexData4f(0)`, `SetVertexData2f(4)`, `SetVertexData4ub(9)` give
    them) and drawn by `d3d8_draw_immediate`: the same record and the same
    registers after it as `Begin`, the calls and `End` (a batch past 4,096
    vertices in more draws, in order; a decal past it by the calls).
  - The shadows (`spu_game.c`, `structure_render.c`): the SHADOW job with
    a triangles' address sorts the surfaces found (its flags in order) and
    gathers their triangles; `structure_render_shadow` copies them into
    dynamic triangles. The one-in-64 check sorts the PPE's search and
    compares the triangles too. The lights' searches keep the walk's
    order.
  - PC check (as before, and the shadows' sorted surfaces and triangles
    against the old image's walk, sorted and gathered): 16.9 M checks, 0
    apart; 19 faults, each found (the two new: a triangle from the next
    surface, the walk's order kept). The decals and the reads: not in the
    check (read in the code: the same record, the same bytes).
  - Comments over 2 lines shortened (`rasterizer_xbox_decals.c`: 3, the
    symbol table pointing to upstream's copy). Warnings (`-Wall`): none in
    the new code. `check`: 0 `r2off`, `.got` 0x8710. PKG of 18:00 in
    `build/ps3_g9/`. Not tested on hardware.
- 2026-10-22: The 18:00 PKG on hardware (b40 through the BSP switch; Matt:
  "Done"; no fault reported): `[ps3] exit 0`, no `SPU off`, 488 shadows
  checked, 0 apart. The 128 KB reads changed nothing: the 5.4 MB BSP 566
  and 567 ms, the tag data 891 ms (before 846). The decals' part outside
  Direct3D fell from about 4 times its Direct3D part to 1 time (the
  Direct3D part copies the vertices in both versions; no decal count in
  the log). The shadows' triangles 0.05-0.19 to 0.01-0.04 ms a frame;
  their SPU job 43 to 57 us.
- 2026-10-22: Test 1 (Matt deleted `z/cache000.map` to `cache005.map`; the
  same PKG): the game made b40's cache file again at the load (the map
  read in 25 s, written in 4 MB pieces); then the 5.4 MB BSP read in 116
  ms (before 564-567), the tag data in 327 ms (before 846-891), the 2.1 MB
  BSP 56 ms (as before). The old cache file's layout on the HDD was the
  slow read. No BSP switch in play this run. Matt: the stutter seems to
  come when enemies first shoot. In the 600 frames of the first combat
  (frames 3600 to 4200): 126 pixel shaders and 4 vertex shaders
  translated at their first draw (on the Direct3D thread), 72 textures
  uploaded (1.8 MB), the ticks 4 to 8 ms each (the AI's combat start). The
  300-frame windows do not show one long frame.
