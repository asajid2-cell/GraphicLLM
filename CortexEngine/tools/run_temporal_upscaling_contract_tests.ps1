param(
    [switch]$NoBuild,
    [switch]$GuardVendorUpscalers,
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
    $runId = "temporal_upscaling_contract_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SceneTransaction.h"
$source = Read-Text "src/Scene/SceneTransaction.cpp"
$releaseValidation = Read-Text "tools/run_release_validation.ps1"

Require-Contains $header "TemporalUpscalingContract" "Temporal upscaling contract is missing from SceneTransaction."
Require-Contains $header "motionVectorsValid" "Motion-vector readiness field is missing."
Require-Contains $header "exposureValid" "Exposure readiness field is missing."
Require-Contains $header "reactiveMaskValid" "Reactive-mask readiness field is missing."
Require-Contains $header "generatedObjectInvalidation" "Generated object invalidation readiness is missing."
Require-Contains $header "dynamicObjectInvalidation" "Dynamic object invalidation readiness is missing."
Require-Contains $source "ValidateTemporalUpscalingContract" "Temporal upscaling contract is not enforced by transaction validation."
Require-Contains $source "ValidateSemanticHistoryInvalidation" "Semantic history invalidation is not validated against graph diffs."
Require-Contains $releaseValidation "run_temporal_upscaling_contract_tests.ps1" "Release validation does not run temporal upscaling contract tests."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before temporal upscaling contract validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "temporal_upscaling_contract_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--scene-transaction-self-test" 2>&1
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
            Add-Failure "Scene transaction runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Scene transaction runtime report pass=false."
                }
                if (-not [bool]$report.history_invalidation.taa -or
                    -not [bool]$report.history_invalidation.rt_reflection -or
                    -not [bool]$report.history_invalidation.rt_gi -or
                    -not [bool]$report.history_invalidation.temporal_masks -or
                    [string]$report.history_invalidation.dirty_region -ne "foreground") {
                    Add-Failure "Semantic history invalidation did not cover TAA, RT reflection, RT GI, temporal masks, and dirty region."
                }
                if (-not [bool]$report.temporal_upscaling_contract.required -or
                    -not [bool]$report.temporal_upscaling_contract.motion_vectors_valid -or
                    -not [bool]$report.temporal_upscaling_contract.exposure_valid -or
                    -not [bool]$report.temporal_upscaling_contract.reactive_mask_valid -or
                    -not [bool]$report.temporal_upscaling_contract.generated_object_invalidation -or
                    -not [bool]$report.temporal_upscaling_contract.dynamic_object_invalidation) {
                    Add-Failure "Temporal upscaling contract readiness fields were not all validated."
                }
            } catch {
                Add-Failure "Could not parse temporal upscaling contract runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Temporal upscaling contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Temporal upscaling contract tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
