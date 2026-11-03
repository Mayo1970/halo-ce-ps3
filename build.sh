#!/bin/sh
# Runs make in the ps3dev-latest:latest image: ./build.sh [pkg|self|check|clean].
# After a change of image, run ./build.sh clean: mixed-toolchain objects link and then fail on hardware.
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd):/src" -w /src ps3dev-latest:latest make "$@"
