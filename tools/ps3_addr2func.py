#!/usr/bin/env python3
"""Names the functions of code addresses (a call stack from the PS3 log): ./build.sh symbols ADDRESSES="...".
On PPC64 ELFv1 a function symbol is its .opd descriptor, whose first doubleword is the code entry."""

import bisect
import os
import struct
import subprocess
import sys
import tempfile


def main() -> None:
    tools = os.path.join(os.environ.get("PS3DEV", "/usr/local/ps3dev"), "ppu", "bin")
    elf = sys.argv[1]
    sections = subprocess.run([os.path.join(tools, "ppu-readelf"), "-SW", elf], capture_output=True,
                              text=True, check=True).stdout
    opd_address = opd_size = 0
    for line in sections.splitlines():
        if " .opd " in line:
            fields = line.split("]")[1].split()
            opd_address, opd_size = int(fields[2], 16), int(fields[4], 16)
    with tempfile.TemporaryDirectory() as work:
        path = os.path.join(work, "opd.bin")
        subprocess.run([os.path.join(tools, "ppu-objcopy"), "-O", "binary", "--only-section=.opd", elf, path],
                       check=True)
        opd = open(path, "rb").read()

    entries = []
    symbols = subprocess.run([os.path.join(tools, "ppu-readelf"), "-sW", elf], capture_output=True, text=True,
                             check=True).stdout
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) < 8 or fields[3] != "FUNC":
            continue
        value = int(fields[1], 16)
        if opd_address <= value < opd_address + opd_size:
            offset = value - opd_address
            entries.append((struct.unpack(">Q", opd[offset:offset + 8])[0], fields[7]))
    entries.sort()
    keys = [entry for entry, _ in entries]
    for text in sys.argv[2:]:
        address = int(text, 16)
        index = bisect.bisect_right(keys, address) - 1
        if index < 0:
            print(f"{address:08x} ?")
        else:
            print(f"{address:08x} {entries[index][1]}+0x{address - entries[index][0]:x}")


if __name__ == "__main__":
    main()
