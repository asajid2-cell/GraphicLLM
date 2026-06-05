#!/usr/bin/env python3
"""Audit whether a V2 reflection candidate packet has meaningful signal."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


SOURCE_VIEW = "reflection_source_weights"
CANDIDATE_VIEW = "reflection_resolver_candidate"
DELTA_VIEW = "reflection_resolver_candidate_delta"


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def mean_rgb_luma(row: dict[str, Any]) -> float:
    r, g, b = row["metrics"]["mean_rgb"]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def build_report(metrics_path: Path, source_threshold: float, delta_threshold: float) -> dict[str, Any]:
    metrics = load_json(metrics_path)
    rows = metrics.get("rows", [])
    by_family: dict[str, dict[str, dict[str, Any]]] = {}
    for row in rows:
        by_family.setdefault(row.get("family", ""), {})[row.get("view", "")] = row

    family_rows: list[dict[str, Any]] = []
    warnings: list[str] = []
    failures: list[str] = []
    for family in sorted(by_family):
        views = by_family[family]
        present = [view for view in (SOURCE_VIEW, CANDIDATE_VIEW, DELTA_VIEW) if view in views]
        if not present:
            continue
        missing = [view for view in (SOURCE_VIEW, CANDIDATE_VIEW, DELTA_VIEW) if view not in views]
        if missing:
            failures.append(f"{family}: missing views: {', '.join(missing)}")
            continue

        source_row = views[SOURCE_VIEW]
        candidate_row = views[CANDIDATE_VIEW]
        delta_row = views[DELTA_VIEW]
        source_luma = mean_rgb_luma(source_row)
        delta_luma = mean_rgb_luma(delta_row)
        source_nonblack = float(source_row["metrics"]["nonblack_ratio"])
        delta_nonblack = float(delta_row["metrics"]["nonblack_ratio"])
        candidate_nonblack = float(candidate_row["metrics"]["nonblack_ratio"])
        has_source_signal = source_luma >= source_threshold or source_nonblack >= source_threshold
        has_candidate_delta = delta_luma >= delta_threshold or delta_nonblack >= delta_threshold
        status = "meaningful_delta" if has_candidate_delta else "wired_no_delta"
        if not has_source_signal:
            status = "no_reflection_source_signal"
            warnings.append(f"{family}: reflection source weights are effectively zero")
        elif not has_candidate_delta:
            warnings.append(f"{family}: candidate is wired but has near-zero delta")

        family_rows.append(
            {
                "family": family,
                "status": status,
                "source_mean_luma": source_luma,
                "source_nonblack_ratio": source_nonblack,
                "candidate_nonblack_ratio": candidate_nonblack,
                "delta_mean_luma": delta_luma,
                "delta_nonblack_ratio": delta_nonblack,
                "has_source_signal": has_source_signal,
                "has_candidate_delta": has_candidate_delta,
            }
        )

    meaningful_families = [row["family"] for row in family_rows if row["has_candidate_delta"]]
    source_signal_families = [row["family"] for row in family_rows if row["has_source_signal"]]
    return {
        "schema": "cortex.full_scene_shader_pipeline_v2.reflection_candidate_signal.v1",
        "metrics": str(metrics_path),
        "source_signal_threshold": source_threshold,
        "candidate_delta_threshold": delta_threshold,
        "family_count": len(family_rows),
        "source_signal_family_count": len(source_signal_families),
        "candidate_delta_family_count": len(meaningful_families),
        "source_signal_families": source_signal_families,
        "candidate_delta_families": meaningful_families,
        "warnings": warnings,
        "failures": failures,
        "rows": family_rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Full Scene Shader V2 Reflection Candidate Signal",
        "",
        f"- metrics: `{report['metrics']}`",
        f"- families: {report['family_count']}",
        f"- source-signal families: {report['source_signal_family_count']}",
        f"- candidate-delta families: {report['candidate_delta_family_count']}",
        f"- warnings: {len(report['warnings'])}",
        f"- failures: {len(report['failures'])}",
        "",
        "| Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |",
        "|---|---|---:|---:|---:|---:|",
    ]
    for row in report["rows"]:
        lines.append(
            "| {family} | {status} | {source_luma:.8f} | {source_nonblack:.8f} | {delta_luma:.8f} | {delta_nonblack:.8f} |".format(
                family=row["family"],
                status=row["status"],
                source_luma=row["source_mean_luma"],
                source_nonblack=row["source_nonblack_ratio"],
                delta_luma=row["delta_mean_luma"],
                delta_nonblack=row["delta_nonblack_ratio"],
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
    parser.add_argument("--metrics", required=True, type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-md", type=Path)
    parser.add_argument("--source-threshold", type=float, default=0.001)
    parser.add_argument("--delta-threshold", type=float, default=0.0005)
    parser.add_argument("--require-candidate-delta", action="store_true")
    args = parser.parse_args()

    report = build_report(args.metrics, args.source_threshold, args.delta_threshold)
    output_json = args.output_json or args.metrics.with_name("reflection_candidate_signal.json")
    output_md = args.output_md or args.metrics.with_name("reflection_candidate_signal.md")
    output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, output_md)

    for warning in report["warnings"]:
        print(f"WARN: {warning}")
    for failure in report["failures"]:
        print(f"ERROR: {failure}", file=sys.stderr)

    if report["failures"]:
        return 1
    if args.require_candidate_delta and report["candidate_delta_family_count"] == 0:
        print("ERROR: no family produced a meaningful reflection candidate delta", file=sys.stderr)
        return 1

    print(
        "PASS: reflection candidate signal audit measured "
        f"{report['family_count']} families; candidate_delta_families={report['candidate_delta_family_count']}"
    )
    print(f"json={output_json}")
    print(f"markdown={output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
