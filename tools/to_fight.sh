#!/usr/bin/env bash
# Drive a headless run (tools/remote.py) from boot into a One-on-One battle:
# skip the intro, title, autosave screens, Battle > One on One > Standard >
# P1 as guest > two fighters > first venue. Each step waits for its screen by
# time, so a slow load may need the step repeated by hand with remote.py.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
r() { python3 -I "$HERE/tools/remote.py" "$@" >/dev/null; }
r start "$@"
r wait 14; r press "0:start:200"; r wait 9; r press "0:start:300"
r wait 6;  r press "0:start:300"; r wait 5; r press "0:a:200"
r wait 4;  r press "0:a:200";     r wait 5; r press "0:right:200"
r wait 2;  r press "0:a:200";     r wait 6; r press "0:a:200"
r wait 6;  r press "0:a:200";     r wait 6; r press "0:start:200"
r wait 4;  r press "0:a:200";     r wait 5; r press "0:a:200"
r wait 7;  r press "0:a:200";     r wait 3; r press "0:right:200"
r wait 2;  r press "0:a:200";     r wait 7; r press "0:a:200"
python3 -I "$HERE/tools/remote.py" wait 20
