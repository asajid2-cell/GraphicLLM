$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$failures = New-Object System.Collections.Generic.List[string]

function Read-RepoFile([string]$relative) {
    $path = Join-Path $root $relative
    if (-not (Test-Path $path)) {
        $script:failures.Add("missing file: $relative") | Out-Null
        return ""
    }
    return Get-Content $path -Raw
}

function Assert-Contains([string]$label, [string]$text, [string]$needle) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -lt 0) {
        $script:failures.Add("$label missing '$needle'") | Out-Null
    }
}

function Assert-Matches([string]$label, [string]$text, [string]$pattern) {
    if (-not [regex]::IsMatch($text, $pattern)) {
        $script:failures.Add("$label does not match /$pattern/") | Out-Null
    }
}

function Assert-NotContains([string]$label, [string]$text, [string]$needle) {
    if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) {
        $script:failures.Add("$label unexpectedly contains '$needle'") | Out-Null
    }
}

$header = Read-RepoFile "src/Graphics/RendererSceneProfile.h"
$impl = Read-RepoFile "src/Graphics/RendererSceneProfile.cpp"
$scenes = Read-RepoFile "src/Core/Engine_Scenes.cpp"
$scenePresets = Read-RepoFile "src/Graphics/RendererControlApplier_ScenePresets.cpp"
$rendererHeader = Read-RepoFile "src/Graphics/Renderer.h"
$frameContract = Read-RepoFile "src/Graphics/FrameContract.h"
$frameContractJson = Read-RepoFile "src/Graphics/FrameContractJson.cpp"
$frameContractSnapshot = Read-RepoFile "src/Graphics/Renderer_FrameContractSnapshot.cpp"
$rendererSceneSnapshot = Read-RepoFile "src/Graphics/RendererSceneSnapshot.cpp"
$frameContractValidation = Read-RepoFile "src/Graphics/FrameContractValidation.cpp"
$materialModelHeader = Read-RepoFile "src/Graphics/MaterialModel.h"
$materialModelImpl = Read-RepoFile "src/Graphics/MaterialModel.cpp"
$materialPresetRegistry = Read-RepoFile "src/Graphics/MaterialPresetRegistry.cpp"
$surfaceClassificationHeader = Read-RepoFile "src/Graphics/SurfaceClassification.h"
$visibilityBufferMaterialKey = Read-RepoFile "src/Graphics/Renderer_VisibilityBufferMaterialKey.h"
$components = Read-RepoFile "src/Scene/Components.h"
$frameLightingConstants = Read-RepoFile "src/Graphics/Renderer_FrameLightingConstants.cpp"
$vbDeferred = Read-RepoFile "src/Graphics/Renderer_VisibilityBufferDeferredLighting.cpp"
$debugSettings = Read-RepoFile "src/Graphics/Renderer_DebugSettings.cpp"
$visibilityBufferHeader = Read-RepoFile "src/Graphics/VisibilityBuffer.h"
$surfaceClassification = Read-RepoFile "assets/shaders/SurfaceClassification.hlsli"
$materialResolve = Read-RepoFile "assets/shaders/MaterialResolve.hlsl"
$deferredLighting = Read-RepoFile "assets/shaders/DeferredLighting.hlsl"
$basicShader = Read-RepoFile "assets/shaders/Basic.hlsl"
$postProcess = Read-RepoFile "assets/shaders/PostProcess.hlsl"
$cmake = Read-RepoFile "CMakeLists.txt"
$packetTool = Read-RepoFile "tools/run_scene_local_cinematic_renderer_v1_packets.ps1"
$ownerAnalyzer = Read-RepoFile "tools/analyze_scene_local_reflection_owner.py"
$materialAnalyzer = Read-RepoFile "tools/analyze_scene_local_material_classes.py"
$stabilityAnalyzer = Read-RepoFile "tools/analyze_scene_local_packet_stability.py"
$visualQualityAnalyzer = Read-RepoFile "tools/analyze_scene_local_visual_quality.py"
$ledger = Read-RepoFile "docs/SCENE_LOCAL_CINEMATIC_RENDERER_V1.md"

foreach ($required in @(
    "SceneEnvironmentProfile",
    "SceneLightingProfile",
    "SceneLightingBalanceProfile",
    "SceneReflectionProfile",
    "SceneReflectionProbeProfile",
    "SceneLightFixtureProfile",
    "SceneTemporalProfile",
    "ScenePostProfile",
    "SceneMaterialProfile",
    "SceneWaterProfile",
    "SceneCinematicProfile",
    "BuildSceneLocalCinematicProfile",
    "BuildGalleryCinematicProfile",
    "ApplySceneCinematicProfile",
    "materialClassSetId",
    "policyId",
    "ownership"
)) {
    Assert-Contains "RendererSceneProfile.h" $header $required
}

foreach ($family in @(
    "home_kitchen_lantern",
    "home_office_evening",
    "basketball_gym_day",
    "neon_streamer_concert",
    "school_classroom_day",
    "red_light_room",
    "stadium_night_match"
)) {
    Assert-Contains "RendererSceneProfile.cpp" $impl $family
}

foreach ($profileId in @(
    "kitchen_morning_warm_scene_local_v1",
    "office_evening_scene_local_v1",
    "basketball_gym_bright_scene_local_v1",
    "neon_concert_auditorium_scene_local_v1",
    "gallery_public_cinematic_v1"
)) {
    Assert-Contains "RendererSceneProfile.cpp" $impl $profileId
}

Assert-Contains "CMakeLists.txt" $cmake "src/Graphics/RendererSceneProfile.cpp"
Assert-Contains "Engine_Scenes.cpp" $scenes "BuildSceneLocalCinematicProfile(sceneFamily)"
Assert-Contains "Engine_Scenes.cpp" $scenes "ApplySceneCinematicProfile(*renderer, profile)"
Assert-Contains "Engine_Scenes.cpp" $scenes "SetBackgroundPresentation(false, 0.0f, 1.0f)"
Assert-Contains "Engine_Scenes.cpp" $scenes "SetIBLEnabled(false)"
Assert-Contains "Engine.cpp" (Read-RepoFile "src/Core/Engine.cpp") "CORTEX_DISABLE_GPU_CULLING"
Assert-Contains "RendererControlApplier_ScenePresets.cpp" $scenePresets "BuildGalleryCinematicProfile(conservativeMode)"
Assert-Contains "RendererControlApplier_ScenePresets.cpp" $scenePresets "ApplySceneCinematicProfile(renderer, profile)"
Assert-Contains "RendererSceneProfile.cpp" $impl "p.visibilityBufferEnabled = true;"
Assert-NotContains "RendererControlApplier_ScenePresets.cpp" $scenePresets "CORTEX_FORCE_VISIBILITY_BUFFER"
Assert-NotContains "RendererControlApplier_ScenePresets.cpp" $scenePresets "forceVisibilityBuffer"
Assert-Contains "Renderer.h" $rendererHeader "SetSceneVisualContract"
Assert-Contains "Renderer.h" $rendererHeader "m_sceneVisualContract"
Assert-Contains "FrameContract.h" $frameContract "SceneVisualInfo"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "scene_visual_contract"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_class_set_id"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "contract.sceneVisual"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_enclosed_external_hdri_visible"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_material_class_set_missing"
Assert-Contains "RendererSceneProfile.cpp" $impl "FrameContract::SceneVisualInfo"
Assert-Contains "RendererSceneProfile.cpp" $impl "SetSceneVisualContract"
Assert-Contains "FrameContract.h" $frameContract "reflectionOwnerDebugViewMode"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "reflection_owner_debug_view_mode"
Assert-Contains "FrameContract.h" $frameContract "materialPolicyDebugViewMode"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_policy_debug_view_mode"
Assert-Contains "FrameContract.h" $frameContract "localReflectionProbeRigId"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "local_reflection_probe_rig_id"
Assert-Contains "FrameContract.h" $frameContract "profileLightFixtureCount"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "profile_light_fixture_count"
Assert-Contains "FrameContract.h" $frameContract "areaRectLightCount"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "area_rect_light_count"
Assert-Contains "FrameContract.h" $frameContract "semanticFixtureLightCount"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "semantic_fixture_light_count"
Assert-Contains "MaterialModel.h" $materialModelHeader "SceneMaterialClassId"
Assert-Contains "MaterialModel.h" $materialModelHeader "MaterialReflectionPreferenceId"
Assert-Contains "MaterialModel.h" $materialModelHeader "MaterialTemporalPolicyId"
Assert-Contains "MaterialModel.h" $materialModelHeader "MaterialPostSensitivityId"
Assert-Contains "MaterialModel.h" $materialModelHeader "MaterialClassPolicyEvidence"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "ResolveSceneMaterialClass"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "ApplyMaterialClassPolicy"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "ResolveMaterialClassPolicy"
Assert-Contains "MaterialModel.h" $materialModelHeader "albedoLuminanceClamped"
Assert-Contains "MaterialModel.h" $materialModelHeader "albedoChromaClamped"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "albedoLuminanceCeiling"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "albedoChromaCeiling"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "ApplyAlbedoTonePolicy"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "MaterialPresetRegistry::Canonicalize(model.presetName)"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "SceneMaterialClassId::CeramicTile"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "MaterialReflectionPreferenceId::PlanarProbe"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "MaterialTemporalPolicyId::EmissiveLocked"
Assert-Contains "MaterialModel.cpp" $materialModelImpl "MaterialPostSensitivityId::BloomEmitter"
Assert-Contains "MaterialPresetRegistry.cpp" $materialPresetRegistry "painted_wall"
Assert-Contains "MaterialPresetRegistry.cpp" $materialPresetRegistry "ceramic_tile"
Assert-Contains "MaterialPresetRegistry.cpp" $materialPresetRegistry "screen_panel"
Assert-Contains "MaterialPresetRegistry.cpp" $materialPresetRegistry "rubber"
Assert-Contains "MaterialPresetRegistry.cpp" $materialPresetRegistry '"paint", "painted_wall"'
Assert-Contains "MaterialPresetRegistry.cpp" $materialPresetRegistry '"matte_tile", "ceramic_tile"'
Assert-Contains "MaterialModel.cpp" $materialModelImpl "material.policyParams"
Assert-Contains "VisibilityBuffer.h" $visibilityBufferHeader "policyParams"
Assert-Contains "Renderer_VisibilityBufferMaterialKey.h" $visibilityBufferMaterialKey "sceneMaterialClassId"
Assert-Contains "Renderer_VisibilityBufferMaterialKey.h" $visibilityBufferMaterialKey "reflectionPreferenceId"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "policyParams"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "EncodeSceneMaterialClass"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "named scene material class"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "sceneMaterialClass = mat.policyParams.x"
Assert-NotContains "MaterialResolve.hlsl" $materialResolve "EncodeSceneMaterialClass(mat.policyParams.x)"
Assert-Contains "SurfaceClassification.h" $surfaceClassificationHeader "material.classPolicy.applied"
Assert-Contains "FrameContract.h" $frameContract "materialClassPolicyApplied"
Assert-Contains "FrameContract.h" $frameContract "materialPolicyNormalClamped"
Assert-Contains "FrameContract.h" $frameContract "materialPolicyAlbedoLuminanceClamped"
Assert-Contains "FrameContract.h" $frameContract "materialPolicyAlbedoChromaClamped"
Assert-Contains "FrameContract.h" $frameContract "sceneMaterialCeramicTile"
Assert-Contains "FrameContract.h" $frameContract "sceneMaterialScreenPanel"
Assert-Contains "FrameContract.h" $frameContract "materialReflectionPlanarProbe"
Assert-Contains "FrameContract.h" $frameContract "materialTemporalStableGlossy"
Assert-Contains "FrameContract.h" $frameContract "materialPostBloomEmitter"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_class_policy_applied"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_policy_normal_clamped"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_policy_albedo_luminance_clamped"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_policy_albedo_chroma_clamped"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "scene_material_ceramic_tile"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_reflection_planar_probe"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_temporal_stable_glossy"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_post_bloom_emitter"
Assert-Contains "RendererSceneSnapshot.cpp" $rendererSceneSnapshot "materialClassPolicyApplied"
Assert-Contains "RendererSceneSnapshot.cpp" $rendererSceneSnapshot "materialPolicyAlbedoLuminanceClamped"
Assert-Contains "RendererSceneSnapshot.cpp" $rendererSceneSnapshot "materialPolicyAlbedoChromaClamped"
Assert-Contains "RendererSceneSnapshot.cpp" $rendererSceneSnapshot "SceneMaterialClassId::CeramicTile"
Assert-Contains "RendererSceneSnapshot.cpp" $rendererSceneSnapshot "MaterialReflectionPreferenceId::RTReflection"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "material_policy_reflection_stable"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_material_class_count_mismatch"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "material_reflection_preference_count_mismatch"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "Scene::LightType::AreaRect"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "semanticClassId"
Assert-Contains "Components.h" $components "semanticClassId"
Assert-Contains "Renderer_FrameLightingConstants.cpp" $frameLightingConstants "semanticClassId"
Assert-Contains "Renderer_VisibilityBufferDeferredLighting.cpp" $vbDeferred "semanticClassId"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "DecodeFixtureClass"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "FixtureRoughnessForSpecular"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "DecodeSceneMaterialClass"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SceneMaterialPolicyDebugColor"
Assert-Contains "Basic.hlsl" $basicShader "DecodeFixtureClass"
Assert-Contains "Basic.hlsl" $basicShader "FixtureWrappedNdotL"
Assert-Contains "Renderer.h" $rendererHeader "SetLocalReflectionProbeRadiance"
Assert-Contains "RendererSceneProfile.cpp" $impl "SetLocalReflectionProbeRadiance"
Assert-Contains "RendererSceneProfile.cpp" $impl "localProbeRigId"
Assert-Contains "RendererSceneProfile.h" $header "lightFixtures"
Assert-Contains "RendererSceneProfile.h" $header "semanticClass"
Assert-Contains "RendererSceneProfile.h" $header "areaSize"
Assert-Contains "RendererSceneProfile.h" $header "materialLayerSetId"
Assert-Contains "RendererSceneProfile.h" $header "shadowPolicyId"
Assert-Contains "RendererSceneProfile.h" $header "qualitySetId"
Assert-Contains "RendererSceneProfile.h" $header "exposurePolicyId"
Assert-Contains "RendererSceneProfile.cpp" $impl "visualContract.materialLayerSetId"
Assert-Contains "RendererSceneProfile.cpp" $impl "visualContract.shadowPolicyId"
Assert-Contains "RendererSceneProfile.cpp" $impl "visualContract.exposurePolicyId"
Assert-Contains "RendererSceneProfile.cpp" $impl "visualContract.postQualitySetId"
Assert-Contains "FrameContract.h" $frameContract "materialLayerSetId"
Assert-Contains "FrameContract.h" $frameContract "shadowPolicyId"
Assert-Contains "FrameContract.h" $frameContract "exposurePolicyId"
Assert-Contains "FrameContract.h" $frameContract "postQualitySetId"
Assert-Contains "FrameContract.h" $frameContract "lightingBalancePolicyId"
Assert-Contains "FrameContract.h" $frameContract "lightingBalanceLocalFixtureScale"
Assert-Contains "FrameContract.h" $frameContract "stabilityPolicyActive"
Assert-Contains "FrameContract.h" $frameContract "materialMotionDamping"
Assert-Contains "FrameContract.h" $frameContract "lookPolicyActive"
Assert-Contains "FrameContract.h" $frameContract "blackToeLift"
Assert-Contains "FrameContract.h" $frameContract "highlightRolloff"
Assert-Contains "FrameContract.h" $frameContract "colorSeparation"
Assert-Contains "FrameContract.h" $frameContract "halationStrength"
Assert-Contains "FrameContract.h" $frameContract "exposurePolicyActive"
Assert-Contains "FrameContract.h" $frameContract "profileExposureTrim"
Assert-Contains "FrameContract.h" $frameContract "hdrShoulderStart"
Assert-Contains "FrameContract.h" $frameContract "hdrShoulderStrength"
Assert-Contains "FrameContract.h" $frameContract "postWhiteCompression"
Assert-Contains "Renderer.h" $rendererHeader "BuildCinematicStabilityParams"
Assert-Contains "Renderer.h" $rendererHeader "BuildCinematicLookParams"
Assert-Contains "Renderer.h" $rendererHeader "BuildCinematicExposureParams"
Assert-Contains "Renderer_FramePostConstants.cpp" (Read-RepoFile "src/Graphics/Renderer_FramePostConstants.cpp") "scene_local_cinematic_post_quality_v1"
Assert-Contains "Renderer_FramePostConstants.cpp" (Read-RepoFile "src/Graphics/Renderer_FramePostConstants.cpp") "cinematicStabilityParams"
Assert-Contains "Renderer_FramePostConstants.cpp" (Read-RepoFile "src/Graphics/Renderer_FramePostConstants.cpp") "cinematicLookParams"
Assert-Contains "Renderer_FramePostConstants.cpp" (Read-RepoFile "src/Graphics/Renderer_FramePostConstants.cpp") "cinematicExposureParams"
Assert-Contains "ShaderTypes.h" (Read-RepoFile "src/Graphics/ShaderTypes.h") "cinematicStabilityParams"
Assert-Contains "ShaderTypes.h" (Read-RepoFile "src/Graphics/ShaderTypes.h") "cinematicLookParams"
Assert-Contains "ShaderTypes.h" (Read-RepoFile "src/Graphics/ShaderTypes.h") "cinematicExposureParams"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_layer_set_id"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "shadow_policy_id"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "exposure_policy_id"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "post_quality_set_id"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "lighting_balance_policy_id"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "lighting_balance_local_fixture_scale"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "quality_set_id"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "stability_policy_active"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "material_motion_damping"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "shadow_softness_scale"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "look_policy_active"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "black_toe_lift"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "highlight_rolloff"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "color_separation"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "halation_strength"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "exposure_policy_active"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "profile_exposure_trim"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "hdr_shoulder_start"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "hdr_shoulder_strength"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "post_white_compression"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "contract.lighting.shadowPolicyId"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "lightingBalancePolicyId"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "lightingBalanceLocalFixtureScale"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "contract.cinematicPost.qualitySetId"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "BuildCinematicStabilityParams"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "BuildCinematicLookParams"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "BuildCinematicExposureParams"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_material_layer_set_missing"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_shadow_policy_missing"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_exposure_policy_missing"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_post_quality_set_missing"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "lighting_balance_policy_inactive"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "lighting_balance_policy_invalid"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_quality_set_mismatch"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_quality_set_has_no_active_shape"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_stability_policy_inactive"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_stability_policy_invalid"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_look_policy_inactive"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_look_policy_invalid"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_exposure_policy_inactive"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "cinematic_post_exposure_policy_invalid"
Assert-Contains "PostProcess.hlsl" $postProcess "g_CinematicStabilityParams"
Assert-Contains "PostProcess.hlsl" $postProcess "g_CinematicLookParams"
Assert-Contains "PostProcess.hlsl" $postProcess "g_CinematicExposureParams"
Assert-Contains "PostProcess.hlsl" $postProcess "highlightProtection"
Assert-Contains "PostProcess.hlsl" $postProcess "glossyMotionDamp"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplyCinematicToeLift"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplyProfileColorSeparation"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplyPostWhiteCompression"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplySceneLocalCinematicMidtoneCurve"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplySceneLocalCinematicChromaPolish"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplySceneLocalCinematicLookPolish"
Assert-Contains "PostProcess.hlsl" $postProcess "color = ApplySceneLocalCinematicLookPolish"
Assert-Contains "PostProcess.hlsl" $postProcess "profileExposureTrim"
Assert-Contains "PostProcess.hlsl" $postProcess "lookHalation"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "g_CinematicStabilityParams"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "stableShadowScale"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "LoadStableShadowDepth"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SampleStableShadowPCF"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SceneMaterialCinematicShadowReceiverSoftness"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ApplySceneMaterialCinematicShadowRadius"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ComputeShadow(worldPos, normal, sceneMaterialClass, surfaceClass, roughness, metallic)"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SceneMaterialCinematicDirectDiffuseTint"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SceneMaterialCinematicDirectSpecularGain"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ApplySceneMaterialCinematicDirectBRDF"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "float3 sunBrdf = ApplySceneMaterialCinematicDirectBRDF"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "float3 localBrdf = ApplySceneMaterialCinematicDirectBRDF"
Assert-Contains "RendererSceneProfile.cpp" $impl "AddPointFixture"
Assert-Contains "RendererSceneProfile.cpp" $impl "AddSpotFixture"
Assert-Contains "RendererSceneProfile.cpp" $impl "AddAreaFixture"
Assert-Contains "RendererSceneProfile.cpp" $impl "ApplyLightingBalance"
Assert-Contains "RendererSceneProfile.cpp" $impl "scene_local_lighting_balance_v1"
Assert-Contains "RendererSceneProfile.cpp" $impl "lightingBalanceLocalFixtureScale"
Assert-Contains "RendererSceneProfile.cpp" $impl "window_softbox"
Assert-Contains "RendererSceneProfile.cpp" $impl "high_bay_panel"
Assert-Contains "RendererSceneProfile.cpp" $impl "neon_strip_magenta"
Assert-Contains "RendererSceneProfile.cpp" $impl "gallery_key_softbox"
Assert-Contains "RendererSceneProfile.cpp" $impl "ProfileLight_Gallery_Softbox"
Assert-Contains "RendererSceneProfile.cpp" $impl "RTGallery_LocalProbe_Left"
Assert-Contains "RendererSceneProfile.cpp" $impl "ProfileLight_Kitchen_WindowFill"
Assert-Contains "RendererSceneProfile.cpp" $impl "ProfileLight_Gym_OverheadCenter"
Assert-Contains "RendererSceneProfile.cpp" $impl "ProfileLight_Concert_StageWashA"
Assert-Contains "Engine_Scenes.cpp" $scenes "AddSceneProfileLights"
Assert-Contains "Engine_Scenes.cpp" $scenes "AddModelAuthoredSeedLights"
Assert-Contains "Engine_Scenes.cpp" $scenes "sceneProfile.lightingBalance.localFixtureScale"
Assert-Contains "Engine_Scenes.cpp" $scenes "modelAuthoredFixtureScale"
Assert-Contains "Engine_Scenes.cpp" $scenes "28.0f * modelAuthoredFixtureScale"
Assert-Contains "Engine_Scenes.cpp" $scenes "AddSceneProfileReflectionProbes"
Assert-Contains "Engine_Scenes.cpp" $scenes "Scene::LightType::AreaRect"
Assert-Contains "Engine_Scenes.cpp" $scenes "light.areaSize"
Assert-Contains "Engine_Scenes.cpp" $scenes "profile_lights="
Assert-NotContains "Engine_Scenes.cpp" $scenes "AddModelAuthoredFamilyLights"
Assert-NotContains "Engine_Scenes.cpp" $scenes "AddModelAuthoredProfileLights"
Assert-Contains "Engine_Scenes.cpp" $scenes "RT Showcase profile assets"
Assert-Contains "Engine_Scenes.cpp" $scenes "reflection_probes="
Assert-Contains "VisibilityBuffer.h" $visibilityBufferHeader "localProbeParams"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "g_LocalProbeParams"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "localProbeRadianceEnabled"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ComputeSceneLocalProbeDiffuse"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ComputeSceneLocalProbeSpecular"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "localProbeTextureRadianceAllowed"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "authoredInteriorNoEnvironment"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SceneMaterialCinematicIndirectContactStrength"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "SceneMaterialCinematicIndirectBounceTint"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ApplySceneMaterialCinematicIndirectShaping"
Assert-Contains "DeferredLighting.hlsl" $deferredLighting "ambient = ApplySceneMaterialCinematicIndirectShaping"
Assert-Contains "FrameContract.h" $frameContract "localReflectionProbeRadianceEnabled"
Assert-Contains "FrameContractJson.cpp" $frameContractJson "local_reflection_probe_radiance_enabled"
Assert-Contains "Renderer_FrameContractSnapshot.cpp" $frameContractSnapshot "localProbeRadianceEnabled"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_local_probe_radiance_disabled"
Assert-Contains "FrameContractValidation.cpp" $frameContractValidation "scene_visual_local_probe_table_missing"
Assert-Contains "Renderer_DebugSettings.cpp" $debugSettings "kMaxDebugViewMode = 91u"
Assert-Contains "Renderer_DebugSettings.cpp" $debugSettings "ReflectionOwner"
Assert-Contains "Renderer_DebugSettings.cpp" $debugSettings "MaterialPolicy"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SurfaceNormalScaleCeiling"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SurfaceReflectionStabilityScale"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SurfacePolicyDebugColor"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SCENE_MATERIAL_CERAMIC_TILE"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "DecodeSceneMaterialClass"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialClassDebugColor"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialReflectionStabilityScale"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialPolicyDebugColor"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialCinematicDetailFloor"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialCinematicClearcoatBoost"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialCinematicWetnessBoost"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialCinematicEmissiveBoost"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialCinematicColorLayerStrength"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialCinematicColorLayerAxis"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "ApplySceneMaterialCinematicColorLayer"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialAlbedoLuminanceCeiling"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "SceneMaterialAlbedoChromaCeiling"
Assert-Contains "SurfaceClassification.hlsli" $surfaceClassification "ApplySceneMaterialAlbedoPolicy"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "SceneMaterialCinematicDetailFloor"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "SceneMaterialCinematicClearcoatBoost"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "SceneMaterialCinematicWetnessBoost"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "SceneMaterialCinematicEmissiveBoost"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "ApplySceneMaterialCinematicColorLayer"
Assert-Contains "MaterialResolve.hlsl" $materialResolve "ApplySceneMaterialAlbedoPolicy(albedo, sceneMaterialClass)"
Assert-Contains "PostProcess.hlsl" $postProcess "Reflection-owner debug"
Assert-Contains "PostProcess.hlsl" $postProcess "g_DebugMode.x == 46.0f"
Assert-Contains "PostProcess.hlsl" $postProcess "g_DebugMode.x == 47.0f"
Assert-Contains "PostProcess.hlsl" $postProcess "DecodeSceneMaterialClass"
Assert-Contains "PostProcess.hlsl" $postProcess "SceneMaterialReflectionStabilityScale"
Assert-Contains "PostProcess.hlsl" $postProcess "SceneMaterialCinematicReflectionTint"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplySceneMaterialCinematicReflectionGrade"
Assert-Contains "PostProcess.hlsl" $postProcess "CompositeSceneMaterialCinematicReflection"
Assert-Contains "PostProcess.hlsl" $postProcess "currentReflectionCompositeColor = CompositeSceneMaterialCinematicReflection"
Assert-Contains "PostProcess.hlsl" $postProcess "SceneMaterialCinematicContactAoStrength"
Assert-Contains "PostProcess.hlsl" $postProcess "SceneMaterialCinematicContactAoTint"
Assert-Contains "PostProcess.hlsl" $postProcess "ApplySceneMaterialCinematicContactAo"
Assert-Contains "PostProcess.hlsl" $postProcess "color = ApplySceneMaterialCinematicContactAo"

foreach ($parameter in @("KitchenSeed", "OfficeSeed", "GymSeed", "ConcertSeed", "RedRoomSeed", "StadiumSeed")) {
    Assert-Contains "packet tool" $packetTool $parameter
}
foreach ($viewName in @(
    "beauty",
    "roughness",
    "metallic",
    "surface_class",
    "surface_policy",
    "reflection_probe_weight",
    "reflection_owner",
    "shadow_factor",
    "direct_light",
    "ambient_ibl",
    "taa_blend"
)) {
    Assert-Contains "packet tool" $packetTool $viewName
}
foreach ($requiredPacketField in @("OnlyGallery", "Resolve-FamilySeed", "resolved_seeds", "scene_visual_contract", "frame_contract_warnings")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}
foreach ($requiredPacketField in @("red_room", "stadium", "red_light_room", "stadium_night_match")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}
foreach ($requiredPacketField in @("FamilyFilter", "ViewFilter", "Split-FilterSet", "Unknown ViewFilter entry", "Unknown FamilyFilter entry", "No families or stress scenes selected", "No views selected", "family_filter", "view_filter")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}
foreach ($requiredPacketField in @("SkipOwnerAnalysis", "analyze_scene_local_reflection_owner.py", "--write-manifest", "reflection_owner_analysis_stdout.txt")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}
foreach ($requiredPacketField in @("SkipMaterialAnalysis", "analyze_scene_local_material_classes.py", "material_class_analysis_stdout.txt")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}
foreach ($requiredPacketField in @("CaptureSequenceCount", "StabilityMotionMode", "mouse_jitter", "camera_sweep", "light_sweep", "MotionLookAmplitude", "MotionSideAmplitude", "MotionForwardAmplitude", "MotionLiftAmplitude", "CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE", "CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE", "CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE", "CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE", "CORTEX_LIGHT_SWEEP", "CORTEX_LIGHT_SWEEP_YAW_AMPLITUDE_DEGREES", "max-large-changed-pixel-ratio", "SkipStabilityAnalysis", "capture_sequence", "analyze_scene_local_packet_stability.py", "packet_stability_analysis_stdout.txt")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}
foreach ($requiredPacketField in @("SkipVisualQualityAnalysis", "VisualQualityFailOnReview", "analyze_scene_local_visual_quality.py", "visual_quality_analysis_stdout.txt")) {
    Assert-Contains "packet tool" $packetTool $requiredPacketField
}

foreach ($requiredAnalyzerField in @(
    "utf-8-sig",
    "owner_debug_view_mode",
    "debug_view",
    "visible_ibl",
    "scene_local_fallback",
    "max_enclosed_visible_ibl_ratio",
    "reflection_signal_ratio",
    "family_summary",
    "reflection_owner_analysis"
)) {
    Assert-Contains "reflection-owner analyzer" $ownerAnalyzer $requiredAnalyzerField
}
Assert-Contains "reflection-owner analyzer" $ownerAnalyzer "debug_view {row.get('debug_view')} != 46"
Assert-Contains "reflection-owner analyzer" $ownerAnalyzer "--write-manifest"

foreach ($requiredAnalyzerField in @(
    "utf-8-sig",
    "material_class_debug_view_mode",
    "named_policy_debug_view_mode",
    "debug_view",
    "surface_class",
    "surface_policy",
    "named_surface_ratio",
    "named_policy_ratio",
    "present_class_count",
    "present_policy_count",
    "min_named_policy_ratio",
    "min_present_policy_count",
    "max_named_policy_unknown_ratio",
    "release_gate",
    "family_summary",
    "named_policy_family_summary",
    "named_policy_aggregate",
    "material_class_analysis",
    "brushed_metal",
    "emissive"
)) {
    Assert-Contains "material-class analyzer" $materialAnalyzer $requiredAnalyzerField
}
Assert-Contains "material-class analyzer" $materialAnalyzer "debug_view {row.get('debug_view')} != 41"
Assert-Contains "material-class analyzer" $materialAnalyzer "--write-manifest"

foreach ($requiredAnalyzerField in @(
    "utf-8-sig",
    "packet_stability_analysis",
    "stability_motion_mode",
    "motion_warning_only_views",
    "motion_informational_views",
    "hard_gate_view",
    "informational_view",
    "motion_look_amplitude",
    "motion_side_amplitude",
    "motion_forward_amplitude",
    "capture_sequence",
    "mean_abs_luma_delta",
    "changed_pixel_ratio",
    "large_changed_pixel_ratio",
    "warn_mean_abs_luma_delta",
    "warn_motion_compensated_mean_abs_luma_delta",
    "alignment_max_dimension",
    "max_mean_abs_luma_delta",
    "max_motion_compensated_mean_abs_luma_delta",
    "motion_stable_core",
    "max_motion_stable_core_mean_abs_luma_delta",
    "motion_stable_core_limits_used",
    "edge_threshold",
    "min_stable_core_ratio",
    "diagnostic_signal_count",
    "diagnostic_signals",
    "hard_gate_warning_count",
    "diagnostic_warning_count",
    "hard_gate_aggregate",
    "summary"
)) {
    Assert-Contains "packet stability analyzer" $stabilityAnalyzer $requiredAnalyzerField
}
Assert-Contains "packet stability analyzer" $stabilityAnalyzer "--write-manifest"

foreach ($requiredAnalyzerField in @(
    "visual_quality_analysis",
    "REVIEW_REQUIRED",
    "high_quality_visuals_proven",
    "visual_quality_review_required",
    "completion_gate",
    "bright_ratio",
    "edge_density",
    "local_contrast",
    "saturation_mean",
    "named_surface_ratio",
    "named_policy_ratio",
    "--fail-on-review",
    "--write-manifest"
)) {
    Assert-Contains "visual-quality analyzer" $visualQualityAnalyzer $requiredAnalyzerField
}

Assert-Matches "ledger" $ledger "Completion Gate"
Assert-Matches "ledger" $ledger "SCR-V1-001"
Assert-Matches "ledger" $ledger "SCR-V1-005"

if ($failures.Count -gt 0) {
    Write-Host "Scene-local cinematic renderer V1 contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "Scene-local cinematic renderer V1 contract tests passed."
