param(
    [string]$BaselinePath = "",
    [switch]$RuntimeSmoke,
    [switch]$NoBuild,
    [int]$MaxRuntimeCases = 1,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $BaselinePath = Join-Path $root "assets/config/visual_baselines.json"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "visual_baseline_contract_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Load-Json([string]$Path, [string]$Name) {
    if (-not (Test-Path $Path)) {
        throw "$Name not found: $Path"
    }
    return Get-Content $Path -Raw | ConvertFrom-Json
}

function Get-SceneById([object]$Doc, [string]$Id) {
    foreach ($scene in $Doc.scenes) {
        if ([string]$scene.id -eq $Id) {
            return $scene
        }
    }
    return $null
}

function Has-Bookmark([object]$Scene, [string]$BookmarkId) {
    if ($null -eq $Scene -or $null -eq $Scene.camera_bookmarks) {
        return $false
    }
    foreach ($bookmark in $Scene.camera_bookmarks) {
        if ([string]$bookmark.id -eq $BookmarkId) {
            return $true
        }
    }
    return $false
}

function Has-Preset([object]$Doc, [string]$Id) {
    foreach ($preset in $Doc.presets) {
        if ([string]$preset.id -eq $Id) {
            return $true
        }
    }
    return $false
}

function Has-Environment([object]$Doc, [string]$Id) {
    foreach ($entry in $Doc.environments) {
        if ([string]$entry.id -eq $Id) {
            return $true
        }
    }
    return $false
}

function Test-MetricRange([string]$CaseId, [string]$MetricName, [object]$Spec) {
    if ($null -eq $Spec) {
        Add-Failure "$CaseId metric '$MetricName' is missing"
        return
    }
    if ($null -ne $Spec.min -and $null -ne $Spec.max -and [double]$Spec.min -gt [double]$Spec.max) {
        Add-Failure "$CaseId metric '$MetricName' has min greater than max"
    }
}

function Check-RuntimeMetric([string]$CaseId, [string]$MetricName, [double]$Value, [object]$Spec) {
    if ($null -eq $Spec) {
        return
    }
    if ($null -ne $Spec.min -and $Value -lt [double]$Spec.min) {
        Add-Failure "$CaseId $MetricName=$Value below min $($Spec.min)"
    }
    if ($null -ne $Spec.max -and $Value -gt [double]$Spec.max) {
        Add-Failure "$CaseId $MetricName=$Value above max $($Spec.max)"
    }
}

$baselineDoc = Load-Json $BaselinePath "Visual baseline manifest"
$showcaseDoc = Load-Json (Join-Path $root "assets/config/showcase_scenes.json") "Showcase scene config"
$presetDoc = Load-Json (Join-Path $root "assets/config/graphics_presets.json") "Graphics preset config"
$environmentDoc = Load-Json (Join-Path $root "assets/environments/environments.json") "Environment manifest"

if ([int]$baselineDoc.schema -ne 1) {
    Add-Failure "visual baseline schema must be 1"
}
if ([string]$baselineDoc.policy.baseline_type -ne "metric_tolerance") {
    Add-Failure "visual baseline policy.baseline_type must be metric_tolerance"
}
if ([bool]$baselineDoc.policy.image_assets_committed) {
    Add-Failure "visual baseline policy should not require committed image assets yet"
}
if ($null -eq $baselineDoc.cases -or $baselineDoc.cases.Count -lt 1) {
    Add-Failure "visual baseline case list is empty"
}

$caseIds = @{}
foreach ($case in $baselineDoc.cases) {
    $caseId = [string]$case.id
    if ([string]::IsNullOrWhiteSpace($caseId)) {
        Add-Failure "visual baseline case id is missing"
        continue
    }
    if ($caseIds.ContainsKey($caseId)) {
        Add-Failure "duplicate visual baseline case id '$caseId'"
    }
    $caseIds[$caseId] = $true

    $sceneId = [string]$case.scene
    $bookmarkId = [string]$case.camera_bookmark
    $scene = Get-SceneById $showcaseDoc $sceneId
    if ($null -eq $scene) {
        Add-Failure "$caseId references unknown showcase scene '$sceneId'"
    } elseif (-not (Has-Bookmark $scene $bookmarkId)) {
        Add-Failure "$caseId references unknown bookmark '${sceneId}:$bookmarkId'"
    }

    if (-not (Has-Preset $presetDoc ([string]$case.graphics_preset))) {
        Add-Failure "$caseId references unknown graphics preset '$($case.graphics_preset)'"
    }
    if (-not (Has-Environment $environmentDoc ([string]$case.environment))) {
        Add-Failure "$caseId references unknown environment '$($case.environment)'"
    }
    if ([string]::IsNullOrWhiteSpace([string]$case.lighting_rig)) {
        Add-Failure "$caseId lighting_rig is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$case.capture.filename)) {
        Add-Failure "$caseId capture.filename is missing"
    }
    if ([int]$case.capture.smoke_frames -lt [int]$baselineDoc.policy.warmup_frames) {
        Add-Failure "$caseId smoke_frames must be >= warmup_frames"
    }

    Test-MetricRange $caseId "gpu_frame_ms" $case.metrics.gpu_frame_ms
    Test-MetricRange $caseId "avg_luma" $case.metrics.avg_luma
    Test-MetricRange $caseId "center_avg_luma" $case.metrics.center_avg_luma
    Test-MetricRange $caseId "nonblack_ratio" $case.metrics.nonblack_ratio
    Test-MetricRange $caseId "saturated_ratio" $case.metrics.saturated_ratio
    Test-MetricRange $caseId "near_white_ratio" $case.metrics.near_white_ratio
    Test-MetricRange $caseId "dark_detail_ratio" $case.metrics.dark_detail_ratio
}

if ($RuntimeSmoke -and $failures.Count -eq 0) {
    $exe = Join-Path $root "build/bin/CortexEngine.exe"
    if (-not $NoBuild) {
        cmake --build (Join-Path $root "build") --config Release --target CortexEngine
    }
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
    } else {
        $caseIndex = 0
        foreach ($case in $baselineDoc.cases) {
            if ($caseIndex -ge $MaxRuntimeCases) {
                break
            }
            $caseIndex++
            $caseLogDir = Join-Path $LogDir ([string]$case.id)
            New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

            $env:CORTEX_LOG_DIR = $caseLogDir
            $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
            $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
            $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = [string]$baselineDoc.policy.warmup_frames

            Push-Location (Split-Path -Parent $exe)
            try {
                $output = & $exe `
                    "--scene" ([string]$case.scene) `
                    "--camera-bookmark" ([string]$case.camera_bookmark) `
                    "--environment" ([string]$case.environment) `
                    "--graphics-preset" ([string]$case.graphics_preset) `
                    "--mode=default" `
                    "--no-llm" `
                    "--no-dreamer" `
                    "--no-launcher" `
                    "--smoke-frames=$($case.capture.smoke_frames)" `
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
            if ($exitCode -ne 0) {
                Add-Failure "$($case.id) runtime visual baseline failed with exit code $exitCode. logs=$caseLogDir"
                continue
            }
            if (-not (Test-Path $reportPath)) {
                Add-Failure "$($case.id) did not write frame_report_last.json. logs=$caseLogDir"
                continue
            }
            $report = Get-Content $reportPath -Raw | ConvertFrom-Json
            if ([string]$report.scene -ne [string]$case.scene) {
                Add-Failure "$($case.id) runtime scene '$($report.scene)' != '$($case.scene)'"
            }
            if ([string]$report.camera.bookmark -ne [string]$case.camera_bookmark) {
                Add-Failure "$($case.id) runtime bookmark '$($report.camera.bookmark)' != '$($case.camera_bookmark)'"
            }
            if ([string]$report.frame_contract.environment.active -ne [string]$case.environment) {
                Add-Failure "$($case.id) runtime environment '$($report.frame_contract.environment.active)' != '$($case.environment)'"
            }
            if ([string]$report.frame_contract.graphics_preset.id -ne [string]$case.graphics_preset) {
                Add-Failure "$($case.id) runtime graphics preset '$($report.frame_contract.graphics_preset.id)' != '$($case.graphics_preset)'"
            }
            if ([string]$report.frame_contract.lighting.rig_id -ne [string]$case.lighting_rig) {
                Add-Failure "$($case.id) runtime lighting rig '$($report.frame_contract.lighting.rig_id)' != '$($case.lighting_rig)'"
            }
            if (-not [bool]$report.visual_validation.captured -or
                -not [bool]$report.visual_validation.image_stats.valid) {
                Add-Failure "$($case.id) runtime visual capture is invalid"
                continue
            }
            $visualPath = Join-Path $caseLogDir ([string]$case.capture.filename)
            if (-not (Test-Path $visualPath)) {
                Add-Failure "$($case.id) visual capture missing at $visualPath"
            }

            Check-RuntimeMetric ([string]$case.id) "gpu_frame_ms" ([double]$report.gpu_frame_ms) $case.metrics.gpu_frame_ms
            Check-RuntimeMetric ([string]$case.id) "avg_luma" ([double]$report.visual_validation.image_stats.avg_luma) $case.metrics.avg_luma
            Check-RuntimeMetric ([string]$case.id) "center_avg_luma" ([double]$report.visual_validation.image_stats.center_avg_luma) $case.metrics.center_avg_luma
            Check-RuntimeMetric ([string]$case.id) "nonblack_ratio" ([double]$report.visual_validation.image_stats.nonblack_ratio) $case.metrics.nonblack_ratio
            Check-RuntimeMetric ([string]$case.id) "saturated_ratio" ([double]$report.visual_validation.image_stats.saturated_ratio) $case.metrics.saturated_ratio
            Check-RuntimeMetric ([string]$case.id) "near_white_ratio" ([double]$report.visual_validation.image_stats.near_white_ratio) $case.metrics.near_white_ratio
            Check-RuntimeMetric ([string]$case.id) "dark_detail_ratio" ([double]$report.visual_validation.image_stats.dark_detail_ratio) $case.metrics.dark_detail_ratio
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Visual baseline contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Visual baseline contract tests passed: cases=$($baselineDoc.cases.Count)" -ForegroundColor Green
if ($RuntimeSmoke) {
    Write-Host "logs=$LogDir"
}
