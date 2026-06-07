#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any

from analyze_full_scene_shader_debug_view_metrics import measure_capture


REQUIRED_VIEWS = {
    "candidate_hdr_scene_color",
    "energy_clamp_policy",
    "overbright_diagnostics",
    "composite_contribution_map",
    "legacy_rescue_usage",
}


def _rows_by_view(manifest: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    rows: dict[str, list[dict[str, Any]]] = {}
    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        view = result.get("view")
        if isinstance(view, str):
            rows.setdefault(view, []).append(result)
    return rows


def _mean(values: list[float]) -> float:
    return sum(values) / max(1, len(values))


def analyze_manifest(manifest_path: pathlib.Path) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    rows_by_view = _rows_by_view(manifest)
    failures: list[str] = []
    warnings: list[str] = []
    rows: list[dict[str, Any]] = []

    missing_views = sorted(REQUIRED_VIEWS - set(rows_by_view))
    for view in missing_views:
        failures.append(f"missing CompositeV3 diagnostic view: {view}")

    for family_result in rows_by_view.get("energy_clamp_policy", []):
        family = str(family_result.get("family", ""))
        capture = pathlib.Path(str(family_result.get("capture", "")))
        if not capture.exists():
            failures.append(f"missing energy_clamp_policy capture for {family}: {capture}")
            continue
        metrics = measure_capture(capture)
        mean_rgb = metrics["mean_rgb"]
        max_rgb = metrics["max_rgb"]
        row = {
            "family": family,
            "view": "energy_clamp_policy",
            "preclamp_luma_mean": mean_rgb[0],
            "clamp_mask_mean": mean_rgb[1],
            "clamp_ratio_mean": mean_rgb[2],
            "preclamp_luma_max": max_rgb[0],
            "clamp_mask_max": max_rgb[1],
            "clamp_ratio_max": max_rgb[2],
            "nonblack_ratio": metrics["nonblack_ratio"],
            "capture": str(capture),
        }
        if row["clamp_mask_mean"] > 0.02:
            warnings.append(
                f"{family} clamp_mask_mean {row['clamp_mask_mean']:.6f} is elevated"
            )
        if row["clamp_ratio_mean"] > 0.02:
            warnings.append(
                f"{family} clamp_ratio_mean {row['clamp_ratio_mean']:.6f} is elevated"
            )
        if row["clamp_mask_mean"] > 0.10 or row["clamp_ratio_mean"] > 0.10:
            failures.append(
                f"{family} CompositeV3 clamp debt is too high: "
                f"mask={row['clamp_mask_mean']:.6f} ratio={row['clamp_ratio_mean']:.6f}"
            )
        rows.append(row)

    for family_result in rows_by_view.get("overbright_diagnostics", []):
        family = str(family_result.get("family", ""))
        capture = pathlib.Path(str(family_result.get("capture", "")))
        if not capture.exists():
            failures.append(f"missing overbright_diagnostics capture for {family}: {capture}")
            continue
        metrics = measure_capture(capture)
        mean_rgb = metrics["mean_rgb"]
        max_rgb = metrics["max_rgb"]
        row = {
            "family": family,
            "view": "overbright_diagnostics",
            "overbright_mean": mean_rgb[0],
            "underlit_mean": mean_rgb[1],
            "legacy_rescue_mean": mean_rgb[2],
            "overbright_max": max_rgb[0],
            "underlit_max": max_rgb[1],
            "legacy_rescue_max": max_rgb[2],
            "nonblack_ratio": metrics["nonblack_ratio"],
            "capture": str(capture),
        }
        if row["legacy_rescue_mean"] > 0.05:
            warnings.append(
                f"{family} legacy_rescue_mean {row['legacy_rescue_mean']:.6f} shows CompositeV3 still leans on hdr_color"
            )
        if row["underlit_mean"] > 0.20:
            warnings.append(
                f"{family} underlit_mean {row['underlit_mean']:.6f} is elevated"
            )
        if row["legacy_rescue_mean"] > 0.25:
            failures.append(
                f"{family} legacy_rescue_mean {row['legacy_rescue_mean']:.6f} exceeds CompositeV3 review gate"
            )
        rows.append(row)

    for family_result in rows_by_view.get("composite_contribution_map", []):
        family = str(family_result.get("family", ""))
        capture = pathlib.Path(str(family_result.get("capture", "")))
        if not capture.exists():
            failures.append(f"missing composite_contribution_map capture for {family}: {capture}")
            continue
        metrics = measure_capture(capture)
        mean_rgb = metrics["mean_rgb"]
        max_rgb = metrics["max_rgb"]
        row = {
            "family": family,
            "view": "composite_contribution_map",
            "direct_contribution_mean": mean_rgb[0],
            "indirect_env_material_contribution_mean": mean_rgb[1],
            "reflection_contribution_mean": mean_rgb[2],
            "direct_contribution_max": max_rgb[0],
            "indirect_env_material_contribution_max": max_rgb[1],
            "reflection_contribution_max": max_rgb[2],
            "nonblack_ratio": metrics["nonblack_ratio"],
            "capture": str(capture),
        }
        if row["nonblack_ratio"] < 0.01:
            failures.append(f"{family} composite_contribution_map is effectively blank")
        if row["direct_contribution_mean"] < 0.001 and row["indirect_env_material_contribution_mean"] < 0.001:
            warnings.append(f"{family} composite_contribution_map shows little owned lighting contribution")
        rows.append(row)

    for family_result in rows_by_view.get("legacy_rescue_usage", []):
        family = str(family_result.get("family", ""))
        capture = pathlib.Path(str(family_result.get("capture", "")))
        if not capture.exists():
            failures.append(f"missing legacy_rescue_usage capture for {family}: {capture}")
            continue
        metrics = measure_capture(capture)
        mean_rgb = metrics["mean_rgb"]
        max_rgb = metrics["max_rgb"]
        row = {
            "family": family,
            "view": "legacy_rescue_usage",
            "legacy_rescue_used_mean": mean_rgb[0],
            "fallback_luma_mean": mean_rgb[1],
            "rescue_weight_mean": mean_rgb[2],
            "legacy_rescue_used_max": max_rgb[0],
            "fallback_luma_max": max_rgb[1],
            "rescue_weight_max": max_rgb[2],
            "nonblack_ratio": metrics["nonblack_ratio"],
            "capture": str(capture),
        }
        if row["legacy_rescue_used_mean"] > 0.05:
            warnings.append(
                f"{family} explicit legacy_rescue_usage mean {row['legacy_rescue_used_mean']:.6f} shows CompositeV3 rescue debt"
            )
        if row["legacy_rescue_used_mean"] > 0.25:
            failures.append(
                f"{family} explicit legacy_rescue_usage mean {row['legacy_rescue_used_mean']:.6f} exceeds review gate"
            )
        rows.append(row)

    energy_rows = [row for row in rows if row["view"] == "energy_clamp_policy"]
    overbright_rows = [row for row in rows if row["view"] == "overbright_diagnostics"]
    contribution_rows = [row for row in rows if row["view"] == "composite_contribution_map"]
    rescue_rows = [row for row in rows if row["view"] == "legacy_rescue_usage"]
    summary = {
        "family_count": len({row["family"] for row in rows}),
        "energy_clamp_policy_count": len(energy_rows),
        "overbright_diagnostics_count": len(overbright_rows),
        "composite_contribution_map_count": len(contribution_rows),
        "legacy_rescue_usage_count": len(rescue_rows),
        "mean_clamp_mask": _mean([float(row["clamp_mask_mean"]) for row in energy_rows]),
        "mean_clamp_ratio": _mean([float(row["clamp_ratio_mean"]) for row in energy_rows]),
        "mean_legacy_rescue": _mean([float(row["legacy_rescue_mean"]) for row in overbright_rows]),
        "mean_explicit_legacy_rescue": _mean([float(row["legacy_rescue_used_mean"]) for row in rescue_rows]),
        "mean_underlit": _mean([float(row["underlit_mean"]) for row in overbright_rows]),
        "mean_overbright": _mean([float(row["overbright_mean"]) for row in overbright_rows]),
        "mean_direct_contribution": _mean([float(row["direct_contribution_mean"]) for row in contribution_rows]),
        "mean_reflection_contribution": _mean([float(row["reflection_contribution_mean"]) for row in contribution_rows]),
    }

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.composite_diagnostics.v1",
        "manifest": str(manifest_path),
        "output_root": manifest.get("output_root", ""),
        "ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "summary": summary,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: pathlib.Path) -> None:
    summary = report["summary"]
    lines = [
        "# CompositeV3 Diagnostics",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- ready: `{str(report['ready']).lower()}`",
        f"- failures: {len(report['failures'])}",
        f"- warnings: {len(report['warnings'])}",
        f"- families: {summary['family_count']}",
        f"- mean clamp mask: {summary['mean_clamp_mask']:.6f}",
        f"- mean clamp ratio: {summary['mean_clamp_ratio']:.6f}",
        f"- mean legacy rescue: {summary['mean_legacy_rescue']:.6f}",
        f"- mean explicit legacy rescue: {summary['mean_explicit_legacy_rescue']:.6f}",
        f"- mean underlit: {summary['mean_underlit']:.6f}",
        f"- mean overbright: {summary['mean_overbright']:.6f}",
        f"- mean direct contribution: {summary['mean_direct_contribution']:.6f}",
        f"- mean reflection contribution: {summary['mean_reflection_contribution']:.6f}",
        "",
        "| Family | View | Key Metrics | Capture |",
        "|---|---|---|---|",
    ]
    for row in report["rows"]:
        if row["view"] == "energy_clamp_policy":
            metrics = (
                f"preclamp={row['preclamp_luma_mean']:.6f}; "
                f"clamp_mask={row['clamp_mask_mean']:.6f}; "
                f"clamp_ratio={row['clamp_ratio_mean']:.6f}"
            )
        elif row["view"] == "overbright_diagnostics":
            metrics = (
                f"overbright={row['overbright_mean']:.6f}; "
                f"underlit={row['underlit_mean']:.6f}; "
                f"legacy_rescue={row['legacy_rescue_mean']:.6f}"
            )
        elif row["view"] == "composite_contribution_map":
            metrics = (
                f"direct={row['direct_contribution_mean']:.6f}; "
                f"indirect_env_material={row['indirect_env_material_contribution_mean']:.6f}; "
                f"reflection={row['reflection_contribution_mean']:.6f}"
            )
        else:
            metrics = (
                f"legacy_used={row['legacy_rescue_used_mean']:.6f}; "
                f"fallback_luma={row['fallback_luma_mean']:.6f}; "
                f"rescue_weight={row['rescue_weight_mean']:.6f}"
            )
        lines.append(f"| {row['family']} | {row['view']} | {metrics} | `{row['capture']}` |")

    if report["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in report["warnings"])
    if report["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--output-json", required=True, type=pathlib.Path)
    parser.add_argument("--output-md", required=True, type=pathlib.Path)
    args = parser.parse_args()

    report = analyze_manifest(args.manifest)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, args.output_md)

    if report["failures"]:
        for failure in report["failures"]:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("PASS: CompositeV3 diagnostics are measurable")
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
