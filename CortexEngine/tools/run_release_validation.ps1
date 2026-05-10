param(
    [switch]$NoBuild,
    [int]$TemporalSmokeFrames = 140,
    [int]$RTSmokeFrames = 240,
    [int]$IBLGalleryMaxEnvironments = 3,
    [int]$BudgetTemporalRuns = 1,
    [int]$BudgetMaxParallel = 1,
    [int]$VoxelSmokeFrames = 120
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$runId = "release_validation_{0}_{1}_{2}" -f `
    (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
    $PID,
    ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$releaseLogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
New-Item -ItemType Directory -Force -Path $releaseLogDir | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
$steps = New-Object System.Collections.Generic.List[object]

function Invoke-ReleaseStep([string]$Name, [string[]]$Arguments) {
    $stepLogDir = Join-Path $script:releaseLogDir $Name
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

    $elapsed = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)
    $stdoutText = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
    $stderrText = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }

    $script:steps.Add([pscustomobject]@{
        Name = $Name
        ExitCode = $process.ExitCode
        Seconds = $elapsed
        LogDir = $stepLogDir
    })

    if ($process.ExitCode -ne 0) {
        $script:failures.Add(
            "$Name failed with exit code $($process.ExitCode). logs=$stepLogDir`n$stderrText`n$stdoutText")
        Write-Host "FAILED $Name in ${elapsed}s" -ForegroundColor Red
        return
    }

    Write-Host "PASSED $Name in ${elapsed}s" -ForegroundColor Green
    if (-not [string]::IsNullOrWhiteSpace($stdoutText)) {
        $stdoutText -split "`r?`n" |
            Where-Object {
                $_ -match "passed" -or
                $_ -match "^\s+frames=" -or
                $_ -match "^\s+logs="
            } |
            Select-Object -Last 8 |
            ForEach-Object { Write-Host $_ }
    }
}

if (-not $NoBuild) {
    Invoke-ReleaseStep "build_release" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $root "rebuild.ps1"),
        "-Config", "Release"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "temporal_validation" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_temporal_validation_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs",
        "-SmokeFrames", [string]$TemporalSmokeFrames
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "rt_showcase" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs",
        "-SmokeFrames", [string]$RTSmokeFrames
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "environment_manifest" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_environment_manifest_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "ibl_gallery" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_ibl_gallery_tests.ps1"),
        "-NoBuild",
        "-MaxEnvironments", [string]$IBLGalleryMaxEnvironments
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "budget_profile_matrix" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_budget_profile_matrix.ps1"),
        "-NoBuild",
        "-TemporalRuns", [string]$BudgetTemporalRuns,
        "-MaxParallel", [string]$BudgetMaxParallel
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "voxel_backend" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_voxel_backend_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs",
        "-SmokeFrames", [string]$VoxelSmokeFrames
    )
}

if ($failures.Count -gt 0) {
    Write-Host "Release validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$releaseLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Release validation passed" -ForegroundColor Green
Write-Host "logs=$releaseLogDir"
foreach ($step in $steps) {
    Write-Host (" - {0}: {1}s logs={2}" -f $step.Name, $step.Seconds, $step.LogDir)
}
