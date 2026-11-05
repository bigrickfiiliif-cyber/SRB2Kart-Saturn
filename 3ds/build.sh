#!/bin/sh
# Build the 3DS port inside the official devkitPro docker image.
#   ./3ds/build.sh          -> 3ds/srb2kart-saturn.3dsx
#   ./3ds/build.sh clean
cd "$(dirname "$0")/.." || exit 1
exec docker run --rm -v "$PWD":/src -w /src/3ds devkitpro/devkitarm \
	make -j"$(nproc)" -k "$@"
