#!/usr/bin/env python3
"""The PS3's button icons (EXTRA OPTIONS' BUTTON ICONS: port/ps3/README.md),
drawn from the approved redraws in ps3_buttons/svg (ps3_buttons/make.py)
in the places of the Xbox redraws (port/assets/buttons/buttons.json,
tools/button_assets.py):

    python tools/ps3_button_assets.py build
    python3 tools/ps3_button_assets.py embed --out build/ps3/ps3_buttons.h

build: renders port/assets/buttons/ps3/*.png at SCALE times the maps'
    bitmaps (720p draws them at 1.5 times), and ps3_buttons.json. Each menu
    icon has a texture a style: cross for A, circle for B, square for X,
    triangle for Y. The message icons' sheets (the menus' hud_msg_icons_sm, the
    HUD's combined hud_msg_icons) have one for both, the stencils the game
    tints, their A, B, X and Y sprites only.
    Needs resvg_py, Pillow, NumPy and SciPy.
embed: the PNGs as the RSX samples them (A8R8G8B8 words, swizzled, every mip
    level), zlib-compressed, in a C header for port/ps3/xbox/hud_hires_rsx.c.
    The standard library only (the Makefile runs it in the build's image).
"""

import argparse
import json
import struct
import sys
import xml.etree.ElementTree as ElementTree
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "ps3_buttons/svg"
XBOX_LIST = ROOT / "port/assets/buttons/buttons.json"
ASSETS = ROOT / "port/assets/buttons/ps3"
LIST = ASSETS / "ps3_buttons.json"
SCALE = 4
STYLES = ("colour", "black")
# the Xbox redraw each button stands in for
FACES = {"a_butn.svg": "cross", "b_butn.svg": "circle", "x_butn.svg": "square", "y_butn.svg": "triangle"}
# the redraws' oval: its centre (both sets'), and the Xbox's radii (tools/button_assets.py's art)
CENTRE = (10.341, 12.879)
PS3_RADIUS = 10.0
XBOX_RADII = (8.659, 12.932)
# the PS3 circles sit lower than the Xbox's tilted ovals: up by this (texels) to the key labels' middle
RAISE = 2.7

# the HUD's message icons (the HUD globals' messaging icons, the menus' are hud_msg_icons_sm): its A, B, X and Y
# sprites, from bitmap 0 of every Xbox map (its CRC); the places of their ovals measured on its alpha, as
# hud_msg_icons_sm's give theirs
PS3_SHEETS = [
    {"name": "hud_msg_icons__0", "tag": "ui\\hud\\bitmaps\\combined\\hud_msg_icons", "bitmap": 0, "width": 128,
     "height": 128, "crc": 1457896459, "sprites": [
         {"sequence": 0, "cell": [48, 72, 72, 101], "svg": "a_butn.svg", "place": [1.0787, 1.051, 1.399]},
         {"sequence": 1, "cell": [0, 72, 24, 102], "svg": "b_butn.svg", "place": [1.08, 1.134, 1.403]},
         {"sequence": 2, "cell": [100, 40, 124, 70], "svg": "x_butn.svg", "place": [1.0608, 1.418, 1.39]},
         {"sequence": 3, "cell": [24, 72, 48, 101], "svg": "y_butn.svg", "place": [1.0489, 1.543, 1.714]},
     ]},
]

SVG_NAMESPACE = "http://www.w3.org/2000/svg"
ElementTree.register_namespace("", SVG_NAMESPACE)


def document(width: int, height: int, body: str) -> str:
    return (f'<svg xmlns="{SVG_NAMESPACE}" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}">\n{body}\n</svg>\n')


def art(name: str, suffix: str) -> str:
    """A redraw's elements, its ids made unique by suffix."""
    text = (ART / name).read_text()
    root = ElementTree.fromstring(text)
    for element in root.iter():
        if element.get("id"):
            old = element.get("id")
            text = text.replace(f'id="{old}"', f'id="{old}{suffix}"').replace(f"url(#{old})", f"url(#{old}{suffix})")
    root = ElementTree.fromstring(text)
    return "".join(ElementTree.tostring(child, encoding="unicode") for child in root)


def stencil(face: str, suffix: str) -> str:
    """A stencil's elements in the redraws' frame (its oval at CENTRE): the
    file's are moved to the middle of a 32x32 cell."""
    moved = ElementTree.fromstring(f'<svg xmlns="{SVG_NAMESPACE}">{art(f"stencil_{face}.svg", suffix)}</svg>')[0]
    return "".join(ElementTree.tostring(child, encoding="unicode") for child in moved)


def render(text: str, scale: int):
    import io

    import numpy as np
    import resvg_py
    from PIL import Image

    png = resvg_py.svg_to_bytes(svg_string=text, zoom=scale)
    return np.asarray(Image.open(io.BytesIO(bytes(png))).convert("RGBA")).copy()


def build(arguments) -> None:
    from PIL import Image

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from hud_assets import bleed

    ASSETS.mkdir(parents=True, exist_ok=True)
    entries = []
    for xbox in json.loads(XBOX_LIST.read_text())["assets"] + PS3_SHEETS:
        # (the maps' A, B, X and Y bitmaps and the sheets: not the PC's own HUD buttons, the triggers and the sticks)
        if "sprites" not in xbox and (xbox.get("svg") not in FACES or not xbox["tag"].startswith("ui\\shell\\")):
            continue
        width, height = xbox["width"], xbox["height"]
        entry = {key: xbox[key] for key in ("name", "tag", "bitmap", "width", "height", "crc")}
        entry["scale"] = SCALE
        if "sprites" in xbox:
            body = []
            mask = 0
            for index, sprite in enumerate(xbox["sprites"]):
                left, top, right, bottom = sprite["cell"]
                scale, x, y = sprite["place"]
                # the Xbox oval's centre, and a circle of its area, inside the cell
                centre = (left + x + scale * CENTRE[0], top + y + scale * CENTRE[1])
                radius = min(scale * (XBOX_RADII[0] * XBOX_RADII[1]) ** 0.5, (right - left) / 2 - 0.5,
                             (bottom - top) / 2 - 0.5)
                size = radius / PS3_RADIUS
                moved = (f"translate({centre[0] - size * CENTRE[0]:.4f} {centre[1] - size * CENTRE[1]:.4f}) "
                         f"scale({size:.4f})")
                area = f'x="{left}" y="{top}" width="{right - left}" height="{bottom - top}"'
                body.append(f'<defs><clipPath id="cell{index}"><rect {area}/></clipPath></defs>'
                            f'<g clip-path="url(#cell{index})"><g transform="{moved}">'
                            f'{stencil(FACES[sprite["svg"]], f"s{index}")}</g></g>')
                mask |= 1 << sprite["sequence"]
            image = render(document(width, height, "\n".join(body)), SCALE)
            # (grey the same as alpha, as the sheet's)
            image[..., :3] = image[..., 3:4]
            entry["sprites"] = mask
            entry["png"] = [f"{xbox['name']}.png"] * 2
            Image.fromarray(image, "RGBA").save(ASSETS / entry["png"][0], optimize=True)
        else:
            scale, x, y = xbox["place"]
            entry["png"] = []
            for style in STYLES:
                svg = f"{style}_{FACES[xbox['svg']]}.svg"
                body = f'<g transform="translate({x} {y}) scale({scale}) translate(0 {-RAISE})">{art(svg, "")}</g>'
                image = bleed(render(document(width, height, body), SCALE))
                name = f"{style}_{xbox['name']}.png"
                Image.fromarray(image, "RGBA").save(ASSETS / name, optimize=True)
                entry["png"].append(name)
        entries.append(entry)
        print(f"{entry['tag']}: {', '.join(sorted(set(entry['png'])))}")
    LIST.write_text(json.dumps({"assets": entries}, indent=1) + "\n")


# ---------- embed (the standard library only)


def png_read(path: Path) -> tuple:
    """An 8-bit RGBA PNG's size and rows of bytes."""
    data = path.read_bytes()
    width, height, depth, colour, _, _, interlace = struct.unpack_from(">IIBBBBB", data, 16)
    if data[:8] != b"\x89PNG\r\n\x1a\n" or depth != 8 or colour != 6 or interlace:
        sys.exit(f"{path}: not an 8-bit RGBA PNG")
    position, compressed = 8, b""
    while position < len(data):
        length, kind = struct.unpack_from(">I4s", data, position)
        if kind == b"IDAT":
            compressed += data[position + 8:position + 8 + length]
        position += 12 + length
    filtered = zlib.decompress(compressed)
    stride = width * 4
    rows, above = [], bytearray(stride)
    for row in range(height):
        kind = filtered[row * (stride + 1)]
        line = bytearray(filtered[row * (stride + 1) + 1:(row + 1) * (stride + 1)])
        for index in range(stride):
            left = line[index - 4] if index >= 4 else 0
            up = above[index]
            up_left = above[index - 4] if index >= 4 else 0
            if kind == 1:
                line[index] = (line[index] + left) & 255
            elif kind == 2:
                line[index] = (line[index] + up) & 255
            elif kind == 3:
                line[index] = (line[index] + (left + up) // 2) & 255
            elif kind == 4:
                estimate = left + up - up_left
                to_left, to_up, to_up_left = abs(estimate - left), abs(estimate - up), abs(estimate - up_left)
                nearest = left if to_left <= to_up and to_left <= to_up_left else up if to_up <= to_up_left else up_left
                line[index] = (line[index] + nearest) & 255
        rows.append(line)
        above = line
    return width, height, b"".join(rows)


def half(width: int, height: int, texels: bytes) -> bytes:
    """The next mip level: each 2x2 block's mean (square power-of-two sizes)."""
    out = bytearray((width // 2) * (height // 2) * 4)
    stride = width * 4
    for y in range(height // 2):
        top, bottom = 2 * y * stride, (2 * y + 1) * stride
        for x in range(width // 2):
            for channel in range(4):
                column = 8 * x + channel
                total = texels[top + column] + texels[top + column + 4] + texels[bottom + column] + \
                    texels[bottom + column + 4]
                out[(y * (width // 2) + x) * 4 + channel] = (total + 2) // 4
    return bytes(out)


def swizzled_argb(width: int, height: int, texels: bytes) -> bytes:
    """RGBA rows as the RSX's swizzled A8R8G8B8 (the NV2A's order: x and y
    bits interleaved, x first; a big-endian word each, A first)."""
    out = bytearray(width * height * 4)
    bits = width.bit_length() - 1
    for y in range(height):
        for x in range(width):
            index = 0
            for bit in range(bits):
                index |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1)
            source = (y * width + x) * 4
            red, green, blue, alpha = texels[source:source + 4]
            out[index * 4:index * 4 + 4] = bytes((alpha, red, green, blue))
    return bytes(out)


def texture(path: Path) -> tuple:
    """A PNG's mip levels for the RSX, compressed, its size and level count."""
    width, height, texels = png_read(path)
    if width != height or width & (width - 1):
        sys.exit(f"{path}: {width}x{height}, not a square power of two")
    levels, size = [], width
    while True:
        levels.append(swizzled_argb(size, size, texels))
        if size == 1:
            break
        texels = half(size, size, texels)
        size //= 2
    raw = b"".join(levels)
    return zlib.compress(raw, 9), len(raw), width, len(levels)


def c_bytes(name: str, data: bytes) -> list:
    lines = [f"static const unsigned char {name}[{len(data)}] =", "{"]
    for start in range(0, len(data), 24):
        lines.append("\t" + ", ".join(str(byte) for byte in data[start:start + 24]) + ",")
    lines.append("};")
    return lines


def embed(arguments) -> None:
    entries = json.loads(LIST.read_text())["assets"]
    lines = ["/* tools/ps3_button_assets.py embed: the PS3's button icons for hud_hires_rsx.c */", ""]
    made = {}
    for entry in entries:
        for png in entry["png"]:
            if png in made:
                continue
            compressed, raw_size, size, levels = texture(ASSETS / png)
            made[png] = (f"ps3_button_{len(made)}", len(compressed), raw_size, size, levels)
            lines += c_bytes(made[png][0], compressed)
    lines += ["", "static const struct ps3_button_embedded ps3_buttons[] =", "{"]
    for entry in entries:
        textures = ", ".join(f"{{ {made[png][0]}, {made[png][1]}, {made[png][2]} }}" for png in entry["png"])
        _, _, _, size, levels = made[entry["png"][0]]
        tag = entry["tag"].replace("\\", "\\\\")
        lines.append(f'\t{{ "{tag}", {entry["bitmap"]}, {entry["width"]}, {entry["height"]}, {entry["crc"]}U, '
                     f'{entry.get("sprites", 0)}U, {size}, {levels}, {{ {textures} }} }},')
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
