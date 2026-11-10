#!/usr/bin/env bash
# Build -> deploy -> run -> collect loop on a real PS Vita, over Wi-Fi, through
# vitacompanion (https://github.com/devnoname120/vitacompanion, MIT): FTP on
# port 1337, a text command server on port 1338.
#
# One-time setup on the Vita (see docs/port-plan.md, "Hardware loop"):
#   - vitacompanion 1.08: vitacompanion_kernel.skprx under *KERNEL and
#     vitacompanion.suprx under *main in ur0:tai/config.txt, then reboot
#     (do not load it together with vita-agent-use, which lists it as incompatible);
#   - gd01 installed once from build-out/gd01.vpk (this script only replaces
#     eboot.bin; reinstall the VPK when sce_sys/param.sfo or LiveArea changes);
#   - the Vita on the same network as this PC, not asleep.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- env VITA_IP=192.168.1.50 bash /mnt/d/Personal/guitar-hero-vita/tools/vita-loop.sh [options]
#
# Options:
#   --build          run tools/build-vita.sh first
#   --no-deploy      keep the eboot.bin on the Vita
#   --no-launch      only collect logs (from a run you started by hand)
#   --wait SECS      seconds to let the game run before collecting (default 60)
#   --input CMDS     vitacompanion input commands sent after --input-delay, e.g.
#                    'press cross; wait 100ms; release cross'
#   --input-delay S  seconds after launch before --input (default 15)
#   --keep-running   leave the game running after collecting
#   --cmd CMD        just send one command to port 1338 and exit (e.g. version)
#   --args ARGS      one-shot gd01 command line for this launch, e.g.
#                    '--song smokeonthewater --autoplay' (written to
#                    ux0:data/gd01/args.txt; the game reads and deletes it)
#
# Output: agents/scratch/runs/<timestamp>/ with gd01.log, memory.log (only
# written on an allocation failure), new crash dumps (decoded with
# tools/parse-core.sh) and summary.txt (perf and memory lines).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-/root/build/gd01-vita}"
TITLE_ID="GDDE00001"
IP="${VITA_IP:-}"
FTP_PORT="${VITA_FTP_PORT:-1337}"
CMD_PORT="${VITA_CMD_PORT:-1338}"

build=0 deploy=1 launch=1 wait_s=60 input="" input_delay=15 keep=0 one_cmd="" game_args=""
while [ $# -gt 0 ]; do
    case "$1" in
        --build) build=1 ;;
        --no-deploy) deploy=0 ;;
        --no-launch) launch=0; deploy=0 ;;
        --wait) wait_s="$2"; shift ;;
        --input) input="$2"; shift ;;
        --input-delay) input_delay="$2"; shift ;;
        --keep-running) keep=1 ;;
        --cmd) one_cmd="$2"; shift ;;
        --args) game_args="$2"; shift ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
[ -n "$IP" ] || { echo "set VITA_IP to the Vita's address (Settings > Network > Wi-Fi)" >&2; exit 2; }

FTP="ftp://$IP:$FTP_PORT"

# One command line to vitacompanion; prints its reply (if any within 3 s).
vcmd() {
    local reply
    exec 3<>"/dev/tcp/$IP/$CMD_PORT" || { echo "no command server at $IP:$CMD_PORT (vitacompanion loaded?)" >&2; return 1; }
    printf '%s\n' "$1" >&3
    reply="$(timeout 3 cat <&3 || true)"
    exec 3>&- 3<&-
    [ -z "$reply" ] || printf '  vita> %s\n' "$reply"
}

if [ -n "$one_cmd" ]; then vcmd "$one_cmd"; exit 0; fi

if [ "$build" = 1 ]; then bash "$REPO/tools/build-vita.sh"; fi

stamp="$(date +%Y%m%d-%H%M%S)"
RUN="$REPO/agents/scratch/runs/$stamp"
mkdir -p "$RUN"

# Crash dumps already on the Vita, so only new ones are fetched afterwards.
list_dumps() { curl -s --ftp-method nocwd --list-only "$FTP/ux0:/data/" | tr -d '\r' | grep '^psp2core-.*\.psp2dmp$' || true; }
before="$(list_dumps)"

if [ "$launch" = 1 ]; then
    echo "== quit $TITLE_ID"
    vcmd "quit $TITLE_ID" || exit 1
    sleep 2
fi

if [ "$deploy" = 1 ]; then
    [ -f "$OUT/eboot.bin" ] || { echo "no $OUT/eboot.bin: build first (--build)" >&2; exit 1; }
    echo "== upload eboot.bin ($(stat -c %s "$OUT/eboot.bin") bytes)"
    curl -sS --ftp-method nocwd -T "$OUT/eboot.bin" "$FTP/ux0:/app/$TITLE_ID/eboot.bin"
    cp "$OUT/eboot.bin" "$RUN/eboot.bin"   # parse-core needs the matching build
    [ -f "$OUT/gd01" ] && cp "$OUT/gd01" "$RUN/gd01.elf" || true   # unstripped ELF
fi

if [ "$launch" = 1 ] && [ -n "$game_args" ]; then
    echo "== arguments: $game_args"
    printf '%s\n' "$game_args" > "$RUN/args.txt"
    curl -sS --ftp-method nocwd -T "$RUN/args.txt" "$FTP/ux0:/data/gd01/args.txt"
fi

if [ "$launch" = 1 ]; then
    vcmd "nosleep on" || true
    echo "== launch $TITLE_ID"
    vcmd "launch $TITLE_ID"
    t=0
    if [ -n "$input" ]; then
        sleep "$input_delay"; t="$input_delay"
        echo "== input: $input"
        vcmd "$input"
    fi
    rest=$(( wait_s > t ? wait_s - t : 0 ))
    echo "== running for ${rest}s more"
    sleep "$rest"
fi

echo "== collect logs into $RUN"
for f in gd01.log memory.log; do
    curl -s --ftp-method nocwd -o "$RUN/$f" "$FTP/ux0:/data/gd01/$f" || rm -f "$RUN/$f"
    [ -s "$RUN/$f" ] || rm -f "$RUN/$f"
done

after="$(list_dumps)"
new_dumps="$(comm -13 <(printf '%s\n' "$before" | sort) <(printf '%s\n' "$after" | sort) | sed '/^$/d')"
for d in $new_dumps; do
    echo "== new crash dump: $d"
    curl -s --ftp-method nocwd -o "$RUN/$d" "$FTP/ux0:/data/$d"
    if [ -f "$RUN/gd01.elf" ]; then
        ELF="$RUN/gd01.elf" bash "$REPO/tools/parse-core.sh" "$RUN/$d" >"$RUN/$d.txt" 2>&1 || true
        echo "   decoded: $RUN/$d.txt"
    fi
done

if [ "$launch" = 1 ] && [ "$keep" = 0 ]; then vcmd "quit $TITLE_ID" || true; fi

{
    echo "run $stamp, wait ${wait_s}s, input: ${input:-none}"
    [ -z "$new_dumps" ] || echo "CRASH DUMPS: $new_dumps"
    if [ -f "$RUN/gd01.log" ]; then
        echo "-- perf"
        grep -E 'vita: (perf|phases)' "$RUN/gd01.log" || echo "(no perf lines)"
        echo "-- memory (last 3)"
        grep 'vita: memory' "$RUN/gd01.log" | tail -n 3 || true
        echo "-- errors / warnings"
        grep -iE 'error|fail|assert|abort' "$RUN/gd01.log" | tail -n 20 || true
        echo "-- last lines"
        tail -n 5 "$RUN/gd01.log"
    else
        echo "no gd01.log on the Vita"
    fi
} | tee "$RUN/summary.txt"
