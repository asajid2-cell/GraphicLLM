#!/usr/bin/env python3
"""Build a promotion-grade LightingShadowV3 attribution/motion matrix.

Tool marker: build_lighting_v3_shadow_promotion_matrix.py.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_v3_lighting_motion import build_report as build_motion_report
from analyze_full_scene_shader_v3_shadow_attribution import build_report as build_attribution_report


ROOT = Path(__file__).resolve().parents[1]


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


def manifest_families(manifest: dict[str, Any]) -> list[str]:
    families = {
        str(row.get("family", ""))
        for row in manifest.get("results", [])
        if isinstance(row, dict) and row.get("family")
    }
    return sorted(families)


def row_for_packet(
    packet_root: Path,
    *,
    motion_delta_threshold: float,
    motion_ratio_warning: float,
    min_sequence_count: int,
) -> dict[str, Any]:
    manifest_path = packet_root / "manifest.json"
    row: dict[str, Any] = {
        "packet_root": str(packet_root),
        "manifest": str(manifest_path),
        "exists": packet_root.exists(),
        "families": [],
        "motion_mode": "unknown",
        "capture_sequence_count": 0,
        "shadow_attribution_ready": False,
        "shadow_motion_ready": None,
        "shadow_attribution_family_count": 0,
        "shadow_motion_family_count": 0,
        "shadow_motion_view_row_count": 0,
        "shadow_attribution_warnings": [],
        "shadow_motion_warnings": [],
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
    row["families"] = manifest_families(manifest)
    row["motion_mode"] = str(manifest.get("stability_motion_mode", "static"))
    row["capture_sequence_count"] = int(manifest.get("capture_sequence_count", 0) or 0)

    attribution = build_attribution_report(
        manifest_path,
        signal_threshold=0.02,
        min_enabled_ratio=0.50,
        min_source_active_ratio=0.001,
        min_loss_active_ratio=0.001,
        max_loss_without_source_ratio=0.50,
    )
    row["shadow_attribution_family_count"] = int(attribution.get("family_count", 0) or 0)
    row["shadow_attribution_ready"] = not attribution.get("failures")
    row["shadow_attribution_warnings"] = list(attribution.get("warnings", []))
    for failure in attribution.get("failures", []):
        row["failures"].append(f"shadow_attribution: {failure}")
    for warning in attribution.get("warnings", []):
        row["warnings"].append(f"shadow_attribution: {warning}")

    # Static rows prove ownership/attribution but do not need temporal motion
    # deltas. Motion rows must provide capture sequences and pass the focused
    # shadow motion analyzer.
    if row["motion_mode"] == "static":
        row["shadow_motion_ready"] = None
    else:
        motion = build_motion_report(
            manifest_path,
            delta_threshold=motion_delta_threshold,
            min_sequence_count=min_sequence_count,
            ratio_warning=motion_ratio_warning,
            focus="shadow",
        )
        row["shadow_motion_family_count"] = int(motion.get("family_count", 0) or 0)
        row["shadow_motion_view_row_count"] = int(motion.get("view_row_count", 0) or 0)
        row["shadow_motion_ready"] = not motion.get("failures")
        row["shadow_motion_warnings"] = list(motion.get("warnings", []))
        for failure in motion.get("failures", []):
            row["failures"].append(f"shadow_motion: {failure}")
        for warning in motion.get("warnings", []):
            row["warnings"].append(f"shadow_motion: {warning}")

    return row


def build_matrix(
    packet_roots: list[Path],
    *,
    required_families: list[str],
    required_motion_modes: list[str],
    motion_delta_threshold: float,
    motion_ratio_warning: float,
    min_sequence_count: int,
) -> dict[str, Any]:
    rows = [
        row_for_packet(
            root,
            motion_delta_threshold=motion_delta_threshold,
            motion_ratio_warning=motion_ratio_warning,
            min_sequence_count=min_sequence_count,
        )
        for root in packet_roots
    ]

    ready_attribution_rows = [
        row for row in rows if row.get("shadow_attribution_ready") is True and not row.get("failures")
    ]
    ready_motion_rows = [
        row
        for row in rows
        if row.get("motion_mode") != "static"
        and row.get("shadow_motion_ready") is True
        and not row.get("failures")
    ]

    observed_families = sorted(
        {
            family
            for row in ready_attribution_rows
            for family in row.get("families", [])
            if isinstance(family, str) and family
        }
    )
    observed_motion_modes = sorted(
        {
            str(row.get("motion_mode"))
            for row in ready_motion_rows
            if row.get("motion_mode") not in (None, "", "unknown")
        }
    )
    if "static" in required_motion_modes and any(
        row.get("motion_mode") == "static" and row.get("shadow_attribution_ready") is True
        for row in ready_attribution_rows
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
        failures.append("missing required shadow families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing required shadow motion modes: " + ", ".join(missing_motion_modes))

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.lighting_shadow_promotion_matrix.v1",
        "packet_count": len(rows),
        "ready_attribution_packet_count": len(ready_attribution_rows),
        "ready_motion_packet_count": len(ready_motion_rows),
        "required_families": required_families,
        "required_motion_modes": required_motion_modes,
        "observed_families": observed_families,
        "missing_families": missing_families,
        "observed_motion_modes": observed_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "shadow_promotion_ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
        "thresholds": {
            "motion_delta_threshold": motion_delta_threshold,
            "motion_ratio_warning": motion_ratio_warning,
            "min_sequence_count": min_sequence_count,
        },
    }


def write_markdown(matrix: dict[str, Any], output: Path) -> None:
    lines = [
        "# LightingShadowV3 Promotion Matrix",
        "",
        f"- packet count: `{matrix['packet_count']}`",
        f"- ready attribution packets: `{matrix['ready_attribution_packet_count']}`",
        f"- ready motion packets: `{matrix['ready_motion_packet_count']}`",
        f"- shadow promotion ready: `{str(matrix['shadow_promotion_ready']).lower()}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- failures: `{len(matrix['failures'])}`",
        f"- warnings: `{len(matrix['warnings'])}`",
        "",
        "| Packet | Motion | Families | Attribution | Motion Ready | Attribution Families | Motion Families | Motion Views | Failures | Warnings |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in matrix["rows"]:
        motion_ready = row.get("shadow_motion_ready")
        motion_label = "n/a" if motion_ready is None else str(bool(motion_ready)).lower()
        lines.append(
            "| {packet} | {motion} | {families} | {attrib} | {motion_ready} | {attrib_count} | {motion_count} | {view_count} | {failures} | {warnings} |".format(
                packet=row["packet_root"],
                motion=row["motion_mode"],
                families=", ".join(row.get("families", [])),
                attrib=str(bool(row.get("shadow_attribution_ready"))).lower(),
                motion_ready=motion_label,
                attrib_count=row.get("shadow_attribution_family_count", 0),
                motion_count=row.get("shadow_motion_family_count", 0),
                view_count=row.get("shadow_motion_view_row_count", 0),
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
    parser.add_argument(
        "--required-motion-modes",
        default="static,mouse_jitter,camera_sweep,light_sweep",
    )
    parser.add_argument("--motion-delta-threshold", type=float, default=0.003)
    parser.add_argument("--motion-ratio-warning", type=float, default=2.50)
    parser.add_argument("--min-sequence-count", type=int, default=2)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    matrix = build_matrix(
        [normalize_packet_root(root) for root in args.packet_root],
        required_families=split_csv(args.required_families),
        required_motion_modes=split_csv(args.required_motion_modes),
        motion_delta_threshold=args.motion_delta_threshold,
        motion_ratio_warning=args.motion_ratio_warning,
        min_sequence_count=args.min_sequence_count,
    )

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")
    write_markdown(matrix, args.output_md)

    print(f"shadow_matrix={args.output_json}")
    print(f"shadow_matrix_md={args.output_md}")
    if not matrix["shadow_promotion_ready"]:
        for failure in matrix["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: LightingShadowV3 promotion matrix is fully covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
