#!/usr/bin/env python3
"""Check the planned Full Scene Shader Pipeline V2 frame-report surface."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PIPELINE_CONTRACT_PATH = ROOT / "assets" / "final_art" / "full_scene_shader_pipeline_v2_contract.json"
FRAME_REPORT_CONTRACT_PATH = (
    ROOT / "assets" / "final_art" / "full_scene_shader_pipeline_v2_frame_report_contract.json"
)


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def get_path(data: dict[str, Any], dotted_path: str) -> Any:
    cursor: Any = data
    for part in dotted_path.split("."):
        if not isinstance(cursor, dict) or part not in cursor:
            return None
        cursor = cursor[part]
    return cursor


def validate_contracts() -> list[str]:
    errors: list[str] = []
    if not PIPELINE_CONTRACT_PATH.exists():
        return [f"missing pipeline contract: {PIPELINE_CONTRACT_PATH}"]
    if not FRAME_REPORT_CONTRACT_PATH.exists():
        return [f"missing frame-report contract: {FRAME_REPORT_CONTRACT_PATH}"]

    pipeline_contract = load_json(PIPELINE_CONTRACT_PATH)
    frame_contract = load_json(FRAME_REPORT_CONTRACT_PATH)

    pipeline_domains = {
        domain.get("id")
        for domain in pipeline_contract.get("required_domains", [])
        if isinstance(domain, dict)
    }
    frame_sections = {
        section.get("id")
        for section in frame_contract.get("required_sections", [])
        if isinstance(section, dict)
    }

    missing_sections = sorted(domain for domain in pipeline_domains - frame_sections if domain)
    if missing_sections:
        errors.append("frame-report contract missing sections: " + ", ".join(missing_sections))

    extra_sections = sorted(section for section in frame_sections - pipeline_domains if section)
    if extra_sections:
        errors.append("frame-report contract has unknown sections: " + ", ".join(extra_sections))

    report_key = frame_contract.get("report_key")
    if report_key != "full_scene_shader_pipeline_v2":
        errors.append("report_key must be full_scene_shader_pipeline_v2")

    required_sections = frame_contract.get("required_sections")
    if not isinstance(required_sections, list) or not required_sections:
        errors.append("required_sections must be a non-empty list")
    else:
        for section in required_sections:
            section_id = section.get("id", "<missing>")
            if not section.get("report_path", "").startswith("full_scene_shader_pipeline_v2."):
                errors.append(f"{section_id} report_path must be under full_scene_shader_pipeline_v2")
            fields = section.get("readiness_fields")
            if not isinstance(fields, list) or len(fields) < 5:
                errors.append(f"{section_id} must define at least five readiness_fields")
            elif "enabled" not in fields:
                errors.append(f"{section_id} readiness_fields must include enabled")
            debug_views = section.get("debug_views")
            if not isinstance(debug_views, list) or len(debug_views) < 3:
                errors.append(f"{section_id} must define at least three debug_views")

    hard_gate_defaults = frame_contract.get("hard_gate_defaults")
    if not isinstance(hard_gate_defaults, dict):
        errors.append("hard_gate_defaults must be an object")
    else:
        expected_false = [
            "allow_external_hdri_in_enclosed_scenes",
            "allow_unknown_material_family_in_v2",
            "allow_unknown_reflection_owner_in_v2",
            "allow_missing_debug_view_source_in_v2",
        ]
        for key in expected_false:
            if hard_gate_defaults.get(key) is not False:
                errors.append(f"hard_gate_defaults.{key} must be false")

    return errors


def validate_frame_report(frame_report_path: Path, strict: bool) -> list[str]:
    errors: list[str] = []
    frame_contract = load_json(FRAME_REPORT_CONTRACT_PATH)
    frame_report = load_json(frame_report_path)

    report_key = frame_contract["report_key"]
    v2_report = frame_report.get(report_key)
    if not isinstance(v2_report, dict):
        message = f"frame report missing top-level {report_key}"
        if strict:
            errors.append(message)
        else:
            print(f"WARN: {message}")
            return errors

    for section in frame_contract["required_sections"]:
        section_id = section["id"]
        section_data = get_path(frame_report, section["report_path"])
        if not isinstance(section_data, dict):
            errors.append(f"missing section {section_id} at {section['report_path']}")
            continue
        for field in section["readiness_fields"]:
            if field not in section_data:
                errors.append(f"{section_id} missing readiness field {field}")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--frame-report", type=Path, help="Optional frame_report JSON to check.")
    parser.add_argument(
        "--strict-frame-report",
        action="store_true",
        help="Fail when the frame report does not yet include the V2 top-level key.",
    )
    args = parser.parse_args()

    errors = validate_contracts()
    if args.frame_report:
        if not args.frame_report.exists():
            errors.append(f"missing frame report: {args.frame_report}")
        else:
            errors.extend(validate_frame_report(args.frame_report, args.strict_frame_report))

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print("PASS: Full Scene Shader Pipeline V2 frame-report contract is coherent")
    print(f"Pipeline contract: {PIPELINE_CONTRACT_PATH}")
    print(f"Frame-report contract: {FRAME_REPORT_CONTRACT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
