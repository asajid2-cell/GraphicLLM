param(
    [switch]$NoBuild,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "fatal_error_contract_{0}_{1}_{2}" -f `
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

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Invoke-FatalScenario {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][hashtable]$Environment,
        [string[]]$Arguments = @("--no-launcher", "--no-llm", "--no-dreamer")
    )

    $scenarioDir = Join-Path $LogDir $Name
    New-Item -ItemType Directory -Force -Path $scenarioDir | Out-Null

    $previous = @{}
    foreach ($key in $Environment.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, [string]$Environment[$key], "Process")
    }
    $previous["CORTEX_LOG_DIR"] = [Environment]::GetEnvironmentVariable("CORTEX_LOG_DIR", "Process")
    $previous["CORTEX_DISABLE_DEBUG_LAYER"] = [Environment]::GetEnvironmentVariable("CORTEX_DISABLE_DEBUG_LAYER", "Process")
    [Environment]::SetEnvironmentVariable("CORTEX_LOG_DIR", $scenarioDir, "Process")
    [Environment]::SetEnvironmentVariable("CORTEX_DISABLE_DEBUG_LAYER", "1", "Process")

    $exeWorkingDir = Split-Path -Parent $exe
    Push-Location $exeWorkingDir
    try {
        $output = & $exe @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $scenarioDir "engine_stdout.txt")
    } finally {
        Pop-Location
        foreach ($key in $previous.Keys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
        }
    }

    [pscustomobject]@{
        Name = $Name
        LogDir = $scenarioDir
        ExitCode = $exitCode
        SummaryPath = Join-Path $scenarioDir "last_renderer_failure.json"
    }
}

function Assert-FatalSummary {
    param(
        [Parameter(Mandatory=$true)]$Scenario,
        [Parameter(Mandatory=$true)][string]$ExpectedKind,
        [Parameter(Mandatory=$true)][string]$ExpectedMessagePattern
    )

    if ($Scenario.ExitCode -eq 0) {
        Add-Failure "$($Scenario.Name) exited successfully; expected nonzero exit code."
    }
    if (-not (Test-Path $Scenario.SummaryPath)) {
        Add-Failure "$($Scenario.Name) did not write last_renderer_failure.json. logs=$($Scenario.LogDir)"
        return
    }

    $summary = Get-Content $Scenario.SummaryPath -Raw | ConvertFrom-Json
    if ([string]$summary.schema -ne "cortex.renderer_failure.v1") {
        Add-Failure "$($Scenario.Name): unexpected failure summary schema '$($summary.schema)'"
    }
    if ([string]$summary.failure.kind -ne $ExpectedKind) {
        Add-Failure "$($Scenario.Name): unexpected failure kind '$($summary.failure.kind)', expected '$ExpectedKind'"
    }
    if ([string]::IsNullOrWhiteSpace([string]$summary.failure.message) -or
        [string]$summary.failure.message -notmatch $ExpectedMessagePattern) {
        Add-Failure "$($Scenario.Name): failure summary message '$($summary.failure.message)' did not match '$ExpectedMessagePattern'."
    }
    if ([int]$summary.failure.exit_code -eq 0) {
        Add-Failure "$($Scenario.Name): failure summary exit_code should be nonzero."
    }
    if ([string]::IsNullOrWhiteSpace([string]$summary.log_file)) {
        Add-Failure "$($Scenario.Name): failure summary missing log_file."
    }
    if ($null -eq $summary.user_message) {
        Add-Failure "$($Scenario.Name): failure summary missing user_message UX block."
    } else {
        if (-not [bool]$summary.user_message.dialog_supported) {
            Add-Failure "$($Scenario.Name): failure summary user_message.dialog_supported was false."
        }
        if ([bool]$summary.user_message.dialog_shown) {
            Add-Failure "$($Scenario.Name): fatal dialog was shown during automation; expected suppression."
        }
        if ([string]$summary.user_message.dialog_suppressed_reason -ne "automation_log_dir") {
            Add-Failure "$($Scenario.Name): fatal dialog suppression reason was '$($summary.user_message.dialog_suppressed_reason)', expected automation_log_dir."
        }
        if ([string]::IsNullOrWhiteSpace([string]$summary.user_message.recovery_hint) -or
            [string]$summary.user_message.recovery_hint -notmatch "Safe Startup") {
            Add-Failure "$($Scenario.Name): failure summary user_message.recovery_hint is missing Safe Startup guidance."
        }
    }
    if ($null -eq $summary.recent_log_lines -or $summary.recent_log_lines.Count -lt 1) {
        Add-Failure "$($Scenario.Name): failure summary missing recent log lines."
    }
}

$forcedFatal = Invoke-FatalScenario `
    -Name "forced_exception" `
    -Environment @{ CORTEX_FORCE_FATAL_ERROR = "1" }
Assert-FatalSummary `
    -Scenario $forcedFatal `
    -ExpectedKind "exception" `
    -ExpectedMessagePattern "Forced fatal error"

$deviceRemoved = Invoke-FatalScenario `
    -Name "simulated_device_removed" `
    -Environment @{ CORTEX_SIMULATE_DEVICE_REMOVED_FRAME = "2" } `
    -Arguments @("--no-launcher", "--no-llm", "--no-dreamer", "--scene=material_lab", "--graphics-preset=safe_startup")
Assert-FatalSummary `
    -Scenario $deviceRemoved `
    -ExpectedKind "device_removed" `
    -ExpectedMessagePattern "device was removed|GPU fault"

$deviceReportPath = Join-Path $deviceRemoved.LogDir "frame_report_shutdown.json"
if (-not (Test-Path $deviceReportPath)) {
    Add-Failure "simulated_device_removed: missing frame_report_shutdown.json."
} else {
    $deviceReport = Get-Content $deviceReportPath -Raw | ConvertFrom-Json
    if (-not [bool]$deviceReport.renderer.device_removed) {
        Add-Failure "simulated_device_removed: frame report did not record renderer.device_removed=true."
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Fatal error contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Fatal error contract tests passed: logs=$LogDir" -ForegroundColor Green
