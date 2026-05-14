param(
    [switch]$NoBuild,
    [switch]$TexturePages,
    [switch]$RuntimeAssets,
    [switch]$CapabilityTier,
    [switch]$DensityGate,
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
    $runId = "generated_asset_admission_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/GeneratedAssetAdmission.h"
$source = Read-Text "src/Scene/GeneratedAssetAdmission.cpp"
$main = Read-Text "src/main.cpp"
$cmake = Read-Text "CMakeLists.txt"

Require-Contains $cmake "src/Scene/GeneratedAssetAdmission.cpp" "GeneratedAssetAdmission.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/GeneratedAssetAdmission.h" "GeneratedAssetAdmission.h is not listed with project headers."
Require-Contains $header "GeneratedRuntimeAssetObligations" "Runtime asset obligations are missing."
Require-Contains $header "texturePages" "Texture page reporting is missing."
Require-Contains $header "residentTextureBytes" "Texture residency reporting is missing."
Require-Contains $header "psoSignatures" "PSO signature reporting is missing."
Require-Contains $header "rtStateObjects" "RT state reporting is missing."
Require-Contains $header "blasBuilds" "BLAS build reporting is missing."
Require-Contains $header "tlasInstances" "TLAS reporting is missing."
Require-Contains $header "probeCount" "Probe obligation reporting is missing."
Require-Contains $header "targetCapabilityTier" "Target capability tier is missing."
Require-Contains $header "fallbackReady" "Fallback readiness is missing."
Require-Contains $header "proceduralDensityScale" "Procedural density scale gate is missing."
Require-Contains $header "streamingReady" "Procedural density streaming readiness is missing."
Require-Contains $header "semanticValidationReady" "Procedural density semantic validation readiness is missing."
Require-Contains $header "rtAdmissionReady" "Procedural density RT admission readiness is missing."
Require-Contains $header "AdmitGeneratedAsset" "Generated asset admission function is missing."
Require-Contains $source "EvaluateProducerBudgetRequest" "Generated asset admission does not use renderer backpressure."
Require-Contains $source "BuildGeneratedAssetTransaction" "Generated asset admission does not emit transaction data."
Require-Contains $main "--generated-asset-admission-self-test" "Engine CLI generated asset admission self-test is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before generated asset admission validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "generated_asset_admission_self_test_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--generated-asset-admission-self-test" 2>&1
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
            Add-Failure "Generated asset admission runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Generated asset admission runtime report pass=false."
                }
                if ([string]$report.accepted.decision -ne "accept") {
                    Add-Failure "Valid generated asset was not accepted."
                }
                if ([string]$report.missing_fallback.decision -ne "reject") {
                    Add-Failure "Generated asset without fallback readiness was not rejected."
                }
                if ([string]$report.degraded.decision -ne "degrade") {
                    Add-Failure "Over-budget degradable generated asset was not degraded."
                }
                if (-not [bool]$report.transaction_has_runtime_assets) {
                    Add-Failure "Generated asset transaction did not preserve runtime asset obligations."
                }
                if ([int]$report.transaction.texture_pages -lt 1 -or [int]$report.transaction.pso_signatures -lt 1 -or [int]$report.transaction.probe_count -lt 1) {
                    Add-Failure "Generated asset transaction did not report texture pages, PSO signatures, and probes."
                }
                if (-not [bool]$report.transaction.fallback_ready) {
                    Add-Failure "Generated asset transaction did not report fallback readiness."
                }
                if ([string]$report.density_rejected.decision -ne "reject") {
                    Add-Failure "Procedural density increase without readiness was not rejected."
                }
                if ([string]$report.density_accepted.decision -ne "accept") {
                    Add-Failure "Procedural density increase with all readiness gates was not accepted."
                }
            } catch {
                Add-Failure "Could not parse generated asset admission runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Generated asset admission tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Generated asset admission tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
