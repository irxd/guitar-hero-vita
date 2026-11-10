#!/usr/bin/env bash
# Runs the desktop reference build of gd01 (tools/build-desktop.sh) with the
# user's own disc image. The window opens on Windows through WSLg.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/run-desktop.sh [path-to-iso]
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-/root/build/gd01-linux/gd01}"
ISO="${1:-$(ls "$REPO"/original_isos/*.iso 2>/dev/null | head -1)}"

[ -x "$BIN" ] || { echo "No build at $BIN: run tools/build-desktop.sh first" >&2; exit 1; }
[ -f "$ISO" ] || { echo "No ISO found in $REPO/original_isos/" >&2; exit 1; }

echo "=== DISPLAY=${DISPLAY:-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
glxinfo -B 2>/dev/null | grep -E 'OpenGL (renderer|core profile version)' || true
echo "=== Running $BIN \"$ISO\""
cd "$(dirname "$BIN")"
exec "$BIN" "$ISO"
