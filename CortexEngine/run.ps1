# Project Cortex - Quick Run Script (PowerShell)

param(
    [string]$Config = "Release"
)

$exePath = Join-Path $PSScriptRoot "build\bin\$Config\CortexEngine.exe"

if (-not (Test-Path $exePath)) {
    Write-Host "Executable not found: $exePath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Did you build the project? Run one of:" -ForegroundColor Yellow
    Write-Host "  .\setup.ps1          (Full setup + build)" -ForegroundColor Gray
    Write-Host "  .\build.ps1          (Just build)" -ForegroundColor Gray
    exit 1
}

Write-Host "Starting Project Cortex..." -ForegroundColor Cyan
Write-Host "Executable: $exePath" -ForegroundColor Gray
Write-Host ""

Push-Location (Join-Path $PSScriptRoot "build\bin\$Config")
& .\CortexEngine.exe
Pop-Location
