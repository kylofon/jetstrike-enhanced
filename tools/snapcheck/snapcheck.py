#!/usr/bin/env python3
"""snapcheck: headless regression check of the rendered frames at the original 320x240 view.

Runs the game with scripted keys (JS_KEYS) on the game-time clock (JS_VCLOCK=1) and a fixed seed, saves frames at
fixed game times (JS_SNAP_AT) and compares a hash of each frame (FNV-1a of the RGB pixels, from the engine) with
tools/snapcheck/ref.txt. Prints only the pass / fail counts (and the failing frame names).

  python tools/snapcheck/snapcheck.py            compare with ref.txt
  python tools/snapcheck/snapcheck.py --update   rewrite ref.txt from this build (only from a build known to be right)
  python tools/snapcheck/snapcheck.py --twice    run twice and compare the runs with each other (determinism check)
  options: --exe PATH (default jsport/build/jsenh.exe[/jsport.exe]), --game-dir DIR (default Game),
           --only NAME[,NAME] (scenario names), --view WxH (passed to the game; default 320x240, the regression view)

Frames land in work/snapcheck/<scenario>/at_NNN.png (look at them when a hash differs).
Scenario times are game seconds; a mission scenario takes about 40 s of real time, scenarios run in parallel.
"""
import argparse, os, subprocess, sys, shutil
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ref.txt")
WORK = os.path.join(ROOT, "work", "snapcheck")

ENTER = "1c"; DOWN = "e050"; SPACE = "39"; THROTTLE = "0b"

def keys(*items):
    return ",".join("%s:%s" % (t, k) for t, k in items)

# front end: main menu, story, briefing, plane select (campaign), then the first frames of mission 1
def flight(t0):
    """take-off script, t0 = seconds when the mission is on screen: full throttle, rotate (crashes into the base:
    explosion, ejection, parachute, restart), gun burst. Every part of it is deterministic on the game clock."""
    return [(t0, THROTTLE), (t0 + 3, DOWN + "p"), (t0 + 5.5, DOWN + "r"), (t0 + 12.5, SPACE + "p"), (t0 + 15.5, SPACE + "r")]

CAMPAIGN = [(2, ENTER), (5, ENTER), (8, ENTER), (11, ENTER)]
CAMPAIGN_SNAPS = [1, 4, 7, 10, 14, 17, 20, 23, 26, 29, 32, 35]
MISSION_SNAPS = CAMPAIGN_SNAPS[4:]

SCENARIOS = {
    # name: (JS_MISSION or None, key list, snapshot times, quit time)
    "fe_campaign": (None, CAMPAIGN, CAMPAIGN_SNAPS[:4], 12),
    "m00_jet": (0, CAMPAIGN + flight(13.5), CAMPAIGN_SNAPS, 36),
    "m04_rock": (4, CAMPAIGN + flight(13.5), MISSION_SNAPS, 36),
    "m10_jungle": (10, CAMPAIGN + flight(13.5), MISSION_SNAPS, 36),
    "m17_sea": (17, CAMPAIGN + flight(13.5), MISSION_SNAPS, 36),
    "m25_city": (25, CAMPAIGN + flight(13.5), MISSION_SNAPS, 36),
    "m73_ice": (73, CAMPAIGN + flight(13.5), MISSION_SNAPS, 36),
    # training BOMBING: cursor to the row, Enter x3 (menu, briefing, plane select)
    "t_bombing": (None, [(2, DOWN), (3, DOWN), (4, DOWN), (5, DOWN), (7, ENTER), (10, ENTER), (13, ENTER)] + flight(17),
                  [6, 9, 12, 16, 20, 24, 28, 32, 36], 38),
    # Aerolympics (bonus course): cursor to DONE (Space), Enter x3
    "aero": (None, [(2, DOWN), (4, ENTER), (6, DOWN), (7, DOWN), (8, SPACE), (11, ENTER), (14, ENTER), (17, ENTER)]
             + flight(21), [3, 6, 10, 13, 16, 19, 23, 27, 31, 35, 39], 41),
}

def find_exe(arg):
    if arg: return arg
    for n in ("jsenh.exe", "jsport.exe", "jsenh", "jsport"):
        p = os.path.join(ROOT, "jsport", "build", n)
        if os.path.exists(p): return p
    sys.exit("snapcheck: no game binary found (build jsport first, or pass --exe)")

def run_scenario(name, exe, game_dir, tag, view):
    mission, ks, snaps, quit_at = SCENARIOS[name]
    out = os.path.join(WORK, tag, name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):                    # only this scenario's own earlier output
        if f == "snap.txt" or (f.startswith("at_") and f.endswith(".png")):
            os.remove(os.path.join(out, f))
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", JS_VCLOCK="1", JS_SEED="1",
               JS_KEYS=keys(*ks), JS_SNAPSHOT_DIR=out, JS_SNAP_AT=",".join(str(s) for s in snaps),
               JS_QUIT_AFTER=str(quit_at))
    if mission is not None: env["JS_MISSION"] = str(mission)
    cmd = [exe, "--game-dir", game_dir, "--no-intro"] + (["--view", view] if view else [])
    p = subprocess.run(cmd, env=env, cwd=ROOT, capture_output=True, text=True, timeout=300)
    res = {}
    try:
        for line in open(os.path.join(out, "snap.txt")):
            n, t, h = line.split()
            res["%s/%s" % (name, n)] = h
    except OSError:
        pass
    return name, res, p.returncode, p.stderr.strip()

def run_all(names, exe, game_dir, tag, view):
    res = {}
    with ThreadPoolExecutor(max_workers=len(names)) as ex:
        for name, r, rc, err in ex.map(lambda n: run_scenario(n, exe, game_dir, tag, view), names):
            if rc != 0 or not r: print("snapcheck: scenario %s failed (exit %s) %s" % (name, rc, err[:200]))
            res.update(r)
    return res

def load_ref():
    ref = {}
    if os.path.exists(REF):
        for line in open(REF):
            if line.strip() and not line.startswith("#"):
                k, h = line.split(); ref[k] = h
    return ref

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true"); ap.add_argument("--twice", action="store_true")
    ap.add_argument("--exe"); ap.add_argument("--game-dir", default=os.path.join(ROOT, "Game"))
    ap.add_argument("--only"); ap.add_argument("--view", default="320x240")
    a = ap.parse_args()
    names = a.only.split(",") if a.only else list(SCENARIOS)
    exe = find_exe(a.exe)
    cur = run_all(names, exe, a.game_dir, "run1", a.view)
    if a.twice:
        cur2 = run_all(names, exe, a.game_dir, "run2", a.view)
        bad = sorted(k for k in set(cur) | set(cur2) if cur.get(k) != cur2.get(k))
        print("determinism: %d identical, %d different" % (len(cur) - len(bad), len(bad)))
        for k in bad: print("  differs:", k)
        return 1 if bad else 0
    if a.update:
        ref = load_ref()
        if a.only: ref.update(cur)
        else: ref = cur
        with open(REF, "w") as f:
            f.write("# snapcheck reference: <scenario>/<frame> <FNV-1a of RGB>, 320x240 view, from the faithful port\n")
            for k in sorted(ref): f.write("%s %s\n" % (k, ref[k]))
        print("snapcheck: wrote %d reference frames" % len(ref))
        return 0
    ref = load_ref()
    keys_ = sorted(k for k in ref if k.split("/")[0] in names)
    bad = [k for k in keys_ if cur.get(k) != ref[k]]
    print("snapcheck: %d passed, %d failed (of %d)" % (len(keys_) - len(bad), len(bad), len(keys_)))
    for k in bad: print("  FAIL", k, "(missing)" if k not in cur else "")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
