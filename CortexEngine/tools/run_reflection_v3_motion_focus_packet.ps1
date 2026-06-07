param(
    [string]$OutputRoot = "build/captures/v3_reflection_motion_focus_packet",
    [string]$StressSceneFilter = "rt_showcase:reflection_closeup",
    [string]$ViewFilter = "beauty,reflection_radiance,reflection_confidence,reflection_source_id,reflection_rejected_source_mask,reflection_temporal_delta,reflection_ssr_source_signal,reflection_rt_source_signal,reflection_source_suppression,reflection_history_v3_curr,reflection_history_v3_prev,reflection_history_v3_validity,reflection_history_v3_rejection",
    [int]$SmokeFrames = 24,
    [int]$CaptureFrame = 12,
    [int]$CaptureSequenceCount = 2,
    [ValidateSet("static", "mouse_jitter", "camera_sweep")]
    [string]$StabilityMotionMode = "mouse_jitter",
    [int]$MotionFrames = 120,
    [double]$MotionLookAmplitude = 0.025,
    [double]$MotionLookCycles = 8.0,
    [double]$FixedDeltaTime = 0.008333333,
    [ValidateSet("auto", "local", "ssr", "rt", "environment", "none")]
    [string]$SourceOverride = "auto",
    [switch]$NoBuild,
    [switch]$RunSceneAnalyzers,
    [switch]$FailOnWarning
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$packetRunner = Join-Path $root "tools/run_scene_local_cinematic_renderer_v1_packets.ps1"
$motionAnalyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_lighting_motion.py"
$sourceResolverAnalyzer = Join-Path $root "tools/analyze_reflection_v3_source_resolver.py"
$reviewSheetBuilder = Join-Path $root "tools/build_full_scene_shader_v2_review_sheet.py"
$outputPath = Join-Path $root $OutputRoot
$manifestPath = Join-Path $outputPath "manifest.json"
$motionJson = Join-Path $outputPath "v3_reflection_motion_focus.json"
$motionMd = Join-Path $outputPath "v3_reflection_motion_focus.md"
$sourceResolverJson = Join-Path $outputPath "v3_reflection_source_resolver.json"
$sourceResolverMd = Join-Path $outputPath "v3_reflection_source_resolver.md"
$reviewSheet = Join-Path $outputPath "v3_reflection_motion_focus_sheet.png"
$reviewSheetJson = Join-Path $outputPath "v3_reflection_motion_focus_sheet.json"
$reviewSheetMd = Join-Path $outputPath "v3_reflection_motion_focus_sheet.md"

if (-not (Test-Path $packetRunner)) {
    throw "Scene-local packet runner missing: $packetRunner"
}
if (-not (Test-Path $motionAnalyzer)) {
    throw "V3 lighting/reflection motion analyzer missing: $motionAnalyzer"
}
if (-not (Test-Path $sourceResolverAnalyzer)) {
    throw "ReflectionV3 source resolver analyzer missing: $sourceResolverAnalyzer"
}
if (-not (Test-Path $reviewSheetBuilder)) {
    throw "Review sheet builder missing: $reviewSheetBuilder"
}

$previousFullSceneLightingV3 = $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT
$previousReflectionOverride = $env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-StressSceneOnly",
    "-StressSceneFilter", $StressSceneFilter,
    "-ViewFilter", $ViewFilter,
    "-SmokeFrames", [string]$SmokeFrames,
    "-CaptureFrame", [string]$CaptureFrame,
    "-CaptureSequenceCount", [string]$CaptureSequenceCount,
    "-StabilityMotionMode", $StabilityMotionMode,
    "-MotionFrames", [string]$MotionFrames,
    "-MotionLookAmplitude", [string]$MotionLookAmplitude,
    "-MotionLookCycles", [string]$MotionLookCycles,
    "-FixedDeltaTime", [string]$FixedDeltaTime
)

if ($NoBuild) {
    $packetArgs += "-NoBuild"
}
if (-not $RunSceneAnalyzers) {
    $packetArgs += @(
        "-SkipOwnerAnalysis",
        "-SkipMaterialAnalysis",
        "-SkipStabilityAnalysis",
        "-SkipVisualQualityAnalysis"
    )
}

$analyzerArgs = @(
    "--manifest", $manifestPath,
    "--output-json", $motionJson,
    "--output-md", $motionMd,
    "--min-sequence-count", [string]$CaptureSequenceCount,
    "--focus", "reflection"
)
if ($FailOnWarning) {
    $analyzerArgs += "--fail-on-warning"
}

$sourceResolverArgs = @(
    "--manifest", $manifestPath,
    "--output-json", $sourceResolverJson,
    "--output-md", $sourceResolverMd,
    "--min-sequence-count", [string]$CaptureSequenceCount
)
if ($FailOnWarning) {
    $sourceResolverArgs += "--fail-on-warning"
}

$reviewViews = "beauty,reflection_radiance,reflection_confidence,reflection_source_id,reflection_rejected_source_mask,reflection_temporal_delta,reflection_history_v3_validity,reflection_history_v3_rejection"
$reviewArgs = @(
    "--manifest", $manifestPath,
    "--output", $reviewSheet,
    "--summary-json", $reviewSheetJson,
    "--summary-md", $reviewSheetMd,
    "--views", $reviewViews,
    "--thumb-width", "300",
    "--thumb-height", "174"
)

try {
    $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT = "1"
    $env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE = $SourceOverride

    & powershell -NoProfile -ExecutionPolicy Bypass -File $packetRunner @packetArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    if (-not (Test-Path $manifestPath)) {
        throw "Focused packet manifest missing: $manifestPath"
    }

    & python $motionAnalyzer @analyzerArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & python $sourceResolverAnalyzer @sourceResolverArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & python $reviewSheetBuilder @reviewArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    if ($null -eq $previousFullSceneLightingV3) {
        Remove-Item Env:\CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT -ErrorAction SilentlyContinue
    } else {
        $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT = $previousFullSceneLightingV3
    }
    if ($null -eq $previousReflectionOverride) {
        Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
    } else {
        $env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE = $previousReflectionOverride
    }
}

Write-Host "ReflectionV3 focused motion packet passed."
Write-Host "manifest=$manifestPath"
Write-Host "motion=$motionJson"
Write-Host "source_resolver=$sourceResolverJson"
Write-Host "review_sheet=$reviewSheet"
