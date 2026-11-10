#!/usr/bin/env bash
# Builds our own vitaGL with extra build options for gd01. Output:
# $OUT/libvitaGL.a + vitaGL.h. tools/build-vita.sh links it when it exists
# (else the SDK's libvitaGL.a).
#
# Commit: newer than the one vdpm installs (2bdbe89, 2026-09-23, built there
# with NO_DEBUG=1 only): the vertex layout cache came after it (92888a7), and
# so did faster uniform setters and glUseProgram without dirty flags when the
# program does not change (2026-10-30).
#
#   HAVE_VERTEX_LAYOUT_CACHE=1  remembers each program's patched vertex program
#                               per vertex layout: no per-draw attribute setup
#                               and patcher lookup (gd01 draws ~400-600 meshes
#                               a frame in songs, all in interleaved VBOs).
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/build-vitagl.sh
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
COMMIT="${VITAGL_COMMIT:-e1473ff}"
SRC="${SRC:-/root/build/vitagl-src}"
OUT="${OUT:-/root/build/vitagl}"
FLAGS=(NO_DEBUG=1 HAVE_VERTEX_LAYOUT_CACHE=1)

if [ ! -d "$SRC/.git" ]; then
    git clone -q https://github.com/Rinnegatamante/vitaGL.git "$SRC"
fi
cd "$SRC"
git rev-parse -q --verify "$COMMIT^{commit}" >/dev/null || git fetch -q origin
git checkout -q "$COMMIT"
make clean >/dev/null
make "${FLAGS[@]}" -j"$(nproc)"
mkdir -p "$OUT"
cp libvitaGL.a "$OUT/"
cp source/vitaGL.h "$OUT/"
echo "vitaGL $(git rev-parse --short HEAD) built with: ${FLAGS[*]} -> $OUT"
