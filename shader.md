# Shader pack: the static sweep (plan, 2026-10-22)

## The problem

- The game can stutter when enemies shoot for the first time. One suspect is
  the first-use shader translation on the PS3.
- The PS3 translates each NV2A pixel shader to RSX microcode when a draw first
  uses it ([d3d8_rsx.c:1320](port/ps3/xbox/d3d8_rsx.c#L1320),
  `fragment_program_get`). The vertex shaders are also translated on first use
  (`vertex_program_get`).
- [STUTTER_FINDINGS.md](STUTTER_FINDINGS.md) estimates about 1.3 ms per pixel
  shader on the PPE, up to 9 ms. Many new shaders in one frame can cost a
  frame.
- This cause is not proven yet.

## Why a sweep, and not a list from the map

- A pixel shader key is not stored in the map. The rasterizer builds it at
  each draw, at about 60 places in 19 files under
  [source/rasterizer/xbox/](source/rasterizer/xbox/).
- Each place mixes tag fields with runtime state: fog on or off, the number of
  lights, the camouflage, and the type of each bound texture (2D, cube, 3D).
- So a tool must run the game's own setup code for each shader tag, in each
  runtime case. Extra keys cost only memory, so collecting too many is safe.

## What is available

- The 24 maps: `E:\Users\Matteo\Desktop\Halo\Hola\maps`. These are 10
  campaign maps, 13 multiplayer maps and `ui.map`.
- The game's sources.
- The PS3 translators, which are plain C that uses only libc:
  [ps3_rsx_fp.c](port/ps3/src/ps3_rsx_fp.c) and
  [ps3_rsx_vp.c](port/ps3/src/ps3_rsx_vp.c). They run on the PC.
- The shader check tool:
  [tools/ps3_rsx_shader_check.c](tools/ps3_rsx_shader_check.c).
- The compilers: clang on Windows, and gcc 13 in the ps3dev Docker image.

## Steps

1. **Clean the key.**
   - `fragment_key_build` ([d3d8_rsx.c:1445](port/ps3/xbox/d3d8_rsx.c#L1445))
     copies all 8 combiner stages and all 4 texture stages, even the unused
     ones.
   - The unused stages hold old values from the draw before. So one shader
     can get many keys, depending on the draw order, and no sweep can predict
     them.
   - The translator reads only the stages below the combiner count
     ([ps3_rsx_fp.c:2926](port/ps3/src/ps3_rsx_fp.c#L2926)) and the texture
     stages that have a mode
     ([ps3_rsx_fp.c:475](port/ps3/src/ps3_rsx_fp.c#L475)). So clearing the
     rest gives the same microcode.
   - Prove this with the check tool: the cleaned key and the full key must
     give the same microcode.
   - This step also means fewer translations during play, even without a
     pack.
2. **Audit the draw places.**
   - For each of the ~60 places, list the tag fields and the runtime state it
     reads.
   - Example: the model draw changes some registers only when its dirty flag
     is set
     ([rasterizer_xbox_models.c:803](source/rasterizer/xbox/rasterizer_xbox_models.c#L803)).
3. **Build the sweep tool on the PC.**
   - It compiles those rasterizer files with a fake Direct3D that records the
     render states and builds the PS3 key at each draw.
   - It loads each map's tags, as the SPU simulator check already does.
   - It gets each texture's type (2D, cube, 3D) from the bitmap tags.
4. **Make the pack.**
   - The translator runs on the PC for all the keys of all 24 maps and for
     the 67 vertex shaders.
   - The PKG carries the microcode. The PS3 fills its shader cache at load.
   - The build makes the pack from the current translator, so the pack always
     matches it.
5. **Keep a fallback.** A key that is not in the pack is translated during
   play, as now, and logged.
6. **Check on the PS3 once.** The log must show 0 translations during play,
   or only the few keys the sweep missed.

## The other option: a scripted run of the PC build

- The tree also builds the Windows and Linux versions of the game. They run
  the same sources on the same Xbox maps.
- Add the PS3's key builder to the PC's Direct3D layer, and save each new key
  to a file.
- A script runs each map with no person and a hidden window. Through the
  console, it:
  1. loads each BSP;
  2. moves the camera through the level;
  3. creates each unit, weapon and vehicle of the map;
  4. lets the AI fight.
- The keys it collects cover only what the run draws.
- It also counts the new keys in each frame, which helps to check the cause.

## Limits

- The PS3 build has its own render changes. A key that only the PS3 makes is
  not in the pack; the fallback translates and logs it.
- The PC version of the game has not been built in this tree yet.
