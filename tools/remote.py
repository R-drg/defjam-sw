#!/usr/bin/env python3
"""Drive a running headless game by hand: look, press, look again.

    python3 -I tools/remote.py start [K=V ...]   launch (background), frames every 0.5 s
                                                 (BIN=path picks another build)
    python3 -I tools/remote.py press STEPS        e.g. "0:a:200" or "0:ldown:250,600:a:200"
    python3 -I tools/remote.py shot [OUT.png]     newest frame -> runs/remote/shot.png
    python3 -I tools/remote.py wait SECS          sleep, then shot
    python3 -I tools/remote.py stop

STEPS use the RECOMP_PAD_SCRIPT syntax with times relative to now (the game
reads the command file through RECOMP_PAD_CMD, usb_gamepad.c). Only the
newest frames are kept on disk.
"""
import glob, os, signal, subprocess, sys, time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(HERE, "runs", "remote")
PIDF = os.path.join(DIR, "pid")
CMD = os.path.join(DIR, "cmd")


def prune(keep=6):
    fs = sorted(glob.glob(os.path.join(DIR, "f*.bmp")))
    for f in fs[:-keep]:
        try:
            os.remove(f)
        except OSError:
            pass
    return fs[-keep:]


def shot(out=None):
    from PIL import Image
    fs = prune()
    if not fs:
        print("no frame yet")
        return
    out = out or os.path.join(DIR, "shot.png")
    for f in reversed(fs[-3:]):            # the newest may still be written
        try:
            Image.open(f).convert("RGB").resize((640, 480)).save(out)
        except OSError:
            continue
        print(os.path.basename(f), "->", out)
        return
    print("no complete frame")


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == "start":
        os.makedirs(DIR, exist_ok=True)
        for f in glob.glob(os.path.join(DIR, "f*.bmp")):
            os.remove(f)
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
                   DEFJAM_GAME_DIR=os.path.join(HERE, "game"), RECOMP_QUIET="1",
                   RECOMP_PAD_CMD=CMD, RECOMP_GL_DUMP=os.path.join(DIR, "f") + ",30")
        for kv in a[1:]:
            k, _, v = kv.partition("=")
            env[k] = v
        log = open(os.path.join(DIR, "log.txt"), "wb")
        exe = os.environ.get("BIN") or os.path.join(HERE, "build", "defjam_recomp")
        p = subprocess.Popen([exe], env=env,
                             stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        open(PIDF, "w").write(str(p.pid))
        print("started", p.pid)
    elif a[0] == "press":
        with open(CMD + ".tmp", "w") as f:
            f.write(a[1])
        os.replace(CMD + ".tmp", CMD)
        print("pressed", a[1])
    elif a[0] == "shot":
        shot(a[1] if len(a) > 1 else None)
    elif a[0] == "wait":
        time.sleep(float(a[1]))
        shot()
    elif a[0] == "stop":
        try:
            pid = int(open(PIDF).read())
            os.kill(pid, signal.SIGKILL)
            print("stopped", pid)
        except (OSError, ValueError) as e:
            print("not running:", e)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
