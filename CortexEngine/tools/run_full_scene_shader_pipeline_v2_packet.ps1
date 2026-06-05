param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v2_facade_packet",
    [string]$FamilyFilter = "gallery",
    [string]$ViewFilter = "beauty,surface_policy,reflection_owner,shadow_factor,direct_light,ambient_ibl,taa_blend",
    [int]$SmokeFrames = 140,
    [int]$CaptureFrame = 60,
    [int]$CaptureSequenceCount = 1,
    [ValidateSet("static", "mouse_jitter", "camera_sweep")]
    [string]$StabilityMotionMode = "static",
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$outRootAbs = Join-Path $root $OutputRoot
$packetRunner = Join-Path $root "tools/run_scene_local_cinematic_renderer_v1_packets.ps1"
$v2Checker = Join-Path $root "tools/check_full_scene_shader_pipeline_v2_frame_report.py"

if (-not (Test-Path $packetRunner)) {
    throw "Scene-local packet runner missing: $packetRunner"
}
if (-not (Test-Path $v2Checker)) {
    throw "Full Scene Shader Pipeline V2 checker missing: $v2Checker"
}

$packetArgs = @(
    "-OutputRoot", $OutputRoot,
    "-FamilyFilter", $FamilyFilter,
    "-ViewFilter", $ViewFilter,
    "-SmokeFrames", [string]$SmokeFrames,
    "-CaptureFrame", [string]$CaptureFrame,
    "-CaptureSequenceCount", [string]$CaptureSequenceCount,
    "-StabilityMotionMode", $StabilityMotionMode
)
if ($NoBuild) {
    $packetArgs += "-NoBuild"
}
if ($SkipSceneAnalyzers) {
    $packetArgs += @(
        "-SkipOwnerAnalysis",
        "-SkipMaterialAnalysis",
        "-SkipStabilityAnalysis",
        "-SkipVisualQualityAnalysis"
    )
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $packetRunner @packetArgs
if ($LASTEXITCODE -ne 0) {
    throw "Scene-local packet runner failed with exit code $LASTEXITCODE"
}

$manifestPath = Join-Path $outRootAbs "manifest.json"
if (-not (Test-Path $manifestPath)) {
    throw "Packet manifest missing: $manifestPath"
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$sections = @(
    "material",
    "gbuffer",
    "lighting",
    "reflections",
    "shadows",
    "temporal",
    "post",
    "render_graph",
    "asset_evidence",
    "packet_gate"
)
$commonEvidence = @(
    "promotion_state",
    "domain_ready",
    "facade_owner",
    "fallback_owner",
    "failure_reason"
)

$rows = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]
$checkerOutput = New-Object System.Collections.Generic.List[string]

foreach ($result in @($manifest.results)) {
    $reportPath = [string]$result.report
    if ([string]::IsNullOrWhiteSpace($reportPath) -or -not (Test-Path $reportPath)) {
        $failures.Add("missing frame report for $($result.family)/$($result.view): $reportPath") | Out-Null
        continue
    }

    $checkOutput = & python $v2Checker --frame-report $reportPath --strict-frame-report 2>&1
    $checkerExit = $LASTEXITCODE
    foreach ($line in @($checkOutput)) {
        $checkerOutput.Add("$($result.family)/$($result.view): $line") | Out-Null
    }
    if ($checkerExit -ne 0) {
        $failures.Add("V2 checker failed for $($result.family)/$($result.view): $reportPath") | Out-Null
        continue
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $v2 = $report.frame_contract.full_scene_shader_pipeline_v2
    if ($null -eq $v2) {
        $failures.Add("frame_contract.full_scene_shader_pipeline_v2 missing for $($result.family)/$($result.view)") | Out-Null
        continue
    }

    foreach ($section in $sections) {
        $sectionData = $v2.$section
        if ($null -eq $sectionData) {
            $failures.Add("section '$section' missing for $($result.family)/$($result.view)") | Out-Null
            continue
        }
        $evidence = $sectionData.evidence
        if ($null -eq $evidence) {
            $failures.Add("section '$section' evidence missing for $($result.family)/$($result.view)") | Out-Null
            continue
        }
        foreach ($field in $commonEvidence) {
            if (-not ($evidence.PSObject.Properties.Name -contains $field)) {
                $failures.Add("section '$section' evidence missing '$field' for $($result.family)/$($result.view)") | Out-Null
            }
        }
        $rows.Add([pscustomobject]@{
            family = [string]$result.family
            view = [string]$result.view
            section = $section
            enabled = [bool]$sectionData.enabled
            domain_ready = [bool]$evidence.domain_ready
            promotion_state = [string]$evidence.promotion_state
            facade_owner = [string]$evidence.facade_owner
            fallback_owner = [string]$evidence.fallback_owner
            failure_reason = [string]$evidence.failure_reason
            status = [string]$v2.status
            beauty_output = [string]$v2.beauty_output
            report = $reportPath
        }) | Out-Null
    }
}

$summary = [ordered]@{
    schema = "cortex.full_scene_shader_pipeline_v2.packet_evidence_summary.v1"
    output_root = $outRootAbs
    manifest = $manifestPath
    family_filter = $FamilyFilter
    view_filter = $ViewFilter
    captured_view_count = @($manifest.results).Count
    evidence_row_count = $rows.Count
    sections = $sections
    common_evidence_fields = $commonEvidence
    failures = @($failures)
    rows = @($rows)
}

$summaryJsonPath = Join-Path $outRootAbs "v2_frame_report_evidence_summary.json"
$summaryMdPath = Join-Path $outRootAbs "v2_frame_report_evidence_summary.md"
$checkerStdoutPath = Join-Path $outRootAbs "v2_frame_report_checker_stdout.txt"

$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $summaryJsonPath
$checkerOutput | Set-Content -Encoding UTF8 $checkerStdoutPath

$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Full Scene Shader Pipeline V2 Packet Evidence") | Out-Null
$md.Add("") | Out-Null
$md.Add(("- manifest: ``{0}``" -f $manifestPath)) | Out-Null
$md.Add(("- captured views: {0}" -f @($manifest.results).Count)) | Out-Null
$md.Add(("- evidence rows: {0}" -f $rows.Count)) | Out-Null
$md.Add(("- failures: {0}" -f $failures.Count)) | Out-Null
$md.Add("") | Out-Null
$md.Add("| Family | View | Section | Enabled | Ready | Promotion | Owner | Fallback |") | Out-Null
$md.Add("|---|---|---|---:|---:|---|---|---|") | Out-Null
foreach ($row in @($rows)) {
    $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f `
        $row.family,
        $row.view,
        $row.section,
        $row.enabled,
        $row.domain_ready,
        $row.promotion_state,
        $row.facade_owner,
        $row.fallback_owner)) | Out-Null
}
$md | Set-Content -Encoding UTF8 $summaryMdPath

if ($failures.Count -gt 0) {
    Write-Host "Full Scene Shader Pipeline V2 packet evidence failed:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    Write-Host "summary=$summaryJsonPath"
    exit 1
}

Write-Host "Full Scene Shader Pipeline V2 packet evidence passed."
Write-Host "manifest=$manifestPath"
Write-Host "summary=$summaryJsonPath"
