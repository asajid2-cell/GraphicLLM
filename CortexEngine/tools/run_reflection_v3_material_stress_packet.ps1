param(
    [string]$OutputRoot = "build/captures/reflection_v3_material_stress_packet",
    [string]$StressSceneFilter = "material_lab:metal_closeup,material_lab:glass_emissive,glass_water_courtyard:water_closeup,desert_relic_gallery:stone_metal_closeup,rain_glass_pavilion:puddle_chrome",
    [string]$ViewFilter = "beauty,candidate_beauty_v3,roughness,metallic,surface_class,material_family,reflection_radiance,reflection_source_id,reflection_rejected_source_mask,reflection_ssr_source_signal,reflection_rt_source_signal,reflection_source_suppression,reflection_history_v3_rejection",
    [int]$SmokeFrames = 30,
    [int]$CaptureFrame = 15,
    [int]$CaptureSequenceCount = 2,
    [ValidateSet("static", "mouse_jitter", "camera_sweep")]
    [string]$StabilityMotionMode = "mouse_jitter",
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers,
    [switch]$RunPromotionDecision
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$v2Packet = Join-Path $root "tools/run_full_scene_shader_pipeline_v2_packet.ps1"
$v3Analyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_placeholders.py"
$stressAnalyzer = Join-Path $root "tools/analyze_reflection_v3_material_stress.py"
$promotionDecision = Join-Path $root "tools/build_full_scene_shader_v3_promotion_decision.py"
$outputPath = Join-Path $root $OutputRoot
$signalOutput = Join-Path $outputPath "v3_signal.json"
$stabilityOutput = Join-Path $outputPath "v3_stability.json"
$stressOutput = Join-Path $outputPath "reflection_v3_material_stress.json"
$stressMarkdown = Join-Path $outputPath "reflection_v3_material_stress.md"
$promotionDecisionOutput = Join-Path $outputPath "promotion_decision.json"
$promotionDecisionMarkdown = Join-Path $outputPath "promotion_decision.md"
$previousFullSceneLightingV3 = $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-ViewFilter", $ViewFilter,
    "-StressSceneFilter", $StressSceneFilter,
    "-StressSceneOnly",
    "-SmokeFrames", "$SmokeFrames",
    "-CaptureFrame", "$CaptureFrame",
    "-CaptureSequenceCount", "$CaptureSequenceCount",
    "-StabilityMotionMode", $StabilityMotionMode
)
if ($NoBuild) {
    $packetArgs += "-NoBuild"
}
if ($SkipSceneAnalyzers) {
    $packetArgs += "-SkipSceneAnalyzers"
}

try {
    $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT = "1"

    & powershell -NoProfile -ExecutionPolicy Bypass -File $v2Packet @packetArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & python $v3Analyzer --input $outputPath --signal-output $signalOutput --stability-output $stabilityOutput --require-lighting-split-ready --require-lighting-split-draw-count 1
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $manifestPath = Join-Path $outputPath "manifest.json"
    & python $stressAnalyzer --manifest $manifestPath --output-json $stressOutput --output-md $stressMarkdown
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($RunPromotionDecision) {
        & python $promotionDecision --packet-root $outputPath --output-json $promotionDecisionOutput --output-md $promotionDecisionMarkdown --allow-subset-review
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
} finally {
    if ($null -eq $previousFullSceneLightingV3) {
        Remove-Item Env:\CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT -ErrorAction SilentlyContinue
    } else {
        $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT = $previousFullSceneLightingV3
    }
}

Write-Host "ReflectionV3 material stress packet passed."
Write-Host "signal=$signalOutput"
Write-Host "stability=$stabilityOutput"
Write-Host "material_stress=$stressOutput"
if ($RunPromotionDecision) {
    Write-Host "promotion_decision=$promotionDecisionOutput"
}
