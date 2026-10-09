#!/usr/bin/env bash
# One headless Linux test run: start the build, wait SECS, kill it by PID
# (the process ignores SIGTERM and may rename itself), keep the log.
#
#   tools/run_test.sh SECS [LOG] [KEY=VALUE ...]
#
# Defaults: offscreen SDL video, dummy audio, game/ as the disc,
# build/defjam_recomp (BIN= to override). Extra KEY=VALUE pairs are passed as
# environment. One run at a time: runs share the frame-dump paths.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="${1:-30}"; LOG="${2:-$HERE/runs/run.log}"; shift 2 2>/dev/null || shift $#
BIN="${BIN:-$HERE/build/defjam_recomp}"
mkdir -p "$(dirname "$LOG")"
env SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-offscreen}" SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" \
    DEFJAM_GAME_DIR="${DEFJAM_GAME_DIR:-$HERE/game}" "$@" "$BIN" > "$LOG" 2>&1 &
P=$!
for ((i = 0; i < SECS; i++)); do
    kill -0 "$P" 2>/dev/null || break
    sleep 1
done
if kill -0 "$P" 2>/dev/null; then
    # SIGUSR2 first: DEFJAM_PROF writes its samples (src/prof_linux.c).
    kill -USR2 "$P" 2>/dev/null; sleep 2
    kill -9 "$P" 2>/dev/null; wait "$P" 2>/dev/null
    echo "killed after ${SECS}s; log $LOG ($(wc -l < "$LOG") lines)"
else
    wait "$P"; echo "exited with $? after ${i}s; log $LOG ($(wc -l < "$LOG") lines)"
fi
