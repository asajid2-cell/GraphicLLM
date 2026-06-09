#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
PLAN_PATH = ROOT / "docs" / "FULL_SCENE_SHADER_PIPELINE_V3.md"
CONTRACT_PATH = ROOT / "assets" / "final_art" / "full_scene_shader_pipeline_v3_contract.json"
SCENE_LOCAL_RESOURCE_CONTRACT_PATH = ROOT / "assets" / "final_art" / "scene_local_resource_contract_v1.json"
FRAME_CONTRACT_JSON_SOURCE_PATH = ROOT / "src" / "Graphics" / "FrameContractJson.cpp"
FULL_SCENE_SHADER_FRAME_CONTEXT_PATH = ROOT / "src" / "Graphics" / "FullSceneShaderFrameContext.h"
RENDERER_RENDER_GRAPH_END_FRAME_PATH = ROOT / "src" / "Graphics" / "Renderer_RenderGraphEndFrame.cpp"
RENDERER_FRAME_POST_CONSTANTS_PATH = ROOT / "src" / "Graphics" / "Renderer_FramePostConstants.cpp"
ENGINE_SOURCE_PATH = ROOT / "src" / "Core" / "Engine.cpp"
V3_GRAPH_BUILDER_HEADER_PATH = ROOT / "src" / "Graphics" / "FullSceneShaderV3GraphBuilder.h"
V3_GRAPH_BUILDER_SOURCE_PATH = ROOT / "src" / "Graphics" / "FullSceneShaderV3GraphBuilder.cpp"
V3_PASSES_HEADER_PATH = ROOT / "src" / "Graphics" / "Passes" / "FullSceneShaderV3Passes.h"
V3_PASSES_SOURCE_PATH = ROOT / "src" / "Graphics" / "Passes" / "FullSceneShaderV3Passes.cpp"
CMAKE_PATH = ROOT / "CMakeLists.txt"
V3_PLACEHOLDER_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_placeholders.py"
V3_PACKET_RUNNER_PATH = ROOT / "tools" / "run_full_scene_shader_pipeline_v3_packet.ps1"
V3_MATRIX_RUNNER_PATH = ROOT / "tools" / "run_full_scene_shader_pipeline_v3_matrix.ps1"
V3_PROMOTION_SUITE_RUNNER_PATH = ROOT / "tools" / "run_full_scene_shader_pipeline_v3_promotion_suite.ps1"
SCENE_LOCAL_PACKET_RUNNER_PATH = ROOT / "tools" / "run_scene_local_cinematic_renderer_v1_packets.ps1"
V3_PROMOTION_DECISION_PATH = ROOT / "tools" / "build_full_scene_shader_v3_promotion_decision.py"
V3_MATRIX_DECISION_PATH = ROOT / "tools" / "build_full_scene_shader_v3_matrix_decision.py"
V3_MATERIAL_PAYLOAD_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_material_payload.py"
V3_SCENE_PROFILE_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_scene_profile.py"
V3_ENVIRONMENT_PAYLOAD_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_environment_payload.py"
SCENE_LOCAL_RESOURCE_CONTRACT_ANALYZER_PATH = ROOT / "tools" / "analyze_scene_local_resource_contract_v1.py"
V3_ENVIRONMENT_PROFILE_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_environment_profiles.py"
V3_ENVIRONMENT_PROXY_GENERATOR_PATH = ROOT / "tools" / "generate_scene_local_environment_proxies.py"
V3_ENVIRONMENT_PROXY_CONTRACT_HEADER_PATH = (
    ROOT / "src" / "Graphics" / "Generated" / "SceneLocalProxyContracts.generated.h"
)
V3_SHADOW_ATTRIBUTION_ANALYZER_PATH = ROOT / "tools" / "analyze_full_scene_shader_v3_shadow_attribution.py"
V3_SHADOW_FOCUS_RUNNER_PATH = ROOT / "tools" / "run_lighting_v3_shadow_motion_focus_packet.ps1"
V3_SHADOW_PROMOTION_MATRIX_PATH = ROOT / "tools" / "build_lighting_v3_shadow_promotion_matrix.py"
V3_REFLECTION_SOURCE_RESOLVER_ANALYZER_PATH = ROOT / "tools" / "analyze_reflection_v3_source_resolver.py"
V3_REFLECTION_FOCUS_RUNNER_PATH = ROOT / "tools" / "run_reflection_v3_motion_focus_packet.ps1"
V3_REFLECTION_PROMOTION_MATRIX_PATH = ROOT / "tools" / "build_reflection_v3_promotion_matrix.py"
SCENE_LOCAL_ENVIRONMENT_V3_SHADER_PATH = ROOT / "assets" / "shaders" / "SceneLocalEnvironmentV3.hlsl"
DEFERRED_LIGHTING_SHADER_PATH = ROOT / "assets" / "shaders" / "DeferredLighting.hlsl"
FULL_SCENE_REFLECTION_RESOLVER_V3_SHADER_PATH = ROOT / "assets" / "shaders" / "FullSceneReflectionResolverV3.hlsl"


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
    "lighting_energy_budget",
    "shadow_source_attribution",
    "local_reflection_radiance",
    "reflection_radiance",
    "reflection_confidence",
    "reflection_source_id",
    "reflection_rejected_source_mask",
    "reflection_temporal_delta",
    "reflection_rt_source_signal",
    "reflection_source_suppression",
    "reflection_history_v3_curr",
    "reflection_history_v3_prev",
    "reflection_history_v3_prev_source_id",
    "reflection_history_v3_validity",
    "reflection_history_v3_rejection",
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
    "scene_profile",
    "material",
    "lighting",
    "reflection",
    "environment",
    "composite",
    "cinematic_post",
    "candidate_beauty",
    "validation",
]

REQUIRED_OUTPUTS = [
    "material_attributes",
    "direct_lighting",
    "indirect_lighting",
    "shadow_visibility",
    "shadow_loss",
    "lighting_energy_budget",
    "shadow_source_attribution",
    "reflection_radiance",
    "reflection_confidence",
    "reflection_rt_source_signal",
    "reflection_source_suppression",
    "reflection_history_v3_curr",
    "reflection_history_v3_prev",
    "reflection_history_v3_prev_source_id",
    "reflection_history_v3_validity",
    "reflection_history_v3_rejection",
    "scene_local_environment",
    "hdr_scene_color",
    "candidate_hdr_scene_color",
    "energy_clamp_policy",
    "overbright_diagnostics",
    "composite_contribution_map",
    "legacy_rescue_usage",
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
        SCENE_LOCAL_RESOURCE_CONTRACT_PATH.exists(),
        errors,
        f"Missing scene-local resource contract: {SCENE_LOCAL_RESOURCE_CONTRACT_PATH}",
    )
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
        RENDERER_FRAME_POST_CONSTANTS_PATH.exists(),
        errors,
        f"Missing renderer frame post constants source: {RENDERER_FRAME_POST_CONSTANTS_PATH}",
    )
    require(
        ENGINE_SOURCE_PATH.exists(),
        errors,
        f"Missing engine source: {ENGINE_SOURCE_PATH}",
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
        V3_MATRIX_RUNNER_PATH.exists(),
        errors,
        f"Missing V3 matrix runner: {V3_MATRIX_RUNNER_PATH}",
    )
    require(
        V3_PROMOTION_SUITE_RUNNER_PATH.exists(),
        errors,
        f"Missing V3 promotion suite runner: {V3_PROMOTION_SUITE_RUNNER_PATH}",
    )
    require(
        SCENE_LOCAL_PACKET_RUNNER_PATH.exists(),
        errors,
        f"Missing scene-local packet runner: {SCENE_LOCAL_PACKET_RUNNER_PATH}",
    )
    require(
        V3_PROMOTION_DECISION_PATH.exists(),
        errors,
        f"Missing V3 promotion decision builder: {V3_PROMOTION_DECISION_PATH}",
    )
    require(
        V3_MATRIX_DECISION_PATH.exists(),
        errors,
        f"Missing V3 matrix decision builder: {V3_MATRIX_DECISION_PATH}",
    )
    require(
        V3_MATERIAL_PAYLOAD_ANALYZER_PATH.exists(),
        errors,
        f"Missing V3 material payload analyzer: {V3_MATERIAL_PAYLOAD_ANALYZER_PATH}",
    )
    require(
        V3_SCENE_PROFILE_ANALYZER_PATH.exists(),
        errors,
        f"Missing V3 scene profile analyzer: {V3_SCENE_PROFILE_ANALYZER_PATH}",
    )
    require(
        V3_ENVIRONMENT_PAYLOAD_ANALYZER_PATH.exists(),
        errors,
        f"Missing V3 environment payload analyzer: {V3_ENVIRONMENT_PAYLOAD_ANALYZER_PATH}",
    )
    require(
        SCENE_LOCAL_RESOURCE_CONTRACT_ANALYZER_PATH.exists(),
        errors,
        f"Missing scene-local resource contract analyzer: {SCENE_LOCAL_RESOURCE_CONTRACT_ANALYZER_PATH}",
    )
    require(
        V3_ENVIRONMENT_PROFILE_ANALYZER_PATH.exists(),
        errors,
        f"Missing V3 environment profile analyzer: {V3_ENVIRONMENT_PROFILE_ANALYZER_PATH}",
    )
    require(
        V3_ENVIRONMENT_PROXY_CONTRACT_HEADER_PATH.exists(),
        errors,
        f"Missing V3 environment proxy contract header: {V3_ENVIRONMENT_PROXY_CONTRACT_HEADER_PATH}",
    )
    require(
        V3_SHADOW_ATTRIBUTION_ANALYZER_PATH.exists(),
        errors,
        f"Missing V3 shadow attribution analyzer: {V3_SHADOW_ATTRIBUTION_ANALYZER_PATH}",
    )
    require(
        V3_SHADOW_FOCUS_RUNNER_PATH.exists(),
        errors,
        f"Missing V3 shadow focus runner: {V3_SHADOW_FOCUS_RUNNER_PATH}",
    )
    require(
        V3_SHADOW_PROMOTION_MATRIX_PATH.exists(),
        errors,
        f"Missing V3 shadow promotion matrix builder: {V3_SHADOW_PROMOTION_MATRIX_PATH}",
    )
    require(
        V3_REFLECTION_SOURCE_RESOLVER_ANALYZER_PATH.exists(),
        errors,
        f"Missing ReflectionV3 source resolver analyzer: {V3_REFLECTION_SOURCE_RESOLVER_ANALYZER_PATH}",
    )
    require(
        V3_REFLECTION_FOCUS_RUNNER_PATH.exists(),
        errors,
        f"Missing ReflectionV3 focus runner: {V3_REFLECTION_FOCUS_RUNNER_PATH}",
    )
    require(
        V3_REFLECTION_PROMOTION_MATRIX_PATH.exists(),
        errors,
        f"Missing ReflectionV3 promotion matrix builder: {V3_REFLECTION_PROMOTION_MATRIX_PATH}",
    )
    require(
        SCENE_LOCAL_ENVIRONMENT_V3_SHADER_PATH.exists(),
        errors,
        f"Missing SceneLocalEnvironmentV3 shader: {SCENE_LOCAL_ENVIRONMENT_V3_SHADER_PATH}",
    )
    require(
        DEFERRED_LIGHTING_SHADER_PATH.exists(),
        errors,
        f"Missing DeferredLighting shader: {DEFERRED_LIGHTING_SHADER_PATH}",
    )
    require(
        FULL_SCENE_REFLECTION_RESOLVER_V3_SHADER_PATH.exists(),
        errors,
        f"Missing FullSceneReflectionResolverV3 shader: {FULL_SCENE_REFLECTION_RESOLVER_V3_SHADER_PATH}",
    )
    require(
        V3_GRAPH_BUILDER_HEADER_PATH.exists(),
        errors,
        f"Missing V3 graph builder header: {V3_GRAPH_BUILDER_HEADER_PATH}",
    )
    require(
        V3_GRAPH_BUILDER_SOURCE_PATH.exists(),
        errors,
        f"Missing V3 graph builder source: {V3_GRAPH_BUILDER_SOURCE_PATH}",
    )
    require(V3_PASSES_HEADER_PATH.exists(), errors, f"Missing V3 pass helper header: {V3_PASSES_HEADER_PATH}")
    require(V3_PASSES_SOURCE_PATH.exists(), errors, f"Missing V3 pass helper source: {V3_PASSES_SOURCE_PATH}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    plan = PLAN_PATH.read_text(encoding="utf-8")
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    scene_local_resource_contract = json.loads(SCENE_LOCAL_RESOURCE_CONTRACT_PATH.read_text(encoding="utf-8"))
    frame_contract_source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")
    frame_context_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    render_graph_end_frame_source = RENDERER_RENDER_GRAPH_END_FRAME_PATH.read_text(encoding="utf-8")
    frame_post_constants_source = RENDERER_FRAME_POST_CONSTANTS_PATH.read_text(encoding="utf-8")
    engine_source = ENGINE_SOURCE_PATH.read_text(encoding="utf-8")
    v3_graph_builder_header_source = V3_GRAPH_BUILDER_HEADER_PATH.read_text(encoding="utf-8")
    v3_graph_builder_source = V3_GRAPH_BUILDER_SOURCE_PATH.read_text(encoding="utf-8")
    v3_passes_header_source = V3_PASSES_HEADER_PATH.read_text(encoding="utf-8")
    v3_passes_source = V3_PASSES_SOURCE_PATH.read_text(encoding="utf-8")
    cmake_source = CMAKE_PATH.read_text(encoding="utf-8")
    analyzer_source = V3_PLACEHOLDER_ANALYZER_PATH.read_text(encoding="utf-8")
    packet_source = V3_PACKET_RUNNER_PATH.read_text(encoding="utf-8")
    matrix_runner_source = V3_MATRIX_RUNNER_PATH.read_text(encoding="utf-8")
    promotion_suite_runner_source = V3_PROMOTION_SUITE_RUNNER_PATH.read_text(encoding="utf-8")
    scene_local_packet_source = SCENE_LOCAL_PACKET_RUNNER_PATH.read_text(encoding="utf-8")
    promotion_source = V3_PROMOTION_DECISION_PATH.read_text(encoding="utf-8")
    matrix_source = V3_MATRIX_DECISION_PATH.read_text(encoding="utf-8")
    material_payload_source = V3_MATERIAL_PAYLOAD_ANALYZER_PATH.read_text(encoding="utf-8")
    scene_profile_source = V3_SCENE_PROFILE_ANALYZER_PATH.read_text(encoding="utf-8")
    environment_payload_source = V3_ENVIRONMENT_PAYLOAD_ANALYZER_PATH.read_text(encoding="utf-8")
    scene_local_resource_contract_analyzer_source = SCENE_LOCAL_RESOURCE_CONTRACT_ANALYZER_PATH.read_text(
        encoding="utf-8"
    )
    environment_profile_source = V3_ENVIRONMENT_PROFILE_ANALYZER_PATH.read_text(encoding="utf-8")
    environment_proxy_generator_source = V3_ENVIRONMENT_PROXY_GENERATOR_PATH.read_text(encoding="utf-8")
    environment_proxy_contract_header_source = V3_ENVIRONMENT_PROXY_CONTRACT_HEADER_PATH.read_text(encoding="utf-8")
    shadow_attribution_source = V3_SHADOW_ATTRIBUTION_ANALYZER_PATH.read_text(encoding="utf-8")
    shadow_focus_runner_source = V3_SHADOW_FOCUS_RUNNER_PATH.read_text(encoding="utf-8")
    shadow_promotion_matrix_source = V3_SHADOW_PROMOTION_MATRIX_PATH.read_text(encoding="utf-8")
    reflection_source_resolver_source = V3_REFLECTION_SOURCE_RESOLVER_ANALYZER_PATH.read_text(encoding="utf-8")
    reflection_focus_runner_source = V3_REFLECTION_FOCUS_RUNNER_PATH.read_text(encoding="utf-8")
    reflection_promotion_matrix_source = V3_REFLECTION_PROMOTION_MATRIX_PATH.read_text(encoding="utf-8")
    scene_local_environment_v3_shader = SCENE_LOCAL_ENVIRONMENT_V3_SHADER_PATH.read_text(encoding="utf-8")
    deferred_lighting_shader = DEFERRED_LIGHTING_SHADER_PATH.read_text(encoding="utf-8")
    full_scene_reflection_resolver_v3_shader = FULL_SCENE_REFLECTION_RESOLVER_V3_SHADER_PATH.read_text(encoding="utf-8")
    runtime_surface = "\n".join(
        [
            frame_contract_source,
            frame_context_source,
            render_graph_end_frame_source,
            frame_post_constants_source,
            engine_source,
            v3_graph_builder_header_source,
            v3_graph_builder_source,
            v3_passes_header_source,
            v3_passes_source,
            cmake_source,
            analyzer_source,
            packet_source,
            matrix_runner_source,
            promotion_suite_runner_source,
            scene_local_packet_source,
            promotion_source,
            matrix_source,
            material_payload_source,
            scene_profile_source,
            environment_payload_source,
            scene_local_resource_contract_analyzer_source,
            environment_profile_source,
            environment_proxy_generator_source,
            environment_proxy_contract_header_source,
            shadow_attribution_source,
            shadow_focus_runner_source,
            shadow_promotion_matrix_source,
            reflection_source_resolver_source,
            reflection_focus_runner_source,
            reflection_promotion_matrix_source,
            scene_local_environment_v3_shader,
            deferred_lighting_shader,
            full_scene_reflection_resolver_v3_shader,
        ]
    )

    for token in REQUIRED_PLAN_TOKENS:
        require(token in plan, errors, f"V3 plan missing token: {token}")

    require(
        "FullSceneShaderV3GraphBuilder.h" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must include the V3 graph builder module",
    )
    require(
        "FullSceneShaderV3GraphBuilder fullSceneShaderV3" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must submit V3 passes through FullSceneShaderV3GraphBuilder",
    )
    require(
        "struct FullSceneCompositeV3Context" not in render_graph_end_frame_source,
        errors,
        "FullSceneCompositeV3Context must not live in Renderer_RenderGraphEndFrame.cpp",
    )
    require(
        "struct SceneLocalEnvironmentV3Context" not in render_graph_end_frame_source,
        errors,
        "SceneLocalEnvironmentV3Context must not live in Renderer_RenderGraphEndFrame.cpp",
    )
    require(
        "struct FullSceneReflectionResolverV3Context" not in render_graph_end_frame_source,
        errors,
        "FullSceneReflectionResolverV3Context must not live in Renderer_RenderGraphEndFrame.cpp",
    )
    require(
        "struct CandidateBeautyDisplayContext" not in render_graph_end_frame_source,
        errors,
        "CandidateBeautyDisplayContext must not live in Renderer_RenderGraphEndFrame.cpp",
    )
    for token in [
        "FullSceneShaderV3GraphBuilder",
        "DisplayCommon",
        "DisplaySubmission",
        "CompositeCommon",
        "CompositeSubmission",
        "SceneLocalEnvironmentCommon",
        "SceneLocalEnvironmentSubmission",
        "SubmitSceneLocalEnvironment",
        "SubmitReflectionResolver",
        "SubmitReflectionHistory",
        "SubmitReflectionHistoryCopy",
        "SubmitComposite",
        "SubmitDisplay",
    ]:
        require(
            token in v3_graph_builder_header_source,
            errors,
            f"V3 graph builder header missing token: {token}",
        )
        require(
            token in v3_graph_builder_source,
            errors,
            f"V3 graph builder source missing token: {token}",
        )
    for token in [
        "FullSceneShaderV3Passes",
        "FullSceneCompositeV3Context",
        "SceneLocalEnvironmentV3Context",
        "FullSceneReflectionResolverV3Context",
        "FullSceneReflectionHistoryV3Context",
        "CandidateBeautyDisplayContext",
        "AddSceneLocalEnvironmentV3Pass",
        "AddFullSceneCompositeV3Pass",
        "AddFullSceneReflectionResolverV3Pass",
        "AddFullSceneReflectionHistoryV3Pass",
        "AddCandidateBeautyDisplayPass",
    ]:
        require(token in v3_passes_header_source, errors, f"V3 pass helper header missing token: {token}")
        require(token in v3_passes_source, errors, f"V3 pass helper source missing token: {token}")
    require(
        "src/Graphics/FullSceneShaderV3GraphBuilder.cpp" in cmake_source,
        errors,
        "CMakeLists.txt must compile FullSceneShaderV3GraphBuilder.cpp",
    )
    require(
        "src/Graphics/Passes/FullSceneShaderV3Passes.cpp" in cmake_source,
        errors,
        "CMakeLists.txt must compile FullSceneShaderV3Passes.cpp",
    )
    require(
        "CandidateBeautyDisplayContext" not in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must not construct CandidateBeautyDisplayContext directly",
    )
    require(
        "DisplayCommon displayCommon" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must use shared V3 display common state",
    )
    require(
        "SubmitDisplay(displayCommon" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must submit V3 displays through graph-builder common display API",
    )
    require(
        "SceneLocalEnvironmentV3Context" not in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must not construct SceneLocalEnvironmentV3Context directly",
    )
    require(
        "SceneLocalEnvironmentCommon environmentCommon" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must use shared V3 environment common state",
    )
    require(
        "SceneLocalEnvironmentSubmission environmentSubmission" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must use V3 environment submission data",
    )
    require(
        "SubmitSceneLocalEnvironment(environmentCommon, environmentSubmission)" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must submit environment through graph-builder environment API",
    )
    require(
        "FullSceneCompositeV3Context" not in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must not construct FullSceneCompositeV3Context directly",
    )
    require(
        "CompositeCommon compositeCommon" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must use shared V3 composite common state",
    )
    require(
        "CompositeSubmission compositeSubmission" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must use V3 composite submission data",
    )
    require(
        "SubmitComposite(compositeCommon, compositeSubmission)" in render_graph_end_frame_source,
        errors,
        "Renderer_RenderGraphEndFrame.cpp must submit composite through graph-builder composite API",
    )
    for token in [
        "AddSceneLocalEnvironmentV3Pass(",
        "AddFullSceneCompositeV3Pass(",
        "AddFullSceneReflectionResolverV3Pass(",
        "AddFullSceneReflectionHistoryV3Pass(",
        "AddFullSceneReflectionHistoryV3CopyPass(",
        "AddCandidateBeautyDisplayPass(",
    ]:
        require(
            token not in render_graph_end_frame_source,
            errors,
            f"Renderer_RenderGraphEndFrame.cpp must not call low-level V3 pass helper directly: {token}",
        )

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
    resource_contract_ref = contract.get("scene_local_resource_contract", {})
    require(
        isinstance(resource_contract_ref, dict),
        errors,
        "V3 contract must reference SceneLocalResourceContractV1",
    )
    if isinstance(resource_contract_ref, dict):
        require(
            resource_contract_ref.get("schema") == "cortex.scene_local_resource_contract.v1",
            errors,
            "V3 contract scene_local_resource_contract schema is wrong",
        )
        require(
            resource_contract_ref.get("path") == "assets/final_art/scene_local_resource_contract_v1.json",
            errors,
            "V3 contract must point at scene_local_resource_contract_v1.json",
        )
        require(
            resource_contract_ref.get("analyzer") == "tools/analyze_scene_local_resource_contract_v1.py",
            errors,
            "V3 contract must point at the scene-local resource contract analyzer",
        )
    require(
        scene_local_resource_contract.get("schema") == "cortex.scene_local_resource_contract.v1",
        errors,
        "Scene-local resource contract schema is wrong",
    )
    scene_local_roles = set(scene_local_resource_contract.get("required_roles", []))
    for role in [
        "diffuse_irradiance",
        "specular_radiance",
        "visible_background",
        "reflection_background",
        "atmosphere",
        "exposure",
    ]:
        require(role in scene_local_roles, errors, f"Scene-local resource contract missing role: {role}")
    runtime_fields = set(resource_contract_ref.get("required_runtime_fields", [])) if isinstance(
        resource_contract_ref,
        dict,
    ) else set()
    for field in [
        "scene_local_resource_contract_ready",
        "scene_local_resource_contract_id",
        "scene_local_resource_contract_family",
        "scene_local_resource_contract_status",
        "scene_local_resource_contract_unsafe_reason",
        "scene_local_resource_contract_external_hdri_safe",
        "scene_local_resource_contract_reflection_source_allowed",
        "scene_local_resource_contract_ready_role_count",
    ]:
        require(field in runtime_fields, errors, f"V3 contract missing scene-local runtime field: {field}")
    scene_local_families = scene_local_resource_contract.get("family_contracts", {})
    require(isinstance(scene_local_families, dict), errors, "Scene-local resource contract missing family_contracts")
    if isinstance(scene_local_families, dict):
        for family in ["gallery", "kitchen", "office", "gym", "concert", "red_room", "stadium"]:
            family_contract = scene_local_families.get(family)
            require(isinstance(family_contract, dict), errors, f"Scene-local resource contract missing {family}")
            if isinstance(family_contract, dict):
                require(
                    "visible_external_hdri_allowed" in family_contract,
                    errors,
                    f"Scene-local resource contract {family} missing visible_external_hdri_allowed",
                )
                require(
                    "allowed_reflection_source_contracts" in family_contract,
                    errors,
                    f"Scene-local resource contract {family} missing allowed_reflection_source_contracts",
                )
    for token in [
        "scene_local_resource_contract_v1.json",
    ]:
        require(token in packet_source, errors, f"V3 packet runner missing scene-local resource token: {token}")
        require(token in promotion_source, errors, f"V3 promotion decision missing scene-local resource token: {token}")
        require(
            token in scene_local_resource_contract_analyzer_source,
            errors,
            f"Scene-local resource contract analyzer missing token: {token}",
        )
    for token in [
        "Scene-local resource contract",
        "reflection_v3_source_contract",
        "visible_external_hdri_allowed",
    ]:
        require(
            token in scene_local_resource_contract_analyzer_source,
            errors,
            f"Scene-local resource contract analyzer missing token: {token}",
        )

    families = set(contract.get("required_scene_families", []))
    for family in ["gallery", "kitchen", "office", "gym", "concert", "red_room", "stadium"]:
        require(family in families, errors, f"V3 contract missing scene family: {family}")
    candidate_path_debt_fields = set(contract.get("candidate_path_debt_fields", []))
    for field in [
        "render_graph_missing_producer_count",
        "render_graph_missing_producer_resources",
        "material_missing_required_channels",
        "lighting_missing_required_channels",
        "environment_missing_required_channels",
        "reflection_missing_required_channels",
        "composite_missing_required_channels",
        "cinematic_post_missing_required_channels",
        "candidate_beauty_missing_required_channels",
        "candidate_beauty_predicate_count",
        "candidate_beauty_ready_predicate_count",
        "candidate_beauty_blockers",
        "total_missing_required_channels",
        "not_ready_domain_count",
        "legacy_rescue_resource_ready",
    ]:
        require(
            field in candidate_path_debt_fields,
            errors,
            f"V3 contract missing candidate-path debt field: {field}",
        )

    domains = contract.get("domains", {})
    for domain in REQUIRED_DOMAINS:
        require(domain in domains, errors, f"V3 contract missing domain: {domain}")

    render_graph_outputs = set(domains.get("render_graph", {}).get("required_outputs", []))
    for output in REQUIRED_OUTPUTS:
        require(output in render_graph_outputs, errors, f"V3 render graph missing output: {output}")
        require(output in runtime_surface, errors, f"V3 runtime placeholder missing output: {output}")
    render_graph_contract_fields = set(domains.get("render_graph", {}).get("required_contract_fields", []))
    for field in [
        "render_graph_v3_inventory_ready",
        "render_graph_v3_pass_count",
        "render_graph_v3_executed_pass_count",
        "render_graph_v3_read_resource_count",
        "render_graph_v3_write_resource_count",
        "render_graph_v3_missing_producer_count",
        "render_graph_v3_pass_names",
        "render_graph_v3_read_resources",
        "render_graph_v3_written_resources",
        "render_graph_v3_missing_producer_resources",
    ]:
        require(
            field in render_graph_contract_fields,
            errors,
            f"V3 render graph missing contract field: {field}",
        )

    scene_profile_contract = domains.get("scene_profile", {})
    require(
        scene_profile_contract.get("producer") == "SceneProfileV3",
        errors,
        "V3 scene_profile domain must be produced by SceneProfileV3",
    )
    require(
        scene_profile_contract.get("adapter_input") == "SceneCinematicProfileV1Adapter",
        errors,
        "V3 scene_profile contract must preserve SceneCinematicProfileV1Adapter as adapter input",
    )
    require(
        scene_profile_contract.get("output_resource") == "scene_profile_policy_contract",
        errors,
        "V3 scene_profile contract must output scene_profile_policy_contract",
    )
    require(
        scene_profile_contract.get("backing_contract") == "frame_contract.scene_visual_contract",
        errors,
        "V3 scene_profile contract must keep scene_visual_contract as backing_contract",
    )
    for field in [
        "profile_id",
        "family",
        "environment_owner",
        "reflection_owner",
        "light_rig_id",
        "shadow_policy_id",
        "exposure_policy_id",
        "material_palette_id",
        "lighting_script_id",
        "material_class_set_id",
        "material_layer_set_id",
        "temporal_policy_id",
        "post_policy_id",
        "post_quality_set_id",
        "tone_mapper_preset",
    ]:
        require(
            field in scene_profile_contract.get("required_fields", []),
            errors,
            f"V3 scene_profile contract missing required field: {field}",
        )
    for field in [
        "owner",
        "contract_id",
        "family",
        "enclosure_mode",
        "environment_policy",
        "lighting_policy",
        "reflection_policy",
        "exposure_policy",
        "material_policy",
        "temporal_policy",
        "post_policy",
        "motion_stability_policy",
    ]:
        require(
            field in scene_profile_contract.get("required_policy_contract_fields", []),
            errors,
            f"V3 scene_profile contract missing required policy contract field: {field}",
        )
    for token in [
        "sceneProfileReady",
        '"scene_profile_ready"',
        "sceneProfilePolicyCount",
        '"scene_profile_policy_count"',
        "sceneProfilePolicyContractReady",
        '"scene_profile_policy_contract_ready"',
        "sceneProfilePolicyOwner",
        '"scene_profile_policy_contract"',
        "SceneProfileV3",
        "SceneCinematicProfileV1Adapter",
        "scene_visual_contract",
        "scene_profile_policy_contract",
        "scene_profile",
    ]:
        require(token in runtime_surface, errors, f"V3 scene_profile runtime missing token: {token}")

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
    material_packet_debug_views = set(material_contract.get("packet_debug_views", []))
    for view in [
        "roughness",
        "metallic",
        "surface_class",
        "surface_policy",
        "material_family",
        "reflection_policy",
        "temporal_policy",
        "post_sensitivity",
        "material_id",
        "object_id",
    ]:
        require(view in material_packet_debug_views, errors, f"V3 material contract missing packet debug view: {view}")
        require(view in runtime_surface, errors, f"V3 runtime surface missing material packet debug view: {view}")

    validation_gates = set(domains.get("validation", {}).get("required_gates", []))
    for gate in [
        "no_missing_required_resource",
        "scene_profile_policy_ready",
        "scene_profile_family_differences_present",
        "reflection_temporal_delta_bounded",
        "environment_mode_matches_scene",
        "material_payload_debug_views_present",
        "material_payload_ranges_valid",
        "default_beauty_unchanged_until_promotion",
    ]:
        require(gate in validation_gates, errors, f"V3 validation missing gate: {gate}")

    required_artifacts = set(domains.get("validation", {}).get("required_artifacts", []))
    require("v3_scene_profile.json" in required_artifacts, errors, "V3 validation missing v3_scene_profile.json artifact")
    require(
        "v3_environment_payload.json" in required_artifacts,
        errors,
        "V3 validation missing v3_environment_payload.json artifact",
    )

    for domain_id in ["composite", "cinematic_post"]:
        require(
            domains.get(domain_id, {}).get("readiness_scope") == "candidate_beauty_requested",
            errors,
            f"V3 {domain_id} contract must declare candidate-only readiness scope",
        )
    for token in [
        "CANDIDATE_ONLY_DOMAINS",
        "candidate_beauty_requested_count",
        "candidate_beauty_requested",
    ]:
        require(token in runtime_surface, errors, f"V3 candidate-only domain gate missing token: {token}")

    candidate_beauty_contract = domains.get("candidate_beauty", {})
    require(
        candidate_beauty_contract.get("producer") == "CinematicPostV3",
        errors,
        "V3 candidate_beauty contract must be produced by CinematicPostV3",
    )
    candidate_inputs = set(candidate_beauty_contract.get("required_inputs", []))
    require(
        "candidate_hdr_scene_color" in candidate_inputs,
        errors,
        "V3 candidate_beauty contract must require candidate_hdr_scene_color",
    )
    rejected_candidate_inputs = set(candidate_beauty_contract.get("rejected_ready_inputs", []))
    for resource in ["hdr_color", "ldr_cinematic_output"]:
        require(
            resource in rejected_candidate_inputs,
            errors,
            f"V3 candidate_beauty contract must reject ready input: {resource}",
        )

    environment_contract = domains.get("environment", {})
    environment_policy_channels = set(environment_contract.get("required_policy_channels", []))
    for channel in [
        "scene_local_environment_policy",
        "visible_background_source",
        "reflection_background_source",
        "ambient_source",
        "atmosphere_source",
        "scene_profile_policy_contract",
        "scene_profile_enclosure_mode",
        "scene_profile_reflection_policy",
        "scene_local_shader_profile",
        "scene_local_background_strength",
    ]:
        require(
            channel in environment_policy_channels,
            errors,
            f"V3 environment contract missing policy channel: {channel}",
        )
    environment_payload_channels = set(environment_contract.get("payload_channels", []))
    for channel in [
        "scene_local_texture_set_id",
        "scene_local_texture_count",
        "scene_local_payload_ready",
        "scene_local_irradiance_proxy_ready",
        "scene_local_specular_proxy_ready",
        "scene_local_visible_background_proxy_ready",
        "scene_local_payload_texture_richness",
        "scene_local_payload_proxy_score",
        "scene_local_payload_shader_influence",
        "scene_local_payload_resource_table_required",
        "scene_local_payload_resource_table_bindable",
        "scene_local_payload_bound_resource_count",
        "scene_local_payload_binding_source",
        "scene_local_payload_fallback_reason",
        "scene_local_proxy_resource_table_required",
        "scene_local_proxy_resource_table_bindable",
        "scene_local_proxy_bound_resource_count",
        "scene_local_proxy_binding_source",
        "scene_local_proxy_fallback_reason",
        "scene_local_proxy_derivation_method",
        "scene_local_proxy_room_shell",
        "scene_local_proxy_room_occlusion",
        "scene_local_proxy_light_rig",
        "scene_local_proxy_light_accent_strength",
        "scene_local_proxy_resource_shape",
        "scene_local_proxy_filtered_output_count",
        "scene_local_proxy_min_filter_variance",
    ]:
        require(
            channel in environment_payload_channels,
            errors,
            f"V3 environment contract missing payload channel: {channel}",
        )

    reflection_contract = domains.get("reflection", {})
    reflection_resolver_inputs = set(reflection_contract.get("required_resolver_inputs", []))
    for resource in [
        "local_reflection_radiance",
        "ssr_color",
        "rt_reflection",
        "reflection_history_v3_prev_source_id",
        "reflection_history_v3_validity",
        "reflection_history_v3_rejection",
        "normal_roughness",
        "vb_gbuffer_emissive_metallic",
        "vb_gbuffer_material_ext2",
    ]:
        require(
            resource in reflection_resolver_inputs,
            errors,
            f"V3 reflection contract missing resolver input: {resource}",
        )
        require(resource in runtime_surface, errors, f"V3 runtime placeholder missing resolver input: {resource}")

    reflection_history_inputs = set(reflection_contract.get("required_history_inputs", []))
    for resource in [
        "reflection_radiance",
        "reflection_source_id",
        "reflection_temporal_delta",
        "reflection_history_v3_prev",
        "reflection_history_v3_prev_source_id",
        "depth",
        "normal_roughness",
        "velocity",
    ]:
        require(
            resource in reflection_history_inputs,
            errors,
            f"V3 reflection contract missing history input: {resource}",
        )
        require(resource in runtime_surface, errors, f"V3 runtime placeholder missing history input: {resource}")

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
        "renderGraphV3InventoryReady",
        '"render_graph_v3_inventory_ready"',
        "renderGraphV3PassCount",
        '"render_graph_v3_pass_count"',
        "renderGraphV3ExecutedPassCount",
        '"render_graph_v3_executed_pass_count"',
        "renderGraphV3ReadResourceCount",
        '"render_graph_v3_read_resource_count"',
        "renderGraphV3WriteResourceCount",
        '"render_graph_v3_write_resource_count"',
        "renderGraphV3MissingProducerCount",
        '"render_graph_v3_missing_producer_count"',
        "renderGraphV3PassNames",
        '"render_graph_v3_pass_names"',
        "renderGraphV3ReadResources",
        '"render_graph_v3_read_resources"',
        "renderGraphV3WrittenResources",
        '"render_graph_v3_written_resources"',
        "renderGraphV3MissingProducerResources",
        '"render_graph_v3_missing_producer_resources"',
        "RenderGraphV3Inventory",
        "v3_resource_inventory",
        "v3_resource_ownership",
        "candidate_path_debt",
        '"candidate_path_debt"',
        "totalMissingRequiredChannels",
        '"total_missing_required_channels"',
        "notReadyDomainCount",
        '"not_ready_domain_count"',
        '"material_missing_required_channels"',
        '"lighting_missing_required_channels"',
        '"environment_missing_required_channels"',
        '"reflection_missing_required_channels"',
        '"composite_missing_required_channels"',
        '"cinematic_post_missing_required_channels"',
        '"candidate_beauty_missing_required_channels"',
        '"candidate_beauty_predicate_count"',
        '"candidate_beauty_ready_predicate_count"',
        '"candidate_beauty_blockers"',
        '"legacy_rescue_resource_ready"',
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
        "sceneLocalEnvironmentPolicy",
        '"scene_local_environment_policy"',
        "sceneLocalVisibleBackgroundSource",
        '"scene_local_visible_background_source"',
        "sceneLocalReflectionBackgroundSource",
        '"scene_local_reflection_background_source"',
        "sceneLocalAmbientSource",
        '"scene_local_ambient_source"',
        "sceneLocalAtmosphereSource",
        '"scene_local_atmosphere_source"',
        "sceneLocalResourceContractReady",
        '"scene_local_resource_contract_ready"',
        "sceneLocalResourceContractId",
        '"scene_local_resource_contract_id"',
        "sceneLocalResourceContractFamily",
        '"scene_local_resource_contract_family"',
        "sceneLocalResourceContractStatus",
        '"scene_local_resource_contract_status"',
        "sceneLocalResourceContractUnsafeReason",
        '"scene_local_resource_contract_unsafe_reason"',
        "sceneLocalResourceContractVisibleExternalHdriAllowed",
        '"scene_local_resource_contract_visible_external_hdri_allowed"',
        "sceneLocalResourceContractExternalHdriSafe",
        '"scene_local_resource_contract_external_hdri_safe"',
        "sceneLocalResourceContractEnvironmentPolicyAllowed",
        '"scene_local_resource_contract_environment_policy_allowed"',
        "sceneLocalResourceContractReflectionPolicyAllowed",
        '"scene_local_resource_contract_reflection_policy_allowed"',
        "sceneLocalResourceContractReflectionSourceAllowed",
        '"scene_local_resource_contract_reflection_source_allowed"',
        "sceneLocalResourceContractProxyResourcesReady",
        '"scene_local_resource_contract_proxy_resources_ready"',
        "sceneLocalResourceContractPayloadResourcesReady",
        '"scene_local_resource_contract_payload_resources_ready"',
        "sceneLocalResourceContractRoleCount",
        '"scene_local_resource_contract_role_count"',
        "sceneLocalResourceContractReadyRoleCount",
        '"scene_local_resource_contract_ready_role_count"',
        "FullSceneShaderSceneLocalResourceContractFamily",
        "FullSceneShaderSceneLocalResourceContractAllowsVisibleExternalHdri",
        "FullSceneShaderSceneLocalResourceContractAllowsEnvironmentPolicy",
        "FullSceneShaderSceneLocalResourceContractAllowsReflectionPolicy",
        "FullSceneShaderSceneLocalResourceContractAllowsReflectionSource",
        "unauthorized_visible_external_hdri",
        "reflection_source_not_allowed",
        "sceneLocalEnvironmentSourceCount",
        '"scene_local_environment_source_count"',
        "sceneLocalEnvironmentConsumesSceneProfilePolicy",
        '"scene_local_environment_consumes_scene_profile_policy"',
        "sceneLocalEnvironmentProfileContractId",
        '"scene_local_environment_profile_contract_id"',
        "sceneLocalEnvironmentProfileEnclosureMode",
        '"scene_local_environment_profile_enclosure_mode"',
        "sceneLocalEnvironmentProfilePolicy",
        '"scene_local_environment_profile_policy"',
        "sceneLocalEnvironmentProfileReflectionPolicy",
        '"scene_local_environment_profile_reflection_policy"',
        "sceneLocalEnvironmentShaderProfile",
        '"scene_local_environment_shader_profile"',
        "sceneLocalEnvironmentShaderProfileMode",
        '"scene_local_environment_shader_profile_mode"',
        "sceneLocalEnvironmentLocalBackgroundStrength",
        '"scene_local_environment_local_background_strength"',
        "BuildSceneLocalEnvironmentV3ProfileParams",
        "BuildSceneLocalEnvironmentV3PayloadParams",
        "BuildSceneLocalEnvironmentV3PayloadBindingInfo",
        "AppendSceneLocalPayloadAliasPaths",
        "g_CinematicDofParams.z",
        "g_CinematicDofParams.w",
        "g_FogExtraParams.y",
        "g_FogExtraParams.z",
        "g_FogExtraParams.w",
        "g_SceneLocalPayloadAlbedo",
        "g_SceneLocalPayloadNormal",
        "sceneLocalTexturePayloadReady",
        '"scene_local_texture_payload_ready"',
        "sceneLocalTexturePayloadCount",
        '"scene_local_texture_payload_count"',
        "sceneLocalTextureSetId",
        '"scene_local_texture_set_id"',
        "sceneLocalTexturePayloadRichness",
        '"scene_local_texture_payload_richness"',
        "sceneLocalTexturePayloadProxyScore",
        '"scene_local_texture_payload_proxy_score"',
        "sceneLocalTexturePayloadShaderInfluence",
        '"scene_local_texture_payload_shader_influence"',
        "sceneLocalTexturePayloadResourceTableRequired",
        '"scene_local_texture_payload_resource_table_required"',
        "sceneLocalTexturePayloadResourceTableBindable",
        '"scene_local_texture_payload_resource_table_bindable"',
        "sceneLocalTexturePayloadBoundResourceCount",
        '"scene_local_texture_payload_bound_resource_count"',
        "sceneLocalTexturePayloadBindingSource",
        '"scene_local_texture_payload_binding_source"',
        "sceneLocalTexturePayloadFallbackReason",
        '"scene_local_texture_payload_fallback_reason"',
        "sceneLocalEnvironmentProxyResourceTableRequired",
        '"scene_local_environment_proxy_resource_table_required"',
        "sceneLocalEnvironmentProxyResourceTableBindable",
        '"scene_local_environment_proxy_resource_table_bindable"',
        "sceneLocalEnvironmentProxyBoundResourceCount",
        '"scene_local_environment_proxy_bound_resource_count"',
        "sceneLocalEnvironmentProxyBindingSource",
        '"scene_local_environment_proxy_binding_source"',
        "sceneLocalEnvironmentProxyFallbackReason",
        '"scene_local_environment_proxy_fallback_reason"',
        "sceneLocalEnvironmentProxyDerivationMethod",
        '"scene_local_environment_proxy_derivation_method"',
        "sceneLocalEnvironmentProxyRoomShell",
        '"scene_local_environment_proxy_room_shell"',
        "sceneLocalEnvironmentProxyRoomOcclusion",
        '"scene_local_environment_proxy_room_occlusion"',
        "sceneLocalEnvironmentProxyLightRig",
        '"scene_local_environment_proxy_light_rig"',
        "sceneLocalEnvironmentProxyLightAccentStrength",
        '"scene_local_environment_proxy_light_accent_strength"',
        "sceneLocalEnvironmentProxyResourceShape",
        '"scene_local_environment_proxy_resource_shape"',
        "sceneLocalEnvironmentProxyFilteredOutputCount",
        '"scene_local_environment_proxy_filtered_output_count"',
        "sceneLocalEnvironmentProxyMinFilterVariance",
        '"scene_local_environment_proxy_min_filter_variance"',
        "proxyResourceShape",
        "filteredOutputCount",
        "minFilterVariance",
        "cached_explicit_scene_local_proxy_triple",
        "cached_payload_derived_scene_local_proxy_triple",
        '"scene_local_proxy_resource_table_required"',
        '"scene_local_proxy_resource_table_bindable"',
        '"scene_local_proxy_bound_resource_count"',
        '"scene_local_proxy_binding_source"',
        '"scene_local_proxy_fallback_reason"',
        '"scene_local_proxy_derivation_method"',
        '"scene_local_proxy_room_shell"',
        '"scene_local_proxy_room_occlusion"',
        '"scene_local_proxy_light_rig"',
        '"scene_local_proxy_light_accent_strength"',
        '"scene_local_proxy_resource_shape"',
        '"scene_local_proxy_filtered_output_count"',
        '"scene_local_proxy_min_filter_variance"',
        '"v3_environment_payload.json"',
        "generate_scene_local_environment_proxies.py",
        "generated_contract_header",
        "SceneLocalProxyContracts.generated.h",
        "FindSceneLocalProxyContract",
        "profile_payload_material_room_light_v1",
        "filtered_directional_bc1_v1",
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
        "reflectionSourceSuppressionReady",
        '"reflection_source_suppression_ready"',
        "reflectionHistoryV3Ready",
        '"reflection_history_v3_ready"',
        "reflectionHistoryV3PrevReady",
        '"reflection_history_v3_prev_ready"',
        "reflectionHistoryV3PrevSourceIdReady",
        '"reflection_history_v3_prev_source_id_ready"',
        "reflectionHistoryV3ValidityReady",
        '"reflection_history_v3_validity_ready"',
        "reflectionHistoryV3RejectionReady",
        '"reflection_history_v3_rejection_ready"',
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
        "compositeContributionMapReady",
        '"composite_contribution_map_ready"',
        "compositeLegacyRescueUsageReady",
        '"composite_legacy_rescue_usage_ready"',
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
        "lighting_energy_budget",
        "shadow_source_attribution",
        "analyze_full_scene_shader_v3_shadow_attribution.py",
        "build_lighting_v3_shadow_promotion_matrix.py",
        "lighting_shadow_promotion_matrix.v1",
        "shadow_promotion_ready",
        "sun_shadow_loss_ratio",
        "local_shadow_loss_ratio",
        "CORTEX_LIGHT_SWEEP",
        "light_sweep",
        "FullSceneReflectionV3",
        "LocalReflectionRadiance",
        "reflection_radiance_owned",
        "reflection_confidence_owned",
        "reflection_source_id_owned",
        "reflection_rt_source_signal_owned",
        "reflection_source_suppression_owned",
        "reflection_history_v3_curr_owned",
        "reflection_history_v3_prev_owned",
        "reflection_history_v3_prev_source_id_owned",
        "reflection_history_v3_validity_owned",
        "reflection_history_v3_rejection_owned",
        "reflection_temporal_delta_scene_local_bound",
        "reflection_temporal_delta_history_bound",
        "analyze_reflection_v3_source_resolver.py",
        "build_reflection_v3_promotion_matrix.py",
        "reflection_promotion_matrix.v1",
        "reflection_promotion_ready",
        "DIAGNOSTIC_VIEWS",
        "source_switch_ratio",
        "active_source_switch_ratio",
        "ssr_signal_changes_under_motion",
        "temporal_delta_tracks_source_churn",
        "FamilyFilter",
        "runStressSceneOnly",
        "hysteresisMargin",
        "hysteresisHold",
        "SceneLocalEnvironmentV3",
        "ambient_lighting_owned",
        "reflection_background_owned",
        "FullSceneCompositeV3",
        "FullSceneCompositeV3Adapter",
        "candidate_hdr_scene_color",
        "vb_gbuffer_albedo",
        "material_albedo_input_read",
        "candidate_hdr_scene_color_owned_by_full_scene_composite_v3",
        "v3_lighting_and_reflection_inputs_read",
        "hdr_scene_color_owned",
        "energy_clamp_policy_owned_by_full_scene_composite_v3",
        "overbright_diagnostics_owned_by_full_scene_composite_v3",
        "composite_contribution_map_owned_by_full_scene_composite_v3",
        "legacy_rescue_usage_owned_by_full_scene_composite_v3",
        "energy_clamp_policy_owned",
        "overbright_diagnostics_owned",
        "CinematicPostV3",
        "CinematicPostV3Adapter",
        "candidate_ldr_cinematic_output",
        "candidate_ldr_cinematic_output_owned_by_cinematic_post_v3",
        "candidateBeautyPredicateCount",
        "candidateBeautyReadyPredicateCount",
        "candidateBeautyBlockers",
        '"candidate_beauty_composite_ready"',
        '"candidate_beauty_cinematic_post_ready"',
        '"candidate_beauty_ldr_output_ready"',
        '"candidate_beauty_reads_candidate_hdr"',
        '"candidate_beauty_legacy_bridge_rejected"',
        '"candidate_beauty_default_beauty_unchanged"',
        '"candidate_beauty_predicate_count"',
        '"candidate_beauty_ready_predicate_count"',
        '"candidate_beauty_blockers"',
        "legacy_hdr_bridge_rejected",
        "legacy_hdr_bridge_present",
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
        "v3_material_payload.json",
        "cortex.full_scene_shader_pipeline_v3.material_payload.v1",
        "preset_class_authored_default_roughness",
        "preset_class_authored_default_transmission",
        "unresolved_default_roughness_fallback",
        "unresolved_default_transmission_fallback",
        "diagnostic_scope",
        "material_payload_report_count",
        "full_pipeline_report_count",
        "promotion_decision.json",
        "promotion_decision.md",
        "v3_matrix_decision.json",
        "v3_matrix_decision.md",
        "ContinueOnPacketFailure",
        "packet_run_status.json",
        "packet_run_status.md",
        "run_full_scene_shader_pipeline_v3_promotion_suite.ps1",
        "cortex.full_scene_shader_pipeline_v3.promotion_suite.v1",
        "ViewProfile",
        "promotion_core",
        "Get-ViewPacks",
        "Resolve-ScenarioViewFilter",
        "view_pack",
        "view_count",
        "estimated_engine_runs",
        "reflection_core",
        "enclosed_core",
        "heavy_light_core",
        "reflection_probe_weight",
        "reflection_owner",
        "reflection_stability_policy",
        "reflection_static",
        "enclosed_mouse_jitter",
        "enclosed_camera_sweep",
        "heavy_light_sweep",
        "light_sweep",
        "cortex.full_scene_shader_pipeline_v3.matrix_packet_run_status.v1",
        "continued_after_failure",
        "packet_exit_code",
        "packet_runner_failed",
        "--packet-list-json",
        "candidate_predicate_summary",
        "candidate_beauty_predicates",
        "candidate_beauty_blocker_counts",
        "candidate_beauty_requested_blocker_counts",
        "candidate_beauty_review_ready",
        "candidate_beauty_review_gate",
        "candidate_beauty_review_blocker_counts",
        "predicate_ready_report_counts",
        "requested_blocker_counts",
        "default_beauty_promotion_blockers",
        "default_beauty_promotion_blocker_counts",
        "observed_promoted_families",
        "missing_promoted_families",
        "observed_promoted_motion_modes",
        "missing_promoted_motion_modes",
        "promoted_packet_count",
        "promoted_report_count",
        "material_quality_gate",
        "MATERIAL_QUALITY_THRESHOLDS",
        "min_named_material_ratio",
        "min_advanced_feature_ratio",
        "min_reflection_eligible_ratio",
        "material_quality_min_score",
        "material_quality_blocker_counts",
        "v3_composite_diagnostics.json",
        "composite_v3_diagnostics",
        "mean_explicit_legacy_rescue",
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
