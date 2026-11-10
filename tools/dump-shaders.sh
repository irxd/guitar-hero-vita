#!/usr/bin/env bash
# Dumps every shader gd01 compiles, as the GLSL ES 3.00 sources the Vita build
# receives: the desktop build (tools/build-desktop.sh) runs --shadertest with
# GD01_GLES=1 under an LD_PRELOAD shim (tools/shader-dump/dump_shaders.c).
# Then runs tools/check-shaders.sh on them (GLSL ES 1.00 rewrite + validator).
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/dump-shaders.sh
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-/root/build/gd01-linux/gd01}"
OUT="${OUT:-/root/build/shader-dump}"

gcc -shared -fPIC -O1 -Wall -o "$OUT.so" "$REPO/tools/shader-dump/dump_shaders.c" -ldl || exit 1
rm -rf "$OUT" && mkdir -p "$OUT"
(cd "$(dirname "$BIN")" && GD01_GLES=1 GD01_SHADER_DUMP="$OUT" LD_PRELOAD="$OUT.so" "$BIN" --shadertest)
echo "=== $(ls "$OUT" | wc -l) shader sources dumped to $OUT"
bash "$REPO/tools/check-shaders.sh" "$OUT"
