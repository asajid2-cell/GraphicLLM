param(
    [int]$SmokeFrames = 240,
    [double]$MinAvgLuma = 20.0,
    [double]$MinNonBlackRatio = 0.85,
    [double]$MinColorfulRatio = 0.20,
    [double]$MaxSaturatedRatio = 0.12,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "rt_gi_visual_signal_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$reportPath = Join-Path $LogDir "frame_report_last.json"
$visualPath = Join-Path $LogDir "visual_validation_rt_showcase.bmp"
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $visualPath, (Join-Path $LogDir "frame_report_shutdown.json"), (Join-Path $LogDir "cortex_last_run.txt")

$previousLogDir = $env:CORTEX_LOG_DIR
$previousCapture = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
$previousDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
$previousDebugCulling = $env:CORTEX_DEBUG_CULLING
$previousMinFrame = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
$previousDebugView = $env:CORTEX_DEBUG_VIEW

try {
    $env:CORTEX_LOG_DIR = $LogDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_DEBUG_CULLING = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"
    $env:CORTEX_DEBUG_VIEW = "21"

    Push-Location (Split-Path -Parent $exe)
    try {
        $output = & $exe --scene rt_showcase --camera-bookmark hero --mode=default --no-llm --no-dreamer --no-launcher "--smoke-frames=$SmokeFrames" --exit-after-visual-validation 2>&1
        $exitCode = $LASTEXITCODE
        $output | ForEach-Object { Write-Host $_ }
    } finally {
        Pop-Location
    }
} finally {
    if ($null -eq $previousLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $previousLogDir }
    if ($null -eq $previousCapture) { Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue } else { $env:CORTEX_CAPTURE_VISUAL_VALIDATION = $previousCapture }
    if ($null -eq $previousDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $previousDebugLayer }
    if ($null -eq $previousDebugCulling) { Remove-Item Env:\CORTEX_DEBUG_CULLING -ErrorAction SilentlyContinue } else { $env:CORTEX_DEBUG_CULLING = $previousDebugCulling }
    if ($null -eq $previousMinFrame) { Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue } else { $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = $previousMinFrame }
    if ($null -eq $previousDebugView) { Remove-Item Env:\CORTEX_DEBUG_VIEW -ErrorAction SilentlyContinue } else { $env:CORTEX_DEBUG_VIEW = $previousDebugView }
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

if ($exitCode -ne 0) {
    Add-Failure "CortexEngine exited with $exitCode"
}
if (-not (Test-Path $reportPath)) {
    Add-Failure "frame report missing: $reportPath"
}
if (-not (Test-Path $visualPath)) {
    Add-Failure "visual capture missing: $visualPath"
}

function Get-ContractPass([object]$report, [string]$name) {
    foreach ($pass in $report.frame_contract.passes) {
        if ([string]$pass.name -eq $name) { return $pass }
    }
    return $null
}

function Get-ContractResource([object]$report, [string]$name) {
    foreach ($resource in $report.frame_contract.resources) {
        if ([string]$resource.name -eq $name) { return $resource }
    }
    return $null
}

function Get-ContractHistory([object]$report, [string]$name) {
    foreach ($history in $report.frame_contract.histories) {
        if ([string]$history.name -eq $name) { return $history }
    }
    return $null
}

if (Test-Path $reportPath) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $fc = $report.frame_contract
    $rt = $fc.ray_tracing
    $stats = $report.visual_validation.image_stats

    if ([int]$report.renderer.debug_view_mode -ne 21) {
        Add-Failure "debug view was $($report.renderer.debug_view_mode), expected 21"
    }
    if (-not [bool]$report.visual_validation.captured -or -not [bool]$stats.valid) {
        Add-Failure "RT GI debug capture is not valid"
    } else {
        if ([double]$stats.avg_luma -lt $MinAvgLuma) {
            Add-Failure "RT GI debug avg luma $($stats.avg_luma) < $MinAvgLuma"
        }
        if ([double]$stats.nonblack_ratio -lt $MinNonBlackRatio) {
            Add-Failure "RT GI debug nonblack ratio $($stats.nonblack_ratio) < $MinNonBlackRatio"
        }
        if ([double]$stats.colorful_ratio -lt $MinColorfulRatio) {
            Add-Failure "RT GI debug colorful ratio $($stats.colorful_ratio) < $MinColorfulRatio"
        }
        if ([double]$stats.saturated_ratio -gt $MaxSaturatedRatio) {
            Add-Failure "RT GI debug saturated ratio $($stats.saturated_ratio) > $MaxSaturatedRatio"
        }
    }

    if ($fc.warnings.Count -ne 0) {
        Add-Failure "frame contract warnings are present: $($fc.warnings -join ', ')"
    }
    if (-not [bool]$rt.enabled -or -not [bool]$fc.features.rt_gi_enabled) {
        Add-Failure "RT GI feature is not active"
    }
    if (-not [bool]$rt.scheduler_enabled -or -not [bool]$rt.dispatch_gi) {
        Add-Failure "RT scheduler did not dispatch GI"
    }
    if (-not [bool]$rt.denoise_gi) {
        Add-Failure "RT scheduler did not denoise GI"
    }
    if ([int]$rt.gi_width -le 0 -or [int]$rt.gi_height -le 0) {
        Add-Failure "RT GI dimensions are invalid: $($rt.gi_width)x$($rt.gi_height)"
    }

    $gi = Get-ContractResource $report "rt_gi"
    $giHistory = Get-ContractResource $report "rt_gi_history"
    foreach ($resource in @($gi, $giHistory)) {
        if ($null -eq $resource) {
            Add-Failure "RT GI resource missing from frame contract"
            continue
        }
        if (-not [bool]$resource.valid -or -not [bool]$resource.size_matches_contract) {
            Add-Failure "RT GI resource '$($resource.name)' invalid or size-mismatched"
        }
    }

    $history = Get-ContractHistory $report "rt_gi"
    if ($null -eq $history -or -not [bool]$history.valid -or -not [bool]$history.resource_valid -or -not [bool]$history.seeded) {
        Add-Failure "RT GI temporal history is not valid/seeded"
    }
    if ($null -ne $history) {
        foreach ($field in @("uses_depth_normal_rejection", "uses_disocclusion_rejection", "uses_velocity_reprojection")) {
            if (-not [bool]$history.$field) {
                Add-Failure "RT GI history missing rejection mode: $field"
            }
        }
    }

    $rtPass = Get-ContractPass $report "RTShadowsGI"
    if ($null -eq $rtPass -or -not [bool]$rtPass.executed -or ($rtPass.writes -notcontains "rt_gi")) {
        Add-Failure "RTShadowsGI pass did not execute/write rt_gi"
    }
    $denoisePass = Get-ContractPass $report "RTDenoise"
    if ($null -eq $denoisePass -or -not [bool]$denoisePass.executed -or
        ($denoisePass.reads -notcontains "rt_gi") -or
        ($denoisePass.reads -notcontains "rt_gi_history") -or
        ($denoisePass.writes -notcontains "rt_gi_history")) {
        Add-Failure "RTDenoise pass did not read/write RT GI history"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "RT GI visual signal tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host " logs=$LogDir"
    exit 1
}

Write-Host "RT GI visual signal tests passed" -ForegroundColor Green
Write-Host " logs=$LogDir"
Write-Host (" gi={0}x{1} luma={2:N2}/{3:N1} nonblack={4:N3}/{5:N2} colorful={6:N3}/{7:N2} saturated={8:N3}/{9:N2}" -f `
    [int]$rt.gi_width,
    [int]$rt.gi_height,
    [double]$stats.avg_luma,
    $MinAvgLuma,
    [double]$stats.nonblack_ratio,
    $MinNonBlackRatio,
    [double]$stats.colorful_ratio,
    $MinColorfulRatio,
    [double]$stats.saturated_ratio,
    $MaxSaturatedRatio)
