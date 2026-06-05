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

    source = FRAME_CONTRACT_JSON_SOURCE_PATH.read_text(encoding="utf-8")
    facade_source = FULL_SCENE_SHADER_FRAME_CONTEXT_PATH.read_text(encoding="utf-8")
    runtime_surface = source + "\n" + facade_source
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
        "BuildFullSceneShaderFrameContext",
        "fallbackOwner = \"v1_fallback\"",
        "FullSceneShaderPromotionState::Instrumented",
        "FullSceneShaderPromotionState::Planned",
    ]
    for token in facade_tokens:
        if token not in facade_source:
            errors.append(f"runtime V2 facade missing required token: {token}")

    for field in frame_contract.get("common_evidence_fields", []):
        if f'"{field}"' not in source:
            errors.append(f"runtime JSON source missing common evidence field {field}")

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

    return errors


def validate_runtime_temporal_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        TEMPORAL_REJECTION_SHADER_PATH,
        TEMPORAL_REJECTION_SOURCE_PATH,
        POST_PROCESS_SHADER_PATH,
        SHADER_TYPES_HEADER_PATH,
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

    return errors


def validate_runtime_reflection_surface() -> list[str]:
    errors: list[str] = []
    required_paths = [
        RAYTRACED_REFLECTIONS_SHADER_PATH,
        SHADER_TYPES_HEADER_PATH,
        FRAME_POST_CONSTANTS_SOURCE_PATH,
    ]
    for path in required_paths:
        if not path.exists():
            errors.append(f"missing runtime reflection source: {path}")
    if errors:
        return errors

    rt_reflections_shader = RAYTRACED_REFLECTIONS_SHADER_PATH.read_text(encoding="utf-8")
    shader_types = SHADER_TYPES_HEADER_PATH.read_text(encoding="utf-8")
    frame_post_source = FRAME_POST_CONSTANTS_SOURCE_PATH.read_text(encoding="utf-8")

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
    errors.extend(validate_runtime_temporal_surface())
    errors.extend(validate_runtime_reflection_surface())
    errors.extend(validate_runtime_scene_local_environment_surface())
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
