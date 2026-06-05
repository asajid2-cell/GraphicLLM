#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


REQUIRED_OUTPUTS = {
    "material_attributes",
    "direct_lighting",
    "indirect_lighting",
    "shadow_visibility",
    "reflection_radiance",
    "reflection_confidence",
    "scene_local_environment",
    "hdr_scene_color",
    "ldr_cinematic_output",
}

REQUIRED_DOMAINS = {
    "render_graph",
    "material",
    "lighting",
    "reflection",
    "environment",
    "cinematic_post",
    "validation",
}

ALLOWED_READY_DOMAINS = {"material"}


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def find_reports(root: pathlib.Path) -> list[pathlib.Path]:
    if root.is_file():
        return [root]
    return sorted(root.glob("**/frame_report_shutdown.json"))


def get_v3(report: dict[str, Any]) -> dict[str, Any] | None:
    frame_contract = report.get("frame_contract")
    if not isinstance(frame_contract, dict):
        return None
    v3 = frame_contract.get("full_scene_shader_pipeline_v3")
    return v3 if isinstance(v3, dict) else None


def analyze_report(path: pathlib.Path) -> dict[str, Any]:
    report = load_json(path)
    v3 = get_v3(report)
    failures: list[str] = []
    warnings: list[str] = []

    if v3 is None:
        return {
            "report": str(path),
            "status": "missing_v3_report",
            "failures": ["frame_contract.full_scene_shader_pipeline_v3 is missing"],
            "warnings": [],
        }

    domains = v3.get("domains", [])
    domain_ids = {
        domain.get("id")
        for domain in domains
        if isinstance(domain, dict) and isinstance(domain.get("id"), str)
    }
    ready_domains = sorted(
        domain.get("id")
        for domain in domains
        if isinstance(domain, dict) and domain.get("ready") is True
    )
    domain_by_id = {
        domain.get("id"): domain
        for domain in domains
        if isinstance(domain, dict) and isinstance(domain.get("id"), str)
    }
    outputs = set(v3.get("required_outputs", []))

    if v3.get("schema") != "cortex.full_scene_shader_pipeline_v3.runtime_report.v1":
        failures.append("wrong V3 runtime report schema")
    if v3.get("status") != "planned_not_promoted":
        failures.append("V3 status must remain planned_not_promoted")
    if v3.get("default_beauty_affects") is not False:
        failures.append("V3 must not affect default beauty in placeholder mode")
    if v3.get("runtime_placeholders_ready") is not True:
        failures.append("runtime_placeholders_ready must be true")
    if v3.get("contract_grounded") is not True:
        failures.append("contract_grounded must be true")
    if v3.get("packet_gate_ready") is not False:
        failures.append("packet_gate_ready must remain false until V3 packets are real gates")

    missing_outputs = sorted(REQUIRED_OUTPUTS - outputs)
    if missing_outputs:
        failures.append("missing required outputs: " + ", ".join(missing_outputs))

    missing_domains = sorted(REQUIRED_DOMAINS - domain_ids)
    if missing_domains:
        failures.append("missing required domains: " + ", ".join(missing_domains))

    unexpected_ready_domains = sorted(set(ready_domains) - ALLOWED_READY_DOMAINS)
    if unexpected_ready_domains:
        warnings.append(
            "V3 has domains ready before their implementation gate: "
            + ", ".join(unexpected_ready_domains)
        )

    material_domain = domain_by_id.get("material")
    material_ready = isinstance(material_domain, dict) and material_domain.get("ready") is True
    if material_ready:
        if material_domain.get("output_resource") != "material_attributes":
            failures.append("material domain must output material_attributes")
        if material_domain.get("producer") != "FullSceneMaterialResolveV3":
            failures.append("material domain must be produced by FullSceneMaterialResolveV3")
        if material_domain.get("default_beauty_affects") is not False:
            failures.append("material domain must not affect default beauty yet")
        if material_domain.get("backing_resource_count", 0) < 6:
            failures.append("material domain ready without all backing resources")
        if material_domain.get("ready_channel_count", 0) < 14:
            failures.append("material domain ready without enough material channels")

    return {
        "report": str(path),
        "status": "ok" if not failures else "failed",
        "schema": v3.get("schema"),
        "v3_status": v3.get("status"),
        "beauty_output": v3.get("beauty_output"),
        "default_beauty_affects": v3.get("default_beauty_affects"),
        "runtime_placeholders_ready": v3.get("runtime_placeholders_ready"),
        "contract_grounded": v3.get("contract_grounded"),
        "packet_gate_ready": v3.get("packet_gate_ready"),
        "required_output_count": len(outputs),
        "domain_count": len(domain_ids),
        "ready_domains": ready_domains,
        "material_attributes_ready": v3.get("material_attributes_ready"),
        "material_attributes_resource_count": v3.get("material_attributes_resource_count"),
        "material_attributes_channel_count": v3.get("material_attributes_channel_count"),
        "failures": failures,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Frame report file or capture root")
    parser.add_argument("--signal-output", required=True)
    parser.add_argument("--stability-output", required=True)
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    reports = find_reports(input_path)
    rows = [analyze_report(path) for path in reports]
    failures = [failure for row in rows for failure in row.get("failures", [])]
    warnings = [warning for row in rows for warning in row.get("warnings", [])]

    signal = {
        "schema": "cortex.full_scene_shader_pipeline_v3.placeholder_signal.v1",
        "input": str(input_path),
        "report_count": len(reports),
        "ok_report_count": sum(1 for row in rows if row.get("status") == "ok"),
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }

    stability = {
        "schema": "cortex.full_scene_shader_pipeline_v3.placeholder_stability.v1",
        "input": str(input_path),
        "report_count": len(reports),
        "default_beauty_affects_any": any(
            row.get("default_beauty_affects") is not False for row in rows
        ),
        "promoted_report_count": sum(
            1 for row in rows if row.get("v3_status") != "planned_not_promoted"
        ),
        "ready_domain_report_count": sum(1 for row in rows if row.get("ready_domains")),
        "material_ready_report_count": sum(
            1 for row in rows if row.get("material_attributes_ready") is True
        ),
        "failures": failures,
        "warnings": warnings,
    }

    signal_path = pathlib.Path(args.signal_output)
    stability_path = pathlib.Path(args.stability_output)
    signal_path.parent.mkdir(parents=True, exist_ok=True)
    stability_path.parent.mkdir(parents=True, exist_ok=True)
    signal_path.write_text(json.dumps(signal, indent=2) + "\n", encoding="utf-8")
    stability_path.write_text(json.dumps(stability, indent=2) + "\n", encoding="utf-8")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("PASS: Full Scene Shader Pipeline V3 placeholder packet artifacts are coherent")
    print(f"reports={len(reports)}")
    print(f"signal={signal_path}")
    print(f"stability={stability_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
