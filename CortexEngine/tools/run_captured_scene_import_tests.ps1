param(
    [switch]$NoBuild,
    [switch]$RejectEditableCaptureWithoutProxy,
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
    $runId = "captured_scene_import_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/CapturedSceneImport.h"
$source = Read-Text "src/Scene/CapturedSceneImport.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Scene/CapturedSceneImport.cpp" "CapturedSceneImport.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/CapturedSceneImport.h" "CapturedSceneImport.h is not listed with project headers."
Require-Contains $header "CapturedSceneReferenceLayer" "Captured scene reference layer type is missing."
Require-Contains $header "proxyGeometryIds" "Captured scene proxy geometry list is missing."
Require-Contains $header "semanticAnchorIds" "Captured scene semantic anchors are missing."
Require-Contains $source "authoritativeGeometry = false" "Captured scenes are not forced into non-authoritative reference layers."
Require-Contains $source "editableWorldRequested" "Captured scene editable-world rejection is missing."
Require-Contains $main "--captured-scene-import-self-test" "Engine CLI captured scene import self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before captured scene import validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "captured_scene_import_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--captured-scene-import-self-test" 2>&1
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
            Add-Failure "Captured scene import runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Captured scene import runtime report pass=false."
                }
                if (-not [bool]$report.valid.accepted -or
                    [int]$report.valid.proxy_count -lt 1 -or
                    [int]$report.valid.anchor_count -lt 1 -or
                    [bool]$report.valid.authoritative_geometry -or
                    [bool]$report.valid.editable_world) {
                    Add-Failure "Valid capture did not import as a non-authoritative reference layer with proxies and anchors."
                }
                if ([bool]$report.missing_proxy.accepted -or [bool]$report.editable_world.accepted) {
                    Add-Failure "Invalid captured scene import was not rejected."
                }
            } catch {
                Add-Failure "Could not parse captured scene import runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Captured scene import tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Captured scene import tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
