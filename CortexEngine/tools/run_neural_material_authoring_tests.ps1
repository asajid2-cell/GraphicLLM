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
    $runId = "neural_material_authoring_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/NeuralMaterialAuthoring.h"
$source = Read-Text "src/Scene/NeuralMaterialAuthoring.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Scene/NeuralMaterialAuthoring.cpp" "NeuralMaterialAuthoring.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/NeuralMaterialAuthoring.h" "NeuralMaterialAuthoring.h is not listed with project headers."
Require-Contains $header "NeuralPBRMaterialAsset" "Editable neural PBR material asset type is missing."
Require-Contains $header "SerializeNeuralPBRMaterial" "Neural material storage serialization is missing."
Require-Contains $header "DeserializeNeuralPBRMaterial" "Neural material reload path is missing."
Require-Contains $source "required PBR texture slot missing" "Neural material validation does not enforce required PBR slots."
Require-Contains $source "editable = true" "Neural material authoring does not mark generated assets editable."
Require-Contains $main "--neural-material-authoring-self-test" "Engine CLI neural material authoring self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before neural material authoring validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "neural_material_authoring_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--neural-material-authoring-self-test" 2>&1
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
            Add-Failure "Neural material authoring runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Neural material authoring runtime report pass=false."
                }
                if (-not [bool]$report.authored.accepted -or
                    -not [bool]$report.authored.editable -or
                    -not [bool]$report.authored.generated -or
                    [int]$report.authored.texture_slot_count -lt 4) {
                    Add-Failure "Generated material fixture was not stored as an editable PBR asset."
                }
                if (-not [bool]$report.reloaded.accepted -or -not [bool]$report.edited.accepted -or [int]$report.edited.version -lt 2) {
                    Add-Failure "Generated material fixture could not be reloaded and edited."
                }
                if ([bool]$report.missing_normal.accepted) {
                    Add-Failure "Incomplete neural material fixture was accepted."
                }
            } catch {
                Add-Failure "Could not parse neural material authoring runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Neural material authoring tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Neural material authoring tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
