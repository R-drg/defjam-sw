#!/usr/bin/env python3
"""Report a DEFJAM_PROF sample file (src/prof_linux.c).

    python3 tools/prof_linux_report.py prof.txt [build/defjam_recomp] [--top N]
        [--tid TID] [--callers FUNC] [--chains N]

Per thread: sample count, then the functions by self and inclusive samples.
--callers FUNC lists who calls FUNC (by inclusive samples); --chains N prints
the N most common full call chains (innermost first) of the busiest thread.
"""
import argparse, bisect, collections, subprocess, os

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument("prof")
ap.add_argument("elf", nargs="?", default=os.path.join(HERE, "build", "defjam_recomp"))
ap.add_argument("--top", type=int, default=25)
ap.add_argument("--tid", type=int)
ap.add_argument("--callers")
ap.add_argument("--chains", type=int, default=0)
a = ap.parse_args()

syms = []
for line in subprocess.run(["nm", "-C", "--defined-only", a.elf], capture_output=True,
                           text=True).stdout.splitlines():
    p = line.split(None, 2)
    if len(p) == 3 and p[1] in "tTwW":
        syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [s[0] for s in syms]
anchor = dict((n, v) for v, n in syms)["prof_write"]

def name(off):
    i = bisect.bisect_right(addrs, off) - 1
    return syms[i][1] if i >= 0 else "?"

lines = open(a.prof).read().split("\n")
base = int(lines[0].split()[1], 16) - anchor
per = collections.defaultdict(list)
for ln in lines[1:]:
    if not ln:
        continue
    f = ln.split()
    stack = []
    for x in f[1:]:
        v = int(x, 16) - base
        stack.append(name(v - 1 if stack else v))   # return address -> call site
    # Drop the profiler's own frames.
    while stack and stack[0] in ("prof_signal", "?", "__restore_rt") or (stack and stack[0].startswith("__")):
        stack.pop(0)
        if not stack:
            break
    per[int(f[0])].append(stack)

tids = sorted(per, key=lambda t: -len(per[t]))
total = sum(len(v) for v in per.values())
print("%d samples, %d threads" % (total, len(per)))
for t in tids:
    if a.tid and t != a.tid:
        continue
    st = per[t]
    self_c = collections.Counter(s[0] for s in st if s)
    incl = collections.Counter()
    for s in st:
        for fn in set(s):
            incl[fn] += 1
    root = collections.Counter(s[-1] for s in st if s)
    print("\n== tid %d: %d samples (%.1f%%), root %s" % (t, len(st), 100.0 * len(st) / total,
                                                     root.most_common(1)[0][0] if root else "?"))
    print("  self:")
    for fn, c in self_c.most_common(a.top):
        print("    %5.1f%%  %s" % (100.0 * c / len(st), fn))
    print("  inclusive (lifted code):")
    shown = 0
    for fn, c in incl.most_common():
        if fn.startswith("sub_"):
            print("    %5.1f%%  %s" % (100.0 * c / len(st), fn))
            shown += 1
            if shown >= a.top:
                break
    if a.callers:
        cc = collections.Counter()
        for s in st:
            for i, fn in enumerate(s[:-1]):
                if fn == a.callers:
                    cc[s[i + 1]] += 1
                    break
        print("  callers of %s:" % a.callers)
        for fn, c in cc.most_common(a.top):
            print("    %5.1f%%  %s" % (100.0 * c / len(st), fn))
    if a.chains and t == (a.tid or tids[0]):
        ch = collections.Counter(" < ".join(x for x in s if x.startswith("sub_"))[:400] for s in st)
        print("  chains:")
        for k, c in ch.most_common(a.chains):
            print("    %5.1f%%  %s" % (100.0 * c / len(st), k))
