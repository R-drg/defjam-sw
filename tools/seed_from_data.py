#!/usr/bin/env python3
"""Candidate function starts from code pointers stored in data.

    python3 -I tools/seed_from_data.py > runs/data_candidates.log
    (cd xboxrecomp && ../.venv/bin/python -m tools.seed_from_log ../runs/data_candidates.log \\
        ../game/default.xbe --functions tools/disasm/output/functions.json \\
        --seeds ../config/seed_functions.json [--dry-run])

Function tables (APT's ActionScript opcode handlers at 0x2E95B8, vtables,
callback tables) hold entries the static pass never reaches; each one found
at run time cost a crash and a regen. This lists every aligned dword in a
non-executable section that points into .text at an address that is not a
known function start, sits right after padding (int3/nop) or a ret --
where functions begin -- and has a known function start beside it in the
table, as "[ICALL] Failed to resolve VA" lines, so
tools.seed_from_log's own gates (executable section, probes as a function
body) decide what is seeded.
"""
import json, os, struct, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(os.path.join(HERE, "game", "default.xbe"), "rb").read()
base = 0x10000
hdr = struct.unpack_from("<I", d, 0x120)[0]
n = struct.unpack_from("<I", d, 0x11C)[0]
secs = []
for k in range(n):
    fl, va, vs, ra, rs, name_addr = struct.unpack_from("<6I", d, hdr - base + k * 56)
    no = name_addr - base
    name = d[no:d.index(b"\0", no)].decode("latin1")
    secs.append((name, fl, va, vs, ra, rs))
text = next(s for s in secs if s[0] == ".text")
t_lo, t_hi, t_raw = text[2], text[2] + text[5], text[4]
funcs = {int(f["start"], 16) for f in json.load(open(os.path.join(
    HERE, "xboxrecomp", "tools", "disasm", "output", "functions.json")))}
seen = set()
for name, fl, va, vs, ra, rs in secs:
    if name not in (".rdata", ".data"):
        continue
    for o in range(0, rs - 3, 4):
        v = struct.unpack_from("<I", d, ra + o)[0]
        if not (t_lo <= v < t_hi) or v in funcs or v in seen:
            continue
        prev = d[t_raw + (v - t_lo) - 1]
        if prev not in (0xCC, 0x90, 0xC3, 0xC2):
            continue
        # Only inside a table of code pointers: a neighbouring dword must be a
        # known function start. Lone values that happen to land in .text
        # (0x0010004C, 0x000E000E ...) are data.
        nb = [struct.unpack_from("<I", d, ra + o + k)[0] for k in (-4, 4)
              if 0 <= o + k <= rs - 4]
        if not any(x in funcs for x in nb):
            continue
        seen.add(v)
        print("[ICALL] Failed to resolve VA 0x%08X (from data at 0x%08X in %s)" % (v, va + o, name))
print("%d candidates" % len(seen), file=sys.stderr)
