param(
    [int]$Runs = 2,
    [int]$SmokeFrames = 120,
    [int]$MaxExpectedFrames = 160,
    [int]$VisualValidationMinFrame = 30,
    [string]$CameraBookmark = "hero",
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$NoBuild,
    [int]$MinTLASInstances = 8,
    [int]$MinRayTracingPasses = 3,
    [double]$MinVisualNonBlackRatio = 0.95,
    [double]$MinVisualAvgLuma = 20.0,
    [double]$MinVisualCenterLuma = 20.0,
    [double]$MaxRunAvgLumaDelta = 12.0,
    [double]$MaxRunNonBlackDelta = 0.05
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
$vcpkgBin = Join-Path $root "build/vcpkg_installed/x64-windows/bin"

if ($Runs -lt 1) {
    throw "-Runs must be >= 1"
}

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild after building."
}

$activeRoot = $LogDir
if ([string]::IsNullOrWhiteSpace($activeRoot)) {
    if ($IsolatedLogs) {
        $runId = "rt_nonblack_fixture_{0}_{1}_{2}" -f `
            (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
            $PID,
            ([Guid]::NewGuid().ToString("N").Substring(0, 8))
        $activeRoot = Join-Path (Join-Path $baseLogDir "runs") $runId
    } else {
        $activeRoot = Join-Path $baseLogDir "rt_nonblack_fixture"
    }
}
New-Item -ItemType Directory -Force -Path $activeRoot | Out-Null

$exeWorkingDir = Split-Path -Parent $exe
$failures = New-Object System.Collections.Generic.List[string]
$summaries = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Get-ContractPass([object]$Report, [string]$Name) {
    if ($null -eq $Report.frame_contract -or $null -eq $Report.frame_contract.passes) {
        return $null
    }
    foreach ($pass in @($Report.frame_contract.passes)) {
        if ([string]$pass.name -eq $Name) {
            return $pass
        }
    }
    return $null
}

function Get-ContractResource([object]$Report, [string]$Name) {
    if ($null -eq $Report.frame_contract -or $null -eq $Report.frame_contract.resources) {
        return $null
    }
    foreach ($resource in @($Report.frame_contract.resources)) {
        if ([string]$resource.name -eq $Name) {
            return $resource
        }
    }
    return $null
}

function Test-ListContains([object]$Values, [string]$Needle) {
    if ($null -eq $Values) {
        return $false
    }
    foreach ($value in @($Values)) {
        if ([string]$value -eq $Needle) {
            return $true
        }
    }
    return $false
}

function Invoke-CortexEngine([string[]]$Arguments, [string]$RunLogPath) {
    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $output | Out-File -FilePath $RunLogPath -Encoding utf8
        return [int]$exitCode
    } finally {
        Pop-Location
    }
}

function Clear-FixtureEnvironment {
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DEBUG_CULLING -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_RT -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOW_GPU_PRIORITY -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_FRAME_PACE_MS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SUPPRESS_CAMERA_HELP -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SUPPRESS_FATAL_DIALOG -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_HEADLESS -ErrorAction SilentlyContinue
}

for ($runIndex = 1; $runIndex -le $Runs; ++$runIndex) {
    $runDir = Join-Path $activeRoot ("run{0}" -f $runIndex)
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null

    $reportPath = Join-Path $runDir "frame_report_last.json"
    $shutdownReportPath = Join-Path $runDir "frame_report_shutdown.json"
    $visualPath = Join-Path $runDir "visual_validation_rt_showcase.bmp"
    $runLogPath = Join-Path $runDir "cortex_last_run.txt"
    $stdoutPath = Join-Path $runDir "fixture_stdout.txt"
    Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $visualPath, $runLogPath, $stdoutPath

    $env:PATH = "$vcpkgBin;$env:PATH"
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_DEBUG_CULLING = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = [string]$VisualValidationMinFrame
    $env:CORTEX_LOG_DIR = $runDir
    $env:CORTEX_LOW_GPU_PRIORITY = "1"
    $env:CORTEX_FRAME_PACE_MS = "20"
    $env:CORTEX_SUPPRESS_CAMERA_HELP = "1"
    $env:CORTEX_SUPPRESS_FATAL_DIALOG = "1"
    $env:CORTEX_HEADLESS = "1"
    Remove-Item Env:\CORTEX_DISABLE_RT -ErrorAction SilentlyContinue

    $exitCode = Invoke-CortexEngine @(
        "--scene", "rt_showcase",
        "--camera-bookmark", $CameraBookmark,
        "--mode=default",
        "--no-llm",
        "--no-dreamer",
        "--no-launcher",
        "--smoke-frames=$SmokeFrames",
        "--max-expected-frames=$MaxExpectedFrames",
        "--exit-after-visual-validation"
    ) $stdoutPath

    Clear-FixtureEnvironment

    if ($exitCode -ne 0) {
        Add-Failure "run $runIndex process exit code $exitCode"
        continue
    }
    if (-not (Test-Path $reportPath)) {
        Add-Failure "run $runIndex did not write frame report: $reportPath"
        continue
    }
    if (-not (Test-Path $visualPath)) {
        Add-Failure "run $runIndex did not write visual capture: $visualPath"
        continue
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $stats = $report.visual_validation.image_stats
    $rt = $report.frame_contract.ray_tracing
    $features = $report.frame_contract.features
    $passSummary = $report.frame_contract.pass_budget_summary

    if ([string]$report.lifecycle -ne "active_frame") {
        Add-Failure "run $runIndex lifecycle is '$($report.lifecycle)', expected active_frame"
    }
    if ($null -ne $report.health_warnings -and @($report.health_warnings).Count -ne 0) {
        Add-Failure "run $runIndex health_warnings is not empty: $($report.health_warnings -join ', ')"
    }
    if (-not [bool]$report.visual_validation.captured -or -not [bool]$stats.valid) {
        Add-Failure "run $runIndex visual validation was not captured with valid image stats"
    }
    if ([double]$stats.nonblack_ratio -lt $MinVisualNonBlackRatio) {
        Add-Failure "run $runIndex nonblack_ratio=$($stats.nonblack_ratio), expected >= $MinVisualNonBlackRatio"
    }
    if ([double]$stats.avg_luma -lt $MinVisualAvgLuma) {
        Add-Failure "run $runIndex avg_luma=$($stats.avg_luma), expected >= $MinVisualAvgLuma"
    }
    if ([double]$stats.center_avg_luma -lt $MinVisualCenterLuma) {
        Add-Failure "run $runIndex center_avg_luma=$($stats.center_avg_luma), expected >= $MinVisualCenterLuma"
    }
    if ((Get-Item $visualPath).Length -lt 1048576) {
        Add-Failure "run $runIndex visual capture is unexpectedly small: $((Get-Item $visualPath).Length) bytes"
    }

    if (-not [bool]$features.ray_tracing_enabled -or -not [bool]$rt.enabled) {
        Add-Failure "run $runIndex ray tracing is not enabled in the frame contract"
    }
    if (-not [bool]$features.ssao_enabled) {
        Add-Failure "run $runIndex SSAO feature flag is not enabled"
    }
    if (-not [bool]$rt.scheduler_enabled) {
        Add-Failure "run $runIndex RT scheduler is not enabled"
    }
    if (-not [bool]$rt.scheduler_build_tlas) {
        Add-Failure "run $runIndex RT scheduler did not plan TLAS build"
    }
    if (-not [bool]$rt.dispatch_shadows) {
        Add-Failure "run $runIndex RT shadow dispatch was not planned"
    }
    if ([int]$rt.tlas_instances -lt $MinTLASInstances) {
        Add-Failure "run $runIndex TLAS instances=$($rt.tlas_instances), expected >= $MinTLASInstances"
    }
    if ([int]$passSummary.ray_tracing_passes -lt $MinRayTracingPasses) {
        Add-Failure "run $runIndex ray-tracing pass count=$($passSummary.ray_tracing_passes), expected >= $MinRayTracingPasses"
    }

    $rtShadows = Get-ContractPass $report "RTShadowsGI"
    if ($null -eq $rtShadows -or -not [bool]$rtShadows.planned -or -not [bool]$rtShadows.executed) {
        Add-Failure "run $runIndex RTShadowsGI pass was not planned and executed"
    } elseif (-not (Test-ListContains $rtShadows.writes "rt_shadow_mask") -or
              -not (Test-ListContains $rtShadows.writes "acceleration_structures")) {
        Add-Failure "run $runIndex RTShadowsGI pass did not write rt_shadow_mask and acceleration_structures"
    }

    $ssaoPass = Get-ContractPass $report "SSAO"
    if ($null -eq $ssaoPass -or -not [bool]$ssaoPass.planned -or -not [bool]$ssaoPass.executed) {
        Add-Failure "run $runIndex SSAO pass was not planned and executed"
    } elseif (-not (Test-ListContains $ssaoPass.writes "ssao")) {
        Add-Failure "run $runIndex SSAO pass did not write ssao"
    }

    $postProcess = Get-ContractPass $report "PostProcess"
    if ($null -eq $postProcess -or -not [bool]$postProcess.executed) {
        Add-Failure "run $runIndex PostProcess pass was not executed"
    } elseif (-not (Test-ListContains $postProcess.reads "ssao")) {
        Add-Failure "run $runIndex PostProcess did not read ssao"
    }

    foreach ($resourceName in @("ssao", "rt_shadow_mask", "rt_reflection")) {
        $resource = Get-ContractResource $report $resourceName
        if ($null -eq $resource -or -not [bool]$resource.valid) {
            Add-Failure "run $runIndex resource '$resourceName' is missing or invalid"
        }
    }

    $summaries.Add([pscustomobject]@{
        run = $runIndex
        log_dir = $runDir
        visual = $visualPath
        report = $reportPath
        nonblack_ratio = [double]$stats.nonblack_ratio
        avg_luma = [double]$stats.avg_luma
        center_avg_luma = [double]$stats.center_avg_luma
        rt_enabled = [bool]$rt.enabled
        ssao_enabled = [bool]$features.ssao_enabled
        tlas_instances = [int]$rt.tlas_instances
        ray_tracing_passes = [int]$passSummary.ray_tracing_passes
    }) | Out-Null
}

if ($summaries.Count -gt 1) {
    $baseline = $summaries[0]
    foreach ($summary in @($summaries | Select-Object -Skip 1)) {
        $avgDelta = [Math]::Abs([double]$summary.avg_luma - [double]$baseline.avg_luma)
        $nonBlackDelta = [Math]::Abs([double]$summary.nonblack_ratio - [double]$baseline.nonblack_ratio)
        if ($avgDelta -gt $MaxRunAvgLumaDelta) {
            Add-Failure "run $($summary.run) avg_luma delta=$avgDelta vs run 1, expected <= $MaxRunAvgLumaDelta"
        }
        if ($nonBlackDelta -gt $MaxRunNonBlackDelta) {
            Add-Failure "run $($summary.run) nonblack delta=$nonBlackDelta vs run 1, expected <= $MaxRunNonBlackDelta"
        }
    }
}

$summaryPath = Join-Path $activeRoot "rt_nonblack_fixture_summary.json"
$summaryRows = @($summaries | ForEach-Object { $_ })
$failureRows = @($failures | ForEach-Object { [string]$_ })
[pscustomobject]@{
    passed = ($failures.Count -eq 0)
    runs = $summaryRows
    failures = $failureRows
    thresholds = [pscustomobject]@{
        min_tlas_instances = $MinTLASInstances
        min_ray_tracing_passes = $MinRayTracingPasses
        min_visual_nonblack_ratio = $MinVisualNonBlackRatio
        min_visual_avg_luma = $MinVisualAvgLuma
        min_visual_center_luma = $MinVisualCenterLuma
        max_run_avg_luma_delta = $MaxRunAvgLumaDelta
        max_run_nonblack_delta = $MaxRunNonBlackDelta
    }
} | ConvertTo-Json -Depth 8 | Out-File -FilePath $summaryPath -Encoding utf8

if ($failures.Count -gt 0) {
    Write-Host "RT nonblack fixture failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host " logs=$activeRoot"
    Write-Host " summary=$summaryPath"
    exit 1
}

Write-Host "RT nonblack fixture passed" -ForegroundColor Green
Write-Host " logs=$activeRoot"
Write-Host " summary=$summaryPath"
foreach ($summary in $summaries) {
    Write-Host (" run={0} nonblack={1:N3} avg_luma={2:N2} center_luma={3:N2} tlas={4} rt_passes={5} visual={6}" -f `
        [int]$summary.run,
        [double]$summary.nonblack_ratio,
        [double]$summary.avg_luma,
        [double]$summary.center_avg_luma,
        [int]$summary.tlas_instances,
        [int]$summary.ray_tracing_passes,
        [string]$summary.visual)
}
exit 0
