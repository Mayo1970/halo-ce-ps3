#!/usr/bin/env python3
"""Draws PS3 button icon sets in the Halo menus' style (32x32 texels, the
tilted oval of port/assets/buttons/svg) and a preview page of them."""

from pathlib import Path

OUT = Path(__file__).resolve().parent
SVG = OUT / "svg"
FONT = Path("E:/Users/Matteo/Desktop/Halo/PS3/port/assets/fonts/Overpass-900.ttf").as_uri()

OVAL = 'transform="translate(10.341 12.879)"'
RADIUS = 10.0
CENTRE = (10.341, 12.879)

# PlayStation symbol colours, and oval bodies (dark top, mid, bright bottom)
FACES = {
    "triangle": {"symbol": "#3ee0a8", "body": ("#04382a", "#0a7d55", "#16c48a")},
    "circle":   {"symbol": "#ff4f5e", "body": ("#470710", "#a01625", "#ec3040")},
    "cross":    {"symbol": "#7aa8ff", "body": ("#0a1850", "#1a44a8", "#3a7cf0")},
    "square":   {"symbol": "#ff86d4", "body": ("#460836", "#9c2878", "#e45ab4")},
}
BLACK_BODY = ("#040404", "#1c1c1c", "#3c3c3c")
GREY_BODY = ("#1e2228", "#4a515c", "#7c8694")
# the shoulder buttons, START and SELECT: darker, as on the pad
DARK_GREY_BODY = ("#0a0c0f", "#1e2228", "#363c45")
DARK_BLACK_BODY = ("#000000", "#0a0a0a", "#1e1e1e")


def symbol(name, colour, width=1.9):
    x, y = CENTRE
    s = f'stroke="{colour}" stroke-width="{width}" fill="none" stroke-linejoin="miter"'
    if name == "triangle":
        return (f'<polygon {s} points="{x:.3f},{y - 5.5:.3f} {x + 5.0:.3f},{y + 3.1:.3f} '
                f'{x - 5.0:.3f},{y + 3.1:.3f}" transform="translate(0 0.6)"/>')
    if name == "circle":
        return f'<circle {s} cx="{x}" cy="{y}" r="4.6"/>'
    if name == "cross":
        d = 4.2
        return (f'<path {s} stroke-linecap="square" d="M{x - d:.3f},{y - d:.3f} L{x + d:.3f},{y + d:.3f} '
                f'M{x + d:.3f},{y - d:.3f} L{x - d:.3f},{y + d:.3f}"/>')
    return f'<rect {s} x="{x - 4.2:.3f}" y="{y - 4.2:.3f}" width="8.4" height="8.4"/>'


def defs(body, uid, y0=-10, y1=10):
    top, mid, bottom = body
    return (f'<defs><linearGradient id="body{uid}" gradientUnits="userSpaceOnUse" x1="0" y1="{y0}" x2="0" y2="{y1}">'
            f'<stop offset="0" stop-color="{top}"/><stop offset="0.5" stop-color="{mid}"/>'
            f'<stop offset="1" stop-color="{bottom}"/></linearGradient>'
            f'<linearGradient id="rim{uid}" gradientUnits="userSpaceOnUse" x1="0" y1="-10" x2="0" y2="10">'
            f'<stop offset="0" stop-color="#ffffff" stop-opacity="0.72"/>'
            f'<stop offset="1" stop-color="#ffffff" stop-opacity="0.66"/></linearGradient></defs>')


def oval(uid):
    return (f'<g {OVAL}><circle r="{RADIUS}" fill="url(#body{uid})"/>'
            f'<circle r="{RADIUS - 0.577}" fill="none" stroke="url(#rim{uid})" stroke-width="1.154"/></g>')


def shadow(inner):
    return f'<g transform="translate(0.75 1.2)" opacity="0.55">{inner}</g>'


def face(name, style, uid):
    f = FACES[name]
    if style == "colour":
        body, colour = f["body"], "#ffffff"
        inner = symbol(name, colour)
        drop = shadow(symbol(name, "#000000"))
    else:
        body, colour = BLACK_BODY, f["symbol"]
        inner = symbol(name, colour)
        drop = shadow(symbol(name, "#000000"))
    return defs(body, uid) + oval(uid) + drop + inner


def stencil(name, uid):
    # the message sheet's sprites: a white oval with the symbol cut out, tinted by the game
    # centred in the sprite, not at the Xbox oval's place in the top left
    dx, dy = 16 - CENTRE[0], 16 - CENTRE[1]
    return (f'<g transform="translate({dx:.3f} {dy:.3f})"><defs><mask id="m{uid}"><rect x="-16" y="-16" width="64" height="64" fill="#fff"/>'
            f'{symbol(name, "#000", 2.1)}</mask></defs>'
            f'<g mask="url(#m{uid})"><g {OVAL}><circle r="{RADIUS}" fill="#fff"/></g></g></g>')


# the extras' outlines (label_icon's and label_stencil's)
SHOULDER = "M2.5,20.5 L2.5,10.5 Q2.5,5.5 8,5 L24,5 Q29.5,5.5 29.5,10.5 L29.5,20.5 Z"
TRIGGER = "M4,22 L4,9 Q4,3 10,3 L22,3 Q28,3 28,9 L28,22 Q16,25.5 4,22 Z"
START = "M4.5,7.7 Q4.5,6.5 5.6,6.9 L26.8,13.3 Q27.8,14 26.8,14.7 L5.6,21.1 Q4.5,21.5 4.5,20.3 Z"
SELECT = 'x="4.5" y="7.5" width="23" height="13" rx="1.5"'
STICK = 'cx="16" cy="14" r="11.2"'


def analog_arrows(colour):
    return "".join(
        f'<polygon points="16,{14 - 10.3:.1f} {16 + 1.7:.1f},{14 - 8.1:.1f} {16 - 1.7:.1f},{14 - 8.1:.1f}" '
        f'fill="{colour}" transform="rotate({angle} 16 14)"/>' for angle in (0, 90, 180, 270))


def label_icon(text, shape, body, uid, size=8.5):
    x, y = CENTRE
    if shape == "oval":
        outline = oval(uid)
    elif shape == "shoulder":
        outline = (f'<path d="{SHOULDER}" '
                   f'fill="url(#body{uid})" stroke="#ffffff" stroke-opacity="0.7" stroke-width="1.15"/>')
        x, y = 16, 13.4
    elif shape == "trigger":
        outline = (f'<path d="{TRIGGER}" '
                   f'fill="url(#body{uid})" stroke="#ffffff" stroke-opacity="0.7" stroke-width="1.15"/>')
        x, y = 16, 13.4
    elif shape == "stick":
        outline = (f'<circle {STICK} fill="url(#body{uid})" stroke="#ffffff" stroke-opacity="0.7" stroke-width="1.15"/>'
                   f'<circle cx="16" cy="14" r="8" fill="none" stroke="#000" stroke-opacity="0.35" stroke-width="1"/>')
        x, y = 16, 14
    elif shape == "analog":
        # the stick seen from above: L3's cap, with an arrow out of its ring each way it moves
        arrows = analog_arrows("#fff")
        outline = (f'<circle {STICK} fill="url(#body{uid})" stroke="#ffffff" stroke-opacity="0.7" stroke-width="1.15"/>'
                   f'<circle cx="16" cy="14" r="7.4" fill="none" stroke="#000" stroke-opacity="0.35" stroke-width="1"/>'
                   f'<g transform="translate(0.6 0.9)" opacity="0.55">{analog_arrows("#000")}</g>{arrows}')
        x, y = 16, 14
    elif shape == "start":
        # a wedge a little taller than SELECT and 1.5x as wide as tall, as on the pad
        outline = (f'<path d="{START}" '
                   f'fill="url(#body{uid})" stroke="#ffffff" stroke-opacity="0.7" stroke-width="1.15" stroke-linejoin="round"/>')
        return defs(body, uid, 7, 21) + outline
    elif shape == "select":
        outline = (f'<rect {SELECT} fill="url(#body{uid})" '
                   f'stroke="#ffffff" stroke-opacity="0.7" stroke-width="1.15"/>')
        return defs(body, uid, 7, 21) + outline
    t = (f'<text x="{x}" y="{y}" font-family="Overpass900" font-size="{size}" text-anchor="middle" '
         f'dominant-baseline="central" fill="#fff">{text}</text>')
    st = (f'<text x="{x + 0.6}" y="{y + 0.9}" font-family="Overpass900" font-size="{size}" text-anchor="middle" '
          f'dominant-baseline="central" fill="#000" opacity="0.55">{text}</text>')
    if shape in ("shoulder", "trigger"):
        return defs(body, uid, 3, 24) + outline + st + t
    return defs(body, uid) + outline + st + t


def label_stencil(text, shape, uid, size=8.5):
    # the HUD sheet's extras: the outline in white with the label and the dark details cut out, tinted by the game
    paint = 'fill="#fff" stroke="#fff" stroke-width="1.15"'
    x, y, cut = 16, 14, ""
    if shape == "shoulder":
        outline, y = f'<path d="{SHOULDER}" {paint}/>', 13.4
    elif shape == "trigger":
        outline, y = f'<path d="{TRIGGER}" {paint}/>', 13.4
    elif shape == "start":
        outline, text = f'<path d="{START}" {paint} stroke-linejoin="round"/>', ""
    elif shape == "select":
        outline, text = f'<rect {SELECT} {paint}/>', ""
    else:
        outline = f'<circle {STICK} {paint}/>'
        ring = 8 if shape == "stick" else 7.4
        cut = f'<circle cx="16" cy="14" r="{ring}" fill="none" stroke="#000" stroke-width="1"/>'
        if shape == "analog":
            cut += analog_arrows("#000")
    if text:
        cut += (f'<text x="{x}" y="{y}" font-family="Overpass900" font-size="{size}" text-anchor="middle" '
                f'dominant-baseline="central" fill="#000">{text}</text>')
    return (f'<defs><mask id="m{uid}"><rect x="-16" y="-16" width="64" height="64" fill="#fff"/>{cut}</mask></defs>'
            f'<g mask="url(#m{uid})">{outline}</g>')


def svg(inner, px):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{px}" height="{px}" viewBox="0 0 32 32">{inner}</svg>'


def main():
    SVG.mkdir(exist_ok=True)
    uid = [0]

    def next_uid():
        uid[0] += 1
        return uid[0]

    sets = {}
    for style in ("colour", "black"):
        sets[style] = {n: face(n, style, next_uid()) for n in FACES}
    sets["stencil"] = {n: stencil(n, next_uid()) for n in FACES}
    extras = {}
    # set A takes set B's shoulder, stick, START and SELECT icons
    for style, body, dark in (("colour", BLACK_BODY, DARK_BLACK_BODY), ("black", BLACK_BODY, DARK_BLACK_BODY)):
        extras[style] = {
            "L1": label_icon("L1", "shoulder", dark, next_uid(), 9.5),
            "R1": label_icon("R1", "shoulder", dark, next_uid(), 9.5),
            "L2": label_icon("L2", "trigger", dark, next_uid(), 9.5),
            "R2": label_icon("R2", "trigger", dark, next_uid(), 9.5),
            "L3": label_icon("L3", "stick", body, next_uid(), 8.5),
            "R3": label_icon("R3", "stick", body, next_uid(), 8.5),
            "START": label_icon("START", "start", dark, next_uid()),
            "SELECT": label_icon("SELECT", "select", dark, next_uid()),
        }
    # (after the others, which keep their ids)
    for style in ("colour", "black"):
        extras[style]["L_ANALOG"] = label_icon("L", "analog", BLACK_BODY, next_uid(), 9.5)
        extras[style]["R_ANALOG"] = label_icon("R", "analog", BLACK_BODY, next_uid(), 9.5)
    extras["stencil"] = {
        "L1": label_stencil("L1", "shoulder", next_uid(), 9.5),
        "R1": label_stencil("R1", "shoulder", next_uid(), 9.5),
        "L2": label_stencil("L2", "trigger", next_uid(), 9.5),
        "R2": label_stencil("R2", "trigger", next_uid(), 9.5),
        "L3": label_stencil("L3", "stick", next_uid(), 8.5),
        "R3": label_stencil("R3", "stick", next_uid(), 8.5),
        "START": label_stencil("", "start", next_uid()),
        "SELECT": label_stencil("", "select", next_uid()),
        "L_ANALOG": label_stencil("L", "analog", next_uid(), 9.5),
        "R_ANALOG": label_stencil("R", "analog", next_uid(), 9.5),
    }

    for style, icons in list(sets.items()) + [(f"{k}_extra", v) for k, v in extras.items()]:
        for name, inner in icons.items():
            (SVG / f"{style}_{name}.svg").write_text(svg(inner, 32))

    # inline SVGs share one id space: give each Xbox icon its own gradient ids
    xbox = {n: Path(f"E:/Users/Matteo/Desktop/Halo/PS3/port/assets/buttons/svg/{n}_butn.svg").read_text()
            .replace('id="body"', f'id="xb{n}"').replace("url(#body)", f"url(#xb{n})")
            .replace('id="rim"', f'id="xr{n}"').replace("url(#rim)", f"url(#xr{n})")
            for n in "abxy"}

    def row(icons, px):
        return "".join(f'<div class="cell">{svg(v, px)}<span>{k}</span></div>' for k, v in icons.items())

    def menu_keys(icons):
        items = [("cross", "SELECT"), ("circle", "BACK"), ("square", "DELETE"), ("triangle", "CREATE NEW")]
        return "".join(f'<span class="key">{svg(icons[n], 40)}{t}</span>' for n, t in items)

    def xbox_keys():
        items = [("a", "SELECT"), ("b", "BACK"), ("x", "DELETE"), ("y", "CREATE NEW")]
        return "".join(f'<span class="key"><span class="x">{xbox[n]}</span>{t}</span>' for n, t in items)

    html = f"""<!doctype html><html><head><meta charset="utf-8"><style>
@font-face {{ font-family: Overpass900; src: url('{FONT}'); }}
body {{ margin:0; padding:24px; background:#0d1117; color:#c9d1d9; font:14px Overpass900, sans-serif; width:1240px; }}
h2 {{ margin:18px 0 8px; font-size:17px; color:#fff; letter-spacing:1px; }}
.set {{ display:flex; gap:18px; align-items:flex-end; flex-wrap:wrap; }}
.cell {{ display:flex; flex-direction:column; align-items:center; gap:4px; font-size:11px; color:#8b949e; }}
.menu {{ background:linear-gradient(#1a2a44,#0b1424); border:1px solid #2c4870; padding:10px 16px; display:flex; gap:26px;
  align-items:center; font-size:17px; color:#dfe8ff; letter-spacing:1px; margin:6px 0; width:fit-content; }}
.key {{ display:flex; align-items:center; gap:4px; }}
.x svg {{ width:40px; height:40px; }}
.tint {{ background:#0b1424; padding:10px; }}
.tint svg {{ filter: drop-shadow(0 0 0 #000); }}
.hud {{ background:#2a3524; padding:14px 18px; font-size:18px; color:#7fc8ff; display:flex; align-items:center; gap:6px; width:fit-content; }}
.cols {{ display:flex; gap:40px; }}
.small svg {{ image-rendering: pixelated; }}
</style></head><body>
<div class="cols"><div>
<h2>SET A - coloured circles, white symbols (Halo style)</h2>
<div class="set">{row(sets['colour'], 96)}</div>
<div class="set" style="margin-top:8px">{row(extras['colour'], 64)}</div>
</div><div>
<h2>SET B - black circles, coloured symbols (DualShock style)</h2>
<div class="set">{row(sets['black'], 96)}</div>
<div class="set" style="margin-top:8px">{row(extras['black'], 64)}</div>
</div></div>

<h2>Menu key bar - original Xbox, then set A, then set B</h2>
<div class="menu">{xbox_keys()}</div>
<div class="menu">{menu_keys(sets['colour'])}</div>
<div class="menu">{menu_keys(sets['black'])}</div>

<h2>Message sheet stencils (game tints them) - in a HUD prompt</h2>
<div class="cols">
<div class="hud">Hold {svg(sets['stencil']['square'].replace('#fff"/></g></g>', '#7fc8ff"/></g></g>'), 30)} to pick up the Assault Rifle</div>
<div class="hud">Press {svg(sets['stencil']['triangle'].replace('#fff"/></g></g>', '#7fc8ff"/></g></g>'), 30)} to switch weapons</div>
<div class="set tint">{row({k: v.replace('#fff"/></g></g>', '#7fc8ff"/></g></g>') for k, v in sets['stencil'].items()}, 64)}</div>
</div>

<h2>Actual size (32x32 texels, 1x and 2x)</h2>
<div class="set small">{row(sets['colour'], 32)}{row(sets['black'], 32)}{row(sets['colour'], 64)}{row(sets['black'], 64)}</div>
</body></html>"""
    (OUT / "preview.html").write_text(html, encoding="utf-8")


if __name__ == "__main__":
    main()
