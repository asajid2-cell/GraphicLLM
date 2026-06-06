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
FRAME_CONTRACT_JSON_SOURCE_PATH = ROOT / "src" / "Graphics" / "FrameContractJson.cpp"
FULL_SCENE_SHADER_FRAME_CONTEXT_PATH = ROOT / "src" / "Graphics" / "FullSceneShaderFrameContext.h"
VISIBILITY_BUFFER_HEADER_PATH = ROOT / "src" / "Graphics" / "VisibilityBuffer.h"
MATERIAL_MODEL_HEADER_PATH = ROOT / "src" / "Graphics" / "MaterialModel.h"
MATERIAL_MODEL_SOURCE_PATH = ROOT / "src" / "Graphics" / "MaterialModel.cpp"
DEBUG_BLIT_VISIBILITY_SHADER_PATH = ROOT / "assets" / "shaders" / "DebugBlitVisibility.hlsl"
VISIBILITY_BUFFER_DEBUG_BLIT_SOURCE_PATH = ROOT / "src" / "Graphics" / "VisibilityBuffer_DebugBlit.cpp"
VISIBILITY_BUFFER_DEBUG_BLIT_PIPELINE_SOURCE_PATH = (
    ROOT / "src" / "Graphics" / "VisibilityBuffer_DebugBlitPipelines.cpp"
)
RENDERER_DEBUG_SETTINGS_SOURCE_PATH = ROOT / "src" / "Graphics" / "Renderer_DebugSettings.cpp"
MATERIAL_RESOLVE_SHADER_PATH = ROOT / "assets" / "shaders" / "MaterialResolve.hlsl"
SURFACE_CLASSIFICATION_SHADER_PATH = ROOT / "assets" / "shaders" / "SurfaceClassification.hlsli"
DEFERRED_LIGHTING_SHADER_PATH = ROOT / "assets" / "shaders" / "DeferredLighting.hlsl"
POST_PROCESS_SHADER_PATH = ROOT / "assets" / "shaders" / "PostProcess.hlsl"
RAYTRACED_REFLECTIONS_SHADER_PATH = ROOT / "assets" / "shaders" / "RaytracedReflections.hlsl"
BASIC_SHADER_PATH = ROOT / "assets" / "shaders" / "Basic.hlsl"
PROCEDURAL_SKY_SHADER_PATH = ROOT / "assets" / "shaders" / "ProceduralSky.hlsl"
WATER_SHADER_PATH = ROOT / "assets" / "shaders" / "Water.hlsl"
SHADER_TYPES_HEADER_PATH = ROOT / "src" / "Graphics" / "ShaderTypes.h"
FRAME_POST_CONSTANTS_SOURCE_PATH = ROOT / "src" / "Graphics" / "Renderer_FramePostConstants.cpp"
DEFERRED_LIGHTING_CONSTANTS_SOURCE_PATH = (
    ROOT / "src" / "Graphics" / "Renderer_VisibilityBufferDeferredLighting.cpp"
)
TEMPORAL_REJECTION_SHADER_PATH = ROOT / "assets" / "shaders" / "TemporalRejectionMask.hlsl"
TEMPORAL_REJECTION_SOURCE_PATH = ROOT / "src" / "Graphics" / "TemporalRejectionMask.cpp"
RUN_FULL_SCENE_SHADER_PACKET_PATH = ROOT / "tools" / "run_full_scene_shader_pipeline_v2_packet.ps1"
DEBUG_VIEW_METRICS_TOOL_PATH = ROOT / "tools" / "analyze_full_scene_shader_debug_view_metrics.py"
REFLECTION_CANDIDATE_SIGNAL_TOOL_PATH = (
    ROOT / "tools" / "analyze_full_scene_shader_reflection_candidate_signal.py"
)
LIGHTING_SIGNAL_TOOL_PATH = ROOT / "tools" / "analyze_full_scene_shader_lighting_signal.py"
SEQUENCE_STABILITY_TOOL_PATH = ROOT / "tools" / "analyze_full_scene_shader_sequence_stability.py"


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

    common_evidence_fields = frame_contract.get("common_evidence_fields")
    expected_common_evidence_fields = {
        "promotion_state",
        "domain_ready",
        "facade_owner",
        "fallback_owner",
        "failure_reason",
    }
    if not isinstance(common_evidence_fields, list):
        errors.append("common_evidence_fields must be a list")
    elif set(common_evidence_fields) != expected_common_evidence_fields:
        errors.append(
            "common_evidence_fields must be exactly: "
            + ", ".join(sorted(expected_common_evidence_fields))
        )

    return errors


def validate_runtime_source_surface() -> list[str]:
    errors: list[str] = []
    if not FRAME_CONTRACT_JSON_SOURCE_PATH.exists():
        return [f"missing runtime frame contract JSON source: {FRAME_CONTRACT_JSON_SOURCE_PATH}"]
    if not FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.exists():
        return [f"missing runtime V2 facade: {FULL_SCENE_SHADER_FRAME_CONTEXT_PATH}"]
    if not VISIBILITY_BUFFER_HEADER_PATH.exists():
        return [f"missing visibility buffer runtime surface: {VISIBILITY_BUFFER_HEADER_PATH}"]
    if not MATERIAL_MODEL_HEADER_PATH.exists():
        return [f"missing material model runtime surface: {MATERIAL_MODEL_HEADER_PATH}"]
    if not MATERIAL_MODEL_SOURCE_PATH.exists():
        return [f"missing material model runtime source: {MATERIAL_MODEL_SOURCE_PATH}"]

    source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")
    facade_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    visibility_source = VISIBILITY_BUFFER_HEADER_PATH.read_text(encoding="utf-8")
    material_header_source = MATERIAL_MODEL_HEADER_PATH.read_text(encoding="utf-8")
    material_model_source = MATERIAL_MODEL_SOURCE_PATH.read_text(encoding="utf-8")
    runtime_surface = "\n".join(
        [
            source,
            facade_source,
            visibility_source,
            material_header_source,
            material_model_source,
        ]
    )
    frame_contract = load_json(FRAME_REPORT_CONTRACT_PATH)
    report_key = frame_contract.get("report_key", "")
    if f'"{report_key}"' not in runtime_surface:
        errors.append(f"runtime JSON source does not emit {report_key}")
    if "runtime_placeholder_v1_fallback" not in runtime_surface:
        errors.append("runtime JSON source must label the V2 report as a V1 fallback placeholder")
    if "FullSceneShaderPipelineV2ToJson" not in source:
        errors.append("runtime JSON source must use a named V2 report builder")
    if "#include \"Graphics/FullSceneShaderFrameContext.h\"" not in source:
        errors.append("runtime JSON source must consume FullSceneShaderFrameContext")
    if "BuildFullSceneShaderFrameContext(contract)" not in source:
        errors.append("runtime JSON source must build the V2 frame context facade")
    if "domainEvidence(context." not in source:
        errors.append("runtime JSON source must emit per-domain facade evidence")

    facade_tokens = [
        "struct FullSceneShaderFrameContext",
        "struct FullSceneShaderDomainEvidence",
        "FullSceneShaderPromotionState",
        "FullSceneMaterialModelEvidence",
        "FullSceneGBufferEvidence",
        "BuildFullSceneShaderFrameContext",
        "BuildFullSceneMaterialModelEvidence(",
        "contract.draws.visibilityBufferMaterials",
        "context.materialPolicyChannelReady",
        "BuildFullSceneGBufferEvidence",
        "visibilityPayloadChannelReady",
        "visibilityPayloadProducerReady",
        "instanceIdentityTableReady",
        "instanceMaterialLookupReady",
        "stableInstanceIdAvailable",
        "visibilityBufferInstanceCount",
        "visibilityBufferMaterialCount",
        "invalidStableInstanceIdCount",
        "FullSceneShaderHasResource(contract, \"visibility_buffer\")",
        "Stable per-pixel material-id channel is not promoted",
        "Stable per-pixel object-id channel is not promoted",
        "Debug-view producer ownership is not reported",
        "fallbackOwner = \"v1_fallback\"",
        "FullSceneShaderPromotionState::Instrumented",
        "FullSceneShaderPromotionState::Planned",
    ]
    for token in facade_tokens:
        if token not in facade_source:
            errors.append(f"runtime V2 facade missing required token: {token}")

    material_table_tokens = [
        "shaderMaterialTableReady",
        "shaderMaterialPolicyRowsReady",
        "gbufferPolicyChannelBackedByMaterialTable",
        '"shader_material_table_ready"',
        '"shader_material_policy_rows_ready"',
        '"gbuffer_policy_channel_backed_by_material_table"',
    ]
    for token in material_table_tokens:
        if token not in runtime_surface:
            errors.append(f"runtime material table surface missing required token: {token}")

    identity_debug_tokens = [
        "enum class DebugBlitVisibilityMode",
        "MaterialId = 1",
        "StableObjectId = 2",
        "MaterialFamily = 3",
        "ReflectionPolicy = 4",
        "TemporalPolicy = 5",
        "PostSensitivity = 6",
    ]
    for token in identity_debug_tokens:
        if token not in runtime_surface:
            errors.append(f"runtime identity debug surface missing required token: {token}")

    for field in frame_contract.get("common_evidence_fields", []):
        if f'"{field}"' not in source:
            errors.append(f"runtime JSON source missing common evidence field {field}")

    for token in [
        '"visibility_buffer"',
        '"visibility_buffer_materials"',
        '"visibility_buffer_invalid_stable_ids"',
    ]:
        if token not in runtime_surface:
            errors.append(f"runtime JSON source missing identity ownership token {token}")

    for section in frame_contract.get("required_sections", []):
        if not isinstance(section, dict):
            continue
        section_path = section.get("report_path", "")
        section_name = section_path.rsplit(".", 1)[-1]
        if section_name and f'"{section_name}"' not in source:
            errors.append(f"runtime JSON source missing V2 section {section_name}")
        for field in section.get("readiness_fields", []):
            if f'"{field}"' not in source:
                errors.append(
                    f"runtime JSON source missing V2 readiness field {section_name}.{field}"
                )

    return errors


def require_source_token(errors: list[str], source: str, token: str, label: str) -> None:
    if token not in source:
        errors.append(f"{label} missing required token: {token}")


def validate_runtime_material_policy_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        VISIBILITY_BUFFER_HEADER_PATH,
        MATERIAL_MODEL_HEADER_PATH,
        MATERIAL_MODEL_SOURCE_PATH,
        MATERIAL_RESOLVE_SHADER_PATH,
        SURFACE_CLASSIFICATION_SHADER_PATH,
        DEFERRED_LIGHTING_SHADER_PATH,
        POST_PROCESS_SHADER_PATH,
        DEBUG_BLIT_VISIBILITY_SHADER_PATH,
        VISIBILITY_BUFFER_DEBUG_BLIT_SOURCE_PATH,
        VISIBILITY_BUFFER_DEBUG_BLIT_PIPELINE_SOURCE_PATH,
        RENDERER_DEBUG_SETTINGS_SOURCE_PATH,
        SHADER_TYPES_HEADER_PATH,
        FRAME_POST_CONSTANTS_SOURCE_PATH,
        DEFERRED_LIGHTING_CONSTANTS_SOURCE_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing runtime material policy source: {path}")
    if errors:
        return errors

    vb_header = VISIBILITY_BUFFER_HEADER_PATH.read_text(encoding="utf-8")
    model_header = MATERIAL_MODEL_HEADER_PATH.read_text(encoding="utf-8")
    model_source = MATERIAL_MODEL_SOURCE_PATH.read_text(encoding="utf-8")
    material_shader = MATERIAL_RESOLVE_SHADER_PATH.read_text(encoding="utf-8")
    surface_shader = SURFACE_CLASSIFICATION_SHADER_PATH.read_text(encoding="utf-8")
    deferred_shader = DEFERRED_LIGHTING_SHADER_PATH.read_text(encoding="utf-8")
    post_shader = POST_PROCESS_SHADER_PATH.read_text(encoding="utf-8")
    debug_blit_shader = DEBUG_BLIT_VISIBILITY_SHADER_PATH.read_text(encoding="utf-8")
    debug_blit_source = VISIBILITY_BUFFER_DEBUG_BLIT_SOURCE_PATH.read_text(encoding="utf-8")
    debug_blit_pipeline_source = VISIBILITY_BUFFER_DEBUG_BLIT_PIPELINE_SOURCE_PATH.read_text(
        encoding="utf-8"
    )
    renderer_debug_source = RENDERER_DEBUG_SETTINGS_SOURCE_PATH.read_text(encoding="utf-8")
    shader_types = SHADER_TYPES_HEADER_PATH.read_text(encoding="utf-8")
    frame_post_source = FRAME_POST_CONSTANTS_SOURCE_PATH.read_text(encoding="utf-8")
    deferred_lighting_source = DEFERRED_LIGHTING_CONSTANTS_SOURCE_PATH.read_text(encoding="utf-8")

    require_source_token(
        errors,
        vb_header,
        "alignas(16) glm::uvec4 policyParams",
        "VisibilityBuffer VBMaterialConstants",
    )
    require_source_token(
        errors,
        model_header,
        "struct MaterialClassPolicyEvidence",
        "MaterialModel policy evidence",
    )
    require_source_token(
        errors,
        model_header,
        "struct FullSceneMaterialModelEvidence",
        "MaterialModel full-scene material evidence",
    )
    require_source_token(
        errors,
        model_header,
        "BuildFullSceneMaterialModelEvidence",
        "MaterialModel full-scene evidence builder",
    )
    require_source_token(
        errors,
        model_header,
        "runtimePolicyBridgeReady",
        "MaterialModel full-scene policy bridge evidence",
    )
    require_source_token(
        errors,
        model_header,
        "MaterialReflectionPreferenceId",
        "MaterialModel policy enum",
    )
    require_source_token(
        errors,
        model_source,
        "ApplyMaterialClassPolicy(model)",
        "MaterialResolver policy application",
    )
    require_source_token(
        errors,
        model_source,
        "FullSceneMaterialModelEvidence BuildFullSceneMaterialModelEvidence",
        "MaterialResolver full-scene evidence implementation",
    )
    require_source_token(
        errors,
        model_source,
        "materials.materialClassPolicyApplied == materials.sampled",
        "MaterialResolver full-scene policy coverage gate",
    )
    require_source_token(
        errors,
        model_source,
        "shaderMaterialTableRowCount",
        "MaterialResolver shader-facing material table row evidence",
    )
    require_source_token(
        errors,
        model_source,
        "gbufferPolicyChannelReady",
        "MaterialResolver GBuffer policy backing gate",
    )
    require_source_token(
        errors,
        model_source,
        "Shader-facing FullSceneMaterialTable is not populated with complete policy rows",
        "MaterialResolver shader material table failure reason",
    )
    require_source_token(
        errors,
        model_source,
        "GBuffer material policy channel is not backed by the shader material table",
        "MaterialResolver GBuffer policy backing failure reason",
    )
    require_source_token(
        errors,
        model_source,
        "materials.descriptorTablesMissingAfterPrepare == 0",
        "MaterialResolver texture descriptor evidence gate",
    )
    require_source_token(
        errors,
        model_source,
        "material.policyParams = glm::uvec4",
        "MaterialResolver VB policy upload",
    )
    require_source_token(
        errors,
        material_shader,
        "uint4 policyParams",
        "MaterialResolve VBMaterialConstants",
    )
    require_source_token(
        errors,
        material_shader,
        "sceneMaterialClass = mat.policyParams.x",
        "MaterialResolve policy unpack",
    )
    require_source_token(
        errors,
        material_shader,
        "EncodeSceneMaterialClass(sceneMaterialClass)",
        "MaterialResolve GBuffer policy write",
    )
    require_source_token(
        errors,
        material_shader,
        "named scene material class",
        "MaterialResolve MaterialExt2 contract comment",
    )
    require_source_token(
        errors,
        surface_shader,
        "SCENE_MATERIAL_PAINTED_WALL",
        "SurfaceClassification scene material vocabulary",
    )
    require_source_token(
        errors,
        surface_shader,
        "float EncodeSceneMaterialClass",
        "SurfaceClassification scene material encoder",
    )
    require_source_token(
        errors,
        surface_shader,
        "float SurfaceReflectionStabilityScale",
        "SurfaceClassification reflection stability policy",
    )
    require_source_token(
        errors,
        deferred_shader,
        "uint sceneMaterialClass = DecodeSceneMaterialClass(materialExt2.a)",
        "DeferredLighting scene material policy read",
    )
    require_source_token(
        errors,
        deferred_shader,
        "SceneMaterialSubsurfaceWrap(sceneMaterialClass)",
        "DeferredLighting subsurface policy replacement",
    )
    require_source_token(
        errors,
        deferred_shader,
        "SceneMaterialPolicyDebugColor(sceneMaterialClass",
        "DeferredLighting material policy debug view",
    )
    require_source_token(
        errors,
        post_shader,
        "uint   sceneMaterialClass = DecodeSceneMaterialClass(materialExt2.a)",
        "PostProcess scene material policy read",
    )
    require_source_token(
        errors,
        post_shader,
        "CompositeSceneMaterialCinematicReflection",
        "PostProcess scene material reflection shaping",
    )
    require_source_token(
        errors,
        post_shader,
        "ApplySceneMaterialCinematicContactAo",
        "PostProcess scene material contact AO",
    )
    require_source_token(
        errors,
        shader_types,
        "glm::vec4 cinematicStabilityParams",
        "ShaderTypes post constants",
    )
    require_source_token(
        errors,
        frame_post_source,
        "frameData.cinematicStabilityParams",
        "Frame post constant upload",
    )
    require_source_token(
        errors,
        deferred_lighting_source,
        "deferredParams.cinematicStabilityParams",
        "Deferred lighting constant upload",
    )
    require_source_token(
        errors,
        debug_blit_shader,
        "StructuredBuffer<VBMaterialConstants> g_Materials : register(t2)",
        "DebugBlitVisibility material table binding",
    )
    require_source_token(
        errors,
        debug_blit_shader,
        "id = material.policyParams.x",
        "DebugBlitVisibility material family policy column",
    )
    require_source_token(
        errors,
        debug_blit_shader,
        "id = material.policyParams.y",
        "DebugBlitVisibility reflection policy column",
    )
    require_source_token(
        errors,
        debug_blit_shader,
        "id = material.policyParams.z",
        "DebugBlitVisibility temporal policy column",
    )
    require_source_token(
        errors,
        debug_blit_shader,
        "id = material.policyParams.w",
        "DebugBlitVisibility post sensitivity policy column",
    )
    require_source_token(
        errors,
        debug_blit_source,
        "Visibility material-policy debug blit requires a populated material table",
        "VisibilityBuffer material-policy debug readiness guard",
    )
    require_source_token(
        errors,
        debug_blit_source,
        "SetGraphicsRootShaderResourceView(4, materialAddress)",
        "VisibilityBuffer material-policy root SRV binding",
    )
    require_source_token(
        errors,
        debug_blit_pipeline_source,
        "params[4].Descriptor.ShaderRegister = 2",
        "VisibilityBuffer material-policy root signature SRV",
    )
    require_source_token(
        errors,
        renderer_debug_source,
        "constexpr uint32_t kMaxDebugViewMode = 77u",
        "Renderer debug mode range",
    )
    for token in [
        "VB_MaterialFamilyPolicy",
        "VB_ReflectionPolicy",
        "VB_TemporalPolicy",
        "VB_PostSensitivity",
    ]:
        require_source_token(errors, renderer_debug_source, token, "Renderer material-policy debug label")

    return errors


def validate_runtime_lighting_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        FULL_SCENE_SHADER_FRAME_CONTEXT_PATH,
        FRAME_CONTRACT_JSON_SOURCE_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing runtime lighting evidence source: {path}")
    if errors:
        return errors

    facade_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    json_source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")

    for token in [
        "struct FullSceneLightingRigEvidence",
        "BuildFullSceneLightingRigEvidence",
        "semanticLightRolesAvailable",
        "rigPolicyIdsConsistent",
        "lightingBalancePolicyReady",
        "localFixtureContractReady",
        "shadowedLightContractReady",
        "shaderLightArrayReady",
        "semanticLightPayloadReady",
        "semanticLightShaderPayloadReady",
        "semanticLightPayloadOwner",
        "semanticLightPayloadChannels",
        "semanticLightPayloadCount",
        "areaLightPayloadReady",
        "clusteredLightListReady",
        "directLightPassReady",
        "directLightShadowOutputReady",
        "directLightDebugViewReady",
        "directLightUnshadowedDebugViewReady",
        "directLightShadowLossDebugViewReady",
        "lightingV2ShadowOutputReady",
        "lightingV2PassOwner",
        "lightingV2OutputResource",
        "FullSceneShaderPassReadsResource(contract, \"VBDeferredLighting\", \"shadow_map\")",
        "exposurePolicyReady",
        "missingLightingContractCount",
        "Scene-local semantic light-rig ownership is ready",
    ]:
        require_source_token(errors, facade_source, token, "FullScene lighting rig evidence")

    for token in [
        '"semantic_light_roles_available"',
        '"rig_policy_ids_consistent"',
        '"lighting_balance_policy_ready"',
        '"local_fixture_contract_ready"',
        '"shadowed_light_contract_ready"',
        '"shader_light_array_ready"',
        '"semantic_light_payload_ready"',
        '"semantic_light_shader_payload_ready"',
        '"semantic_light_payload_owner"',
        '"semantic_light_payload_channels"',
        '"semantic_light_payload_count"',
        '"area_light_payload_ready"',
        '"clustered_light_list_ready"',
        '"direct_light_pass_ready"',
        '"direct_light_shadow_output_ready"',
        '"direct_light_debug_view_ready"',
        '"direct_light_unshadowed_debug_view_ready"',
        '"direct_light_shadow_loss_debug_view_ready"',
        '"lighting_v2_shadow_output_ready"',
        '"lighting_v2_pass_owner"',
        '"lighting_v2_output_resource"',
        '"exposure_policy_ready"',
        '"point_light_count"',
        '"spot_light_count"',
        '"two_sided_area_light_count"',
        '"semantic_fixture_light_count"',
        '"stage_fixture_light_count"',
        '"shadow_casting_light_count"',
        '"missing_lighting_contract_count"',
        '"rig_source"',
    ]:
        require_source_token(errors, json_source, token, "FullScene lighting frame-report JSON")

    deferred_lighting = DEFERRED_LIGHTING_SHADER_PATH.read_text(encoding="utf-8")
    renderer_debug = RENDERER_DEBUG_SETTINGS_SOURCE_PATH.read_text(encoding="utf-8")
    packet_script = RUN_FULL_SCENE_SHADER_PACKET_PATH.read_text(encoding="utf-8")
    scene_packet_script = (ROOT / "tools" / "run_scene_local_cinematic_renderer_v1_packets.ps1").read_text(
        encoding="utf-8"
    )

    for token in [
        "directLightUnshadowed",
        "localDirectUnshadowed",
        "g_ReflectionProbeParams.z == 54u",
        "g_ReflectionProbeParams.z == 55u",
        "directLightUnshadowed - directLight",
    ]:
        require_source_token(errors, deferred_lighting, token, "DeferredLighting V2 direct-light comparison")

    deferred_lighting_constants = DEFERRED_LIGHTING_CONSTANTS_SOURCE_PATH.read_text(encoding="utf-8")
    for token in [
        "semanticClassId",
        "direction_cosInner",
        "isAreaRect ? areaHalfSize.x : semanticClassId",
    ]:
        require_source_token(errors, deferred_lighting_constants, token, "Deferred lighting semantic payload upload")

    for token in [
        "constexpr uint32_t kMaxDebugViewMode = 77u",
        "VB_DeferredDirectLightUnshadowed",
        "VB_DeferredDirectLightShadowLoss",
    ]:
        require_source_token(errors, renderer_debug, token, "Renderer V2 direct-light debug labels")

    for token in [
        "direct_light_unshadowed",
        "direct_light_shadow_loss",
    ]:
        require_source_token(errors, packet_script, token, "V2 packet runner direct-light comparison views")
        require_source_token(errors, scene_packet_script, token, "Scene-local packet direct-light comparison views")

    return errors


def validate_runtime_temporal_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        TEMPORAL_REJECTION_SHADER_PATH,
        TEMPORAL_REJECTION_SOURCE_PATH,
        POST_PROCESS_SHADER_PATH,
        SHADER_TYPES_HEADER_PATH,
        FULL_SCENE_SHADER_FRAME_CONTEXT_PATH,
        FRAME_CONTRACT_JSON_SOURCE_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing runtime temporal source: {path}")
    if errors:
        return errors

    temporal_shader = TEMPORAL_REJECTION_SHADER_PATH.read_text(encoding="utf-8")
    temporal_source = TEMPORAL_REJECTION_SOURCE_PATH.read_text(encoding="utf-8")
    post_shader = POST_PROCESS_SHADER_PATH.read_text(encoding="utf-8")
    shader_types = SHADER_TYPES_HEADER_PATH.read_text(encoding="utf-8")
    facade_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    json_source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")

    require_source_token(
        errors,
        temporal_shader,
        "cbuffer FrameConstants : register(b1)",
        "TemporalRejectionMask frame constants",
    )
    require_source_token(
        errors,
        temporal_shader,
        "float4   g_TAAParams",
        "TemporalRejectionMask TAA params",
    )
    require_source_token(
        errors,
        temporal_shader,
        "historyUv = uv + velocity + g_TAAParams.xy",
        "TemporalRejectionMask jitter reprojection",
    )
    require_source_token(
        errors,
        temporal_shader,
        "max(speedPixels - 4.0f, 0.0f) / 56.0f",
        "TemporalRejectionMask camera-sweep motion taper",
    )
    require_source_token(
        errors,
        post_shader,
        "historyUV = saturate(uv + vel + g_TAAParams.xy)",
        "PostProcess TAA resolve jitter reprojection",
    )
    require_source_token(
        errors,
        shader_types,
        "glm::vec4 taaParams",
        "ShaderTypes TAA params",
    )
    require_source_token(
        errors,
        temporal_source,
        "SetComputeRootConstantBufferView(kFrameConstantsRoot, desc.frameConstants)",
        "TemporalRejectionMask frame constants binding",
    )

    for token in [
        "struct FullSceneTemporalEvidence",
        "BuildFullSceneTemporalEvidence",
        "visibilityBufferMotionReady",
        "previousTransformHistoryReady",
        "temporalMaskStatsReady",
        "taaHistoryVelocityReprojectionReady",
        "taaHistoryDisocclusionRejectionReady",
        "missingTemporalContractCount",
        "Material-aware temporal stability is ready",
    ]:
        require_source_token(errors, facade_source, token, "FullScene temporal evidence")

    for token in [
        '"visibility_buffer_motion_ready"',
        '"previous_transform_history_ready"',
        '"temporal_mask_ready"',
        '"temporal_mask_stats_ready"',
        '"temporal_mask_latency_ready"',
        '"taa_history_ready"',
        '"taa_history_velocity_reprojection_ready"',
        '"taa_history_disocclusion_rejection_ready"',
        '"temporal_mask_accepted_ratio"',
        '"temporal_mask_disocclusion_ratio"',
        '"temporal_mask_high_motion_ratio"',
        '"temporal_mask_out_of_bounds_ratio"',
        '"temporal_mask_readback_latency_frames"',
        '"taa_history_age_frames"',
        '"taa_history_accumulation_alpha"',
        '"missing_temporal_contract_count"',
    ]:
        require_source_token(errors, json_source, token, "FullScene temporal frame-report JSON")

    return errors


def validate_runtime_reflection_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        RAYTRACED_REFLECTIONS_SHADER_PATH,
        POST_PROCESS_SHADER_PATH,
        RENDERER_DEBUG_SETTINGS_SOURCE_PATH,
        SHADER_TYPES_HEADER_PATH,
        FRAME_POST_CONSTANTS_SOURCE_PATH,
        FULL_SCENE_SHADER_FRAME_CONTEXT_PATH,
        FRAME_CONTRACT_JSON_SOURCE_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing runtime reflection source: {path}")
    if errors:
        return errors

    rt_reflections_shader = RAYTRACED_REFLECTIONS_SHADER_PATH.read_text(encoding="utf-8")
    post_process_shader = POST_PROCESS_SHADER_PATH.read_text(encoding="utf-8")
    renderer_debug_source = RENDERER_DEBUG_SETTINGS_SOURCE_PATH.read_text(encoding="utf-8")
    packet_script = RUN_FULL_SCENE_SHADER_PACKET_PATH.read_text(encoding="utf-8")
    scene_packet_script = (ROOT / "tools" / "run_scene_local_cinematic_renderer_v1_packets.ps1").read_text(
        encoding="utf-8"
    )
    shader_types = SHADER_TYPES_HEADER_PATH.read_text(encoding="utf-8")
    frame_post_source = FRAME_POST_CONSTANTS_SOURCE_PATH.read_text(encoding="utf-8")
    facade_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    json_source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")

    require_source_token(
        errors,
        shader_types,
        "glm::vec4 ambientColor",
        "ShaderTypes ambient/background blur params",
    )
    require_source_token(
        errors,
        shader_types,
        "glm::vec4 envParams",
        "ShaderTypes environment params",
    )
    require_source_token(
        errors,
        frame_post_source,
        "m_environmentState.backgroundExposure",
        "Frame environment background exposure upload",
    )
    require_source_token(
        errors,
        rt_reflections_shader,
        "background exposure to zero, so misses must not synthesize a sky lobe",
        "RaytracedReflections enclosed miss policy comment",
    )
    require_source_token(
        errors,
        rt_reflections_shader,
        "return (g_EnvParams.w <= 0.001f) ? float3(0.0f, 0.0f, 0.0f) : g_AmbientColor.rgb",
        "RaytracedReflections zero-exposure miss fallback",
    )
    require_source_token(
        errors,
        rt_reflections_shader,
        "reflectionSafeMipFloor = saturate(g_AmbientColor.w) * kApproxEnvMaxMip",
        "RaytracedReflections background-blur mip floor",
    )
    require_source_token(
        errors,
        rt_reflections_shader,
        "authoredInteriorNoEnvironment",
        "RaytracedReflections authored interior ambient policy",
    )
    for token in [
        "struct FullSceneReflectionOwnershipEvidence",
        "BuildFullSceneReflectionOwnershipEvidence",
        "externalIblVisibilityAuthorized",
        "localProbeContractReady",
        "rtMissEnvironmentPolicyReady",
        "enclosedMissFallbackSafe",
        "reflectionSourceContractReady",
        "missingReflectionContractCount",
        "Scene-local reflection/probe ownership is ready",
    ]:
        require_source_token(errors, facade_source, token, "FullScene reflection ownership evidence")

    for token in [
        '"skipped_probe_count"',
        '"local_probe_rig_declared"',
        '"local_probe_table_ready"',
        '"local_probe_radiance_ready"',
        '"local_probe_intensity_ready"',
        '"local_probe_contract_ready"',
        '"local_probe_diffuse_intensity"',
        '"local_probe_specular_intensity"',
        '"enclosed_miss_fallback_safe"',
        '"reflection_source_contract_ready"',
        '"external_ibl_visibility_authorized"',
        '"reflection_owner_known"',
        '"missing_reflection_contract_count"',
    ]:
        require_source_token(errors, json_source, token, "FullScene reflection frame-report JSON")

    for token in [
        "iblReflectionPotential",
        "g_DebugMode.x == 56.0f",
        "g_DebugMode.x == 57.0f",
        "g_DebugMode.x == 60.0f",
        "g_DebugMode.x == 61.0f",
        "Reflection-source resolver weights",
        "Reflection source authority",
        "Local reflection radiance buffer proof view",
        "Reflection stability policy",
        "reflectionStabilityScale",
        "stableSSRConfidence",
        "candidateReflectionCompositeColor",
        "g_DebugMode.x == 58.0f",
        "g_DebugMode.x == 59.0f",
    ]:
        require_source_token(errors, post_process_shader, token, "PostProcess reflection resolver debug views")

    for token in [
        "PostReflectionSourceWeights",
        "PostReflectionSourceAuthority",
        "LocalReflectionRadiance",
        "PostReflectionStabilityPolicy",
        "PostReflectionResolverV2Candidate",
        "PostReflectionResolverV2CandidateDelta",
    ]:
        require_source_token(errors, renderer_debug_source, token, "Renderer reflection resolver debug labels")

    for token in [
        "reflection_source_weights",
        "reflection_source_authority",
        "local_reflection_radiance",
        "reflection_stability_policy",
        "reflection_resolver_candidate",
        "reflection_resolver_candidate_delta",
    ]:
        require_source_token(errors, json.dumps(load_json(FRAME_REPORT_CONTRACT_PATH)), token, "V2 reflection debug-view contract")
        require_source_token(errors, packet_script, token, "V2 packet runner reflection resolver views")
        require_source_token(errors, scene_packet_script, token, "Scene-local packet reflection resolver views")

    return errors


def validate_runtime_shadow_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        FULL_SCENE_SHADER_FRAME_CONTEXT_PATH,
        FRAME_CONTRACT_JSON_SOURCE_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing runtime shadow evidence source: {path}")
    if errors:
        return errors

    facade_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    json_source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")

    for token in [
        "struct FullSceneShadowContactEvidence",
        "BuildFullSceneShadowContactEvidence",
        "shadowMapProducerReady",
        "shadowCasterOwnershipReady",
        "shadowBiasPolicyReady",
        "shadowFilterPolicyReady",
        "rtShadowMaskReady",
        "rtShadowHistoryReady",
        "missingShadowContractCount",
        "Scene-local shadow/contact stability is ready",
    ]:
        require_source_token(errors, facade_source, token, "FullScene shadow/contact evidence")

    for token in [
        '"shadow_map_ready"',
        '"shadow_map_producer_ready"',
        '"cascade_policy_ready"',
        '"shadow_bias_policy_ready"',
        '"shadow_filter_policy_ready"',
        '"shadow_caster_ownership_ready"',
        '"rt_shadow_mask_ready"',
        '"rt_shadow_history_ready"',
        '"shadow_casting_light_count"',
        '"shadow_pcf_radius"',
        '"missing_shadow_contract_count"',
    ]:
        require_source_token(errors, json_source, token, "FullScene shadow frame-report JSON")

    return errors


def validate_runtime_scene_local_environment_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        BASIC_SHADER_PATH,
        PROCEDURAL_SKY_SHADER_PATH,
        WATER_SHADER_PATH,
        SHADER_TYPES_HEADER_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing scene-local environment shader source: {path}")
    if errors:
        return errors

    basic_shader = BASIC_SHADER_PATH.read_text(encoding="utf-8")
    sky_shader = PROCEDURAL_SKY_SHADER_PATH.read_text(encoding="utf-8")
    water_shader = WATER_SHADER_PATH.read_text(encoding="utf-8")
    shader_types = SHADER_TYPES_HEADER_PATH.read_text(encoding="utf-8")

    require_source_token(
        errors,
        shader_types,
        "glm::vec4 ambientColor",
        "ShaderTypes ambient/background blur params",
    )
    require_source_token(
        errors,
        basic_shader,
        "EnvReflectionFootprintMip",
        "Basic forward IBL footprint filtering",
    )
    require_source_token(
        errors,
        basic_shader,
        "reflectionSafeMipFloor = saturate(g_AmbientColor.w) * maxMip",
        "Basic forward background-blur reflection floor",
    )
    require_source_token(
        errors,
        basic_shader,
        "StableIblMipRoughness",
        "Basic forward stable IBL roughness policy",
    )
    require_source_token(
        errors,
        sky_shader,
        "local outdoor scenes rather than generic HDRI replacement",
        "ProceduralSky scene-local atmosphere contract",
    )
    require_source_token(
        errors,
        sky_shader,
        "CloudMask(viewDir, horizon, up)",
        "ProceduralSky local cloud shaping",
    )
    require_source_token(
        errors,
        water_shader,
        "LiquidReflectionPalette",
        "Water scene-local reflection palette",
    )
    require_source_token(
        errors,
        water_shader,
        "max(g_AmbientColor.rgb * 1.35f, shallowProfile)",
        "Water ambient-owned sky tint",
    )
    require_source_token(
        errors,
        water_shader,
        "LiquidSpecularGlint",
        "Water cinematic specular glint",
    )

    return errors


def validate_v2_packet_runner_surface() -> list[str]:
    errors: list[str] = []
    if not RUN_FULL_SCENE_SHADER_PACKET_PATH.exists():
        return [f"missing V2 packet runner: {RUN_FULL_SCENE_SHADER_PACKET_PATH}"]

    packet_script = RUN_FULL_SCENE_SHADER_PACKET_PATH.read_text(encoding="utf-8")
    metrics_tool = DEBUG_VIEW_METRICS_TOOL_PATH.read_text(encoding="utf-8")
    reflection_signal_tool = REFLECTION_CANDIDATE_SIGNAL_TOOL_PATH.read_text(encoding="utf-8")
    lighting_signal_tool = LIGHTING_SIGNAL_TOOL_PATH.read_text(encoding="utf-8")
    sequence_stability_tool = SEQUENCE_STABILITY_TOOL_PATH.read_text(encoding="utf-8")
    required_tokens = [
        "run_scene_local_cinematic_renderer_v1_packets.ps1",
        "analyze_full_scene_shader_debug_view_metrics.py",
        "analyze_full_scene_shader_reflection_candidate_signal.py",
        "analyze_full_scene_shader_lighting_signal.py",
        "analyze_full_scene_shader_sequence_stability.py",
        "debug_view_metrics.json",
        "debug_view_metrics.md",
        "reflection_candidate_signal.json",
        "reflection_candidate_signal.md",
        "lighting_signal.json",
        "lighting_signal.md",
        "sequence_stability.json",
        "sequence_stability.md",
        "full_scene_shader_pipeline_v2",
        "frame_contract.full_scene_shader_pipeline_v2",
        "promotion_state",
        "domain_ready",
        "facade_owner",
        "fallback_owner",
        "failure_reason",
        "v2_frame_report_evidence_summary.json",
        "v2_frame_report_evidence_summary.md",
        "check_full_scene_shader_pipeline_v2_frame_report.py",
        "surface_policy",
        "material_family",
        "reflection_policy",
        "temporal_policy",
        "post_sensitivity",
        "material_id",
        "object_id",
        "reflection_owner",
        "reflection_source_weights",
        "reflection_source_authority",
        "reflection_stability_policy",
        "reflection_resolver_candidate",
        "reflection_resolver_candidate_delta",
        "shadow_factor",
        "direct_light",
        "direct_light_unshadowed",
        "direct_light_shadow_loss",
        "ambient_ibl",
        "taa_blend",
    ]
    for token in required_tokens:
        if token not in packet_script:
            errors.append(f"V2 packet runner missing required token: {token}")

    for token in [
        "cortex.full_scene_shader_pipeline_v2.debug_view_metrics.v1",
        "read_bmp_rgb",
        "mean_rgb",
        "nonblack_ratio",
        "hot_pixel_ratio",
    ]:
        if token not in metrics_tool:
            errors.append(f"V2 debug-view metrics tool missing required token: {token}")

    for token in [
        "cortex.full_scene_shader_pipeline_v2.reflection_candidate_signal.v1",
        "reflection_source_weights",
        "reflection_resolver_candidate",
        "reflection_resolver_candidate_delta",
        "candidate_delta_family_count",
        "no_reflection_source_signal",
    ]:
        if token not in reflection_signal_tool:
            errors.append(f"V2 reflection candidate signal tool missing required token: {token}")

    for token in [
        "cortex.full_scene_shader_pipeline_v2.lighting_signal.v1",
        "direct_light",
        "direct_light_unshadowed",
        "direct_light_shadow_loss",
        "direct_signal_family_count",
        "no_direct_light_signal",
    ]:
        if token not in lighting_signal_tool:
            errors.append(f"V2 lighting signal tool missing required token: {token}")

    for token in [
        "cortex.full_scene_shader_pipeline_v2.sequence_stability.v1",
        "capture_sequence",
        "mean_abs_luma_delta",
        "candidate_over_beauty_ratio",
        "reflection_resolver_candidate",
        "reflection_resolver_candidate_delta",
    ]:
        if token not in sequence_stability_tool:
            errors.append(f"V2 sequence stability tool missing required token: {token}")

    return errors


def validate_frame_report(frame_report_path: Path, strict: bool) -> list[str]:
    errors: list[str] = []
    frame_contract = load_json(FRAME_REPORT_CONTRACT_PATH)
    frame_report = load_json(frame_report_path)
    report_root = frame_report.get("frame_contract", frame_report)
    if not isinstance(report_root, dict):
        errors.append("frame report must be an object or contain a frame_contract object")
        return errors

    report_key = frame_contract["report_key"]
    v2_report = report_root.get(report_key)
    if not isinstance(v2_report, dict):
        message = f"frame report missing top-level {report_key}"
        if strict:
            errors.append(message)
        else:
            print(f"WARN: {message}")
            return errors

    for section in frame_contract["required_sections"]:
        section_id = section["id"]
        section_data = get_path(report_root, section["report_path"])
        if not isinstance(section_data, dict):
            errors.append(f"missing section {section_id} at {section['report_path']}")
            continue
        for field in section["readiness_fields"]:
            if field not in section_data:
                errors.append(f"{section_id} missing readiness field {field}")
        evidence = section_data.get("evidence")
        if not isinstance(evidence, dict):
            errors.append(f"{section_id} missing common evidence object")
            continue
        for field in frame_contract.get("common_evidence_fields", []):
            if field not in evidence:
                errors.append(f"{section_id} evidence missing field {field}")

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
    errors.extend(validate_runtime_source_surface())
    errors.extend(validate_runtime_material_policy_surface())
    errors.extend(validate_runtime_lighting_surface())
    errors.extend(validate_runtime_temporal_surface())
    errors.extend(validate_runtime_reflection_surface())
    errors.extend(validate_runtime_shadow_surface())
    errors.extend(validate_runtime_scene_local_environment_surface())
    errors.extend(validate_v2_packet_runner_surface())
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
