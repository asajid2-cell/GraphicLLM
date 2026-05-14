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
    $runId = "authoring_input_router_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/AuthoringInputRouter.h"
$source = Read-Text "src/Scene/AuthoringInputRouter.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Scene/AuthoringInputRouter.cpp" "AuthoringInputRouter.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/AuthoringInputRouter.h" "AuthoringInputRouter.h is not listed with project headers."
Require-Contains $header "RouteAuthoringInput" "Authoring input router entrypoint is missing."
Require-Contains $source "SceneIRResolver" "Authoring input router does not compile through Scene IR."
Require-Contains $source "ApplyTransactionToRuntime" "Authoring input router does not apply runtime transactions."
Require-Contains $source "AdmitGeneratedAsset" "Authoring input router does not route generated assets through admission."
Require-Contains $source "unconstrained large LLM entity list rejected" "Authoring input router does not reject unconstrained large LLM entity lists."
Require-Contains $main "--authoring-input-router-self-test" "Engine CLI authoring input router self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before authoring input router validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "authoring_input_router_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--authoring-input-router-self-test" 2>&1
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
            Add-Failure "Authoring input router self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Authoring input router runtime report pass=false."
                }
                if (-not [bool]$report.all_sources_accepted -or
                    -not [bool]$report.all_compiled_to_scene_ir -or
                    -not [bool]$report.all_targeted_semantic_groups -or
                    -not [bool]$report.large_llm_rejected_before_mutation -or
                    -not [bool]$report.generated_asset_asked_budget_before_emit) {
                    Add-Failure "Authoring input router did not prove shared IR routing, LLM guard, and producer backpressure."
                }
            } catch {
                Add-Failure "Could not parse authoring input router report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Authoring input router tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Authoring input router tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
