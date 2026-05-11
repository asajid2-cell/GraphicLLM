param(
    [switch]$NoBuild,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "hud_mode_contract_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
}

$engineHeader = Get-Content -Path (Join-Path $root "src/Core/Engine.h") -Raw
$engineInput = Get-Content -Path (Join-Path $root "src/Core/Engine_Input.cpp") -Raw
$main = Get-Content -Path (Join-Path $root "src/main.cpp") -Raw
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

foreach ($required in @("enum class EngineHudMode", "Off", "Minimal", "Performance", "RendererHealth", "FullDebug")) {
    if ($engineHeader.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure "Engine.h missing HUD mode marker: $required"
    }
}
if ($engineInput.IndexOf("HUD mode set to", [StringComparison]::Ordinal) -lt 0 -or
    $engineInput.IndexOf("SDLK_F7", [StringComparison]::Ordinal) -lt 0) {
    Add-Failure "Engine input does not expose F7 HUD mode cycling."
}
if ($main.IndexOf("--hud", [StringComparison]::Ordinal) -lt 0 -or
    $main.IndexOf("CORTEX_HUD_MODE", [StringComparison]::Ordinal) -lt 0) {
    Add-Failure "main.cpp does not expose HUD mode CLI/env configuration."
}
if ($engineHeader.IndexOf("initialHudMode = EngineHudMode::Off", [StringComparison]::Ordinal) -lt 0) {
    Add-Failure "EngineConfig default HUD mode is not public-capture clean/off."
}

$exeWorkingDir = Split-Path -Parent $exe

function Invoke-HudCase([string]$Mode, [bool]$ExpectedVisible) {
    $caseLogDir = Join-Path $script:LogDir $Mode
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $env:CORTEX_LOG_DIR = $caseLogDir
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"

    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe `
            "--scene" "rt_showcase" `
            "--hud" $Mode `
            "--mode=default" `
            "--no-llm" `
            "--no-dreamer" `
            "--no-launcher" `
            "--smoke-frames=3" 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
    } finally {
        Pop-Location
        Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    }

    if ($exitCode -ne 0) {
        Add-Failure "HUD mode '$Mode' runtime failed with exit code $exitCode. logs=$caseLogDir"
        return
    }

    $reportPath = Join-Path $caseLogDir "frame_report_shutdown.json"
    if (-not (Test-Path $reportPath)) {
        Add-Failure "HUD mode '$Mode' did not write frame_report_shutdown.json. logs=$caseLogDir"
        return
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.hud.mode -ne $Mode) {
        Add-Failure "HUD mode '$Mode' reported '$($report.hud.mode)'"
    }
    if ([bool]$report.hud.visible -ne $ExpectedVisible) {
        Add-Failure "HUD mode '$Mode' visible=$($report.hud.visible), expected $ExpectedVisible"
    }
    if (-not [bool]$report.hud.debug_available) {
        Add-Failure "HUD mode '$Mode' did not report debug_available=true"
    }
    $expectedClean = -not $ExpectedVisible
    if ([bool]$report.hud.public_capture_clean -ne $expectedClean) {
        Add-Failure "HUD mode '$Mode' public_capture_clean=$($report.hud.public_capture_clean), expected $expectedClean"
    }
    $expectedOverlayCount = if ($ExpectedVisible) { 1 } else { 0 }
    if ([int]$report.hud.overlay_count -ne $expectedOverlayCount) {
        Add-Failure "HUD mode '$Mode' overlay_count=$($report.hud.overlay_count), expected $expectedOverlayCount"
    }
}

function Invoke-HudDefaultCase() {
    $caseLogDir = Join-Path $script:LogDir "default"
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $env:CORTEX_LOG_DIR = $caseLogDir
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"

    Push-Location $script:exeWorkingDir
    try {
        $output = & $script:exe `
            "--scene" "rt_showcase" `
            "--mode=default" `
            "--no-llm" `
            "--no-dreamer" `
            "--no-launcher" `
            "--smoke-frames=3" 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
    } finally {
        Pop-Location
        Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
    }

    if ($exitCode -ne 0) {
        Add-Failure "HUD default runtime failed with exit code $exitCode. logs=$caseLogDir"
        return
    }

    $reportPath = Join-Path $caseLogDir "frame_report_shutdown.json"
    if (-not (Test-Path $reportPath)) {
        Add-Failure "HUD default did not write frame_report_shutdown.json. logs=$caseLogDir"
        return
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if ([string]$report.hud.mode -ne "off") {
        Add-Failure "HUD default reported '$($report.hud.mode)', expected off"
    }
    if ([bool]$report.hud.visible) {
        Add-Failure "HUD default visible was true"
    }
    if (-not [bool]$report.hud.public_capture_clean) {
        Add-Failure "HUD default did not report public_capture_clean=true"
    }
    if (-not [bool]$report.hud.debug_available) {
        Add-Failure "HUD default did not report debug_available=true"
    }
    if ([int]$report.hud.overlay_count -ne 0) {
        Add-Failure "HUD default overlay_count was $($report.hud.overlay_count), expected 0"
    }
}

Invoke-HudDefaultCase
Invoke-HudCase "off" $false
Invoke-HudCase "full_debug" $true

if ($failures.Count -gt 0) {
    Write-Host "HUD mode contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "HUD mode contract tests passed: logs=$LogDir" -ForegroundColor Green
