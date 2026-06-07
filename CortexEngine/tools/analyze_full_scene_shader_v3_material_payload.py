#!/usr/bin/env python3
"""Gate V3 material payload evidence from packet debug views and frame reports."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


REQUIRED_DEBUG_VIEWS = {
    "material_base_color": {"min_nonblack_ratio": 0.05, "min_mean_luma": 0.01, "max_mean_luma": 0.98},
    "material_normal": {"min_nonblack_ratio": 0.05, "min_mean_luma": 0.01, "max_mean_luma": 0.98},
    "roughness": {"min_nonblack_ratio": 0.05, "min_mean_luma": 0.01, "max_mean_luma": 0.98},
    "surface_class": {"min_nonblack_ratio": 0.01},
    "surface_policy": {"min_nonblack_ratio": 0.01},
    "material_family": {"min_nonblack_ratio": 0.01},
    "reflection_policy": {"min_nonblack_ratio": 0.01},
    "temporal_policy": {"min_nonblack_ratio": 0.01},
    "post_sensitivity": {"min_nonblack_ratio": 0.01},
    "material_id": {"min_nonblack_ratio": 0.001},
    "object_id": {"min_nonblack_ratio": 0.001},
}

OPTIONAL_DEBUG_VIEWS = {
    "metallic": {"warn_nonblack_below": 0.001},
}

CONTRACT_DEBUG_VIEW_ALIASES = {
    "material_base_color": "material_base_color",
    "material_roughness": "roughness",
    "material_metallic": "metallic",
    "material_normal": "material_normal",
    "material_class": "surface_class",
    "material_missing_channel_mask": "",
}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def metrics_by_view(metrics: dict[str, Any]) -> dict[tuple[str, str], list[dict[str, Any]]]:
    rows: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for row in metrics.get("rows", []):
        if not isinstance(row, dict):
            continue
        family = str(row.get("family", ""))
        view = str(row.get("view", ""))
        rows.setdefault((family, view), []).append(row)
    return rows


def report_paths(manifest: dict[str, Any]) -> list[Path]:
    paths: list[Path] = []
    seen: set[str] = set()
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        report = row.get("report")
        if not isinstance(report, str) or not report:
            continue
        if report in seen:
            continue
        seen.add(report)
        paths.append(Path(report))
    return paths


def captured_families(manifest: dict[str, Any]) -> list[str]:
    families = {
        str(row.get("family", ""))
        for row in manifest.get("results", [])
        if isinstance(row, dict) and row.get("family")
    }
    return sorted(families)


def load_material_contract_debug_views() -> list[str]:
    contract_path = Path(__file__).resolve().parents[1] / "assets/final_art/full_scene_shader_pipeline_v3_contract.json"
    if not contract_path.exists():
        return []
    contract = load_json(contract_path)
    domains = contract.get("domains")
    if not isinstance(domains, dict):
        return []
    material = domains.get("material")
    if not isinstance(material, dict):
        return []
    views = material.get("required_debug_views")
    if not isinstance(views, list):
        return []
    return [str(view) for view in views if str(view)]


def check_contract_debug_view_coverage(
    *,
    contract_views: list[str],
    captured_views: set[str],
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    warnings: list[str] = []
    for contract_view in contract_views:
        packet_view = CONTRACT_DEBUG_VIEW_ALIASES.get(contract_view, "")
        status = "covered"
        if not packet_view:
            status = "missing_packet_alias"
            warnings.append(f"contract material debug view '{contract_view}' has no packet/debug-view alias yet")
        elif packet_view not in captured_views:
            status = "not_captured"
            warnings.append(
                f"contract material debug view '{contract_view}' maps to '{packet_view}' but was not captured"
            )
        rows.append(
            {
                "contract_view": contract_view,
                "packet_view": packet_view,
                "status": status,
            }
        )
    return rows, warnings


def check_view_metrics(
    *,
    families: list[str],
    rows_by_view: dict[tuple[str, str], list[dict[str, Any]]],
) -> tuple[list[dict[str, Any]], list[str], list[str]]:
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []

    for family in families:
        for view, thresholds in REQUIRED_DEBUG_VIEWS.items():
            candidates = rows_by_view.get((family, view), [])
            if not candidates:
                failures.append(f"{family}:{view}: required material debug view missing")
                rows.append({"family": family, "view": view, "status": "missing"})
                continue
            metric = candidates[0].get("metrics", {})
            if not isinstance(metric, dict):
                failures.append(f"{family}:{view}: material debug metrics malformed")
                rows.append({"family": family, "view": view, "status": "malformed"})
                continue
            mean_luma = float(metric.get("mean_luma", 0.0) or 0.0)
            nonblack = float(metric.get("nonblack_ratio", 0.0) or 0.0)
            max_luma = float(metric.get("max_luma", 0.0) or 0.0)
            status = "ok"
            min_nonblack = thresholds.get("min_nonblack_ratio")
            if min_nonblack is not None and nonblack < float(min_nonblack):
                failures.append(
                    f"{family}:{view}: nonblack_ratio {nonblack:.6f} below {float(min_nonblack):.6f}"
                )
                status = "failed"
            min_mean = thresholds.get("min_mean_luma")
            if min_mean is not None and mean_luma < float(min_mean):
                failures.append(
                    f"{family}:{view}: mean_luma {mean_luma:.6f} below {float(min_mean):.6f}"
                )
                status = "failed"
            max_mean = thresholds.get("max_mean_luma")
            if max_mean is not None and mean_luma > float(max_mean):
                failures.append(
                    f"{family}:{view}: mean_luma {mean_luma:.6f} above {float(max_mean):.6f}"
                )
                status = "failed"
            rows.append(
                {
                    "family": family,
                    "view": view,
                    "status": status,
                    "mean_luma": mean_luma,
                    "max_luma": max_luma,
                    "nonblack_ratio": nonblack,
                }
            )

        for view, thresholds in OPTIONAL_DEBUG_VIEWS.items():
            candidates = rows_by_view.get((family, view), [])
            if not candidates:
                warnings.append(f"{family}:{view}: optional material debug view missing")
                rows.append({"family": family, "view": view, "status": "missing_optional"})
                continue
            metric = candidates[0].get("metrics", {})
            if not isinstance(metric, dict):
                warnings.append(f"{family}:{view}: optional material debug metrics malformed")
                rows.append({"family": family, "view": view, "status": "malformed_optional"})
                continue
            mean_luma = float(metric.get("mean_luma", 0.0) or 0.0)
            nonblack = float(metric.get("nonblack_ratio", 0.0) or 0.0)
            status = "ok"
            warn_nonblack_below = thresholds.get("warn_nonblack_below")
            if warn_nonblack_below is not None and nonblack < float(warn_nonblack_below):
                warnings.append(
                    f"{family}:{view}: nonblack_ratio {nonblack:.6f} below optional signal {float(warn_nonblack_below):.6f}"
                )
                status = "weak_optional"
            rows.append(
                {
                    "family": family,
                    "view": view,
                    "status": status,
                    "mean_luma": mean_luma,
                    "nonblack_ratio": nonblack,
                }
            )

    return rows, failures, warnings


def material_stats_from_report(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    report = load_json(path)
    frame_contract = report.get("frame_contract")
    if not isinstance(frame_contract, dict):
        return None
    materials = frame_contract.get("materials")
    return materials if isinstance(materials, dict) else None


def check_material_stats(paths: list[Path]) -> tuple[list[dict[str, Any]], list[str], list[str]]:
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []

    for path in paths:
        stats = material_stats_from_report(path)
        if stats is None:
            failures.append(f"{path}: frame_contract.materials missing")
            rows.append({"report": str(path), "status": "missing"})
            continue

        sampled = int(stats.get("sampled", 0) or 0)
        preset_named = int(stats.get("preset_named", 0) or 0)
        scene_material_default = int(stats.get("scene_material_default", 0) or 0)
        validation_errors = int(stats.get("validation_errors", 0) or 0)
        validation_issues = int(stats.get("validation_issues", 0) or 0)
        unresolved_default_roughness = int(stats.get("unresolved_default_roughness_fallback", 0) or 0)
        unresolved_default_transmission = int(stats.get("unresolved_default_transmission_fallback", 0) or 0)
        class_authored_roughness = int(stats.get("preset_class_authored_default_roughness", 0) or 0)
        class_authored_transmission = int(stats.get("preset_class_authored_default_transmission", 0) or 0)
        roughness_out = int(stats.get("roughness_out_of_range", 0) or 0)
        metallic_out = int(stats.get("metallic_out_of_range", 0) or 0)
        avg_roughness = float(stats.get("avg_roughness", 0.0) or 0.0)
        min_roughness = float(stats.get("min_roughness", 0.0) or 0.0)
        max_roughness = float(stats.get("max_roughness", 0.0) or 0.0)
        avg_metallic = float(stats.get("avg_metallic", 0.0) or 0.0)
        min_metallic = float(stats.get("min_metallic", 0.0) or 0.0)
        max_metallic = float(stats.get("max_metallic", 0.0) or 0.0)
        avg_albedo = float(stats.get("avg_albedo_luminance", 0.0) or 0.0)
        min_albedo = float(stats.get("min_albedo_luminance", 0.0) or 0.0)
        max_albedo = float(stats.get("max_albedo_luminance", 0.0) or 0.0)

        status = "ok"
        if sampled <= 0:
            failures.append(f"{path}: material sampled count is zero")
            status = "failed"
        if validation_errors or validation_issues:
            failures.append(
                f"{path}: material validation errors/issues nonzero ({validation_errors}/{validation_issues})"
            )
            status = "failed"
        if roughness_out or metallic_out:
            failures.append(
                f"{path}: material roughness/metallic out-of-range nonzero ({roughness_out}/{metallic_out})"
            )
            status = "failed"
        if scene_material_default > 0:
            failures.append(f"{path}: scene_material_default count {scene_material_default} is unresolved material debt")
            status = "failed"
        if unresolved_default_roughness > 0 or unresolved_default_transmission > 0:
            failures.append(
                f"{path}: unresolved default roughness/transmission fallback nonzero "
                f"({unresolved_default_roughness}/{unresolved_default_transmission})"
            )
            status = "failed"
        if sampled > 0 and preset_named / sampled < 0.80:
            failures.append(f"{path}: named material preset coverage below 80% ({preset_named}/{sampled})")
            status = "failed"
        if not (0.0 <= min_roughness <= avg_roughness <= max_roughness <= 1.0):
            failures.append(
                f"{path}: roughness range invalid min/avg/max={min_roughness:.4f}/{avg_roughness:.4f}/{max_roughness:.4f}"
            )
            status = "failed"
        if not (0.0 <= min_metallic <= avg_metallic <= max_metallic <= 1.0):
            failures.append(
                f"{path}: metallic range invalid min/avg/max={min_metallic:.4f}/{avg_metallic:.4f}/{max_metallic:.4f}"
            )
            status = "failed"
        if not (0.0 <= min_albedo <= avg_albedo <= max_albedo <= 1.0):
            failures.append(
                f"{path}: albedo luminance range invalid min/avg/max={min_albedo:.4f}/{avg_albedo:.4f}/{max_albedo:.4f}"
            )
            status = "failed"

        rows.append(
            {
                "report": str(path),
                "status": status,
                "sampled": sampled,
                "preset_named": preset_named,
                "scene_material_default": scene_material_default,
                "validation_errors": validation_errors,
                "validation_issues": validation_issues,
                "roughness_out_of_range": roughness_out,
                "metallic_out_of_range": metallic_out,
                "unresolved_default_roughness_fallback": unresolved_default_roughness,
                "unresolved_default_transmission_fallback": unresolved_default_transmission,
                "preset_class_authored_default_roughness": class_authored_roughness,
                "preset_class_authored_default_transmission": class_authored_transmission,
                "avg_roughness": avg_roughness,
                "avg_metallic": avg_metallic,
                "avg_albedo_luminance": avg_albedo,
                "reflection_eligible": int(stats.get("reflection_eligible", 0) or 0),
                "advanced_feature_materials": int(stats.get("advanced_feature_materials", 0) or 0),
            }
        )

    return rows, failures, warnings


def build_report(manifest_path: Path) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    metrics_path = manifest_path.with_name("debug_view_metrics.json")
    failures: list[str] = []
    warnings: list[str] = []

    if not metrics_path.exists():
        return {
            "schema": "cortex.full_scene_shader_pipeline_v3.material_payload.v1",
            "manifest": str(manifest_path),
            "debug_view_metrics": str(metrics_path),
            "ready": False,
            "failures": [f"missing debug_view_metrics.json: {metrics_path}"],
            "warnings": warnings,
            "debug_view_rows": [],
            "material_stat_rows": [],
            "summary": {},
        }

    metrics = load_json(metrics_path)
    failures.extend(str(item) for item in metrics.get("failures", []))
    families = captured_families(manifest)
    captured_views = {
        str(row.get("view", ""))
        for row in manifest.get("results", [])
        if isinstance(row, dict) and row.get("view")
    }
    rows_by_view = metrics_by_view(metrics)
    debug_rows, debug_failures, debug_warnings = check_view_metrics(families=families, rows_by_view=rows_by_view)
    stat_rows, stat_failures, stat_warnings = check_material_stats(report_paths(manifest))
    contract_views = load_material_contract_debug_views()
    contract_rows, contract_warnings = check_contract_debug_view_coverage(
        contract_views=contract_views,
        captured_views=captured_views,
    )
    failures.extend(debug_failures)
    failures.extend(stat_failures)
    warnings.extend(debug_warnings)
    warnings.extend(stat_warnings)
    warnings.extend(contract_warnings)

    summary = {
        "family_count": len(families),
        "required_debug_view_count": len(REQUIRED_DEBUG_VIEWS),
        "optional_debug_view_count": len(OPTIONAL_DEBUG_VIEWS),
        "contract_required_debug_view_count": len(contract_views),
        "contract_debug_view_debt_count": sum(1 for row in contract_rows if row["status"] != "covered"),
        "material_report_count": len(stat_rows),
        "sampled_materials_total": sum(int(row.get("sampled", 0) or 0) for row in stat_rows),
        "named_materials_total": sum(int(row.get("preset_named", 0) or 0) for row in stat_rows),
        "advanced_feature_materials_total": sum(int(row.get("advanced_feature_materials", 0) or 0) for row in stat_rows),
        "reflection_eligible_total": sum(int(row.get("reflection_eligible", 0) or 0) for row in stat_rows),
        "class_authored_default_roughness_total": sum(
            int(row.get("preset_class_authored_default_roughness", 0) or 0) for row in stat_rows
        ),
        "class_authored_default_transmission_total": sum(
            int(row.get("preset_class_authored_default_transmission", 0) or 0) for row in stat_rows
        ),
        "unresolved_default_roughness_fallback_total": sum(
            int(row.get("unresolved_default_roughness_fallback", 0) or 0) for row in stat_rows
        ),
        "unresolved_default_transmission_fallback_total": sum(
            int(row.get("unresolved_default_transmission_fallback", 0) or 0) for row in stat_rows
        ),
    }

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.material_payload.v1",
        "manifest": str(manifest_path),
        "debug_view_metrics": str(metrics_path),
        "ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "summary": summary,
        "debug_view_rows": debug_rows,
        "contract_debug_view_rows": contract_rows,
        "material_stat_rows": stat_rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    summary = report.get("summary", {})
    lines = [
        "# V3 Material Payload Diagnostics",
        "",
        f"- manifest: `{report.get('manifest')}`",
        f"- ready: `{str(report.get('ready')).lower()}`",
        f"- failures: {len(report.get('failures', []))}",
        f"- warnings: {len(report.get('warnings', []))}",
        f"- families: {summary.get('family_count', 0)}",
        f"- sampled materials: {summary.get('sampled_materials_total', 0)}",
        f"- named materials: {summary.get('named_materials_total', 0)}",
        f"- advanced feature materials: {summary.get('advanced_feature_materials_total', 0)}",
        f"- reflection eligible: {summary.get('reflection_eligible_total', 0)}",
        f"- class-authored roughness defaults: {summary.get('class_authored_default_roughness_total', 0)}",
        f"- class-authored transmission defaults: {summary.get('class_authored_default_transmission_total', 0)}",
        f"- unresolved roughness fallback: {summary.get('unresolved_default_roughness_fallback_total', 0)}",
        f"- unresolved transmission fallback: {summary.get('unresolved_default_transmission_fallback_total', 0)}",
        f"- contract required debug views: {summary.get('contract_required_debug_view_count', 0)}",
        f"- contract debug view debt: {summary.get('contract_debug_view_debt_count', 0)}",
        "",
        "## Contract Debug View Coverage",
        "",
        "| Contract View | Packet View | Status |",
        "|---|---|---|",
    ]
    for row in report.get("contract_debug_view_rows", []):
        lines.append(
            "| {contract_view} | {packet_view} | {status} |".format(
                contract_view=row.get("contract_view", ""),
                packet_view=row.get("packet_view", ""),
                status=row.get("status", ""),
            )
        )
    lines.extend([
        "",
        "## Captured Debug Views",
        "",
        "| Family | View | Status | Mean Luma | Nonblack |",
        "|---|---|---|---:|---:|",
    ])
    for row in report.get("debug_view_rows", []):
        lines.append(
            "| {family} | {view} | {status} | {mean:.5f} | {nonblack:.5f} |".format(
                family=row.get("family", ""),
                view=row.get("view", ""),
                status=row.get("status", ""),
                mean=float(row.get("mean_luma", 0.0) or 0.0),
                nonblack=float(row.get("nonblack_ratio", 0.0) or 0.0),
            )
        )
    lines.extend(["", "| Report | Status | Sampled | Named | Avg Rough | Avg Metal | Avg Albedo |", "|---|---|---:|---:|---:|---:|---:|"])
    for row in report.get("material_stat_rows", []):
        lines.append(
            "| {report_path} | {status} | {sampled} | {named} | {rough:.4f} | {metal:.4f} | {albedo:.4f} |".format(
                report_path=row.get("report", ""),
                status=row.get("status", ""),
                sampled=int(row.get("sampled", 0) or 0),
                named=int(row.get("preset_named", 0) or 0),
                rough=float(row.get("avg_roughness", 0.0) or 0.0),
                metal=float(row.get("avg_metallic", 0.0) or 0.0),
                albedo=float(row.get("avg_albedo_luminance", 0.0) or 0.0),
            )
        )
    if report.get("failures"):
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {item}" for item in report["failures"])
    if report.get("warnings"):
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {item}" for item in report["warnings"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    report = build_report(args.manifest)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, args.output_md)
    if report["failures"]:
        for failure in report["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: V3 material payload diagnostics are measurable")
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
