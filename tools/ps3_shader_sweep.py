"""The shader sweep (shader.md, step 3): the Windows build of the game, with sweep_d3d.c in front of its Direct3D
draws and sweep_game.c's set-ups, runs each map without a window and writes the PS3 pixel shader keys it made;
the keys of all maps go into port/ps3/shader_keys.txt.

    python tools/ps3_shader_sweep.py --maps <folder of the Xbox maps> [--only a10,b40] [--no-merge | --merge-only]

It needs the Windows build's objects (python configure.py, then ninja windows) and a native ninja (NINJA, or the
one of Visual Studio). Run it with Windows' Python.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SWEEP = ROOT / "build" / "windows" / "sweep"
KEYS = ROOT / "port" / "ps3" / "shader_keys.txt"
SOURCES = ROOT / "tools" / "ps3_shader_sweep"
VISUAL_STUDIO_NINJA = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions"
                           r"\Microsoft\CMake\Ninja\ninja.exe")
# the Direct3D entries sweep_d3d.c stands in front of
WRAPPED = ("D3DDevice_DrawVertices", "D3DDevice_DrawIndexedVertices", "D3DDevice_End", "D3DDevice_SetTexture",
           "D3DDevice_SetRenderTarget", "D3DDevice_GetBackBuffer", "D3DDevice_Present")
RSX_SOURCES = ("ps3_rsx_asm.c", "ps3_rsx_vp.c", "ps3_rsx_fp.c")
# the sweep's game units; one that includes a game unit to reach its statics takes that unit's object's place
GAME_UNITS = {
    "sweep_game.c": None,
    "sweep_structure.c": "source/structures/structure_render.o",
    "sweep_transparent.c": "source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.o",
    "sweep_structures.c": "source/structures/structures.o",
}
CAMPAIGN = ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")


def ninja() -> str:
    found = os.environ.get("NINJA") or (str(VISUAL_STUDIO_NINJA) if VISUAL_STUDIO_NINJA.is_file() else "ninja")
    return found


def run(command, **options):
    print(" ".join(command) if isinstance(command, list) else command, flush=True)
    subprocess.run(command, check=True, cwd=ROOT, **options)


def commands() -> list:
    output = subprocess.run([ninja(), "-t", "commands", r"build\windows\halo.exe"], check=True, cwd=ROOT,
                            capture_output=True, text=True).stdout
    return output.splitlines()


def compile_like(template: str, source: str, output: Path, extra: str = "") -> None:
    """a unit compiled as the template's, without its depfile"""
    command = re.sub(r"-MMD -MF \S+ ", "", template)
    replacement = f" {extra} -c {source} -o {output}"
    command = re.sub(r" -c \S+ -o \S+", lambda match: replacement, command)
    run(command)


def link_inputs() -> list:
    output = subprocess.run([ninja(), "-t", "query", r"build\windows\halo.exe"], check=True, cwd=ROOT,
                            capture_output=True, text=True).stdout
    inputs, reading = [], False
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("input:"):
            reading = True
            continue
        if stripped.startswith("outputs:"):
            break
        if reading and stripped and not stripped.startswith("|") and not stripped.endswith(":"):
            inputs.append(stripped)
    return [item for item in inputs if item.endswith((".o", ".res", ".lib", ".obj"))]


def build() -> Path:
    run([ninja(), "windows"])
    SWEEP.mkdir(parents=True, exist_ok=True)
    lines = commands()
    d3d8 = next(line for line in lines if "d3d8_gl.c" in line and " -c " in line)
    game = next(line for line in lines if "rasterizer_xbox_environment.c" in line and " -c " in line)
    link = lines[-1]

    renames = " ".join(f"-D{name}=sweep_real_{name}" for name in WRAPPED)
    compile_like(d3d8, r"port\linux\src\d3d8_gl.c", SWEEP / "d3d8_gl.o", renames)
    compile_like(d3d8, str(SOURCES / "sweep_d3d.c"), SWEEP / "sweep_d3d.o")
    objects = [SWEEP / "sweep_d3d.o"]
    for name in GAME_UNITS:
        output = SWEEP / name.replace(".c", ".o")
        compile_like(game, str(SOURCES / name), output)
        objects.append(output)
    for name in RSX_SOURCES:
        output = SWEEP / name.replace(".c", ".o")
        run(["clang", "--target=i686-pc-windows-msvc", "-std=gnu99", "-O2", "-Wall", "-ffp-contract=off",
             "-D_CRT_SECURE_NO_WARNINGS", "-c", str(ROOT / "port" / "ps3" / "src" / name), "-o", str(output)])
        objects.append(output)

    replaced = ["port/linux/src/d3d8_gl.o"] + [unit for unit in GAME_UNITS.values() if unit]
    inputs = [item for item in link_inputs()
              if not any(item.replace("\\", "/").endswith(unit) for unit in replaced)]
    response = SWEEP / "shader_sweep.rsp"
    paths = inputs + [str(SWEEP / "d3d8_gl.o")] + [str(item) for item in objects]
    response.write_text("\n".join(f'"{path}"' for path in paths) + "\n")
    executable = SWEEP / "shader_sweep.exe"
    link = re.sub(r"-o \S+", lambda match: f"-o {executable}", link, count=1)
    link = re.sub(r"@\S+\.rsp", lambda match: f"@{response}", link)
    run(link)
    shutil.copy2(ROOT / "build" / "windows" / "SDL3.dll", SWEEP / "SDL3.dll")
    return executable


def map_level(name: str) -> str:
    if name == "ui":
        return ""
    if name in CAMPAIGN:
        return f"levels\\{name}\\{name}"
    return f"levels\\test\\{name}\\{name}"


def runs(name: str) -> list:
    """a campaign map also runs without its scripts: no opening cinematic, the player in control with the HUD"""
    return [name, f"{name}_noscripts"] if name in CAMPAIGN else [name]


def sweep_map(executable: Path, maps: Path, run_name: str) -> Path:
    name = run_name.replace("_noscripts", "")
    data = SWEEP / "data" / run_name
    data.mkdir(parents=True, exist_ok=True)
    link = data / "maps"
    if not link.exists():
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(maps)], check=True, capture_output=True)
    level = map_level(name)
    scripts = "run_game_scripts false\n" if run_name != name else ""
    (data / "init.txt").write_text(scripts + (f"map_name {level}\n" if level else ""))
    output = SWEEP / f"keys_{run_name}.txt"
    if output.exists():
        output.unlink()
    environment = dict(os.environ, HALO_DATA_ROOT=str(data), HALO_SAVE_ROOT=str(SWEEP / "saves"),
                       HALO_NULL_RENDERER="1", HALO_HIDDEN_WINDOW="1", HALO_NET_ONLINE="false",
                       HALO_SHADER_SWEEP_OUT=str(output))
    (SWEEP / "saves").mkdir(exist_ok=True)
    log = SWEEP / f"log_{run_name}.txt"
    # (an assertion stops the game without its exit: data\<run>\debug.txt names it)
    with open(log, "w") as handle:
        try:
            result = subprocess.run([str(executable)], cwd=SWEEP, env=environment, stdout=handle,
                                    stderr=subprocess.STDOUT, timeout=2400)
        except subprocess.TimeoutExpired:
            raise SystemExit(f"{run_name}: the sweep did not end; see {data / 'debug.txt'}")
    if result.returncode != 0 or not output.exists():
        raise SystemExit(f"{run_name}: the sweep failed (exit {result.returncode}); see {log}")
    return output


def merge(outputs: list) -> None:
    """the keys of every map into shader_keys.txt: its header kept, each key once with the first map and site"""
    header, seen, lines = [], set(), []
    for line in KEYS.read_text().splitlines():
        if line.startswith("#"):
            header.append(line)
    for output in outputs:
        for line in output.read_text().splitlines():
            key = line.split("#", 1)[0].strip()
            if key and key not in seen:
                seen.add(key)
                lines.append(line)
    KEYS.write_text("\n".join(header + lines) + "\n", newline="\n")
    print(f"{KEYS}: {len(lines)} keys from {len(outputs)} maps")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--maps", required=True, type=Path)
    parser.add_argument("--only", default="")
    parser.add_argument("--no-merge", action="store_true")
    parser.add_argument("--merge-only", action="store_true", help="merge the keys of an earlier sweep")
    arguments = parser.parse_args()
    if sys.platform != "win32":
        raise SystemExit("run it on Windows, with Windows' Python")

    names = [item.strip() for item in arguments.only.split(",") if item.strip()] or \
        sorted(path.stem for path in arguments.maps.glob("*.map"))
    if arguments.merge_only:
        outputs = [SWEEP / f"keys_{run}.txt" for name in names for run in runs(name)]
    else:
        executable = build()
        outputs, failures = [], []
        for run in [run for name in names for run in runs(name)]:
            try:
                outputs.append(sweep_map(executable, arguments.maps, run))
            except SystemExit as failure:
                print(failure, flush=True)
                failures.append(run)
        if failures:
            raise SystemExit(f"failed: {', '.join(failures)} (nothing merged)")
    if not arguments.no_merge:
        merge(outputs)


if __name__ == "__main__":
    main()
