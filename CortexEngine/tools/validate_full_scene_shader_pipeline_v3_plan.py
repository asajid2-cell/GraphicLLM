#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
PLAN_PATH = ROOT / "docs" / "FULL_SCENE_SHADER_PIPELINE_V3.md"
CONTRACT_PATH = ROOT / "assets" / "final_art" / "full_scene_shader_pipeline_v3_contract.json"
FRAME_CONTRACT_JSON_SOURCE_PATH = ROOT / "src" / "Graphics" / "FrameContractJson.cpp"
FULL_SCENE_SHADER_FRAME_CONTEXT_PATH = ROOT / "src" / "Graphics" / "FullSceneShaderFrameContext.h"
V3_PLACEHOLDER_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_placeholders.py"
V3_PACKET_RUNNER_PATH = ROOT / "tools" / "run_full_scene_shader_pipeline_v3_packet.ps1"


REQUIRED_PLAN_TOKENS = [
    "FullSceneShaderPipeline V3 Refactor Plan and Ledger",
    "FullSceneMaterialResolveV3",
    "FullSceneLightingV3",
    "FullSceneReflectionV3",
    "SceneLocalEnvironmentV3",
    "CinematicPostV3",
    "material_attributes",
    "direct_lighting",
    "shadow_visibility",
    "reflection_radiance",
    "reflection_confidence",
    "scene_local_environment",
    "hdr_scene_color",
    "ldr_cinematic_output",
    "Default beauty remains unchanged",
    "gallery",
    "kitchen",
    "office",
    "gym",
    "concert",
    "red_room",
    "stadium",
    "L001 - V3 Contract Exists",
    "L010 - Default Beauty Promotion",
]

REQUIRED_DOMAINS = [
    "render_graph",
    "material",
    "lighting",
    "reflection",
    "environment",
    "cinematic_post",
    "validation",
]

REQUIRED_OUTPUTS = [
    "material_attributes",
    "direct_lighting",
    "indirect_lighting",
    "shadow_visibility",
    "reflection_radiance",
    "reflection_confidence",
    "scene_local_environment",
    "hdr_scene_color",
    "ldr_cinematic_output",
]


def require(condition: bool, errors: list[str], message: str) -> None:
    if not condition:
        errors.append(message)


def main() -> int:
    errors: list[str] = []

    require(PLAN_PATH.exists(), errors, f"Missing V3 plan: {PLAN_PATH}")
    require(CONTRACT_PATH.exists(), errors, f"Missing V3 contract: {CONTRACT_PATH}")
    require(
        FRAME_CONTRACT_JSON_SOURCE_PATH.exists(),
        errors,
        f"Missing frame contract JSON source: {FRAME_CONTRACT_JSON_SOURCE_PATH}",
    )
    require(
        FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.exists(),
        errors,
        f"Missing full scene shader frame context: {FULL_SCENE_SHADER_FRAME_CONTEXT_PATH}",
    )
    require(
        V3_PLACEHOLDER_ANALYZER_PATH.exists(),
        errors,
        f"Missing V3 placeholder analyzer: {V3_PLACEHOLDER_ANALYZER_PATH}",
    )
    require(
        V3_PACKET_RUNNER_PATH.exists(),
        errors,
        f"Missing V3 packet runner: {V3_PACKET_RUNNER_PATH}",
    )
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    plan = PLAN_PATH.read_text(encoding="utf-8")
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    frame_contract_source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")
    frame_context_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    analyzer_source = V3_PLACEHOLDER_ANALYZER_PATH.read_text(encoding="utf-8")
    packet_source = V3_PACKET_RUNNER_PATH.read_text(encoding="utf-8")
    runtime_surface = "\n".join(
        [frame_contract_source, frame_context_source, analyzer_source, packet_source]
    )

    for token in REQUIRED_PLAN_TOKENS:
        require(token in plan, errors, f"V3 plan missing token: {token}")

    require(
        contract.get("schema") == "cortex.full_scene_shader_pipeline_v3.contract.v1",
        errors,
        "V3 contract schema is wrong",
    )
    require(
        contract.get("status") == "planned_not_promoted",
        errors,
        "V3 contract must remain planned_not_promoted until runtime evidence exists",
    )

    families = set(contract.get("required_scene_families", []))
    for family in ["gallery", "kitchen", "office", "gym", "concert", "red_room", "stadium"]:
        require(family in families, errors, f"V3 contract missing scene family: {family}")

    domains = contract.get("domains", {})
    for domain in REQUIRED_DOMAINS:
        require(domain in domains, errors, f"V3 contract missing domain: {domain}")

    render_graph_outputs = set(domains.get("render_graph", {}).get("required_outputs", []))
    for output in REQUIRED_OUTPUTS:
        require(output in render_graph_outputs, errors, f"V3 render graph missing output: {output}")
        require(output in runtime_surface, errors, f"V3 runtime placeholder missing output: {output}")

    validation_gates = set(domains.get("validation", {}).get("required_gates", []))
    for gate in [
        "no_missing_required_resource",
        "reflection_temporal_delta_bounded",
        "environment_mode_matches_scene",
        "default_beauty_unchanged_until_promotion",
    ]:
        require(gate in validation_gates, errors, f"V3 validation missing gate: {gate}")

    for token in [
        "FullSceneShaderPipelineV3ToJson",
        "BuildFullSceneShaderPipelineV3FrameContext",
        "FullSceneShaderPipelineV3FrameContext",
        '"full_scene_shader_pipeline_v3"',
        "cortex.full_scene_shader_pipeline_v3.runtime_report.v1",
        "planned_not_promoted",
        "defaultBeautyAffects = false",
        '"default_beauty_affects"',
        "runtimePlaceholdersReady",
        '"runtime_placeholders_ready"',
        "contractGrounded",
        '"contract_grounded"',
        "packetGateReady",
        '"packet_gate_ready"',
        "FullSceneMaterialResolveV3",
        "FullSceneLightingV3",
        "FullSceneReflectionV3",
        "SceneLocalEnvironmentV3",
        "CinematicPostV3",
        "FullSceneShaderPipelineV3PacketGate",
        "cortex.full_scene_shader_pipeline_v3.placeholder_signal.v1",
        "cortex.full_scene_shader_pipeline_v3.placeholder_stability.v1",
        "v3_signal.json",
        "v3_stability.json",
        "run_full_scene_shader_pipeline_v2_packet.ps1",
    ]:
        require(token in runtime_surface, errors, f"V3 runtime surface missing token: {token}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print("PASS: Full Scene Shader Pipeline V3 plan contract is coherent")
    print(f"Plan: {PLAN_PATH}")
    print(f"Contract: {CONTRACT_PATH}")
    print(f"Domains: {len(domains)}")
    print(f"Required outputs: {len(render_graph_outputs)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
