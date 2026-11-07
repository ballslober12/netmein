#!/bin/bash
# Build netmein (LG NetCast 4 GUI exploit) for armv7l.
# Requires: buildroot arm-lgtv-linux-gnueabi toolchain, SDL2 built statically
# with the nc4 video driver, and liblgncopenapi from webos-userland-nc4.
# Edit the paths below to match your toolchain/build layout.
set -e

# --- paths (adjust to your environment) ---
BUILDROOT=/root/arm-lgtv-linux-gnueabi_sdk-buildroot
GCC="$BUILDROOT/bin/arm-lgtv-linux-gnueabi-gcc"
OBJCOPY="$BUILDROOT/bin/arm-lgtv-linux-gnueabi-objcopy"
SDL_BUILD=/butterfart/build/netcast/sdl-static     # built with SDL_STATIC=ON + nc4 driver
PREFIX=/butterfart/build/netcast/prefix            # webos-userland-nc4 install prefix

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$SCRIPT_DIR/netmein.c"
SUC="$SCRIPT_DIR/su.c"
INC="$SCRIPT_DIR"
TMP=${TMPDIR:-/tmp}

SDL_INC="$SDL_BUILD/include/SDL2"
SDL_CONFIG_INC="$SDL_BUILD/include-config-release/SDL2"

# --- 1) build the minimal static setuid su ---
"$GCC" -static -Os -o "$TMP/su" "$SUC"

# --- 2) embed it into an object file ---
"$OBJCOPY" -I binary -O elf32-littlearm -B arm "$TMP/su" "$TMP/su_blob.o"

# --- 3) build netmein: static SDL2 (+nc4) + dynamic liblgncopenapi ---
"$GCC" -Os -Wall \
  -I"$INC" \
  -I"$SDL_INC" \
  -I"$SDL_CONFIG_INC" \
  -I"$PREFIX/include" \
  -I"$PREFIX/include/lgncopenapi" \
  -o "$TMP/netmein_gui" "$SRC" "$TMP/su_blob.o" \
  "$SDL_BUILD/libSDL2.a" \
  -L"$PREFIX/lib" -llgncopenapi -lm -ldl -lpthread \
  -Wl,--no-undefined -rdynamic

echo "BUILD_OK: $TMP/netmein_gui"
ls -la "$TMP/netmein_gui"