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
    $runId = "many_light_sampling_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Graphics/ManyLightReservoir.h"
$source = Read-Text "src/Graphics/ManyLightReservoir.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Graphics/ManyLightReservoir.cpp" "ManyLightReservoir.cpp is not compiled by CMake."
Require-Contains $cmake "src/Graphics/ManyLightReservoir.h" "ManyLightReservoir.h is not listed with project headers."
Require-Contains $header "BuildManyLightReservoir" "Many-light reservoir builder is missing."
Require-Contains $header "ManyLightSampleInput" "Many-light input schema is missing."
Require-Contains $source "usedReservoirSampling" "Many-light implementation does not report reservoir sampling use."
Require-Contains $source "4096" "Many-light self-test does not exercise a large light set."
Require-Contains $main "--many-light-sampling-self-test" "Engine CLI many-light self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before many-light sampling validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "many_light_sampling_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--many-light-sampling-self-test" 2>&1
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
            Add-Failure "Many-light runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Many-light runtime report pass=false."
                }
                if ([int]$report.input_lights -lt 4096) {
                    Add-Failure "Many-light self-test did not exercise thousands of lights."
                }
                if ([int]$report.sample_count -ne 64 -or -not [bool]$report.used_reservoir_sampling) {
                    Add-Failure "Many-light self-test did not reduce the light set through reservoir sampling."
                }
                if (-not [bool]$report.deterministic_replay) {
                    Add-Failure "Many-light sampling was not deterministic for the same seed."
                }
            } catch {
                Add-Failure "Could not parse many-light runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Many-light sampling tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Many-light sampling tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
