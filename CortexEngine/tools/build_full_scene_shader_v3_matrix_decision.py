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


def is_packet_shard_coverage_warning(warning: str) -> bool:
    lowered = warning.lower()
    return (
        "missing required families" in lowered
        or "missing required motion modes" in lowered
        or "motion promotion evidence requires capture_sequence_count" in lowered
    )


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
    stability = load_json(stability_path) if stability_path.exists() else {}
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
    row["default_beauty_promotion_allowed"] = promotion.get("default_beauty_promotion_allowed")
    row["default_beauty_promotion_blockers"] = promotion.get(
        "default_beauty_promotion_blockers", []
    )
    row["default_beauty_affects_any"] = stability.get("default_beauty_affects_any")
    row["promoted_report_count"] = int(stability.get("promoted_report_count", 0) or 0)
    row["candidate_beauty_review_ready"] = promotion.get("candidate_beauty_review_ready")
    row["candidate_beauty_review_gate"] = promotion.get("candidate_beauty_review_gate", {})
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
    promoted_rows = [
        row
        for row in passed_rows
        if int(row.get("promoted_report_count", 0) or 0) > 0
        and row.get("default_beauty_affects_any") is True
    ]
    observed_promoted_families = sorted(
        {
            family
            for row in promoted_rows
            for family in row.get("families", [])
            if isinstance(family, str)
        }
    )
    observed_promoted_motion_modes = sorted(
        {
            str(row.get("motion_mode"))
            for row in promoted_rows
            if isinstance(row.get("motion_mode"), str) and row.get("motion_mode") != "unknown"
        }
    )
    failures: list[str] = []
    packet_warning_records: list[str] = []
    aggregate_candidate_blockers: dict[str, int] = {}
    aggregate_requested_candidate_blockers: dict[str, int] = {}
    aggregate_candidate_review_blockers: dict[str, int] = {}
    packet_default_promotion_blockers: dict[str, int] = {}
    aggregate_material_quality_blockers: dict[str, int] = {}
    material_quality_scores: list[float] = []
    total_candidate_requested_reports = 0
    total_candidate_ready_reports = 0
    for row in rows:
        for failure in row.get("failures", []):
            failures.append(f"{row.get('packet_root')}: {failure}")
        for warning in row.get("warnings", []):
            packet_warning_records.append(f"{row.get('packet_root')}: {warning}")
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
        total_candidate_requested_reports += int(row.get("candidate_beauty_requested_report_count", 0) or 0)
        total_candidate_ready_reports += int(row.get("candidate_beauty_ready_report_count", 0) or 0)
        candidate_review = row.get("candidate_beauty_review_gate", {})
        if isinstance(candidate_review, dict):
            for blocker in candidate_review.get("blockers", []):
                key = str(blocker)
                aggregate_candidate_review_blockers[key] = (
                    aggregate_candidate_review_blockers.get(key, 0) + 1
                )
        for blocker in row.get("default_beauty_promotion_blockers", []):
            key = str(blocker)
            packet_default_promotion_blockers[key] = (
                packet_default_promotion_blockers.get(key, 0) + 1
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
    missing_promoted_families = sorted(set(required_families) - set(observed_promoted_families))
    missing_promoted_motion_modes = sorted(
        set(required_motion_modes) - set(observed_promoted_motion_modes)
    )
    if missing_families:
        failures.append("missing required families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing required motion modes: " + ", ".join(missing_motion_modes))

    full_matrix_ready = not failures
    warnings: list[str] = []
    packet_shard_coverage_notes: list[str] = []
    for warning in packet_warning_records:
        if full_matrix_ready and is_packet_shard_coverage_warning(warning):
            packet_shard_coverage_notes.append(warning)
        else:
            warnings.append(warning)
    candidate_beauty_review_ready = (
        full_matrix_ready
        and total_candidate_requested_reports > 0
        and total_candidate_ready_reports == total_candidate_requested_reports
        and not aggregate_requested_candidate_blockers
        and not aggregate_candidate_review_blockers
        and not aggregate_material_quality_blockers
    )
    if not full_matrix_ready:
        aggregate_candidate_review_blockers["full_matrix_not_ready"] = (
            aggregate_candidate_review_blockers.get("full_matrix_not_ready", 0) + 1
        )
    if total_candidate_requested_reports <= 0:
        aggregate_candidate_review_blockers["candidate_beauty_not_requested"] = (
            aggregate_candidate_review_blockers.get("candidate_beauty_not_requested", 0) + 1
        )
    if total_candidate_ready_reports != total_candidate_requested_reports:
        aggregate_candidate_review_blockers["candidate_beauty_ready_count_mismatch"] = (
            aggregate_candidate_review_blockers.get("candidate_beauty_ready_count_mismatch", 0) + 1
        )
    if aggregate_requested_candidate_blockers:
        aggregate_candidate_review_blockers["candidate_beauty_requested_blockers_present"] = (
            aggregate_candidate_review_blockers.get("candidate_beauty_requested_blockers_present", 0) + 1
        )
    if aggregate_material_quality_blockers:
        aggregate_candidate_review_blockers["material_quality_gate_not_ready"] = (
            aggregate_candidate_review_blockers.get("material_quality_gate_not_ready", 0) + 1
        )

    aggregate_default_promotion_blockers: dict[str, int] = {}
    if not candidate_beauty_review_ready:
        aggregate_default_promotion_blockers["candidate_beauty_review_not_ready"] = 1
    if not full_matrix_ready:
        aggregate_default_promotion_blockers["full_matrix_not_ready"] = 1
    if not promoted_rows:
        aggregate_default_promotion_blockers["default_beauty_runtime_path_not_enabled"] = 1
    if missing_promoted_families:
        aggregate_default_promotion_blockers["missing_promoted_families"] = len(
            missing_promoted_families
        )
    if missing_promoted_motion_modes:
        aggregate_default_promotion_blockers["missing_promoted_motion_modes"] = len(
            missing_promoted_motion_modes
        )
    runtime_path_blockers = packet_default_promotion_blockers.get(
        "default_beauty_runtime_path_not_enabled", 0
    )
    if runtime_path_blockers and not promoted_rows:
        aggregate_default_promotion_blockers["default_beauty_runtime_path_not_enabled"] = (
            runtime_path_blockers
        )
    default_beauty_promotable = (
        candidate_beauty_review_ready
        and full_matrix_ready
        and bool(promoted_rows)
        and not missing_promoted_families
        and not missing_promoted_motion_modes
        and not aggregate_default_promotion_blockers
    )

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
        "observed_promoted_families": observed_promoted_families,
        "missing_promoted_families": missing_promoted_families,
        "observed_promoted_motion_modes": observed_promoted_motion_modes,
        "missing_promoted_motion_modes": missing_promoted_motion_modes,
        "promoted_packet_count": len(promoted_rows),
        "promoted_report_count": sum(
            int(row.get("promoted_report_count", 0) or 0) for row in promoted_rows
        ),
        "full_matrix_ready": full_matrix_ready,
        "candidate_beauty_review_ready": candidate_beauty_review_ready,
        "default_beauty_promotable": default_beauty_promotable,
        "candidate_beauty_blocker_counts": dict(sorted(aggregate_candidate_blockers.items())),
        "candidate_beauty_requested_blocker_counts": dict(
            sorted(aggregate_requested_candidate_blockers.items())
        ),
        "candidate_beauty_review_blocker_counts": dict(
            sorted(aggregate_candidate_review_blockers.items())
        ),
        "candidate_beauty_requested_report_count": total_candidate_requested_reports,
        "candidate_beauty_ready_report_count": total_candidate_ready_reports,
        "default_beauty_promotion_blocker_counts": dict(
            sorted(aggregate_default_promotion_blockers.items())
        ),
        "packet_default_beauty_promotion_blocker_counts": dict(
            sorted(packet_default_promotion_blockers.items())
        ),
        "material_quality_min_score": min(material_quality_scores) if material_quality_scores else 0.0,
        "material_quality_blocker_counts": dict(sorted(aggregate_material_quality_blockers.items())),
        "packet_shard_coverage_note_count": len(packet_shard_coverage_notes),
        "packet_shard_coverage_notes": packet_shard_coverage_notes,
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
        f"- candidate beauty review ready: `{str(matrix.get('candidate_beauty_review_ready')).lower()}`",
        f"- default beauty promotable: `{str(matrix['default_beauty_promotable']).lower()}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- promoted packet count: `{matrix.get('promoted_packet_count', 0)}`",
        f"- promoted report count: `{matrix.get('promoted_report_count', 0)}`",
        f"- observed promoted families: `{', '.join(matrix.get('observed_promoted_families', []))}`",
        f"- missing promoted families: `{', '.join(matrix.get('missing_promoted_families', []))}`",
        f"- observed promoted motion modes: `{', '.join(matrix.get('observed_promoted_motion_modes', []))}`",
        f"- missing promoted motion modes: `{', '.join(matrix.get('missing_promoted_motion_modes', []))}`",
        f"- candidate blocker counts: `{json.dumps(matrix.get('candidate_beauty_blocker_counts', {}), sort_keys=True)}`",
        f"- requested candidate blocker counts: `{json.dumps(matrix.get('candidate_beauty_requested_blocker_counts', {}), sort_keys=True)}`",
        f"- candidate review blocker counts: `{json.dumps(matrix.get('candidate_beauty_review_blocker_counts', {}), sort_keys=True)}`",
        f"- candidate beauty ready reports: `{matrix.get('candidate_beauty_ready_report_count', 0)}/{matrix.get('candidate_beauty_requested_report_count', 0)}`",
        f"- default promotion blocker counts: `{json.dumps(matrix.get('default_beauty_promotion_blocker_counts', {}), sort_keys=True)}`",
        f"- packet default promotion blocker counts: `{json.dumps(matrix.get('packet_default_beauty_promotion_blocker_counts', {}), sort_keys=True)}`",
        f"- material quality min score: `{float(matrix.get('material_quality_min_score', 0.0) or 0.0):.4f}`",
        f"- material quality blocker counts: `{json.dumps(matrix.get('material_quality_blocker_counts', {}), sort_keys=True)}`",
        f"- packet shard coverage notes: `{matrix.get('packet_shard_coverage_note_count', 0)}`",
        "",
        "| Packet | Motion | Families | Exit | Passed | Status | Candidate Review | Promoted Reports | Material Score | Candidate Ready | Requested Blockers | Default Blockers |",
        "|---|---|---|---:|---|---|---|---:|---:|---:|---|---|",
    ]
    for row in matrix["packets"]:
        predicates = row.get("candidate_beauty_predicates", {})
        if not isinstance(predicates, dict):
            predicates = {}
        blocker_counts = predicates.get("blocker_counts", {})
        if not isinstance(blocker_counts, dict):
            blocker_counts = {}
        requested_blocker_counts = predicates.get("requested_blocker_counts", {})
        if not isinstance(requested_blocker_counts, dict):
            requested_blocker_counts = {}
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
                    f"`{str(row.get('candidate_beauty_review_ready')).lower()}`",
                    f"`{row.get('promoted_report_count', 0)}`",
                    f"`{material_score:.4f}`",
                    f"`{row.get('candidate_beauty_ready_report_count', 0)}/{row.get('candidate_beauty_requested_report_count', 0)}`",
                    f"`{json.dumps(requested_blocker_counts, sort_keys=True)}`",
                    f"`{json.dumps(row.get('default_beauty_promotion_blockers', []), sort_keys=True)}`",
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
    if matrix.get("packet_shard_coverage_notes"):
        lines.extend(["", "## Packet Shard Coverage Notes", ""])
        lines.extend(f"- {note}" for note in matrix["packet_shard_coverage_notes"])
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
