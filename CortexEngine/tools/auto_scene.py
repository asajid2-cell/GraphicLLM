#!/usr/bin/env python3
"""Autonomous compose -> render -> critique -> fix loop for CortexEngine scenes.

Generalizes the per-scene hand-tuning we did for the living/bedroom showcases:
given ANY prompt, it composes the scene (engine recipe router), renders it
headless, has a VISION MODEL critique the resulting image (composition +
lighting), then FIXES it by driving the engine's camera/exposure override levers
(CORTEX_AUTOCAM_DOLLY / LIFT / FOV_ADD / CORTEX_AUTOEXPOSURE_MULT) and re-renders.
It iterates until the critic says the frame is good, keeping the best score.

This is how the hand-tuning becomes automatic: instead of a human picking the
hero camera + exposure per recipe, a vision critic does it from the pixels.
Run it on prompts we never tuned (office, kitchen, ...) to prove generality.

The critic is the `claude` CLI in headless vision mode (subscription-backed, no
API key). If it's unavailable it falls back to a heuristic exposure/emptiness
critic so the loop still runs.

    python tools/auto_scene.py "a modern office" --iters 6
    python tools/auto_scene.py "a modern kitchen" --iters 6 --night
"""
from __future__ import annotations
import argparse, json, math, os, re, subprocess, sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent          # CortexEngine/
RENDER = ROOT / "tools" / "render_scene.ps1"
LOGS = ROOT / "build" / "bin" / "logs"

# --- render one frame with the current override params -----------------------
def render(prompt: str, outname: str, p: dict, showcase: bool, night: bool) -> Path:
    env = dict(os.environ)
    env["CORTEX_HEADLESS"] = "1"
    if showcase: env["CORTEX_SHOWCASE"] = "1"
    else: env.pop("CORTEX_SHOWCASE", None)
    if night: env["CORTEX_SHOWCASE_NIGHT"] = "1"
    else: env.pop("CORTEX_SHOWCASE_NIGHT", None)
    env["CORTEX_AUTOCAM_DOLLY"] = f"{p['dolly']:.3f}"
    env["CORTEX_AUTOCAM_LIFT"] = f"{p['lift']:.3f}"
    env["CORTEX_AUTOCAM_FOV_ADD"] = f"{p['fov']:.2f}"
    env["CORTEX_AUTOCAM_YAW"] = f"{p['yaw']:.2f}"
    env["CORTEX_AUTOEXPOSURE_MULT"] = f"{p['exp']:.3f}"
    subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(RENDER),
         "-Prompt", prompt, "-OutName", outname],
        env=env, cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=280)
    return LOGS / f"{outname}.png"

# --- VISION critic: the claude CLI reviews the frame -------------------------
CRITIC_PROMPT = """You are a photography art-director reviewing ONE rendered 3D INTERIOR scene
for a portfolio showcase. Look ONLY at this image: {img}

Judge the CAMERA COMPOSITION and LIGHTING (not model detail). Then reply with ONLY a
minified JSON object, no prose, no code fence:
{{"score":<0-10 overall framing+lighting quality>,
"issue":"<the ONE biggest problem, <=8 words, or 'none'>",
"fix":{{"dolly":<-1|0|1>,"lift":<-1|0|1>,"pan":<-1|0|1>,"fov":<0|1>,"exposure":<-1|0|1>}},
"verdict":"<good|fix>"}}
Fix meanings: dolly +1 = move camera BACK (a foreground object crowds/blocks the view or the frame is cramped); dolly -1 = move CLOSER (subject too small/distant/empty). lift +1 = RAISE camera (floor dominates / angle too low); lift -1 = LOWER (ceiling/empty top dominates). pan +1 = turn camera RIGHT, pan -1 = turn LEFT -- use it to CENTRE a subject that crowds ONE side of the frame (subject crowding the right edge -> pan +1 to bring it toward centre). fov +1 = WIDEN (too tight). exposure +1 = brighten (too dark); -1 = darken (too bright/blown). Use 0 to keep a dimension. Be decisive: if the room is well-framed and well-lit, set verdict "good" and all fixes 0."""

def vision_critique(png: Path) -> dict | None:
    try:
        r = subprocess.run(["claude", "-p", CRITIC_PROMPT.format(img=str(png))],
                           capture_output=True, text=True, timeout=150, cwd=str(ROOT))
        out = r.stdout.strip()
        m = re.search(r"\{.*\}", out, re.DOTALL)
        if not m:
            return None
        j = json.loads(m.group(0))
        fix = j.get("fix", {})
        return dict(source="vision",
                    score=float(j.get("score", 5)),
                    issue=str(j.get("issue", "")),
                    verdict=str(j.get("verdict", "fix")),
                    fix=dict(dolly=int(fix.get("dolly", 0)), lift=int(fix.get("lift", 0)),
                             pan=int(fix.get("pan", 0)), fov=int(fix.get("fov", 0)),
                             exposure=int(fix.get("exposure", 0))))
    except Exception as e:
        print(f"  (vision critic unavailable: {e})")
        return None

# --- HEURISTIC critic (fallback): exposure + emptiness from pixels ------------
def _region(px, W, H, x0, y0, x1, y1, step=4):
    n = s = s2 = 0
    for y in range(int(y0 * H), int(y1 * H), step):
        for x in range(int(x0 * W), int(x1 * W), step):
            r, g, b = px[x, y]; l = 0.2126*r + 0.7152*g + 0.0722*b
            s += l; s2 += l*l; n += 1
    m = s / max(n, 1); return m, math.sqrt(max(s2/max(n,1) - m*m, 0.0))

def heuristic_critique(png: Path) -> dict:
    im = Image.open(png).convert("RGB"); W, H = im.size; px = im.load()
    wm, wr = _region(px, W, H, 0, 0, 1, 1)
    top, _ = _region(px, W, H, 0.15, 0, 0.85, 0.16)
    bot, _ = _region(px, W, H, 0.15, 0.84, 0.85, 1.0)
    fix = dict(dolly=0, lift=0, pan=0, fov=0, exposure=0); issue = "none"; verdict = "good"
    if wm < 92: fix["exposure"] = 1; issue = "too dark"; verdict = "fix"
    elif wm > 150: fix["exposure"] = -1; issue = "too bright"; verdict = "fix"
    elif wr < 14: issue = "flat/empty scene"; verdict = "fix"          # nothing to fix via camera
    elif top > bot + 22: fix["lift"] = -1; issue = "ceiling-heavy"; verdict = "fix"
    score = 6.0*(1 - min(abs(wm-112)/112, 1)) + 3.0*min(wr/38, 1) + 1.0
    return dict(source="heuristic", score=round(score, 2), issue=issue, verdict=verdict, fix=fix)

# --- apply the critic's fix to the override params ---------------------------
def apply_fix(p: dict, fix: dict) -> dict:
    q = dict(p)
    q["dolly"] = max(-0.8, min(p["dolly"] + 0.55 * fix["dolly"], 1.9))
    q["lift"]  = max(-0.5, min(p["lift"]  + 0.28 * fix["lift"],  1.2))
    q["yaw"]   = max(-24.0, min(p["yaw"]  + 7.0  * fix["pan"],   24.0))
    q["fov"]   = max(0.0,  min(p["fov"]   + 7.0  * fix["fov"],   24.0))
    q["exp"]   = max(0.35, min(p["exp"]   * (1 + 0.18 * fix["exposure"]), 2.4))
    return q

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prompt")
    ap.add_argument("--iters", type=int, default=6)
    ap.add_argument("--no-showcase", dest="showcase", action="store_false")
    ap.add_argument("--night", action="store_true")
    ap.add_argument("--tag", default=None)
    a = ap.parse_args()
    tag = a.tag or "auto_" + "".join(ch if ch.isalnum() else "_" for ch in a.prompt).strip("_")[:24]

    p = dict(dolly=0.0, lift=0.0, fov=0.0, yaw=0.0, exp=1.0)   # start = the recipe's hand-untuned framing
    history, best, seen = [], None, set()
    for it in range(a.iters):
        out = f"{tag}_it{it}"
        png = render(a.prompt, out, p, a.showcase, a.night)
        c = vision_critique(png) or heuristic_critique(png)
        rec = dict(iter=it, params=dict(p), critic=c["source"], score=c["score"],
                   issue=c["issue"], verdict=c["verdict"], fix=c["fix"], png=str(png))
        history.append(rec)
        print(f"[it{it}] {c['source']} score={c['score']:.1f} verdict={c['verdict']} "
              f"issue='{c['issue']}' fix={c['fix']} "
              f"params(dolly={p['dolly']:.2f},lift={p['lift']:.2f},yaw={p['yaw']:.1f},fov={p['fov']:.1f},exp={p['exp']:.2f})")
        if best is None or c["score"] > best["score"]:
            best = rec
        if c["verdict"] == "good" or all(v == 0 for v in c["fix"].values()):
            print(f"[it{it}] critic satisfied -> converged."); break
        q = apply_fix(p, c["fix"])
        key = (round(q["dolly"], 2), round(q["lift"], 2), round(q["yaw"], 1), round(q["fov"], 1), round(q["exp"], 2))
        if key in seen:                                 # oscillation / already tried -> stop
            print(f"[it{it}] fix repeats a prior state -> converged."); break
        seen.add(key); p = q

    result = dict(prompt=a.prompt, tag=tag, best=best, iterations=history)
    (LOGS / f"{tag}_log.json").write_text(json.dumps(result, indent=2))
    print(f"\nBEST score={best['score']:.1f} at iter {best['iter']} (critic={best['critic']}) params={best['params']}")
    print(f"best frame: {best['png']}\nlog: {LOGS / (tag + '_log.json')}")

if __name__ == "__main__":
    main()
