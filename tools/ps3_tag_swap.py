#!/usr/bin/env python3
"""Writes port/ps3/xbox/xbox_tag_swap_tables.h, the tables of the PS3 port's tag data swap
(port/ps3/xbox/xbox_tag_swap.c), from Invader's tag definitions (GPL-3.0-only):
https://github.com/SnowyMouse/invader, src/tag/hek/definition/*.json.
Usage: python3 tools/ps3_tag_swap.py --invader path/to/invader"""

import argparse
import json
import re
import subprocess
from pathlib import Path

# the operations of xbox_tag_swap.h, and their sizes in bytes
SKIP, SWAP2, SWAP4, BLOCK, REFERENCE, DATA = range(6)
OP_NAMES = ["_tag_swap_skip", "_tag_swap_2", "_tag_swap_4", "_tag_swap_block", "_tag_swap_reference",
            "_tag_swap_data"]
OP_SIZE = {SKIP: 1, SWAP2: 2, SWAP4: 4, BLOCK: 12, REFERENCE: 16, DATA: 20}

PRIMITIVE = {
    "int8": (SKIP, 1), "uint8": (SKIP, 1),
    "int16": (SWAP2, 1), "uint16": (SWAP2, 1), "Index": (SWAP2, 1),
    "int32": (SWAP4, 1), "uint32": (SWAP4, 1), "float": (SWAP4, 1), "Angle": (SWAP4, 1),
    "Fraction": (SWAP4, 1), "Pointer": (SWAP4, 1), "TagID": (SWAP4, 1), "TagFourCC": (SWAP4, 1),
    "ColorARGBInt": (SWAP4, 1), "ScenarioScriptNodeValue": (SWAP4, 1),
    "TagString": (SKIP, 32),
    "ColorARGB": (SWAP4, 4), "ColorRGB": (SWAP4, 3), "Euler2D": (SWAP4, 2), "Euler3D": (SWAP4, 3),
    "Matrix": (SWAP4, 9), "Plane2D": (SWAP4, 3), "Plane3D": (SWAP4, 4), "Point2D": (SWAP4, 2),
    "Point2DInt": (SWAP2, 2), "Point3D": (SWAP4, 3), "Quaternion": (SWAP4, 4), "Rectangle2D": (SWAP2, 4),
    "Vector2D": (SWAP4, 2), "Vector3D": (SWAP4, 3),
}

# what each data field holds in an Xbox map; FILE data is elsewhere in the map file (or absent)
DATA_KINDS = {
    "Bitmap.compressed color plate data": "file",
    "Bitmap.processed pixel data": "file",
    "Font.pixels": "bytes",
    "HUDMessageText.text data": "swap2",
    "InputDeviceDefaults.device id": "bytes",
    "InputDeviceDefaults.profile": "bytes",
    "Meter.encoded stencil": "bytes",
    "ModelAnimationsAnimation.frame info": "swap4",
    "ModelAnimationsAnimation.default data": "animation_default_data",
    "ModelAnimationsAnimation.frame data": "animation_frame_data",
    "ScenarioEditorComment.comment": "bytes",
    "ScenarioRecordedAnimation.recorded animation event stream": "recorded_animation",
    "ScenarioSourceFile.source": "bytes",
    "Scenario.editor scenario data": "bytes",
    "Scenario.script syntax data": "script_syntax",
    "Scenario.script string data": "bytes",
    "ScenarioStructureBSPMaterial.uncompressed vertices": "bsp_uncompressed_vertices",
    "ScenarioStructureBSPMaterial.compressed vertices": "bsp_compressed_vertices",
    "ScenarioStructureBSP.cluster data": "swap4",
    "ScenarioStructureBSP.sound pas data": "bytes",
    "SoundPermutation.samples": "file",
    "SoundPermutation.mouth data": "bytes",
    "SoundPermutation.subtitle data": "bytes",
    "StringListString.string": "bytes",
    "UnicodeStringListString.string": "swap2",
}

# fields that the game reads narrower than Invader defines them: ui_widget.c reads the low word of these
# flags as text_box_flags, so each word swaps alone
FIELD_UNITS = {
    ("UIWidgetDefinition", "flags 1"): [(SWAP2, 2, None)],
}

# structs with a swap step after their fields, and the field offsets that the C code reads
HOOKS = {"ModelGeometryPart": "_tag_swap_hook_model_part"}
OFFSETS = {
    "ModelGeometryPart": ["triangle count", "triangle offset", "vertex type", "vertex count", "vertex offset"],
    "ScenarioStructureBSPMaterial": ["rendered vertices count", "lightmap vertices count"],
    "ModelAnimationsAnimation": ["frame count", "frame size", "node count", "flags", "node transform flag data",
                                 "node rotation flag data", "node scale flag data", "offset to compressed data"],
    "Scenario": ["structure bsps"],
    "ScenarioBSP": ["bsp start", "bsp size", "bsp address"],
    "ScenarioRecordedAnimation": ["version", "unit control data version"],
}
# the structs that the C code needs by index
NAMED_STRUCTS = ["ModelVertexUncompressed", "ModelVertexCompressed", "ScenarioStructureBSP",
                 "ScenarioStructureBSPMaterialUncompressedRenderedVertex",
                 "ScenarioStructureBSPMaterialUncompressedLightmapVertex",
                 "ScenarioStructureBSPMaterialCompressedRenderedVertex",
                 "ScenarioStructureBSPMaterialCompressedLightmapVertex", "ScenarioBSP"]


class Definitions:
    def __init__(self, invader: Path):
        self.enums, self.bitfields, self.structs = {}, {}, {}
        self.groups = {}
        fourccs = {}
        fourcc_text = (invader / "include/invader/hek/fourcc.hpp").read_text(encoding="utf-8")
        for match in re.finditer(r"TAG_FOURCC_(\w+) = 0x([0-9A-Fa-f]+)", fourcc_text):
            fourccs[match.group(1)] = int(match.group(2), 16)
        for path in sorted((invader / "src/tag/hek/definition").glob("*.json")):
            for item in json.loads(path.read_text(encoding="utf-8")):
                if item["type"] == "enum":
                    self.enums[item["name"]] = item
                elif item["type"] == "bitfield":
                    self.bitfields[item["name"]] = item
                else:
                    self.structs[item["name"]] = item
                    if "class" in item:
                        self.groups[fourccs[item["class"].upper()]] = item["name"]
        # Invader compiles the UI widget collection with the tag collection's struct
        self.groups[fourccs["UI_WIDGET_COLLECTION"]] = "TagCollection"

    def unit(self, field):
        kind = field["type"]
        if kind in PRIMITIVE:
            return PRIMITIVE[kind]
        if kind in self.enums:
            return (SWAP2, 1)
        if kind in self.bitfields:
            width = self.bitfields[kind]["width"]
            return {8: (SKIP, 1), 16: (SWAP2, 1), 32: (SWAP4, 1)}[width]
        raise ValueError(f"unknown field type {kind}")

    def field_ops(self, struct, field):
        if (struct["name"], field.get("name")) in FIELD_UNITS:
            return FIELD_UNITS[(struct["name"], field.get("name"))]
        kind = field["type"]
        repeat = field.get("count", 1) * (2 if field.get("bounds") else 1)
        if kind == "pad":
            return [(SKIP, field["size"], None)]
        if kind == "TagReflexive":
            ops = [(BLOCK, 1, field["struct"])]
        elif kind == "TagDependency":
            ops = [(REFERENCE, 1, None)]
        elif kind == "TagDataOffset":
            name = f"{struct['name']}.{field['name']}"
            if name not in DATA_KINDS:
                raise ValueError(f"no data kind for {name}")
            ops = [(DATA, 1, DATA_KINDS[name])]
        else:
            op, count = self.unit(field)
            ops = [(op, count, None)]
        if field.get("engines") == ["mcc"]:
            # Xbox maps have padding where MCC added this field
            return [(SKIP, sum(OP_SIZE[op] * count for op, count, _ in ops) * repeat, None)]
        return ops * repeat

    def fields(self, name):
        struct = self.structs[name]
        inherited = self.fields(struct["inherits"]) if "inherits" in struct else []
        return inherited + [(struct, field) for field in struct["fields"]]

    def ops(self, name):
        ops = []
        for struct, field in self.fields(name):
            ops += self.field_ops(struct, field)
        size = sum(OP_SIZE[op] * count for op, count, _ in ops)
        if size != self.structs[name]["size"]:
            raise ValueError(f"{name} is {size} bytes, its definition says {self.structs[name]['size']}")
        merged = []
        for op, count, argument in ops:
            if merged and op in (SKIP, SWAP2, SWAP4) and merged[-1][0] == op:
                merged[-1] = (op, merged[-1][1] + count, None)
            else:
                merged.append((op, count, argument))
        return merged

    def offset(self, name, field_name):
        offset = 0
        for struct, field in self.fields(name):
            if field.get("name") == field_name:
                return offset
            offset += sum(OP_SIZE[op] * count for op, count, _ in self.field_ops(struct, field))
        raise ValueError(f"{name} has no field {field_name}")


def c_name(text):
    return re.sub(r"[^0-9A-Za-z]+", "_", text).strip("_").upper()


def fourcc_text(value):
    return "".join(chr((value >> shift) & 0xFF) for shift in (24, 16, 8, 0))


def generate(definitions: Definitions, commit: str) -> str:
    # the structs reachable from the groups, in a stable order
    order, index = [], {}

    def add(name):
        if name in index:
            return
        index[name] = None
        for op, _, argument in definitions.ops(name):
            if op == BLOCK:
                add(argument)
        index[name] = len(order)
        order.append(name)

    for fourcc in sorted(definitions.groups):
        add(definitions.groups[fourcc])
    for name in NAMED_STRUCTS + list(HOOKS):
        add(name)

    lines = [
        "/* XBOX_TAG_SWAP_TABLES.H: written by tools/ps3_tag_swap.py from Invader's tag definitions",
        f"(GPL-3.0-only, https://github.com/SnowyMouse/invader {commit}). Do not edit: rerun the tool. */",
        "",
    ]
    for name, fields in OFFSETS.items():
        for field in fields:
            lines.append(f"#define TAG_SWAP_{c_name(name)}_{c_name(field)} 0x{definitions.offset(name, field):X}")
    for name in NAMED_STRUCTS + ["ModelGeometryPart", "Scenario"]:
        lines.append(f"#define TAG_SWAP_STRUCT_{c_name(name)} {index[name]}")
    lines += ["", "static const struct tag_swap_op tag_swap_ops[] =", "{"]
    firsts = []
    count = 0
    for name in order:
        firsts.append(count)
        lines.append(f"\t/* {name} */")
        for op, op_count, argument in definitions.ops(name):
            if op == BLOCK:
                argument_text = str(index[argument])
            elif op == DATA:
                argument_text = f"_tag_swap_data_{argument}"
            else:
                argument_text = "0"
            lines.append(f"\t{{ {OP_NAMES[op]}, {argument_text}, {op_count} }},")
            count += 1
    lines += ["};", "", "static const struct tag_swap_struct tag_swap_structs[] =", "{"]
    for position, name in enumerate(order):
        op_count = len(definitions.ops(name))
        hook = HOOKS.get(name, "_tag_swap_hook_none")
        lines.append(f"\t{{ 0x{definitions.structs[name]['size']:X}, {firsts[position]}, {op_count}, {hook} }}, /* {name} */")
    lines += ["};", "", "static const struct tag_swap_group tag_swap_groups[] =", "{"]
    for fourcc in sorted(definitions.groups):
        lines.append(f"\t{{ 0x{fourcc:08X}, {index[definitions.groups[fourcc]]} }}, /* {fourcc_text(fourcc)} */")
    lines += ["};", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--invader", type=Path, required=True, help="a clone of Invader")
    parser.add_argument("--output", type=Path, default=Path("port/ps3/xbox/xbox_tag_swap_tables.h"))
    args = parser.parse_args()

    commit = subprocess.run(["git", "-C", str(args.invader), "rev-parse", "--short=12", "HEAD"],
                            capture_output=True, text=True).stdout.strip() or "unknown commit"
    text = generate(Definitions(args.invader), commit)
    args.output.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
