#!/usr/bin/env python3
"""Analyze Scene-Local Cinematic Renderer reflection-owner packet views.

The renderer's debug view 46 encodes reflection ownership with stable colors:

- black: no meaningful reflection owner
- blue: SSR
- magenta: ray-traced reflection
- yellow: visible/prelit IBL
- green: scene-local neutral/local fallback
- gray: background / no scene depth

This tool turns those colors into packet-level counts so the release gate can
reason about ownership without relying on screenshots alone.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from PIL import Image


OWNER_COLORS: dict[str, tuple[int, int, int]] = {
    "no_owner": (0, 0, 0),
    "ssr": (13, 210, 255),
    "rt_reflection": (242, 102, 242),
    "visible_ibl": (255, 220, 13),
    "scene_local_fallback": (13, 178, 56),
    "background": (46, 46, 46),
}

OWNER_COLOR_DISTANCE_LIMITS: dict[str, float] = {
    "no_owner": 48.0,
    "background": 46.0,
    "ssr": 115.0,
    "rt_reflection": 115.0,
    "visible_ibl": 115.0,
    "scene_local_fallback": 95.0,
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def resolve_path(path: str, base: Path) -> Path:
    p = Path(path)
    if p.is_absolute():
        return p
    return (base / p).resolve()


def color_distance(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
    return math.sqrt(sum((float(a[i]) - float(b[i])) ** 2 for i in range(3)))


def classify_pixel(rgb: tuple[int, int, int]) -> str:
    r, g, b = rgb
    if max(rgb) <= 28:
        return "no_owner"
    if abs(r - g) <= 18 and abs(g - b) <= 18 and 24 <= r <= 86:
        return "background"
    if r >= 210 and g >= 145 and b <= 90:
        return "visible_ibl"
    if r >= 170 and b >= 170 and g <= 155:
        return "rt_reflection"
    if b >= 170 and g >= 80 and r <= 95:
        return "ssr"
    if g >= 120 and r <= 80 and b <= 105:
        return "scene_local_fallback"

    nearest_name = "unknown"
    nearest_dist = 1e9
    for name, color in OWNER_COLORS.items():
        dist = color_distance(rgb, color)
        if dist < nearest_dist:
            nearest_name = name
            nearest_dist = dist
    if nearest_dist <= OWNER_COLOR_DISTANCE_LIMITS.get(nearest_name, 80.0):
        return nearest_name
    return "unknown"


def analyze_image(path: Path, *, max_dimension: int = 512) -> dict[str, Any]:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        original_width, original_height = rgb.size
        if max(original_width, original_height) > max_dimension:
            scale = max_dimension / float(max(original_width, original_height))
            size = (
                max(1, int(round(original_width * scale))),
                max(1, int(round(original_height * scale))),
            )
            rgb = rgb.resize(size, Image.Resampling.NEAREST)

        counts = {name: 0 for name in [*OWNER_COLORS.keys(), "unknown"]}
        width, height = rgb.size
        pixels = rgb.load()
        for y in range(height):
            for x in range(width):
                counts[classify_pixel(pixels[x, y])] += 1

    pixel_count = max(1, width * height)
    ratios = {key: counts[key] / float(pixel_count) for key in counts}
    reflection_signal = (
        ratios["ssr"]
        + ratios["rt_reflection"]
        + ratios["visible_ibl"]
        + ratios["scene_local_fallback"]
    )
    dominant_owner = max(ratios.items(), key=lambda item: item[1])[0]
    return {
        "image": str(path),
        "original_width": original_width,
        "original_height": original_height,
        "sample_width": width,
        "sample_height": height,
        "pixel_count": pixel_count,
        "counts": counts,
        "ratios": ratios,
        "dominant_owner": dominant_owner,
        "reflection_signal_ratio": reflection_signal,
    }


def is_enclosed_scene(result: dict[str, Any]) -> bool:
    svc = result.get("scene_visual_contract") or {}
    return bool(svc.get("enclosed_scene"))


def visible_hdri_allowed(result: dict[str, Any]) -> bool:
    svc = result.get("scene_visual_contract") or {}
    return bool(svc.get("visible_external_hdri_allowed"))


def analyze_manifest(
    manifest_path: Path,
    *,
    max_unknown_ratio: float,
    max_enclosed_visible_ibl_ratio: float,
    min_reflection_signal_ratio: float,
    max_dimension: int,
) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    manifest_base = manifest_path.parent
    owner_rows = [
        row
        for row in manifest.get("results", [])
        if str(row.get("view", "")) == "reflection_owner"
    ]

    failures: list[str] = []
    family_reports: list[dict[str, Any]] = []
    aggregate_counts = {name: 0 for name in [*OWNER_COLORS.keys(), "unknown"]}

    if not owner_rows:
        failures.append("reflection_owner_view_missing")

    for row in owner_rows:
        capture_raw = str(row.get("capture") or "")
        family = str(row.get("family") or "unknown")
        row_failures: list[str] = []

        if row.get("debug_view") != 46:
            row_failures.append(f"debug_view {row.get('debug_view')} != 46")

        if not capture_raw:
            failures.append(f"{family}:reflection_owner_capture_missing")
            continue
        capture = resolve_path(capture_raw, manifest_base)
        if not capture.exists():
            failures.append(f"{family}:reflection_owner_capture_not_found:{capture}")
            continue

        image_report = analyze_image(capture, max_dimension=max_dimension)
        for key, value in image_report["counts"].items():
            aggregate_counts[key] += int(value)

        ratios = image_report["ratios"]
        if ratios["unknown"] > max_unknown_ratio:
            row_failures.append(
                f"unknown_ratio {ratios['unknown']:.6f} > {max_unknown_ratio:.6f}"
            )
        if (
            is_enclosed_scene(row)
            and not visible_hdri_allowed(row)
            and ratios["visible_ibl"] > max_enclosed_visible_ibl_ratio
        ):
            row_failures.append(
                "enclosed_visible_ibl_ratio "
                f"{ratios['visible_ibl']:.6f} > {max_enclosed_visible_ibl_ratio:.6f}"
            )
        if image_report["reflection_signal_ratio"] < min_reflection_signal_ratio:
            row_failures.append(
                "reflection_signal_ratio "
                f"{image_report['reflection_signal_ratio']:.6f} < {min_reflection_signal_ratio:.6f}"
            )

        for failure in row_failures:
            failures.append(f"{family}:{failure}")

        family_reports.append(
            {
                "family": family,
                "debug_view": row.get("debug_view"),
                "capture": str(capture),
                "profile_id": (row.get("scene_visual_contract") or {}).get("profile_id"),
                "enclosed_scene": is_enclosed_scene(row),
                "visible_external_hdri_allowed": visible_hdri_allowed(row),
                "image": image_report,
                "status": "FAIL" if row_failures else "PASS",
                "failures": row_failures,
            }
        )

    aggregate_pixels = max(1, sum(aggregate_counts.values()))
    aggregate_ratios = {
        key: aggregate_counts[key] / float(aggregate_pixels) for key in aggregate_counts
    }
    report = {
        "schema": "cortex.scene_local_cinematic_renderer_v1.reflection_owner_analysis",
        "manifest": str(manifest_path),
        "owner_debug_view_mode": 46,
        "thresholds": {
            "max_unknown_ratio": max_unknown_ratio,
            "max_enclosed_visible_ibl_ratio": max_enclosed_visible_ibl_ratio,
            "min_reflection_signal_ratio": min_reflection_signal_ratio,
            "max_dimension": max_dimension,
        },
        "status": "FAIL" if failures else "PASS",
        "failure_count": len(failures),
        "failures": failures,
        "family_count": len(family_reports),
        "families": family_reports,
        "family_summary": [
            {
                "family": item["family"],
                "status": item["status"],
                "profile_id": item["profile_id"],
                "enclosed_scene": item["enclosed_scene"],
                "visible_external_hdri_allowed": item["visible_external_hdri_allowed"],
                "dominant_owner": item["image"]["dominant_owner"],
                "reflection_signal_ratio": item["image"]["reflection_signal_ratio"],
                "unknown_ratio": item["image"]["ratios"]["unknown"],
                "visible_ibl_ratio": item["image"]["ratios"]["visible_ibl"],
                "scene_local_fallback_ratio": item["image"]["ratios"]["scene_local_fallback"],
                "rt_reflection_ratio": item["image"]["ratios"]["rt_reflection"],
                "ssr_ratio": item["image"]["ratios"]["ssr"],
            }
            for item in family_reports
        ],
        "aggregate": {
            "pixel_count": aggregate_pixels,
            "counts": aggregate_counts,
            "ratios": aggregate_ratios,
        },
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    parser.add_argument("--max-unknown-ratio", type=float, default=0.08)
    parser.add_argument("--max-enclosed-visible-ibl-ratio", type=float, default=0.01)
    parser.add_argument("--min-reflection-signal-ratio", type=float, default=0.0001)
    parser.add_argument("--max-dimension", type=int, default=512)
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    report = analyze_manifest(
        manifest_path,
        max_unknown_ratio=args.max_unknown_ratio,
        max_enclosed_visible_ibl_ratio=args.max_enclosed_visible_ibl_ratio,
        min_reflection_signal_ratio=args.min_reflection_signal_ratio,
        max_dimension=args.max_dimension,
    )

    out_path = args.out or (manifest_path.parent / "reflection_owner_analysis.json")
    write_json(out_path, report)

    if args.write_manifest:
        manifest = load_json(manifest_path)
        manifest["reflection_owner_analysis"] = {
            "status": report["status"],
            "report": str(out_path),
            "failure_count": report["failure_count"],
            "owner_debug_view_mode": report["owner_debug_view_mode"],
            "aggregate": report["aggregate"],
            "family_summary": report["family_summary"],
        }
        write_json(manifest_path, manifest)

    print(json.dumps({
        "status": report["status"],
        "manifest": str(manifest_path),
        "report": str(out_path),
        "failure_count": report["failure_count"],
        "family_count": report["family_count"],
    }, indent=2))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
