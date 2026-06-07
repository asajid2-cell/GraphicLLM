#!/usr/bin/env python3
"""Analyze ReflectionV3 source resolver stability.

The generic motion analyzer treats debug views as luma images. Reflection source
IDs are categorical: red encodes the selected provider class, green encodes
confidence, and blue encodes source override policy. This analyzer measures
source-class churn directly so smooth/metallic jitter cannot hide behind an
acceptable beauty-frame delta.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_debug_view_metrics import read_bmp_rgb


SOURCE_ID_VIEW = "reflection_source_id"
DIAGNOSTIC_VIEWS = {
    "reflection_ssr_source_signal": ("ssr_raw_luma", "ssr_raw_alpha", "ssr_admitted_confidence"),
    "reflection_rt_source_signal": ("rt_raw_luma", "rt_raw_alpha", "rt_admitted_confidence"),
    "reflection_rejected_source_mask": ("local_rejected", "ssr_rejected", "rt_or_environment_rejected"),
    "reflection_temporal_delta": ("inactive_continuous", "forced_unavailable", "history_required_missing"),
    "reflection_source_suppression": ("history_suppression", "material_suppression", "roughness"),
    "reflection_history_v3_validity": ("current_active", "source_class", "history_reusable"),
    "reflection_history_v3_rejection": ("source_switch", "disocclusion_rejection", "high_motion_rejection"),
}


def _first_capture(result: dict[str, Any]) -> Path | None:
    sequence = [Path(path) for path in result.get("capture_sequence", []) if path]
    if sequence:
        return sequence[0]
    capture = result.get("capture")
    return Path(capture) if capture else None


def _source_name(value: float) -> str:
    if value < 0.08:
        return "none"
    if abs(value - 0.25) < 0.10:
        return "local"
    if abs(value - 0.50) < 0.10:
        return "ssr"
    if abs(value - 0.75) < 0.10:
        return "rt"
    if value > 0.88:
        return "environment"
    return "mixed"


def _summarize_source_frame(path: Path) -> dict[str, Any]:
    width, height, pixels = read_bmp_rgb(path)
    count = max(len(pixels), 1)
    source_sum = 0.0
    confidence_sum = 0.0
    override_sum = 0.0
    active = 0
    buckets = {
        "none": 0,
        "local": 0,
        "ssr": 0,
        "rt": 0,
        "environment": 0,
        "mixed": 0,
    }
    for r, g, b in pixels:
        source = r / 255.0
        confidence = g / 255.0
        override = b / 255.0
        source_sum += source
        confidence_sum += confidence
        override_sum += override
        if source > 0.08:
            active += 1
        buckets[_source_name(source)] += 1
    ratios = {key: value / count for key, value in buckets.items()}
    dominant = max(ratios.items(), key=lambda item: item[1])[0]
    return {
        "path": str(path),
        "width": width,
        "height": height,
        "pixel_count": count,
        "mean_source_class": source_sum / count,
        "mean_confidence": confidence_sum / count,
        "mean_override": override_sum / count,
        "active_source_ratio": active / count,
        "dominant_source": dominant,
        "source_ratios": ratios,
    }


def _measure_source_pair(path_a: Path, path_b: Path, switch_threshold: float) -> dict[str, Any]:
    width_a, height_a, pixels_a = read_bmp_rgb(path_a)
    width_b, height_b, pixels_b = read_bmp_rgb(path_b)
    if width_a != width_b or height_a != height_b or len(pixels_a) != len(pixels_b):
        raise ValueError(f"dimension mismatch: {path_a} vs {path_b}")

    count = max(len(pixels_a), 1)
    source_delta_sum = 0.0
    confidence_delta_sum = 0.0
    override_delta_sum = 0.0
    switched = 0
    active_switched = 0
    active_pixels = 0
    max_source_delta = 0.0
    for a, b in zip(pixels_a, pixels_b):
        source_a = a[0] / 255.0
        source_b = b[0] / 255.0
        confidence_a = a[1] / 255.0
        confidence_b = b[1] / 255.0
        override_a = a[2] / 255.0
        override_b = b[2] / 255.0
        source_delta = abs(source_a - source_b)
        confidence_delta = abs(confidence_a - confidence_b)
        override_delta = abs(override_a - override_b)
        source_delta_sum += source_delta
        confidence_delta_sum += confidence_delta
        override_delta_sum += override_delta
        max_source_delta = max(max_source_delta, source_delta)
        active_pair = max(source_a, source_b) > 0.08
        if active_pair:
            active_pixels += 1
        if source_delta > switch_threshold:
            switched += 1
            if active_pair:
                active_switched += 1

    return {
        "a": str(path_a),
        "b": str(path_b),
        "mean_source_delta": source_delta_sum / count,
        "mean_confidence_delta": confidence_delta_sum / count,
        "mean_override_delta": override_delta_sum / count,
        "max_source_delta": max_source_delta,
        "source_switch_ratio": switched / count,
        "active_source_switch_ratio": active_switched / max(active_pixels, 1),
        "active_pair_ratio": active_pixels / count,
    }


def _capture_sequence(result: dict[str, Any] | None) -> list[Path]:
    if not isinstance(result, dict):
        return []
    return [Path(path) for path in result.get("capture_sequence", []) if path]


def _summarize_rgb_frame(path: Path) -> dict[str, Any]:
    width, height, pixels = read_bmp_rgb(path)
    count = max(len(pixels), 1)
    sums = [0.0, 0.0, 0.0]
    active = [0, 0, 0]
    maxes = [0, 0, 0]
    for r, g, b in pixels:
        for index, value in enumerate((r, g, b)):
            sums[index] += value
            maxes[index] = max(maxes[index], value)
            if value > 12:
                active[index] += 1
    return {
        "path": str(path),
        "width": width,
        "height": height,
        "pixel_count": count,
        "mean_rgb": [value / count / 255.0 for value in sums],
        "max_rgb": [value / 255.0 for value in maxes],
        "active_rgb_ratio": [value / count for value in active],
    }


def _measure_rgb_pair(path_a: Path, path_b: Path) -> dict[str, Any]:
    width_a, height_a, pixels_a = read_bmp_rgb(path_a)
    width_b, height_b, pixels_b = read_bmp_rgb(path_b)
    if width_a != width_b or height_a != height_b or len(pixels_a) != len(pixels_b):
        raise ValueError(f"dimension mismatch: {path_a} vs {path_b}")

    count = max(len(pixels_a), 1)
    sums = [0.0, 0.0, 0.0]
    active = [0, 0, 0]
    maxes = [0, 0, 0]
    for pixel_a, pixel_b in zip(pixels_a, pixels_b):
        for index in range(3):
            delta = abs(pixel_a[index] - pixel_b[index])
            sums[index] += delta
            maxes[index] = max(maxes[index], delta)
            if delta > 12:
                active[index] += 1
    return {
        "a": str(path_a),
        "b": str(path_b),
        "mean_abs_rgb_delta": [value / count / 255.0 for value in sums],
        "max_abs_rgb_delta": [value / 255.0 for value in maxes],
        "active_rgb_delta_ratio": [value / count for value in active],
    }


def _summarize_rgb_pairs(pairs: list[dict[str, Any]]) -> dict[str, Any]:
    if not pairs:
        return {
            "pair_count": 0,
            "mean_abs_rgb_delta": [0.0, 0.0, 0.0],
            "max_abs_rgb_delta": [0.0, 0.0, 0.0],
            "mean_active_rgb_delta_ratio": [0.0, 0.0, 0.0],
            "max_active_rgb_delta_ratio": [0.0, 0.0, 0.0],
        }
    return {
        "pair_count": len(pairs),
        "mean_abs_rgb_delta": [
            sum(pair["mean_abs_rgb_delta"][index] for pair in pairs) / len(pairs)
            for index in range(3)
        ],
        "max_abs_rgb_delta": [
            max(pair["max_abs_rgb_delta"][index] for pair in pairs)
            for index in range(3)
        ],
        "mean_active_rgb_delta_ratio": [
            sum(pair["active_rgb_delta_ratio"][index] for pair in pairs) / len(pairs)
            for index in range(3)
        ],
        "max_active_rgb_delta_ratio": [
            max(pair["active_rgb_delta_ratio"][index] for pair in pairs)
            for index in range(3)
        ],
    }


def _summarize_diagnostic_view(result: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(result, dict):
        return {"present": False, "reason": "missing_manifest_row"}
    sequence = _capture_sequence(result)
    if not sequence:
        return {"present": False, "reason": "missing_capture_sequence"}
    missing = [str(path) for path in sequence if not path.exists()]
    if missing:
        return {"present": False, "reason": "missing_capture", "missing": missing}
    first = _summarize_rgb_frame(_first_capture(result) or sequence[0])
    pairs = [_measure_rgb_pair(path_a, path_b) for path_a, path_b in zip(sequence, sequence[1:])]
    return {
        "present": True,
        "view": str(result.get("view", "")),
        "debug_view": result.get("debug_view"),
        "channel_names": DIAGNOSTIC_VIEWS.get(str(result.get("view", "")), ("r", "g", "b")),
        "capture_count": len(sequence),
        "first_frame": first,
        "motion": _summarize_rgb_pairs(pairs),
        "pairs": pairs,
    }


def _summarize_pairs(pairs: list[dict[str, Any]]) -> dict[str, Any]:
    if not pairs:
        return {
            "pair_count": 0,
            "mean_source_delta": 0.0,
            "mean_confidence_delta": 0.0,
            "mean_override_delta": 0.0,
            "max_source_delta": 0.0,
            "mean_source_switch_ratio": 0.0,
            "max_source_switch_ratio": 0.0,
            "mean_active_source_switch_ratio": 0.0,
            "max_active_source_switch_ratio": 0.0,
        }
    return {
        "pair_count": len(pairs),
        "mean_source_delta": sum(row["mean_source_delta"] for row in pairs) / len(pairs),
        "mean_confidence_delta": sum(row["mean_confidence_delta"] for row in pairs) / len(pairs),
        "mean_override_delta": sum(row["mean_override_delta"] for row in pairs) / len(pairs),
        "max_source_delta": max(row["max_source_delta"] for row in pairs),
        "mean_source_switch_ratio": sum(row["source_switch_ratio"] for row in pairs) / len(pairs),
        "max_source_switch_ratio": max(row["source_switch_ratio"] for row in pairs),
        "mean_active_source_switch_ratio": (
            sum(row["active_source_switch_ratio"] for row in pairs) / len(pairs)
        ),
        "max_active_source_switch_ratio": max(row["active_source_switch_ratio"] for row in pairs),
    }


def _diagnose_row(summary: dict[str, Any], diagnostics: dict[str, dict[str, Any]]) -> list[str]:
    diagnoses: list[str] = []
    source_churn = (
        summary["max_source_switch_ratio"] > 0.055
        or summary["max_active_source_switch_ratio"] > 0.18
    )
    if not source_churn:
        return diagnoses

    ssr = diagnostics.get("reflection_ssr_source_signal", {})
    rejected = diagnostics.get("reflection_rejected_source_mask", {})
    temporal = diagnostics.get("reflection_temporal_delta", {})
    suppression = diagnostics.get("reflection_source_suppression", {})
    validity = diagnostics.get("reflection_history_v3_validity", {})
    history_rejection = diagnostics.get("reflection_history_v3_rejection", {})

    if ssr.get("present"):
        ssr_first = ssr["first_frame"]["mean_rgb"]
        ssr_motion = ssr["motion"]["mean_abs_rgb_delta"]
        ssr_active_motion = ssr["motion"]["max_active_rgb_delta_ratio"]
        if max(ssr_motion[:2]) > 0.012 or max(ssr_active_motion[:2]) > 0.08:
            diagnoses.append("ssr_signal_changes_under_motion")
        if max(ssr_first[:2]) < 0.08:
            diagnoses.append("ssr_signal_sparse_or_low_confidence")
    else:
        diagnoses.append("ssr_signal_view_missing")

    if rejected.get("present"):
        rejected_first = rejected["first_frame"]["mean_rgb"]
        rejected_motion = rejected["motion"]["mean_abs_rgb_delta"]
        if rejected_first[1] > 0.25:
            diagnoses.append("ssr_rejection_mask_high")
        if rejected_motion[1] > 0.010:
            diagnoses.append("ssr_rejection_changes_under_motion")

    if temporal.get("present"):
        temporal_first = temporal["first_frame"]["mean_rgb"]
        temporal_motion = temporal["motion"]["mean_abs_rgb_delta"]
        if max(temporal_first[1:3]) > 0.04:
            diagnoses.append("forced_or_history_debt_present")
        if max(temporal_motion[:3]) > 0.012:
            diagnoses.append("temporal_delta_tracks_source_churn")

    if suppression.get("present"):
        suppression_first = suppression["first_frame"]["mean_rgb"]
        suppression_motion = suppression["motion"]["mean_abs_rgb_delta"]
        if suppression_first[0] > 0.01 or suppression_motion[0] > 0.006:
            diagnoses.append("history_suppression_contributes")
        if suppression_first[1] > 0.01 or suppression_motion[1] > 0.006:
            diagnoses.append("material_suppression_contributes")

    if validity.get("present"):
        validity_motion = validity["motion"]["mean_abs_rgb_delta"]
        if max(validity_motion[:3]) > 0.012:
            diagnoses.append("history_validity_changes_under_motion")

    if history_rejection.get("present"):
        rejection_first = history_rejection["first_frame"]["mean_rgb"]
        rejection_motion = history_rejection["motion"]["mean_abs_rgb_delta"]
        if max(rejection_first[:3]) > 0.025:
            diagnoses.append("history_rejection_present")
        if max(rejection_motion[:3]) > 0.010:
            diagnoses.append("history_rejection_changes_under_motion")

    if not diagnoses:
        diagnoses.append("source_churn_unattributed_by_available_debug_views")
    return diagnoses


def build_report(
    manifest_path: Path,
    *,
    min_sequence_count: int,
    switch_threshold: float,
    max_switch_ratio_warning: float,
    max_active_switch_ratio_warning: float,
) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []
    results_by_family_view: dict[str, dict[str, dict[str, Any]]] = {}

    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        family = str(result.get("family", ""))
        view = str(result.get("view", ""))
        results_by_family_view.setdefault(family, {})[view] = result

    for result in manifest.get("results", []):
        if not isinstance(result, dict) or str(result.get("view", "")) != SOURCE_ID_VIEW:
            continue
        family = str(result.get("family", ""))
        sequence = [Path(path) for path in result.get("capture_sequence", []) if path]
        if len(sequence) < min_sequence_count:
            failures.append(
                f"{family}/{SOURCE_ID_VIEW}: expected at least {min_sequence_count} "
                f"captures, got {len(sequence)}"
            )
            continue
        missing = [str(path) for path in sequence if not path.exists()]
        if missing:
            failures.append(f"{family}/{SOURCE_ID_VIEW}: missing captures: {', '.join(missing)}")
            continue

        try:
            first_frame = _summarize_source_frame(_first_capture(result) or sequence[0])
            pairs = [
                _measure_source_pair(path_a, path_b, switch_threshold)
                for path_a, path_b in zip(sequence, sequence[1:])
            ]
        except Exception as exc:  # noqa: BLE001 - artifact path should be reported.
            failures.append(f"{family}/{SOURCE_ID_VIEW}: {exc}")
            continue

        summary = _summarize_pairs(pairs)
        row_warnings: list[str] = []
        if summary["max_source_switch_ratio"] > max_switch_ratio_warning:
            row_warnings.append("source_switch_ratio_above_warning")
        if summary["max_active_source_switch_ratio"] > max_active_switch_ratio_warning:
            row_warnings.append("active_source_switch_ratio_above_warning")
        if first_frame["active_source_ratio"] <= 0.001:
            row_warnings.append("source_id_view_has_no_active_reflection_source")
        diagnostics: dict[str, dict[str, Any]] = {}
        try:
            diagnostics = {
                view: _summarize_diagnostic_view(results_by_family_view.get(family, {}).get(view))
                for view in DIAGNOSTIC_VIEWS
            }
        except Exception as exc:  # noqa: BLE001 - optional diagnostic path.
            failures.append(f"{family}: failed diagnostic-channel analysis: {exc}")
            continue
        diagnosis = _diagnose_row(summary, diagnostics)
        warnings.extend(f"{family}: {warning}" for warning in row_warnings)

        rows.append(
            {
                "family": family,
                "scene": str(result.get("scene", "")),
                "camera_bookmark": str(result.get("camera_bookmark", "")),
                "stability_motion_mode": str(
                    result.get("stability_motion_mode", manifest.get("stability_motion_mode", ""))
                ),
                "debug_view": result.get("debug_view"),
                "capture_count": len(sequence),
                "first_frame": first_frame,
                "summary": summary,
                "diagnosis": diagnosis,
                "diagnostics": diagnostics,
                "pairs": pairs,
                "warnings": row_warnings,
            }
        )

    if not rows and not failures:
        failures.append("manifest contained no reflection_source_id capture rows")

    return {
        "schema": "cortex.reflection_v3_source_resolver.v1",
        "manifest": str(manifest_path),
        "stability_motion_mode": manifest.get("stability_motion_mode", ""),
        "capture_sequence_count": manifest.get("capture_sequence_count", 0),
        "min_sequence_count": min_sequence_count,
        "source_switch_threshold": switch_threshold,
        "max_switch_ratio_warning": max_switch_ratio_warning,
        "max_active_switch_ratio_warning": max_active_switch_ratio_warning,
        "family_count": len(rows),
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], output: Path) -> None:
    lines = [
        "# ReflectionV3 Source Resolver",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- motion mode: `{report['stability_motion_mode']}`",
        f"- sequence count: {report['capture_sequence_count']}",
        f"- families: {report['family_count']}",
        f"- warnings: {len(report['warnings'])}",
        f"- failures: {len(report['failures'])}",
        "",
        "| Family | Motion | Dominant Source | Active Source | Mean Source Delta | Max Switch | Max Active Switch | Mean Confidence Delta | Diagnosis | Warnings |",
        "|---|---|---|---:|---:|---:|---:|---:|---|---|",
    ]
    for row in report["rows"]:
        first = row["first_frame"]
        summary = row["summary"]
        lines.append(
            "| {family} | {motion} | {dominant} | {active:.5f} | {source_delta:.6f} | "
            "{switch:.6f} | {active_switch:.6f} | {confidence_delta:.6f} | {diagnosis} | {warnings} |".format(
                family=row["family"],
                motion=row["stability_motion_mode"],
                dominant=first["dominant_source"],
                active=first["active_source_ratio"],
                source_delta=summary["mean_source_delta"],
                switch=summary["max_source_switch_ratio"],
                active_switch=summary["max_active_source_switch_ratio"],
                confidence_delta=summary["mean_confidence_delta"],
                diagnosis=", ".join(row["diagnosis"]),
                warnings=", ".join(row["warnings"]),
            )
        )
    if report["rows"]:
        lines.extend(["", "## Diagnostic Channels", ""])
        for row in report["rows"]:
            lines.append(f"### {row['family']}")
            lines.append("")
            lines.append("| View | Mean RGB | Motion RGB Delta | Active Motion RGB |")
            lines.append("|---|---:|---:|---:|")
            for view, diagnostic in row["diagnostics"].items():
                if not diagnostic.get("present"):
                    lines.append(f"| {view} | missing | missing | missing |")
                    continue
                first = diagnostic["first_frame"]
                motion = diagnostic["motion"]
                lines.append(
                    "| {view} | {mean} | {delta} | {active} |".format(
                        view=view,
                        mean=", ".join(f"{value:.5f}" for value in first["mean_rgb"]),
                        delta=", ".join(f"{value:.5f}" for value in motion["mean_abs_rgb_delta"]),
                        active=", ".join(
                            f"{value:.5f}" for value in motion["mean_active_rgb_delta_ratio"]
                        ),
                    )
                )
            lines.append("")
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
    parser.add_argument("--min-sequence-count", type=int, default=2)
    parser.add_argument("--source-switch-threshold", type=float, default=0.10)
    parser.add_argument("--max-switch-ratio-warning", type=float, default=0.055)
    parser.add_argument("--max-active-switch-ratio-warning", type=float, default=0.18)
    parser.add_argument("--fail-on-warning", action="store_true")
    args = parser.parse_args()

    report = build_report(
        args.manifest,
        min_sequence_count=args.min_sequence_count,
        switch_threshold=args.source_switch_threshold,
        max_switch_ratio_warning=args.max_switch_ratio_warning,
        max_active_switch_ratio_warning=args.max_active_switch_ratio_warning,
    )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, args.output_md)

    for warning in report["warnings"]:
        print(f"WARN: {warning}")
    for failure in report["failures"]:
        print(f"ERROR: {failure}", file=sys.stderr)

    if report["failures"]:
        return 1
    if args.fail_on_warning and report["warnings"]:
        return 1

    print(
        "PASS: ReflectionV3 source resolver analyzed "
        f"{report['family_count']} families with {len(report['warnings'])} warnings"
    )
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
