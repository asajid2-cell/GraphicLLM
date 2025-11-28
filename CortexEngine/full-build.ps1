$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Push-Location -Path (Split-Path -Parent $MyInvocation.MyCommand.Path)
try {
    Write-Host "== CortexEngine Full Setup + Build ==" -ForegroundColor Cyan

    # Ensure vcpkg toolchain is available
    if (-not $env:VCPKG_ROOT) {
        $defaultVcpkg = Join-Path $PWD "..\\..\\vcpkg"
        if (Test-Path $defaultVcpkg) {
            $env:VCPKG_ROOT = (Resolve-Path $defaultVcpkg)
            Write-Host "Using VCPKG_ROOT=$env:VCPKG_ROOT"
        } else {
            Write-Warning "VCPKG_ROOT not set and default vcpkg not found. setup.ps1 will attempt install."
        }
    }

    # Run repo setup (installs deps, configures manifests)
    Write-Host "Running setup.ps1..."
    & "$PWD\\setup.ps1"

    # Configure + build (Release)
    Write-Host "Configuring CMake..."
    $cmakeArgs = @("-B", "build", "-S", ".", "-DCMAKE_BUILD_TYPE=Release")
    if ($env:VCPKG_ROOT) {
        $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\\scripts\\buildsystems\\vcpkg.cmake"
    }
    & cmake @cmakeArgs

    Write-Host "Building CortexEngine (Release)..."
    & cmake --build build --config Release

    Write-Host "Full build complete." -ForegroundColor Green
}
finally {
    Pop-Location
}
