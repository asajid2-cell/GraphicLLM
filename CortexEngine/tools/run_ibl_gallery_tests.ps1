param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 90,
    [int]$MaxEnvironments = 0,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$manifestPath = Join-Path $root "assets/environments/environments.json"

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "ibl_gallery_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}
if (-not (Test-Path $manifestPath)) {
    throw "Environment manifest not found: $manifestPath"
}

$manifestDir = Split-Path -Parent $manifestPath
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$candidates = New-Object System.Collections.Generic.List[object]
foreach ($entry in $manifest.environments) {
    if ($entry.enabled -eq $false) { continue }
    if ([string]$entry.type -eq "procedural") { continue }
    if ([string]::IsNullOrWhiteSpace([string]$entry.runtime_path)) { continue }

    $runtimePath = [System.IO.Path]::GetFullPath((Join-Path $manifestDir ([string]$entry.runtime_path)))
    if (-not (Test-Path $runtimePath)) {
        if ($entry.required -eq $true) {
            throw "Required IBL '$($entry.id)' runtime asset missing: $runtimePath"
        }
        Write-Host "Skipping optional missing IBL '$($entry.id)': $runtimePath" -ForegroundColor Yellow
        continue
    }

    $candidates.Add([pscustomobject]@{
        id = [string]$entry.id
        budget_class = [string]$entry.budget_class
        max_runtime_dimension = [int]$entry.max_runtime_dimension
        runtime_path = $runtimePath
    })
}

if ($MaxEnvironments -gt 0 -and $candidates.Count -gt $MaxEnvironments) {
    $candidates = [System.Collections.Generic.List[object]]($candidates | Select-Object -First $MaxEnvironments)
}
if ($candidates.Count -lt 1) {
    throw "No runtime IBL candidates found in $manifestPath"
}

$failures = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]
$exeWorkingDir = Split-Path -Parent $exe

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

foreach ($candidate in $candidates) {
    $caseLogDir = Join-Path $LogDir $candidate.id
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null
    $env:CORTEX_LOG_DIR = $caseLogDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

    Push-Location $exeWorkingDir
    try {
        $output = & $exe `
            "--scene" "rt_showcase" `
            "--environment" $candidate.id `
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
    }

    $reportPath = Join-Path $caseLogDir "frame_report_last.json"
    $row = [ordered]@{
        id = $candidate.id
        exit_code = $exitCode
        report = $reportPath
        active = ""
        budget_class = ""
        width = 0
        height = 0
        resident_count = 0
        pending_count = 0
        avg_luma = $null
        warnings = $null
        passed = $false
    }

    if ($exitCode -ne 0) {
        Add-Failure "IBL '$($candidate.id)' process failed with exit code $exitCode. log=$caseLogDir"
        $rows.Add([pscustomobject]$row)
        continue
    }
    if (-not (Test-Path $reportPath)) {
        Add-Failure "IBL '$($candidate.id)' did not write frame report: $reportPath"
        $rows.Add([pscustomobject]$row)
        continue
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $envContract = $report.frame_contract.environment
    $row.active = [string]$envContract.active
    $row.budget_class = [string]$envContract.budget_class
    $row.width = [int]$envContract.active_width
    $row.height = [int]$envContract.active_height
    $row.resident_count = [int]$envContract.resident_count
    $row.pending_count = [int]$envContract.pending_count
    $row.warnings = $report.frame_contract.warnings.Count
    if ($null -ne $report.visual_validation.image_stats) {
        $row.avg_luma = $report.visual_validation.image_stats.avg_luma
    }

    if ([string]$envContract.active -ne [string]$candidate.id) {
        Add-Failure "IBL '$($candidate.id)' active environment is '$($envContract.active)'"
    }
    if (-not [bool]$envContract.loaded) {
        Add-Failure "IBL '$($candidate.id)' did not report loaded=true"
    }
    if ([bool]$envContract.fallback) {
        Add-Failure "IBL '$($candidate.id)' unexpectedly used fallback"
    }
    if ([int]$envContract.active_width -le 0 -or [int]$envContract.active_height -le 0) {
        Add-Failure "IBL '$($candidate.id)' active extent is invalid"
    }
    if ([int]$candidate.max_runtime_dimension -gt 0 -and
        ([int]$envContract.active_width -gt [int]$candidate.max_runtime_dimension -or
         [int]$envContract.active_height -gt [int]$candidate.max_runtime_dimension)) {
        Add-Failure "IBL '$($candidate.id)' exceeds manifest max_runtime_dimension"
    }
    if ([string]$envContract.budget_class -ne [string]$candidate.budget_class) {
        Add-Failure "IBL '$($candidate.id)' budget class is '$($envContract.budget_class)', expected '$($candidate.budget_class)'"
    }
    if (-not [bool]$report.visual_validation.captured -or
        -not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "IBL '$($candidate.id)' did not produce a valid visual capture"
    } elseif ([double]$report.visual_validation.image_stats.nonblack_ratio -lt 0.95) {
        Add-Failure "IBL '$($candidate.id)' visual nonblack ratio too low: $($report.visual_validation.image_stats.nonblack_ratio)"
    }

    $row.passed = $true
    $rows.Add([pscustomobject]$row)
    Write-Host "IBL '$($candidate.id)' passed: active=$($row.active) $($row.width)x$($row.height) budget=$($row.budget_class)" -ForegroundColor Green
}

$rows | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $LogDir "ibl_gallery_summary.json")

if ($failures.Count -gt 0) {
    Write-Host "IBL gallery tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "IBL gallery tests passed: environments=$($candidates.Count) logs=$LogDir" -ForegroundColor Green
