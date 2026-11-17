#!/bin/sh
# Builds game.elf with the PS2SDK toolchain (no make needed).
# Inside Docker:  docker run --rm -v "$PWD":/src -w /src ps2dev/ps2dev sh build.sh
set -e
CC=mips64r5900el-ps2-elf-gcc
CFLAGS="-D_EE -G0 -O2 -Wall -I$PS2SDK/ee/include -I$PS2SDK/common/include -I$GSKIT/include -Isrc"
for f in main_ps2 rt render game; do $CC $CFLAGS -c src/$f.c -o src/$f.o; done
$CC -T$PS2SDK/ee/startup/linkfile -O2 -o game.elf src/main_ps2.o src/rt.o src/render.o src/game.o \
  -L$PS2SDK/ee/lib -L$GSKIT/lib -Wl,-zmax-page-size=128 -lgskit -ldmakit -lpad -lm
echo "OK: game.elf"
