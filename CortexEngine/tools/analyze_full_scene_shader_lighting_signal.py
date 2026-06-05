#!/usr/bin/env python3
"""Audit whether a V2 packet captured meaningful direct-light signal."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


SHADOWED_VIEW = "direct_light"
UNSHADOWED_VIEW = "direct_light_unshadowed"
SHADOW_LOSS_VIEW = "direct_light_shadow_loss"
LIGHTING_VIEWS = (SHADOWED_VIEW, UNSHADOWED_VIEW, SHADOW_LOSS_VIEW)


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def mean_rgb_luma(row: dict[str, Any]) -> float:
    r, g, b = row["metrics"]["mean_rgb"]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def view_signal(row: dict[str, Any], threshold: float) -> bool:
    metrics = row["metrics"]
    return mean_rgb_luma(row) >= threshold or float(metrics["nonblack_ratio"]) >= threshold


def build_report(metrics_path: Path, direct_threshold: float, shadow_loss_threshold: float) -> dict[str, Any]:
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
        present = [view for view in LIGHTING_VIEWS if view in views]
        if not present:
            continue
        missing = [view for view in LIGHTING_VIEWS if view not in views]
        if missing:
            failures.append(f"{family}: incomplete direct-light view set; missing: {', '.join(missing)}")
            continue

        shadowed = views[SHADOWED_VIEW]
        unshadowed = views[UNSHADOWED_VIEW]
        shadow_loss = views[SHADOW_LOSS_VIEW]
        shadowed_luma = mean_rgb_luma(shadowed)
        unshadowed_luma = mean_rgb_luma(unshadowed)
        shadow_loss_luma = mean_rgb_luma(shadow_loss)
        shadowed_nonblack = float(shadowed["metrics"]["nonblack_ratio"])
        unshadowed_nonblack = float(unshadowed["metrics"]["nonblack_ratio"])
        shadow_loss_nonblack = float(shadow_loss["metrics"]["nonblack_ratio"])
        has_direct_signal = view_signal(shadowed, direct_threshold) or view_signal(unshadowed, direct_threshold)
        has_shadow_loss_signal = view_signal(shadow_loss, shadow_loss_threshold)
        status = "direct_light_shadow_contract"
        if not has_direct_signal:
            status = "no_direct_light_signal"
            warnings.append(f"{family}: direct-light debug views are effectively zero")
        elif not has_shadow_loss_signal:
            status = "direct_light_no_shadow_loss"

        family_rows.append(
            {
                "family": family,
                "status": status,
                "shadowed_mean_luma": shadowed_luma,
                "shadowed_nonblack_ratio": shadowed_nonblack,
                "unshadowed_mean_luma": unshadowed_luma,
                "unshadowed_nonblack_ratio": unshadowed_nonblack,
                "shadow_loss_mean_luma": shadow_loss_luma,
                "shadow_loss_nonblack_ratio": shadow_loss_nonblack,
                "has_direct_signal": has_direct_signal,
                "has_shadow_loss_signal": has_shadow_loss_signal,
            }
        )

    direct_signal_families = [row["family"] for row in family_rows if row["has_direct_signal"]]
    shadow_loss_families = [row["family"] for row in family_rows if row["has_shadow_loss_signal"]]
    return {
        "schema": "cortex.full_scene_shader_pipeline_v2.lighting_signal.v1",
        "metrics": str(metrics_path),
        "direct_signal_threshold": direct_threshold,
        "shadow_loss_threshold": shadow_loss_threshold,
        "family_count": len(family_rows),
        "direct_signal_family_count": len(direct_signal_families),
        "shadow_loss_family_count": len(shadow_loss_families),
        "direct_signal_families": direct_signal_families,
        "shadow_loss_families": shadow_loss_families,
        "warnings": warnings,
        "failures": failures,
        "rows": family_rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Full Scene Shader V2 Lighting Signal",
        "",
        f"- metrics: `{report['metrics']}`",
        f"- families: {report['family_count']}",
        f"- direct-signal families: {report['direct_signal_family_count']}",
        f"- shadow-loss families: {report['shadow_loss_family_count']}",
        f"- warnings: {len(report['warnings'])}",
        f"- failures: {len(report['failures'])}",
        "",
        "| Family | Status | Shadowed Luma | Shadowed Nonblack | Unshadowed Luma | Shadow Loss Luma | Shadow Loss Nonblack |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for row in report["rows"]:
        lines.append(
            "| {family} | {status} | {shadowed_luma:.8f} | {shadowed_nonblack:.8f} | {unshadowed_luma:.8f} | {shadow_loss_luma:.8f} | {shadow_loss_nonblack:.8f} |".format(
                family=row["family"],
                status=row["status"],
                shadowed_luma=row["shadowed_mean_luma"],
                shadowed_nonblack=row["shadowed_nonblack_ratio"],
                unshadowed_luma=row["unshadowed_mean_luma"],
                shadow_loss_luma=row["shadow_loss_mean_luma"],
                shadow_loss_nonblack=row["shadow_loss_nonblack_ratio"],
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
    parser.add_argument("--direct-threshold", type=float, default=0.001)
    parser.add_argument("--shadow-loss-threshold", type=float, default=0.00025)
    parser.add_argument("--require-direct-signal", action="store_true")
    args = parser.parse_args()

    report = build_report(args.metrics, args.direct_threshold, args.shadow_loss_threshold)
    output_json = args.output_json or args.metrics.with_name("lighting_signal.json")
    output_md = args.output_md or args.metrics.with_name("lighting_signal.md")
    output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, output_md)

    for warning in report["warnings"]:
        print(f"WARN: {warning}")
    for failure in report["failures"]:
        print(f"ERROR: {failure}", file=sys.stderr)

    if report["failures"]:
        return 1
    if args.require_direct_signal and report["family_count"] > 0 and report["direct_signal_family_count"] == 0:
        print("ERROR: no family produced meaningful direct-light signal", file=sys.stderr)
        return 1

    print(
        "PASS: lighting signal audit measured "
        f"{report['family_count']} families; direct_signal_families={report['direct_signal_family_count']}"
    )
    print(f"json={output_json}")
    print(f"markdown={output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
