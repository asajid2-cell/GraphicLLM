param(
    [switch]$NoBuild,
    [switch]$ProducerDegrade,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-Text([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path $path)) {
        throw "Missing required file: $RelativePath"
    }
    return Get-Content -Raw -Path $path
}

function Require-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if ($Text.IndexOf($Needle, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure $Message
    }
}

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "renderer_backpressure_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/RendererBackpressure.h"
$source = Read-Text "src/Scene/RendererBackpressure.cpp"
$main = Read-Text "src/main.cpp"
$cmake = Read-Text "CMakeLists.txt"

Require-Contains $cmake "src/Scene/RendererBackpressure.cpp" "RendererBackpressure.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/RendererBackpressure.h" "RendererBackpressure.h is not listed with project headers."
Require-Contains $header "RendererBackpressureSnapshot" "Backpressure snapshot is missing."
Require-Contains $header "ProducerBudgetRequest" "Producer budget request is missing."
Require-Contains $header "ProducerBudgetResponse" "Producer budget response is missing."
Require-Contains $header "BuildRendererBackpressureSnapshot" "Frame-contract-to-backpressure adapter is missing."
Require-Contains $header "EvaluateProducerBudgetRequest" "Producer budget evaluator is missing."
Require-Contains $source "availableTextureBytes" "Texture budget signal is not exposed."
Require-Contains $source "availableTLASInstances" "TLAS pressure signal is not exposed."
Require-Contains $source "pendingBLAS" "BLAS backlog signal is not exposed."
Require-Contains $source "availablePersistentDescriptors" "Descriptor pressure signal is not exposed."
Require-Contains $source "validationCameraFailures" "Validation-camera failure signal is not exposed."
Require-Contains $main "--renderer-backpressure-self-test" "Engine CLI backpressure self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before renderer backpressure validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "renderer_backpressure_self_test_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--renderer-backpressure-self-test" 2>&1
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }

        $outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
        $outputText | Set-Content -Path $stdoutPath -Encoding UTF8
        if (-not [string]::IsNullOrWhiteSpace($outputText)) {
            $outputText -split "`r?`n" | ForEach-Object { Write-Host $_ }
        }

        if ($exitCode -ne 0) {
            Add-Failure "Renderer backpressure runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Renderer backpressure runtime report pass=false."
                }
                if ([string]$report.accepted.decision -ne "accept") {
                    Add-Failure "Small producer request was not accepted."
                }
                if ([string]$report.degraded.decision -ne "degrade") {
                    Add-Failure "Over-budget degradable producer request was not degraded."
                }
                if ([string]$report.rejected.decision -ne "reject") {
                    Add-Failure "Over-budget rigid producer request was not rejected."
                }
                if (-not [bool]$report.producer_asked_before_emit) {
                    Add-Failure "Producer request path did not prove budget ask before emit."
                }
                if (-not [bool]$report.degraded_before_recovery) {
                    Add-Failure "Producer degradation did not occur before renderer recovery."
                }
            } catch {
                Add-Failure "Could not parse renderer backpressure runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Renderer backpressure tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Renderer backpressure tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
