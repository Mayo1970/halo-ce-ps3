#!/usr/bin/env python3
"""tools/linux_msvc_semantics.py for GCC, which rejects `#pragma weak` on a static function: the
PS3 header leaves out the inline functions that some unit declares static (ui_widget.c has some)."""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import linux_msvc_semantics as semantics  # noqa: E402

STATIC_INLINE = re.compile(
    r"\bstatic\s+(?:__inline|_inline|__forceinline|__inline__|inline)\b[^;{}()]*?\b([A-Za-z_]\w*)\s*\("
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tags", type=Path, action="append", default=[])
    parser.add_argument("--inlines", type=Path, action="append", default=[])
    args = parser.parse_args()
    tag_files = semantics.source_files(args.tags)
    inline_files = semantics.source_files(args.inlines)
    inline_functions = semantics.scan_inline_functions(inline_files, True)
    static_names = set()
    for path in inline_files:
        static_names.update(STATIC_INLINE.findall(semantics.read(path)))
    text = semantics.render(semantics.scan_tags(tag_files), inline_functions - static_names)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
        args.output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
