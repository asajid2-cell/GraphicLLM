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
    $runId = "semantic_runtime_compiler_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SemanticRuntimeCompiler.h"
$source = Read-Text "src/Scene/SemanticRuntimeCompiler.cpp"
$cmake = Read-Text "CMakeLists.txt"
$main = Read-Text "src/main.cpp"

Require-Contains $cmake "src/Scene/SemanticRuntimeCompiler.cpp" "SemanticRuntimeCompiler.cpp is not compiled by CMake."
Require-Contains $cmake "src/Scene/SemanticRuntimeCompiler.h" "SemanticRuntimeCompiler.h is not listed with project headers."
Require-Contains $header "SemanticEcsEntityJob" "Semantic runtime compiler does not define ECS entity jobs."
Require-Contains $header "SemanticRendererResourceJob" "Semantic runtime compiler does not define renderer resource jobs."
Require-Contains $header "CompileSemanticGraphForRuntime" "Semantic graph runtime compiler function is missing."
Require-Contains $source "CompileRuntimePlan" "Semantic runtime compiler does not consume semantic graph runtime plans."
Require-Contains $main "--semantic-runtime-compiler-self-test" "Engine CLI semantic runtime compiler self-test entrypoint is missing."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before semantic runtime compiler validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "semantic_runtime_compiler_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--semantic-runtime-compiler-self-test" 2>&1
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
            Add-Failure "Semantic runtime compiler self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Semantic runtime compiler runtime report pass=false."
                }
                if ([int]$report.ecs_entity_jobs -lt 1 -or [int]$report.renderer_resource_jobs -lt 1 -or -not [bool]$report.has_invalidation_hints) {
                    Add-Failure "Semantic graph did not compile to ECS jobs, renderer jobs, and invalidation hints."
                }
            } catch {
                Add-Failure "Could not parse semantic runtime compiler report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Semantic runtime compiler tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Semantic runtime compiler tests passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
