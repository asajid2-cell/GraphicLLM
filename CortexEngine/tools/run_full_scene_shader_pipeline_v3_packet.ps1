param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v3_placeholder_packet",
    [string]$FamilyFilter = "gallery",
    [string]$StressSceneFilter = "rt_showcase:reflection_closeup",
    [string]$ViewFilter = "beauty,candidate_beauty_v3,candidate_hdr_scene_color,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,local_reflection_radiance,reflection_radiance,reflection_confidence,reflection_source_id,reflection_rejected_source_mask,reflection_temporal_delta,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta",
    [int]$SmokeFrames = 30,
    [int]$CaptureFrame = 15,
    [int]$CaptureSequenceCount = 1,
    [string]$StabilityMotionMode = "static",
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers,
    [switch]$StressSceneOnly,
    [switch]$NoStressScene
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$v2Packet = Join-Path $root "tools/run_full_scene_shader_pipeline_v2_packet.ps1"
$v3Analyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_placeholders.py"
$v3LightingMotionAnalyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_lighting_motion.py"
$v3PromotionDecision = Join-Path $root "tools/build_full_scene_shader_v3_promotion_decision.py"
$outputPath = Join-Path $root $OutputRoot
$signalOutput = Join-Path $outputPath "v3_signal.json"
$stabilityOutput = Join-Path $outputPath "v3_stability.json"
$lightingMotionOutput = Join-Path $outputPath "v3_lighting_motion.json"
$lightingMotionMarkdown = Join-Path $outputPath "v3_lighting_motion.md"
$promotionDecisionOutput = Join-Path $outputPath "promotion_decision.json"
$promotionDecisionMarkdown = Join-Path $outputPath "promotion_decision.md"
$previousFullSceneLightingV3 = $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-FamilyFilter", $FamilyFilter,
    "-ViewFilter", $ViewFilter,
    "-SmokeFrames", "$SmokeFrames",
    "-CaptureFrame", "$CaptureFrame",
    "-CaptureSequenceCount", "$CaptureSequenceCount",
    "-StabilityMotionMode", $StabilityMotionMode
)
if (-not $NoStressScene) {
    $packetArgs += @("-StressSceneFilter", $StressSceneFilter)
}

if ($NoBuild) {
    $packetArgs += "-NoBuild"
}
if ($SkipSceneAnalyzers) {
    $packetArgs += "-SkipSceneAnalyzers"
}
if ($StressSceneOnly) {
    $packetArgs += "-StressSceneOnly"
}
if ($NoStressScene) {
    $packetArgs += "-NoStressScene"
}

try {
    $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT = "1"

    & powershell -NoProfile -ExecutionPolicy Bypass -File $v2Packet @packetArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & python $v3Analyzer --input $outputPath --signal-output $signalOutput --stability-output $stabilityOutput --require-lighting-split-ready --require-lighting-split-draw-count 1 --require-lighting-signal-metrics
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($CaptureSequenceCount -ge 2) {
        $manifestPath = Join-Path $outputPath "manifest.json"
        & python $v3LightingMotionAnalyzer --manifest $manifestPath --output-json $lightingMotionOutput --output-md $lightingMotionMarkdown --min-sequence-count $CaptureSequenceCount
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    & python $v3PromotionDecision --packet-root $outputPath --output-json $promotionDecisionOutput --output-md $promotionDecisionMarkdown --allow-subset-review
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    if ($null -eq $previousFullSceneLightingV3) {
        Remove-Item Env:\CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT -ErrorAction SilentlyContinue
    } else {
        $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT = $previousFullSceneLightingV3
    }
}

Write-Host "Full Scene Shader Pipeline V3 placeholder packet evidence passed."
Write-Host "signal=$signalOutput"
Write-Host "stability=$stabilityOutput"
if ($CaptureSequenceCount -ge 2) {
    Write-Host "lighting_motion=$lightingMotionOutput"
}
Write-Host "promotion_decision=$promotionDecisionOutput"
