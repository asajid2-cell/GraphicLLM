param(
    [string]$OutputRoot = "",
    [int]$CaptureStartFrame = 70,
    [int]$CaptureCount = 16,
    [int]$MotionFrames = 100,
    [double]$MotionLookAmplitude = 0.025,
    [double]$MotionLookCycles = 6.0,
    [double]$FixedDeltaTime = 0.008333333,
    [string[]]$ForegroundGateRois = @("left_wall_panel_clean"),
    [double]$MaxForegroundMeanLumaDelta = 8.0,
    [double]$MaxForegroundChangedRatio = 0.12,
    [double]$MaxForegroundLargeChangedRatio = 0.04,
    [double]$MinForegroundMaskCoverage = 0.90,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputRoot = Join-Path $root "build\captures\rt_showcase_wall_floor_masked_owner_packet_$stamp"
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Set-EnvOrClear([string]$Name, [string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        Remove-Item "Env:\$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:\$Name" $Value
    }
}

function Run-CaptureCase([string]$Name, [string]$DebugView, [bool]$DisableAux) {
    $caseDir = Join-Path $OutputRoot $Name
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null

    Set-EnvOrClear "CORTEX_DEBUG_VIEW" $DebugView
    if ($DisableAux) {
        Set-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" "1"
    } else {
        Remove-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" -ErrorAction SilentlyContinue
    }

    $childArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_showcase_wall_floor_flicker_stability_smoke.ps1"),
        "-LogDir", $caseDir,
        "-CaptureStartFrame", [string]$CaptureStartFrame,
        "-CaptureCount", [string]$CaptureCount,
        "-MotionFrames", [string]$MotionFrames,
        "-MotionLookAmplitude", [string]$MotionLookAmplitude,
        "-MotionLookCycles", [string]$MotionLookCycles,
        "-FixedDeltaTime", [string]$FixedDeltaTime,
        "-MaxMeanAbsLumaDelta", "999",
        "-MaxChangedPixelRatio", "1",
        "-MaxLargeChangedPixelRatio", "1"
    )
    if ($NoBuild) {
        $childArgs += "-NoBuild"
    }

    & powershell.exe @childArgs | ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
    $captureCountActual = @(Get-ChildItem -Path $caseDir -Filter "visual_validation_frame_*.bmp" -ErrorAction SilentlyContinue).Count
    [pscustomobject]@{
        name = $Name
        debug_view = $DebugView
        disable_aux = $DisableAux
        exit_code = $exitCode
        capture_count = $captureCountActual
        dir = $caseDir
    }
}

function Run-Analysis([string]$Name, [string]$CaptureDir, [string]$MaskDir, [bool]$InvertMask) {
    $json = Join-Path $CaptureDir "$Name.json"
    $md = Join-Path $CaptureDir "$Name.md"
    $analysisArgs = @(
        (Join-Path $root "tools\analyze_rt_showcase_wall_floor_roi_stability.py"),
        "--capture-dir", $CaptureDir,
        "--mask-dir", $MaskDir,
        "--mask-mode", "not-reference-color",
        "--mask-threshold", "12",
        "--mask-reference-x", "1270",
        "--mask-reference-y", "10",
        "--output-json", $json,
        "--output-md", $md
    )
    if ($InvertMask) {
        $analysisArgs += "--invert-mask"
    }
    & python @analysisArgs | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "analysis failed for $Name"
    }
    [pscustomobject]@{
        name = $Name
        json = $json
        md = $md
        inverted = $InvertMask
    }
}

function Get-RoiAggregateMetric($AnalysisJson, [string]$Roi, [string]$Metric) {
    if ($null -eq $AnalysisJson.aggregate -or
        -not ($AnalysisJson.aggregate.PSObject.Properties.Name -contains $Roi)) {
        throw "analysis '$($AnalysisJson.capture_dir)' missing ROI '$Roi'"
    }
    $roiStats = $AnalysisJson.aggregate.$Roi
    if (-not ($roiStats.PSObject.Properties.Name -contains $Metric)) {
        throw "analysis '$($AnalysisJson.capture_dir)' ROI '$Roi' missing metric '$Metric'"
    }
    [double]$roiStats.$Metric
}

function Test-ForegroundGate([string]$AnalysisName, [string]$JsonPath) {
    $json = Get-Content -Path $JsonPath -Raw | ConvertFrom-Json
    $rows = @()
    $failures = New-Object System.Collections.Generic.List[string]
    foreach ($roi in $ForegroundGateRois) {
        $mean = Get-RoiAggregateMetric $json $roi "max_mean_abs_luma_delta"
        $changed = Get-RoiAggregateMetric $json $roi "max_changed_pixel_ratio"
        $large = Get-RoiAggregateMetric $json $roi "max_large_changed_pixel_ratio"
        $coverage = Get-RoiAggregateMetric $json $roi "max_mask_coverage"
        $passed =
            $coverage -ge $MinForegroundMaskCoverage -and
            $mean -le $MaxForegroundMeanLumaDelta -and
            $changed -le $MaxForegroundChangedRatio -and
            $large -le $MaxForegroundLargeChangedRatio
        if (-not $passed) {
            $failures.Add("$AnalysisName/$roi failed foreground gate: mean=$mean changed=$changed large=$large coverage=$coverage")
        }
        $rows += [pscustomobject]@{
            analysis = $AnalysisName
            roi = $roi
            mean_abs_luma_delta = $mean
            changed_pixel_ratio = $changed
            large_changed_pixel_ratio = $large
            mask_coverage = $coverage
            passed = $passed
        }
    }
    [pscustomobject]@{
        analysis = $AnalysisName
        json = $JsonPath
        rows = $rows
        failures = @($failures)
        passed = ($failures.Count -eq 0)
    }
}

Push-Location $root
try {
    Copy-Item assets\shaders\Basic.hlsl build\bin\assets\shaders\Basic.hlsl -Force
    Copy-Item assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl -Force
    Copy-Item assets\shaders\MaterialResolve.hlsl build\bin\assets\shaders\MaterialResolve.hlsl -Force

    Set-Item "Env:\CORTEX_DISABLE_SHADER_CACHE" "1"

    $captures = @()
    $captures += Run-CaptureCase "beauty" "" $false
    $captures += Run-CaptureCase "mask_normal_roughness36" "36" $true
    $captures += Run-CaptureCase "specular9" "9" $true
    $captures += Run-CaptureCase "ownership92" "92" $true

    Remove-Item "Env:\CORTEX_DEBUG_VIEW" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_SHADER_CACHE" -ErrorAction SilentlyContinue

    $beautyDir = Join-Path $OutputRoot "beauty"
    $maskDir = Join-Path $OutputRoot "mask_normal_roughness36"
    $specularDir = Join-Path $OutputRoot "specular9"
    $ownershipDir = Join-Path $OutputRoot "ownership92"

    $analysis = @()
    $analysis += Run-Analysis "beauty_foreground" $beautyDir $maskDir $false
    $analysis += Run-Analysis "beauty_background" $beautyDir $maskDir $true
    $analysis += Run-Analysis "specular_foreground" $specularDir $maskDir $false
    $analysis += Run-Analysis "specular_background" $specularDir $maskDir $true
    $analysis += Run-Analysis "ownership_foreground" $ownershipDir $maskDir $false
    $analysis += Run-Analysis "ownership_background" $ownershipDir $maskDir $true

    $failures = New-Object System.Collections.Generic.List[string]
    foreach ($capture in $captures) {
        if ($capture.capture_count -lt $CaptureCount) {
            $failures.Add("capture '$($capture.name)' wrote $($capture.capture_count) BMPs, expected at least $CaptureCount")
        }
    }

    $beautyForegroundJson = Join-Path $beautyDir "beauty_foreground.json"
    $specularForegroundJson = Join-Path $specularDir "specular_foreground.json"
    $gateResults = @()
    $gateResults += Test-ForegroundGate "beauty_foreground" $beautyForegroundJson
    $gateResults += Test-ForegroundGate "specular_foreground" $specularForegroundJson
    foreach ($gate in $gateResults) {
        foreach ($failure in $gate.failures) {
            $failures.Add($failure)
        }
    }
    $gatePassed = ($failures.Count -eq 0)

    $summary = [pscustomobject]@{
        schema = "cortex.rt_showcase.wall_floor_masked_owner_packet.v1"
        output_root = $OutputRoot
        captures = $captures
        analyses = $analysis
        gate = [pscustomobject]@{
            passed = $gatePassed
            foreground_rois = $ForegroundGateRois
            max_mean_abs_luma_delta = $MaxForegroundMeanLumaDelta
            max_changed_pixel_ratio = $MaxForegroundChangedRatio
            max_large_changed_pixel_ratio = $MaxForegroundLargeChangedRatio
            min_mask_coverage = $MinForegroundMaskCoverage
            results = $gateResults
            failures = @($failures)
        }
    }
    $summaryPath = Join-Path $OutputRoot "masked_owner_packet_summary.json"
    $summary | ConvertTo-Json -Depth 6 | Set-Content -Path $summaryPath -Encoding UTF8

    $summaryMd = Join-Path $OutputRoot "masked_owner_packet_summary.md"
    $lines = @(
        "# RT Showcase Wall/Floor Masked Owner Packet",
        "",
        "Output root: ``$OutputRoot``",
        "",
        "## Captures",
        "",
        "| Case | Debug view | Disable aux | Exit | BMPs |",
        "|---|---:|---:|---:|---:|"
    )
    foreach ($capture in $captures) {
        $lines += "| $($capture.name) | $($capture.debug_view) | $($capture.disable_aux) | $($capture.exit_code) | $($capture.capture_count) |"
    }
    $lines += ""
    $lines += "## Analyses"
    $lines += ""
    foreach ($item in $analysis) {
        $lines += "- $($item.name): ``$($item.md)``"
    }
    $lines += ""
    $lines += "## Foreground Gate"
    $lines += ""
    $lines += "Passed: ``$gatePassed``"
    $lines += ""
    $lines += "| Analysis | ROI | Mean | Changed | Large Changed | Coverage | Passed |"
    $lines += "|---|---|---:|---:|---:|---:|---:|"
    foreach ($gate in $gateResults) {
        foreach ($row in $gate.rows) {
            $lines += "| $($row.analysis) | $($row.roi) | $([string]::Format('{0:F4}', $row.mean_abs_luma_delta)) | $([string]::Format('{0:F4}', $row.changed_pixel_ratio)) | $([string]::Format('{0:F4}', $row.large_changed_pixel_ratio)) | $([string]::Format('{0:F4}', $row.mask_coverage)) | $($row.passed) |"
        }
    }
    if ($failures.Count -gt 0) {
        $lines += ""
        $lines += "## Failures"
        $lines += ""
        foreach ($failure in $failures) {
            $lines += "- $failure"
        }
    }
    $lines | Set-Content -Path $summaryMd -Encoding UTF8

    Write-Host "RT Showcase masked owner packet complete"
    Write-Host " summary=$summaryPath"
    Write-Host " report=$summaryMd"
    if (-not $gatePassed) {
        Write-Host "Foreground gate failed:" -ForegroundColor Red
        foreach ($failure in $failures) {
            Write-Host " - $failure" -ForegroundColor Red
        }
        exit 1
    }
} finally {
    Pop-Location
    Remove-Item "Env:\CORTEX_DEBUG_VIEW" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_SHADER_CACHE" -ErrorAction SilentlyContinue
}
