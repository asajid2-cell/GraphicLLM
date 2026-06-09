#!/usr/bin/env python3
"""Build a promotion-grade ReflectionV3 source/debug matrix.

Tool marker: build_reflection_v3_promotion_matrix.py.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_v3_lighting_motion import build_report as build_motion_report
from analyze_reflection_v3_source_resolver import (
    DIAGNOSTIC_VIEWS,
    SOURCE_ID_VIEW,
    build_report as build_source_resolver_report,
)


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_REFLECTION_DEBUG_VIEWS = [
    "reflection_radiance",
    "reflection_confidence",
    SOURCE_ID_VIEW,
    *DIAGNOSTIC_VIEWS.keys(),
]


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def normalize_packet_root(raw: str) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path
    for candidate in (Path.cwd() / path, ROOT / path):
        if candidate.exists():
            return candidate.resolve()
    return (ROOT / path).resolve()


def manifest_family_views(manifest: dict[str, Any]) -> dict[str, set[str]]:
    by_family: dict[str, set[str]] = {}
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        family = str(row.get("family", ""))
        view = str(row.get("view", ""))
        if family and view:
            by_family.setdefault(family, set()).add(view)
    return by_family


def check_debug_visibility(family_views: dict[str, set[str]]) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    for family in sorted(family_views):
        views = family_views[family]
        missing = [view for view in REQUIRED_REFLECTION_DEBUG_VIEWS if view not in views]
        ready = not missing
        if missing:
            failures.append(f"{family}: missing reflection debug views: {', '.join(missing)}")
        rows.append(
            {
                "family": family,
                "ready": ready,
                "view_count": len(views),
                "required_view_count": len(REQUIRED_REFLECTION_DEBUG_VIEWS),
                "missing_views": missing,
            }
        )
    return rows, failures


def row_for_packet(
    packet_root: Path,
    *,
    min_sequence_count: int,
    motion_delta_threshold: float,
    motion_ratio_warning: float,
    source_switch_threshold: float,
    max_switch_ratio_warning: float,
    max_active_switch_ratio_warning: float,
) -> dict[str, Any]:
    manifest_path = packet_root / "manifest.json"
    row: dict[str, Any] = {
        "packet_root": str(packet_root),
        "manifest": str(manifest_path),
        "exists": packet_root.exists(),
        "families": [],
        "motion_mode": "unknown",
        "capture_sequence_count": 0,
        "debug_visibility_ready": False,
        "source_resolver_ready": None,
        "reflection_motion_ready": None,
        "debug_visibility_rows": [],
        "source_resolver_family_count": 0,
        "reflection_motion_family_count": 0,
        "reflection_motion_view_row_count": 0,
        "source_resolver_warnings": [],
        "reflection_motion_warnings": [],
        "failures": [],
        "warnings": [],
    }
    if not packet_root.exists():
        row["failures"].append("packet root missing")
        return row
    if not manifest_path.exists():
        row["failures"].append("manifest.json missing")
        return row

    manifest = load_json(manifest_path)
    family_views = manifest_family_views(manifest)
    row["families"] = sorted(family_views)
    row["motion_mode"] = str(manifest.get("stability_motion_mode", "static"))
    row["capture_sequence_count"] = int(manifest.get("capture_sequence_count", 0) or 0)
    debug_rows, debug_failures = check_debug_visibility(family_views)
    row["debug_visibility_rows"] = debug_rows
    row["debug_visibility_ready"] = not debug_failures
    for failure in debug_failures:
        row["failures"].append(f"debug_visibility: {failure}")

    if row["capture_sequence_count"] >= min_sequence_count:
        source = build_source_resolver_report(
            manifest_path,
            min_sequence_count=min_sequence_count,
            switch_threshold=source_switch_threshold,
            max_switch_ratio_warning=max_switch_ratio_warning,
            max_active_switch_ratio_warning=max_active_switch_ratio_warning,
        )
        row["source_resolver_family_count"] = int(source.get("family_count", 0) or 0)
        row["source_resolver_ready"] = not source.get("failures")
        row["source_resolver_warnings"] = list(source.get("warnings", []))
        for failure in source.get("failures", []):
            row["failures"].append(f"source_resolver: {failure}")
        for warning in source.get("warnings", []):
            row["warnings"].append(f"source_resolver: {warning}")

        motion = build_motion_report(
            manifest_path,
            delta_threshold=motion_delta_threshold,
            min_sequence_count=min_sequence_count,
            ratio_warning=motion_ratio_warning,
            focus="reflection",
        )
        row["reflection_motion_family_count"] = int(motion.get("family_count", 0) or 0)
        row["reflection_motion_view_row_count"] = int(motion.get("view_row_count", 0) or 0)
        row["reflection_motion_ready"] = not motion.get("failures")
        row["reflection_motion_warnings"] = list(motion.get("warnings", []))
        for failure in motion.get("failures", []):
            row["failures"].append(f"reflection_motion: {failure}")
        for warning in motion.get("warnings", []):
            row["warnings"].append(f"reflection_motion: {warning}")

    return row


def build_matrix(
    packet_roots: list[Path],
    *,
    required_families: list[str],
    required_motion_modes: list[str],
    min_sequence_count: int,
    motion_delta_threshold: float,
    motion_ratio_warning: float,
    source_switch_threshold: float,
    max_switch_ratio_warning: float,
    max_active_switch_ratio_warning: float,
) -> dict[str, Any]:
    rows = [
        row_for_packet(
            root,
            min_sequence_count=min_sequence_count,
            motion_delta_threshold=motion_delta_threshold,
            motion_ratio_warning=motion_ratio_warning,
            source_switch_threshold=source_switch_threshold,
            max_switch_ratio_warning=max_switch_ratio_warning,
            max_active_switch_ratio_warning=max_active_switch_ratio_warning,
        )
        for root in packet_roots
    ]

    debug_ready_rows = [
        row for row in rows if row.get("debug_visibility_ready") is True and not row.get("failures")
    ]
    source_ready_rows = [
        row
        for row in rows
        if row.get("source_resolver_ready") is True
        and row.get("reflection_motion_ready") is True
        and not row.get("failures")
    ]

    observed_families = sorted(
        {
            family
            for row in debug_ready_rows
            for family in row.get("families", [])
            if isinstance(family, str) and family
        }
    )
    observed_motion_modes = sorted(
        {
            str(row.get("motion_mode"))
            for row in source_ready_rows
            if row.get("motion_mode") not in (None, "", "unknown")
        }
    )
    if "static" in required_motion_modes and any(
        row.get("motion_mode") == "static" and row.get("debug_visibility_ready") is True
        for row in debug_ready_rows
    ):
        observed_motion_modes = sorted(set(observed_motion_modes) | {"static"})

    failures: list[str] = []
    warnings: list[str] = []
    for row in rows:
        for failure in row.get("failures", []):
            failures.append(f"{row.get('packet_root')}: {failure}")
        for warning in row.get("warnings", []):
            warnings.append(f"{row.get('packet_root')}: {warning}")

    missing_families = sorted(set(required_families) - set(observed_families))
    missing_motion_modes = sorted(set(required_motion_modes) - set(observed_motion_modes))
    if missing_families:
        failures.append("missing required reflection families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing required reflection motion modes: " + ", ".join(missing_motion_modes))

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.reflection_promotion_matrix.v1",
        "packet_count": len(rows),
        "debug_visibility_packet_count": len(debug_ready_rows),
        "source_resolver_packet_count": len(source_ready_rows),
        "required_families": required_families,
        "required_motion_modes": required_motion_modes,
        "observed_families": observed_families,
        "missing_families": missing_families,
        "observed_motion_modes": observed_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "reflection_promotion_ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
        "thresholds": {
            "min_sequence_count": min_sequence_count,
            "motion_delta_threshold": motion_delta_threshold,
            "motion_ratio_warning": motion_ratio_warning,
            "source_switch_threshold": source_switch_threshold,
            "max_switch_ratio_warning": max_switch_ratio_warning,
            "max_active_switch_ratio_warning": max_active_switch_ratio_warning,
        },
    }


def write_markdown(matrix: dict[str, Any], output: Path) -> None:
    lines = [
        "# ReflectionV3 Promotion Matrix",
        "",
        f"- packet count: `{matrix['packet_count']}`",
        f"- debug visibility packets: `{matrix['debug_visibility_packet_count']}`",
        f"- source resolver packets: `{matrix['source_resolver_packet_count']}`",
        f"- reflection promotion ready: `{str(matrix['reflection_promotion_ready']).lower()}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- failures: `{len(matrix['failures'])}`",
        f"- warnings: `{len(matrix['warnings'])}`",
        "",
        "| Packet | Motion | Families | Debug | Source Resolver | Motion Ready | Source Families | Motion Families | Motion Views | Failures | Warnings |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in matrix["rows"]:
        source_ready = row.get("source_resolver_ready")
        motion_ready = row.get("reflection_motion_ready")
        source_label = "n/a" if source_ready is None else str(bool(source_ready)).lower()
        motion_label = "n/a" if motion_ready is None else str(bool(motion_ready)).lower()
        lines.append(
            "| {packet} | {motion} | {families} | {debug} | {source} | {motion_ready} | {source_count} | {motion_count} | {view_count} | {failures} | {warnings} |".format(
                packet=row["packet_root"],
                motion=row["motion_mode"],
                families=", ".join(row.get("families", [])),
                debug=str(bool(row.get("debug_visibility_ready"))).lower(),
                source=source_label,
                motion_ready=motion_label,
                source_count=row.get("source_resolver_family_count", 0),
                motion_count=row.get("reflection_motion_family_count", 0),
                view_count=row.get("reflection_motion_view_row_count", 0),
                failures=len(row.get("failures", [])),
                warnings=len(row.get("warnings", [])),
            )
        )
    if matrix["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in matrix["warnings"])
    if matrix["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in matrix["failures"])
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packet-root", action="append", required=True)
    parser.add_argument(
        "--required-families",
        default="stress_rt_showcase_reflection_closeup,gallery,kitchen,office,gym,concert,red_room,stadium",
    )
    parser.add_argument("--required-motion-modes", default="static,mouse_jitter,camera_sweep,light_sweep")
    parser.add_argument("--min-sequence-count", type=int, default=2)
    parser.add_argument("--motion-delta-threshold", type=float, default=0.003)
    parser.add_argument("--motion-ratio-warning", type=float, default=2.50)
    parser.add_argument("--source-switch-threshold", type=float, default=0.16)
    parser.add_argument("--max-switch-ratio-warning", type=float, default=0.06)
    parser.add_argument("--max-active-switch-ratio-warning", type=float, default=0.20)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    matrix = build_matrix(
        [normalize_packet_root(root) for root in args.packet_root],
        required_families=split_csv(args.required_families),
        required_motion_modes=split_csv(args.required_motion_modes),
        min_sequence_count=args.min_sequence_count,
        motion_delta_threshold=args.motion_delta_threshold,
        motion_ratio_warning=args.motion_ratio_warning,
        source_switch_threshold=args.source_switch_threshold,
        max_switch_ratio_warning=args.max_switch_ratio_warning,
        max_active_switch_ratio_warning=args.max_active_switch_ratio_warning,
    )

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")
    write_markdown(matrix, args.output_md)

    print(f"reflection_matrix={args.output_json}")
    print(f"reflection_matrix_md={args.output_md}")
    if not matrix["reflection_promotion_ready"]:
        for failure in matrix["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: ReflectionV3 promotion matrix is fully covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
