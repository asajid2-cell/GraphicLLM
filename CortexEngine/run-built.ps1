$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Push-Location -Path (Split-Path -Parent $MyInvocation.MyCommand.Path)
try {
    $exePath = Join-Path $PWD "build\\bin\\Release\\CortexEngine.exe"
    if (-not (Test-Path $exePath)) {
        Write-Host "Executable not found at $exePath" -ForegroundColor Yellow
        Write-Host "Run full-build.ps1 first to compile the project."
        exit 1
    }

    Write-Host "Launching CortexEngine..."
    & $exePath
}
finally {
    Pop-Location
}
