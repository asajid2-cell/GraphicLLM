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
    $runId = "scene_layering_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SceneLayering.h"
$source = Read-Text "src/Scene/SceneLayering.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Scene/SceneLayering.cpp" "SceneLayering.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/SceneLayering.h" "SceneLayering.h is not listed with project headers."
Require-Contains $header "AuthoredBaseline" "Authored baseline layer kind is missing."
Require-Contains $header "GeneratedProposal" "Generated proposal layer kind is missing."
Require-Contains $header "UserOverride" "User override layer kind is missing."
Require-Contains $header "MaterialVariant" "Material variant layer kind is missing."
Require-Contains $header "ValidationAnnotation" "Validation annotation layer kind is missing."
Require-Contains $header "ResolveSceneLayersToTransaction" "Scene layer resolver is missing."
Require-Contains $source "SceneTransaction transaction" "Scene layer resolver does not emit a transaction."
Require-Contains $main "--scene-layering-self-test" "Engine CLI scene layering self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before scene layering validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "scene_layering_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--scene-layering-self-test" 2>&1
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
            Add-Failure "Scene layering runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Scene layering runtime report pass=false."
                }
                if (-not [bool]$report.runtime_receives_transaction -or
                    -not [bool]$report.override_applied -or
                    -not [bool]$report.provenance_complete -or
                    [int]$report.resolved_object_count -lt 3) {
                    Add-Failure "Layered scene fixture did not resolve to a provenance-complete runtime transaction."
                }
            } catch {
                Add-Failure "Could not parse scene layering runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Scene layering contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Scene layering contract tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
