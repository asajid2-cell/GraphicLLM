#!/usr/bin/env python3
"""Validate the Full Scene Shader Pipeline V2 planning contract.

This is intentionally lightweight. It prevents the V2 plan from drifting into
vague prose by checking that every required machine-readable domain is present
in the Markdown ledger and has concrete outputs in the JSON contract.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN_PATH = ROOT / "docs" / "FULL_SCENE_SHADER_PIPELINE_V2.md"
CONTRACT_PATH = ROOT / "assets" / "final_art" / "full_scene_shader_pipeline_v2_contract.json"
FRAME_REPORT_CONTRACT_PATH = (
    ROOT / "assets" / "final_art" / "full_scene_shader_pipeline_v2_frame_report_contract.json"
)
MATERIAL_EVIDENCE_SCHEMA_PATH = (
    ROOT / "assets" / "final_art" / "full_scene_shader_material_evidence_v2.schema.json"
)
MATERIAL_EVIDENCE_REPORT_PATH = (
    ROOT / "assets" / "final_art" / "full_scene_shader_material_evidence_v2.json"
)
MATERIAL_UPGRADE_PLAN_SCHEMA_PATH = (
    ROOT / "assets" / "final_art" / "full_scene_shader_material_upgrade_plan_v2.schema.json"
)
MATERIAL_UPGRADE_PLAN_PATH = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.json"
)
MATERIAL_PROVIDER_REQUEST_SCHEMA_PATH = (
    ROOT / "assets" / "final_art" / "full_scene_shader_material_provider_requests_v2.schema.json"
)
MATERIAL_PROVIDER_MANIFEST_PATH = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.json"
)


REQUIRED_DOMAIN_IDS = {
    "material_contract",
    "gbuffer_contract",
    "scene_local_lighting",
    "local_reflections",
    "shadow_contact_stability",
    "material_aware_temporal",
    "hdr_post_v2",
    "render_graph_ownership",
    "asset_registry_material_evidence",
    "cross_family_packets",
}

REQUIRED_PHASES = {f"FSSP-V2-{index:03d}" for index in range(1, 11)}

REQUIRED_PLAN_TERMS = {
    "FullSceneMaterialModel",
    "Scene-Local Light Rig System",
    "Local Reflection Probe System",
    "Shadow And Contact Stability",
    "Material-Aware Temporal Pipeline",
    "HDR Cinematic Post V2",
    "Render Graph Ownership Refactor",
    "Cross-Family V2 Gate",
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main() -> int:
    if not PLAN_PATH.exists():
        return fail(f"missing plan: {PLAN_PATH}")
    if not CONTRACT_PATH.exists():
        return fail(f"missing contract: {CONTRACT_PATH}")
    if not FRAME_REPORT_CONTRACT_PATH.exists():
        return fail(f"missing frame-report contract: {FRAME_REPORT_CONTRACT_PATH}")
    if not MATERIAL_EVIDENCE_SCHEMA_PATH.exists():
        return fail(f"missing material evidence schema: {MATERIAL_EVIDENCE_SCHEMA_PATH}")
    if not MATERIAL_EVIDENCE_REPORT_PATH.exists():
        return fail(f"missing material evidence report: {MATERIAL_EVIDENCE_REPORT_PATH}")
    if not MATERIAL_UPGRADE_PLAN_SCHEMA_PATH.exists():
        return fail(f"missing material upgrade plan schema: {MATERIAL_UPGRADE_PLAN_SCHEMA_PATH}")
    if not MATERIAL_UPGRADE_PLAN_PATH.exists():
        return fail(f"missing material upgrade plan: {MATERIAL_UPGRADE_PLAN_PATH}")
    if not MATERIAL_PROVIDER_REQUEST_SCHEMA_PATH.exists():
        return fail(f"missing material provider request schema: {MATERIAL_PROVIDER_REQUEST_SCHEMA_PATH}")
    if not MATERIAL_PROVIDER_MANIFEST_PATH.exists():
        return fail(f"missing material provider request manifest: {MATERIAL_PROVIDER_MANIFEST_PATH}")

    plan_text = PLAN_PATH.read_text(encoding="utf-8")
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    frame_report_contract = json.loads(FRAME_REPORT_CONTRACT_PATH.read_text(encoding="utf-8"))
    material_evidence_schema = json.loads(MATERIAL_EVIDENCE_SCHEMA_PATH.read_text(encoding="utf-8"))
    material_evidence_report = json.loads(MATERIAL_EVIDENCE_REPORT_PATH.read_text(encoding="utf-8"))
    material_upgrade_schema = json.loads(MATERIAL_UPGRADE_PLAN_SCHEMA_PATH.read_text(encoding="utf-8"))
    material_upgrade_plan = json.loads(MATERIAL_UPGRADE_PLAN_PATH.read_text(encoding="utf-8"))
    material_provider_schema = json.loads(
        MATERIAL_PROVIDER_REQUEST_SCHEMA_PATH.read_text(encoding="utf-8")
    )
    material_provider_manifest = json.loads(MATERIAL_PROVIDER_MANIFEST_PATH.read_text(encoding="utf-8"))

    missing_terms = sorted(term for term in REQUIRED_PLAN_TERMS if term not in plan_text)
    if missing_terms:
        return fail("plan missing required terms: " + ", ".join(missing_terms))

    for phase in REQUIRED_PHASES:
        if phase not in plan_text:
            return fail(f"plan missing phase {phase}")

    domains = contract.get("required_domains")
    if not isinstance(domains, list):
        return fail("contract required_domains must be a list")

    domain_ids = {domain.get("id") for domain in domains if isinstance(domain, dict)}
    missing_domains = sorted(REQUIRED_DOMAIN_IDS - domain_ids)
    extra_domains = sorted(domain_id for domain_id in domain_ids - REQUIRED_DOMAIN_IDS if domain_id)
    if missing_domains:
        return fail("contract missing domains: " + ", ".join(missing_domains))
    if extra_domains:
        return fail("contract has unexpected domains: " + ", ".join(extra_domains))

    phase_order = contract.get("phase_order")
    if phase_order != sorted(REQUIRED_PHASES):
        return fail("phase_order must exactly list FSSP-V2-001 through FSSP-V2-010")

    for domain in domains:
        domain_id = domain.get("id", "<missing>")
        phase = domain.get("phase")
        outputs = domain.get("required_outputs")
        gate = domain.get("must_preserve_v1_gate")
        if phase not in REQUIRED_PHASES:
            return fail(f"{domain_id} has invalid phase {phase!r}")
        if not isinstance(outputs, list) or len(outputs) < 3:
            return fail(f"{domain_id} must define at least three required outputs")
        if not gate:
            return fail(f"{domain_id} must name a V1 gate to preserve")
        if phase not in plan_text:
            return fail(f"{domain_id} phase {phase} is not documented in plan")

    frame_sections = frame_report_contract.get("required_sections")
    if not isinstance(frame_sections, list):
        return fail("frame-report contract required_sections must be a list")
    frame_section_ids = {section.get("id") for section in frame_sections if isinstance(section, dict)}
    missing_frame_sections = sorted(REQUIRED_DOMAIN_IDS - frame_section_ids)
    if missing_frame_sections:
        return fail("frame-report contract missing sections: " + ", ".join(missing_frame_sections))
    if frame_report_contract.get("report_key") != "full_scene_shader_pipeline_v2":
        return fail("frame-report contract report_key must be full_scene_shader_pipeline_v2")

    required_material_fields = material_evidence_schema.get("required_root_fields")
    if not isinstance(required_material_fields, list):
        return fail("material evidence schema required_root_fields must be a list")
    missing_material_fields = [
        field for field in required_material_fields if field not in material_evidence_report
    ]
    if missing_material_fields:
        return fail("material evidence report missing fields: " + ", ".join(missing_material_fields))
    if material_evidence_report.get("schema") != "cortex.full_scene_shader_material_evidence_v2":
        return fail("material evidence report schema id is invalid")
    summary = material_evidence_report.get("summary")
    if not isinstance(summary, dict):
        return fail("material evidence report summary must be an object")
    if "v2_material_ready_asset_count" not in summary:
        return fail("material evidence report summary missing v2_material_ready_asset_count")
    if "primitive_hero_material_blocker_count" not in summary:
        return fail("material evidence report summary missing primitive_hero_material_blocker_count")

    required_upgrade_fields = material_upgrade_schema.get("required_root_fields")
    if not isinstance(required_upgrade_fields, list):
        return fail("material upgrade plan schema required_root_fields must be a list")
    missing_upgrade_fields = [
        field for field in required_upgrade_fields if field not in material_upgrade_plan
    ]
    if missing_upgrade_fields:
        return fail("material upgrade plan missing fields: " + ", ".join(missing_upgrade_fields))
    if material_upgrade_plan.get("schema") != "cortex.full_scene_shader_material_upgrade_plan_v2":
        return fail("material upgrade plan schema id is invalid")
    if not isinstance(material_upgrade_plan.get("work_orders"), list):
        return fail("material upgrade plan work_orders must be a list")
    if material_evidence_report.get("status") == "BLOCKED" and not material_upgrade_plan["work_orders"]:
        return fail("blocked material evidence must produce upgrade work orders")

    required_manifest_fields = material_provider_schema.get("required_manifest_fields")
    if not isinstance(required_manifest_fields, list):
        return fail("material provider schema required_manifest_fields must be a list")
    missing_manifest_fields = [
        field for field in required_manifest_fields if field not in material_provider_manifest
    ]
    if missing_manifest_fields:
        return fail("material provider manifest missing fields: " + ", ".join(missing_manifest_fields))
    if (
        material_provider_manifest.get("schema")
        != "cortex.full_scene_shader_material_provider_request_manifest.v2"
    ):
        return fail("material provider manifest schema id is invalid")
    if material_provider_manifest.get("request_count", 0) < material_upgrade_plan["summary"].get("p0_count", 0):
        return fail("material provider manifest must cover at least all P0 material upgrade orders")
    requests = material_provider_manifest.get("requests")
    if not isinstance(requests, list):
        return fail("material provider manifest requests must be a list")
    required_request_fields = material_provider_schema.get("required_request_fields", [])
    for request in requests:
        missing_request_fields = [
            field for field in required_request_fields if field not in request
        ]
        if missing_request_fields:
            return fail(
                f"material provider request {request.get('id', '<missing>')} missing fields: "
                + ", ".join(missing_request_fields)
            )

    target_families = contract.get("target_families")
    if target_families != ["gallery", "kitchen", "office", "gym", "concert"]:
        return fail("target_families must preserve the Renderer V1 five-family gate order")

    hard_rules = contract.get("hard_rules")
    if not isinstance(hard_rules, list) or len(hard_rules) < 5:
        return fail("contract must include at least five hard rules")

    print("PASS: Full Scene Shader Pipeline V2 plan contract is coherent")
    print(f"Plan: {PLAN_PATH}")
    print(f"Contract: {CONTRACT_PATH}")
    print(f"Frame-report contract: {FRAME_REPORT_CONTRACT_PATH}")
    print(f"Material evidence report: {MATERIAL_EVIDENCE_REPORT_PATH}")
    print(f"Material upgrade plan: {MATERIAL_UPGRADE_PLAN_PATH}")
    print(f"Material provider manifest: {MATERIAL_PROVIDER_MANIFEST_PATH}")
    print(f"Domains: {len(domains)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
