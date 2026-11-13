#!/usr/bin/env bash
# Builds and runs vita/tests/movie_fast_check.cpp on the desktop: the accuracy
# of the Vita movie fast paths (integer IDCT, fast colour conversion) against
# the exact ones. Needs no disc image.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/movie-fast-check.sh
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN=/root/build/movie_fast_check
mkdir -p "$(dirname "$BIN")"

g++ -std=c++20 -O2 -Wall -Wextra -I"$REPO/vita/src" $(pkg-config --cflags sdl3) \
    "$REPO/vita/tests/movie_fast_check.cpp" "$REPO/vita/src/mpeg2_neon.cpp" \
    $(pkg-config --libs sdl3) -o "$BIN" || exit 1
"$BIN"
