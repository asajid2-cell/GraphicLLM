param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 90,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "lighting_energy_budget_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before lighting energy budget tests"
    }
}

$failures = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Invoke-LightingCase([string]$Name, [string]$ScriptName, [string[]]$ExtraArgs = @()) {
    $caseLogDir = Join-Path $script:LogDir $Name
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $script:PSScriptRoot $ScriptName),
        "-NoBuild",
        "-LogDir", $caseLogDir,
        "-SmokeFrames", [string]$script:SmokeFrames
    ) + $ExtraArgs
    $output = & powershell @args 2>&1
    $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "stdout.txt")
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "$Name failed with exit code $LASTEXITCODE"
        return
    }

    $reportPath = Join-Path $caseLogDir "frame_report_last.json"
    if (-not (Test-Path $reportPath)) {
        Add-Failure "$Name did not write frame_report_last.json"
        return
    }
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $lighting = $report.frame_contract.lighting
    $stats = $report.visual_validation.image_stats

    $row = [ordered]@{
        name = $Name
        rig = [string]$lighting.rig_id
        light_count = [int]$lighting.light_count
        shadow_casting_light_count = [int]$lighting.shadow_casting_light_count
        total_light_intensity = [double]$lighting.total_light_intensity
        max_light_intensity = [double]$lighting.max_light_intensity
        exposure = [double]$lighting.exposure
        bloom_intensity = [double]$lighting.bloom_intensity
        near_white_ratio = if ($null -ne $stats) { [double]$stats.near_white_ratio } else { $null }
        saturated_ratio = if ($null -ne $stats) { [double]$stats.saturated_ratio } else { $null }
        avg_luma = if ($null -ne $stats) { [double]$stats.avg_luma } else { $null }
    }
    $rows.Add([pscustomobject]$row)

    if ([int]$lighting.light_count -lt 1) {
        Add-Failure "$Name has no lights"
    }
    if ([double]$lighting.total_light_intensity -le 0.0 -or [double]$lighting.total_light_intensity -gt 180.0) {
        Add-Failure "$Name total_light_intensity out of budget: $($lighting.total_light_intensity)"
    }
    if ([double]$lighting.max_light_intensity -le 0.0 -or [double]$lighting.max_light_intensity -gt 80.0) {
        Add-Failure "$Name max_light_intensity out of budget: $($lighting.max_light_intensity)"
    }
    if ([double]$lighting.exposure -lt 0.01 -or [double]$lighting.exposure -gt 4.0) {
        Add-Failure "$Name exposure out of budget: $($lighting.exposure)"
    }
    if ([double]$lighting.bloom_intensity -lt 0.0 -or [double]$lighting.bloom_intensity -gt 2.0) {
        Add-Failure "$Name bloom_intensity out of budget: $($lighting.bloom_intensity)"
    }
    if ($null -eq $stats -or -not [bool]$stats.valid) {
        Add-Failure "$Name missing valid visual stats"
    } else {
        if ([double]$stats.near_white_ratio -gt 0.16) {
            Add-Failure "$Name near_white_ratio out of budget: $($stats.near_white_ratio)"
        }
        if ([double]$stats.saturated_ratio -gt 0.14) {
            Add-Failure "$Name saturated_ratio out of budget: $($stats.saturated_ratio)"
        }
    }
}

Invoke-LightingCase "rt_showcase" "run_rt_showcase_smoke.ps1" @("-SkipSurfaceDebug")
Invoke-LightingCase "material_lab" "run_material_lab_smoke.ps1"
Invoke-LightingCase "glass_water_courtyard" "run_glass_water_courtyard_smoke.ps1"
Invoke-LightingCase "effects_showcase" "run_effects_showcase_smoke.ps1"

$rows | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $LogDir "lighting_energy_budget_summary.json")

if ($failures.Count -gt 0) {
    Write-Host "Lighting energy budget tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Lighting energy budget tests passed: cases=$($rows.Count) logs=$LogDir" -ForegroundColor Green
exit 0
