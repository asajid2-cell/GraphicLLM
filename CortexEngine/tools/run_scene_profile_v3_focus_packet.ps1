param(
    [string]$OutputRoot = "build/captures/scene_profile_v3_focus_packet",
    [string]$FamilyFilter = "gallery,kitchen,concert",
    [string]$ViewFilter = "beauty,reflection_owner,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity",
    [int]$SmokeFrames = 14,
    [int]$CaptureFrame = 7,
    [int]$CaptureSequenceCount = 1,
    [ValidateSet("static", "mouse_jitter", "camera_sweep")]
    [string]$StabilityMotionMode = "static",
    [int]$MinFamilyCount = 3,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$packetRunner = Join-Path $root "tools/run_scene_local_cinematic_renderer_v1_packets.ps1"
$profileAnalyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_scene_profile.py"
$outputPath = Join-Path $root $OutputRoot
$profileOutput = Join-Path $outputPath "v3_scene_profile.json"
$profileMarkdown = Join-Path $outputPath "v3_scene_profile.md"

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-FamilyFilter", $FamilyFilter,
    "-ViewFilter", $ViewFilter,
    "-SmokeFrames", "$SmokeFrames",
    "-CaptureFrame", "$CaptureFrame",
    "-CaptureSequenceCount", "$CaptureSequenceCount",
    "-StabilityMotionMode", $StabilityMotionMode,
    "-SkipOwnerAnalysis",
    "-SkipMaterialAnalysis",
    "-SkipStabilityAnalysis",
    "-SkipVisualQualityAnalysis"
)
if ($NoBuild) {
    $packetArgs += "-NoBuild"
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $packetRunner @packetArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$manifestPath = Join-Path $outputPath "manifest.json"
& python $profileAnalyzer --manifest $manifestPath --output-json $profileOutput --output-md $profileMarkdown --min-family-count $MinFamilyCount
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "SceneProfileV3 focus packet passed."
Write-Host "scene_profile=$profileOutput"
