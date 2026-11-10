#!/usr/bin/env bash
# Decodes a PS Vita crash dump (ux0:data/psp2core-*.psp2dmp) against the
# matching gd01 ELF: crashing thread, registers, and the stack as
# function + source line. Installs xyzz/vita-parse-core on first use.
#
# The dump must come from the SAME build as $ELF (rebuilds change addresses).
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- bash /mnt/d/Personal/guitar-hero-vita/tools/parse-core.sh /mnt/d/path/to/psp2core-....psp2dmp
set -euo pipefail

export VITASDK="${VITASDK:-/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
TOOL="${TOOL:-/root/vita-src/vita-parse-core}"
ELF="${ELF:-/root/build/gd01-vita/gd01}"
DUMP="${1:?usage: parse-core.sh <psp2dmp file>}"

if [ ! -f "$TOOL/main.py" ]; then
    echo "=== Installing vita-parse-core"
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq python3-pyelftools >/dev/null
    git clone --depth 1 https://github.com/xyzz/vita-parse-core "$TOOL"
fi
[ -f "$ELF" ] || { echo "No ELF at $ELF: build first (tools/build-vita.sh)" >&2; exit 1; }

# vita-parse-core resolves source lines with arm-vita-eabi-addr2line from PATH.
python3 "$TOOL/main.py" "$DUMP" "$ELF"
