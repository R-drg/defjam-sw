#!/usr/bin/env python3
"""Strings referenced from the given lifted functions (sub_XXXXXXXX or hex).

    python3 tools/fn_strings.py sub_00077740 0x000D7700 ...
"""
import bisect, json, os, sys
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "xboxrecomp", "tools", "disasm", "output")
s = json.load(open(os.path.join(OUT, "strings.json")))
fs = json.load(open(os.path.join(OUT, "functions.json")))
rng = sorted((int(f["start"], 16), int(f["end"], 16), f["name"]) for f in fs)
starts = [r[0] for r in rng]
def fn(va):
    i = bisect.bisect_right(starts, va) - 1
    return rng[i][2] if i >= 0 and rng[i][0] <= va < rng[i][1] else None
want = {a if a.startswith("sub_") else "sub_%08X" % int(a, 16) for a in sys.argv[1:]}
for e in s:
    for r in e.get("referenced_from", []):
        f = fn(int(r, 16))
        if f in want:
            print(f, r, repr(e["string"][:90]))
