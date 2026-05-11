param(
    [int]$SmokeFrames = 140,
    [double]$MaxAvgLumaDelta = 80.0,
    [double]$MaxCenterLumaDelta = 90.0,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "material_path_equivalence_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Assert-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if (-not $Text.Contains($Needle)) {
        Add-Failure $Message
    }
}

function Test-ShaderContract {
    $shaderRoot = Join-Path $root "assets/shaders"
    $materialResolve = Get-Content (Join-Path $shaderRoot "MaterialResolve.hlsl") -Raw
    $deferredLighting = Get-Content (Join-Path $shaderRoot "DeferredLighting.hlsl") -Raw
    $postProcess = Get-Content (Join-Path $shaderRoot "PostProcess.hlsl") -Raw

    Assert-Contains $materialResolve "anisotropy = saturate(mat.extraParams.z)" `
        "MaterialResolve does not source anisotropy from material extraParams.z"
    Assert-Contains $materialResolve "float4(EncodeSurfaceClass(materialClass), anisotropy, sheenWeight, subsurfaceWrap)" `
        "MaterialResolve does not write anisotropy into MaterialExt2.g"
    Assert-Contains $deferredLighting "float anisotropy = saturate(materialExt2.g)" `
        "DeferredLighting does not read MaterialExt2.g as anisotropy"
    Assert-Contains $deferredLighting "ApplyDeferredAnisotropy" `
        "DeferredLighting does not apply the dedicated anisotropy G-buffer channel"
    Assert-Contains $postProcess "const bool reflectionEligibleClass" `
        "PostProcess does not derive reflection eligibility from surface/material class"

    if ($postProcess -match "reflectionClassMask\s*=\s*saturate\s*\(\s*materialExt2\.g\s*\)") {
        Add-Failure "PostProcess still treats MaterialExt2.g as a reflection mask"
    }
}

Test-ShaderContract

function Invoke-MaterialCase([string]$Name, [bool]$DisableVisibilityBuffer) {
    $caseLogDir = Join-Path $LogDir $Name
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $env:CORTEX_LOG_DIR = $caseLogDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"
    if ($DisableVisibilityBuffer) {
        $env:CORTEX_DISABLE_VISIBILITY_BUFFER = "1"
    } else {
        Remove-Item Env:\CORTEX_DISABLE_VISIBILITY_BUFFER -ErrorAction SilentlyContinue
    }

    Push-Location (Split-Path -Parent $exe)
    try {
        $output = & $exe `
            "--scene" "material_lab" `
            "--camera-bookmark" "hero" `
            "--environment" "cool_overcast" `
            "--graphics-preset" "release_showcase" `
            "--mode=default" `
            "--no-llm" `
            "--no-dreamer" `
            "--no-launcher" `
            "--smoke-frames=$SmokeFrames" `
            "--exit-after-visual-validation" 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
    } finally {
        Pop-Location
        Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_VISIBILITY_BUFFER -ErrorAction SilentlyContinue
    }

    $reportPath = Join-Path $caseLogDir "frame_report_last.json"
    if (-not (Test-Path $reportPath)) {
        $reportPath = Join-Path $caseLogDir "frame_report_shutdown.json"
    }
    if ($exitCode -ne 0) {
        Add-Failure "$Name failed with exit code $exitCode. logs=$caseLogDir"
        return $null
    }
    if (-not (Test-Path $reportPath)) {
        Add-Failure "$Name did not write a frame report. logs=$caseLogDir"
        return $null
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.scene -ne "material_lab") {
        Add-Failure "$Name reported scene '$($report.scene)', expected material_lab"
    }
    if ($report.health_warnings.Count -ne 0) {
        Add-Failure "$Name health_warnings is not empty: $($report.health_warnings -join ', ')"
    }
    if ($report.frame_contract.warnings.Count -ne 0) {
        Add-Failure "$Name frame_contract warnings is not empty: $($report.frame_contract.warnings -join ', ')"
    }
    if (-not [bool]$report.visual_validation.captured -or
        -not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "$Name visual validation capture is invalid"
    }
    return $report
}

$vbReport = Invoke-MaterialCase "visibility_buffer" $false
$forwardReport = Invoke-MaterialCase "forward_fallback" $true

if ($null -ne $vbReport -and $null -ne $forwardReport) {
    if (-not [bool]$vbReport.frame_contract.culling.visibility_buffer_rendered) {
        Add-Failure "default material path did not render through visibility buffer"
    }
    if ([bool]$forwardReport.frame_contract.culling.visibility_buffer_rendered) {
        Add-Failure "forward fallback still rendered through visibility buffer"
    }

    $materialFields = @(
        "sampled",
        "surface_mirror",
        "surface_glass",
        "surface_plastic",
        "surface_masonry",
        "surface_emissive",
        "surface_brushed_metal",
        "advanced_feature_materials",
        "advanced_clearcoat",
        "advanced_transmission",
        "advanced_emissive",
        "advanced_specular",
        "advanced_sheen",
        "advanced_subsurface",
        "advanced_anisotropy",
        "advanced_wetness",
        "advanced_emissive_bloom",
        "advanced_procedural_mask"
    )
    foreach ($field in $materialFields) {
        $vbValue = [int]$vbReport.frame_contract.materials.$field
        $forwardValue = [int]$forwardReport.frame_contract.materials.$field
        if ($vbValue -ne $forwardValue) {
            Add-Failure "material field '$field' differs across paths: vb=$vbValue forward=$forwardValue"
        }
    }

    $vbStats = $vbReport.visual_validation.image_stats
    $forwardStats = $forwardReport.visual_validation.image_stats
    $avgDelta = [Math]::Abs([double]$vbStats.avg_luma - [double]$forwardStats.avg_luma)
    $centerDelta = [Math]::Abs([double]$vbStats.center_avg_luma - [double]$forwardStats.center_avg_luma)
    if ($avgDelta -gt $MaxAvgLumaDelta) {
        Add-Failure "avg_luma delta across paths is $avgDelta, max $MaxAvgLumaDelta"
    }
    if ($centerDelta -gt $MaxCenterLumaDelta) {
        Add-Failure "center_avg_luma delta across paths is $centerDelta, max $MaxCenterLumaDelta"
    }
    foreach ($stats in @($vbStats, $forwardStats)) {
        if ([double]$stats.nonblack_ratio -lt 0.95) {
            Add-Failure "material path nonblack ratio too low: $($stats.nonblack_ratio)"
        }
        if ([double]$stats.saturated_ratio -gt 0.12) {
            Add-Failure "material path saturated ratio too high: $($stats.saturated_ratio)"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Material path equivalence tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Material path equivalence tests passed" -ForegroundColor Green
Write-Host (" logs={0}" -f $LogDir)
Write-Host (" vb_luma={0:N2} forward_luma={1:N2} vb_rendered={2}/{3}" -f `
    [double]$vbReport.visual_validation.image_stats.avg_luma,
    [double]$forwardReport.visual_validation.image_stats.avg_luma,
    [bool]$vbReport.frame_contract.culling.visibility_buffer_rendered,
    [bool]$forwardReport.frame_contract.culling.visibility_buffer_rendered)
exit 0
