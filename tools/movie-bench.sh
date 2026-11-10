#!/usr/bin/env bash
# Builds and runs vita/tests/movie_bench.cpp on the desktop against the
# user's disc image: lists the videos, then times demux+decode and the RGBA
# conversion of each given movie (or the first few).
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/movie-bench.sh [videos/<name>.pss ...]
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/upstream/gd-game-01/engine/src"
BIN=/root/build/movie_bench
ISO="$(ls "$REPO"/original_isos/*.iso | head -1)"

g++ -std=c++20 -O3 -DNDEBUG -I"$SRC" $(pkg-config --cflags sdl3) "$REPO/vita/tests/movie_bench.cpp" \
    "$SRC/data/game_data.cpp" "$SRC/data/iso9660.cpp" "$SRC/data/inflate.cpp" "$SRC/video/mpeg2.cpp" "$SRC/video/pss.cpp" \
    $(pkg-config --libs sdl3) -o "$BIN" || exit 1

if [ $# -eq 0 ]; then
    echo "=== videos on the disc"
    "$BIN" "$ISO"
    set -- $("$BIN" "$ISO" | head -3)
fi
for movie in "$@"; do "$BIN" "$ISO" "$movie" 300; done
