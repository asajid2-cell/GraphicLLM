param(
    [switch]$NoBuild,
    [int]$TemporalSmokeFrames = 140,
    [int]$RTSmokeFrames = 240,
    [int]$IBLGalleryMaxEnvironments = 0,
    [int]$BudgetTemporalRuns = 1,
    [int]$BudgetMaxParallel = 1,
    [int]$VoxelSmokeFrames = 120,
    [int]$StepRetries = 1
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
    $maxAttempts = 1 + [Math]::Max(0, $script:StepRetries)
    $lastStdout = ""
    $lastStderr = ""
    $lastLogDir = ""
    $lastExitCode = 0

    for ($attempt = 1; $attempt -le $maxAttempts; ++$attempt) {
        $attemptName = if ($attempt -eq 1) { $Name } else { "{0}_retry{1}" -f $Name, $attempt }
        $stepLogDir = Join-Path $script:releaseLogDir $attemptName
        New-Item -ItemType Directory -Force -Path $stepLogDir | Out-Null

        $stdoutPath = Join-Path $stepLogDir "stdout.txt"
        $stderrPath = Join-Path $stepLogDir "stderr.txt"
        $started = Get-Date

        if ($attempt -eq 1) {
            Write-Host "==> $Name" -ForegroundColor Cyan
        } else {
            Write-Host "==> $Name retry $attempt/$maxAttempts" -ForegroundColor Yellow
        }

        try {
            $child = Start-Process `
                -FilePath "powershell.exe" `
                -ArgumentList $Arguments `
                -NoNewWindow `
                -RedirectStandardOutput $stdoutPath `
                -RedirectStandardError $stderrPath `
                -Wait `
                -PassThru
            $exitCode = $child.ExitCode
        } catch {
            $_.Exception.Message | Set-Content -Encoding UTF8 $stdoutPath
            "" | Set-Content -Encoding UTF8 $stderrPath
            $exitCode = 1
        }

        $elapsed = [Math]::Round(((Get-Date) - $started).TotalSeconds, 1)
        $stdoutText = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
        $stderrText = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }
        if ($exitCode -eq 0 -and $stdoutText -match "(?im)^\s*[A-Za-z0-9 _/'`"-]+ failed:") {
            $exitCode = 1
            $stdoutText = $stdoutText + [Environment]::NewLine + "Release validation detected a failure sentinel in stdout."
            $stdoutText | Set-Content -Encoding UTF8 $stdoutPath
        }
        $lastStdout = $stdoutText
        $lastStderr = $stderrText
        $lastLogDir = $stepLogDir
        $lastExitCode = $exitCode

        $script:steps.Add([pscustomobject]@{
            Name = $attemptName
            ExitCode = $exitCode
            Seconds = $elapsed
            LogDir = $stepLogDir
        })

        if ($exitCode -eq 0) {
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
            return
        }

        Write-Host "FAILED $Name attempt $attempt in ${elapsed}s" -ForegroundColor Red
    }

    if ($lastExitCode -ne 0) {
        $script:failures.Add(
            "$Name failed with exit code $lastExitCode after $maxAttempts attempt(s). logs=$lastLogDir`n$lastStderr`n$lastStdout")
    }
}

function Write-ReleaseSummary([string]$Status) {
    $summaryPath = Join-Path $script:releaseLogDir "release_validation_summary.json"
    $stepArray = @($script:steps.ToArray())
    $failureArray = @($script:failures.ToArray())
    $summary = [pscustomobject][ordered]@{
        schema = 1
        status = $Status
        run_id = $script:runId
        log_dir = $script:releaseLogDir
        no_build = [bool]$script:NoBuild
        temporal_smoke_frames = $script:TemporalSmokeFrames
        rt_smoke_frames = $script:RTSmokeFrames
        ibl_gallery_max_environments = $script:IBLGalleryMaxEnvironments
        budget_temporal_runs = $script:BudgetTemporalRuns
        budget_max_parallel = $script:BudgetMaxParallel
        voxel_smoke_frames = $script:VoxelSmokeFrames
        step_retries = $script:StepRetries
        step_count = $script:steps.Count
        failure_count = $script:failures.Count
        steps = $stepArray
        failures = $failureArray
    }
    $summary | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $summaryPath
    Write-Host "summary=$summaryPath"
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
    Invoke-ReleaseStep "build_entrypoint_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_build_entrypoint_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "repo_hygiene" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_repo_hygiene_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "public_readme_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_public_readme_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "source_list_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_source_list_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "render_graph_boundary_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_render_graph_boundary_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "render_graph_declaration_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_render_graph_declaration_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "visibility_buffer_transition_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_visibility_buffer_transition_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "debug_primitive_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_debug_primitive_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "depth_stability_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_depth_stability_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "editor_frame_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_editor_frame_contract_tests.ps1")
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
    Invoke-ReleaseStep "temporal_camera_cut" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_temporal_camera_cut_validation.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "camera_motion_stability" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_camera_motion_stability_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
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
    Invoke-ReleaseStep "rt_reflection_history_quality" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_reflection_history_quality_tests.ps1"),
        "-NoBuild",
        "-SmokeFrames", [string]$RTSmokeFrames
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "rt_gi_visual_signal" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_gi_visual_signal_tests.ps1"),
        "-NoBuild",
        "-SmokeFrames", [string]$RTSmokeFrames
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "vb_debug_views" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_vb_debug_views.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "descriptor_memory_stress" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_descriptor_memory_stress_scene.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "render_graph_transient_matrix" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_render_graph_transient_matrix.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "graphics_settings_persistence" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_graphics_settings_persistence_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "graphics_ui_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_graphics_ui_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "graphics_ui_interaction" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_graphics_ui_interaction_smoke.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "graphics_native_widget" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_graphics_native_widget_smoke.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "graphics_material_controls" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_graphics_material_controls_smoke.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "llm_renderer_command" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_llm_renderer_command_smoke.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "dreamer_positive_runtime" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_dreamer_positive_runtime_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "hud_mode_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_hud_mode_contract_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "graphics_preset" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_graphics_preset_tests.ps1"),
        "-NoBuild",
        "-RuntimeSmoke"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "launcher_native_widget" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_launcher_native_widget_smoke.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "showcase_scene_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_showcase_scene_contract_tests.ps1"),
        "-NoBuild",
        "-RuntimeSmoke"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "material_editor_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_material_editor_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "material_robustness_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_material_robustness_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "material_editor_native_widget" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_material_editor_native_widget_smoke.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "material_lab" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_material_lab_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "material_path_equivalence" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_material_path_equivalence_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "conductor_energy_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_conductor_energy_contract_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "lighting_energy_budget" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_lighting_energy_budget_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "vegetation_state_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_vegetation_state_contract_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "reflection_probe_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_reflection_probe_contract_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "liquid_graphics_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_liquid_graphics_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "glass_water_courtyard" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_glass_water_courtyard_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "liquid_gallery" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_liquid_gallery_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "visual_baseline_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_visual_baseline_contract_tests.ps1"),
        "-NoBuild",
        "-RuntimeSmoke",
        "-MaxRuntimeCases", "1"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "visual_probe_validation" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_visual_probe_validation.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "screenshot_negative_gates" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_screenshot_negative_gates.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "phase3_visual_matrix" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_phase3_visual_matrix.ps1"),
        "-NoBuild",
        "-TemporalSmokeFrames", "90",
        "-RTSmokeFrames", "180",
        "-IBLGalleryMaxEnvironments", [string]$IBLGalleryMaxEnvironments,
        "-SkipSurfaceDebug"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "renderer_ownership" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_renderer_ownership_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "renderer_full_ownership_audit" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_renderer_full_ownership_audit.ps1"),
        "-LogDir", (Join-Path $releaseLogDir "renderer_full_ownership_audit_artifacts")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "minimal_frame" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_minimal_frame_smoke.ps1"),
        "-NoBuild",
        "-IsolatedLogs"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "fatal_error_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_fatal_error_contract_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "advanced_graphics_catalog" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_advanced_graphics_catalog_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "gpu_particle_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_gpu_particle_contract_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "effects_gallery" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_effects_gallery_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "particle_disabled_zero_cost" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_particle_disabled_zero_cost.ps1"),
        "-NoBuild"
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
    Invoke-ReleaseStep "ibl_asset_policy" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_ibl_asset_policy_tests.ps1")
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "phase3_fallback_matrix" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_phase3_fallback_matrix.ps1"),
        "-NoBuild"
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
    Invoke-ReleaseStep "rt_firefly_outlier" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_firefly_outlier_scene.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "rt_overbright_clamp" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_overbright_clamp_scene.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "release_package_contract" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_release_package_contract_tests.ps1"),
        "-NoBuild"
    )
}

if ($failures.Count -eq 0) {
    Invoke-ReleaseStep "release_package_launch_smoke" @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_release_package_launch_smoke.ps1"),
        "-NoBuild"
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
    Write-ReleaseSummary "failed"
    Write-Host "Release validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$releaseLogDir" -ForegroundColor Red
    exit 1
}

Write-ReleaseSummary "passed"
Write-Host "Release validation passed" -ForegroundColor Green
Write-Host "logs=$releaseLogDir"
foreach ($step in $steps) {
    Write-Host (" - {0}: {1}s logs={2}" -f $step.Name, $step.Seconds, $step.LogDir)
}
