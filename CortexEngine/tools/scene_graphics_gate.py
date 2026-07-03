#!/usr/bin/env python3
"""Graphics-fidelity gate for generated exterior stills.

This complements scene_quality_gate.py. It does not claim an image is AAA; it
rejects the obvious blockout class: flat generated exteriors with disconnected
props, no terrain/contact/material pass, and no runtime evidence that the
high-quality exterior graphics path ran.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except Exception:  # pragma: no cover
    Image = None


ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "build" / "bin" / "logs"


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")[:56] or "scene"


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _prompt_flags(prompt: str) -> dict[str, bool]:
    p = prompt.lower()
    return {
        "exterior": any(w in p for w in ("lake", "river", "mountain", "campsite", "camp", "canyon", "alpine", "desert", "forest")),
        "water": any(w in p for w in ("lake", "river", "water", "shore")),
        "campsite": any(w in p for w in ("camp", "campsite")),
    }


def _read_log(path: Path | None) -> str:
    candidates: list[Path] = []
    if path:
        candidates.append(path)
    candidates.append(LOGS / "cortex_last_run.txt")
    for candidate in candidates:
        try:
            if candidate.exists():
                return candidate.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue
    return ""


def _objects(ir: dict[str, Any]) -> list[dict[str, Any]]:
    return [o for o in ir.get("objects") or [] if isinstance(o, dict)]


def _ground(ir: dict[str, Any]) -> dict[str, Any]:
    return ((ir.get("environment") or {}).get("ground") or {}) if isinstance(ir, dict) else {}


def _graphics(ir: dict[str, Any]) -> dict[str, Any]:
    env = ir.get("environment") or {}
    direct = env.get("graphics_pass")
    if isinstance(direct, dict):
        return direct
    director = ir.get("director") or {}
    nested = director.get("graphics_pass")
    return nested if isinstance(nested, dict) else {}


def _material_detail_count(ir: dict[str, Any]) -> int:
    count = 0
    for obj in _objects(ir):
        mat = obj.get("material") or {}
        if not isinstance(mat, dict):
            continue
        richness = 0
        for key in ("preset", "roughness", "normal_scale", "procedural_mask", "wetness", "specular"):
            if key in mat:
                richness += 1
        if richness >= 3:
            count += 1
    return count


def _image_metrics(path: Path | None) -> dict[str, Any]:
    if not path or not path.exists() or Image is None:
        return {}
    im = Image.open(path).convert("RGB")
    w, h = im.size
    # Lower-mid ground band: where planar terrain and ungrounded props dominate.
    box = (int(w * 0.06), int(h * 0.48), int(w * 0.94), int(h * 0.93))
    roi = im.crop(box)
    rw, rh = roi.size
    px = roi.load()
    samples = 0
    edge_sum = 0.0
    vertical_sum = 0.0
    dark_contact = 0
    for y in range(1, rh - 1, 2):
        for x in range(1, rw - 1, 2):
            r, g, b = px[x, y]
            l = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            rl, gl, bl = px[x - 1, y]
            rr, gr, br = px[x + 1, y]
            ru, gu, bu = px[x, y - 1]
            rd, gd, bd = px[x, y + 1]
            lx0 = (0.2126 * rl + 0.7152 * gl + 0.0722 * bl) / 255.0
            lx1 = (0.2126 * rr + 0.7152 * gr + 0.0722 * br) / 255.0
            ly0 = (0.2126 * ru + 0.7152 * gu + 0.0722 * bu) / 255.0
            ly1 = (0.2126 * rd + 0.7152 * gd + 0.0722 * bd) / 255.0
            gx = abs(lx1 - lx0)
            gy = abs(ly1 - ly0)
            edge_sum += math.sqrt(gx * gx + gy * gy)
            vertical_sum += gy
            if l < 0.075 and (gx + gy) > 0.055:
                dark_contact += 1
            samples += 1
    samples = max(samples, 1)
    return {
        "ground_box": box,
        "ground_edge_density": round(edge_sum / samples, 4),
        "ground_vertical_detail": round(vertical_sum / samples, 4),
        "dark_contact_fraction": round(dark_contact / samples, 4),
        "sample_count": samples,
    }


def evaluate(prompt: str, ir: dict[str, Any], png: Path | None, log_text: str) -> dict[str, Any]:
    flags = _prompt_flags(prompt)
    graphics = _graphics(ir)
    ground = _ground(ir)
    terrain = ground.get("terrain") or {}
    materials = graphics.get("materials") or {}
    contact = graphics.get("contact") or {}
    renderer = graphics.get("renderer") or {}
    image = _image_metrics(png)

    failures: list[dict[str, Any]] = []
    warnings: list[dict[str, Any]] = []

    def fail(code: str, message: str, **detail: Any) -> None:
        failures.append({"code": code, "message": message, "detail": detail})

    def warn(code: str, message: str, **detail: Any) -> None:
        warnings.append({"code": code, "message": message, "detail": detail})

    if flags["exterior"] or ir.get("setting") == "exterior":
        relief = 0.0
        if isinstance(terrain, dict):
            try:
                relief = float(terrain.get("relief_m", 0.0) or 0.0)
            except Exception:
                relief = 0.0
        has_heightfield = isinstance(terrain, dict) and terrain.get("mode") == "heightfield" and relief >= 0.22
        has_runtime_heightfield = "generative_exterior: created procedural terrain heightfield" in log_text
        if not (has_heightfield and has_runtime_heightfield):
            fail(
                "missing_terrain_relief",
                "Generated exterior lacks non-flat terrain heightfield evidence",
                terrain=terrain,
                runtime_heightfield=has_runtime_heightfield,
            )

        decals = 0
        shore_layers = 0
        if isinstance(contact, dict):
            try:
                decals = int(contact.get("decal_count", 0) or 0)
                shore_layers = int(contact.get("shore_layer_count", 0) or 0)
            except Exception:
                decals = 0
        has_runtime_contact = "generative_exterior: created contact grounding" in log_text
        if decals < 6 or (flags["water"] and shore_layers < 2) or not has_runtime_contact:
            fail(
                "missing_contact_grounding",
                "Scene lacks explicit contact/shore grounding layers",
                contact=contact,
                runtime_contact=has_runtime_contact,
            )

        rich_objects = _material_detail_count(ir)
        has_material_contract = (
            isinstance(materials, dict)
            and bool(materials.get("enabled"))
            and rich_objects >= 8
            and float(materials.get("ground_normal_scale", 0.0) or 0.0) >= 0.55
            and float(materials.get("procedural_mask", 0.0) or 0.0) >= 0.20
        )
        has_runtime_materials = "generative_exterior: graphics material pass" in log_text
        if not (has_material_contract and has_runtime_materials):
            fail(
                "missing_material_pass",
                "Generated exterior lacks authored material detail controls",
                materials=materials,
                rich_object_materials=rich_objects,
                runtime_materials=has_runtime_materials,
            )

        has_renderer_contract = (
            isinstance(renderer, dict)
            and bool(renderer.get("ssao"))
            and bool(renderer.get("ssr"))
            and bool(renderer.get("shadows"))
            and float(renderer.get("ssao_intensity", 0.0) or 0.0) >= 1.7
        )
        has_runtime_renderer = "generative_exterior: graphics renderer quality" in log_text
        if not (has_renderer_contract and has_runtime_renderer):
            fail(
                "missing_runtime_graphics_evidence",
                "No runtime evidence that AO/SSR/shadow graphics controls were applied",
                renderer=renderer,
                runtime_renderer=has_runtime_renderer,
            )

        if image:
            if image["ground_vertical_detail"] < 0.010:
                fail("low_ground_surface_detail", "Ground band has too little vertical/detail variation", image=image)
            elif image["dark_contact_fraction"] < 0.002:
                warn("weak_image_contact_metric", "Image contact-shadow metric is weak; IR/runtime contact evidence still required", image=image)
        else:
            warn("image_metrics_skipped", "PNG/Pillow unavailable; only IR/runtime graphics evidence was checked")

    return {
        "prompt": prompt,
        "passed": not failures,
        "failures": failures,
        "warnings": warnings,
        "metrics": {"image": image} if image else {},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Generated-scene graphics fidelity gate")
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--ir", required=True, type=Path)
    ap.add_argument("--png", type=Path)
    ap.add_argument("--log", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--expect-fail", action="store_true")
    args = ap.parse_args()

    ir = _load_json(args.ir)
    log_text = _read_log(args.log)
    report = evaluate(args.prompt, ir, args.png, log_text)

    out_dir = args.out or (LOGS / "scene_graphics" / _slug(args.prompt))
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "graphics_gate_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(f"graphics report: {report_path}")

    if args.expect_fail:
        required = {
            "missing_terrain_relief",
            "missing_contact_grounding",
            "missing_material_pass",
            "missing_runtime_graphics_evidence",
        }
        got = {f["code"] for f in report["failures"]}
        missing = sorted(required - got)
        if report["passed"] or missing:
            print(f"expected known-bad graphics failure codes missing: {missing}", file=sys.stderr)
            return 2
        return 0
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
