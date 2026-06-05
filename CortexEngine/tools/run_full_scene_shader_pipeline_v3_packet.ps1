param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v3_placeholder_packet",
    [string]$FamilyFilter = "gallery",
    [string]$StressSceneFilter = "rt_showcase:reflection_closeup",
    [string]$ViewFilter = "beauty,local_reflection_radiance,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta",
    [int]$SmokeFrames = 30,
    [int]$CaptureFrame = 15,
    [int]$CaptureSequenceCount = 1,
    [string]$StabilityMotionMode = "static",
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers,
    [switch]$StressSceneOnly
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$v2Packet = Join-Path $root "tools/run_full_scene_shader_pipeline_v2_packet.ps1"
$v3Analyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_placeholders.py"
$outputPath = Join-Path $root $OutputRoot
$signalOutput = Join-Path $outputPath "v3_signal.json"
$stabilityOutput = Join-Path $outputPath "v3_stability.json"
$previousFullSceneLightingV3 = $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-FamilyFilter", $FamilyFilter,
    "-StressSceneFilter", $StressSceneFilter,
    "-ViewFilter", $ViewFilter,
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
if ($StressSceneOnly) {
    $packetArgs += "-StressSceneOnly"
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
