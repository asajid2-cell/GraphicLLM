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
V3_PROMOTION_DECISION_PATH = ROOT / "tools" / "build_full_scene_shader_v3_promotion_decision.py"


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
    "reflection_source_id",
    "reflection_rejected_source_mask",
    "reflection_temporal_delta",
    "reflection_rt_source_signal",
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
    "composite",
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
    "reflection_rt_source_signal",
    "scene_local_environment",
    "hdr_scene_color",
    "candidate_hdr_scene_color",
    "ldr_cinematic_output",
    "candidate_ldr_cinematic_output",
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
    require(
        V3_PROMOTION_DECISION_PATH.exists(),
        errors,
        f"Missing V3 promotion decision builder: {V3_PROMOTION_DECISION_PATH}",
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
    promotion_source = V3_PROMOTION_DECISION_PATH.read_text(encoding="utf-8")
    runtime_surface = "\n".join(
        [frame_contract_source, frame_context_source, analyzer_source, packet_source, promotion_source]
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

    material_contract = domains.get("material", {})
    require(
        material_contract.get("output_resource") == "material_attributes",
        errors,
        "V3 material contract must output material_attributes",
    )
    material_backing_resources = set(material_contract.get("required_backing_resources", []))
    for resource in [
        "vb_gbuffer_albedo",
        "vb_gbuffer_normal_roughness",
        "vb_gbuffer_emissive_metallic",
        "vb_gbuffer_material_ext0",
        "vb_gbuffer_material_ext1",
        "vb_gbuffer_material_ext2",
    ]:
        require(
            resource in material_backing_resources,
            errors,
            f"V3 material contract missing backing resource: {resource}",
        )

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
        "materialAttributesReady",
        '"material_attributes_ready"',
        "lightingAdapterReady",
        '"lighting_adapter_ready"',
        "lightingSplitResourcesAllocated",
        '"lighting_split_resources_allocated"',
        "lightingSplitResourcesReady",
        '"lighting_split_resources_ready"',
        "sceneLocalEnvironmentReady",
        '"scene_local_environment_ready"',
        "sceneLocalEnvironmentMode",
        '"scene_local_environment_mode"',
        "reflectionV3Ready",
        '"reflection_v3_ready"',
        "reflectionRadianceReady",
        '"reflection_radiance_ready"',
        "reflectionConfidenceReady",
        '"reflection_confidence_ready"',
        "reflectionSourceIdReady",
        '"reflection_source_id_ready"',
        "reflectionTemporalDeltaReady",
        '"reflection_temporal_delta_ready"',
        "reflectionRTSourceSignalReady",
        '"reflection_rt_source_signal_ready"',
        "reflectionV3SourceContract",
        '"reflection_v3_source_contract"',
        "compositeV3Ready",
        '"composite_v3_ready"',
        "hdrSceneColorReady",
        '"hdr_scene_color_ready"',
        "compositeInputsReady",
        '"composite_inputs_ready"',
        "compositeEnergyPolicyReady",
        '"composite_energy_policy_ready"',
        "compositeOverbrightDiagnosticsReady",
        '"composite_overbright_diagnostics_ready"',
        "compositeV3Producer",
        '"composite_v3_producer"',
        "cinematicPostV3Ready",
        '"cinematic_post_v3_ready"',
        "ldrCinematicOutputReady",
        '"ldr_cinematic_output_ready"',
        "exposureMeterReady",
        '"exposure_meter_ready"',
        "bloomExtractReady",
        '"bloom_extract_ready"',
        "colorGradeReady",
        '"color_grade_ready"',
        "toneMapReady",
        '"tone_map_ready"',
        "cinematicPostV3Producer",
        '"cinematic_post_v3_producer"',
        "materialAttributesResourceCount",
        '"material_attributes_resource_count"',
        "materialAttributesChannelCount",
        '"material_attributes_channel_count"',
        "lightingAdapterSignalCount",
        '"lighting_adapter_signal_count"',
        "lightingSplitResourceCount",
        '"lighting_split_resource_count"',
        "sceneLocalEnvironmentChannelCount",
        '"scene_local_environment_channel_count"',
        "reflectionV3ChannelCount",
        '"reflection_v3_channel_count"',
        "reflectionV3SourceCount",
        '"reflection_v3_source_count"',
        "compositeV3ChannelCount",
        '"composite_v3_channel_count"',
        "cinematicPostV3ChannelCount",
        '"cinematic_post_v3_channel_count"',
        "backingResources",
        '"backing_resources"',
        "debugViews",
        '"debug_views"',
        "readyChannelCount",
        '"ready_channel_count"',
        "vb_gbuffer_albedo",
        "vb_gbuffer_normal_roughness",
        "vb_gbuffer_emissive_metallic",
        "vb_gbuffer_material_ext0",
        "vb_gbuffer_material_ext1",
        "vb_gbuffer_material_ext2",
        "FullSceneMaterialResolveV3",
        "FullSceneLightingV3Adapter",
        "VBDeferredLighting",
        "hdr_color",
        "VB_DeferredDirectLight",
        "VB_DeferredDirectLightUnshadowed",
        "VB_DeferredDirectLightShadowLoss",
        "FullSceneLightingV3",
        "FullSceneReflectionV3",
        "reflection_radiance_owned",
        "reflection_confidence_owned",
        "reflection_source_id_owned",
        "reflection_rt_source_signal_owned",
        "reflection_temporal_delta_scene_local_bound",
        "reflection_temporal_delta_history_bound",
        "SceneLocalEnvironmentV3",
        "ambient_lighting_owned",
        "reflection_background_owned",
        "FullSceneCompositeV3",
        "FullSceneCompositeV3Adapter",
        "candidate_hdr_scene_color",
        "candidate_hdr_scene_color_owned_by_full_scene_composite_v3",
        "v3_lighting_and_reflection_inputs_read",
        "hdr_scene_color_owned",
        "energy_clamp_policy_owned",
        "overbright_diagnostics_owned",
        "CinematicPostV3",
        "CinematicPostV3Adapter",
        "candidate_ldr_cinematic_output",
        "candidate_ldr_cinematic_output_owned_by_cinematic_post_v3",
        "ldr_cinematic_output_owned",
        "exposure_meter_owned",
        "bloom_extract_owned",
        "color_grade_delta_owned",
        "tone_map_owned",
        "FullSceneShaderPipelineV3PacketGate",
        "cortex.full_scene_shader_pipeline_v3.placeholder_signal.v1",
        "cortex.full_scene_shader_pipeline_v3.placeholder_stability.v1",
        "v3_signal.json",
        "v3_stability.json",
        "promotion_decision.json",
        "promotion_decision.md",
        "cortex.full_scene_shader_pipeline_v3.promotion_decision.v1",
        "candidate_ready_not_promoted",
        "review_packet_passed",
        "default_beauty_promotable",
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
