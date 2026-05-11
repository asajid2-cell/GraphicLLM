param(
    [int]$SmokeFrames = 240,
    [int]$MaxExpectedFrames = 45,
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MaxDxgiMemoryMb = 512.0,
    [double]$MaxEstimatedMemoryMb = 256.0,
    [double]$MaxRenderTargetMemoryMb = 160.0,
    [double]$MaxEstimatedWriteMb = 128.0,
    [double]$MaxAlbedoLuminance = 1.0,
    [int]$TemporalRuns = 2,
    [double]$MaxTemporalLumaDelta = 3.0,
    [double]$MaxTemporalNonBlackDelta = 0.02,
    [double]$MaxTemporalMeanAbsLumaDelta = 2.5,
    [double]$MaxTemporalChangedPixelRatio = 0.08,
    [int]$MaxPersistentDescriptors = 1024,
    [int]$MaxStagingDescriptors = 128,
    [int]$MaxTextureUploadPending = 0,
    [int]$MinRTTLASInstances = 16,
    [double]$MaxRTReflectionSignalOutlierRatio = 0.02,
    [double]$MaxRTReflectionHistorySignalOutlierRatio = 0.02,
    [int]$MinOpaqueDepthWriting = 16,
    [int]$MinDoubleSidedOpaqueDepthWriting = 1,
    [int]$MinTransparentDepthTested = 1,
    [int]$MinWaterDepthTestedNoWrite = 1,
    [int]$MaxWaterDraws = 0,
    [double]$MaxVisualSaturatedRatio = 0.12,
    [double]$MaxVisualNearWhiteRatio = 0.14,
    [double]$MaxVisualCenterLuma = 210.0,
    [double]$MinVisualAvgLuma = 60.0,
    [double]$MinVisualCenterLuma = 60.0,
    [double]$MaxVisualDarkDetailRatio = 0.68,
    [double]$MaxCameraPositionError = 0.25,
    [string]$CameraBookmark = "hero",
    [bool]$ValidateSurfaceDebug = $true,
    [switch]$SkipSurfaceDebug,
    [int]$SurfaceDebugView = 41,
    [double]$MinSurfaceDebugColorfulRatio = 0.25,
    [double]$MinSurfaceDebugNonBlackRatio = 0.95,
    [string]$RTBudgetProfile = "",
    [string]$ExpectedRTBudgetProfile = "",
    [switch]$AllowRTCadenceSkips,
    [switch]$AllowStartupRenderTargetReallocation,
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

if ($SkipSurfaceDebug) {
    $ValidateSurfaceDebug = $false
}

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
$activeLogDir = $baseLogDir
if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    $activeLogDir = $LogDir
    $env:CORTEX_LOG_DIR = $activeLogDir
} elseif ($IsolatedLogs) {
    $runId = "rt_showcase_{0}_{1}_{2}" -f `
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
$mainVisualPreservedPath = Join-Path $activeLogDir "visual_validation_rt_showcase_main.bmp"
$temporalBaselineVisualPath = Join-Path $activeLogDir "visual_validation_rt_showcase_temporal_baseline.bmp"
$surfaceDebugVisualPath = Join-Path $activeLogDir "visual_validation_rt_showcase_surface_class.bmp"
$surfaceDebugReportPath = Join-Path $activeLogDir "frame_report_surface_debug.json"

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
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath, $mainVisualPreservedPath, $temporalBaselineVisualPath, $surfaceDebugVisualPath, $surfaceDebugReportPath
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $activeLogDir "visual_validation_rt_showcase_temporal_run*.bmp")

$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_DEBUG_CULLING = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"
if (-not [string]::IsNullOrWhiteSpace($RTBudgetProfile)) {
    $env:CORTEX_RT_BUDGET_PROFILE = $RTBudgetProfile
}

$exitCode = Invoke-CortexEngine @("--scene", "rt_showcase", "--camera-bookmark", $CameraBookmark, "--mode=default", "--no-llm", "--no-dreamer", "--no-launcher", "--smoke-frames=$SmokeFrames", "--exit-after-visual-validation")

Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
Remove-Item Env:\CORTEX_DEBUG_CULLING -ErrorAction SilentlyContinue
Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
Remove-Item Env:\CORTEX_RT_BUDGET_PROFILE -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    throw "CortexEngine smoke process failed with exit code $exitCode"
}
if (-not (Test-Path $reportPath)) {
    throw "Expected frame report was not written: $reportPath"
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

function Get-FrameContractHistory([object]$reportObject, [string]$name) {
    if ($null -eq $reportObject.frame_contract.histories) {
        return $null
    }
    foreach ($history in $reportObject.frame_contract.histories) {
        if ([string]$history.name -eq $name) {
            return $history
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

function Get-BmpInfo([string]$Path) {
    if (-not (Test-Path $Path)) {
        return [pscustomobject]@{ valid = $false; reason = "missing_file" }
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 54 -or $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4d) {
        return [pscustomobject]@{ valid = $false; reason = "not_bmp" }
    }

    $dataOffset = [BitConverter]::ToUInt32($bytes, 10)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $heightSigned = [BitConverter]::ToInt32($bytes, 22)
    $planes = [BitConverter]::ToUInt16($bytes, 26)
    $bpp = [BitConverter]::ToUInt16($bytes, 28)
    $compression = [BitConverter]::ToUInt32($bytes, 30)
    if ($width -le 0 -or $heightSigned -eq 0 -or $planes -ne 1 -or
        ($bpp -ne 24 -and $bpp -ne 32) -or $compression -ne 0) {
        return [pscustomobject]@{ valid = $false; reason = "unsupported_bmp" }
    }

    $height = [Math]::Abs($heightSigned)
    $bytesPerPixel = [int]($bpp / 8)
    $rowStride = [int]([Math]::Floor(((($width * $bytesPerPixel) + 3) / 4.0))) * 4
    $requiredSize = [int64]$dataOffset + ([int64]$rowStride * [int64]$height)
    if ($dataOffset -ge $bytes.Length -or $requiredSize -gt $bytes.Length) {
        return [pscustomobject]@{ valid = $false; reason = "truncated_pixels" }
    }

    return [pscustomobject]@{
        valid = $true
        reason = ""
        bytes = $bytes
        data_offset = [int]$dataOffset
        width = [int]$width
        height = [int]$height
        bytes_per_pixel = $bytesPerPixel
        row_stride = $rowStride
    }
}

function Measure-BmpLumaDifference([string]$BaselinePath, [string]$CandidatePath) {
    $a = Get-BmpInfo $BaselinePath
    $b = Get-BmpInfo $CandidatePath
    if (-not [bool]$a.valid) {
        return [pscustomobject]@{ valid = $false; reason = "baseline_$($a.reason)" }
    }
    if (-not [bool]$b.valid) {
        return [pscustomobject]@{ valid = $false; reason = "candidate_$($b.reason)" }
    }
    if ($a.width -ne $b.width -or $a.height -ne $b.height -or
        $a.bytes_per_pixel -ne $b.bytes_per_pixel) {
        return [pscustomobject]@{ valid = $false; reason = "dimension_or_format_mismatch" }
    }

    $sumAbs = 0.0
    $changed = 0
    $pixelCount = [int64]$a.width * [int64]$a.height
    for ($y = 0; $y -lt $a.height; ++$y) {
        $rowA = $a.data_offset + ($y * $a.row_stride)
        $rowB = $b.data_offset + ($y * $b.row_stride)
        for ($x = 0; $x -lt $a.width; ++$x) {
            $pa = $rowA + ($x * $a.bytes_per_pixel)
            $pb = $rowB + ($x * $b.bytes_per_pixel)
            $lumaA = (0.2126 * [double]$a.bytes[$pa + 2]) + (0.7152 * [double]$a.bytes[$pa + 1]) + (0.0722 * [double]$a.bytes[$pa])
            $lumaB = (0.2126 * [double]$b.bytes[$pb + 2]) + (0.7152 * [double]$b.bytes[$pb + 1]) + (0.0722 * [double]$b.bytes[$pb])
            $delta = [Math]::Abs($lumaA - $lumaB)
            $sumAbs += $delta
            if ($delta -gt 10.0) {
                ++$changed
            }
        }
    }

    $meanAbsLumaDelta = 0.0
    $changedPixelRatio = 0.0
    if ($pixelCount -gt 0) {
        $meanAbsLumaDelta = $sumAbs / [double]$pixelCount
        $changedPixelRatio = [double]$changed / [double]$pixelCount
    }

    return [pscustomobject]@{
        valid = $true
        reason = ""
        mean_abs_luma_delta = $meanAbsLumaDelta
        changed_pixel_ratio = $changedPixelRatio
    }
}

if ($report.lifecycle -ne "active_frame") {
    Add-Failure "frame_report_last.json lifecycle is '$($report.lifecycle)', expected active_frame"
}
if ($report.health_warnings.Count -ne 0) {
    Add-Failure "health_warnings is not empty: $($report.health_warnings -join ', ')"
}
if ($report.frame_contract.warnings.Count -ne 0) {
    Add-Failure "frame_contract warnings is not empty: $($report.frame_contract.warnings -join ', ')"
}
$lighting = $report.frame_contract.lighting
if ($null -eq $lighting) {
    Add-Failure "frame_contract.lighting is missing"
} else {
    if ([string]$lighting.rig_id -ne "rt_showcase_gallery") {
        Add-Failure "RT showcase lighting rig is '$($lighting.rig_id)', expected 'rt_showcase_gallery'"
    }
    if ([string]$lighting.rig_source -ne "scene_preset") {
        Add-Failure "RT showcase lighting rig source is '$($lighting.rig_source)', expected 'scene_preset'"
    }
    if ([int]$lighting.light_count -lt 4) {
        Add-Failure "RT showcase light count is $($lighting.light_count), expected >= 4"
    }
    if ([int]$lighting.shadow_casting_light_count -lt 1) {
        Add-Failure "RT showcase shadow-casting light count is $($lighting.shadow_casting_light_count), expected >= 1"
    }
    if ([double]$lighting.total_light_intensity -le 0.0 -or [double]$lighting.max_light_intensity -le 0.0) {
        Add-Failure "RT showcase lighting intensity contract is not positive"
    }
}
if ([int]$report.frame_contract.pass_budget_summary.transient_descriptor_delta_total -ne 0) {
    Add-Failure "transient descriptor delta is $($report.frame_contract.pass_budget_summary.transient_descriptor_delta_total), expected 0"
}
if (-not [bool]$report.camera.active) {
    Add-Failure "diagnostics report does not contain an active camera"
} else {
    if (-not [string]::IsNullOrWhiteSpace($CameraBookmark) -and
        [string]$report.camera.bookmark -ne $CameraBookmark) {
        Add-Failure "RT showcase camera bookmark is '$($report.camera.bookmark)', expected '$CameraBookmark'"
    }
    $cameraError = [Math]::Sqrt(
        [Math]::Pow([double]$report.camera.position.x - (-14.0), 2.0) +
        [Math]::Pow([double]$report.camera.position.y - 2.05, 2.0) +
        [Math]::Pow([double]$report.camera.position.z - (-6.8), 2.0))
    if ($cameraError -gt $MaxCameraPositionError) {
        Add-Failure "RT showcase camera drifted from the validation view: error=$cameraError, budget <= $MaxCameraPositionError, position=($($report.camera.position.x), $($report.camera.position.y), $($report.camera.position.z))"
    }
}
if (-not [bool]$report.frame_contract.culling.stats_valid) {
    Add-Failure "GPU culling stats are not valid"
}
if ([int]$report.frame_contract.culling.tested -le 0) {
    Add-Failure "GPU culling tested count is not positive"
}
if ([bool]$report.frame_contract.ray_tracing.enabled) {
    if ($null -eq $report.frame_contract.renderer_budget) {
        Add-Failure "renderer budget plan is missing from frame contract"
    } elseif ([string]::IsNullOrWhiteSpace([string]$report.frame_contract.renderer_budget.profile)) {
        Add-Failure "renderer budget profile is missing"
    } elseif (-not [string]::IsNullOrWhiteSpace($ExpectedRTBudgetProfile) -and
              [string]$report.frame_contract.renderer_budget.profile -ne $ExpectedRTBudgetProfile) {
        Add-Failure "renderer budget profile is '$($report.frame_contract.renderer_budget.profile)', expected '$ExpectedRTBudgetProfile'"
    }
    if (-not [bool]$report.frame_contract.ray_tracing.scheduler_enabled) {
        Add-Failure "RT scheduler did not enable the frame plan while ray tracing is active"
    }
    if ([string]::IsNullOrWhiteSpace([string]$report.frame_contract.ray_tracing.budget_profile)) {
        Add-Failure "RT scheduler budget profile is missing"
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedRTBudgetProfile) -and
        [string]$report.frame_contract.ray_tracing.budget_profile -ne $ExpectedRTBudgetProfile) {
        Add-Failure "RT scheduler budget profile is '$($report.frame_contract.ray_tracing.budget_profile)', expected '$ExpectedRTBudgetProfile'"
    }
    if (-not [bool]$report.frame_contract.ray_tracing.scheduler_build_tlas) {
        Add-Failure "RT scheduler did not plan a TLAS build"
    }
    if (-not [bool]$report.frame_contract.ray_tracing.dispatch_shadows) {
        Add-Failure "RT scheduler did not plan RT shadow dispatch"
    }
    if ([int]$report.frame_contract.ray_tracing.scheduler_max_tlas_instances -le 0) {
        Add-Failure "RT scheduler max TLAS instance budget is not positive"
    }
    if ([int64]$report.frame_contract.ray_tracing.max_blas_total_bytes -le 0 -or
        [int64]$report.frame_contract.ray_tracing.max_blas_build_bytes_per_frame -le 0) {
        Add-Failure "RT scheduler BLAS budgets are not positive"
    }
    if ([bool]$report.frame_contract.features.rt_reflections_enabled) {
        $rtReflectionDispatched = [bool]$report.frame_contract.ray_tracing.dispatch_reflections
        $requireRTReflectionDispatchArtifacts = $rtReflectionDispatched -or -not $AllowRTCadenceSkips
        if (-not [bool]$report.frame_contract.ray_tracing.dispatch_reflections -and -not $AllowRTCadenceSkips) {
            Add-Failure "RT scheduler did not dispatch reflections while RT reflections are active"
        } elseif (-not [bool]$report.frame_contract.ray_tracing.dispatch_reflections -and
                  [int]$report.frame_contract.ray_tracing.reflection_frame_phase -eq 0) {
            Add-Failure "RT reflection dispatch skipped on its scheduled cadence phase"
        }
        if ([int]$report.frame_contract.ray_tracing.reflection_width -le 0 -or
            [int]$report.frame_contract.ray_tracing.reflection_height -le 0) {
            Add-Failure "RT scheduler reflection resolution is invalid: $($report.frame_contract.ray_tracing.reflection_width)x$($report.frame_contract.ray_tracing.reflection_height)"
        }
        if ($requireRTReflectionDispatchArtifacts) {
            if ($null -ne $report.frame_contract.ray_tracing.reflection_dispatch_ready -and
                -not [bool]$report.frame_contract.ray_tracing.reflection_dispatch_ready) {
                Add-Failure "RT reflection dispatch was not ready: $($report.frame_contract.ray_tracing.reflection_readiness_reason)"
            }
            foreach ($fieldName in @(
                "reflection_has_pipeline",
                "reflection_has_tlas",
                "reflection_has_material_buffer",
                "reflection_has_output",
                "reflection_has_depth",
                "reflection_has_normal_roughness",
                "reflection_has_environment_table",
                "reflection_has_frame_constants",
                "reflection_has_dispatch_descriptors")) {
                if ($null -ne $report.frame_contract.ray_tracing.$fieldName -and
                    -not [bool]$report.frame_contract.ray_tracing.$fieldName) {
                    Add-Failure "RT reflection dispatch input '$fieldName' is false"
                }
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_dispatch_width -and
                ([int]$report.frame_contract.ray_tracing.reflection_dispatch_width -le 0 -or
                 [int]$report.frame_contract.ray_tracing.reflection_dispatch_height -le 0)) {
                Add-Failure "RT reflection dispatch extent is invalid: $($report.frame_contract.ray_tracing.reflection_dispatch_width)x$($report.frame_contract.ray_tracing.reflection_dispatch_height)"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_signal_stats_captured -and
                -not [bool]$report.frame_contract.ray_tracing.reflection_signal_stats_captured) {
                Add-Failure "RT reflection signal stats were not captured"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_signal_valid -and
                -not [bool]$report.frame_contract.ray_tracing.reflection_signal_valid) {
                Add-Failure "RT reflection signal stats are not valid"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_signal_pixel_count -and
                [int]$report.frame_contract.ray_tracing.reflection_signal_pixel_count -le 0) {
                Add-Failure "RT reflection signal stats have zero pixels"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_signal_nonzero_ratio -and
                [double]$report.frame_contract.ray_tracing.reflection_signal_nonzero_ratio -le 0.001) {
                Add-Failure "RT reflection signal nonzero ratio is too low: $($report.frame_contract.ray_tracing.reflection_signal_nonzero_ratio)"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_signal_max_luma -and
                [double]$report.frame_contract.ray_tracing.reflection_signal_max_luma -le 0.001) {
                Add-Failure "RT reflection signal max luma is too low: $($report.frame_contract.ray_tracing.reflection_signal_max_luma)"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_signal_outlier_ratio -and
                [double]$report.frame_contract.ray_tracing.reflection_signal_outlier_ratio -gt $MaxRTReflectionSignalOutlierRatio) {
                Add-Failure "RT reflection signal outlier ratio is too high: $($report.frame_contract.ray_tracing.reflection_signal_outlier_ratio) > $MaxRTReflectionSignalOutlierRatio"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_history_signal_stats_captured -and
                -not [bool]$report.frame_contract.ray_tracing.reflection_history_signal_stats_captured) {
                Add-Failure "RT reflection history signal stats were not captured"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_history_signal_valid -and
                -not [bool]$report.frame_contract.ray_tracing.reflection_history_signal_valid) {
                Add-Failure "RT reflection history signal stats are not valid"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_history_signal_pixel_count -and
                [int]$report.frame_contract.ray_tracing.reflection_history_signal_pixel_count -le 0) {
                Add-Failure "RT reflection history signal stats have zero pixels"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_history_signal_nonzero_ratio -and
                [double]$report.frame_contract.ray_tracing.reflection_history_signal_nonzero_ratio -le 0.001) {
                Add-Failure "RT reflection history signal nonzero ratio is too low: $($report.frame_contract.ray_tracing.reflection_history_signal_nonzero_ratio)"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_history_signal_max_luma -and
                [double]$report.frame_contract.ray_tracing.reflection_history_signal_max_luma -le 0.001) {
                Add-Failure "RT reflection history signal max luma is too low: $($report.frame_contract.ray_tracing.reflection_history_signal_max_luma)"
            }
            if ($null -ne $report.frame_contract.ray_tracing.reflection_history_signal_outlier_ratio -and
                [double]$report.frame_contract.ray_tracing.reflection_history_signal_outlier_ratio -gt $MaxRTReflectionHistorySignalOutlierRatio) {
                Add-Failure "RT reflection history signal outlier ratio is too high: $($report.frame_contract.ray_tracing.reflection_history_signal_outlier_ratio) > $MaxRTReflectionHistorySignalOutlierRatio"
            }
        }
    }
    if ([bool]$report.frame_contract.features.rt_gi_enabled) {
        if (-not [bool]$report.frame_contract.ray_tracing.dispatch_gi -and -not $AllowRTCadenceSkips) {
            Add-Failure "RT scheduler did not dispatch GI while RT GI is active"
        } elseif (-not [bool]$report.frame_contract.ray_tracing.dispatch_gi -and
                  [int]$report.frame_contract.ray_tracing.gi_frame_phase -eq 0) {
            Add-Failure "RT GI dispatch skipped on its scheduled cadence phase"
        }
        if ([int]$report.frame_contract.ray_tracing.gi_width -le 0 -or
            [int]$report.frame_contract.ray_tracing.gi_height -le 0) {
            Add-Failure "RT scheduler GI resolution is invalid: $($report.frame_contract.ray_tracing.gi_width)x$($report.frame_contract.ray_tracing.gi_height)"
        }
    }
    if ([int]$report.frame_contract.ray_tracing.tlas_instances -le 0) {
        Add-Failure "RT TLAS instance count is not positive"
    }
    if ([int]$report.frame_contract.ray_tracing.tlas_instances -lt $MinRTTLASInstances) {
        Add-Failure "RT TLAS coverage is too low: instances=$($report.frame_contract.ray_tracing.tlas_instances), expected >= $MinRTTLASInstances; candidates=$($report.frame_contract.ray_tracing.tlas_candidates) invalid=$($report.frame_contract.ray_tracing.tlas_skipped_invalid) missing_geometry=$($report.frame_contract.ray_tracing.tlas_missing_geometry) distance_culled=$($report.frame_contract.ray_tracing.tlas_distance_culled) build_deferred=$($report.frame_contract.ray_tracing.tlas_blas_build_budget_deferred)"
    }
    if ([int]$report.frame_contract.ray_tracing.tlas_instances -ne
        [int]$report.frame_contract.ray_tracing.material_records) {
        Add-Failure "RT material records do not match TLAS instances: tlas=$($report.frame_contract.ray_tracing.tlas_instances) materials=$($report.frame_contract.ray_tracing.material_records)"
    }
    if ([int64]$report.frame_contract.ray_tracing.material_buffer_bytes -le 0) {
        Add-Failure "RT material buffer was not populated"
    }
    $expectedMaterialBufferBytes = [int64]$report.frame_contract.ray_tracing.material_records * 64
    if ([int64]$report.frame_contract.ray_tracing.material_buffer_bytes -ne $expectedMaterialBufferBytes) {
        Add-Failure "RT material buffer layout drifted: bytes=$($report.frame_contract.ray_tracing.material_buffer_bytes) expected=$expectedMaterialBufferBytes for $($report.frame_contract.ray_tracing.material_records) 64-byte records"
    }
    $rtSurfaceClassTotal =
        [int]$report.frame_contract.ray_tracing.surface_default +
        [int]$report.frame_contract.ray_tracing.surface_glass +
        [int]$report.frame_contract.ray_tracing.surface_mirror +
        [int]$report.frame_contract.ray_tracing.surface_plastic +
        [int]$report.frame_contract.ray_tracing.surface_masonry +
        [int]$report.frame_contract.ray_tracing.surface_emissive +
        [int]$report.frame_contract.ray_tracing.surface_brushed_metal +
        [int]$report.frame_contract.ray_tracing.surface_wood +
        [int]$report.frame_contract.ray_tracing.surface_water
    if ($rtSurfaceClassTotal -ne [int]$report.frame_contract.ray_tracing.material_records) {
        Add-Failure "RT material surface class counts do not sum to material records: classes=$rtSurfaceClassTotal records=$($report.frame_contract.ray_tracing.material_records)"
    }
    if ([int]$report.frame_contract.ray_tracing.surface_mirror -lt 1) {
        Add-Failure "RT TLAS did not include any mirror-class materials"
    }
    if ([int]$report.frame_contract.ray_tracing.surface_glass -lt 1) {
        Add-Failure "RT TLAS did not include any glass-class materials"
    }
    if ([int]$report.frame_contract.ray_tracing.tlas_blas_build_failed -ne 0) {
        Add-Failure "RT BLAS builds failed: $($report.frame_contract.ray_tracing.tlas_blas_build_failed)"
    }
    if ([int]$report.frame_contract.ray_tracing.tlas_blas_total_budget_skipped -ne 0) {
        Add-Failure "RT BLAS total budget skipped meshes: $($report.frame_contract.ray_tracing.tlas_blas_total_budget_skipped)"
    }
}
if ([bool]$report.frame_contract.culling.stats_valid) {
    $culledTotal = [int]$report.frame_contract.culling.frustum_culled +
                   [int]$report.frame_contract.culling.occluded +
                   [int]$report.frame_contract.culling.visible
    if ($culledTotal -ne [int]$report.frame_contract.culling.tested) {
        Add-Failure "GPU culling counters do not sum to tested count: visible=$($report.frame_contract.culling.visible) frustum=$($report.frame_contract.culling.frustum_culled) occluded=$($report.frame_contract.culling.occluded) tested=$($report.frame_contract.culling.tested)"
    }
}
if ($null -ne $report.frame_contract.draw_counts.water_draws -and
    [int]$report.frame_contract.draw_counts.water_draws -gt $MaxWaterDraws) {
    Add-Failure "water draw count is $($report.frame_contract.draw_counts.water_draws), budget is <= $MaxWaterDraws for the RT showcase validation camera"
}
if (-not [bool]$report.frame_contract.features.visibility_buffer_enabled) {
    Add-Failure "visibility buffer feature is not active in RT showcase smoke"
}
if ($null -eq $report.frame_contract.motion_vectors) {
    Add-Failure "frame contract does not report motion vector diagnostics"
} else {
    if (-not [bool]$report.frame_contract.motion_vectors.executed) {
        Add-Failure "motion vector pass did not execute"
    }
    if (-not [bool]$report.frame_contract.motion_vectors.visibility_buffer_motion) {
        Add-Failure "motion vectors are not using visibility-buffer per-object mode"
    }
    if ([bool]$report.frame_contract.motion_vectors.camera_only_fallback) {
        Add-Failure "motion vectors fell back to camera-only mode"
    }
    if ([int]$report.frame_contract.motion_vectors.instance_count -le 0) {
        Add-Failure "motion vector instance count is not positive"
    }
    $previousTransformTotal =
        [int]$report.frame_contract.motion_vectors.previous_world_matrices +
        [int]$report.frame_contract.motion_vectors.seeded_previous_world_matrices
    if ($previousTransformTotal -ne [int]$report.frame_contract.motion_vectors.instance_count) {
        Add-Failure "motion vector previous-transform accounting mismatch: previous=$($report.frame_contract.motion_vectors.previous_world_matrices) seeded=$($report.frame_contract.motion_vectors.seeded_previous_world_matrices) instances=$($report.frame_contract.motion_vectors.instance_count)"
    }
}
$visibilityBufferPass = Get-FrameContractPass $report "VisibilityBuffer"
if ($null -eq $visibilityBufferPass) {
    Add-Failure "frame contract does not report VisibilityBuffer pass"
} else {
    foreach ($resourceName in @("visibility_buffer",
                                "vb_gbuffer_albedo",
                                "vb_gbuffer_normal_roughness",
                                "vb_gbuffer_emissive_metallic",
                                "vb_gbuffer_material_ext0",
                                "vb_gbuffer_material_ext1",
                                "vb_gbuffer_material_ext2")) {
        if (-not (Test-PassWrites $visibilityBufferPass $resourceName)) {
            Add-Failure "VisibilityBuffer pass does not report $resourceName as a write dependency"
        }
    }
}
$rtReflectionsPass = Get-FrameContractPass $report "RTReflections"
if ($null -eq $rtReflectionsPass) {
    Add-Failure "frame contract does not report RTReflections pass"
} elseif (-not (Test-PassReads $rtReflectionsPass "vb_gbuffer_material_ext2")) {
    Add-Failure "RTReflections pass does not report vb_gbuffer_material_ext2 as a read dependency"
} elseif (-not (Test-PassReads $rtReflectionsPass "vb_gbuffer_normal_roughness")) {
    Add-Failure "RTReflections pass does not report vb_gbuffer_normal_roughness as a read dependency"
}
$requireRTReflectionDispatchArtifacts = [bool]$report.frame_contract.features.rt_reflections_enabled -and
                                        ([bool]$report.frame_contract.ray_tracing.dispatch_reflections -or
                                         -not $AllowRTCadenceSkips)
if ($requireRTReflectionDispatchArtifacts) {
    $rtReflectionSignalStatsPass = Get-FrameContractPass $report "RTReflectionSignalStats"
    if ($null -eq $rtReflectionSignalStatsPass) {
        Add-Failure "frame contract does not report RTReflectionSignalStats pass"
    } else {
        if (-not [bool]$rtReflectionSignalStatsPass.executed) {
            Add-Failure "RTReflectionSignalStats pass was planned but did not execute"
        }
        if (-not (Test-PassReads $rtReflectionSignalStatsPass "rt_reflection")) {
            Add-Failure "RTReflectionSignalStats pass does not report rt_reflection as a read dependency"
        }
        if (-not (Test-PassWrites $rtReflectionSignalStatsPass "rt_reflection_signal_stats")) {
            Add-Failure "RTReflectionSignalStats pass does not report rt_reflection_signal_stats as a write dependency"
        }
    }
    $rtReflectionSignalStatsResource = Get-FrameContractResource $report "rt_reflection_signal_stats"
    if ($null -eq $rtReflectionSignalStatsResource) {
        Add-Failure "frame contract does not report rt_reflection_signal_stats"
    } elseif (-not [bool]$rtReflectionSignalStatsResource.valid) {
        Add-Failure "rt_reflection_signal_stats is not valid"
    }
    $rtReflectionHistorySignalStatsPass = Get-FrameContractPass $report "RTReflectionHistorySignalStats"
    if ($null -eq $rtReflectionHistorySignalStatsPass) {
        Add-Failure "frame contract does not report RTReflectionHistorySignalStats pass"
    } else {
        if (-not [bool]$rtReflectionHistorySignalStatsPass.executed) {
            Add-Failure "RTReflectionHistorySignalStats pass was planned but did not execute"
        }
        if (-not (Test-PassReads $rtReflectionHistorySignalStatsPass "rt_reflection_history")) {
            Add-Failure "RTReflectionHistorySignalStats pass does not report rt_reflection_history as a read dependency"
        }
        if (-not (Test-PassWrites $rtReflectionHistorySignalStatsPass "rt_reflection_history_signal_stats")) {
            Add-Failure "RTReflectionHistorySignalStats pass does not report rt_reflection_history_signal_stats as a write dependency"
        }
    }
    $rtReflectionHistorySignalStatsResource = Get-FrameContractResource $report "rt_reflection_history_signal_stats"
    if ($null -eq $rtReflectionHistorySignalStatsResource) {
        Add-Failure "frame contract does not report rt_reflection_history_signal_stats"
    } elseif (-not [bool]$rtReflectionHistorySignalStatsResource.valid) {
        Add-Failure "rt_reflection_history_signal_stats is not valid"
    }
}
$rtDenoisePass = Get-FrameContractPass $report "RTDenoise"
if ($null -eq $rtDenoisePass) {
    Add-Failure "frame contract does not report RTDenoise pass"
} else {
    if (-not [bool]$rtDenoisePass.executed) {
        Add-Failure "RTDenoise pass was planned but did not execute"
    }
    foreach ($resourceName in @("rt_shadow_history", "rt_reflection_history", "rt_gi_history")) {
        if (-not (Test-PassWrites $rtDenoisePass $resourceName)) {
            Add-Failure "RTDenoise pass does not report $resourceName as a write dependency"
        }
    }
    if (-not (Test-PassReads $rtDenoisePass "temporal_rejection_mask")) {
        Add-Failure "RTDenoise pass does not report temporal_rejection_mask as a read dependency"
    }
}
$temporalMaskPass = Get-FrameContractPass $report "TemporalRejectionMask"
if ($null -eq $temporalMaskPass) {
    Add-Failure "frame contract does not report TemporalRejectionMask pass"
} else {
    if (-not [bool]$temporalMaskPass.executed) {
        Add-Failure "TemporalRejectionMask pass was planned but did not execute"
    }
    if (-not [bool]$temporalMaskPass.render_graph) {
        Add-Failure "TemporalRejectionMask pass is not graph-owned"
    }
    if ([bool]$temporalMaskPass.fallback_used) {
        Add-Failure "TemporalRejectionMask pass used fallback path: $($temporalMaskPass.fallback_reason)"
    }
    if (-not (Test-PassWrites $temporalMaskPass "temporal_rejection_mask")) {
        Add-Failure "TemporalRejectionMask pass does not report temporal_rejection_mask as a write dependency"
    }
}
$temporalMaskResource = Get-FrameContractResource $report "temporal_rejection_mask"
if ($null -eq $temporalMaskResource) {
    Add-Failure "frame contract does not report temporal_rejection_mask"
} else {
    if (-not [bool]$temporalMaskResource.valid) {
        Add-Failure "temporal_rejection_mask is not valid"
    }
    if (-not [bool]$temporalMaskResource.size_matches_contract) {
        Add-Failure "temporal_rejection_mask size mismatch: actual=$($temporalMaskResource.width)x$($temporalMaskResource.height), expected=$($temporalMaskResource.expected_width)x$($temporalMaskResource.expected_height)"
    }
}
$temporalMaskStatsResource = Get-FrameContractResource $report "temporal_rejection_mask_stats"
if ($null -eq $temporalMaskStatsResource) {
    Add-Failure "frame contract does not report temporal_rejection_mask_stats"
} elseif (-not [bool]$temporalMaskStatsResource.valid) {
    Add-Failure "temporal_rejection_mask_stats is not valid"
}
if ($null -eq $report.frame_contract.temporal_mask) {
    Add-Failure "frame contract does not report temporal_mask statistics"
} else {
    $temporalStats = $report.frame_contract.temporal_mask
    if (-not [bool]$temporalStats.built) {
        Add-Failure "temporal_mask contract does not report mask construction"
    }
    if (-not [bool]$temporalStats.valid) {
        Add-Failure "temporal_mask statistics are not valid"
    }
    if ([int]$temporalStats.pixel_count -le 0) {
        Add-Failure "temporal_mask statistics have zero pixels"
    }
    foreach ($field in @("accepted_ratio", "disocclusion_ratio", "high_motion_ratio", "out_of_bounds_ratio")) {
        $value = [double]$temporalStats.$field
        if ($value -lt -0.001 -or $value -gt 1.001) {
            Add-Failure "temporal_mask $field is outside [0,1]: $value"
        }
    }
    if ([double]$temporalStats.accepted_ratio -lt 0.01) {
        Add-Failure "temporal_mask accepted_ratio is suspiciously low: $($temporalStats.accepted_ratio)"
    }
    if ([double]$temporalStats.out_of_bounds_ratio -gt 0.20) {
        Add-Failure "temporal_mask out_of_bounds_ratio is suspiciously high: $($temporalStats.out_of_bounds_ratio)"
    }
}
if ([bool]$report.frame_contract.ray_tracing.enabled) {
    $rtTuning = $report.frame_contract.ray_tracing.rt_reflection_tuning
    if ($null -eq $rtTuning) {
        Add-Failure "ray_tracing.rt_reflection_tuning is missing"
    } else {
        $denoiseAlpha = [double]$rtTuning.denoise_alpha
        $compositionStrength = [double]$rtTuning.composition_strength
        $roughnessThreshold = [double]$rtTuning.roughness_threshold
        $historyMaxBlend = [double]$rtTuning.history_max_blend
        $fireflyClampLuma = [double]$rtTuning.firefly_clamp_luma
        $signalScale = [double]$rtTuning.signal_scale
        if ($denoiseAlpha -lt 0.02 -or $denoiseAlpha -gt 1.0) {
            Add-Failure "RT reflection denoise alpha is out of range: $denoiseAlpha"
        }
        if ($compositionStrength -lt 0.0 -or $compositionStrength -gt 1.0) {
            Add-Failure "RT reflection composition strength is out of range: $compositionStrength"
        }
        if ($roughnessThreshold -lt 0.05 -or $roughnessThreshold -gt 1.0) {
            Add-Failure "RT reflection roughness threshold is out of range: $roughnessThreshold"
        }
        if ($historyMaxBlend -lt 0.0 -or $historyMaxBlend -gt 0.5) {
            Add-Failure "RT reflection history max blend is out of range: $historyMaxBlend"
        }
        if ($fireflyClampLuma -lt 4.0 -or $fireflyClampLuma -gt 32.0) {
            Add-Failure "RT reflection firefly clamp luma is out of range: $fireflyClampLuma"
        }
        if ($signalScale -lt 0.0 -or $signalScale -gt 2.0) {
            Add-Failure "RT reflection signal scale is out of range: $signalScale"
        }
    }
    if (-not [bool]$report.frame_contract.ray_tracing.denoiser_executed) {
        Add-Failure "RT denoiser did not execute while ray tracing is active"
    }
    if ([int]$report.frame_contract.ray_tracing.denoiser_passes -le 0) {
        Add-Failure "RT denoiser pass count is not positive"
    }
    if (-not [bool]$report.frame_contract.ray_tracing.denoiser_uses_depth_normal_rejection) {
        Add-Failure "RT denoiser is not reporting depth/normal rejection"
    }
    if (-not [bool]$report.frame_contract.ray_tracing.denoiser_uses_disocclusion_rejection) {
        Add-Failure "RT denoiser is not reporting disocclusion rejection"
    }
}
foreach ($historyName in @("taa_color", "rt_shadow_mask", "rt_reflection", "rt_gi")) {
    $history = Get-FrameContractHistory $report $historyName
    if ($null -eq $history) {
        Add-Failure "frame contract does not report temporal history '$historyName'"
        continue
    }
    if ($null -eq $history.seeded) {
        Add-Failure "temporal history '$historyName' is missing seeded field"
    }
    if ($null -eq $history.last_invalidated_frame) {
        Add-Failure "temporal history '$historyName' is missing last_invalidated_frame"
    }
    if ([string]::IsNullOrWhiteSpace([string]$history.rejection_mode)) {
        Add-Failure "temporal history '$historyName' is missing rejection_mode"
    }
    if ($null -eq $history.accumulation_alpha) {
        Add-Failure "temporal history '$historyName' is missing accumulation_alpha"
    }
    if ($null -eq $history.uses_disocclusion_rejection) {
        Add-Failure "temporal history '$historyName' is missing uses_disocclusion_rejection"
    }
    if ([bool]$history.valid -and -not [bool]$history.seeded) {
        Add-Failure "temporal history '$historyName' is valid but not seeded"
    }
    if ([bool]$history.valid -and
        [bool]$history.uses_velocity_reprojection -and
        -not [bool]$history.uses_disocclusion_rejection) {
        Add-Failure "temporal history '$historyName' reprojects without disocclusion rejection"
    }
    if (-not [bool]$history.valid -and
        [string]::IsNullOrWhiteSpace([string]$history.invalid_reason)) {
        Add-Failure "temporal history '$historyName' is invalid without an invalid_reason"
    }
}
$postProcessPass = Get-FrameContractPass $report "PostProcess"
if ($null -eq $postProcessPass) {
    Add-Failure "frame contract does not report PostProcess pass"
} elseif (-not (Test-PassReads $postProcessPass "vb_gbuffer_material_ext2")) {
    Add-Failure "PostProcess pass does not report vb_gbuffer_material_ext2 as a read dependency"
} elseif (-not (Test-PassReads $postProcessPass "vb_gbuffer_material_ext1")) {
    Add-Failure "PostProcess pass does not report vb_gbuffer_material_ext1 as a read dependency"
} elseif (-not (Test-PassReads $postProcessPass "vb_gbuffer_normal_roughness")) {
    Add-Failure "PostProcess pass does not report vb_gbuffer_normal_roughness as a read dependency"
}
$materialClassResource = Get-FrameContractResource $report "vb_gbuffer_material_ext2"
if ($null -eq $materialClassResource) {
    Add-Failure "frame contract does not report vb_gbuffer_material_ext2"
} else {
    if (-not [bool]$materialClassResource.valid) {
        Add-Failure "vb_gbuffer_material_ext2 is not valid"
    }
    if (-not [bool]$materialClassResource.size_matches_contract) {
        Add-Failure "vb_gbuffer_material_ext2 size mismatch: actual=$($materialClassResource.width)x$($materialClassResource.height), expected=$($materialClassResource.expected_width)x$($materialClassResource.expected_height)"
    }
    if ($null -ne $materialClassResource.mb -and [double]$materialClassResource.mb -le 0.0) {
        Add-Failure "vb_gbuffer_material_ext2 memory estimate is not positive"
    }
}
if (-not [bool]$report.visual_validation.captured) {
    Add-Failure "visual validation capture was not recorded"
}
if (-not [bool]$report.visual_validation.image_stats.valid) {
    Add-Failure "visual validation image stats are invalid: $($report.visual_validation.image_stats.reason)"
}
if ([double]$report.visual_validation.image_stats.avg_luma -lt $MinVisualAvgLuma) {
    Add-Failure "visual validation average luma is $($report.visual_validation.image_stats.avg_luma), expected >= $MinVisualAvgLuma"
}
if ([double]$report.visual_validation.image_stats.nonblack_ratio -lt 0.05) {
    Add-Failure "visual validation nonblack ratio is too low: $($report.visual_validation.image_stats.nonblack_ratio)"
}
if ($null -ne $report.visual_validation.image_stats.saturated_ratio -and
    [double]$report.visual_validation.image_stats.saturated_ratio -gt $MaxVisualSaturatedRatio) {
    Add-Failure "visual validation saturated ratio is $($report.visual_validation.image_stats.saturated_ratio), budget is <= $MaxVisualSaturatedRatio"
}
if ($null -ne $report.visual_validation.image_stats.near_white_ratio -and
    [double]$report.visual_validation.image_stats.near_white_ratio -gt $MaxVisualNearWhiteRatio) {
    Add-Failure "visual validation near-white ratio is $($report.visual_validation.image_stats.near_white_ratio), budget is <= $MaxVisualNearWhiteRatio"
}
if ($null -ne $report.visual_validation.image_stats.center_avg_luma -and
    [double]$report.visual_validation.image_stats.center_avg_luma -gt $MaxVisualCenterLuma) {
    Add-Failure "visual validation center luma is $($report.visual_validation.image_stats.center_avg_luma), budget is <= $MaxVisualCenterLuma"
}
if ($null -ne $report.visual_validation.image_stats.center_avg_luma -and
    [double]$report.visual_validation.image_stats.center_avg_luma -lt $MinVisualCenterLuma) {
    Add-Failure "visual validation center luma is $($report.visual_validation.image_stats.center_avg_luma), expected >= $MinVisualCenterLuma"
}
if ($null -ne $report.visual_validation.image_stats.dark_detail_ratio -and
    [double]$report.visual_validation.image_stats.dark_detail_ratio -gt $MaxVisualDarkDetailRatio) {
    Add-Failure "visual validation dark-detail ratio is $($report.visual_validation.image_stats.dark_detail_ratio), budget is <= $MaxVisualDarkDetailRatio"
}
if ([int]$report.smoke_automation.total_frames -gt $MaxExpectedFrames) {
    Add-Failure "smoke took $($report.smoke_automation.total_frames) frames, expected <= $MaxExpectedFrames"
}
if ($null -eq $report.gpu_frame_ms -or [double]$report.gpu_frame_ms -le 0.0) {
    Add-Failure "GPU frame timing was not available"
}
if ($null -ne $report.gpu_frame_ms -and [double]$report.gpu_frame_ms -gt $MaxGpuFrameMs) {
    Add-Failure "GPU frame time is $([double]$report.gpu_frame_ms) ms, budget is <= $MaxGpuFrameMs ms"
}
if ($null -ne $report.dxgi_memory_mb.current_usage -and [double]$report.dxgi_memory_mb.current_usage -gt $MaxDxgiMemoryMb) {
    Add-Failure "DXGI current usage is $([double]$report.dxgi_memory_mb.current_usage) MB, budget is <= $MaxDxgiMemoryMb MB"
}
if ($null -ne $report.memory_mb.total_estimated -and [double]$report.memory_mb.total_estimated -gt $MaxEstimatedMemoryMb) {
    Add-Failure "estimated renderer memory is $([double]$report.memory_mb.total_estimated) MB, budget is <= $MaxEstimatedMemoryMb MB"
}
if ($null -ne $report.memory_mb.render_targets -and [double]$report.memory_mb.render_targets -gt $MaxRenderTargetMemoryMb) {
    Add-Failure "render target memory is $([double]$report.memory_mb.render_targets) MB, budget is <= $MaxRenderTargetMemoryMb MB"
}
if ($null -ne $report.frame_contract.pass_budget_summary.estimated_write_mb_total -and
    [double]$report.frame_contract.pass_budget_summary.estimated_write_mb_total -gt $MaxEstimatedWriteMb) {
    Add-Failure "estimated pass write bandwidth is $([double]$report.frame_contract.pass_budget_summary.estimated_write_mb_total) MB/frame, budget is <= $MaxEstimatedWriteMb MB/frame"
}
if ($null -eq $report.frame_contract.renderables) {
    Add-Failure "frame contract does not report renderable depth classes"
} else {
    $depthClassTotal =
        [int]$report.frame_contract.renderables.opaque_depth_writing +
        [int]$report.frame_contract.renderables.alpha_tested_depth_writing +
        [int]$report.frame_contract.renderables.double_sided_opaque_depth_writing +
        [int]$report.frame_contract.renderables.double_sided_alpha_tested_depth_writing +
        [int]$report.frame_contract.renderables.transparent_depth_tested +
        [int]$report.frame_contract.renderables.water_depth_tested_no_write +
        [int]$report.frame_contract.renderables.overlay
    $meshRenderableTotal = [int]$report.frame_contract.renderables.visible - [int]$report.frame_contract.renderables.meshless
    if ($depthClassTotal -ne $meshRenderableTotal) {
        Add-Failure "renderable depth-class counts do not sum to visible mesh renderables: classes=$depthClassTotal visible_mesh=$meshRenderableTotal"
    }
    if ([int]$report.frame_contract.renderables.opaque_depth_writing -lt $MinOpaqueDepthWriting) {
        Add-Failure "opaque depth-writing renderables are too low: $($report.frame_contract.renderables.opaque_depth_writing), expected >= $MinOpaqueDepthWriting"
    }
    if ([int]$report.frame_contract.renderables.double_sided_opaque_depth_writing -lt $MinDoubleSidedOpaqueDepthWriting) {
        Add-Failure "double-sided opaque depth-writing renderables are too low: $($report.frame_contract.renderables.double_sided_opaque_depth_writing), expected >= $MinDoubleSidedOpaqueDepthWriting"
    }
    if ([int]$report.frame_contract.renderables.transparent_depth_tested -lt $MinTransparentDepthTested) {
        Add-Failure "transparent depth-tested renderables are too low: $($report.frame_contract.renderables.transparent_depth_tested), expected >= $MinTransparentDepthTested"
    }
    if ([int]$report.frame_contract.renderables.water_depth_tested_no_write -lt $MinWaterDepthTestedNoWrite) {
        Add-Failure "water depth-tested/no-write renderables are too low: $($report.frame_contract.renderables.water_depth_tested_no_write), expected >= $MinWaterDepthTestedNoWrite"
    }
}
if ($null -ne $report.frame_contract.materials.very_bright_albedo -and
    [int]$report.frame_contract.materials.very_bright_albedo -ne 0) {
    Add-Failure "very bright non-emissive albedo count is $([int]$report.frame_contract.materials.very_bright_albedo), expected 0"
}
if ($null -ne $report.frame_contract.materials.max_albedo_luminance -and
    [double]$report.frame_contract.materials.max_albedo_luminance -gt ($MaxAlbedoLuminance + 0.001)) {
    Add-Failure "max albedo luminance is $([double]$report.frame_contract.materials.max_albedo_luminance), budget is <= $MaxAlbedoLuminance"
}
if ($null -ne $report.frame_contract.materials.roughness_out_of_range -and
    [int]$report.frame_contract.materials.roughness_out_of_range -ne 0) {
    Add-Failure "roughness out-of-range count is $([int]$report.frame_contract.materials.roughness_out_of_range), expected 0"
}
if ($null -ne $report.frame_contract.materials.metallic_out_of_range -and
    [int]$report.frame_contract.materials.metallic_out_of_range -ne 0) {
    Add-Failure "metallic out-of-range count is $([int]$report.frame_contract.materials.metallic_out_of_range), expected 0"
}
if ($null -ne $report.frame_contract.materials.validation_warnings -and
    [int]$report.frame_contract.materials.validation_warnings -ne 0) {
    Add-Failure "material validation warnings is $([int]$report.frame_contract.materials.validation_warnings), expected 0"
}
if ($null -ne $report.frame_contract.materials.validation_errors -and
    [int]$report.frame_contract.materials.validation_errors -ne 0) {
    Add-Failure "material validation errors is $([int]$report.frame_contract.materials.validation_errors), expected 0"
}
if ($null -ne $report.frame_contract.materials.blend_transmission -and
    [int]$report.frame_contract.materials.blend_transmission -ne 0) {
    Add-Failure "blend+transmission material count is $([int]$report.frame_contract.materials.blend_transmission), expected 0"
}
if ($null -ne $report.frame_contract.materials.metallic_transmission -and
    [int]$report.frame_contract.materials.metallic_transmission -ne 0) {
    Add-Failure "metallic+transmission material count is $([int]$report.frame_contract.materials.metallic_transmission), expected 0"
}
$surfaceClassTotal =
    [int]$report.frame_contract.materials.surface_default +
    [int]$report.frame_contract.materials.surface_glass +
    [int]$report.frame_contract.materials.surface_mirror +
    [int]$report.frame_contract.materials.surface_plastic +
    [int]$report.frame_contract.materials.surface_masonry +
    [int]$report.frame_contract.materials.surface_emissive +
    [int]$report.frame_contract.materials.surface_brushed_metal +
    [int]$report.frame_contract.materials.surface_wood +
    [int]$report.frame_contract.materials.surface_water
if ($surfaceClassTotal -ne [int]$report.frame_contract.materials.sampled) {
    Add-Failure "material surface class counts do not sum to sampled materials: classes=$surfaceClassTotal sampled=$($report.frame_contract.materials.sampled)"
}
if ([int]$report.frame_contract.materials.surface_mirror -lt 1) {
    Add-Failure "RT showcase did not report any mirror-class materials"
}
if ([int]$report.frame_contract.materials.surface_glass -lt 1) {
    Add-Failure "RT showcase did not report any glass-class materials"
}
if ([int]$report.frame_contract.materials.surface_water -lt 1) {
    Add-Failure "RT showcase did not report any water-class materials"
}
if ($null -ne $report.frame_contract.materials.resolved_conductor -and
    [int]$report.frame_contract.materials.resolved_conductor -lt 1) {
    Add-Failure "RT showcase did not report any resolved conductor materials"
}
if ($null -ne $report.frame_contract.materials.resolved_transmissive -and
    [int]$report.frame_contract.materials.resolved_transmissive -lt 1) {
    Add-Failure "RT showcase did not report any resolved transmissive materials"
}
if ($null -ne $report.frame_contract.materials.resolved_emissive -and
    [int]$report.frame_contract.materials.resolved_emissive -lt 1) {
    Add-Failure "RT showcase did not report any resolved emissive materials"
}
if ($null -eq $report.frame_contract.materials.advanced_feature_materials) {
    Add-Failure "RT showcase frame contract is missing advanced material feature coverage"
} else {
    if ([int]$report.frame_contract.materials.advanced_feature_materials -lt 4) {
        Add-Failure "RT showcase advanced material feature coverage is $($report.frame_contract.materials.advanced_feature_materials), expected >= 4"
    }
    if ([int]$report.frame_contract.materials.advanced_clearcoat -lt 1) {
        Add-Failure "RT showcase did not report any advanced clearcoat materials"
    }
    if ([int]$report.frame_contract.materials.advanced_transmission -lt 1) {
        Add-Failure "RT showcase did not report any advanced transmission materials"
    }
    if ([int]$report.frame_contract.materials.advanced_emissive -lt 1) {
        Add-Failure "RT showcase did not report any advanced emissive materials"
    }
    if ([int]$report.frame_contract.materials.advanced_specular -lt 1) {
        Add-Failure "RT showcase did not report any advanced specular materials"
    }
}
if ($null -ne $report.frame_contract.materials.reflection_eligible -and
    [int]$report.frame_contract.materials.reflection_eligible -lt 3) {
    Add-Failure "RT showcase reflection-eligible material count is $($report.frame_contract.materials.reflection_eligible), expected >= 3"
}
if ($null -ne $report.frame_contract.materials.reflection_high_ceiling -and
    [int]$report.frame_contract.materials.reflection_high_ceiling -lt 2) {
    Add-Failure "RT showcase high-ceiling reflection material count is $($report.frame_contract.materials.reflection_high_ceiling), expected >= 2"
}
if ($null -ne $report.frame_contract.materials.max_reflection_ceiling_estimate -and
    [double]$report.frame_contract.materials.max_reflection_ceiling_estimate -lt 0.5) {
    Add-Failure "RT showcase max reflection ceiling estimate is $($report.frame_contract.materials.max_reflection_ceiling_estimate), expected >= 0.5"
}
if ($null -ne $report.frame_contract.ray_tracing.material_surface_parity_comparable -and
    -not [bool]$report.frame_contract.ray_tracing.material_surface_parity_comparable) {
    Add-Failure "RT material/snapshot surface parity was not comparable"
}
if ($null -ne $report.frame_contract.ray_tracing.material_surface_parity_matches -and
    -not [bool]$report.frame_contract.ray_tracing.material_surface_parity_matches) {
    Add-Failure "RT material/snapshot surface parity does not match"
}
if ($null -ne $report.frame_contract.ray_tracing.material_surface_parity_mismatches -and
    [int]$report.frame_contract.ray_tracing.material_surface_parity_mismatches -ne 0) {
    Add-Failure "RT material/snapshot surface parity mismatch count is $($report.frame_contract.ray_tracing.material_surface_parity_mismatches), expected 0"
}
if ($null -ne $report.descriptors.persistent_used -and [int]$report.descriptors.persistent_used -gt $MaxPersistentDescriptors) {
    Add-Failure "persistent descriptors used is $([int]$report.descriptors.persistent_used), budget is <= $MaxPersistentDescriptors"
}
if ($null -ne $report.descriptors.staging_used -and [int]$report.descriptors.staging_used -gt $MaxStagingDescriptors) {
    Add-Failure "staging descriptors used is $([int]$report.descriptors.staging_used), budget is <= $MaxStagingDescriptors"
}
if (-not $AllowStartupRenderTargetReallocation) {
    if (-not (Test-Path $runLogPath)) {
        Add-Failure "run log was not written: $runLogPath"
    } else {
        $runLog = Get-Content $runLogPath -Raw
        $startupReallocationPatterns = @(
            "BeginFrame: reallocating render targets",
            "BeginFrame: recreating depth buffer",
            "BeginFrame: recreating HDR target",
            "BeginFrame: recreating SSAO target"
        )
        foreach ($pattern in $startupReallocationPatterns) {
            if ($runLog.Contains($pattern)) {
                Add-Failure "startup render-target reallocation returned: '$pattern'"
            }
        }
    }
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

$maxObservedTemporalMeanAbsLumaDelta = 0.0
$maxObservedTemporalChangedPixelRatio = 0.0
$temporalDiffSummary = "off"

if ($failures.Count -eq 0 -and $TemporalRuns -gt 1) {
    if (Test-Path $visualPath) {
        Copy-Item -Force $visualPath $temporalBaselineVisualPath
    } else {
        Add-Failure "temporal baseline visual capture is missing: $visualPath"
    }
}

if ($failures.Count -eq 0 -and $TemporalRuns -gt 1) {
    $baselineLuma = [double]$report.visual_validation.image_stats.avg_luma
    $baselineNonBlack = [double]$report.visual_validation.image_stats.nonblack_ratio
    $baselineSaturated = [double]$report.visual_validation.image_stats.saturated_ratio
    $baselineNearWhite = [double]$report.visual_validation.image_stats.near_white_ratio

    for ($runIndex = 2; $runIndex -le $TemporalRuns; ++$runIndex) {
        Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath

        $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
        $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
        $env:CORTEX_DEBUG_CULLING = "1"
        $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"
        if (-not [string]::IsNullOrWhiteSpace($RTBudgetProfile)) {
            $env:CORTEX_RT_BUDGET_PROFILE = $RTBudgetProfile
        }

        $temporalExitCode = Invoke-CortexEngine @("--scene", "rt_showcase", "--camera-bookmark", $CameraBookmark, "--mode=default", "--no-llm", "--no-dreamer", "--no-launcher", "--smoke-frames=$SmokeFrames", "--exit-after-visual-validation")

        Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DEBUG_CULLING -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_RT_BUDGET_PROFILE -ErrorAction SilentlyContinue

        if ($temporalExitCode -ne 0) {
            Add-Failure "temporal smoke run $runIndex failed with exit code $temporalExitCode"
            break
        }
        if (-not (Test-Path $reportPath)) {
            Add-Failure "temporal smoke run $runIndex did not write frame report: $reportPath"
            break
        }

        $temporalReport = Get-Content $reportPath -Raw | ConvertFrom-Json
        if (-not [bool]$temporalReport.visual_validation.captured -or
            -not [bool]$temporalReport.visual_validation.image_stats.valid) {
            Add-Failure "temporal smoke run $runIndex did not produce a valid visual capture"
            break
        }
        $temporalVisualPath = Join-Path $activeLogDir ("visual_validation_rt_showcase_temporal_run{0}.bmp" -f $runIndex)
        if (Test-Path $visualPath) {
            Copy-Item -Force $visualPath $temporalVisualPath
            $diff = Measure-BmpLumaDifference $temporalBaselineVisualPath $temporalVisualPath
            if (-not [bool]$diff.valid) {
                Add-Failure "temporal smoke run $runIndex visual diff failed: $($diff.reason)"
            } else {
                $maxObservedTemporalMeanAbsLumaDelta =
                    [Math]::Max($maxObservedTemporalMeanAbsLumaDelta, [double]$diff.mean_abs_luma_delta)
                $maxObservedTemporalChangedPixelRatio =
                    [Math]::Max($maxObservedTemporalChangedPixelRatio, [double]$diff.changed_pixel_ratio)
                if ([double]$diff.mean_abs_luma_delta -gt $MaxTemporalMeanAbsLumaDelta) {
                    Add-Failure "temporal smoke run $runIndex mean luma diff is $($diff.mean_abs_luma_delta), budget is <= $MaxTemporalMeanAbsLumaDelta"
                }
                if ([double]$diff.changed_pixel_ratio -gt $MaxTemporalChangedPixelRatio) {
                    Add-Failure "temporal smoke run $runIndex changed-pixel ratio is $($diff.changed_pixel_ratio), budget is <= $MaxTemporalChangedPixelRatio"
                }
            }
        } else {
            Add-Failure "temporal smoke run $runIndex did not write visual capture: $visualPath"
        }

        $lumaDelta = [Math]::Abs([double]$temporalReport.visual_validation.image_stats.avg_luma - $baselineLuma)
        $nonBlackDelta = [Math]::Abs([double]$temporalReport.visual_validation.image_stats.nonblack_ratio - $baselineNonBlack)
        $saturatedDelta = [Math]::Abs([double]$temporalReport.visual_validation.image_stats.saturated_ratio - $baselineSaturated)
        $nearWhiteDelta = [Math]::Abs([double]$temporalReport.visual_validation.image_stats.near_white_ratio - $baselineNearWhite)
        if ($lumaDelta -gt $MaxTemporalLumaDelta) {
            Add-Failure "temporal smoke run $runIndex average luma delta is $lumaDelta, budget is <= $MaxTemporalLumaDelta"
        }
        if ($nonBlackDelta -gt $MaxTemporalNonBlackDelta) {
            Add-Failure "temporal smoke run $runIndex nonblack ratio delta is $nonBlackDelta, budget is <= $MaxTemporalNonBlackDelta"
        }
        if ($saturatedDelta -gt $MaxTemporalNonBlackDelta) {
            Add-Failure "temporal smoke run $runIndex saturated ratio delta is $saturatedDelta, budget is <= $MaxTemporalNonBlackDelta"
        }
        if ($nearWhiteDelta -gt $MaxTemporalNonBlackDelta) {
            Add-Failure "temporal smoke run $runIndex near-white ratio delta is $nearWhiteDelta, budget is <= $MaxTemporalNonBlackDelta"
        }
        $report = $temporalReport
    }
    $temporalDiffSummary = ("mean={0:N3}/{1:N1} changed={2:N3}/{3:N2}" -f `
        $maxObservedTemporalMeanAbsLumaDelta,
        $MaxTemporalMeanAbsLumaDelta,
        $maxObservedTemporalChangedPixelRatio,
        $MaxTemporalChangedPixelRatio)
}

$surfaceDebugReport = $null
if ($failures.Count -eq 0 -and $ValidateSurfaceDebug) {
    $mainReportJson = $report | ConvertTo-Json -Depth 64
    if (Test-Path $visualPath) {
        Copy-Item -Force $visualPath $mainVisualPreservedPath
    }
    Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $shutdownReportPath, $runLogPath, $visualPath, $surfaceDebugVisualPath

    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    $env:CORTEX_DEBUG_CULLING = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"
    $env:CORTEX_DEBUG_VIEW = [string]$SurfaceDebugView
    if (-not [string]::IsNullOrWhiteSpace($RTBudgetProfile)) {
        $env:CORTEX_RT_BUDGET_PROFILE = $RTBudgetProfile
    }

    $surfaceDebugExitCode = Invoke-CortexEngine @("--scene", "rt_showcase", "--camera-bookmark", $CameraBookmark, "--mode=default", "--no-llm", "--no-dreamer", "--no-launcher", "--smoke-frames=$SmokeFrames", "--exit-after-visual-validation")

    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DEBUG_CULLING -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DEBUG_VIEW -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_RT_BUDGET_PROFILE -ErrorAction SilentlyContinue

    if ($surfaceDebugExitCode -ne 0) {
        Add-Failure "surface-class debug smoke failed with exit code $surfaceDebugExitCode"
    } elseif (-not (Test-Path $reportPath)) {
        Add-Failure "surface-class debug smoke did not write frame report: $reportPath"
    } else {
        $surfaceDebugReport = Get-Content $reportPath -Raw | ConvertFrom-Json
        Copy-Item -Force $reportPath $surfaceDebugReportPath
        $mainReportJson | Set-Content -Path $reportPath -Encoding UTF8
        if ([int]$surfaceDebugReport.renderer.debug_view_mode -ne $SurfaceDebugView) {
            Add-Failure "surface-class debug smoke captured debug view $($surfaceDebugReport.renderer.debug_view_mode), expected $SurfaceDebugView"
        }
        if (-not [bool]$surfaceDebugReport.visual_validation.captured -or
            -not [bool]$surfaceDebugReport.visual_validation.image_stats.valid) {
            Add-Failure "surface-class debug smoke did not produce a valid visual capture"
        } else {
            if ([double]$surfaceDebugReport.visual_validation.image_stats.nonblack_ratio -lt $MinSurfaceDebugNonBlackRatio) {
                Add-Failure "surface-class debug nonblack ratio is $($surfaceDebugReport.visual_validation.image_stats.nonblack_ratio), expected >= $MinSurfaceDebugNonBlackRatio"
            }
            if ([double]$surfaceDebugReport.visual_validation.image_stats.colorful_ratio -lt $MinSurfaceDebugColorfulRatio) {
                Add-Failure "surface-class debug colorful ratio is $($surfaceDebugReport.visual_validation.image_stats.colorful_ratio), expected >= $MinSurfaceDebugColorfulRatio"
            }
            if (Test-Path $visualPath) {
                Copy-Item -Force $visualPath $surfaceDebugVisualPath
            }
            if (Test-Path $mainVisualPreservedPath) {
                Copy-Item -Force $mainVisualPreservedPath $visualPath
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "RT showcase smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "RT showcase smoke passed" -ForegroundColor Green
Write-Host " logs=$activeLogDir"
$surfaceDebugSummary = "off"
if ($null -ne $surfaceDebugReport -and $null -ne $surfaceDebugReport.visual_validation.image_stats) {
    $surfaceDebugSummary = ("view={0} colorful={1:N3} nonblack={2:N3}" -f `
        [int]$surfaceDebugReport.renderer.debug_view_mode,
        [double]$surfaceDebugReport.visual_validation.image_stats.colorful_ratio,
        [double]$surfaceDebugReport.visual_validation.image_stats.nonblack_ratio)
}

Write-Host (" frames={0} gpu_ms={1:N3}/{2:N1} dxgi_mb={3:N2}/{4:N0} est_mb={5:N2}/{6:N0} rt_mb={7:N2}/{8:N0} write_mb={9:N2}/{10:N0} luma={11:N2} center_luma={12:N2} dark={13:N3}/{14:N2} sat={15:N3}/{16:N2} near_white={17:N3}/{18:N2} water_draws={19}/{20} max_albedo={21:N3}/{22:N1} material_issues={23} resolved_mat={24}/{25}/{26} preset_defaults={27}/{28}/{29}/{30} reflection={31}/{32}/{33:N2}/{34:N2} rt_parity={35}/{36} rt_refl_ready={37}/{38} rt_signal={39:N4}/{40:N4}/{41:N4}/{42:N4} rt_hist={43:N4}/{44:N4}/{45:N4}/{46:N4} transient_delta={47} rt_budget={48} lighting={49}/{50} startup_realloc=0 temporal_diff={51} surface_debug={52} advanced_mat={53}/{54}/{55}/{56}/{57}" -f `
    $report.smoke_automation.total_frames,
    [double]$report.gpu_frame_ms,
    $MaxGpuFrameMs,
    [double]$report.dxgi_memory_mb.current_usage,
    $MaxDxgiMemoryMb,
    [double]$report.memory_mb.total_estimated,
    $MaxEstimatedMemoryMb,
    [double]$report.memory_mb.render_targets,
    $MaxRenderTargetMemoryMb,
    [double]$report.frame_contract.pass_budget_summary.estimated_write_mb_total,
    $MaxEstimatedWriteMb,
    [double]$report.visual_validation.image_stats.avg_luma,
    [double]$report.visual_validation.image_stats.center_avg_luma,
    [double]$report.visual_validation.image_stats.dark_detail_ratio,
    $MaxVisualDarkDetailRatio,
    [double]$report.visual_validation.image_stats.saturated_ratio,
    $MaxVisualSaturatedRatio,
    [double]$report.visual_validation.image_stats.near_white_ratio,
    $MaxVisualNearWhiteRatio,
    [int]$report.frame_contract.draw_counts.water_draws,
    $MaxWaterDraws,
    [double]$report.frame_contract.materials.max_albedo_luminance,
    $MaxAlbedoLuminance,
    [int]$report.frame_contract.materials.validation_issues,
    [int]$report.frame_contract.materials.resolved_conductor,
    [int]$report.frame_contract.materials.resolved_transmissive,
    [int]$report.frame_contract.materials.resolved_emissive,
    [int]$report.frame_contract.materials.preset_default_metallic,
    [int]$report.frame_contract.materials.preset_default_roughness,
    [int]$report.frame_contract.materials.preset_default_transmission,
    [int]$report.frame_contract.materials.preset_default_emission,
    [int]$report.frame_contract.materials.reflection_eligible,
    [int]$report.frame_contract.materials.reflection_high_ceiling,
    [double]$report.frame_contract.materials.max_reflection_ceiling_estimate,
    [double]$report.frame_contract.materials.avg_reflection_ceiling_estimate,
    [bool]$report.frame_contract.ray_tracing.material_surface_parity_matches,
    [int]$report.frame_contract.ray_tracing.material_surface_parity_mismatches,
    [bool]$report.frame_contract.ray_tracing.reflection_dispatch_ready,
    [string]$report.frame_contract.ray_tracing.reflection_readiness_reason,
    [double]$report.frame_contract.ray_tracing.reflection_signal_avg_luma,
    [double]$report.frame_contract.ray_tracing.reflection_signal_nonzero_ratio,
    [double]$report.frame_contract.ray_tracing.reflection_signal_max_luma,
    [double]$report.frame_contract.ray_tracing.reflection_signal_outlier_ratio,
    [double]$report.frame_contract.ray_tracing.reflection_history_signal_avg_luma,
    [double]$report.frame_contract.ray_tracing.reflection_history_signal_nonzero_ratio,
    [double]$report.frame_contract.ray_tracing.reflection_history_signal_max_luma,
    [double]$report.frame_contract.ray_tracing.reflection_history_signal_avg_luma_delta,
    [int]$report.frame_contract.pass_budget_summary.transient_descriptor_delta_total,
    [string]$report.frame_contract.ray_tracing.budget_profile,
    [string]$report.frame_contract.lighting.rig_id,
    [int]$report.frame_contract.lighting.light_count,
    $temporalDiffSummary,
    $surfaceDebugSummary,
    [int]$report.frame_contract.materials.advanced_feature_materials,
    [int]$report.frame_contract.materials.advanced_clearcoat,
    [int]$report.frame_contract.materials.advanced_transmission,
    [int]$report.frame_contract.materials.advanced_emissive,
    [int]$report.frame_contract.materials.advanced_specular)
