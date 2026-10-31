#!/usr/bin/env python3
"""Writes the copies of the sources compiled with the Xbox's ABI, `long` spelled `int` (the game's
tools/lp64_rewrite.py), into the build folder. Only copies older than their source are written again."""

import argparse
import os
import sys
from pathlib import Path

# the game's folders that its 64-bit builds rewrite (tools/lp64_build.py's rewritten_inputs), and port/console
GAME_ROOTS = ("source", "port/linux/game", "port/linux/src", "port/linux/include", "port/include",
              "port/third_party/kcp", "port/third_party/tomlc17", "port/console/game", "port/console/include")
# ours: the Xbox layer, our game units and the headers they include (ps3_spu_game.h includes ../spu/spu_game.h)
PORT_ROOTS = ("port/ps3/xbox", "port/ps3/game", "port/ps3/include", "port/ps3/spu")
SUFFIXES = (".c", ".h", ".inl", ".inc")


def sources(root: Path, names, native_prefix=None):
    for name in names:
        for directory, _, files in os.walk(root / name):
            for file in files:
                if file.endswith(SUFFIXES) and not (native_prefix and file.startswith(native_prefix)):
                    yield Path(directory, file).relative_to(root)


def sync(rewrite, root: Path, relative: Path, out: Path) -> bool:
    source = root / relative
    target = out / relative
    try:
        if target.stat().st_mtime >= source.stat().st_mtime:
            return False
    except FileNotFoundError:
        pass
    with open(source, encoding="latin-1", newline="") as f:
        text = f'#line 1 "{relative.as_posix()}"\n' + rewrite(f.read())
    target.parent.mkdir(parents=True, exist_ok=True)
    with open(target, "w", encoding="latin-1", newline="") as f:
        f.write(text)
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.game / "tools"))
    from lp64_rewrite import rewrite  # noqa: E402

    written = 0
    # (the game's posix_*.c have the host's ABI; our posix_ps3.c has the game's)
    for relative in sources(args.game, GAME_ROOTS, "posix_"):
        written += sync(rewrite, args.game, relative, args.out)
    for relative in sources(Path("."), PORT_ROOTS):
        written += sync(rewrite, Path("."), relative, args.out)
    if written:
        print(f"LP64 {written} files")


if __name__ == "__main__":
    main()
