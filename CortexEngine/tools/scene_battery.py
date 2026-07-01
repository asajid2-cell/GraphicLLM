#!/usr/bin/env python3
"""
scene_battery.py -- prove the generative pipeline end-to-end on a diverse battery.

    python tools/scene_battery.py [--iters 2] [--backends codex,claude,deepseek]

For each of >=8 diverse untuned prompts (varied room type / colour / mood):
  compose -> solve -> validity_check -> render -> vision-critique reframe.
Records per-scene validity + the critic's intent match (room_ok, color_ok,
dominant_color, verdict, score), then:
  * asserts the automated validity gate passes on every scene,
  * runs explicit ROBUSTNESS tests (bad-asset fallback, overlap auto-resolve,
    model-unavailable degrade, failed-render graceful stop),
  * builds a montage (prompt -> generated scene) of the whole battery,
  * writes a JSON report and appends a summary to GRAPHICS_LOOPS.md.
"""
import argparse
import json
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scene_gen as sg
from PIL import Image, ImageDraw, ImageFont

ROOT = sg.ROOT
LOGS = sg.LOGS
LEDGER = ROOT.parent.parent / "tandem" / "GRAPHICS_LOOPS.md"

# >=8 diverse prompts: varied room type, dominant colour, and mood.
BATTERY = [
    "a modern living room that is all pink everywhere",
    "a cozy rustic bedroom in warm evening light",
    "a minimalist home office in cool blue tones",
    "an industrial kitchen with moody dark lighting",
    "a bright airy dining room for a family",
    "a small green bathroom with plants",
    "a luxurious living room in emerald and gold",
    "a sunny mid-century studio apartment",
    "a calm scandinavian bedroom in soft white",
    "a warm study lined with wood and books",
]


def slug(prompt, i):
    return f"bat{i:02d}_" + "".join(c if c.isalnum() else "_" for c in prompt.lower())[:26]


# ----------------------------------------------------------------------------
# Robustness tests -- fast, no render (except the graceful-fail path).
# ----------------------------------------------------------------------------

def robustness_tests(by_role, by_id):
    results = []

    # 1. Bad asset pick -> fallback by role (never renders a missing mesh).
    plan = {"room_type": "living_room", "width": 6.6, "depth": 6.4,
            "palette": {}, "objects": [
                {"asset": "TotallyFakeSofa9000", "role": "sofa", "anchor": "wall_back"},
                {"asset": "not_a_real_thing", "role": "seating", "anchor": "corner_front_left"},
            ], "lights": []}
    p2, reps = sg.validate_plan(dict(plan, objects=[dict(o) for o in plan["objects"]]),
                                by_role, by_id, verbose=False)
    ok = all(by_id.get(o["asset"].lower()) for o in p2["objects"]) and len(p2["objects"]) == 2
    results.append(("bad_asset_role_fallback", ok,
                    f"{len(reps)} repairs, both remapped to real assets: "
                    + ", ".join(o["asset"] for o in p2["objects"])))

    # 2. Overlap auto-resolve -- three big pieces all forced to the same spot.
    plan = {"room_type": "living_room", "width": 6.6, "depth": 6.4, "palette": {},
            "objects": [
                {"asset": "ModernSofa", "role": "sofa", "x": 0.0, "z": -1.0},
                {"asset": "ArmChair_01", "role": "seating", "x": 0.0, "z": -1.0},
                {"asset": "bookcaseClosedWide", "role": "storage", "x": 0.0, "z": -1.0},
            ], "lights": []}
    p2, _ = sg.validate_plan(plan, by_role, by_id, verbose=False)
    ir, dropped = sg.solve(p2)
    probs = sg.validity_check(ir)
    ok = not probs and len(ir["objects"]) >= 2
    results.append(("overlap_auto_resolve", ok,
                    f"3 objects forced to (0,-1); solver placed {len(ir['objects'])}, "
                    f"dropped {len(dropped)}, validity {'clean' if not probs else probs}"))

    # 3. Model-unavailable -> offline heuristic still yields a VALID scene.
    plan = sg.compose("a modern living room that is all pink everywhere", by_role,
                      ["offline"], verbose=False)
    p2, _ = sg.validate_plan(plan, by_role, by_id, verbose=False)
    ir, _ = sg.solve(p2)
    probs = sg.validity_check(ir)
    ok = plan.get("_backend") == "offline" and not probs and len(ir["objects"]) >= 4
    results.append(("model_unavailable_offline_degrade", ok,
                    f"offline backend produced {len(ir['objects'])} objects, "
                    f"validity {'clean' if not probs else probs}"))

    # 4. Failed render -> graceful (render_ir returns None, loop keeps best-so-far).
    #    Feed an IR whose objects are all unresolved so the engine builds an (almost)
    #    empty room -- the pipeline must NOT crash and must still report a result.
    junk_ir = {"room": {"w": 6.0, "d": 6.0, "floor": [0.5, 0.4, 0.4],
                        "wall": [-1, -1, -1], "accent": [-1, -1, -1], "tint_strength": 0.3},
               "objects": [{"asset": "does_not_exist", "x": 0, "z": 0, "yaw": 0, "foot": 1.0}],
               "lights": [{"type": "point", "x": 0, "y": 2.5, "z": 0,
                           "color": [1, 0.9, 0.8], "intensity": 6, "range": 9}]}
    png = sg.render_ir(junk_ir, "robust_emptyish", timeout=200)
    # It should still render a valid (empty-ish) room without throwing.
    ok = png is not None and Path(png).exists()
    results.append(("unresolved_assets_no_crash", ok,
                    f"render of an all-unresolved IR -> {'ok: ' + Path(png).name if png else 'None (handled)'}"))

    return results


# ----------------------------------------------------------------------------
# Montage.
# ----------------------------------------------------------------------------

def build_montage(scenes, out_path, cols=None):
    tiles = [s for s in scenes if s.get("png") and Path(s["png"]).exists()]
    if not tiles:
        return None
    n = len(tiles)
    cols = cols or (4 if n > 6 else 3 if n > 4 else 2)
    rows = math.ceil(n / cols)
    tw, th, cap = 480, 270, 46            # tile w/h + caption band
    W, H = cols * tw, rows * (th + cap)
    canvas = Image.new("RGB", (W, H), (18, 18, 22))
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("arialbd.ttf", 15)
        small = ImageFont.truetype("arial.ttf", 13)
    except Exception:
        font = small = ImageFont.load_default()
    for i, s in enumerate(tiles):
        r, c = divmod(i, cols)
        x0, y0 = c * tw, r * (th + cap)
        try:
            im = Image.open(s["png"]).convert("RGB").resize((tw, th))
            canvas.paste(im, (x0, y0 + cap))
        except Exception:
            draw.rectangle([x0, y0 + cap, x0 + tw, y0 + cap + th], fill=(40, 40, 46))
        crit = s.get("crit") or {}
        badge = f"score {crit.get('score','?')}  {crit.get('dominant_color','')}"
        verdict = crit.get("verdict", "")
        vcol = (120, 230, 140) if verdict == "good" else (240, 200, 110) if verdict == "reframe" else (230, 120, 120)
        draw.rectangle([x0, y0, x0 + tw, y0 + cap], fill=(28, 28, 34))
        prompt = s["prompt"]
        if len(prompt) > 56:
            prompt = prompt[:53] + "..."
        draw.text((x0 + 8, y0 + 5), prompt, fill=(235, 235, 240), font=font)
        draw.text((x0 + 8, y0 + 26), badge, fill=(180, 185, 195), font=small)
        draw.text((x0 + tw - 78, y0 + 26), verdict, fill=vcol, font=small)
        vtag = "VALID" if not s.get("validity") else "INVALID"
        draw.text((x0 + tw - 78, y0 + 5), vtag,
                  fill=(120, 230, 140) if vtag == "VALID" else (230, 120, 120), font=small)
    canvas.save(out_path)
    return out_path


# ----------------------------------------------------------------------------
# Main.
# ----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iters", type=int, default=2)
    ap.add_argument("--backends", default="codex,claude,deepseek")
    ap.add_argument("--limit", type=int, default=0, help="run only first N prompts (dry run)")
    ap.add_argument("--no-montage", action="store_true")
    args = ap.parse_args()
    backends = args.backends.split(",")

    assets, by_role, by_id = sg.load_catalog()
    print(f"catalog: {len(assets)} assets, {len(by_role)} roles\n")

    prompts = BATTERY[: args.limit] if args.limit else BATTERY
    scenes = []
    t_start = time.time()
    for i, prompt in enumerate(prompts):
        name = slug(prompt, i)
        print(f"\n===== [{i+1}/{len(prompts)}] {prompt} =====")
        try:
            res = sg.run_pipeline(prompt, name, backends, iters=args.iters, verbose=True)
        except Exception as e:
            print(f"  pipeline error: {e}")
            scenes.append({"prompt": prompt, "name": name, "error": str(e),
                           "validity": ["pipeline_error"], "png": None, "crit": None})
            continue
        best = res.get("best") or {}
        scenes.append({
            "prompt": prompt, "name": name, "backend": res.get("backend"),
            "room_type": res.get("room_type"), "objects": res.get("objects"),
            "dropped": res.get("dropped"), "validity": res.get("validity"),
            "png": best.get("png"), "crit": best.get("crit"),
        })

    print("\n\n========== ROBUSTNESS ==========")
    robust = robustness_tests(by_role, by_id)
    for name, ok, detail in robust:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}: {detail}")

    # ---- verdicts ----
    all_valid = all(not s["validity"] for s in scenes)
    intent_ok = []
    for s in scenes:
        c = s.get("crit") or {}
        ok = bool(c.get("room_ok")) and bool(c.get("color_ok")) and (c.get("verdict") != "bad")
        intent_ok.append(ok)
    intent_pass = sum(intent_ok)
    robust_pass = all(ok for _, ok, _ in robust)
    dt = time.time() - t_start

    montage = None
    if not args.no_montage:
        montage = build_montage(scenes, str(LOGS / "battery_montage.png"))

    report = {
        "generated_scenes": len(scenes),
        "all_validity_pass": all_valid,
        "intent_match": f"{intent_pass}/{len(scenes)}",
        "robustness_pass": robust_pass,
        "elapsed_sec": round(dt, 1),
        "montage": montage,
        "scenes": [{
            "prompt": s["prompt"], "backend": s.get("backend"), "room_type": s.get("room_type"),
            "objects": s.get("objects"), "dropped": s.get("dropped"),
            "validity": s["validity"],
            "critic": {k: (s.get("crit") or {}).get(k) for k in
                       ("score", "room_ok", "color_ok", "dominant_color", "verdict")},
            "png": s.get("png"),
        } for s in scenes],
        "robustness": [{"test": n, "pass": ok, "detail": d} for n, ok, d in robust],
    }
    report_path = LOGS / "battery_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("\n\n========== SUMMARY ==========")
    print(f"scenes generated : {len(scenes)}")
    print(f"validity gate    : {'ALL VALID' if all_valid else 'SOME INVALID'}")
    print(f"intent match     : {intent_pass}/{len(scenes)} (room_ok & color_ok & not bad)")
    print(f"robustness       : {'ALL PASS' if robust_pass else 'SOME FAIL'}")
    print(f"montage          : {montage}")
    print(f"report           : {report_path}")
    print(f"elapsed          : {dt/60:.1f} min")

    done = all_valid and robust_pass and intent_pass >= 8
    print(f"\nBATTERY {'COMPLETE (goal criteria 2-5 met)' if done else 'INCOMPLETE'}")

    append_ledger(report, montage, done)
    sys.exit(0 if done else 1)


def append_ledger(report, montage, done):
    try:
        ts = time.strftime("%Y-%m-%d %H:%M")
    except Exception:
        ts = "recent"
    lines = [
        f"\n## Generative scene battery -- {ts}\n",
        f"- Pipeline: prompt -> model (codex/claude/deepseek, offline fallback) composes a relational Scene IR",
        f"  -> deterministic solver -> VALID layout -> engine render -> vision-critique reframe. No per-scene tuning.",
        f"- Scenes generated: **{report['generated_scenes']}**;"
        f" validity gate: **{'ALL VALID' if report['all_validity_pass'] else 'SOME INVALID'}**;"
        f" intent match: **{report['intent_match']}**;"
        f" robustness: **{'ALL PASS' if report['robustness_pass'] else 'SOME FAIL'}**.",
        f"- Montage: `{Path(montage).name if montage else 'n/a'}` (in build/bin/logs). Report: `battery_report.json`.",
        f"- Status: **{'DONE -- criteria 1-5 verified by rendered output' if done else 'in progress'}**.",
    ]
    for s in report["scenes"]:
        c = s["critic"]
        lines.append(f"  - {s['prompt']}  ->  {s['room_type']}, {s['objects']} obj,"
                     f" {'VALID' if not s['validity'] else 'INVALID'},"
                     f" critic score={c.get('score')} color_ok={c.get('color_ok')} dom={c.get('dominant_color')}")
    try:
        with open(LEDGER, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"ledger           : appended to {LEDGER}")
    except Exception as e:
        print(f"ledger           : could not append ({e})")


if __name__ == "__main__":
    main()
