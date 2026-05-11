param(
    [int]$SmokeFrames = 180,
    [double]$MaxGpuFrameMs = 16.7,
    [double]$MinVisualAvgLuma = 30.0,
    [double]$MaxVisualAvgLuma = 185.0,
    [double]$MinVisualCenterLuma = 20.0,
    [double]$MaxVisualCenterLuma = 210.0,
    [double]$MinVisualNonBlackRatio = 0.80,
    [double]$MaxVisualSaturatedRatio = 0.20,
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
    $runId = "effects_showcase_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
    $env:CORTEX_LOG_DIR = $activeLogDir
}

$reportPath = Join-Path $activeLogDir "frame_report_last.json"
$visualPath = Join-Path $activeLogDir "visual_validation_rt_showcase.bmp"
$runLogPath = Join-Path $activeLogDir "cortex_last_run.txt"

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $visualPath, $runLogPath

$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"
$env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

Push-Location (Split-Path -Parent $exe)
try {
    $output = & $exe `
        "--scene" "effects_showcase" `
        "--camera-bookmark" "hero" `
        "--environment" "night_city" `
        "--graphics-preset" "release_showcase" `
        "--mode=default" `
        "--no-llm" `
        "--no-dreamer" `
        "--no-launcher" `
        "--smoke-frames=$SmokeFrames" `
        "--exit-after-visual-validation" 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -Encoding UTF8 (Join-Path $activeLogDir "engine_stdout.txt")
} finally {
    Pop-Location
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
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

if ($exitCode -ne 0) {
    Add-Failure "Effects Showcase smoke process failed with exit code $exitCode"
}
if (-not (Test-Path $reportPath)) {
    Add-Failure "Expected frame report was not written: $reportPath"
}

if ($failures.Count -eq 0) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $materials = $report.frame_contract.materials
    $particles = $report.frame_contract.particles
    $cinematicPost = $report.frame_contract.cinematic_post

    if ([string]$report.scene -ne "effects_showcase") {
        Add-Failure "expected effects_showcase scene but report scene was '$($report.scene)'"
    }
    if (-not [bool]$report.camera.active -or [string]$report.camera.bookmark -ne "hero") {
        Add-Failure "Effects Showcase did not report the hero camera bookmark"
    }
    if ($report.health_warnings.Count -ne 0) {
        Add-Failure "health_warnings is not empty: $($report.health_warnings -join ', ')"
    }
    if ($report.frame_contract.warnings.Count -ne 0) {
        Add-Failure "frame_contract warnings is not empty: $($report.frame_contract.warnings -join ', ')"
    }
    if ([double]$report.gpu_frame_ms -gt $MaxGpuFrameMs) {
        Add-Failure "gpu_frame_ms is $($report.gpu_frame_ms), budget is <= $MaxGpuFrameMs"
    }
    if ([string]$report.frame_contract.environment.active -ne "night_city") {
        Add-Failure "environment is '$($report.frame_contract.environment.active)', expected night_city"
    }
    if ([string]$report.frame_contract.lighting.rig_id -ne "night_emissive") {
        Add-Failure "lighting rig is '$($report.frame_contract.lighting.rig_id)', expected night_emissive"
    }
    if ([string]$report.frame_contract.graphics_preset.id -ne "release_showcase") {
        Add-Failure "graphics preset is '$($report.frame_contract.graphics_preset.id)', expected release_showcase"
    }

    if (-not [bool]$report.visual_validation.captured -or
        -not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "visual validation capture is invalid"
    } else {
        $stats = $report.visual_validation.image_stats
        if ([double]$stats.avg_luma -lt $MinVisualAvgLuma -or [double]$stats.avg_luma -gt $MaxVisualAvgLuma) {
            Add-Failure "visual avg_luma=$($stats.avg_luma), expected [$MinVisualAvgLuma, $MaxVisualAvgLuma]"
        }
        if ([double]$stats.center_avg_luma -lt $MinVisualCenterLuma -or [double]$stats.center_avg_luma -gt $MaxVisualCenterLuma) {
            Add-Failure "visual center_avg_luma=$($stats.center_avg_luma), expected [$MinVisualCenterLuma, $MaxVisualCenterLuma]"
        }
        if ([double]$stats.nonblack_ratio -lt $MinVisualNonBlackRatio) {
            Add-Failure "visual nonblack_ratio=$($stats.nonblack_ratio), expected >= $MinVisualNonBlackRatio"
        }
        if ([double]$stats.saturated_ratio -gt $MaxVisualSaturatedRatio) {
            Add-Failure "visual saturated_ratio=$($stats.saturated_ratio), expected <= $MaxVisualSaturatedRatio"
        }
    }
    if (-not (Test-Path $visualPath)) {
        Add-Failure "visual validation image missing: $visualPath"
    }

    if ($null -eq $particles) {
        Add-Failure "frame_contract.particles is missing"
    } else {
        if (-not [bool]$particles.enabled) { Add-Failure "particles.enabled is false" }
        if (-not [bool]$particles.planned) { Add-Failure "particles.planned is false" }
        if (-not [bool]$particles.executed) { Add-Failure "particles.executed is false" }
        if ([bool]$particles.instance_map_failed) { Add-Failure "particles.instance_map_failed is true" }
        if ([bool]$particles.capped) { Add-Failure "particles.capped is true" }
        if ([int]$particles.emitter_count -lt 2) { Add-Failure "expected at least two particle emitters" }
        if ([int]$particles.live_particles -lt 8) { Add-Failure "expected live particles" }
        if ([int]$particles.submitted_instances -lt 8) { Add-Failure "expected submitted particle instances" }
        if ([int]$particles.submitted_instances -ne [int]$report.frame_contract.draw_counts.particle_instances) {
            Add-Failure "particle contract submitted_instances ($($particles.submitted_instances)) does not match draw count ($($report.frame_contract.draw_counts.particle_instances))"
        }
        if ([string]$particles.runtime_backend -ne "ecs_cpu_sim_dx12_instanced_billboard") {
            Add-Failure "particle runtime_backend was '$($particles.runtime_backend)', expected ecs_cpu_sim_dx12_instanced_billboard"
        }
        if (-not [bool]$particles.public_runtime_path) {
            Add-Failure "particles.public_runtime_path is false"
        }
        if (-not [bool]$particles.cpu_simulation_path) {
            Add-Failure "particles.cpu_simulation_path is false for the current ECS simulation path"
        }
        if ([bool]$particles.gpu_simulation_path) {
            Add-Failure "particles.gpu_simulation_path unexpectedly reported true before the GPU simulation path is public"
        }
        if ([bool]$particles.gpu_sort_path) {
            Add-Failure "particles.gpu_sort_path unexpectedly reported true before the GPU sort path is public"
        }
        if (-not [bool]$particles.gpu_draw_path) {
            Add-Failure "particles.gpu_draw_path is false; particles should still render through DX12 instancing"
        }
        if ([string]$particles.simulation_backend -ne "ecs_cpu") {
            Add-Failure "particle simulation_backend was '$($particles.simulation_backend)', expected ecs_cpu"
        }
        if ([string]$particles.render_backend -ne "dx12_instanced_billboard") {
            Add-Failure "particle render_backend was '$($particles.render_backend)', expected dx12_instanced_billboard"
        }
        if ([bool]$particles.gpu_particle_public_path) {
            Add-Failure "particles.gpu_particle_public_path unexpectedly reported true before GPU simulation/sort/render is public"
        }
        if (-not [bool]$particles.simulation_budget_tracked) {
            Add-Failure "particles.simulation_budget_tracked is false"
        }
        if ([int]$particles.simulation_budget_particles -lt [int]$particles.submitted_instances) {
            Add-Failure "particle simulation budget is smaller than submitted instances"
        }
        if ([int64]$particles.simulation_budget_bytes -le 0) {
            Add-Failure "particle simulation_budget_bytes is not positive"
        }
        if ([int64]$particles.upload_bytes_this_frame -le 0) {
            Add-Failure "particle upload_bytes_this_frame is not positive"
        }
    }

    $particlePass = Get-FrameContractPass $report "Particles"
    if ($null -eq $particlePass) {
        Add-Failure "Particles pass record is missing"
    } elseif (-not [bool]$particlePass.executed) {
        Add-Failure "Particles pass record was not executed"
    }

    if ($null -eq $cinematicPost) {
        Add-Failure "frame_contract.cinematic_post is missing"
    } else {
        if (-not [bool]$cinematicPost.enabled) {
            Add-Failure "cinematic_post.enabled is false"
        }
        if (-not [bool]$cinematicPost.post_process_planned) {
            Add-Failure "cinematic_post.post_process_planned is false"
        }
        if (-not [bool]$cinematicPost.post_process_executed) {
            Add-Failure "cinematic_post.post_process_executed is false"
        }
        if (-not [bool]$cinematicPost.budget_tracked) {
            Add-Failure "cinematic_post.budget_tracked is false"
        }
        if ($null -eq $cinematicPost.PSObject.Properties["estimated_write_mb"] -or
            [double]$cinematicPost.estimated_write_mb -le 0.0) {
            Add-Failure "cinematic_post.estimated_write_mb is not positive"
        }
        if ($null -eq $cinematicPost.PSObject.Properties["full_screen_passes"] -or
            [int]$cinematicPost.full_screen_passes -lt 1) {
            Add-Failure "cinematic_post.full_screen_passes is not tracked"
        }
        if ($null -eq $cinematicPost.PSObject.Properties["active_effect_count"] -or
            [int]$cinematicPost.active_effect_count -lt 4) {
            Add-Failure "cinematic_post.active_effect_count is too low"
        }
        foreach ($field in @("motion_blur_budgeted", "depth_of_field_budgeted", "vignette_budgeted", "lens_dirt_budgeted")) {
            if ($null -eq $cinematicPost.PSObject.Properties[$field]) {
                Add-Failure "cinematic_post.$field is missing"
            } elseif (-not [bool]$cinematicPost.PSObject.Properties[$field].Value) {
                Add-Failure "cinematic_post.$field is false"
            }
        }
        if ([double]$cinematicPost.vignette -le 0.0) {
            Add-Failure "cinematic_post.vignette is not active"
        }
        if ([double]$cinematicPost.lens_dirt -le 0.0) {
            Add-Failure "cinematic_post.lens_dirt is not active"
        }
        if ([double]$cinematicPost.motion_blur -le 0.0) {
            Add-Failure "cinematic_post.motion_blur is not active"
        }
        if ([double]$cinematicPost.depth_of_field -le 0.0) {
            Add-Failure "cinematic_post.depth_of_field is not active"
        }
        if ([string]$cinematicPost.tone_mapper_preset -ne "aces") {
            Add-Failure "cinematic_post.tone_mapper_preset was '$($cinematicPost.tone_mapper_preset)', expected aces from release_showcase preset"
        }
    }

    if ([int]$materials.sampled -lt 10) {
        Add-Failure "sampled material count is $($materials.sampled), expected >= 10"
    }
    if ([int]$materials.validation_warnings -ne 0 -or [int]$materials.validation_errors -ne 0) {
        Add-Failure "material validation issues: warnings=$($materials.validation_warnings) errors=$($materials.validation_errors)"
    }
    if ([int]$materials.surface_emissive -lt 1 -or
        [int]$materials.surface_glass -lt 1 -or
        [int]$materials.surface_brushed_metal -lt 1) {
        Add-Failure "effects material surface coverage is incomplete"
    }
    if ([int]$materials.advanced_feature_materials -lt 3 -or
        [int]$materials.advanced_clearcoat -lt 1 -or
        [int]$materials.advanced_transmission -lt 1 -or
        [int]$materials.advanced_emissive -lt 1) {
        Add-Failure "effects advanced material coverage is incomplete"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Effects Showcase smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Effects Showcase smoke passed" -ForegroundColor Green
Write-Host " logs=$activeLogDir"
