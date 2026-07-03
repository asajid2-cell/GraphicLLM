#!/usr/bin/env python3
"""Objective prompt-scene quality gate.

This is not an aesthetics judge. It catches hard failures that the old
validity_check deliberately did not cover: wrong semantic asset classes,
missing prompt-critical entities, weak rendered color intent, and incoherent
hero staging. Human review remains the final AA/AAA gate.
"""

from __future__ import annotations

import argparse
import colorsys
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except Exception:  # pragma: no cover - checked at runtime
    Image = None


ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "build" / "bin" / "logs"


EXTERIOR_WORDS = {
    "beach", "forest", "lake", "river", "mountain", "campsite", "camp",
    "desert", "canyon", "garden", "meadow", "shore", "outdoor", "alpine",
}

FORBIDDEN_EXTERIOR_ROLES = {
    "appliance", "bathroom", "bed", "desk", "electronics", "sofa",
}

FORBIDDEN_EXTERIOR_ID_TOKENS = (
    "kitchen", "fridge", "stove", "sink", "microwave", "oven", "toilet",
    "bath", "shower", "sofa", "couch", "television", "computer", "monitor",
)


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")[:48] or "scene"


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _catalog_by_id() -> dict[str, dict[str, Any]]:
    try:
        sys.path.insert(0, str(ROOT / "tools"))
        import scene_gen as sg  # type: ignore

        _, _, by_id = sg.load_catalog()
        return by_id
    except Exception:
        return {}


def _role_for(asset_id: str, by_id: dict[str, dict[str, Any]]) -> str:
    found = by_id.get(asset_id.lower())
    if found:
        return str(found.get("role") or "misc")
    low = asset_id.lower()
    if any(t in low for t in ("fridge", "stove", "sink", "kitchen", "oven")):
        return "appliance"
    if any(t in low for t in ("tent", "campfire", "canoe", "boat", "log")):
        return "camp"
    if any(t in low for t in ("tree", "pine", "oak", "willow")):
        return "tree"
    if any(t in low for t in ("rock", "boulder", "stone", "cliff", "ridge")):
        return "rock"
    return "misc"


def _prompt_flags(prompt: str) -> dict[str, bool]:
    p = prompt.lower()
    return {
        "exterior": any(w in p for w in EXTERIOR_WORDS),
        "campsite": any(w in p for w in ("campsite", "campfire", "camp site", "camp")),
        "cabin": "cabin" in p or "hut" in p,
        "mountain": any(w in p for w in ("mountain", "ridge", "alpine", "canyon")),
        "canyon": "canyon" in p,
        "water": any(w in p for w in ("lake", "river", "water", "shore", "sea", "ocean")),
        "purple_water": any(w in p for w in ("purple lake", "purple water", "violet lake", "violet water")),
        "turquoise_water": any(w in p for w in ("turquoise lake", "turquoise water", "turquoise river")),
        "blue_water": any(w in p for w in ("blue lake", "blue water", "blue river")),
        "red_rocks": "red rock" in p or "red rocks" in p,
        "dawn": any(w in p for w in ("dawn", "sunrise", "daybreak")),
        "fog": any(w in p for w in ("fog", "foggy", "mist", "misty", "haze", "hazy")),
        "storm": any(w in p for w in ("storm", "stormy")),
        "moonlight": any(w in p for w in ("moon", "moonlight")),
    }


def _objects(ir: dict[str, Any]) -> list[dict[str, Any]]:
    objs = ir.get("objects") or []
    return [o for o in objs if isinstance(o, dict)]


def _has_ridge_layer(ir: dict[str, Any], by_id: dict[str, dict[str, Any]] | None = None) -> bool:
    env = ir.get("environment") or {}
    bg = env.get("background") or ir.get("background") or {}
    if isinstance(bg, dict):
        for key in ("ridge_layers", "ridges", "mountains", "backdrops"):
            value = bg.get(key)
            if isinstance(value, list) and value:
                return True
    for o in _objects(ir):
        low = str(o.get("asset") or "").lower()
        role = _role_for(low, by_id or {})
        if role not in {"rock", "misc"}:
            continue
        if any(t in low for t in ("mountain", "ridge", "backdrop")):
            return True
    return False


def _has_water(ir: dict[str, Any]) -> bool:
    env = ir.get("environment") or {}
    water = env.get("water") or {}
    return bool(water.get("enabled"))

def _has_cabin(ir: dict[str, Any]) -> bool:
    env = ir.get("environment") or {}
    for item in env.get("structures") or []:
        if isinstance(item, dict) and str(item.get("type") or "").lower() in {"cabin", "hut"}:
            return True
    for item in (ir.get("director") or {}).get("structures") or []:
        if isinstance(item, dict) and str(item.get("type") or "").lower() in {"cabin", "hut"}:
            return True
    for o in _objects(ir):
        low = str(o.get("asset") or "").lower()
        if any(t in low for t in ("cabin", "hut", "chalet", "lodge")):
            return True
    return False


def _has_dawn(ir: dict[str, Any]) -> bool:
    env = ir.get("environment") or {}
    sun = env.get("sun") or {}
    try:
        low_sun = float(sun.get("elevation_deg", 90.0)) <= 16.0
    except Exception:
        low_sun = False
    return low_sun or env.get("sky") == "sky_sunset"


def _has_fog(ir: dict[str, Any]) -> bool:
    env = ir.get("environment") or {}
    fog = env.get("fog") or {}
    try:
        return float(fog.get("density", 0.0)) >= 0.010
    except Exception:
        return False

def _has_storm_controls(ir: dict[str, Any]) -> bool:
    env = ir.get("environment") or {}
    fog = env.get("fog") or {}
    water = env.get("water") or {}
    try:
        return float(fog.get("density", 0.0)) >= 0.016 or float(water.get("roughness", 0.0)) >= 0.14
    except Exception:
        return False


def _has_moonlight(ir: dict[str, Any]) -> bool:
    env = ir.get("environment") or {}
    sun = env.get("sun") or {}
    try:
        color = sun.get("color") or []
        intensity = float(sun.get("intensity", 99.0))
        if len(color) >= 3:
            r, g, b = float(color[0]), float(color[1]), float(color[2])
            return b > r * 1.18 and b >= g and intensity <= 1.8
    except Exception:
        pass
    return str((ir.get("director") or {}).get("scene_type") or "").lower().find("moon") >= 0


def _has_canyon_intent(ir: dict[str, Any]) -> bool:
    director = ir.get("director") or {}
    scene_type = str(director.get("scene_type") or "").lower()
    if "canyon" in scene_type:
        return True
    env = ir.get("environment") or {}
    ground = env.get("ground") or {}
    color = ground.get("color") or []
    try:
        return len(color) >= 3 and float(color[0]) > float(color[1]) * 1.35 and float(color[0]) > float(color[2]) * 1.9
    except Exception:
        return False


def _has_red_rock_evidence(ir: dict[str, Any]) -> bool:
    rock_like = 0
    for o in _objects(ir):
        low = str(o.get("asset") or "").lower()
        if not any(t in low for t in ("rock", "boulder", "stone")):
            continue
        tint = o.get("tint") or []
        try:
            if len(tint) >= 3 and float(tint[0]) > float(tint[1]) * 1.35 and float(tint[0]) > float(tint[2]) * 1.9:
                rock_like += 1
        except Exception:
            pass
    return rock_like >= 3 or _has_canyon_intent(ir)


def _hero_assets(ir: dict[str, Any]) -> list[dict[str, Any]]:
    heroes = []
    for o in _objects(ir):
        low = str(o.get("asset") or "").lower()
        if any(t in low for t in ("tent", "campfire", "fire", "log", "lantern")):
            heroes.append(o)
    return heroes


def _hero_cluster_ok(ir: dict[str, Any]) -> tuple[bool, dict[str, Any]]:
    heroes = _hero_assets(ir)
    if len(heroes) < 3:
        return False, {"hero_count": len(heroes)}
    points = []
    for h in heroes:
        try:
            points.append((float(h.get("x")), float(h.get("z")), str(h.get("asset"))))
        except Exception:
            pass
    if len(points) < 3:
        return False, {"hero_count": len(heroes), "positioned": len(points)}
    E = float(((ir.get("environment") or {}).get("ground") or {}).get("extent", 34.0) or 34.0)
    centroid_x = sum(p[0] for p in points) / len(points)
    centroid_z = sum(p[1] for p in points) / len(points)
    max_pair = 0.0
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            max_pair = max(max_pair, math.hypot(points[i][0] - points[j][0], points[i][1] - points[j][1]))
    ok = abs(centroid_x) <= 0.22 * E and -0.18 * E <= centroid_z <= 0.24 * E and max_pair <= 0.42 * E
    return ok, {
        "hero_count": len(heroes),
        "centroid": [round(centroid_x, 3), round(centroid_z, 3)],
        "max_pair_distance": round(max_pair, 3),
        "assets": [p[2] for p in points],
    }


def _sample_roi(image_path: Path, out_dir: Path | None) -> dict[str, Any]:
    if Image is None:
        raise RuntimeError("Pillow is required for PNG quality checks")
    im = Image.open(image_path).convert("RGB")
    w, h = im.size
    # Default water/horizon band for current generative exterior camera.
    box = (
        int(w * 0.22),
        int(h * 0.30),
        int(w * 0.96),
        int(h * 0.49),
    )
    roi = im.crop(box)
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)
        roi.save(out_dir / "roi_water.png")
    pixels = list(roi.getdata())
    step = max(1, len(pixels) // 60000)
    pixels = pixels[::step]
    avg = [sum(c[i] for c in pixels) / (255.0 * len(pixels)) for i in range(3)]
    hsv = [colorsys.rgb_to_hsv(*(c / 255.0 for c in px)) for px in pixels]
    avg_sat = sum(v[1] for v in hsv) / len(hsv)
    purple_like = 0
    turquoise_like = 0
    blue_like = 0
    for r, g, b in [(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0) for c in pixels]:
        if b > g * 1.06 and r > g * 0.68 and max(r, b) - g > 0.035:
            purple_like += 1
        if g > r * 1.20 and b > r * 1.24 and abs(g - b) < 0.28 and max(g, b) - r > 0.075:
            turquoise_like += 1
        if b > r * 1.18 and b > g * 1.08 and b - min(r, g) > 0.06:
            blue_like += 1
    return {
        "box": box,
        "avg_rgb": [round(v, 4) for v in avg],
        "avg_saturation": round(avg_sat, 4),
        "purple_fraction": round(purple_like / max(len(pixels), 1), 4),
        "turquoise_fraction": round(turquoise_like / max(len(pixels), 1), 4),
        "blue_fraction": round(blue_like / max(len(pixels), 1), 4),
        "pixel_count": len(pixels),
    }


def _sample_frame(image_path: Path) -> dict[str, Any]:
    if Image is None:
        raise RuntimeError("Pillow is required for PNG quality checks")
    im = Image.open(image_path).convert("RGB")
    pixels = list(im.getdata())
    step = max(1, len(pixels) // 100000)
    pixels = pixels[::step]
    luma = sorted((0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0 for r, g, b in pixels)
    avg_rgb = [sum(c[i] for c in pixels) / (255.0 * max(len(pixels), 1)) for i in range(3)]
    cool_fraction = sum(
        (b / 255.0) >= (r / 255.0) * 1.02 and (b / 255.0) >= (g / 255.0) * 0.88
        for r, g, b in pixels
    ) / max(len(pixels), 1)
    avg_luma = sum(luma) / max(len(luma), 1)
    p05 = luma[int(len(luma) * 0.05)] if luma else 0.0
    p95 = luma[int(len(luma) * 0.95)] if luma else 0.0
    nonblack = sum(v > 0.02 for v in luma) / max(len(luma), 1)
    return {
        "avg_luma": round(avg_luma, 4),
        "p05_luma": round(p05, 4),
        "p95_luma": round(p95, 4),
        "contrast_span": round(p95 - p05, 4),
        "nonblack_fraction": round(nonblack, 4),
        "avg_rgb": [round(v, 4) for v in avg_rgb],
        "cool_fraction": round(cool_fraction, 4),
        "pixel_count": len(pixels),
    }


def _frame_visible_ok(metrics: dict[str, Any]) -> bool:
    return (
        metrics["avg_luma"] >= 0.035
        and metrics["p95_luma"] >= 0.090
        and metrics["nonblack_fraction"] >= 0.65
        and metrics["contrast_span"] >= 0.025
    )


def _moonlight_render_ok(metrics: dict[str, Any]) -> bool:
    r, _g, b = metrics["avg_rgb"]
    return b >= r * 0.96 and metrics["cool_fraction"] >= 0.34


def _purple_water_ok(metrics: dict[str, Any]) -> bool:
    r, g, b = metrics["avg_rgb"]
    return (
        b >= g * 1.08
        and r >= g * 0.70
        and metrics["avg_saturation"] >= 0.18
        and metrics["purple_fraction"] >= 0.30
    )

def _turquoise_water_ok(metrics: dict[str, Any]) -> bool:
    r, g, b = metrics["avg_rgb"]
    return (
        g >= r * 1.12
        and b >= r * 1.14
        and metrics["avg_saturation"] >= 0.14
        and metrics["turquoise_fraction"] >= 0.22
    )


def _blue_water_ok(metrics: dict[str, Any]) -> bool:
    r, g, b = metrics["avg_rgb"]
    return b >= r * 1.10 and b >= g * 1.03 and metrics["avg_saturation"] >= 0.12


def evaluate(prompt: str, ir: dict[str, Any], png: Path | None, out_dir: Path | None, require_ir_only: bool) -> dict[str, Any]:
    flags = _prompt_flags(prompt)
    by_id = _catalog_by_id()
    failures: list[dict[str, Any]] = []
    warnings: list[dict[str, Any]] = []

    def fail(code: str, message: str, **detail: Any) -> None:
        failures.append({"code": code, "message": message, "detail": detail})

    def warn(code: str, message: str, **detail: Any) -> None:
        warnings.append({"code": code, "message": message, "detail": detail})

    if flags["exterior"] or ir.get("setting") == "exterior":
        forbidden = []
        for o in _objects(ir):
            asset = str(o.get("asset") or "")
            role = _role_for(asset, by_id)
            low = asset.lower()
            if role in FORBIDDEN_EXTERIOR_ROLES or any(t in low for t in FORBIDDEN_EXTERIOR_ID_TOKENS):
                forbidden.append({"asset": asset, "role": role})
        if forbidden:
            fail("forbidden_asset_class", "Exterior scene contains indoor/domain-wrong assets", assets=forbidden)

    missing = []
    if flags["campsite"] and not _hero_assets(ir):
        missing.append("campsite_hero_set")
    if flags["cabin"] and not _has_cabin(ir):
        missing.append("cabin_structure")
    if flags["mountain"] and not _has_ridge_layer(ir, by_id):
        missing.append("mountain_or_ridge_layer")
    if flags["water"] and not _has_water(ir):
        missing.append("waterbody")
    if flags["canyon"] and not _has_canyon_intent(ir):
        missing.append("canyon_scene_intent")
    if flags["red_rocks"] and not _has_red_rock_evidence(ir):
        missing.append("red_rock_materials")
    if flags["dawn"] and not _has_dawn(ir):
        missing.append("dawn_low_sun_or_sunset_sky")
    if flags["fog"] and not _has_fog(ir):
        missing.append("fog_density")
    if flags["storm"] and not _has_storm_controls(ir):
        missing.append("storm_fog_or_rough_water")
    if flags["moonlight"] and not _has_moonlight(ir):
        missing.append("blue_moonlight_low_key")
    if missing:
        fail("missing_prompt_entity", "Prompt-critical entities or mood controls are missing", missing=missing)

    if flags["campsite"]:
        ok, detail = _hero_cluster_ok(ir)
        if not ok:
            fail("focal_visibility_fail", "Campsite hero assets are missing or not staged as a coherent focal cluster", **detail)

    roi_metrics = None
    frame_metrics = None
    if png and png.exists():
        frame_metrics = _sample_frame(png)
        if not _frame_visible_ok(frame_metrics):
            fail("render_blank_or_underlit", "Rendered frame is blank or too underlit to inspect", frame=frame_metrics)
        if flags["moonlight"] and not _moonlight_render_ok(frame_metrics):
            fail("moonlight_render_coolness_fail", "Rendered frame does not carry a visible cool moonlight grade", frame=frame_metrics)
        if flags["purple_water"] or flags["turquoise_water"] or flags["blue_water"]:
            roi_metrics = _sample_roi(png, out_dir)
            if flags["purple_water"] and not _purple_water_ok(roi_metrics):
                fail("purple_water_roi_fail", "Rendered lake/water ROI does not read as purple", roi=roi_metrics)
            if flags["turquoise_water"] and not _turquoise_water_ok(roi_metrics):
                fail("turquoise_water_roi_fail", "Rendered lake/river ROI does not read as turquoise", roi=roi_metrics)
            if flags["blue_water"] and not _blue_water_ok(roi_metrics):
                fail("blue_water_roi_fail", "Rendered lake/river ROI does not read as blue", roi=roi_metrics)
    elif not require_ir_only and (flags["purple_water"] or flags["turquoise_water"] or flags["blue_water"]):
        fail("missing_render_for_visual_gate", "PNG is required to verify rendered water color")
    elif require_ir_only:
        warn("visual_checks_skipped", "IR-only mode skips rendered color/composition checks")

    passed = not failures
    metrics: dict[str, Any] = {}
    if roi_metrics:
        metrics["water_roi"] = roi_metrics
    if frame_metrics:
        metrics["frame"] = frame_metrics
    return {
        "prompt": prompt,
        "passed": passed,
        "failures": failures,
        "warnings": warnings,
        "metrics": metrics,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Objective prompt-scene quality gate")
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--ir", required=True, type=Path)
    ap.add_argument("--png", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--expect-fail", action="store_true")
    ap.add_argument("--require-ir-only", action="store_true")
    ap.add_argument("--no-png", action="store_true")
    args = ap.parse_args()

    ir = _load_json(args.ir)
    png = None if args.no_png else args.png
    out_dir = args.out
    if out_dir is None:
        out_dir = LOGS / "scene_quality" / _slug(args.prompt)
    report = evaluate(args.prompt, ir, png, out_dir, args.require_ir_only)
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "quality_gate_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(f"quality report: {report_path}")

    if args.expect_fail:
        required = {
            "forbidden_asset_class",
            "missing_prompt_entity",
            "purple_water_roi_fail",
            "focal_visibility_fail",
        }
        got = {f["code"] for f in report["failures"]}
        missing = sorted(required - got)
        if report["passed"] or missing:
            print(f"expected known-bad failure codes missing: {missing}", file=sys.stderr)
            return 2
        return 0
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
