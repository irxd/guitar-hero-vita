#!/usr/bin/env bash
# Runs the GLSL ES 3.00 -> 1.00 rewrite (vita/src/glsl_es100.cpp) on shader
# sources and validates the results as GLSL ES 1.00 with glslangValidator.
# Input: a folder of <id>.orig.glsl (or <id>.glsl) files as saved by the Vita
# build's shader tracing in ux0:data/gd01/shaders/.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/check-shaders.sh /mnt/d/.../shaders
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IN="${1:?usage: check-shaders.sh <shader folder>}"
OUT="${OUT:-/root/build/shader-check}"
BIN="$OUT/glsl_es100_check"

command -v glslangValidator >/dev/null || DEBIAN_FRONTEND=noninteractive apt-get install -y -qq glslang-tools >/dev/null
mkdir -p "$OUT"
g++ -std=c++20 -O1 -Wall -Wextra -I"$REPO/vita/src" \
    "$REPO/vita/tests/glsl_es100_check.cpp" "$REPO/vita/src/glsl_es100.cpp" -o "$BIN" || exit 1

pass=0 fail=0
for f in "$IN"/*.glsl; do
    case "$f" in *.es100.*) continue ;; esac
    base="$(basename "$f" .glsl)"; base="${base%.orig}"
    # Prefer the untouched source when both exist.
    [ -f "$IN/$base.orig.glsl" ] && [ "$f" != "$IN/$base.orig.glsl" ] && continue
    if grep -q 'gl_Position' "$f"; then stage=vert; else stage=frag; fi
    out="$OUT/$base.es100.$stage"
    echo "=== $base ($stage)"
    "$BIN" "$stage" "$f" "$out"
    if glslangValidator "$out" >"$OUT/$base.log" 2>&1; then
        echo "  valid GLSL ES 1.00"; pass=$((pass + 1))
    else
        sed 's/^/  /' "$OUT/$base.log"; fail=$((fail + 1))
    fi
done
echo "=== $pass valid, $fail invalid (outputs in $OUT)"
[ "$fail" -eq 0 ]
