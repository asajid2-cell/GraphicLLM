#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]

DEFAULT_REQUIRED_FAMILIES = [
    "gallery",
    "kitchen",
    "office",
    "gym",
    "concert",
    "red_room",
    "stadium",
]

DEFAULT_REQUIRED_MOTION_MODES = ["static", "mouse_jitter", "camera_sweep"]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def normalize_packet_root(raw: str, *, packet_list_path: Path | None = None, suite_output_root: Path | None = None) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path

    candidates = [Path.cwd() / path, ROOT / path]
    if packet_list_path is not None:
        candidates.append(packet_list_path.parent / path)
    if suite_output_root is not None:
        candidates.append(suite_output_root / path.name)

    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return (ROOT / path).resolve()


def packet_row(packet_root: Path, packet_status: dict[str, Any] | None = None) -> dict[str, Any]:
    manifest_path = packet_root / "manifest.json"
    promotion_path = packet_root / "promotion_decision.json"
    stability_path = packet_root / "v3_stability.json"
    packet_exit_code = None
    packet_runner_failed = False
    continued_after_failure = False
    if packet_status:
        packet_exit_code = packet_status.get("exit_code")
        packet_runner_failed = packet_exit_code not in (None, 0)
        continued_after_failure = packet_status.get("continued_after_failure") is True
    row: dict[str, Any] = {
        "packet_root": str(packet_root),
        "manifest": str(manifest_path),
        "promotion_decision": str(promotion_path),
        "stability": str(stability_path),
        "packet_exit_code": packet_exit_code,
        "packet_runner_failed": packet_runner_failed,
        "continued_after_failure": continued_after_failure,
        "exists": packet_root.exists(),
        "review_packet_passed": False,
        "families": [],
        "motion_mode": "unknown",
        "failures": [],
        "warnings": [],
    }
    if packet_runner_failed:
        row["failures"].append(f"packet runner exited {packet_exit_code}")
    if not packet_root.exists():
        row["failures"].append("packet root missing")
        return row
    if not manifest_path.exists():
        row["failures"].append("manifest.json missing")
        return row
    if not promotion_path.exists():
        row["failures"].append("promotion_decision.json missing")
        return row

    manifest = load_json(manifest_path)
    promotion = load_json(promotion_path)
    families: set[str] = set()
    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        family = result.get("family")
        if isinstance(family, str) and family:
            families.add(family)
    row["families"] = sorted(families)
    row["motion_mode"] = str(manifest.get("stability_motion_mode", "static"))
    row["review_packet_passed"] = promotion.get("review_packet_passed") is True
    row["promotion_status"] = promotion.get("status", "unknown")
    row["default_beauty_promotable"] = promotion.get("default_beauty_promotable")
    row["full_coverage_ready"] = promotion.get("full_coverage_ready")
    row["ready_domain_report_counts"] = promotion.get("ready_domain_report_counts", {})
    row["candidate_beauty_requested_report_count"] = promotion.get(
        "candidate_beauty_requested_report_count", 0
    )
    row["candidate_beauty_ready_report_count"] = promotion.get(
        "candidate_beauty_ready_report_count", 0
    )
    row["candidate_beauty_predicates"] = promotion.get("candidate_beauty_predicates", {})
    row["material_quality_gate"] = promotion.get("material_quality_gate", {})
    row["failures"].extend(str(item) for item in promotion.get("failures", []))
    row["warnings"].extend(str(item) for item in promotion.get("warnings", []))
    if not row["review_packet_passed"]:
        row["failures"].append("packet promotion decision did not pass review")
    return row


def build_matrix(
    packet_roots: list[Path],
    required_families: list[str],
    required_motion_modes: list[str],
    packet_status_by_root: dict[str, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    packet_status_by_root = packet_status_by_root or {}
    rows = [
        packet_row(root, packet_status_by_root.get(str(root)) or packet_status_by_root.get(root.as_posix()))
        for root in packet_roots
    ]
    passed_rows = [row for row in rows if row.get("review_packet_passed") is True]
    observed_families = sorted(
        {
            family
            for row in passed_rows
            for family in row.get("families", [])
            if isinstance(family, str)
        }
    )
    observed_motion_modes = sorted(
        {
            str(row.get("motion_mode"))
            for row in passed_rows
            if isinstance(row.get("motion_mode"), str) and row.get("motion_mode") != "unknown"
        }
    )
    failures: list[str] = []
    warnings: list[str] = []
    aggregate_candidate_blockers: dict[str, int] = {}
    aggregate_requested_candidate_blockers: dict[str, int] = {}
    aggregate_material_quality_blockers: dict[str, int] = {}
    material_quality_scores: list[float] = []
    for row in rows:
        for failure in row.get("failures", []):
            failures.append(f"{row.get('packet_root')}: {failure}")
        for warning in row.get("warnings", []):
            warnings.append(f"{row.get('packet_root')}: {warning}")
        predicates = row.get("candidate_beauty_predicates", {})
        if isinstance(predicates, dict):
            blockers = predicates.get("blocker_counts", {})
            if isinstance(blockers, dict):
                for key, value in blockers.items():
                    aggregate_candidate_blockers[str(key)] = (
                        aggregate_candidate_blockers.get(str(key), 0) + int(value or 0)
                    )
            requested_blockers = predicates.get("requested_blocker_counts", {})
            if isinstance(requested_blockers, dict):
                for key, value in requested_blockers.items():
                    aggregate_requested_candidate_blockers[str(key)] = (
                        aggregate_requested_candidate_blockers.get(str(key), 0) + int(value or 0)
                    )
        material_quality = row.get("material_quality_gate", {})
        if isinstance(material_quality, dict):
            material_quality_scores.append(float(material_quality.get("score", 0.0) or 0.0))
            blockers = material_quality.get("blockers", [])
            if isinstance(blockers, list):
                for blocker in blockers:
                    key = str(blocker)
                    aggregate_material_quality_blockers[key] = (
                        aggregate_material_quality_blockers.get(key, 0) + 1
                    )

    missing_families = sorted(set(required_families) - set(observed_families))
    missing_motion_modes = sorted(set(required_motion_modes) - set(observed_motion_modes))
    if missing_families:
        failures.append("missing required families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing required motion modes: " + ", ".join(missing_motion_modes))

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.matrix_decision.v1",
        "packet_count": len(rows),
        "passed_packet_count": len(passed_rows),
        "required_families": required_families,
        "observed_families": observed_families,
        "missing_families": missing_families,
        "required_motion_modes": required_motion_modes,
        "observed_motion_modes": observed_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "full_matrix_ready": not failures,
        "default_beauty_promotable": False,
        "candidate_beauty_blocker_counts": dict(sorted(aggregate_candidate_blockers.items())),
        "candidate_beauty_requested_blocker_counts": dict(
            sorted(aggregate_requested_candidate_blockers.items())
        ),
        "material_quality_min_score": min(material_quality_scores) if material_quality_scores else 0.0,
        "material_quality_blocker_counts": dict(sorted(aggregate_material_quality_blockers.items())),
        "packets": rows,
        "failures": failures,
        "warnings": warnings,
    }


def write_markdown(path: Path, matrix: dict[str, Any]) -> None:
    lines = [
        "# Full Scene Shader Pipeline V3 Matrix Decision",
        "",
        f"- packet count: `{matrix['packet_count']}`",
        f"- passed packet count: `{matrix['passed_packet_count']}`",
        f"- full matrix ready: `{str(matrix['full_matrix_ready']).lower()}`",
        f"- default beauty promotable: `{str(matrix['default_beauty_promotable']).lower()}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- candidate blocker counts: `{json.dumps(matrix.get('candidate_beauty_blocker_counts', {}), sort_keys=True)}`",
        f"- requested candidate blocker counts: `{json.dumps(matrix.get('candidate_beauty_requested_blocker_counts', {}), sort_keys=True)}`",
        f"- material quality min score: `{float(matrix.get('material_quality_min_score', 0.0) or 0.0):.4f}`",
        f"- material quality blocker counts: `{json.dumps(matrix.get('material_quality_blocker_counts', {}), sort_keys=True)}`",
        "",
        "| Packet | Motion | Families | Exit | Passed | Status | Material Score | Candidate Ready | Candidate Blockers |",
        "|---|---|---|---:|---|---|---:|---:|---|",
    ]
    for row in matrix["packets"]:
        predicates = row.get("candidate_beauty_predicates", {})
        if not isinstance(predicates, dict):
            predicates = {}
        blocker_counts = predicates.get("blocker_counts", {})
        if not isinstance(blocker_counts, dict):
            blocker_counts = {}
        material_quality = row.get("material_quality_gate", {})
        material_score = 0.0
        if isinstance(material_quality, dict):
            material_score = float(material_quality.get("score", 0.0) or 0.0)
        lines.append(
            "| "
            + " | ".join(
                [
                    f"`{row.get('packet_root')}`",
                    f"`{row.get('motion_mode')}`",
                    f"`{', '.join(row.get('families', []))}`",
                    f"`{row.get('packet_exit_code')}`",
                    f"`{str(row.get('review_packet_passed')).lower()}`",
                    f"`{row.get('promotion_status', 'unknown')}`",
                    f"`{material_score:.4f}`",
                    f"`{row.get('candidate_beauty_ready_report_count', 0)}/{row.get('candidate_beauty_requested_report_count', 0)}`",
                    f"`{json.dumps(blocker_counts, sort_keys=True)}`",
                ]
            )
            + " |"
        )
    if matrix["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in matrix["failures"])
    if matrix["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in matrix["warnings"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packet-root", action="append", default=[], help="Packet root to include; repeatable.")
    parser.add_argument("--packet-list-json", type=Path, help="Optional JSON file containing packet_roots.")
    parser.add_argument("--required-families", default=",".join(DEFAULT_REQUIRED_FAMILIES))
    parser.add_argument("--required-motion-modes", default=",".join(DEFAULT_REQUIRED_MOTION_MODES))
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    roots = [normalize_packet_root(str(item)) for item in args.packet_root]
    packet_status_by_root: dict[str, dict[str, Any]] = {}
    if args.packet_list_json:
        data = load_json(args.packet_list_json)
        suite_output_root_raw = data.get("output_root")
        suite_output_root = Path(str(suite_output_root_raw)) if suite_output_root_raw else None
        roots.extend(
            normalize_packet_root(
                str(item),
                packet_list_path=args.packet_list_json,
                suite_output_root=suite_output_root,
            )
            for item in data.get("packet_roots", [])
        )
        for status in data.get("packet_status", []):
            if not isinstance(status, dict):
                continue
            packet_root = status.get("packet_root")
            if isinstance(packet_root, str) and packet_root:
                packet_status_by_root[packet_root] = status
                resolved_root = normalize_packet_root(
                    packet_root,
                    packet_list_path=args.packet_list_json,
                    suite_output_root=suite_output_root,
                )
                packet_status_by_root[str(resolved_root)] = status
                packet_status_by_root[resolved_root.as_posix()] = status
    if not roots:
        raise SystemExit("at least one --packet-root or --packet-list-json entry is required")

    matrix = build_matrix(
        roots,
        split_csv(args.required_families),
        split_csv(args.required_motion_modes),
        packet_status_by_root,
    )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.output_md, matrix)
    print(f"matrix_decision={args.output_json}")
    print(f"matrix_markdown={args.output_md}")
    if matrix["failures"]:
        print(f"MATRIX INCOMPLETE: failures={len(matrix['failures'])}")
    else:
        print("PASS: V3 promotion matrix is fully covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
