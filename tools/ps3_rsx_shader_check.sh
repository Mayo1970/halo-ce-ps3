#!/bin/sh
# Builds tools/ps3_rsx_shader_check.c with the host compiler of the ps3dev image and runs it: the game's
# 67 vertex shaders and random pixel shaders (KEYS, SEED), against cgcomp -a and the NV2A semantics.
repo=$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))
MSYS_NO_PATHCONV=1 docker run --rm -v "$repo:/src" -w /src -e VERBOSE -e KEYS -e SEED ps3dev-latest:latest sh -c "
mkdir -p build/ps3/tools/rsx &&
gcc -std=gnu99 -O2 -Wall -Iport/ps3/src -o build/ps3/tools/ps3_rsx_shader_check tools/ps3_rsx_shader_check.c \
	port/ps3/src/ps3_rsx_asm.c port/ps3/src/ps3_rsx_vp.c port/ps3/src/ps3_rsx_fp.c -lm &&
build/ps3/tools/ps3_rsx_shader_check /src build/ps3/tools/rsx"
