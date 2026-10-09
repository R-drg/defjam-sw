#!/usr/bin/env bash
# Host stacks of every thread after SECS seconds of a headless run, via gdb
# (ptrace_scope=1 allows only a child, so gdb starts the process).
#
#   tools/snap.sh SECS [OUT] [KEY=VALUE ...]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="${1:-30}"; OUT="${2:-$HERE/runs/snap.txt}"; shift 2 2>/dev/null || shift $#
BIN="${BIN:-$HERE/build/defjam_recomp}"
env SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-offscreen}" SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" \
    DEFJAM_GAME_DIR="${DEFJAM_GAME_DIR:-$HERE/game}" "$@" \
    gdb -q -batch -nx \
    -ex 'set pagination off' -ex 'set print thread-events off' -ex 'set disable-randomization off' \
    -ex 'handle SIGSEGV SIGBUS nostop noprint pass' -ex 'handle SIGUSR1 SIGUSR2 SIG34 SIG35 nostop noprint pass' \
    -ex 'run' -ex 'p (long)s_gil_owner_tid' -ex 'p/x (unsigned)s_gil_owner_esp' -ex 'p (unsigned long)s_gil_next' -ex 'p (unsigned long)s_gil_serving' -ex 'info threads' -ex 'thread apply all bt 30' -ex 'kill' "$BIN" > "$OUT" 2>&1 &
G=$!
sleep "$SECS"
# SIGINT to the game: gdb stops it and runs the backtraces.
C=$(pgrep -P "$G" | head -1)
[ -n "$C" ] && kill -INT "$C"
for ((i = 0; i < 60; i++)); do kill -0 "$G" 2>/dev/null || break; sleep 1; done
kill -9 "$G" "$C" 2>/dev/null
echo "stacks in $OUT"
