param(
    [string]$Prompt = "a foggy mountain campsite beside a purple lake at dawn",
    [string]$Name = "genscene_rt_fixture_campsite",
    [int]$Runs = 2,
    [int]$Frames = 220,
    [int]$VisualValidationMinFrame = 30,
    [int]$TimeoutSec = 260,
    [string]$BudgetProfile = "4gb_low",
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$EnableRTReflections,
    [switch]$EnableRTGI,
    [int]$MinObjects = 20,
    [int]$MaxObjects = 48,
    [int]$MinTLASInstances = 16,
    [int]$MaxTLASInstances = 512,
    [int]$MaxTLASCandidates = 512,
    [int]$MinRayTracingPasses = 3,
    [double]$MinVisualNonBlackRatio = 0.90,
    [double]$MinVisualAvgLuma = 20.0,
    [double]$MinVisualCenterLuma = 20.0
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$baseLogDir = Join-Path $root "build/bin/logs"
$vcpkgBin = Join-Path $root "build/vcpkg_installed/x64-windows/bin"

if ($Runs -lt 1) {
    throw "-Runs must be >= 1"
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first."
}

$activeRoot = $LogDir
if ([string]::IsNullOrWhiteSpace($activeRoot)) {
    if ($IsolatedLogs) {
        $runId = "genscene_rt_fixture_{0}_{1}_{2}" -f `
            (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
            $PID,
            ([Guid]::NewGuid().ToString("N").Substring(0, 8))
        $activeRoot = Join-Path (Join-Path $baseLogDir "runs") $runId
    } else {
        $activeRoot = Join-Path $baseLogDir "genscene_rt_fixture"
    }
}
New-Item -ItemType Directory -Force -Path $activeRoot | Out-Null

$irPath = Join-Path $activeRoot "$Name`_ir.json"
$directorPath = Join-Path $activeRoot "$Name`_director_v3.json"
$irBuildLog = Join-Path $activeRoot "build_ir.log"
$irBuildOut = & python tools\build_genscene_fixture_ir.py `
    --prompt $Prompt `
    --ir-out $irPath `
    --director-out $directorPath 2>&1
$irBuildCode = $LASTEXITCODE
$irBuildOut | Out-File -FilePath $irBuildLog -Encoding utf8
if ($irBuildCode -ne 0) {
    throw "Fixture IR build failed; see $irBuildLog"
}

$ir = Get-Content $irPath -Raw | ConvertFrom-Json
$objectCount = @($ir.objects).Count
$failures = New-Object System.Collections.Generic.List[string]
$summaries = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

if ($objectCount -lt $MinObjects -or $objectCount -gt $MaxObjects) {
    Add-Failure "fixture IR object count=$objectCount, expected $MinObjects..$MaxObjects"
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

function Convert-BmpToPng([string]$BmpPath, [string]$PngPath) {
    Add-Type -AssemblyName System.Drawing
    $img = [System.Drawing.Image]::FromFile($BmpPath)
    try {
        $img.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $img.Dispose()
    }
}

function Clear-FixtureEnvironment {
    Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_EXIT_AFTER_VISUAL_VALIDATION -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SMOKE_FRAMES -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_HEADLESS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SHOWCASE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SCENE_IR_JSON_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SCENE_IR_JSON -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SCENE_PROMPT -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SCENE_RECIPE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_RT -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_RT_REFLECTIONS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_DISABLE_RT_GI -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_RT_BUDGET_PROFILE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_RENDER_BUDGET_PROFILE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_LOW_GPU_PRIORITY -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_FRAME_PACE_MS -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SUPPRESS_CAMERA_HELP -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_SUPPRESS_FATAL_DIALOG -ErrorAction SilentlyContinue
}

$exeWorkingDir = Split-Path -Parent $exe
for ($runIndex = 1; $runIndex -le $Runs; ++$runIndex) {
    $runDir = Join-Path $activeRoot ("run{0}" -f $runIndex)
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $reportPath = Join-Path $runDir "frame_report_last.json"
    $visualPath = Join-Path $runDir "visual_validation_rt_showcase.bmp"
    $pngPath = Join-Path $runDir "$Name`_run$runIndex.png"
    $stdoutPath = Join-Path $runDir "fixture_stdout.txt"
    $stderrPath = Join-Path $runDir "fixture_stderr.txt"

    Remove-Item -Force -ErrorAction SilentlyContinue $reportPath, $visualPath, $pngPath, $stdoutPath, $stderrPath

    $env:PATH = "$vcpkgBin;$env:PATH"
    $env:CORTEX_LOG_DIR = $runDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_EXIT_AFTER_VISUAL_VALIDATION = "1"
    $env:CORTEX_SMOKE_FRAMES = [string]$Frames
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = [string]$VisualValidationMinFrame
    $env:CORTEX_HEADLESS = "1"
    $env:CORTEX_SHOWCASE = "1"
    $env:CORTEX_SCENE_IR_JSON_FILE = (Resolve-Path $irPath).Path
    $env:CORTEX_RT_BUDGET_PROFILE = $BudgetProfile
    $env:CORTEX_RENDER_BUDGET_PROFILE = $BudgetProfile
    $env:CORTEX_LOW_GPU_PRIORITY = "1"
    $env:CORTEX_FRAME_PACE_MS = "35"
    $env:CORTEX_SUPPRESS_CAMERA_HELP = "1"
    $env:CORTEX_SUPPRESS_FATAL_DIALOG = "1"
    Remove-Item Env:\CORTEX_DISABLE_RT -ErrorAction SilentlyContinue
    if (-not $EnableRTReflections) {
        $env:CORTEX_DISABLE_RT_REFLECTIONS = "1"
    } else {
        Remove-Item Env:\CORTEX_DISABLE_RT_REFLECTIONS -ErrorAction SilentlyContinue
    }
    if (-not $EnableRTGI) {
        $env:CORTEX_DISABLE_RT_GI = "1"
    } else {
        Remove-Item Env:\CORTEX_DISABLE_RT_GI -ErrorAction SilentlyContinue
    }
    Remove-Item Env:\CORTEX_SCENE_IR_JSON,Env:\CORTEX_SCENE_PROMPT,Env:\CORTEX_SCENE_RECIPE -ErrorAction SilentlyContinue

    Push-Location $exeWorkingDir
    try {
        $p = Start-Process -FilePath $exe `
            -ArgumentList @("--no-llm", "--no-launcher") `
            -NoNewWindow `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
        try { $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch {}
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            $p.Kill()
            Add-Failure "run $runIndex timed out after ${TimeoutSec}s"
            continue
        }
        $p.WaitForExit()
        $p.Refresh()
        $exitCode = $null
        try { $exitCode = [int]$p.ExitCode } catch {}
        if ($null -eq $exitCode) {
            Add-Failure "run $runIndex process exit code was unavailable after process exit"
            continue
        }
        if ($exitCode -ne 0) {
            Add-Failure "run $runIndex process exit code $exitCode"
            continue
        }
    } finally {
        Pop-Location
        Clear-FixtureEnvironment
    }

    if (-not (Test-Path $reportPath)) {
        Add-Failure "run $runIndex did not write frame report: $reportPath"
        continue
    }
    if (-not (Test-Path $visualPath)) {
        Add-Failure "run $runIndex did not write visual capture: $visualPath"
        continue
    }
    Convert-BmpToPng $visualPath $pngPath

    $stdoutText = ""
    if (Test-Path $stdoutPath) {
        $stdoutText = Get-Content $stdoutPath -Raw
    }
    foreach ($badPattern in @("queue fence timeout", "render_health_image", "Device Removed", "DXGI_ERROR_DEVICE_REMOVED")) {
        if ($stdoutText -match [regex]::Escape($badPattern)) {
            Add-Failure "run $runIndex log contains '$badPattern'"
        }
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $stats = $report.visual_validation.image_stats
    $rt = $report.frame_contract.ray_tracing
    $features = $report.frame_contract.features
    $budget = $report.frame_contract.renderer_budget
    $passSummary = $report.frame_contract.pass_budget_summary

    $allowedWarnings = @("cinematic_post_params_out_of_range")
    foreach ($warning in @($report.frame_contract.warnings)) {
        if ($allowedWarnings -notcontains [string]$warning) {
            Add-Failure "run $runIndex unexpected frame_contract warning: $warning"
        }
    }
    foreach ($warning in @($report.health_warnings)) {
        $text = [string]$warning
        if ($text -ne "frame_contract:cinematic_post_params_out_of_range") {
            Add-Failure "run $runIndex unexpected health warning: $warning"
        }
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
    if (-not [bool]$rt.enabled -or -not [bool]$features.ray_tracing_enabled) {
        Add-Failure "run $runIndex ray tracing is not enabled"
    }
    if (-not [bool]$features.ssao_enabled) {
        Add-Failure "run $runIndex SSAO is not enabled"
    }
    if ([string]$budget.profile -ne $BudgetProfile -or -not [bool]$budget.forced) {
        Add-Failure "run $runIndex renderer budget profile is '$($budget.profile)' forced=$($budget.forced), expected forced '$BudgetProfile'"
    }
    if ([string]$rt.budget_profile -ne $BudgetProfile) {
        Add-Failure "run $runIndex RT budget profile is '$($rt.budget_profile)', expected '$BudgetProfile'"
    }
    if (-not [bool]$rt.scheduler_build_tlas -or -not [bool]$rt.dispatch_shadows) {
        Add-Failure "run $runIndex RT scheduler did not build TLAS and dispatch shadows"
    }
    if ([int]$rt.tlas_instances -lt $MinTLASInstances -or [int]$rt.tlas_instances -gt $MaxTLASInstances) {
        Add-Failure "run $runIndex TLAS instances=$($rt.tlas_instances), expected $MinTLASInstances..$MaxTLASInstances"
    }
    if ([int]$rt.tlas_candidates -gt $MaxTLASCandidates) {
        Add-Failure "run $runIndex TLAS candidates=$($rt.tlas_candidates), expected <= $MaxTLASCandidates"
    }
    if ([int]$rt.tlas_blas_build_failed -ne 0 -or
        [int]$rt.tlas_blas_build_budget_deferred -ne 0 -or
        [int]$rt.tlas_blas_total_budget_skipped -ne 0) {
        Add-Failure "run $runIndex BLAS/TLAS build was not clean: failed=$($rt.tlas_blas_build_failed) deferred=$($rt.tlas_blas_build_budget_deferred) total_skipped=$($rt.tlas_blas_total_budget_skipped)"
    }
    if ([int]$rt.pending_blas -ne 0 -or [int]$rt.pending_renderer_blas_jobs -ne 0) {
        Add-Failure "run $runIndex captured before RT settled: pending_blas=$($rt.pending_blas) pending_renderer=$($rt.pending_renderer_blas_jobs)"
    }
    if ([int]$passSummary.ray_tracing_passes -lt $MinRayTracingPasses) {
        Add-Failure "run $runIndex ray-tracing pass count=$($passSummary.ray_tracing_passes), expected >= $MinRayTracingPasses"
    }

    $rtShadows = Get-ContractPass $report "RTShadowsGI"
    if ($null -eq $rtShadows -or -not [bool]$rtShadows.executed) {
        Add-Failure "run $runIndex RTShadowsGI pass was not executed"
    } elseif (-not (Test-ListContains $rtShadows.writes "rt_shadow_mask")) {
        Add-Failure "run $runIndex RTShadowsGI did not write rt_shadow_mask"
    }
    $ssaoPass = Get-ContractPass $report "SSAO"
    if ($null -eq $ssaoPass -or -not [bool]$ssaoPass.executed) {
        Add-Failure "run $runIndex SSAO pass was not executed"
    } elseif (-not (Test-ListContains $ssaoPass.writes "ssao")) {
        Add-Failure "run $runIndex SSAO did not write ssao"
    }
    $postPass = Get-ContractPass $report "PostProcess"
    if ($null -eq $postPass -or -not [bool]$postPass.executed -or -not (Test-ListContains $postPass.reads "ssao")) {
        Add-Failure "run $runIndex PostProcess did not execute with ssao input"
    }

    $summaries.Add([pscustomobject]@{
        run = $runIndex
        log_dir = $runDir
        png = $pngPath
        report = $reportPath
        objects = $objectCount
        nonblack_ratio = [double]$stats.nonblack_ratio
        avg_luma = [double]$stats.avg_luma
        center_avg_luma = [double]$stats.center_avg_luma
        budget_profile = [string]$budget.profile
        rt_enabled = [bool]$rt.enabled
        rt_reflections_enabled = [bool]$features.rt_reflections_enabled
        rt_gi_enabled = [bool]$features.rt_gi_enabled
        ssao_enabled = [bool]$features.ssao_enabled
        tlas_instances = [int]$rt.tlas_instances
        tlas_candidates = [int]$rt.tlas_candidates
        ray_tracing_passes = [int]$passSummary.ray_tracing_passes
    }) | Out-Null
}

$summaryPath = Join-Path $activeRoot "genscene_rt_fixture_summary.json"
[pscustomobject]@{
    passed = ($failures.Count -eq 0)
    prompt = $Prompt
    name = $Name
    ir = $irPath
    director = $directorPath
    runs = @($summaries | ForEach-Object { $_ })
    failures = @($failures | ForEach-Object { [string]$_ })
    thresholds = [pscustomobject]@{
        budget_profile = $BudgetProfile
        min_objects = $MinObjects
        max_objects = $MaxObjects
        min_tlas_instances = $MinTLASInstances
        max_tlas_instances = $MaxTLASInstances
        max_tlas_candidates = $MaxTLASCandidates
        min_ray_tracing_passes = $MinRayTracingPasses
        min_visual_nonblack_ratio = $MinVisualNonBlackRatio
        min_visual_avg_luma = $MinVisualAvgLuma
        min_visual_center_luma = $MinVisualCenterLuma
    }
} | ConvertTo-Json -Depth 8 | Out-File -FilePath $summaryPath -Encoding utf8

if ($failures.Count -gt 0) {
    Write-Host "GenScene RT fixture failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host " logs=$activeRoot"
    Write-Host " summary=$summaryPath"
    exit 1
}

Write-Host "GenScene RT fixture passed" -ForegroundColor Green
Write-Host " logs=$activeRoot"
Write-Host " summary=$summaryPath"
foreach ($summary in $summaries) {
    Write-Host (" run={0} objects={1} budget={2} nonblack={3:N3} avg_luma={4:N2} center_luma={5:N2} tlas={6}/{7} rt_passes={8} rt_refl={9} rt_gi={10} png={11}" -f `
        [int]$summary.run,
        [int]$summary.objects,
        [string]$summary.budget_profile,
        [double]$summary.nonblack_ratio,
        [double]$summary.avg_luma,
        [double]$summary.center_avg_luma,
        [int]$summary.tlas_instances,
        [int]$summary.tlas_candidates,
        [int]$summary.ray_tracing_passes,
        [bool]$summary.rt_reflections_enabled,
        [bool]$summary.rt_gi_enabled,
        [string]$summary.png)
}
exit 0
