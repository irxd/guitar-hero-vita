#!/usr/bin/env bash
# Measures gd01's GPU memory (textures, render targets) on the desktop build
# with the Vita's settings (internal_res 448, no depth of field, 960x544
# window), so the Vita's video memory use can be studied without hardware.
# Starts a song directly (--skip-boot --song), runs for $SECONDS_TO_RUN s,
# then prints the report written by tools/gl-probe/gl_probe.c.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/gl-probe.sh [song-id]
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-/root/build/gd01-linux/gd01}"
SONG="${1:-iloverockandroll}"
RUN_SECONDS="${SECONDS_TO_RUN:-90}"
WORK=/root/build/gl-probe
ISO="$(ls "$REPO"/original_isos/*.iso | head -1)"

mkdir -p "$WORK"
gcc -shared -fPIC -O1 -Wall -o "$WORK/gl_probe.so" "$REPO/tools/gl-probe/gl_probe.c" -ldl || exit 1

# A separate settings folder with the Vita profile's values (patches/0001).
export XDG_DATA_HOME="$WORK/xdg"
PREF="$XDG_DATA_HOME/gd-engine/gd01"
mkdir -p "$PREF"
cat > "$PREF/enhanced.json" <<'EOF'
{"internal_res": "448", "bokeh": "none", "max_fps": "60", "fps_counter": true}
EOF

rm -f "$WORK/report.txt"
(cd "$(dirname "$BIN")" && GD01_GLES=1 GD01_WINDOW=960x544 GD01_GL_PROBE="$WORK/report.txt" LD_PRELOAD="$WORK/gl_probe.so" \
    timeout --signal=INT "$RUN_SECONDS" "$BIN" --skip-boot --song "$SONG" "$ISO" >"$WORK/stdout.txt" 2>&1)
echo "=== report ($WORK/report.txt)"
cat "$WORK/report.txt"
