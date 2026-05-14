param(
    [switch]$NoBuild,
    [switch]$Resolve,
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
    $runId = "scene_ir_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SceneIR.h"
$source = Read-Text "src/Scene/SceneIR.cpp"
$main = Read-Text "src/main.cpp"
$cmake = Read-Text "CMakeLists.txt"

Require-Contains $cmake "src/Scene/SceneIR.cpp" "SceneIR.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/SceneIR.h" "SceneIR.h is not listed with project headers."
Require-Contains $header "struct SceneIRCommand" "SceneIRCommand is missing."
Require-Contains $header "SceneIRSource" "SceneIRSource enum is missing."
Require-Contains $header "SceneIROpType" "SceneIROpType enum is missing."
Require-Contains $header "class SceneIRResolver" "SceneIRResolver is missing."
Require-Contains $header "MakeTextSceneIR" "Text scene IR adapter is missing."
Require-Contains $header "MakeSpeechSceneIR" "Speech scene IR adapter is missing."
Require-Contains $header "MakeUISceneIR" "UI scene IR adapter is missing."
Require-Contains $header "MakeProceduralSceneIR" "Procedural scene IR adapter is missing."
Require-Contains $source "resolver.Resolve" "Runtime IR self-test does not exercise resolver."
Require-Contains $source "FindByGroup" "Scene IR resolver does not target semantic groups."
Require-Contains $source "SceneTransaction" "Scene IR resolver does not emit transactions."
Require-Contains $main "--scene-ir-self-test" "Engine CLI scene IR self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before scene IR validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "scene_ir_self_test_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--scene-ir-self-test" 2>&1
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
            Add-Failure "Scene IR runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Scene IR runtime report pass=false."
                }
                if (-not [bool]$report.all_sources_accepted) {
                    Add-Failure "Not all source adapters resolved to typed scene IR."
                }
                if (-not [bool]$report.equivalent_transaction_shape) {
                    Add-Failure "Equivalent source adapters did not produce equivalent transaction shape."
                }
                if (-not [bool]$report.group_targeted) {
                    Add-Failure "Scene IR did not target semantic group members."
                }
                if (-not [bool]$report.bad_target_rejected) {
                    Add-Failure "Missing semantic group target was not rejected."
                }
            } catch {
                Add-Failure "Could not parse scene IR runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Scene IR contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Scene IR contract tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
