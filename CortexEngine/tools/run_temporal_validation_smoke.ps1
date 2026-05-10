param(
    [int]$SmokeFrames = 140,
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MinTemporalDisocclusionRatio = 0.0005,
    [double]$MinTemporalHighMotionRatio = 0.00001,
    [double]$MaxTemporalOutOfBoundsRatio = 0.20,
    [int]$MinVisibleRenderables = 7,
    [int]$MinMotionVectorInstances = 6,
    [int]$MaxTextureUploadPending = 0,
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
    $runId = "temporal_validation_{0}_{1}_{2}" -f `
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
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "60"

try {
    $exitCode = Invoke-CortexEngine @("--scene", "temporal_validation", "--mode=default", "--no-llm", "--no-dreamer", "--no-launcher", "--smoke-frames=$SmokeFrames", "--exit-after-visual-validation")
} finally {
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
}

if ($exitCode -ne 0) {
    throw "CortexEngine temporal validation process failed with exit code $exitCode"
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

function Get-FrameContractResource([object]$reportObject, [string]$name) {
    if ($null -eq $reportObject.frame_contract.resources) {
        return $null
    }
    foreach ($resource in $reportObject.frame_contract.resources) {
        if ([string]$resource.name -eq $name) {
            return $resource
        }
    }
    return $null
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

function Test-PassReads([object]$pass, [string]$resourceName) {
    if ($null -eq $pass -or $null -eq $pass.reads) {
        return $false
    }
    foreach ($read in $pass.reads) {
        if ([string]$read -eq $resourceName) {
            return $true
        }
    }
    return $false
}

function Test-PassWrites([object]$pass, [string]$resourceName) {
    if ($null -eq $pass -or $null -eq $pass.writes) {
        return $false
    }
    foreach ($write in $pass.writes) {
        if ([string]$write -eq $resourceName) {
            return $true
        }
    }
    return $false
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
    Add-Failure "GPU frame time exceeded budget: $($report.gpu_frame_ms) ms > $MaxGpuFrameMs ms"
}
if ($null -eq $report.frame_contract.renderer_budget) {
    Add-Failure "renderer budget plan is missing from frame contract"
} elseif ([string]::IsNullOrWhiteSpace([string]$report.frame_contract.renderer_budget.profile)) {
    Add-Failure "renderer budget profile is missing"
}
if ($null -eq $report.texture_upload_queue) {
    Add-Failure "frame report does not contain texture upload queue diagnostics"
} else {
    $textureSubmitted = [int64]$report.texture_upload_queue.submitted
    $textureCompleted = [int64]$report.texture_upload_queue.completed
    $textureFailed = [int64]$report.texture_upload_queue.failed
    $texturePending = [int64]$report.texture_upload_queue.pending
    if ($textureFailed -ne 0) {
        Add-Failure "texture upload queue reported failed jobs: submitted=$textureSubmitted completed=$textureCompleted failed=$textureFailed pending=$texturePending"
    }
    if ($texturePending -gt $MaxTextureUploadPending) {
        Add-Failure "texture upload queue pending jobs is $texturePending, budget is <= $MaxTextureUploadPending"
    }
}

$temporalMaskResource = Get-FrameContractResource $report "temporal_rejection_mask"
$temporalStatsResource = Get-FrameContractResource $report "temporal_rejection_mask_stats"
if ($null -eq $temporalMaskResource) {
    Add-Failure "frame contract does not include temporal_rejection_mask resource"
}
if ($null -eq $temporalStatsResource) {
    Add-Failure "frame contract does not include temporal_rejection_mask_stats resource"
}

$motionVectors = $report.frame_contract.motion_vectors
if ($null -eq $motionVectors -or -not [bool]$motionVectors.executed) {
    Add-Failure "motion vectors did not execute"
} else {
    if (-not [bool]$motionVectors.visibility_buffer_motion) {
        Add-Failure "motion vectors did not use the visibility-buffer path"
    }
    if ([bool]$motionVectors.camera_only_fallback) {
        Add-Failure "motion vectors used the camera-only fallback"
    }
    if ([int]$motionVectors.instance_count -lt $MinMotionVectorInstances) {
        Add-Failure "motion vector instance count is too low: $($motionVectors.instance_count)"
    }
    if ([double]$motionVectors.max_object_motion_world -le 0.02) {
        Add-Failure "object motion is too weak for temporal validation: $($motionVectors.max_object_motion_world)"
    }
}

$temporalMask = $report.frame_contract.temporal_mask
if ($null -eq $temporalMask) {
    Add-Failure "frame contract does not report temporal_mask statistics"
} else {
    if (-not [bool]$temporalMask.built) {
        Add-Failure "temporal rejection mask was not built"
    }
    if (-not [bool]$temporalMask.valid) {
        Add-Failure "temporal rejection mask statistics are not valid"
    }
    if ([int]$temporalMask.pixel_count -le 0) {
        Add-Failure "temporal rejection mask statistics have zero pixels"
    }
    foreach ($field in @("accepted_ratio", "disocclusion_ratio", "high_motion_ratio", "out_of_bounds_ratio")) {
        $value = [double]$temporalMask.$field
        if ($value -lt 0.0 -or $value -gt 1.0) {
            Add-Failure "temporal_mask $field is outside [0,1]: $value"
        }
    }
    if ([double]$temporalMask.disocclusion_ratio -lt $MinTemporalDisocclusionRatio) {
        Add-Failure "temporal disocclusion ratio is too low: $($temporalMask.disocclusion_ratio)"
    }
    if ([double]$temporalMask.high_motion_ratio -lt $MinTemporalHighMotionRatio) {
        Add-Failure "temporal high-motion ratio is too low: $($temporalMask.high_motion_ratio)"
    }
    if ([double]$temporalMask.out_of_bounds_ratio -gt $MaxTemporalOutOfBoundsRatio) {
        Add-Failure "temporal out-of-bounds ratio is too high: $($temporalMask.out_of_bounds_ratio)"
    }
}

$renderables = $report.frame_contract.renderables
if ($null -eq $renderables) {
    Add-Failure "frame contract does not report renderable classification"
} else {
    if ([int]$renderables.visible -lt $MinVisibleRenderables) {
        Add-Failure "visible renderable count is too low: $($renderables.visible)"
    }
    if ([int]$renderables.double_sided_alpha_tested_depth_writing -lt 1) {
        Add-Failure "temporal scene lacks a double-sided alpha-tested depth-writing surface"
    }
    if ([int]$renderables.water_depth_tested_no_write -lt 1) {
        Add-Failure "temporal scene lacks a water surface"
    }
    if ([int]$renderables.emissive -lt 1) {
        Add-Failure "temporal scene lacks an emissive renderable"
    }
    if ([int]$renderables.metallic -lt 1) {
        Add-Failure "temporal scene lacks a metallic renderable"
    }
}

$temporalMaskPass = Get-FrameContractPass $report "TemporalRejectionMask"
$temporalStatsPass = Get-FrameContractPass $report "TemporalRejectionMaskStats"
if ($null -eq $temporalMaskPass) {
    Add-Failure "TemporalRejectionMask pass was not recorded"
} else {
    if (-not [bool]$temporalMaskPass.render_graph) {
        Add-Failure "TemporalRejectionMask pass is not graph-owned"
    }
    if ([bool]$temporalMaskPass.fallback_used) {
        Add-Failure "TemporalRejectionMask pass used fallback execution"
    }
    if (-not (Test-PassWrites $temporalMaskPass "temporal_rejection_mask")) {
        Add-Failure "TemporalRejectionMask pass does not write temporal_rejection_mask"
    }
}
if ($null -eq $temporalStatsPass) {
    Add-Failure "TemporalRejectionMaskStats pass was not recorded"
} else {
    if ([bool]$temporalStatsPass.fallback_used) {
        Add-Failure "TemporalRejectionMaskStats pass used fallback execution"
    }
    if (-not (Test-PassReads $temporalStatsPass "temporal_rejection_mask")) {
        Add-Failure "TemporalRejectionMaskStats pass does not read temporal_rejection_mask"
    }
    if (-not (Test-PassWrites $temporalStatsPass "temporal_rejection_mask_stats")) {
        Add-Failure "TemporalRejectionMaskStats pass does not write temporal_rejection_mask_stats"
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Temporal validation smoke failed with $($failures.Count) issue(s)."
}

Write-Host (
    "Temporal validation smoke passed: " +
    "gpu_ms=$([Math]::Round([double]$report.gpu_frame_ms, 3)) " +
    "disocclusion=$([Math]::Round([double]$temporalMask.disocclusion_ratio, 6)) " +
    "high_motion=$([Math]::Round([double]$temporalMask.high_motion_ratio, 6)) " +
    "object_motion=$([Math]::Round([double]$motionVectors.max_object_motion_world, 4)) " +
    "visible=$($renderables.visible) " +
    "warnings=0"
)
Write-Host " logs=$activeLogDir"
