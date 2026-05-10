param(
    [int]$SmokeFrames = 120,
    [double]$MaxGpuFrameMs = 50.0,
    [double]$MinVisualAvgLuma = 5.0,
    [double]$MinVisualNonBlackRatio = 0.10,
    [int]$MinVisibleRenderables = 7,
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
$activeLogDir = $baseLogDir
if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    $activeLogDir = $LogDir
    $env:CORTEX_LOG_DIR = $activeLogDir
} elseif ($IsolatedLogs) {
    $runId = "voxel_backend_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
    $env:CORTEX_LOG_DIR = $activeLogDir
}
$reportPath = Join-Path $activeLogDir "frame_report_last.json"
$shutdownReportPath = Join-Path $activeLogDir "frame_report_shutdown.json"
$runLogPath = Join-Path $activeLogDir "cortex_last_run.txt"
$visualPath = Join-Path $activeLogDir "visual_validation_rt_showcase.bmp"

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

$exeWorkingDir = Split-Path -Parent $exe
function Invoke-CortexEngine([string[]]$Arguments) {
    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $output | ForEach-Object { Write-Host $_ }
        return [int]$exitCode
    } finally {
        Pop-Location
    }
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath

$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

try {
    $exitCode = Invoke-CortexEngine @("--scene", "temporal_validation", "--mode=default", "--backend=voxel", "--no-llm", "--no-dreamer", "--no-launcher", "--smoke-frames=$SmokeFrames", "--exit-after-visual-validation")
} finally {
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
}

if ($exitCode -ne 0) {
    throw "CortexEngine voxel backend process failed with exit code $exitCode"
}
if (-not (Test-Path $reportPath)) {
    if (Test-Path $shutdownReportPath) {
        $reportPath = $shutdownReportPath
    } else {
        throw "Expected frame report was not written: $reportPath"
    }
}

$report = Get-Content $reportPath -Raw | ConvertFrom-Json
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Get-FrameContractPass([object]$reportObject, [string]$name) {
    if ($null -eq $reportObject.frame_contract.passes) {
        return $null
    }
    foreach ($pass in $reportObject.frame_contract.passes) {
        if ([string]$pass.name -eq $name) {
            return $pass
        }
    }
    return $null
}

function Test-LogContains([string]$pattern) {
    if (-not (Test-Path $runLogPath)) {
        return $false
    }
    return [bool](Select-String -Path $runLogPath -Pattern $pattern -SimpleMatch -Quiet)
}

if ([string]$report.scene -ne "temporal_validation") {
    Add-Failure "expected temporal_validation scene but report scene was '$($report.scene)'"
}
if ($report.health_warnings.Count -gt 0) {
    Add-Failure "health warnings were reported: $($report.health_warnings -join ', ')"
}
if ($report.frame_contract.warnings.Count -gt 0) {
    Add-Failure "frame contract warnings were reported: $($report.frame_contract.warnings -join ', ')"
}
if ([double]$report.gpu_frame_ms -gt $MaxGpuFrameMs) {
    Add-Failure "GPU frame time exceeded voxel budget: $($report.gpu_frame_ms) ms > $MaxGpuFrameMs ms"
}

$features = $report.frame_contract.features
$planned = $report.frame_contract.planned_features
$executed = $report.frame_contract.executed_features
if ($null -eq $features -or -not [bool]$features.voxel_backend_enabled) {
    Add-Failure "frame contract features do not report voxel_backend_enabled=true"
}
if ($null -eq $planned -or -not [bool]$planned.voxel_backend_enabled) {
    Add-Failure "frame contract planned_features do not report voxel_backend_enabled=true"
}
if ($null -eq $executed -or -not [bool]$executed.voxel_backend_enabled) {
    Add-Failure "frame contract executed_features do not report voxel_backend_enabled=true"
}
if ($null -ne $executed -and [bool]$executed.ray_tracing_enabled) {
    Add-Failure "voxel backend unexpectedly executed ray tracing"
}

$renderables = $report.frame_contract.renderables
if ($null -eq $renderables) {
    Add-Failure "frame contract does not report renderable classification"
} elseif ([int]$renderables.visible -lt $MinVisibleRenderables) {
    Add-Failure "visible renderable count is too low: $($renderables.visible)"
}

$voxelPass = Get-FrameContractPass $report "RenderVoxel"
if ($null -eq $voxelPass) {
    Add-Failure "RenderVoxel pass was not recorded"
} elseif ([bool]$voxelPass.fallback_used) {
    Add-Failure "RenderVoxel pass reported fallback execution"
}

if (-not [bool]$report.visual_validation.captured) {
    Add-Failure "visual validation capture was not written"
}
if ($null -eq $report.visual_validation.image_stats -or -not [bool]$report.visual_validation.image_stats.valid) {
    Add-Failure "visual validation image stats are invalid: $($report.visual_validation.image_stats.reason)"
} else {
    if ([double]$report.visual_validation.image_stats.avg_luma -lt $MinVisualAvgLuma) {
        Add-Failure "visual validation average luma is $($report.visual_validation.image_stats.avg_luma), expected >= $MinVisualAvgLuma"
    }
    if ([double]$report.visual_validation.image_stats.nonblack_ratio -lt $MinVisualNonBlackRatio) {
        Add-Failure "visual validation nonblack ratio is $($report.visual_validation.image_stats.nonblack_ratio), expected >= $MinVisualNonBlackRatio"
    }
}

if (-not (Test-LogContains "Render backend: VoxelExperimental")) {
    Add-Failure "run log does not confirm VoxelExperimental backend selection"
}
if (-not (Test-LogContains "RenderVoxel: voxel backend active")) {
    Add-Failure "run log does not confirm active RenderVoxel execution"
}
if (-not (Test-LogContains "Voxel grid built: dim=")) {
    Add-Failure "run log does not confirm voxel grid construction"
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Voxel backend smoke failed with $($failures.Count) issue(s)."
}

Write-Host (
    "Voxel backend smoke passed: " +
    "gpu_ms=$([Math]::Round([double]$report.gpu_frame_ms, 3)) " +
    "visible=$($renderables.visible) " +
    "avg_luma=$([Math]::Round([double]$report.visual_validation.image_stats.avg_luma, 2)) " +
    "nonblack=$([Math]::Round([double]$report.visual_validation.image_stats.nonblack_ratio, 3))"
)
Write-Host " logs=$activeLogDir"
