#!/usr/bin/env python3
"""Run the game under gdb and take all-thread stacks when a log pattern stops.

    python3 tools/hang_snap.py PATTERN [--quiet S] [--max S] [--out F] [--env K=V ...]

Waits for PATTERN to appear in the output, then for it to stop appearing for
--quiet seconds (default 1.5); then SIGINTs the game so gdb prints stacks,
the guest-lock state and the dispatch-lock owner. Gives up after --max s.
"""
import argparse, os, re, signal, subprocess, sys, time
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument("pattern")
ap.add_argument("--quiet", type=float, default=1.5)
ap.add_argument("--max", type=float, default=60)
ap.add_argument("--out", default=os.path.join(HERE, "runs", "hang.txt"))
ap.add_argument("--env", action="append", default=[])
a = ap.parse_args()
env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
           DEFJAM_GAME_DIR=os.path.join(HERE, "game"), RECOMP_QUIET="1")
for kv in a.env:
    k, _, v = kv.partition("=")
    env[k] = v
cmds = ["set pagination off", "set print thread-events off", "set disable-randomization off",
        "handle SIGSEGV SIGBUS nostop noprint pass",
        "handle SIGUSR1 SIGUSR2 SIGPROF SIG34 SIG35 nostop noprint pass", "run",
        "p (long)s_gil_owner_tid", "p/x (unsigned)s_gil_owner_esp",
        "p (unsigned long)s_gil_next", "p (unsigned long)s_gil_serving",
        "p *(int*)&g_dispatch_lock", "p ((long*)&g_dispatch_lock)[0]@5",
        "thread apply all bt 40", "kill"]
args = ["gdb", "-q", "-batch", "-nx"]
for c in cmds:
    args += ["-ex", c]
args.append(os.path.join(HERE, "build", "defjam_recomp"))
out = open(a.out, "wb")
g = subprocess.Popen(args, env=env, stdout=out, stderr=subprocess.STDOUT)
pat = re.compile(a.pattern.encode())
t0 = time.time(); last_n = 0; last_t = None
while time.time() - t0 < a.max and g.poll() is None:
    time.sleep(0.25)
    n = len(pat.findall(open(a.out, "rb").read()))
    if n != last_n:
        last_n, last_t = n, time.time()
    elif n and time.time() - last_t > a.quiet:
        break
print("pattern seen %d times; %s" % (last_n, "stopped -> stacks" if last_n and time.time() - t0 < a.max else "no hang caught"))
kids = subprocess.run(["pgrep", "-P", str(g.pid)], capture_output=True, text=True).stdout.split()
for k in kids:
    os.kill(int(k), signal.SIGINT)
try:
    g.wait(60)
except subprocess.TimeoutExpired:
    g.kill()
for k in kids:
    try: os.kill(int(k), signal.SIGKILL)
    except ProcessLookupError: pass
print("output in", a.out)
