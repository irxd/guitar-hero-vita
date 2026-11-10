#!/usr/bin/env bash
# Builds the unmodified upstream gd01 for Linux x86_64 inside WSL, as the
# behaviour/visual reference for the Vita port. Runs via WSLg on Windows 11.
# Build output stays in the Linux home (fast); sources are read from /mnt/d.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/build-desktop.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/upstream/gd-game-01/engine"
OUT="${OUT:-/root/build/gd01-linux}"

if ! dpkg -s libsdl3-dev >/dev/null 2>&1; then
    echo "=== Installing SDL3 + GL development packages"
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        libsdl3-dev libgl-dev libegl-dev libasound2-dev mesa-utils >/dev/null
fi

echo "=== Configuring ($SRC -> $OUT)"
cmake -S "$SRC" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "=== Building"
cmake --build "$OUT"
ls -la "$OUT/gd01"
echo "=== Done"
