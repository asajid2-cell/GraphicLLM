#!/usr/bin/env python3
"""Measure frame-to-frame stability for Full Scene Shader V2 packet sequences."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_debug_view_metrics import read_bmp_rgb


BEAUTY_VIEW = "beauty"
CANDIDATE_VIEW = "reflection_resolver_candidate"
DELTA_VIEW = "reflection_resolver_candidate_delta"


def _luma(pixel: tuple[int, int, int]) -> float:
    r, g, b = pixel
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def measure_pair(path_a: Path, path_b: Path, delta_threshold: float) -> dict[str, Any]:
    width_a, height_a, pixels_a = read_bmp_rgb(path_a)
    width_b, height_b, pixels_b = read_bmp_rgb(path_b)
    if width_a != width_b or height_a != height_b or len(pixels_a) != len(pixels_b):
        raise ValueError(f"dimension mismatch: {path_a} vs {path_b}")

    count = max(len(pixels_a), 1)
    sum_rgb = [0.0, 0.0, 0.0]
    sum_luma = 0.0
    max_luma = 0.0
    active = 0
    threshold_255 = delta_threshold * 255.0
    for pa, pb in zip(pixels_a, pixels_b):
        dr = abs(pa[0] - pb[0])
        dg = abs(pa[1] - pb[1])
        db = abs(pa[2] - pb[2])
        dl = abs(_luma(pa) - _luma(pb))
        sum_rgb[0] += dr
        sum_rgb[1] += dg
        sum_rgb[2] += db
        sum_luma += dl
        max_luma = max(max_luma, dl)
        if dl > threshold_255:
            active += 1

    return {
        "a": str(path_a),
        "b": str(path_b),
        "width": width_a,
        "height": height_a,
        "pixel_count": count,
        "mean_abs_rgb_delta": [v / count / 255.0 for v in sum_rgb],
        "mean_abs_luma_delta": sum_luma / count / 255.0,
        "max_abs_luma_delta": max_luma / 255.0,
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
        "mean_abs_luma_delta": sum(p["mean_abs_luma_delta"] for p in pairs) / len(pairs),
        "max_pair_mean_abs_luma_delta": max(p["mean_abs_luma_delta"] for p in pairs),
        "max_abs_luma_delta": max(p["max_abs_luma_delta"] for p in pairs),
        "mean_active_delta_ratio": sum(p["active_delta_ratio"] for p in pairs) / len(pairs),
    }


def build_report(manifest_path: Path, delta_threshold: float, ratio_warning: float) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []

    by_family: dict[str, dict[str, dict[str, Any]]] = {}
    for result in manifest.get("results", []):
        family = str(result.get("family", ""))
        view = str(result.get("view", ""))
        sequence = [Path(p) for p in result.get("capture_sequence", []) if p]
        if len(sequence) < 2:
            continue

        pairs: list[dict[str, Any]] = []
        for a, b in zip(sequence, sequence[1:]):
            try:
                pairs.append(measure_pair(a, b, delta_threshold))
            except Exception as exc:  # noqa: BLE001 - keep packet artifact path in report.
                failures.append(f"{family}/{view}: {exc}")
                pairs = []
                break

        summary = summarize_pairs(pairs)
        row = {
            "family": family,
            "view": view,
            "debug_view": result.get("debug_view"),
            "scene": result.get("scene", ""),
            "stability_motion_mode": result.get("stability_motion_mode", manifest.get("stability_motion_mode", "")),
            "summary": summary,
            "pairs": pairs,
        }
        rows.append(row)
        by_family.setdefault(family, {})[view] = row

    family_rows: list[dict[str, Any]] = []
    for family in sorted(by_family):
        views = by_family[family]
        beauty = views.get(BEAUTY_VIEW)
        candidate = views.get(CANDIDATE_VIEW)
        delta = views.get(DELTA_VIEW)
        beauty_motion = beauty["summary"]["mean_abs_luma_delta"] if beauty else 0.0
        candidate_motion = candidate["summary"]["mean_abs_luma_delta"] if candidate else 0.0
        ratio = candidate_motion / max(beauty_motion, 1e-6)
        delta_motion = delta["summary"]["mean_abs_luma_delta"] if delta else 0.0
        status = "ok"
        if beauty and candidate and ratio > ratio_warning and candidate_motion > 0.0025:
            status = "candidate_motion_warning"
            warnings.append(
                f"{family}: candidate motion delta {candidate_motion:.6f} is {ratio:.2f}x beauty"
            )
        family_rows.append(
            {
                "family": family,
                "status": status,
                "beauty_mean_abs_luma_delta": beauty_motion,
                "candidate_mean_abs_luma_delta": candidate_motion,
                "candidate_over_beauty_ratio": ratio,
                "delta_view_mean_abs_luma_delta": delta_motion,
            }
        )

    return {
        "schema": "cortex.full_scene_shader_pipeline_v2.sequence_stability.v1",
        "manifest": str(manifest_path),
        "delta_threshold": delta_threshold,
        "candidate_over_beauty_warning_ratio": ratio_warning,
        "view_row_count": len(rows),
        "family_count": len(family_rows),
        "warnings": warnings,
        "failures": failures,
        "families": family_rows,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Full Scene Shader V2 Sequence Stability",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- families: {report['family_count']}",
        f"- view rows: {report['view_row_count']}",
        f"- warnings: {len(report['warnings'])}",
        f"- failures: {len(report['failures'])}",
        "",
        "| Family | Status | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty | Delta View Luma Delta |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for row in report["families"]:
        lines.append(
            "| {family} | {status} | {beauty:.8f} | {candidate:.8f} | {ratio:.3f} | {delta:.8f} |".format(
                family=row["family"],
                status=row["status"],
                beauty=row["beauty_mean_abs_luma_delta"],
                candidate=row["candidate_mean_abs_luma_delta"],
                ratio=row["candidate_over_beauty_ratio"],
                delta=row["delta_view_mean_abs_luma_delta"],
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
    parser.add_argument("--candidate-over-beauty-warning-ratio", type=float, default=1.25)
    parser.add_argument("--fail-on-warning", action="store_true")
    args = parser.parse_args()

    report = build_report(
        args.manifest,
        args.delta_threshold,
        args.candidate_over_beauty_warning_ratio,
    )
    output_json = args.output_json or args.manifest.with_name("sequence_stability.json")
    output_md = args.output_md or args.manifest.with_name("sequence_stability.md")
    output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
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
        "PASS: sequence stability measured "
        f"{report['view_row_count']} view sequences across {report['family_count']} families"
    )
    print(f"json={output_json}")
    print(f"markdown={output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
