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

    plan_text = PLAN_PATH.read_text(encoding="utf-8")
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))

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

    target_families = contract.get("target_families")
    if target_families != ["gallery", "kitchen", "office", "gym", "concert"]:
        return fail("target_families must preserve the Renderer V1 five-family gate order")

    hard_rules = contract.get("hard_rules")
    if not isinstance(hard_rules, list) or len(hard_rules) < 5:
        return fail("contract must include at least five hard rules")

    print("PASS: Full Scene Shader Pipeline V2 plan contract is coherent")
    print(f"Plan: {PLAN_PATH}")
    print(f"Contract: {CONTRACT_PATH}")
    print(f"Domains: {len(domains)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
