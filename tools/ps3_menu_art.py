#!/usr/bin/env python3
"""The Chupathingy menus for the PS3 (port/ps3/xbox/menu_art_rsx.c): build shrinks the menus' PNGs to their size in the
640x480 menu (Pillow) into port/assets/menus/ps3; embed writes those pictures and the menus' XML as C (stdlib only)."""

import argparse
import json
import sys
import xml.etree.ElementTree as ElementTree
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "chupathingyce/port/assets/menus"
LIST = MENUS / "menus.json"
ASSETS = ROOT / "port/assets/menus/ps3"
PACK = ASSETS / "ps3_menu_art.bin"
INDEX = ASSETS / "ps3_menu_art.json"


def menu_files() -> list:
    return json.loads(LIST.read_text())["files"]


def picture_sizes() -> dict:
    """Each PNG the XML names, at the largest size a frame gives it in the menu."""
    sizes = {}
    for name in menu_files():
        if not name.endswith(".xml"):
            continue
        for element in ElementTree.parse(MENUS / name).iter():
            pngs = []
            if element.tag == "frame" and element.get("png"):
                pngs = [element.get("png")]
            elif element.tag == "bitmap" and element.get("frames"):
                pngs = element.get("frames").split()
            if not pngs or not element.get("width") or not element.get("height"):
                continue
            width, height = int(element.get("width")), int(element.get("height"))
            for png in pngs:
                known = sizes.get(png, (0, 0))
                sizes[png] = (max(known[0], width), max(known[1], height))
    return sizes


def source_crc(png: str) -> int:
    return zlib.crc32((MENUS / png).read_bytes())


def build(arguments) -> None:
    from PIL import Image

    entries, pack = [], bytearray()
    for png, (width, height) in sorted(picture_sizes().items()):
        image = Image.open(MENUS / png).convert("RGBA")
        if image.size != (width, height):
            # (premultiplied, so the transparent texels' colour does not bleed into the edges)
            image = image.convert("RGBa").resize((width, height), Image.LANCZOS).convert("RGBA")
        red, green, blue, alpha = image.split()
        data = zlib.compress(Image.merge("RGBA", (alpha, red, green, blue)).tobytes(), 9)
        entries.append({"name": png, "width": width, "height": height, "offset": len(pack), "size": len(data),
                        "source": source_crc(png)})
        pack += data
    ASSETS.mkdir(parents=True, exist_ok=True)
    PACK.write_bytes(bytes(pack))
    INDEX.write_text(json.dumps({"pictures": entries}, indent=1) + "\n")
    print(f"{len(entries)} pictures, {len(pack) / 1e6:.2f} MB, "
          f"{sum(entry['width'] * entry['height'] * 4 for entry in entries) / 1e6:.1f} MB as textures")


def words(data: bytes) -> list:
    """data as lines of big-endian 32-bit words (padded with zeros)."""
    padded = data + b"\0" * (-len(data) % 4)
    values = [int.from_bytes(padded[start:start + 4], "big") for start in range(0, len(padded), 4)]
    return ["\t" + ", ".join(f"0x{value:08x}" for value in values[start:start + 8]) + ","
            for start in range(0, len(values), 8)]


def embed(arguments) -> None:
    entries = json.loads(INDEX.read_text())["pictures"]
    pack = PACK.read_bytes()
    known = {entry["name"]: entry for entry in entries}
    stale = [png for png in picture_sizes() if png not in known or known[png]["source"] != source_crc(png)]
    if stale:
        sys.exit(f"ps3_menu_art.py: {len(stale)} pictures changed or new ({stale[0]}...): run tools/ps3_menu_art.py build")
    lines = ["/* tools/ps3_menu_art.py embed: the Chupathingy menus' XML and pictures for menu_art_rsx.c */", ""]
    # (the XML alone: menu_art_rsx.c draws the pictures, so their PNGs stay out)
    xml = [name for name in menu_files() if name.endswith(".xml")]
    for index, name in enumerate(xml):
        lines += [f"static const unsigned int menu{index}[] =", "{", *words((MENUS / name).read_bytes()), "};"]
    lines += ["", "const struct menu_file_embedded menu_files_embedded[] =", "{"]
    lines += [f'\t{{ "{name}", menu{index}, {(MENUS / name).stat().st_size} }},' for index, name in enumerate(xml)]
    lines += ["};", f"const unsigned int menu_files_embedded_count = {len(xml)};", ""]
    for index, entry in enumerate(entries):
        data = pack[entry["offset"]:entry["offset"] + entry["size"]]
        lines += [f"static const unsigned char picture{index}[{len(data)}] =", "{"]
        lines += ["\t" + ", ".join(str(byte) for byte in data[start:start + 24]) + ","
                  for start in range(0, len(data), 24)]
        lines.append("};")
    # (sorted by name, as menu_art_rsx.c's search needs: Python's order is the bytes', as strcmp's)
    lines += ["", "static const struct ps3_menu_picture ps3_menu_pictures[] =", "{"]
    order = sorted(range(len(entries)), key=lambda index: entries[index]["name"].encode())
    lines += [f'\t{{ "{entries[index]["name"]}", {entries[index]["width"]}, {entries[index]["height"]}, '
              f'picture{index}, {entries[index]["size"]} }},' for index in order]
    lines += ["};", ""]
    Path(arguments.out).parent.mkdir(parents=True, exist_ok=True)
    Path(arguments.out).write_text("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("build")
    command = commands.add_parser("embed")
    command.add_argument("--out", required=True)
    arguments = parser.parse_args()
    {"build": build, "embed": embed}[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
