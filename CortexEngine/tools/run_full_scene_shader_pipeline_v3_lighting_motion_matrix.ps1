param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v3_lighting_motion_matrix",
    [string]$FamilyFilter = "gallery,kitchen,office,gym,concert,red_room,stadium",
    [string]$StressSceneFilter = "",
    [string]$MotionModes = "static,mouse_jitter,camera_sweep",
    [string]$ViewFilter = "beauty,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution",
    [int]$SmokeFrames = 40,
    [int]$CaptureFrame = 20,
    [int]$CaptureSequenceCount = 2,
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers,
    [switch]$SummarizeExisting
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$packetRunner = Join-Path $root "tools/run_full_scene_shader_pipeline_v3_packet.ps1"
$outputPath = Join-Path $root $OutputRoot
$matrixJson = Join-Path $outputPath "v3_lighting_motion_matrix.json"
$matrixMd = Join-Path $outputPath "v3_lighting_motion_matrix.md"

if (-not (Test-Path $packetRunner)) {
    throw "V3 packet runner missing: $packetRunner"
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$modeList = @(
    $MotionModes -split "," |
        ForEach-Object { $_.Trim() } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
)
foreach ($mode in $modeList) {
    if (@("static", "mouse_jitter", "camera_sweep", "light_sweep") -notcontains $mode) {
        throw "Unknown motion mode '$mode'. Expected static, mouse_jitter, camera_sweep, or light_sweep."
    }
}

$rows = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

foreach ($mode in $modeList) {
    $modeOutputRoot = Join-Path $OutputRoot $mode
    $packetArgs = @(
        "-OutputRoot", $modeOutputRoot,
        "-FamilyFilter", $FamilyFilter,
        "-ViewFilter", $ViewFilter,
        "-SmokeFrames", [string]$SmokeFrames,
        "-CaptureFrame", [string]$CaptureFrame,
        "-CaptureSequenceCount", [string]$CaptureSequenceCount,
        "-StabilityMotionMode", $mode
    )
    if ($NoBuild) {
        $packetArgs += "-NoBuild"
    }
    if ($SkipSceneAnalyzers) {
        $packetArgs += "-SkipSceneAnalyzers"
    }
    if ([string]::IsNullOrWhiteSpace($StressSceneFilter)) {
        $packetArgs += "-NoStressScene"
    } else {
        $packetArgs += @("-StressSceneFilter", $StressSceneFilter)
    }

    $modeOutputPath = Join-Path $outputPath $mode
    $motionJson = Join-Path $modeOutputPath "v3_lighting_motion.json"
    $motionMd = Join-Path $modeOutputPath "v3_lighting_motion.md"
    $manifest = Join-Path $modeOutputPath "manifest.json"

    if (-not $SummarizeExisting) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $packetRunner @packetArgs
        if ($LASTEXITCODE -ne 0) {
            $failures.Add("packet failed for motion mode '$mode'") | Out-Null
            continue
        }
    } elseif (-not (Test-Path $manifest)) {
        $failures.Add("missing existing manifest for '$mode': $manifest") | Out-Null
        continue
    }

    if (-not (Test-Path $motionJson)) {
        $analyzer = Join-Path $root "tools/analyze_full_scene_shader_v3_lighting_motion.py"
        & python $analyzer --manifest $manifest --output-json $motionJson --output-md $motionMd --min-sequence-count $CaptureSequenceCount
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $motionJson)) {
            $failures.Add("missing lighting motion report for '$mode': $motionJson") | Out-Null
            continue
        }
    }

    $report = Get-Content $motionJson -Raw | ConvertFrom-Json
    foreach ($family in @($report.families)) {
        foreach ($view in @($family.views)) {
            $rows.Add([pscustomobject]@{
                motion_mode = $mode
                family = [string]$family.family
                view = [string]$view.view
                status = [string]$view.status
                v3_mean_abs_luma_delta = [double]$view.v3_mean_abs_luma_delta
                legacy_mean_abs_luma_delta = [double]$view.legacy_mean_abs_luma_delta
                v3_over_legacy_ratio = [double]$view.v3_over_legacy_ratio
                beauty_mean_abs_luma_delta = [double]$view.beauty_mean_abs_luma_delta
                v3_over_beauty_ratio = [double]$view.v3_over_beauty_ratio
                v3_active_delta_ratio = [double]$view.v3_active_delta_ratio
                source_report = $motionJson
            }) | Out-Null
        }
    }

    foreach ($failure in @($report.failures)) {
        $failures.Add("$mode`: $failure") | Out-Null
    }
    foreach ($warning in @($report.warnings)) {
        $warnings.Add("$mode`: $warning") | Out-Null
    }
}

$summary = [ordered]@{
    schema = "cortex.full_scene_shader_pipeline_v3.lighting_motion_matrix.v1"
    output_root = [string]$outputPath
    family_filter = $FamilyFilter
    stress_scene_filter = $StressSceneFilter
    motion_modes = @($modeList)
    view_filter = $ViewFilter
    capture_sequence_count = $CaptureSequenceCount
    row_count = $rows.Count
    failures = @($failures.ToArray())
    warnings = @($warnings.ToArray())
    rows = @($rows.ToArray())
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $matrixJson

$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Full Scene Shader V3 Lighting Motion Matrix") | Out-Null
$md.Add("") | Out-Null
$md.Add(("- output root: ``{0}``" -f $outputPath)) | Out-Null
$md.Add(("- families: ``{0}``" -f $FamilyFilter)) | Out-Null
$md.Add(("- stress scenes: ``{0}``" -f $StressSceneFilter)) | Out-Null
$md.Add(("- motion modes: ``{0}``" -f ($modeList -join ","))) | Out-Null
$md.Add(("- rows: {0}" -f $rows.Count)) | Out-Null
$md.Add(("- failures: {0}" -f $failures.Count)) | Out-Null
$md.Add(("- warnings: {0}" -f $warnings.Count)) | Out-Null
$md.Add("") | Out-Null
$md.Add("| Motion | Family | View | Status | V3 Delta | Legacy Delta | V3/Legacy | Beauty Delta | V3/Beauty | Active Delta |") | Out-Null
$md.Add("|---|---|---|---|---:|---:|---:|---:|---:|---:|") | Out-Null
foreach ($row in $rows.ToArray()) {
    $md.Add(("| {0} | {1} | {2} | {3} | {4:N8} | {5:N8} | {6:N3} | {7:N8} | {8:N3} | {9:N8} |" -f `
        $row.motion_mode,
        $row.family,
        $row.view,
        $row.status,
        $row.v3_mean_abs_luma_delta,
        $row.legacy_mean_abs_luma_delta,
        $row.v3_over_legacy_ratio,
        $row.beauty_mean_abs_luma_delta,
        $row.v3_over_beauty_ratio,
        $row.v3_active_delta_ratio)) | Out-Null
}
if ($failures.Count -gt 0) {
    $md.Add("") | Out-Null
    $md.Add("## Failures") | Out-Null
    $md.Add("") | Out-Null
    foreach ($failure in $failures.ToArray()) {
        $md.Add("- $failure") | Out-Null
    }
}
if ($warnings.Count -gt 0) {
    $md.Add("") | Out-Null
    $md.Add("## Warnings") | Out-Null
    $md.Add("") | Out-Null
    foreach ($warning in $warnings.ToArray()) {
        $md.Add("- $warning") | Out-Null
    }
}
$md | Set-Content -Encoding UTF8 $matrixMd

if ($failures.Count -gt 0) {
    Write-Host "V3 lighting motion matrix failed:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    Write-Host "summary=$matrixJson"
    exit 1
}

Write-Host "Full Scene Shader Pipeline V3 lighting motion matrix passed."
Write-Host "summary=$matrixJson"
Write-Host "markdown=$matrixMd"
