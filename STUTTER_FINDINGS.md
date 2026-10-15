# Stutter findings (2026-10-10)

Matt: "Whenever I have to save, the game microstutters for a second. Or if
it's loading a shader or something. Investigate."

A read-only investigation of the code, the run data in `ROADMAP.md` and one
timing on the PC. No file of the tree changed. Line numbers are those of the
tree on 2026-10-10.

## Summary

- The save stutter has one cause, and it is measured: each checkpoint
  writes 16 MB to the HDD on the game thread, 1.12 to 1.20 s.
- The recommended fix is a checkpoint in RAM: an estimated 20 to 60 ms.
- The other stutters have candidates only. The present timers give the
  average of 300 frames, so they cannot show one long frame.
- The recommended next step for these is a spike log.

## 1. Save stutter (measured)

### Cause

- Each checkpoint writes the full game state to `z:\savegame.bin` in one
  blocking write on the game thread.
- The path:
  1. `main_saving_map_private` ([main.c:1529](source/main/main.c#L1529)),
     called at the top of the main loop
     ([main.c:2793](source/main/main.c#L2793)).
  2. `game_state_save`
     ([game_state.c:296](source/saved%20games/game_state.c#L296)).
  3. `game_state_write_to_file`
     ([game_state_xbox.c:192](source/saved%20games/game_state_xbox.c#L192)):
     one `WriteFile` of `buffer_size` bytes.
  4. `transfer` ([xbox_files.c:265](port/ps3/xbox/xbox_files.c#L265)), then
     `sysLv2FsWrite` ([ps3_host.c:428](port/ps3/src/ps3_host.c#L428)).
- The size is `HALO_PORT_GAME_STATE_SIZE`: 16 MB
  ([halo_port_capacity.h:34](port/linux/include/halo_port_capacity.h#L34)).
  The Xbox state is 3.3 MB (0x345000). The port uses the larger state of
  the native builds.
- About 13.6 MB of the CPU part is allocated, and 0.25 MB of the GPU part.
  A write of only the used part saves about 13%. That is not sufficient.

### Data (run of 2026-10-06, PKG of 21:03, a30)

| Action | Time on the game thread |
| --- | --- |
| Checkpoint: write 16 MB to `z:\savegame.bin` | 1.12 to 1.20 s |
| Death or "restart at checkpoint": read 16 MB | 1.0 s |
| Pause > quit: the revert, then 16 MB to the profile and `fsync` | 2.1 s |
| Check of the profile's save (menu, level start) | 0.7 s |
| Resume read | 0.75 s |

The disk speed from this data is about 14 MB/s for a write and 16 MB/s for
a read.

### The timer log agrees

- "rest" in the `[time]` line is the frame time minus the ticks, the
  render, the throttle wait and the present
  ([ps3_timers.c:44](port/ps3/src/ps3_timers.c#L44)).
- The save runs at the top of the main loop, outside these spans. Thus its
  time goes into "rest".
- The optimizer run of 2026-10-06 had two windows at 26.9 frames per second
  with "rest" 4.0 ms a frame. 4.0 ms x 300 frames = 1.2 s: one checkpoint
  in each window.
- `ROADMAP.md` ("Optimizer run") calls these two windows "loads, not the
  RSX". That is incorrect. Texture uploads, new pixel shaders and vertex
  cache fills are in "render", not in "rest".

### What the file is for

- `z:\savegame.bin` is a scratch file of the current boot only.
  - `file_valid_for_read` starts as FALSE at each boot
    ([game_state_xbox.c:56](source/saved%20games/game_state_xbox.c#L56)),
    and only a write in this boot sets it.
  - `saved_game_valid` goes to FALSE at each map start
    ([game_state.c:267](source/saved%20games/game_state.c#L267)).
  - The only other reader is the debug global `recover_saved_games_hack`.
- The resume after a restart uses the profile's `savegame.bin`. Only
  pause > quit writes it
  ([ui_widget_event_handler_functions.c:1117](source/interface/ui_widget_event_handler_functions.c#L1117)).

### Recommended fix: the checkpoint in RAM

- On the PS3, replace the file with a 16 MB buffer in main memory.
  - `game_state_write_to_file`: one copy of the game state into the buffer.
  - `game_state_read_from_file`: one copy back.
- Estimate (not measured): 20 to 60 ms for a 16 MB copy on the PPE, in
  place of 1.2 s for a save and 1.0 s for a revert.
- Main memory: 93.1 MB free at the lowest (measurement run), so 16 MB is
  available.
- A power loss has the same result as now: the scratch file is not read
  after a restart.
- One frame without a picture stays. `game_state_save` calls
  `main_stop_time`, so the save frame gets no game time and no render
  ([main.c:2953](source/main/main.c#L2953)). The Xbox does the same.
- Pause > quit keeps its write to the profile (0.96 s and the `fsync`). It
  is a menu action, so a wait there is less visible. Its first part, the
  revert, becomes a copy.

## 2. Other stutters (candidates, not measured)

The game thread does this work at the first use of a resource, in the
render. The times are estimates from the code and from older run data.

### 2.1 New texture (largest candidate)

- The disk wait:
  - The world's texture path asks with `block` TRUE
    ([rasterizer_xbox.c:1513](source/rasterizer/xbox/rasterizer_xbox.c#L1513)).
  - If the texture is not in the texture cache, the game thread waits in a
    `SwitchToThread` loop while the cache thread reads it
    ([xbox_texture_cache.c:892](source/cache/xbox_texture_cache.c#L892)).
  - At 16 MB/s, a texture of 0.2 to 1 MB is 15 to 80 ms.
  - The predicted resources of each cluster start loads early without a
    wait ([observer.c:1118](source/camera/observer.c#L1118)). They prevent
    most waits, not all. The Xbox has the same wait.
- The PS3-only cost, after the read:
  - `texture_get` hashes all texels
    ([d3d8_rsx.c:682](port/ps3/xbox/d3d8_rsx.c#L682)). The timer build
    measured 8.8 to 15.3 ms for 5.0 MB: 2 to 3 ms for each MB.
  - `texture_upload` copies DXT data to RSX memory. For the other formats
    it calls `convert_texel` for each texel, through a `switch`
    ([xbox_texture_layout.c:207](port/ps3/xbox/xbox_texture_layout.c#L207)).
- The RSX copy has the texture's address in the texture cache as its key.
  The texture cache is 22 MB, and the bitmaps of one BSP are up to 25.4 MB
  (map scan). When the cache loads a dropped texture again at a different
  address, the PS3 hashes and uploads it again.
- Calm play in a10 had about 12 uploads in 600 frames.

### 2.2 New pixel shader

- The first draw with a new key translates and optimizes the shader
  ([d3d8_rsx.c:931](port/ps3/xbox/d3d8_rsx.c#L931),
  `rsx_fragment_program_translate` in `ps3_rsx_fp.c`).
- PC timing of 2026-10-10 (Ryzen 5 3600, host GCC `-O2`, 3000 random keys
  of the check tool, three passes): 0.16 ms on average, 0.7 to 1.1 ms at
  most.
- The random keys are heavier than the game's shaders (249 instructions on
  average before the optimizer).
- Estimate for the PPE, 8 times slower: 1.3 ms on average, up to 9 ms.
- An a10 run translates 143 shaders. There is no cache on the disk, so
  each boot does this again.
- One new shader is not visible. Many in one frame (a new area, a new
  effect) can cost a frame.

### 2.3 New vertex buffer

- The first draw of a map vertex buffer converts the full buffer for each
  element ([d3d8_rsx.c:1621](port/ps3/xbox/d3d8_rsx.c#L1621)).
- The timer build gives at most about 0.5 us for each element. A model of
  3000 vertices with 7 elements is up to 10 ms.
- A map or BSP load empties the full cache, the models included
  ([d3d8_rsx.c:1528](port/ps3/xbox/d3d8_rsx.c#L1528)). After a BSP switch,
  all visible models and BSP parts convert again in the first frames.

### 2.4 Not a cause

- Sounds: all requests have `block` FALSE. A stream packet is at most
  9216 bytes (`MAXIMUM_COMPRESSED_SOUND_PACKET_SIZE` 2304, x2 stereo, x2
  44 kHz), so its ADPCM decode on the game thread is below 1 ms.
- The RSX: the wait was 0.0 ms in all timer windows of the optimizer run.
  `ps3_gpu_wait_idle` runs only at a load.
- The log: one UDP datagram for each line.

### Recommended next step: a spike log

- One log line for each frame above 40 ms on the game thread, with the
  split of that frame only:
  - the wait for the texture cache (count and ms),
  - the texture hash and the uploads (count, KB and ms),
  - the new pixel shaders and vertex shaders (count and ms),
  - the vertex cache fills (elements and ms),
  - the ticks, the render, the rest.
- One run in a level then shows the real cause of each stutter.

## PC timing of the pixel shader translation

The harness includes the check tool, so it uses the tool's `random_key`.
Save it as `fp_time.c` in a folder outside the tree (`OUT`, a Windows path
with forward slashes):

```c
#define _POSIX_C_SOURCE 200809L
#include <time.h>

#define main check_main
#include "/src/tools/ps3_rsx_shader_check.c"
#undef main

static double now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main(void)
{
	enum { KEY_COUNT = 3000 };
	static struct rsx_fragment_key keys[KEY_COUNT];
	double total = 0, worst = 0, start, elapsed;
	unsigned long words = 0, done = 0;
	int index, pass;

	for (index = 0; index < KEY_COUNT; index++)
		random_key(&keys[index]);
	for (pass = 0; pass < 3; pass++)
	{
		total = worst = 0;
		words = done = 0;
		for (index = 0; index < KEY_COUNT; index++)
		{
			struct rsx_fragment_program program;

			memset(&program, 0, sizeof(program));
			start = now_us();
			if (rsx_fragment_program_translate(&keys[index], &program) == 0)
			{
				elapsed = now_us() - start;
				total += elapsed;
				if (elapsed > worst)
					worst = elapsed;
				words += program.word_count;
				done++;
				rsx_fragment_program_free(&program);
			}
		}
		printf("pass %d: %lu keys, %.1f us on average, %.1f us at most, %lu words on average\n",
			pass, done, total / done, worst, words / done);
	}
	return 0;
}
```

The command (Git Bash). The tree is mounted read-only:

```sh
OUT=C:/path/to/a/folder
MSYS_NO_PATHCONV=1 docker run --rm -v "E:/Users/Matteo/Desktop/Halo/PS3:/src:ro" -v "$OUT:/out" ps3dev-latest:latest sh -c "
gcc -std=gnu99 -O2 -w -I/src/port/ps3/src -o /tmp/fp_time /out/fp_time.c \
	/src/port/ps3/src/ps3_rsx_asm.c /src/port/ps3/src/ps3_rsx_vp.c /src/port/ps3/src/ps3_rsx_fp.c -lm &&
/tmp/fp_time"
```

The result of 2026-10-10:

```
pass 0: 3000 keys, 159.9 us on average, 712.2 us at most, 173 words on average
pass 1: 3000 keys, 159.0 us on average, 694.1 us at most, 173 words on average
pass 2: 3000 keys, 167.2 us on average, 1130.2 us at most, 173 words on average
```
