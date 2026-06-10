#!/usr/bin/env python3
"""Aggregate FullSceneShaderPipeline V3 promotion/readiness matrices.

Tool marker: build_full_scene_shader_v3_readiness_audit.py.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def gate(
    *,
    name: str,
    path: Path,
    ready_field: str,
    extra_required_true: list[str] | None = None,
) -> dict[str, Any]:
    extra_required_true = extra_required_true or []
    row: dict[str, Any] = {
        "name": name,
        "path": str(path),
        "exists": path.exists(),
        "ready_field": ready_field,
        "ready": False,
        "failures": [],
        "warnings": [],
        "summary": {},
    }
    if not path.exists():
        row["failures"].append("matrix file missing")
        return row

    data = load_json(path)
    ready = data.get(ready_field) is True
    row["ready"] = ready
    if not ready:
        row["failures"].append(f"{ready_field} is not true")
    for field in extra_required_true:
        if data.get(field) is not True:
            row["failures"].append(f"{field} is not true")
    row["warnings"] = [str(item) for item in data.get("warnings", [])]
    row["failures"].extend(str(item) for item in data.get("failures", []))
    row["summary"] = {
        "packet_count": data.get("packet_count"),
        "observed_families": data.get("observed_families"),
        "missing_families": data.get("missing_families"),
        "observed_motion_modes": data.get("observed_motion_modes"),
        "missing_motion_modes": data.get("missing_motion_modes"),
    }
    for key in (
        "promoted_packet_count",
        "promoted_report_count",
        "ready_packet_count",
        "ready_attribution_packet_count",
        "debug_visibility_packet_count",
        "source_resolver_packet_count",
        "minimum_material_quality_score",
        "total_report_count",
        "total_ready_report_count",
        "candidate_beauty_ready_report_count",
        "candidate_beauty_requested_report_count",
        "material_quality_min_score",
    ):
        if key in data:
            row["summary"][key] = data.get(key)
    return row


def build_audit(args: argparse.Namespace) -> dict[str, Any]:
    gates = [
        gate(
            name="default_beauty_promotion",
            path=args.default_promotion_matrix,
            ready_field="default_beauty_promotable",
            extra_required_true=["full_matrix_ready", "candidate_beauty_review_ready"],
        ),
        gate(
            name="scene_local_resource_contract",
            path=args.scene_local_matrix,
            ready_field="scene_local_resource_contract_ready",
        ),
        gate(
            name="material_quality",
            path=args.material_quality_matrix,
            ready_field="material_quality_ready",
        ),
        gate(
            name="lighting_shadow_v3",
            path=args.shadow_matrix,
            ready_field="shadow_promotion_ready",
        ),
        gate(
            name="reflection_v3",
            path=args.reflection_matrix,
            ready_field="reflection_promotion_ready",
        ),
    ]
    failures: list[str] = []
    warnings: list[str] = []
    for row in gates:
        for failure in row["failures"]:
            failures.append(f"{row['name']}: {failure}")
        for warning in row["warnings"]:
            warnings.append(f"{row['name']}: {warning}")

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.readiness_audit.v1",
        "engineering_readiness_ready": not failures,
        "human_visual_acceptance_required": True,
        "goal_completion_ready": False,
        "goal_completion_blockers": [
            "human_visual_acceptance_required",
            "release_visual_artifact_review_not_encoded_in_audit",
        ],
        "gate_count": len(gates),
        "ready_gate_count": sum(1 for row in gates if row["ready"] and not row["failures"]),
        "failures": failures,
        "warnings": warnings,
        "gates": gates,
    }


def write_markdown(audit: dict[str, Any], output: Path) -> None:
    lines = [
        "# Full Scene Shader Pipeline V3 Readiness Audit",
        "",
        f"- engineering readiness ready: `{str(audit['engineering_readiness_ready']).lower()}`",
        f"- goal completion ready: `{str(audit['goal_completion_ready']).lower()}`",
        f"- human visual acceptance required: `{str(audit['human_visual_acceptance_required']).lower()}`",
        f"- gates: `{audit['ready_gate_count']}/{audit['gate_count']}`",
        f"- failures: `{len(audit['failures'])}`",
        f"- warnings: `{len(audit['warnings'])}`",
        "",
        "| Gate | Ready | Matrix | Key Summary | Failures | Warnings |",
        "|---|---:|---|---|---:|---:|",
    ]
    for row in audit["gates"]:
        summary = row.get("summary", {})
        key_summary = []
        for key in (
            "packet_count",
            "promoted_packet_count",
            "promoted_report_count",
            "ready_packet_count",
            "total_report_count",
            "minimum_material_quality_score",
        ):
            if summary.get(key) is not None:
                key_summary.append(f"{key}={summary[key]}")
        missing_families = summary.get("missing_families")
        missing_motion = summary.get("missing_motion_modes")
        if missing_families is not None:
            key_summary.append(f"missing_families={len(missing_families)}")
        if missing_motion is not None:
            key_summary.append(f"missing_motion={len(missing_motion)}")
        lines.append(
            "| {name} | {ready} | `{path}` | {summary} | {failures} | {warnings} |".format(
                name=row["name"],
                ready=str(bool(row["ready"] and not row["failures"])).lower(),
                path=row["path"],
                summary=", ".join(key_summary),
                failures=len(row["failures"]),
                warnings=len(row["warnings"]),
            )
        )
    if audit["goal_completion_blockers"]:
        lines.extend(["", "## Goal Completion Blockers", ""])
        lines.extend(f"- `{blocker}`" for blocker in audit["goal_completion_blockers"])
    if audit["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in audit["warnings"])
    if audit["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in audit["failures"])
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--default-promotion-matrix", required=True, type=Path)
    parser.add_argument("--scene-local-matrix", required=True, type=Path)
    parser.add_argument("--material-quality-matrix", required=True, type=Path)
    parser.add_argument("--shadow-matrix", required=True, type=Path)
    parser.add_argument("--reflection-matrix", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    audit = build_audit(args)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    write_markdown(audit, args.output_md)

    print(f"readiness_audit={args.output_json}")
    print(f"readiness_audit_md={args.output_md}")
    if not audit["engineering_readiness_ready"]:
        for failure in audit["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: FullSceneShaderPipeline V3 engineering readiness gates are satisfied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
