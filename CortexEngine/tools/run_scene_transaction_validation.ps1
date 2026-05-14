param(
    [switch]$NoBuild,
    [switch]$Schema,
    [switch]$PreviewOnly,
    [switch]$BadLayouts,
    [switch]$Commit,
    [switch]$Replay,
    [switch]$Undo,
    [switch]$HistoryInvalidation,
    [switch]$Layers,
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
    $runId = "scene_transaction_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SceneTransaction.h"
$source = Read-Text "src/Scene/SceneTransaction.cpp"
$main = Read-Text "src/main.cpp"
$cmake = Read-Text "CMakeLists.txt"

Require-Contains $cmake "src/Scene/SceneTransaction.cpp" "SceneTransaction.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/SceneTransaction.h" "SceneTransaction.h is not listed with project headers."
Require-Contains $header "struct SceneTransaction" "SceneTransaction runtime object is missing."
Require-Contains $header "entityDiff" "Transaction entity diff field is missing."
Require-Contains $header "semanticGraphDiff" "Transaction semantic graph diff field is missing."
Require-Contains $header "SceneResourceDiff" "Transaction resource diff field group is missing."
Require-Contains $header "rendererBudgetDelta" "Transaction renderer budget delta field is missing."
Require-Contains $header "requiredFeatureTiers" "Transaction required feature tiers are missing."
Require-Contains $header "historyInvalidation" "Transaction history invalidation mask is missing."
Require-Contains $header "validationCameras" "Transaction validation camera set is missing."
Require-Contains $header "SceneTransactionProvenance" "Transaction provenance field group is missing."
Require-Contains $header "Preview" "Transaction preview path is missing."
Require-Contains $header "Commit" "Transaction commit path is missing."
Require-Contains $header "Rollback" "Transaction rollback path is missing."
Require-Contains $source "RunSceneTransactionSelfTestJson" "Runtime scene transaction self-test is missing."
Require-Contains $main "--scene-transaction-self-test" "Engine CLI transaction self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before scene transaction validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "scene_transaction_self_test_stdout.txt"
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
                if ([int]$report.transaction.entity_diff_count -lt 1) {
                    Add-Failure "Runtime transaction did not report an entity diff."
                }
                if ([int]$report.transaction.semantic_graph_diff_ops -lt 1) {
                    Add-Failure "Runtime transaction did not report semantic graph diff ops."
                }
                if ([int]$report.transaction.resource_diff.resource_ids.Count -lt 1) {
                    Add-Failure "Runtime transaction did not report resource diff ids."
                }
                if (-not [bool]$report.transaction.provenance_complete) {
                    Add-Failure "Runtime transaction provenance was incomplete."
                }
                if (-not [bool]$report.preview.did_not_mutate_graph) {
                    Add-Failure "Preview mutated the source semantic graph."
                }
                if (-not [bool]$report.commit.committed -or -not [bool]$report.commit.rollback_restored) {
                    Add-Failure "Commit/rollback did not complete cleanly."
                }
                if (-not [bool]$report.bad_layout.rejected_before_mutation) {
                    Add-Failure "Bad generated layout was not rejected before graph mutation."
                }
                if (-not [bool]$report.replay.same_graph_diff -or -not [bool]$report.replay.same_visual_validation) {
                    Add-Failure "Transaction provenance replay did not reproduce graph diff and validation output."
                }
            } catch {
                Add-Failure "Could not parse scene transaction runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Scene transaction validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Scene transaction validation passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
