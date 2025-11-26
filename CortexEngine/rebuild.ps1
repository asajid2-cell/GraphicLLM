# Project Cortex - Fast Rebuild Script
# Run this after setup.ps1 to rebuild incrementally.
param(
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Rebuilding Project Cortex ($Config)" -ForegroundColor Cyan

$root = $PSScriptRoot
$buildDir = Join-Path $root "build"

if (-not (Test-Path $buildDir)) {
    Write-Host "[WARN] Build folder not found. Run setup.ps1 first." -ForegroundColor Yellow
    exit 1
}

Push-Location $buildDir

if ($Clean) {
    Write-Host "Cleaning..." -ForegroundColor Gray
    & cmake --build . --config $Config --target clean | Out-Null
}

Write-Host "Compiling..." -ForegroundColor Gray
$start = Get-Date
& cmake --build . --config $Config --parallel
$result = $LASTEXITCODE
$elapsed = (Get-Date) - $start

Pop-Location

if ($result -ne 0) {
    Write-Host "[ERROR] Rebuild failed." -ForegroundColor Red
    exit $result
}

Write-Host "[OK] Build complete in $($elapsed.TotalSeconds.ToString('F1'))s" -ForegroundColor Green

if ($Run) {
    $exe = Join-Path $root "build\bin\$Config\CortexEngine.exe"
    if (Test-Path $exe) {
        Write-Host "Launching $exe" -ForegroundColor Cyan
        Start-Process -FilePath $exe
    } else {
        Write-Host "[WARN] Executable not found at $exe" -ForegroundColor Yellow
    }
}
