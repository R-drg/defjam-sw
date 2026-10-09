#!/usr/bin/env python3
"""Run the game headless and read guest memory while it runs.

    python3 tools/peek_run.py SECS ADDR[:LEN] ... [--every S] [--env K=V ...]

ADDR is a guest VA (hex), or @PTR+OFF to read at [PTR]+OFF (PTR, OFF hex),
or &SYMBOL[:LEN] for a host symbol of the binary (nm). Every --every seconds (default: once, at SECS) the
dwords at each ADDR are printed. The game is our child, so /proc/<pid>/mem is
readable under ptrace_scope=1. Log goes to runs/peek.log.
"""
import argparse, os, re, signal, subprocess, sys, time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument("secs", type=float)
ap.add_argument("addrs", nargs="+")
ap.add_argument("--every", type=float, default=0)
ap.add_argument("--env", action="append", default=[])
ap.add_argument("--log", default=os.path.join(HERE, "runs", "peek.log"))
a = ap.parse_args()

env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
           DEFJAM_GAME_DIR=os.path.join(HERE, "game"), RECOMP_QUIET="1")
for kv in a.env:
    k, _, v = kv.partition("=")
    env[k] = v
log = open(a.log, "wb")
p = subprocess.Popen([os.path.join(HERE, "build", "defjam_recomp")], env=env,
                     stdout=log, stderr=subprocess.STDOUT)
off = None
t0 = time.time()
try:
    while off is None and time.time() - t0 < 20:
        time.sleep(0.2)
        m = re.search(rb"host offset 0x([0-9A-Fa-f]+)", open(a.log, "rb").read())
        if m:
            off = int(m.group(1), 16)
    if off is None:
        sys.exit("no host offset in the log")
    mem = open("/proc/%d/mem" % p.pid, "rb", buffering=0)
    specs = []
    for s in a.addrs:
        va, _, n = s.partition(":")
        specs.append((va, int(n or "4", 0)))

    host_syms = {}
    exe = os.path.join(HERE, "build", "defjam_recomp")
    for ln in subprocess.run(["nm", exe], capture_output=True, text=True).stdout.splitlines():
        f = ln.split()
        if len(f) == 3:
            host_syms[f[2]] = int(f[0], 16)
    pie = None
    for ln in open("/proc/%d/maps" % p.pid):
        if ln.rstrip().endswith("defjam_recomp") and " 00000000 " in ln:
            pie = int(ln.split("-")[0], 16)
            break

    def resolve(va):
        if va.startswith("&"):
            return host_syms[va[1:]] + pie - off
        if not va.startswith("@"):
            return int(va, 16)
        ptr, _, o = va[1:].partition("+")
        mem.seek(off + int(ptr, 16))
        return int.from_bytes(mem.read(4), "little") + int(o or "0", 16)
    nxt = a.every or a.secs
    while nxt <= a.secs + 1e-6:
        time.sleep(max(0, nxt - (time.time() - t0)))
        line = ["t=%5.1f" % (time.time() - t0)]
        for spec, n in specs:
            va = resolve(spec)
            if va >= 0x100000000 - n:
                line.append("%s: <bad pointer>" % spec)
                continue
            mem.seek(off + va)
            b = mem.read(n)
            words = " ".join("%08X" % int.from_bytes(b[i:i + 4], "little") for i in range(0, n - n % 4, 4))
            line.append("%08X: %s" % (va, words))
        print("  ".join(line), flush=True)
        if not a.every:
            break
        nxt += a.every
finally:
    p.send_signal(signal.SIGKILL)
    p.wait()
