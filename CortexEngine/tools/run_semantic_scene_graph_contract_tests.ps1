param(
    [switch]$NoBuild,
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
    $runId = "semantic_scene_graph_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SemanticGraph.h"
$source = Read-Text "src/Scene/SemanticGraph.cpp"
$main = Read-Text "src/main.cpp"
$cmake = Read-Text "CMakeLists.txt"

Require-Contains $cmake "src/Scene/SemanticGraph.cpp" "SemanticGraph.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/SemanticGraph.h" "SemanticGraph.h is not listed with project headers."
Require-Contains $header "class SemanticSceneGraph" "SemanticSceneGraph runtime type is missing."
Require-Contains $header "SemanticGraphDiff" "Semantic graph diff type is missing."
Require-Contains $header "SemanticRuntimeObjectPlan" "Semantic graph runtime plan type is missing."
Require-Contains $header "SemanticProvenance" "Semantic provenance field group is missing."
Require-Contains $header "SemanticBudget" "Semantic budget field group is missing."
Require-Contains $header "SemanticInvalidation" "Semantic invalidation field group is missing."
Require-Contains $header "FindByGroup" "Semantic group lookup is missing."
Require-Contains $header "CompileRuntimePlan" "Semantic graph compile plan hook is missing."
Require-Contains $source "RunSemanticGraphSelfTestJson" "Runtime semantic graph self-test is missing."
Require-Contains $main "--semantic-graph-self-test" "Engine CLI self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before semantic scene graph runtime test."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "semantic_graph_self_test_stdout.txt"
        $stderrPath = Join-Path $LogDir "semantic_graph_self_test_stderr.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--semantic-graph-self-test" 2>&1
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }

        $outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
        $outputText | Set-Content -Path $stdoutPath -Encoding UTF8
        "" | Set-Content -Path $stderrPath -Encoding UTF8
        if (-not [string]::IsNullOrWhiteSpace($outputText)) {
            $outputText -split "`r?`n" | ForEach-Object { Write-Host $_ }
        }

        if ($exitCode -ne 0) {
            Add-Failure "Semantic graph runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Semantic graph runtime report pass=false."
                }
                if ([int]$report.runtime_plan_count_before_undo -lt 3) {
                    Add-Failure "Runtime plan did not include the expected semantic objects."
                }
                if (-not [bool]$report.diff.updated_material) {
                    Add-Failure "Semantic diff update was not observed."
                }
                if (-not [bool]$report.diff.undo_restored) {
                    Add-Failure "Semantic diff inversion did not restore graph state."
                }
                foreach ($field in @(
                    "object_identity", "editable_group", "semantic_type", "support_relation",
                    "region", "material_intent", "provenance", "budget", "invalidation")) {
                    if (-not [bool]$report.required_v0_fields.$field) {
                        Add-Failure "Required V0 semantic field '$field' was not proven by runtime self-test."
                    }
                }
            } catch {
                Add-Failure "Could not parse semantic graph runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Semantic scene graph contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Semantic scene graph contract tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
