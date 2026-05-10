param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 240,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "effects_gallery_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stdoutPath = Join-Path $LogDir "effects_gallery_stdout.txt"
$stderrPath = Join-Path $LogDir "effects_gallery_stderr.txt"
$reportPath = Join-Path $LogDir "frame_report_last.json"
$catalogPath = Join-Path $root "assets/config/advanced_graphics_catalog.json"

$rtArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"),
    "-LogDir", $LogDir,
    "-SmokeFrames", [string]$SmokeFrames,
    "-SkipSurfaceDebug"
)
if ($NoBuild) {
    $rtArgs += "-NoBuild"
}

$output = & powershell @rtArgs 2>&1
$exitCode = $LASTEXITCODE
$output | Set-Content -Encoding UTF8 $stdoutPath
"" | Set-Content -Encoding UTF8 $stderrPath

if ($exitCode -ne 0) {
    $stdoutText = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { "" }
    $stderrText = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { "" }
    throw "RT showcase smoke failed during effects gallery validation. log=$LogDir`n$stderrText`n$stdoutText"
}

if (-not (Test-Path $reportPath)) {
    throw "Expected frame report was not written: $reportPath"
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

if (-not (Test-Path $catalogPath)) {
    Add-Failure "advanced graphics catalog is missing: $catalogPath"
} else {
    $catalog = Get-Content $catalogPath -Raw | ConvertFrom-Json
    $systemIds = @{}
    foreach ($system in $catalog.systems) {
        $systemIds[[string]$system.id] = $system
    }
    foreach ($requiredSystem in @("particles", "cinematic_post")) {
        if (-not $systemIds.ContainsKey($requiredSystem)) {
            Add-Failure "advanced graphics catalog missing '$requiredSystem' system"
        } elseif ([string]$systemIds[$requiredSystem].first_validation_scene -ne "effects_showcase") {
            Add-Failure "catalog '$requiredSystem' first_validation_scene is '$($systemIds[$requiredSystem].first_validation_scene)', expected effects_showcase"
        }
    }
}

$particles = $report.frame_contract.particles
if ($null -eq $particles) {
    Add-Failure "frame_contract.particles is missing"
} else {
    if (-not [bool]$particles.enabled) { Add-Failure "particles.enabled is false" }
    if (-not [bool]$particles.planned) { Add-Failure "particles.planned is false" }
    if (-not [bool]$particles.executed) { Add-Failure "particles.executed is false" }
    if ([bool]$particles.instance_map_failed) { Add-Failure "particles.instance_map_failed is true" }
    if ([bool]$particles.capped) { Add-Failure "particles.capped is true" }
    if ([int]$particles.emitter_count -lt 1) { Add-Failure "expected at least one particle emitter" }
    if ([int]$particles.live_particles -lt 1) { Add-Failure "expected live particles" }
    if ([int]$particles.submitted_instances -lt 1) { Add-Failure "expected submitted particle instances" }
    if ([int]$particles.instance_capacity -lt [int]$particles.submitted_instances) {
        Add-Failure "particle instance capacity is smaller than submitted instances"
    }

    $drawInstances = [int]$report.frame_contract.draw_counts.particle_instances
    if ([int]$particles.submitted_instances -ne $drawInstances) {
        Add-Failure "particle contract submitted_instances ($($particles.submitted_instances)) does not match draw count ($drawInstances)"
    }
}

$particlePass = Get-FrameContractPass $report "Particles"
if ($null -eq $particlePass) {
    Add-Failure "Particles pass record is missing"
} elseif (-not [bool]$particlePass.executed) {
    Add-Failure "Particles pass record was not executed"
}

$cinematicPost = $report.frame_contract.cinematic_post
if ($null -eq $cinematicPost) {
    Add-Failure "frame_contract.cinematic_post is missing"
} else {
    if (-not [bool]$cinematicPost.post_process_planned) {
        Add-Failure "cinematic_post.post_process_planned is false"
    }
    if (-not [bool]$cinematicPost.post_process_executed) {
        Add-Failure "cinematic_post.post_process_executed is false"
    }
    if ($null -eq $cinematicPost.vignette) {
        Add-Failure "cinematic_post.vignette is missing"
    }
    if ($null -eq $cinematicPost.lens_dirt) {
        Add-Failure "cinematic_post.lens_dirt is missing"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Effects gallery tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "log=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Effects gallery tests passed: particles=$($particles.submitted_instances) emitters=$($particles.emitter_count) log=$LogDir"
