param(
    [switch]$NoBuild,
    [int]$TemporalSmokeFrames = 140,
    [int]$RTSmokeFrames = 240,
    [int]$IBLGalleryMaxEnvironments = 3,
    [switch]$SkipSurfaceDebug
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = "phase3_visual_matrix_{0}_{1}_{2}" -f `
    (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
    $PID,
    ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$matrixLogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
New-Item -ItemType Directory -Force -Path $matrixLogDir | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]

function Invoke-MatrixStep([string]$Name, [string[]]$Arguments, [string]$ReportPath) {
    $stepLogDir = Join-Path $script:matrixLogDir $Name
    New-Item -ItemType Directory -Force -Path $stepLogDir | Out-Null
    $stdoutPath = Join-Path $stepLogDir "stdout.txt"
    $stderrPath = Join-Path $stepLogDir "stderr.txt"
    $started = Get-Date

    Write-Host "==> $Name" -ForegroundColor Cyan
    $process = Start-Process `
        -FilePath "powershell" `
        -ArgumentList $Arguments `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru `
        -Wait `
        -WindowStyle Hidden

    $seconds = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)
    $stdoutText = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
    $stderrText = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }

    $row = [ordered]@{
        name = $Name
        exit_code = $process.ExitCode
        seconds = $seconds
        log_dir = $stepLogDir
        report = $ReportPath
        passed = $process.ExitCode -eq 0
        gpu_ms = $null
        avg_luma = $null
        frame_warnings = $null
        environment = ""
        health_preset = ""
        lighting_rig = ""
        rt_ready = $null
        particles = $null
    }

    if ($process.ExitCode -ne 0) {
        $script:failures.Add("$Name failed with exit code $($process.ExitCode). logs=$stepLogDir`n$stderrText`n$stdoutText")
        $script:rows.Add([pscustomobject]$row)
        return
    }

    if ($ReportPath -and (Test-Path $ReportPath)) {
        $report = Get-Content $ReportPath -Raw | ConvertFrom-Json
        $row.gpu_ms = $report.gpu_frame_ms
        if ($null -ne $report.visual_validation.image_stats) {
            $row.avg_luma = $report.visual_validation.image_stats.avg_luma
        }
        $row.frame_warnings = $report.frame_contract.warnings.Count
        $row.environment = $report.frame_contract.environment.active
        $row.health_preset = $report.frame_contract.health.quality_preset
        $row.lighting_rig = $report.frame_contract.lighting.rig_id
        $row.rt_ready = $report.frame_contract.ray_tracing.reflection_dispatch_ready
        if ($null -ne $report.frame_contract.particles) {
            $row.particles = $report.frame_contract.particles.submitted_instances
        }
    }

    $script:rows.Add([pscustomobject]$row)
    Write-Host "PASSED $Name in ${seconds}s" -ForegroundColor Green
}

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

$temporalLogDir = Join-Path $matrixLogDir "temporal_validation_run"
Invoke-MatrixStep "temporal_validation_balanced" @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run_temporal_validation_smoke.ps1"),
    "-NoBuild",
    "-LogDir", $temporalLogDir,
    "-SmokeFrames", [string]$TemporalSmokeFrames
) (Join-Path $temporalLogDir "frame_report_last.json")

if ($failures.Count -eq 0) {
    $rtLogDir = Join-Path $matrixLogDir "rt_showcase_release"
    $rtArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"),
        "-NoBuild",
        "-LogDir", $rtLogDir,
        "-SmokeFrames", [string]$RTSmokeFrames
    )
    if ($SkipSurfaceDebug) {
        $rtArgs += "-SkipSurfaceDebug"
    }
    Invoke-MatrixStep "rt_showcase_release" $rtArgs (Join-Path $rtLogDir "frame_report_last.json")
}

if ($failures.Count -eq 0) {
    $iblLogDir = Join-Path $matrixLogDir "ibl_gallery"
    $iblArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_ibl_gallery_tests.ps1"),
        "-NoBuild",
        "-LogDir", $iblLogDir,
        "-MaxEnvironments", [string]$IBLGalleryMaxEnvironments
    )
    Invoke-MatrixStep "ibl_gallery" $iblArgs ""
}

$summaryPath = Join-Path $matrixLogDir "phase3_visual_matrix_summary.json"
$markdownPath = Join-Path $matrixLogDir "phase3_visual_matrix_summary.md"
$rows | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $summaryPath

$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Phase 3 Visual Matrix")
$md.Add("")
$md.Add("| Case | Passed | GPU ms | Avg luma | Environment | Preset | Lighting Rig | Warnings | Particles |")
$md.Add("|---|---:|---:|---:|---|---|---|---:|---:|")
foreach ($row in $rows) {
    $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} |" -f `
        $row.name,
        $row.passed,
        $row.gpu_ms,
        $row.avg_luma,
        $row.environment,
        $row.health_preset,
        $row.lighting_rig,
        $row.frame_warnings,
        $row.particles))
}
$md | Set-Content -Encoding UTF8 $markdownPath

if ($failures.Count -gt 0) {
    Write-Host "Phase 3 visual matrix failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$matrixLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Phase 3 visual matrix passed" -ForegroundColor Green
Write-Host "logs=$matrixLogDir"
Write-Host "summary=$summaryPath"
