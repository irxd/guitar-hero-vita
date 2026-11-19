#!/usr/bin/env bash
# Build -> deploy -> run -> collect loop on a real PS Vita, over Wi-Fi, through
# vitacompanion (https://github.com/devnoname120/vitacompanion, MIT): FTP on
# port 1337, a text command server on port 1338.
#
# One-time setup on the Vita (see docs/port-plan.md, "Hardware loop"):
#   - vitacompanion 1.08: vitacompanion_kernel.skprx under *KERNEL and
#     vitacompanion.suprx under *main in ur0:tai/config.txt, then reboot
#     (do not load it together with vita-agent-use, which lists it as incompatible);
#   - GH Vita installed once from build-out/ghvita.vpk, or with --install (the
#     default deploy only replaces eboot.bin; use --install when sce_sys/param.sfo
#     or the LiveArea changes);
#   - the Vita on the same network as this PC, not asleep.
#
# Usage (from PowerShell):
#   wsl -d Ubuntu -u root -- env VITA_IP=192.168.1.50 bash /mnt/d/Personal/guitar-hero-vita/tools/vita-loop.sh [options]
#
# Options:
#   --build          run tools/build-vita.sh first
#   --no-deploy      keep the eboot.bin on the Vita
#   --install        install the whole VPK (OUT/ghvita.vpk) instead of replacing
#                    eboot.bin: unpacked, uploaded to ux0:data/ghvt_promote and
#                    installed by vitacompanion's promote (what VitaShell does
#                    with a VPK). Needed for LiveArea / param.sfo changes; the
#                    saves and settings in ux0:data/ghvita stay
#   --no-launch      only collect logs (from a run you started by hand)
#   --wait SECS      seconds to let the game run before collecting (default 60)
#   --input CMDS     vitacompanion input commands sent after --input-delay, e.g.
#                    'press cross; wait 100ms; release cross'
#   --input-delay S  seconds after launch before --input (default 15)
#   --keep-running   leave the game running after collecting
#   --cmd CMD        just send one command to port 1338 and exit (e.g. version)
#   --args ARGS      one-shot gd01 command line for this launch, e.g.
#                    '--song smokeonthewater --autoplay' (written to
#                    ux0:data/ghvita/args.txt; the game reads and deletes it)
#
# Output: agents/scratch/runs/<timestamp>/ with ghvita.log, memory.log (only
# written on an allocation failure), new crash dumps (decoded with
# tools/parse-core.sh) and summary.txt (perf and memory lines).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-/root/build/gd01-vita}"
TITLE_ID="GHVT00001"
IP="${VITA_IP:-}"
FTP_PORT="${VITA_FTP_PORT:-1337}"
CMD_PORT="${VITA_CMD_PORT:-1338}"

build=0 deploy=1 install=0 launch=1 wait_s=60 input="" input_delay=15 keep=0 one_cmd="" game_args=""
while [ $# -gt 0 ]; do
    case "$1" in
        --build) build=1 ;;
        --no-deploy) deploy=0 ;;
        --install) install=1 ;;
        --no-launch) launch=0; deploy=0 ;;
        --wait) wait_s="$2"; shift ;;
        --input) input="$2"; shift ;;
        --input-delay) input_delay="$2"; shift ;;
        --keep-running) keep=1 ;;
        --cmd) one_cmd="$2"; shift ;;
        --args) game_args="$2"; shift ;;
        -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
[ -n "$IP" ] || { echo "set VITA_IP to the Vita's address (Settings > Network > Wi-Fi)" >&2; exit 2; }

FTP="ftp://$IP:$FTP_PORT"

# One command line to vitacompanion; prints its reply (if any within $2
# seconds, default 3).
vcmd() {
    local reply
    exec 3<>"/dev/tcp/$IP/$CMD_PORT" || { echo "no command server at $IP:$CMD_PORT (vitacompanion loaded?)" >&2; return 1; }
    printf '%s\n' "$1" >&3
    reply="$(timeout "${2:-3}" cat <&3 | tr -d '\0' || true)"
    exec 3>&- 3<&-
    [ -z "$reply" ] || printf '  vita> %s\n' "$reply"
}

# The whole VPK: unpacked, uploaded and promoted (installed in place). The
# FTP server needs each folder made first (curl's --ftp-create-dirs does not
# work with --ftp-method nocwd). promote answers "Promoted." when done.
PROMOTE_DIR="ghvt_promote"
install_vpk() {
    local vpk="$OUT/ghvita.vpk" tmp n=0 reply
    [ -f "$vpk" ] || { echo "no $vpk: build first (--build)" >&2; return 1; }
    tmp="$(mktemp -d)"
    unzip -q "$vpk" -d "$tmp"
    echo "== install ghvita.vpk ($(stat -c %s "$vpk") bytes)"
    curl -s -Q "MKD ux0:/data/$PROMOTE_DIR" "$FTP/" -o /dev/null || true
    while IFS= read -r d; do
        curl -s -Q "MKD ux0:/data/$PROMOTE_DIR/${d#./}" "$FTP/" -o /dev/null || true
    done < <(cd "$tmp" && find . -mindepth 1 -type d | sort)
    while IFS= read -r f; do
        curl -sS --ftp-method nocwd -T "$tmp/${f#./}" "$FTP/ux0:/data/$PROMOTE_DIR/${f#./}"
        n=$((n + 1))
    done < <(cd "$tmp" && find . -type f)
    rm -rf "$tmp"
    echo "   uploaded $n files"
    reply="$(vcmd "promote ux0:data/$PROMOTE_DIR" 120)"
    printf '%s\n' "$reply"
    case "$reply" in *Promoted*) ;; *) echo "install failed (no 'Promoted.' from the Vita)" >&2; return 1 ;; esac
}

if [ -n "$one_cmd" ]; then vcmd "$one_cmd"; exit 0; fi

if [ "$build" = 1 ]; then bash "$REPO/tools/build-vita.sh"; fi

stamp="$(date +%Y%m%d-%H%M%S)"
RUN="$REPO/agents/scratch/runs/$stamp"
mkdir -p "$RUN"

# Crash dumps already on the Vita, so only new ones are fetched afterwards.
list_dumps() { curl -s --ftp-method nocwd --list-only "$FTP/ux0:/data/" | tr -d '\r' | grep '^psp2core-.*\.psp2dmp$' || true; }
before="$(list_dumps)"

if [ "$launch" = 1 ] || [ "$install" = 1 ]; then
    echo "== quit $TITLE_ID"
    vcmd "quit $TITLE_ID" || exit 1
    sleep 2
fi

if [ "$install" = 1 ]; then
    install_vpk || exit 1
    cp "$OUT/eboot.bin" "$RUN/eboot.bin" 2>/dev/null || true   # parse-core needs the matching build
    [ -f "$OUT/ghvita" ] && cp "$OUT/ghvita" "$RUN/ghvita.elf" || true   # unstripped ELF
elif [ "$deploy" = 1 ]; then
    [ -f "$OUT/eboot.bin" ] || { echo "no $OUT/eboot.bin: build first (--build)" >&2; exit 1; }
    echo "== upload eboot.bin ($(stat -c %s "$OUT/eboot.bin") bytes)"
    curl -sS --ftp-method nocwd -T "$OUT/eboot.bin" "$FTP/ux0:/app/$TITLE_ID/eboot.bin"
    cp "$OUT/eboot.bin" "$RUN/eboot.bin"   # parse-core needs the matching build
    [ -f "$OUT/ghvita" ] && cp "$OUT/ghvita" "$RUN/ghvita.elf" || true   # unstripped ELF
fi

if [ "$launch" = 1 ] && [ -n "$game_args" ]; then
    echo "== arguments: $game_args"
    printf '%s\n' "$game_args" > "$RUN/args.txt"
    curl -sS --ftp-method nocwd -T "$RUN/args.txt" "$FTP/ux0:/data/ghvita/args.txt"
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
for f in ghvita.log memory.log; do
    curl -s --ftp-method nocwd -o "$RUN/$f" "$FTP/ux0:/data/ghvita/$f" || rm -f "$RUN/$f"
    [ -s "$RUN/$f" ] || rm -f "$RUN/$f"
done

after="$(list_dumps)"
new_dumps="$(comm -13 <(printf '%s\n' "$before" | sort) <(printf '%s\n' "$after" | sort) | sed '/^$/d')"
for d in $new_dumps; do
    echo "== new crash dump: $d"
    curl -s --ftp-method nocwd -o "$RUN/$d" "$FTP/ux0:/data/$d"
    if [ -f "$RUN/ghvita.elf" ]; then
        ELF="$RUN/ghvita.elf" bash "$REPO/tools/parse-core.sh" "$RUN/$d" >"$RUN/$d.txt" 2>&1 || true
        echo "   decoded: $RUN/$d.txt"
    fi
done

if [ "$launch" = 1 ]; then
    [ "$keep" = 0 ] && { vcmd "quit $TITLE_ID" || true; }
    # Let the Vita sleep again (nosleep stays on until turned off: the LED
    # stayed green with the screen off, 2026-11-11). Also with --keep-running:
    # the game was left open but the Vita must still be able to sleep (2026-11-13).
    vcmd "nosleep off" || true
fi

{
    echo "run $stamp, wait ${wait_s}s, input: ${input:-none}"
    [ -z "$new_dumps" ] || echo "CRASH DUMPS: $new_dumps"
    if [ -f "$RUN/ghvita.log" ]; then
        echo "-- perf"
        grep -E 'vita: (perf|phases)' "$RUN/ghvita.log" || echo "(no perf lines)"
        echo "-- memory (last 3)"
        grep 'vita: memory' "$RUN/ghvita.log" | tail -n 3 || true
        echo "-- errors / warnings"
        grep -iE 'error|fail|assert|abort' "$RUN/ghvita.log" | tail -n 20 || true
        echo "-- last lines"
        tail -n 5 "$RUN/ghvita.log"
    else
        echo "no ghvita.log on the Vita"
    fi
} | tee "$RUN/summary.txt"
