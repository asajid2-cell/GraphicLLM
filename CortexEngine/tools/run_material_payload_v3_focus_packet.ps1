param(
    [string]$OutputRoot = "build/captures/material_payload_v3_focus_packet",
    [string]$StressSceneFilter = "rt_showcase:reflection_closeup",
    [string]$ViewFilter = "material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id",
    [int]$SmokeFrames = 18,
    [int]$CaptureFrame = 9,
    [int]$CaptureSequenceCount = 1,
    [ValidateSet("static", "mouse_jitter", "camera_sweep")]
    [string]$StabilityMotionMode = "static",
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$v2Packet = Join-Path $root "tools/run_full_scene_shader_pipeline_v2_packet.ps1"
$materialAnalyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_material_payload.py"
$outputPath = Join-Path $root $OutputRoot
$materialPayloadOutput = Join-Path $outputPath "v3_material_payload.json"
$materialPayloadMarkdown = Join-Path $outputPath "v3_material_payload.md"

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-ViewFilter", $ViewFilter,
    "-StressSceneFilter", $StressSceneFilter,
    "-StressSceneOnly",
    "-SkipSceneAnalyzers",
    "-SmokeFrames", "$SmokeFrames",
    "-CaptureFrame", "$CaptureFrame",
    "-CaptureSequenceCount", "$CaptureSequenceCount",
    "-StabilityMotionMode", $StabilityMotionMode
)
if ($NoBuild) {
    $packetArgs += "-NoBuild"
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $v2Packet @packetArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$manifestPath = Join-Path $outputPath "manifest.json"
& python $materialAnalyzer --manifest $manifestPath --output-json $materialPayloadOutput --output-md $materialPayloadMarkdown
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "MaterialPayloadV3 focus packet passed."
Write-Host "material_payload=$materialPayloadOutput"
