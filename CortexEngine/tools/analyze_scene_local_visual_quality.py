#!/usr/bin/env python3
"""Analyze final visual-quality proxies for Scene-Local Cinematic Renderer packets.

This is not a human taste model. It is a release-review gate that combines
measurable image proxies with the existing owner/material/stability reports so
the renderer cannot be called "high-quality" only because low-level stability
tests passed.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from PIL import Image


REQUIRED_FAMILIES = ("gallery", "kitchen", "office", "gym", "concert")


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def split_csv(value: Any) -> list[str]:
    if not isinstance(value, str):
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def expected_families_for_manifest(manifest: dict[str, Any], seen_families: set[str]) -> list[str]:
    filtered = split_csv(manifest.get("family_filter"))
    if filtered:
        return filtered
    if seen_families:
        return sorted(seen_families)
    return list(REQUIRED_FAMILIES)


def resolve_path(path: str, base: Path) -> Path:
    p = Path(path)
    if p.is_absolute():
        return p
    return (base / p).resolve()


def image_metrics(path: Path, *, max_width: int = 320) -> dict[str, Any]:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        original_width, original_height = rgb.size
        if rgb.width > max_width:
            height = max(1, int(round(rgb.height * (max_width / float(rgb.width)))))
            rgb = rgb.resize((max_width, height), Image.Resampling.BILINEAR)

        width, height = rgb.size
        raw = rgb.tobytes()
        pixels = [
            (raw[i], raw[i + 1], raw[i + 2])
            for i in range(0, len(raw), 3)
        ]

    luma: list[float] = []
    saturation: list[float] = []
    for r, g, b in pixels:
        rf = r / 255.0
        gf = g / 255.0
        bf = b / 255.0
        y = 0.2126 * rf + 0.7152 * gf + 0.0722 * bf
        luma.append(y)
        mx = max(rf, gf, bf)
        mn = min(rf, gf, bf)
        saturation.append(0.0 if mx <= 1e-6 else (mx - mn) / mx)

    pixel_count = max(1, len(luma))
    mean_luma = sum(luma) / pixel_count
    luma_std = math.sqrt(sum((v - mean_luma) ** 2 for v in luma) / pixel_count)
    saturation_mean = sum(saturation) / pixel_count
    dark_ratio = sum(1 for v in luma if v < 0.035) / pixel_count
    bright_ratio = sum(1 for v in luma if v > 0.94) / pixel_count
    midtone_ratio = sum(1 for v in luma if 0.10 < v < 0.88) / pixel_count

    edge_count = 0
    grad_sum = 0.0
    grad_samples = 0
    for y in range(height - 1):
        base = y * width
        next_base = (y + 1) * width
        for x in range(width - 1):
            i = base + x
            grad = max(abs(luma[i + 1] - luma[i]), abs(luma[next_base + x] - luma[i]))
            grad_sum += grad
            grad_samples += 1
            if grad > 0.045:
                edge_count += 1
    edge_density = edge_count / max(1, grad_samples)
    gradient_mean = grad_sum / max(1, grad_samples)

    tile_stds: list[float] = []
    tile_w = 16
    tile_h = 16
    for yy in range(0, height, tile_h):
        for xx in range(0, width, tile_w):
            values: list[float] = []
            for y in range(yy, min(yy + tile_h, height)):
                start = y * width + xx
                end = y * width + min(xx + tile_w, width)
                values.extend(luma[start:end])
            if values:
                tile_mean = sum(values) / len(values)
                tile_stds.append(
                    math.sqrt(sum((v - tile_mean) ** 2 for v in values) / len(values))
                )
    local_contrast = sum(tile_stds) / max(1, len(tile_stds))

    return {
        "image": str(path),
        "original_width": original_width,
        "original_height": original_height,
        "sample_width": width,
        "sample_height": height,
        "mean_luma": mean_luma,
        "luma_std": luma_std,
        "local_contrast": local_contrast,
        "edge_density": edge_density,
        "gradient_mean": gradient_mean,
        "saturation_mean": saturation_mean,
        "dark_ratio": dark_ratio,
        "bright_ratio": bright_ratio,
        "midtone_ratio": midtone_ratio,
    }


def find_report(manifest_path: Path, name: str) -> Path | None:
    path = manifest_path.parent / name
    return path if path.exists() else None


def load_optional_report(manifest_path: Path, name: str) -> dict[str, Any] | None:
    path = find_report(manifest_path, name)
    if path is None:
        return None
    return load_json(path)


def ratio_by_family(report: dict[str, Any] | None, field: str) -> dict[str, float]:
    if not report:
        return {}
    values: dict[str, float] = {}
    for row in report.get("family_summary", []):
        family = str(row.get("family", ""))
        if family:
            values[family] = float(row.get(field, 0.0) or 0.0)
    return values


def named_policy_ratio_by_family(report: dict[str, Any] | None) -> dict[str, float]:
    if not report:
        return {}
    values: dict[str, float] = {}
    for row in report.get("named_policy_family_summary", []):
        family = str(row.get("family", ""))
        if family:
            values[family] = float(row.get("named_policy_ratio", 0.0) or 0.0)
    return values


def analyze_manifest(
    manifest_path: Path,
    *,
    min_edge_density: float,
    min_saturation_mean: float,
    min_local_contrast: float,
    max_bright_ratio: float,
    max_dark_ratio: float,
    min_midtone_ratio: float,
    min_named_surface_ratio: float,
    min_named_policy_ratio: float,
) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    manifest_base = manifest_path.parent
    beauty_rows = [
        row for row in manifest.get("results", [])
        if str(row.get("view", "")) == "beauty"
    ]

    owner_report = load_optional_report(manifest_path, "reflection_owner_analysis.json")
    material_report = load_optional_report(manifest_path, "material_class_analysis.json")
    stability_report = load_optional_report(manifest_path, "packet_stability_analysis.json")

    named_surface_ratio = ratio_by_family(material_report, "named_surface_ratio")
    named_policy_ratio = named_policy_ratio_by_family(material_report)

    failures: list[str] = []
    warnings: list[str] = []
    family_reports: list[dict[str, Any]] = []
    seen_families: set[str] = set()

    if not beauty_rows:
        failures.append("beauty_view_missing")

    for row in beauty_rows:
        family = str(row.get("family") or "unknown")
        seen_families.add(family)
        row_failures: list[str] = []
        row_warnings: list[str] = []
        capture_raw = str(row.get("capture") or "")
        if not capture_raw:
            failures.append(f"{family}:beauty_capture_missing")
            continue
        capture = resolve_path(capture_raw, manifest_base)
        if not capture.exists():
            failures.append(f"{family}:beauty_capture_not_found:{capture}")
            continue

        metrics = image_metrics(capture)
        if metrics["edge_density"] < min_edge_density:
            row_warnings.append(
                f"edge_density {metrics['edge_density']:.6f} < {min_edge_density:.6f}"
            )
        if metrics["saturation_mean"] < min_saturation_mean:
            row_warnings.append(
                "saturation_mean "
                f"{metrics['saturation_mean']:.6f} < {min_saturation_mean:.6f}"
            )
        if metrics["local_contrast"] < min_local_contrast:
            row_warnings.append(
                f"local_contrast {metrics['local_contrast']:.6f} < {min_local_contrast:.6f}"
            )
        if metrics["bright_ratio"] > max_bright_ratio:
            row_warnings.append(
                f"bright_ratio {metrics['bright_ratio']:.6f} > {max_bright_ratio:.6f}"
            )
        if metrics["dark_ratio"] > max_dark_ratio:
            row_warnings.append(
                f"dark_ratio {metrics['dark_ratio']:.6f} > {max_dark_ratio:.6f}"
            )
        if metrics["midtone_ratio"] < min_midtone_ratio:
            row_warnings.append(
                f"midtone_ratio {metrics['midtone_ratio']:.6f} < {min_midtone_ratio:.6f}"
            )

        surface_ratio = named_surface_ratio.get(family)
        if surface_ratio is not None and family != "gallery" and surface_ratio < min_named_surface_ratio:
            row_warnings.append(
                f"named_surface_ratio {surface_ratio:.6f} < {min_named_surface_ratio:.6f}"
            )
        policy_ratio = named_policy_ratio.get(family)
        if policy_ratio is not None and family != "gallery" and policy_ratio < min_named_policy_ratio:
            row_failures.append(
                f"named_policy_ratio {policy_ratio:.6f} < {min_named_policy_ratio:.6f}"
            )

        for failure in row_failures:
            failures.append(f"{family}:{failure}")
        for warning in row_warnings:
            warnings.append(f"{family}:{warning}")

        family_reports.append({
            "family": family,
            "profile_id": (row.get("scene_visual_contract") or {}).get("profile_id"),
            "capture": str(capture),
            "metrics": metrics,
            "named_surface_ratio": surface_ratio,
            "named_policy_ratio": policy_ratio,
            "release_gate": "PASS" if not row_failures and not row_warnings else (
                "FAIL" if row_failures else "REVIEW_REQUIRED"
            ),
            "failures": row_failures,
            "warnings": row_warnings,
        })

    expected_families = expected_families_for_manifest(manifest, seen_families)
    missing = [family for family in expected_families if family not in seen_families]
    for family in missing:
        failures.append(f"{family}:beauty_view_missing")

    report_dependencies = {
        "reflection_owner_analysis": (owner_report or {}).get("status"),
        "material_class_analysis": (material_report or {}).get("status"),
        "packet_stability_analysis": (stability_report or {}).get("status"),
    }
    for name, status in report_dependencies.items():
        if status is None:
            warnings.append(f"{name}:report_missing")
        elif status != "PASS":
            failures.append(f"{name}:status_{status}")

    hard_gate_warnings = 0
    if stability_report is not None:
        hard_gate_warnings = int(stability_report.get("hard_gate_warning_count", 0) or 0)
        if hard_gate_warnings > 0:
            failures.append(f"stability:hard_gate_warning_count {hard_gate_warnings} > 0")

    renderer_contract_passed = not failures
    release_gate = "PASS"
    if failures:
        release_gate = "FAIL"
    elif warnings:
        release_gate = "REVIEW_REQUIRED"
    high_quality_visuals_proven = release_gate == "PASS"

    return {
        "schema": "cortex.scene_local_cinematic_renderer_v1.visual_quality_analysis",
        "manifest": str(manifest_path),
        "status": release_gate,
        "release_gate": release_gate,
        "completion_gate": {
            "renderer_contract_passed": renderer_contract_passed,
            "visual_quality_review_required": release_gate == "REVIEW_REQUIRED",
            "high_quality_visuals_proven": high_quality_visuals_proven,
        },
        "thresholds": {
            "min_edge_density": min_edge_density,
            "min_saturation_mean": min_saturation_mean,
            "min_local_contrast": min_local_contrast,
            "max_bright_ratio": max_bright_ratio,
            "max_dark_ratio": max_dark_ratio,
            "min_midtone_ratio": min_midtone_ratio,
            "min_named_surface_ratio": min_named_surface_ratio,
            "min_named_policy_ratio": min_named_policy_ratio,
        },
        "expected_families": expected_families,
        "dependencies": report_dependencies,
        "hard_gate_warning_count": hard_gate_warnings,
        "failure_count": len(failures),
        "warning_count": len(warnings),
        "failures": failures,
        "warnings": warnings,
        "family_count": len(family_reports),
        "family_summary": [
            {
                "family": row["family"],
                "profile_id": row["profile_id"],
                "release_gate": row["release_gate"],
                "edge_density": row["metrics"]["edge_density"],
                "saturation_mean": row["metrics"]["saturation_mean"],
                "local_contrast": row["metrics"]["local_contrast"],
                "bright_ratio": row["metrics"]["bright_ratio"],
                "dark_ratio": row["metrics"]["dark_ratio"],
                "midtone_ratio": row["metrics"]["midtone_ratio"],
                "named_surface_ratio": row["named_surface_ratio"],
                "named_policy_ratio": row["named_policy_ratio"],
                "failures": row["failures"],
                "warnings": row["warnings"],
            }
            for row in family_reports
        ],
        "results": family_reports,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    parser.add_argument("--fail-on-review", action="store_true")
    parser.add_argument("--min-edge-density", type=float, default=0.10)
    parser.add_argument("--min-saturation-mean", type=float, default=0.14)
    parser.add_argument("--min-local-contrast", type=float, default=0.060)
    parser.add_argument("--max-bright-ratio", type=float, default=0.24)
    parser.add_argument("--max-dark-ratio", type=float, default=0.12)
    parser.add_argument("--min-midtone-ratio", type=float, default=0.50)
    parser.add_argument("--min-named-surface-ratio", type=float, default=0.20)
    parser.add_argument("--min-named-policy-ratio", type=float, default=0.95)
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    report = analyze_manifest(
        manifest_path,
        min_edge_density=args.min_edge_density,
        min_saturation_mean=args.min_saturation_mean,
        min_local_contrast=args.min_local_contrast,
        max_bright_ratio=args.max_bright_ratio,
        max_dark_ratio=args.max_dark_ratio,
        min_midtone_ratio=args.min_midtone_ratio,
        min_named_surface_ratio=args.min_named_surface_ratio,
        min_named_policy_ratio=args.min_named_policy_ratio,
    )

    out_path = args.out or (manifest_path.parent / "visual_quality_analysis.json")
    write_json(out_path, report)

    if args.write_manifest:
        manifest = load_json(manifest_path)
        manifest["visual_quality_analysis"] = {
            "status": report["status"],
            "release_gate": report["release_gate"],
            "report": str(out_path),
            "failure_count": report["failure_count"],
            "warning_count": report["warning_count"],
            "completion_gate": report["completion_gate"],
            "family_summary": report["family_summary"],
        }
        write_json(manifest_path, manifest)

    summary = {
        "status": report["status"],
        "release_gate": report["release_gate"],
        "manifest": str(manifest_path),
        "report": str(out_path),
        "failure_count": report["failure_count"],
        "warning_count": report["warning_count"],
        "family_count": report["family_count"],
    }
    print(json.dumps(summary, indent=2))

    if report["release_gate"] == "FAIL":
        return 1
    if args.fail_on_review and report["release_gate"] == "REVIEW_REQUIRED":
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
