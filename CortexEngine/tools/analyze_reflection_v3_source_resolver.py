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
        "| Family | Motion | Dominant Source | Active Source | Mean Source Delta | Max Switch | Max Active Switch | Mean Confidence Delta | Warnings |",
        "|---|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for row in report["rows"]:
        first = row["first_frame"]
        summary = row["summary"]
        lines.append(
            "| {family} | {motion} | {dominant} | {active:.5f} | {source_delta:.6f} | "
            "{switch:.6f} | {active_switch:.6f} | {confidence_delta:.6f} | {warnings} |".format(
                family=row["family"],
                motion=row["stability_motion_mode"],
                dominant=first["dominant_source"],
                active=first["active_source_ratio"],
                source_delta=summary["mean_source_delta"],
                switch=summary["max_source_switch_ratio"],
                active_switch=summary["max_active_source_switch_ratio"],
                confidence_delta=summary["mean_confidence_delta"],
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
