#!/usr/bin/env python3
"""Analyze Scene-Local Cinematic Renderer material-class packet views.

Debug view 41 encodes the renderer surface class using the shared
SurfaceClassDebugColor() palette. This analyzer turns that image into
packet-level counts so material coverage is visible before BRDF/detail polish.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from PIL import Image


MATERIAL_COLORS: dict[str, tuple[int, int, int]] = {
    "default": (89, 89, 89),
    "glass": (115, 204, 255),
    "mirror": (242, 242, 255),
    "plastic": (230, 115, 242),
    "masonry": (191, 82, 46),
    "emissive": (255, 224, 46),
    "brushed_metal": (178, 184, 194),
    "wood": (163, 107, 51),
    "water": (13, 107, 242),
}

MATERIAL_DISTANCE_LIMITS: dict[str, float] = {
    "default": 54.0,
    "glass": 95.0,
    "mirror": 70.0,
    "plastic": 95.0,
    "masonry": 95.0,
    "emissive": 95.0,
    "brushed_metal": 74.0,
    "wood": 95.0,
    "water": 95.0,
}

NAMED_POLICY_COLORS: dict[str, tuple[int, int, int]] = {
    # Approximate output of SceneMaterialPolicyDebugColor() in debug view 47.
    # The shader blends compact policy color with named class color, so the
    # analyzer treats this as a policy-family classifier, not exact material id
    # readback.
    "painted_wall": (131, 155, 160),
    "ceramic_tile": (80, 166, 205),
    "polished_wood": (148, 113, 53),
    "brushed_metal": (148, 160, 172),
    "polished_metal": (191, 197, 209),
    "glass_pane": (101, 175, 218),
    "fabric": (144, 91, 129),
    "plastic": (181, 109, 173),
    "wet_surface": (42, 91, 220),
    "emissive_neon": (161, 104, 49),
    "screen_panel": (78, 178, 79),
    "concrete": (126, 119, 102),
    "rubber": (55, 59, 48),
    "water": (31, 101, 231),
    "mirror": (221, 224, 236),
    "default_policy": (107, 114, 51),
}

NAMED_POLICY_DISTANCE_LIMITS: dict[str, float] = {
    "default_policy": 70.0,
    "rubber": 72.0,
    "mirror": 78.0,
    "ceramic_tile": 95.0,
    "screen_panel": 95.0,
    "wet_surface": 95.0,
    "water": 95.0,
}


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
    nearest_name = "unknown"
    nearest_dist = 1e9
    for name, color in MATERIAL_COLORS.items():
        dist = color_distance(rgb, color)
        if dist < nearest_dist:
            nearest_name = name
            nearest_dist = dist
    if nearest_dist <= MATERIAL_DISTANCE_LIMITS.get(nearest_name, 80.0):
        return nearest_name
    return "unknown"

def classify_named_policy_pixel(rgb: tuple[int, int, int]) -> str:
    nearest_name = "unknown"
    nearest_dist = 1e9
    for name, color in NAMED_POLICY_COLORS.items():
        dist = color_distance(rgb, color)
        if dist < nearest_dist:
            nearest_name = name
            nearest_dist = dist
    # Debug view 47 is a continuous policy response: named class color is
    # blended with compact surface policy and roughness/metallic terms. Treat
    # nearest policy family as the useful signal; keep an extreme cutoff only
    # for truly unrelated/blank pixels.
    if nearest_dist <= max(180.0, NAMED_POLICY_DISTANCE_LIMITS.get(nearest_name, 88.0)):
        return nearest_name
    return "unknown"


def analyze_image(
    path: Path,
    *,
    max_dimension: int,
    colors: dict[str, tuple[int, int, int]] | None = None,
    classifier: Any = classify_pixel,
) -> dict[str, Any]:
    palette = colors or MATERIAL_COLORS
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        original_width, original_height = rgb.size
        if max(original_width, original_height) > max_dimension:
            scale = max_dimension / float(max(original_width, original_height))
            rgb = rgb.resize(
                (
                    max(1, int(round(original_width * scale))),
                    max(1, int(round(original_height * scale))),
                ),
                Image.Resampling.NEAREST,
            )

        counts = {name: 0 for name in [*palette.keys(), "unknown"]}
        width, height = rgb.size
        pixels = rgb.load()
        for y in range(height):
            for x in range(width):
                counts[classifier(pixels[x, y])] += 1

    pixel_count = max(1, width * height)
    ratios = {key: counts[key] / float(pixel_count) for key in counts}
    default_key = "default" if "default" in ratios else "default_policy"
    named_surface_ratio = 1.0 - ratios.get(default_key, 0.0) - ratios["unknown"]
    present_classes = [
        key for key, value in ratios.items()
        if key != "unknown" and value > 0.001
    ]
    dominant_class = max(ratios.items(), key=lambda item: item[1])[0]
    return {
        "image": str(path),
        "original_width": original_width,
        "original_height": original_height,
        "sample_width": width,
        "sample_height": height,
        "pixel_count": pixel_count,
        "counts": counts,
        "ratios": ratios,
        "dominant_class": dominant_class,
        "present_classes": present_classes,
        "present_class_count": len(present_classes),
        "named_surface_ratio": named_surface_ratio,
    }


def analyze_manifest(
    manifest_path: Path,
    *,
    max_unknown_ratio: float,
    warn_min_named_surface_ratio: float,
    warn_min_present_class_count: int,
    min_named_policy_ratio: float,
    min_present_policy_count: int,
    max_named_policy_unknown_ratio: float,
    max_dimension: int,
) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    manifest_base = manifest_path.parent
    material_rows = [
        row
        for row in manifest.get("results", [])
        if str(row.get("view", "")) == "surface_class"
    ]
    policy_rows = [
        row
        for row in manifest.get("results", [])
        if str(row.get("view", "")) == "surface_policy"
    ]

    failures: list[str] = []
    warnings: list[str] = []
    family_reports: list[dict[str, Any]] = []
    aggregate_counts = {name: 0 for name in [*MATERIAL_COLORS.keys(), "unknown"]}
    policy_family_reports: list[dict[str, Any]] = []
    policy_aggregate_counts = {name: 0 for name in [*NAMED_POLICY_COLORS.keys(), "unknown"]}

    if not material_rows:
        failures.append("surface_class_view_missing")

    for row in material_rows:
        family = str(row.get("family") or "unknown")
        row_failures: list[str] = []
        row_warnings: list[str] = []

        if row.get("debug_view") != 41:
            row_failures.append(f"debug_view {row.get('debug_view')} != 41")

        capture_raw = str(row.get("capture") or "")
        if not capture_raw:
            failures.append(f"{family}:surface_class_capture_missing")
            continue
        capture = resolve_path(capture_raw, manifest_base)
        if not capture.exists():
            failures.append(f"{family}:surface_class_capture_not_found:{capture}")
            continue

        image_report = analyze_image(capture, max_dimension=max_dimension)
        for key, value in image_report["counts"].items():
            aggregate_counts[key] += int(value)

        ratios = image_report["ratios"]
        if ratios["unknown"] > max_unknown_ratio:
            row_failures.append(
                f"unknown_ratio {ratios['unknown']:.6f} > {max_unknown_ratio:.6f}"
            )
        if image_report["named_surface_ratio"] < warn_min_named_surface_ratio:
            row_warnings.append(
                "named_surface_ratio "
                f"{image_report['named_surface_ratio']:.6f} < {warn_min_named_surface_ratio:.6f}"
            )
        if image_report["present_class_count"] < warn_min_present_class_count:
            row_warnings.append(
                "present_class_count "
                f"{image_report['present_class_count']} < {warn_min_present_class_count}"
            )

        for failure in row_failures:
            failures.append(f"{family}:{failure}")
        for warning in row_warnings:
            warnings.append(f"{family}:{warning}")

        family_reports.append(
            {
                "family": family,
                "debug_view": row.get("debug_view"),
                "capture": str(capture),
                "profile_id": (row.get("scene_visual_contract") or {}).get("profile_id"),
                "image": image_report,
                "status": "FAIL" if row_failures else "PASS",
                "failures": row_failures,
                "warnings": row_warnings,
            }
        )

    if not policy_rows:
        failures.append("surface_policy_view_missing")

    for row in policy_rows:
        family = str(row.get("family") or "unknown")
        row_failures: list[str] = []
        row_warnings: list[str] = []

        if row.get("debug_view") != 47:
            row_failures.append(f"debug_view {row.get('debug_view')} != 47")

        capture_raw = str(row.get("capture") or "")
        if not capture_raw:
            warnings.append(f"{family}:surface_policy_capture_missing")
            continue
        capture = resolve_path(capture_raw, manifest_base)
        if not capture.exists():
            warnings.append(f"{family}:surface_policy_capture_not_found:{capture}")
            continue

        image_report = analyze_image(
            capture,
            max_dimension=max_dimension,
            colors=NAMED_POLICY_COLORS,
            classifier=classify_named_policy_pixel,
        )
        for key, value in image_report["counts"].items():
            policy_aggregate_counts[key] += int(value)

        ratios = image_report["ratios"]
        named_policy_ratio = image_report["named_surface_ratio"]
        if ratios["unknown"] > max_named_policy_unknown_ratio:
            row_failures.append(
                "unknown_policy_ratio "
                f"{ratios['unknown']:.6f} > {max_named_policy_unknown_ratio:.6f}"
            )
        if named_policy_ratio < min_named_policy_ratio:
            row_failures.append(
                "named_policy_ratio "
                f"{named_policy_ratio:.6f} < {min_named_policy_ratio:.6f}"
            )
        if image_report["present_class_count"] < min_present_policy_count:
            row_failures.append(
                "present_policy_count "
                f"{image_report['present_class_count']} < {min_present_policy_count}"
            )

        for failure in row_failures:
            failures.append(f"{family}:surface_policy:{failure}")
        for warning in row_warnings:
            warnings.append(f"{family}:surface_policy:{warning}")

        policy_family_reports.append(
            {
                "family": family,
                "debug_view": row.get("debug_view"),
                "capture": str(capture),
                "profile_id": (row.get("scene_visual_contract") or {}).get("profile_id"),
                "image": image_report,
                "status": "FAIL" if row_failures else "PASS",
                "failures": row_failures,
                "warnings": row_warnings,
            }
        )

    aggregate_pixels = max(1, sum(aggregate_counts.values()))
    aggregate_ratios = {
        key: aggregate_counts[key] / float(aggregate_pixels) for key in aggregate_counts
    }
    policy_aggregate_pixels = max(1, sum(policy_aggregate_counts.values()))
    policy_aggregate_ratios = {
        key: policy_aggregate_counts[key] / float(policy_aggregate_pixels)
        for key in policy_aggregate_counts
    }
    report = {
        "schema": "cortex.scene_local_cinematic_renderer_v1.material_class_analysis",
        "manifest": str(manifest_path),
        "material_class_debug_view_mode": 41,
        "named_policy_debug_view_mode": 47,
        "thresholds": {
            "max_unknown_ratio": max_unknown_ratio,
            "warn_min_named_surface_ratio": warn_min_named_surface_ratio,
            "warn_min_present_class_count": warn_min_present_class_count,
            "min_named_policy_ratio": min_named_policy_ratio,
            "min_present_policy_count": min_present_policy_count,
            "max_named_policy_unknown_ratio": max_named_policy_unknown_ratio,
            "max_dimension": max_dimension,
        },
        "status": "FAIL" if failures else "PASS",
        "failure_count": len(failures),
        "warning_count": len(warnings),
        "failures": failures,
        "warnings": warnings,
        "family_count": len(family_reports),
        "families": family_reports,
        "family_summary": [
            {
                "family": item["family"],
                "status": item["status"],
                "profile_id": item["profile_id"],
                "dominant_class": item["image"]["dominant_class"],
                "present_classes": item["image"]["present_classes"],
                "present_class_count": item["image"]["present_class_count"],
                "named_surface_ratio": item["image"]["named_surface_ratio"],
                "unknown_ratio": item["image"]["ratios"]["unknown"],
                "default_ratio": item["image"]["ratios"]["default"],
                "glass_ratio": item["image"]["ratios"]["glass"],
                "mirror_ratio": item["image"]["ratios"]["mirror"],
                "plastic_ratio": item["image"]["ratios"]["plastic"],
                "masonry_ratio": item["image"]["ratios"]["masonry"],
                "emissive_ratio": item["image"]["ratios"]["emissive"],
                "brushed_metal_ratio": item["image"]["ratios"]["brushed_metal"],
                "wood_ratio": item["image"]["ratios"]["wood"],
                "water_ratio": item["image"]["ratios"]["water"],
                "warnings": item["warnings"],
            }
            for item in family_reports
        ],
        "named_policy_family_summary": [
            {
                "family": item["family"],
                "status": item["status"],
                "profile_id": item["profile_id"],
                "dominant_policy": item["image"]["dominant_class"],
                "present_policies": item["image"]["present_classes"],
                "present_policy_count": item["image"]["present_class_count"],
                "named_policy_ratio": item["image"]["named_surface_ratio"],
                "unknown_policy_ratio": item["image"]["ratios"]["unknown"],
                "default_policy_ratio": item["image"]["ratios"]["default_policy"],
                "release_gate": "FAIL" if item["failures"] else "PASS",
                "failures": item["failures"],
                "warnings": item["warnings"],
            }
            for item in policy_family_reports
        ],
        "aggregate": {
            "pixel_count": aggregate_pixels,
            "counts": aggregate_counts,
            "ratios": aggregate_ratios,
        },
        "named_policy_aggregate": {
            "pixel_count": policy_aggregate_pixels,
            "counts": policy_aggregate_counts,
            "ratios": policy_aggregate_ratios,
        },
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    parser.add_argument("--max-unknown-ratio", type=float, default=0.08)
    parser.add_argument("--warn-min-named-surface-ratio", type=float, default=0.02)
    parser.add_argument("--warn-min-present-class-count", type=int, default=2)
    parser.add_argument("--min-named-policy-ratio", type=float, default=0.20)
    parser.add_argument("--min-present-policy-count", type=int, default=4)
    parser.add_argument("--max-named-policy-unknown-ratio", type=float, default=0.12)
    parser.add_argument("--max-dimension", type=int, default=512)
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    report = analyze_manifest(
        manifest_path,
        max_unknown_ratio=args.max_unknown_ratio,
        warn_min_named_surface_ratio=args.warn_min_named_surface_ratio,
        warn_min_present_class_count=args.warn_min_present_class_count,
        min_named_policy_ratio=args.min_named_policy_ratio,
        min_present_policy_count=args.min_present_policy_count,
        max_named_policy_unknown_ratio=args.max_named_policy_unknown_ratio,
        max_dimension=args.max_dimension,
    )

    out_path = args.out or (manifest_path.parent / "material_class_analysis.json")
    write_json(out_path, report)

    if args.write_manifest:
        manifest = load_json(manifest_path)
        manifest["material_class_analysis"] = {
            "status": report["status"],
            "report": str(out_path),
            "failure_count": report["failure_count"],
            "warning_count": report["warning_count"],
            "material_class_debug_view_mode": report["material_class_debug_view_mode"],
            "named_policy_debug_view_mode": report["named_policy_debug_view_mode"],
            "aggregate": report["aggregate"],
            "named_policy_aggregate": report["named_policy_aggregate"],
            "family_summary": report["family_summary"],
            "named_policy_family_summary": report["named_policy_family_summary"],
        }
        write_json(manifest_path, manifest)

    print(json.dumps({
        "status": report["status"],
        "manifest": str(manifest_path),
        "report": str(out_path),
        "failure_count": report["failure_count"],
        "warning_count": report["warning_count"],
        "family_count": report["family_count"],
    }, indent=2))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
