param(
    [string]$OutputRoot = "build/captures/v3_lighting_shadow_motion_focus_packet",
    [string]$StressSceneFilter = "rt_showcase:reflection_closeup",
    [string]$ViewFilter = "beauty,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_lighting_energy_budget,v3_shadow_source_attribution",
    [int]$SmokeFrames = 24,
    [int]$CaptureFrame = 12,
    [int]$CaptureSequenceCount = 2,
    [ValidateSet("static", "mouse_jitter", "camera_sweep", "light_sweep")]
    [string]$StabilityMotionMode = "mouse_jitter",
    [int]$MotionFrames = 120,
    [double]$MotionLookAmplitude = 0.025,
    [double]$MotionLookCycles = 8.0,
    [double]$FixedDeltaTime = 0.008333333,
    [switch]$NoBuild,
    [switch]$RunSceneAnalyzers,
    [switch]$FailOnWarning
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$packetRunner = Join-Path $root "tools/run_scene_local_cinematic_renderer_v1_packets.ps1"
$motionAnalyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_lighting_motion.py"
$shadowAttributionAnalyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_shadow_attribution.py"
$reviewSheetBuilder = Join-Path $root "tools/build_full_scene_shader_v2_review_sheet.py"
$outputPath = Join-Path $root $OutputRoot
$manifestPath = Join-Path $outputPath "manifest.json"
$motionJson = Join-Path $outputPath "v3_lighting_shadow_motion_focus.json"
$motionMd = Join-Path $outputPath "v3_lighting_shadow_motion_focus.md"
$shadowAttributionJson = Join-Path $outputPath "v3_shadow_attribution.json"
$shadowAttributionMd = Join-Path $outputPath "v3_shadow_attribution.md"
$reviewSheet = Join-Path $outputPath "v3_lighting_shadow_motion_focus_sheet.png"
$reviewSheetJson = Join-Path $outputPath "v3_lighting_shadow_motion_focus_sheet.json"
$reviewSheetMd = Join-Path $outputPath "v3_lighting_shadow_motion_focus_sheet.md"

if (-not (Test-Path $packetRunner)) {
    throw "Scene-local packet runner missing: $packetRunner"
}
if (-not (Test-Path $motionAnalyzer)) {
    throw "V3 lighting motion analyzer missing: $motionAnalyzer"
}
if (-not (Test-Path $shadowAttributionAnalyzer)) {
    throw "V3 shadow attribution analyzer missing: $shadowAttributionAnalyzer"
}
if (-not (Test-Path $reviewSheetBuilder)) {
    throw "Review sheet builder missing: $reviewSheetBuilder"
}

$previousFullSceneLightingV3 = $env:CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT

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
    "--focus", "shadow"
)
if ($FailOnWarning) {
    $analyzerArgs += "--fail-on-warning"
}

$shadowAttributionArgs = @(
    "--manifest", $manifestPath,
    "--output-json", $shadowAttributionJson,
    "--output-md", $shadowAttributionMd
)
if ($FailOnWarning) {
    $shadowAttributionArgs += "--fail-on-warning"
}

$reviewViews = "beauty,v3_shadow_visibility,v3_shadow_loss,v3_lighting_energy_budget,v3_shadow_source_attribution,direct_light_shadow_loss,shadow_factor"
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

    & python $shadowAttributionAnalyzer @shadowAttributionArgs
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
}

Write-Host "LightingV3 focused shadow-motion packet passed."
Write-Host "manifest=$manifestPath"
Write-Host "motion=$motionJson"
Write-Host "shadow_attribution=$shadowAttributionJson"
Write-Host "review_sheet=$reviewSheet"
