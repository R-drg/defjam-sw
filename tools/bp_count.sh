#!/usr/bin/env bash
# Count how often lifted functions run: tools/bp_count.sh SECS FUNC... [-- K=V ...]
# Runs headless under gdb with ignore-counted breakpoints and prints hit counts.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="$1"; shift
FUNCS=(); ENVS=()
while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; ENVS=("$@"); break; fi
    FUNCS+=("$1"); shift
done
G="$HERE/runs/bp_count.gdb"
{
    echo "set pagination off"; echo "set print thread-events off"; echo "set disable-randomization off"
    echo "handle SIGSEGV SIGBUS nostop noprint pass"
    echo "handle SIGUSR1 SIGUSR2 SIGPROF SIG34 SIG35 nostop noprint pass"
    i=1; for f in "${FUNCS[@]}"; do echo "break $f"; echo "ignore $i 100000000"; i=$((i+1)); done
    echo "run"; echo "info breakpoints"; echo "kill"
} > "$G"
env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy DEFJAM_GAME_DIR="$HERE/game" RECOMP_QUIET=1 "${ENVS[@]}" \
    gdb -q -batch -nx -x "$G" "$HERE/build/defjam_recomp" > "$HERE/runs/bp_count.txt" 2>&1 &
GP=$!
sleep "$SECS"
C=$(pgrep -P "$GP" | head -1)
[ -n "$C" ] && kill -INT "$C"
for ((i = 0; i < 30; i++)); do kill -0 "$GP" 2>/dev/null || break; sleep 1; done
kill -9 "$GP" "$C" 2>/dev/null
awk '/^[0-9]+ +breakpoint/{name=$NF} /already hit/{print $4, name; name=""} END{}' "$HERE/runs/bp_count.txt"
grep -E "^[0-9]+ +breakpoint" "$HERE/runs/bp_count.txt" | awk '{print $NF}' | tr -d '<>' | while read f; do :; done
