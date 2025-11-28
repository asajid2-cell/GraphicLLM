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

# Ensure Visual Studio build environment is available (cl.exe, standard headers, Windows SDK).
try {
    $clCmd = Get-Command cl -ErrorAction SilentlyContinue
} catch {
    $clCmd = $null
}

if (-not $clCmd) {
    try {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -property installationPath
            if ($vsPath) {
                $vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
                if (Test-Path $vsDevCmd) {
                    Write-Host "Importing Visual Studio build environment..." -ForegroundColor Gray
                    $tempFile = [System.IO.Path]::GetTempFileName()
                    cmd /c " `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 > NUL && set > `"$tempFile`" "
                    Get-Content $tempFile | ForEach-Object {
                        if ($_ -match "^(.*?)=(.*)$") {
                            $name = $matches[1]; $value = $matches[2]
                            if ($name -match '^(PATH|INCLUDE|LIB|LIBPATH|VC|WindowsSDK)') {
                                Set-Item -Path "env:$name" -Value $value
                            }
                        }
                    }
                    Remove-Item $tempFile -Force
                }
            }
        }
    } catch {
        Write-Host "[WARN] Failed to import Visual Studio environment automatically; if the build fails with missing standard headers, run setup.ps1 from a VS Developer shell." -ForegroundColor Yellow
    }
}

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
