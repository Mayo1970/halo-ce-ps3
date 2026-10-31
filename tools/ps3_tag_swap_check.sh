#!/bin/sh
# Builds tools/ps3_tag_swap_check.c with the host compiler of the ps3dev image, and runs it on every map
# of a folder of Xbox maps. Usage (Git Bash): tools/ps3_tag_swap_check.sh "E:/path/to/maps" [map names]
if [ -z "$1" ]; then
	echo "usage: $0 <folder of Xbox maps> [map names, default all]" >&2
	exit 2
fi
maps=$(cygpath -m "$1" 2>/dev/null || echo "$1")
shift
repo=$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))
names=${*:-"*"}
MSYS_NO_PATHCONV=1 docker run --rm -v "$repo:/src" -v "$maps:/maps:ro" -w /src ps3dev-latest:latest sh -c "
mkdir -p build/ps3/tools &&
gcc -O2 -Wall -Wno-unused-variable -Iport/ps3/xbox -o build/ps3/tools/ps3_tag_swap_check \
	tools/ps3_tag_swap_check.c port/ps3/xbox/xbox_tag_swap.c -lz &&
cd /maps && /src/build/ps3/tools/ps3_tag_swap_check \$(for name in $names; do ls \$name.map 2>/dev/null || ls \$name; done)"
