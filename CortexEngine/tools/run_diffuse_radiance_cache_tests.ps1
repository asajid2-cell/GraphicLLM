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
    $runId = "diffuse_radiance_cache_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Graphics/DiffuseRadianceCache.h"
$source = Read-Text "src/Graphics/DiffuseRadianceCache.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Graphics/DiffuseRadianceCache.cpp" "DiffuseRadianceCache.cpp is not compiled by CMake."
Require-Contains $cmake "src/Graphics/DiffuseRadianceCache.h" "DiffuseRadianceCache.h is not listed with project headers."
Require-Contains $header "DiffuseRadianceCache" "Diffuse radiance cache class is missing."
Require-Contains $header "historyBlend" "Diffuse radiance cache history blending is missing."
Require-Contains $source "bruteForcePathTracingRequired" "Diffuse radiance cache does not report non-path-tracing behavior."
Require-Contains $source "1024" "Diffuse radiance cache self-test does not exercise a large probe set."
Require-Contains $main "--diffuse-radiance-cache-self-test" "Engine CLI diffuse radiance cache self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before diffuse radiance cache validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "diffuse_radiance_cache_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--diffuse-radiance-cache-self-test" 2>&1
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
            Add-Failure "Diffuse radiance cache runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Diffuse radiance cache runtime report pass=false."
                }
                if ([int]$report.probe_count -lt 1024 -or [int]$report.history_frames -lt 12) {
                    Add-Failure "Diffuse radiance cache did not validate a large temporally accumulated probe set."
                }
                if (-not [bool]$report.stable_history -or [bool]$report.brute_force_path_tracing_required) {
                    Add-Failure "Diffuse radiance cache did not validate stable non-path-traced indirect light."
                }
            } catch {
                Add-Failure "Could not parse diffuse radiance cache runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Diffuse radiance cache tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Diffuse radiance cache tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
