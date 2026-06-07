#!/usr/bin/env python3
"""Summarize ReflectionV3 material stress packets.

The regular V3 motion analyzer treats every debug view as luma. This analyzer
keeps the ReflectionV3 suppression channels separate so glossy metal, glass,
rough dielectric, and water packets can show whether rejection came from
history instability or material policy.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_debug_view_metrics import read_bmp_rgb


KEY_VIEWS = {
    "beauty",
    "roughness",
    "metallic",
    "surface_class",
    "material_family",
    "reflection_radiance",
    "reflection_source_id",
    "reflection_rejected_source_mask",
    "reflection_ssr_source_signal",
    "reflection_rt_source_signal",
    "reflection_source_suppression",
    "reflection_history_v3_rejection",
}


def classify_material_target(scene: str, bookmark: str, family: str) -> str:
    key = f"{scene} {bookmark} {family}".lower()
    if "water" in key or "liquid" in key:
        return "water"
    if "glass" in key:
        return "glass"
    if "chrome" in key:
        return "chrome"
    if "metal" in key:
        if "stone" in key or "relic" in key:
            return "rough_dielectric_and_metal"
        return "glossy_metal"
    if "stone" in key or "moss" in key or "sand" in key:
        return "rough_dielectric"
    return "unknown"


def measure_rgb(path: Path) -> dict[str, Any]:
    width, height, pixels = read_bmp_rgb(path)

    def summarize(sample_pixels: list[tuple[int, int, int]]) -> dict[str, Any]:
        count = max(len(sample_pixels), 1)
        sums = [0.0, 0.0, 0.0]
        maxes = [0, 0, 0]
        active = [0, 0, 0]
        luma_sum = 0.0
        nonblack = 0
        for r, g, b in sample_pixels:
            channels = (r, g, b)
            luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
            luma_sum += luma
            if max(channels) > 3:
                nonblack += 1
            for i, value in enumerate(channels):
                sums[i] += value
                maxes[i] = max(maxes[i], value)
                if value > 12:
                    active[i] += 1
        return {
            "pixel_count": count,
            "mean_rgb": [v / count / 255.0 for v in sums],
            "max_rgb": [v / 255.0 for v in maxes],
            "active_rgb_ratio": [v / count for v in active],
            "mean_luma": luma_sum / count / 255.0,
            "nonblack_ratio": nonblack / count,
        }

    center_x0 = width // 4
    center_x1 = width - center_x0
    center_y0 = height // 4
    center_y1 = height - center_y0
    center_pixels = [
        pixels[y * width + x]
        for y in range(center_y0, center_y1)
        for x in range(center_x0, center_x1)
    ]
    full = summarize(pixels)
    center = summarize(center_pixels)
    count = full["pixel_count"]
    return {
        "path": str(path),
        "width": width,
        "height": height,
        "pixel_count": count,
        "mean_rgb": full["mean_rgb"],
        "max_rgb": full["max_rgb"],
        "active_rgb_ratio": full["active_rgb_ratio"],
        "mean_luma": full["mean_luma"],
        "nonblack_ratio": full["nonblack_ratio"],
        "center_roi": center,
    }


def measure_pair_delta(path_a: Path, path_b: Path) -> dict[str, Any]:
    width_a, height_a, pixels_a = read_bmp_rgb(path_a)
    width_b, height_b, pixels_b = read_bmp_rgb(path_b)
    if width_a != width_b or height_a != height_b or len(pixels_a) != len(pixels_b):
        raise ValueError(f"dimension mismatch: {path_a} vs {path_b}")
    count = max(len(pixels_a), 1)
    sums = [0.0, 0.0, 0.0]
    maxes = [0.0, 0.0, 0.0]
    luma_sum = 0.0
    for a, b in zip(pixels_a, pixels_b):
        luma_a = 0.2126 * a[0] + 0.7152 * a[1] + 0.0722 * a[2]
        luma_b = 0.2126 * b[0] + 0.7152 * b[1] + 0.0722 * b[2]
        luma_sum += abs(luma_a - luma_b)
        for i in range(3):
            delta = abs(a[i] - b[i])
            sums[i] += delta
            maxes[i] = max(maxes[i], delta)
    return {
        "a": str(path_a),
        "b": str(path_b),
        "mean_abs_rgb_delta": [v / count / 255.0 for v in sums],
        "max_abs_rgb_delta": [v / 255.0 for v in maxes],
        "mean_abs_luma_delta": luma_sum / count / 255.0,
    }


def summarize_deltas(sequence: list[Path]) -> dict[str, Any]:
    if len(sequence) < 2:
        return {
            "pair_count": 0,
            "mean_abs_rgb_delta": [0.0, 0.0, 0.0],
            "max_abs_rgb_delta": [0.0, 0.0, 0.0],
            "mean_abs_luma_delta": 0.0,
        }
    pairs = [measure_pair_delta(a, b) for a, b in zip(sequence, sequence[1:])]
    return {
        "pair_count": len(pairs),
        "mean_abs_rgb_delta": [
            sum(pair["mean_abs_rgb_delta"][i] for pair in pairs) / len(pairs)
            for i in range(3)
        ],
        "max_abs_rgb_delta": [
            max(pair["max_abs_rgb_delta"][i] for pair in pairs)
            for i in range(3)
        ],
        "mean_abs_luma_delta": sum(pair["mean_abs_luma_delta"] for pair in pairs) / len(pairs),
    }


def first_capture(result: dict[str, Any]) -> Path | None:
    sequence = [Path(path) for path in result.get("capture_sequence", []) if path]
    if sequence:
        return sequence[0]
    capture = result.get("capture")
    return Path(capture) if capture else None


def find_material_policy_counts(node: Any) -> dict[str, Any]:
    if isinstance(node, dict):
        if "material_class_policy_applied" in node and "sampled" in node:
            keys = [
                "sampled",
                "scene_material_glass_pane",
                "scene_material_mirror",
                "scene_material_polished_metal",
                "scene_material_water",
                "scene_material_wet_surface",
                "surface_glass",
                "surface_mirror",
                "surface_brushed_metal",
                "surface_water",
                "reflection_eligible",
                "reflection_high_ceiling",
                "reflection_transmissive",
                "reflection_water",
            ]
            return {key: node.get(key, 0) for key in keys}
        for value in node.values():
            found = find_material_policy_counts(value)
            if found:
                return found
    elif isinstance(node, list):
        for value in node:
            found = find_material_policy_counts(value)
            if found:
                return found
    return {}


def read_material_policy_counts(capture: Path | None) -> dict[str, Any]:
    if capture is None:
        return {}
    report_path = capture.parent / "frame_report_shutdown.json"
    if not report_path.exists():
        return {}
    try:
        report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    except Exception:  # noqa: BLE001 - optional diagnostic path.
        return {}
    return find_material_policy_counts(report)


def read_frame_contract_section(capture: Path | None, section: str) -> dict[str, Any]:
    if capture is None:
        return {}
    report_path = capture.parent / "frame_report_shutdown.json"
    if not report_path.exists():
        return {}
    try:
        report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    except Exception:  # noqa: BLE001 - optional diagnostic path.
        return {}
    value = report.get("frame_contract", {}).get(section, {})
    return value if isinstance(value, dict) else {}


def build_report(manifest_path: Path) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    by_family: dict[str, dict[str, dict[str, Any]]] = {}
    failures: list[str] = []

    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        view = str(result.get("view", ""))
        if view not in KEY_VIEWS:
            continue
        family = str(result.get("family", ""))
        capture = first_capture(result)
        if capture is None or not capture.exists():
            failures.append(f"{family}/{view}: missing capture")
            continue
        try:
            metrics = measure_rgb(capture)
            sequence = [Path(path) for path in result.get("capture_sequence", []) if path]
            delta = summarize_deltas(sequence)
        except Exception as exc:  # noqa: BLE001 - report packet artifact path.
            failures.append(f"{family}/{view}: {exc}")
            continue
        by_family.setdefault(family, {})[view] = {
            "view": view,
            "debug_view": result.get("debug_view"),
            "scene": str(result.get("scene", "")),
            "camera_bookmark": str(result.get("camera_bookmark", "")),
            "capture": str(capture),
            "metrics": metrics,
            "motion": delta,
        }

    rows: list[dict[str, Any]] = []
    warnings: list[str] = []
    for family in sorted(by_family):
        views = by_family[family]
        any_view = next(iter(views.values()))
        scene = any_view["scene"]
        bookmark = any_view["camera_bookmark"]
        material_target = classify_material_target(scene, bookmark, family)
        suppression = views.get("reflection_source_suppression", {})
        roughness = views.get("roughness", {})
        metallic = views.get("metallic", {})
        report_capture = first_capture(suppression) or first_capture(roughness) or first_capture(metallic)
        material_counts = read_material_policy_counts(report_capture)
        water_contract = read_frame_contract_section(report_capture, "water")
        suppression_mean = suppression.get("metrics", {}).get("mean_rgb", [0.0, 0.0, 0.0])
        suppression_motion = suppression.get("motion", {}).get("mean_abs_rgb_delta", [0.0, 0.0, 0.0])
        roughness_metrics = roughness.get("metrics", {})
        metallic_metrics = metallic.get("metrics", {})
        roughness_mean = roughness_metrics.get("mean_luma", 0.0)
        roughness_center_mean = roughness_metrics.get("center_roi", {}).get("mean_luma", roughness_mean)
        roughness_max = roughness_metrics.get("max_rgb", [0.0, 0.0, 0.0])[0]
        metallic_mean = metallic_metrics.get("mean_luma", 0.0)
        metallic_center_mean = metallic_metrics.get("center_roi", {}).get("mean_luma", metallic_mean)
        metallic_max = metallic_metrics.get("max_rgb", [0.0, 0.0, 0.0])[0]
        metallic_active_ratio = metallic_metrics.get("active_rgb_ratio", [0.0, 0.0, 0.0])[0]
        water_surface_count = int(water_contract.get("surface_count", 0))
        water_contract_roughness = float(water_contract.get("roughness", roughness_center_mean))
        target_roughness_for_warning = (
            water_contract_roughness
            if material_target == "water" and water_surface_count > 0
            else roughness_center_mean
        )
        sampled_materials = max(int(material_counts.get("sampled", 0)), 0)
        water_class_count = int(material_counts.get("scene_material_water", 0)) + int(material_counts.get("surface_water", 0))
        glass_class_count = int(material_counts.get("scene_material_glass_pane", 0)) + int(material_counts.get("surface_glass", 0))
        polished_class_count = (
            int(material_counts.get("scene_material_mirror", 0)) +
            int(material_counts.get("scene_material_polished_metal", 0)) +
            int(material_counts.get("surface_mirror", 0)) +
            int(material_counts.get("surface_brushed_metal", 0))
        )
        smooth_class_count = water_class_count + glass_class_count + polished_class_count
        smooth_class_ratio = smooth_class_count / sampled_materials if sampled_materials else 0.0
        row_warnings: list[str] = []

        if "reflection_source_suppression" not in views:
            row_warnings.append("missing_source_suppression")
        if "roughness" not in views:
            row_warnings.append("missing_roughness_view")
        if "metallic" not in views:
            row_warnings.append("missing_metallic_view")
        if abs(suppression_mean[2] - roughness_mean) > 0.18 and "reflection_source_suppression" in views:
            row_warnings.append("suppression_roughness_channel_diverges_from_roughness_view")
        if material_target in {"glossy_metal", "chrome"} and metallic_max < 0.35 and metallic_active_ratio < 0.01:
            row_warnings.append("metal_target_has_low_metallic_signal")
        if material_target in {"glass", "water"} and target_roughness_for_warning > 0.72:
            row_warnings.append("smooth_target_center_roi_has_high_roughness_signal")
        elif material_target in {"glass", "water"} and material_target != "water" and roughness_mean > 0.72:
            row_warnings.append("smooth_target_full_frame_has_high_roughness_signal")
        if material_target in {"glass", "water", "chrome", "glossy_metal"} and sampled_materials > 0 and smooth_class_ratio < 0.12:
            row_warnings.append("smooth_target_has_sparse_scene_class_coverage")
        if max(suppression_motion[:2]) > 0.08:
            row_warnings.append("suppression_gate_changes_under_motion")

        warnings.extend(f"{family}: {warning}" for warning in row_warnings)
        rows.append(
            {
                "family": family,
                "scene": scene,
                "camera_bookmark": bookmark,
                "material_target": material_target,
                "history_suppression_mean": suppression_mean[0],
                "material_suppression_mean": suppression_mean[1],
                "suppression_roughness_mean": suppression_mean[2],
                "roughness_view_mean": roughness_mean,
                "roughness_center_mean": roughness_center_mean,
                "target_roughness_for_warning": target_roughness_for_warning,
                "roughness_view_max": roughness_max,
                "metallic_view_mean": metallic_mean,
                "metallic_center_mean": metallic_center_mean,
                "metallic_view_max": metallic_max,
                "metallic_active_ratio": metallic_active_ratio,
                "sampled_materials": sampled_materials,
                "water_class_count": water_class_count,
                "glass_class_count": glass_class_count,
                "polished_class_count": polished_class_count,
                "smooth_class_count": smooth_class_count,
                "smooth_class_ratio": smooth_class_ratio,
                "material_policy_counts": material_counts,
                "water_surface_count": water_surface_count,
                "water_contract_roughness": water_contract_roughness,
                "water_contract": water_contract,
                "history_suppression_motion_delta": suppression_motion[0],
                "material_suppression_motion_delta": suppression_motion[1],
                "roughness_motion_delta": suppression_motion[2],
                "warnings": row_warnings,
                "views": views,
            }
        )

    return {
        "schema": "cortex.reflection_v3_material_stress.v1",
        "manifest": str(manifest_path),
        "output_root": manifest.get("output_root", ""),
        "stability_motion_mode": manifest.get("stability_motion_mode", ""),
        "capture_sequence_count": manifest.get("capture_sequence_count", 0),
        "family_count": len(rows),
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# ReflectionV3 Material Stress",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- motion mode: `{report['stability_motion_mode']}`",
        f"- sequence count: `{report['capture_sequence_count']}`",
        f"- families: {report['family_count']}",
        f"- failures: {len(report['failures'])}",
        f"- warnings: {len(report['warnings'])}",
        "",
        "| Family | Target | History Supp | Material Supp | Supp Rough | Rough Mean | Rough Center | Target Rough | Metal Mean | Metal Center | Metal Max | Metal Active | Smooth Class | Hist Motion | Mat Motion | Rough Motion | Warnings |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in report["rows"]:
        lines.append(
            "| {family} | {target} | {history:.5f} | {material:.5f} | {supp_rough:.5f} | "
            "{rough:.5f} | {rough_center:.5f} | {target_rough:.5f} | {metal:.5f} | {metal_center:.5f} | {metal_max:.5f} | {metal_active:.5f} | {smooth_class} | {hist_motion:.5f} | {mat_motion:.5f} | "
            "{rough_motion:.5f} | {warnings} |".format(
                family=row["family"],
                target=row["material_target"],
                history=row["history_suppression_mean"],
                material=row["material_suppression_mean"],
                supp_rough=row["suppression_roughness_mean"],
                rough=row["roughness_view_mean"],
                rough_center=row["roughness_center_mean"],
                target_rough=row["target_roughness_for_warning"],
                metal=row["metallic_view_mean"],
                metal_center=row["metallic_center_mean"],
                metal_max=row["metallic_view_max"],
                metal_active=row["metallic_active_ratio"],
                smooth_class=(
                    f"{row['smooth_class_count']}/{row['sampled_materials']} "
                    f"({row['smooth_class_ratio']:.3f})"
                ),
                hist_motion=row["history_suppression_motion_delta"],
                mat_motion=row["material_suppression_motion_delta"],
                rough_motion=row["roughness_motion_delta"],
                warnings=", ".join(row["warnings"]),
            )
        )
    if report["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in report["warnings"])
    if report["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    report = build_report(args.manifest)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, args.output_md)
    if report["failures"]:
        for failure in report["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print(
        "PASS: ReflectionV3 material stress summarized "
        f"{report['family_count']} families with {len(report['warnings'])} warnings"
    )
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
