#!/usr/bin/env bash
# Grow config/seed_functions.json from runs until a run finds nothing new:
# run SECS headless -> tools.seed_from_log -> full regen -> rebuild.
#
#   tools/seed_cycle.sh [SECS] [MAX_ROUNDS] [K=V ...]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECS="${1:-45}"; ROUNDS="${2:-6}"; shift 2 2>/dev/null || shift $#
PY="$HERE/.venv/bin/python"
for ((r = 1; r <= ROUNDS; r++)); do
    LOG="$HERE/runs/seed_round$r.log"
    "$HERE/tools/run_test.sh" "$SECS" "$LOG" RECOMP_QUIET=1 "$@" >/dev/null
    added=$(cd "$HERE/xboxrecomp" && "$PY" -m tools.seed_from_log "$LOG" "$HERE/game/default.xbe" \
        --functions tools/disasm/output/functions.json \
        --seeds "$HERE/config/seed_functions.json" 2>/dev/null | grep -E "^\s+\+ " )
    echo "round $r: $(echo -n "$added" | grep -c .) new seed(s)"
    [ -n "$added" ] && echo "$added"
    [ -z "$added" ] && break
    "$HERE/tools/regen.sh" > "$HERE/runs/seed_regen$r.log" 2>&1 || { echo "regen failed"; exit 1; }
    cmake --build "$HERE/build" -j8 > "$HERE/runs/seed_build$r.log" 2>&1 || { echo "build failed"; exit 1; }
done
