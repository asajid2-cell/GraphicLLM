#!/usr/bin/env python3
"""Measure motion stability for concrete Full Scene Shader V3 lighting buffers."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_debug_view_metrics import read_bmp_rgb


V3_LIGHTING_VIEWS = [
    "v3_direct_lighting",
    "v3_direct_lighting_unshadowed",
    "v3_shadow_visibility",
    "v3_shadow_loss",
    "v3_indirect_lighting",
    "v3_lighting_energy_budget",
    "v3_shadow_source_attribution",
]

CANDIDATE_COMPOSITE_VIEWS = [
    "candidate_hdr_scene_color",
    "reflection_radiance",
    "reflection_confidence",
    "reflection_source_id",
    "reflection_rejected_source_mask",
    "reflection_temporal_delta",
    "reflection_ssr_source_signal",
    "reflection_rt_source_signal",
    "reflection_source_suppression",
    "reflection_history_v3_curr",
    "reflection_history_v3_prev",
    "reflection_history_v3_validity",
    "reflection_history_v3_rejection",
]

REFLECTION_DIAGNOSTIC_MOTION_VIEWS = {
    "reflection_rejected_source_mask",
    "reflection_temporal_delta",
    "reflection_source_suppression",
    "reflection_history_v3_validity",
    "reflection_history_v3_rejection",
}

LEGACY_PAIRS = {
    "v3_direct_lighting": "direct_light",
    "v3_direct_lighting_unshadowed": "direct_light_unshadowed",
    "v3_shadow_visibility": "shadow_factor",
    "v3_shadow_loss": "direct_light_shadow_loss",
    "v3_indirect_lighting": "ambient_ibl",
}

FOCUS_VIEW_SETS = {
    "all": {
        "lighting": V3_LIGHTING_VIEWS,
        "candidate": CANDIDATE_COMPOSITE_VIEWS,
        "legacy": list(LEGACY_PAIRS.values()),
    },
    "reflection": {
        "lighting": [],
        "candidate": [
            "reflection_radiance",
            "reflection_confidence",
            "reflection_source_id",
            "reflection_rejected_source_mask",
            "reflection_temporal_delta",
            "reflection_ssr_source_signal",
            "reflection_rt_source_signal",
            "reflection_source_suppression",
            "reflection_history_v3_curr",
            "reflection_history_v3_prev",
            "reflection_history_v3_validity",
            "reflection_history_v3_rejection",
        ],
        "legacy": [],
    },
    "shadow": {
        "lighting": V3_LIGHTING_VIEWS,
        "candidate": [],
        "legacy": list(LEGACY_PAIRS.values()),
    },
}


def _luma(pixel: tuple[int, int, int]) -> float:
    r, g, b = pixel
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def measure_pair(path_a: Path, path_b: Path, threshold: float) -> dict[str, Any]:
    width_a, height_a, pixels_a = read_bmp_rgb(path_a)
    width_b, height_b, pixels_b = read_bmp_rgb(path_b)
    if width_a != width_b or height_a != height_b or len(pixels_a) != len(pixels_b):
        raise ValueError(f"dimension mismatch: {path_a} vs {path_b}")

    count = max(len(pixels_a), 1)
    threshold_255 = threshold * 255.0
    luma_sum = 0.0
    luma_max = 0.0
    active = 0
    for pixel_a, pixel_b in zip(pixels_a, pixels_b):
        delta = abs(_luma(pixel_a) - _luma(pixel_b))
        luma_sum += delta
        luma_max = max(luma_max, delta)
        if delta > threshold_255:
            active += 1

    return {
        "a": str(path_a),
        "b": str(path_b),
        "width": width_a,
        "height": height_a,
        "pixel_count": count,
        "mean_abs_luma_delta": luma_sum / count / 255.0,
        "max_abs_luma_delta": luma_max / 255.0,
        "active_delta_ratio": active / count,
    }


def summarize_pairs(pairs: list[dict[str, Any]]) -> dict[str, Any]:
    if not pairs:
        return {
            "pair_count": 0,
            "mean_abs_luma_delta": 0.0,
            "max_pair_mean_abs_luma_delta": 0.0,
            "max_abs_luma_delta": 0.0,
            "mean_active_delta_ratio": 0.0,
        }
    return {
        "pair_count": len(pairs),
        "mean_abs_luma_delta": sum(row["mean_abs_luma_delta"] for row in pairs) / len(pairs),
        "max_pair_mean_abs_luma_delta": max(row["mean_abs_luma_delta"] for row in pairs),
        "max_abs_luma_delta": max(row["max_abs_luma_delta"] for row in pairs),
        "mean_active_delta_ratio": sum(row["active_delta_ratio"] for row in pairs) / len(pairs),
    }


def build_view_rows(
    manifest: dict[str, Any],
    *,
    delta_threshold: float,
    min_sequence_count: int,
    focus: str,
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    focus_views = FOCUS_VIEW_SETS[focus]
    measured_views = (
        set(focus_views["lighting"])
        | set(focus_views["candidate"])
        | set(focus_views["legacy"])
        | {"beauty"}
    )

    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        view = str(result.get("view", ""))
        if view not in measured_views:
            continue

        sequence = [Path(path) for path in result.get("capture_sequence", []) if path]
        if len(sequence) < min_sequence_count:
            failures.append(
                f"{result.get('family', '')}/{view}: expected at least "
                f"{min_sequence_count} captures, got {len(sequence)}"
            )
            continue

        pairs: list[dict[str, Any]] = []
        for path_a, path_b in zip(sequence, sequence[1:]):
            try:
                pairs.append(measure_pair(path_a, path_b, delta_threshold))
            except Exception as exc:  # noqa: BLE001 - include artifact path in report.
                failures.append(f"{result.get('family', '')}/{view}: {exc}")
                pairs = []
                break

        rows.append(
            {
                "family": str(result.get("family", "")),
                "view": view,
                "debug_view": result.get("debug_view"),
                "scene": str(result.get("scene", "")),
                "stability_motion_mode": str(
                    result.get("stability_motion_mode", manifest.get("stability_motion_mode", ""))
                ),
                "capture_count": len(sequence),
                "summary": summarize_pairs(pairs),
                "pairs": pairs,
            }
        )

    return rows, failures


def build_family_rows(
    rows: list[dict[str, Any]],
    ratio_warning: float,
    *,
    focus: str,
) -> tuple[list[dict[str, Any]], list[str]]:
    warnings: list[str] = []
    by_family: dict[str, dict[str, dict[str, Any]]] = {}
    for row in rows:
        by_family.setdefault(row["family"], {})[row["view"]] = row
    focus_views = FOCUS_VIEW_SETS[focus]

    family_rows: list[dict[str, Any]] = []
    for family in sorted(by_family):
        views = by_family[family]
        family_status = "ok"
        view_rows: list[dict[str, Any]] = []
        beauty_delta = views.get("beauty", {}).get("summary", {}).get("mean_abs_luma_delta", 0.0)

        for v3_view in focus_views["lighting"]:
            v3_row = views.get(v3_view)
            legacy_view = LEGACY_PAIRS.get(v3_view, "")
            legacy_row = views.get(legacy_view) if legacy_view else None
            if not v3_row:
                family_status = "missing_v3_view"
                view_rows.append({"view": v3_view, "status": "missing_v3_view"})
                continue

            v3_delta = v3_row["summary"]["mean_abs_luma_delta"]
            legacy_delta = 0.0
            ratio = 0.0
            status = "ok"
            if legacy_row:
                legacy_delta = legacy_row["summary"]["mean_abs_luma_delta"]
                ratio = v3_delta / max(legacy_delta, 1e-6)
                if ratio > ratio_warning and v3_delta > 0.0025:
                    status = "v3_motion_above_legacy"
                    family_status = "motion_warning"
                    warnings.append(
                        f"{family}/{v3_view}: motion delta {v3_delta:.6f} is "
                        f"{ratio:.2f}x legacy {legacy_view} ({legacy_delta:.6f})"
                    )
            elif v3_delta > 0.02 and (v3_delta / max(beauty_delta, 1e-6)) > 1.75:
                status = "v3_motion_above_beauty"
                family_status = "motion_warning"
                warnings.append(
                    f"{family}/{v3_view}: diagnostic motion delta {v3_delta:.6f} is "
                    f"{v3_delta / max(beauty_delta, 1e-6):.2f}x beauty ({beauty_delta:.6f})"
                )
            view_rows.append(
                {
                    "view": v3_view,
                    "legacy_view": legacy_view,
                    "status": status,
                    "v3_mean_abs_luma_delta": v3_delta,
                    "legacy_mean_abs_luma_delta": legacy_delta,
                    "v3_over_legacy_ratio": ratio,
                    "beauty_mean_abs_luma_delta": beauty_delta,
                    "v3_over_beauty_ratio": v3_delta / max(beauty_delta, 1e-6),
                    "v3_active_delta_ratio": v3_row["summary"]["mean_active_delta_ratio"],
                }
            )

        for candidate_view in focus_views["candidate"]:
            candidate_row = views.get(candidate_view)
            if not candidate_row:
                family_status = "missing_candidate_composite_view"
                view_rows.append({"view": candidate_view, "status": "missing_candidate_composite_view"})
                continue

            candidate_delta = candidate_row["summary"]["mean_abs_luma_delta"]
            beauty_ratio = candidate_delta / max(beauty_delta, 1e-6)
            active_delta = candidate_row["summary"]["mean_active_delta_ratio"]
            status = "ok"
            if (
                candidate_view in REFLECTION_DIAGNOSTIC_MOTION_VIEWS
                and candidate_delta > 0.02
                and beauty_ratio > 1.75
            ):
                status = "reflection_diagnostic_motion_warning"
                family_status = "motion_warning"
                warnings.append(
                    f"{family}/{candidate_view}: reflection diagnostic motion delta "
                    f"{candidate_delta:.6f} is {beauty_ratio:.2f}x beauty ({beauty_delta:.6f})"
                )
            view_rows.append(
                {
                    "view": candidate_view,
                    "legacy_view": "",
                    "status": status,
                    "v3_mean_abs_luma_delta": candidate_delta,
                    "legacy_mean_abs_luma_delta": 0.0,
                    "v3_over_legacy_ratio": 0.0,
                    "beauty_mean_abs_luma_delta": beauty_delta,
                    "v3_over_beauty_ratio": beauty_ratio,
                    "v3_active_delta_ratio": active_delta,
                }
            )

        family_rows.append(
            {
                "family": family,
                "status": family_status,
                "stability_motion_mode": next(
                    (row.get("stability_motion_mode", "") for row in views.values()),
                    "",
                ),
                "beauty_mean_abs_luma_delta": beauty_delta,
                "views": view_rows,
            }
        )

    return family_rows, warnings


def build_report(
    manifest_path: Path,
    *,
    delta_threshold: float,
    min_sequence_count: int,
    ratio_warning: float,
    focus: str,
) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    rows, failures = build_view_rows(
        manifest,
        delta_threshold=delta_threshold,
        min_sequence_count=min_sequence_count,
        focus=focus,
    )
    families, warnings = build_family_rows(rows, ratio_warning, focus=focus)

    required_families = {
        str(result.get("family", ""))
        for result in manifest.get("results", [])
        if isinstance(result, dict)
    }
    present_families = {row["family"] for row in families}
    missing_families = sorted(required_families - present_families)
    if missing_families:
        failures.append("missing V3 lighting motion rows for families: " + ", ".join(missing_families))

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.lighting_motion.v1",
        "manifest": str(manifest_path),
        "stability_motion_mode": manifest.get("stability_motion_mode", ""),
        "capture_sequence_count": manifest.get("capture_sequence_count", 0),
        "delta_threshold": delta_threshold,
        "min_sequence_count": min_sequence_count,
        "focus": focus,
        "required_lighting_views": FOCUS_VIEW_SETS[focus]["lighting"],
        "required_candidate_views": FOCUS_VIEW_SETS[focus]["candidate"],
        "v3_over_legacy_warning_ratio": ratio_warning,
        "reflection_diagnostic_motion_warning_ratio": 1.75,
        "reflection_diagnostic_motion_min_delta": 0.02,
        "family_count": len(families),
        "view_row_count": len(rows),
        "failures": failures,
        "warnings": warnings,
        "families": families,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Full Scene Shader V3 Lighting Motion",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- motion mode: `{report['stability_motion_mode']}`",
        f"- capture sequence count: {report['capture_sequence_count']}",
        f"- families: {report['family_count']}",
        f"- view rows: {report['view_row_count']}",
        f"- warnings: {len(report['warnings'])}",
        f"- failures: {len(report['failures'])}",
        "",
        "| Family | Motion | View | Status | V3 Delta | Legacy Delta | V3/Legacy | Beauty Delta | V3/Beauty | Active Delta |",
        "|---|---|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for family in report["families"]:
        for row in family["views"]:
            lines.append(
                "| {family} | {motion} | {view} | {status} | {v3:.8f} | {legacy:.8f} | {legacy_ratio:.3f} | {beauty:.8f} | {beauty_ratio:.3f} | {active:.8f} |".format(
                    family=family["family"],
                    motion=family["stability_motion_mode"],
                    view=row.get("view", ""),
                    status=row.get("status", ""),
                    v3=row.get("v3_mean_abs_luma_delta", 0.0),
                    legacy=row.get("legacy_mean_abs_luma_delta", 0.0),
                    legacy_ratio=row.get("v3_over_legacy_ratio", 0.0),
                    beauty=row.get("beauty_mean_abs_luma_delta", 0.0),
                    beauty_ratio=row.get("v3_over_beauty_ratio", 0.0),
                    active=row.get("v3_active_delta_ratio", 0.0),
                )
            )
    if report["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in report["warnings"])
    if report["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-md", type=Path)
    parser.add_argument("--delta-threshold", type=float, default=0.02)
    parser.add_argument("--min-sequence-count", type=int, default=2)
    parser.add_argument("--v3-over-legacy-warning-ratio", type=float, default=1.50)
    parser.add_argument("--focus", choices=sorted(FOCUS_VIEW_SETS), default="all")
    parser.add_argument("--fail-on-warning", action="store_true")
    args = parser.parse_args()

    report = build_report(
        args.manifest,
        delta_threshold=args.delta_threshold,
        min_sequence_count=args.min_sequence_count,
        ratio_warning=args.v3_over_legacy_warning_ratio,
        focus=args.focus,
    )
    output_json = args.output_json or args.manifest.with_name("v3_lighting_motion.json")
    output_md = args.output_md or args.manifest.with_name("v3_lighting_motion.md")
    output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, output_md)

    for warning in report["warnings"]:
        print(f"WARN: {warning}")
    for failure in report["failures"]:
        print(f"ERROR: {failure}", file=sys.stderr)

    if report["failures"]:
        return 1
    if args.fail_on_warning and report["warnings"]:
        return 1

    print(
        "PASS: V3 lighting motion measured "
        f"{report['view_row_count']} view sequences across {report['family_count']} families"
    )
    print(f"json={output_json}")
    print(f"markdown={output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
