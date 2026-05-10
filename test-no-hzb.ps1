# Test with HZB disabled to isolate flickering cause

param(
    [int]$Duration = 15
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Testing with HZB Occlusion Culling DISABLED" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "This test disables HZB to check if HZB temporal" -ForegroundColor Yellow
Write-Host "desynchronization is causing the flickering." -ForegroundColor Yellow
Write-Host ""

# Try multiple possible exe locations
$exePaths = @(
    (Join-Path $ENGINE_DIR "build\bin\CortexEngine.exe"),
    (Join-Path $ENGINE_DIR "build\bin\Release\CortexEngine.exe"),
    (Join-Path $ENGINE_DIR "build\bin\Debug\CortexEngine.exe")
)

$exe = $null
foreach ($path in $exePaths) {
    if (Test-Path $path) {
        $exe = $path
        Write-Host "Found exe: $exe" -ForegroundColor Green
        break
    }
}

if (-not $exe) {
    Write-Host "[ERROR] Engine executable not found" -ForegroundColor Red
    exit 1
}

# Set environment variables
$env:CORTEX_NO_HELP = "1"
$env:CORTEX_AUTO_CAMERA = "1"
$env:CORTEX_DISABLE_GPUCULL_HZB = "1"  # DISABLE HZB

$exeDir = Split-Path $exe -Parent
Write-Host "Launching with:" -ForegroundColor Yellow
Write-Host "  CORTEX_NO_HELP=1 (skip dialog)" -ForegroundColor Gray
Write-Host "  CORTEX_AUTO_CAMERA=1 (auto movement)" -ForegroundColor Gray
Write-Host "  CORTEX_DISABLE_GPUCULL_HZB=1 (HZB DISABLED)" -ForegroundColor Red
Write-Host ""

Push-Location $exeDir

$engineProcess = Start-Process -FilePath $exe -ArgumentList "--scene","engine_editor" -PassThru -WindowStyle Normal

Pop-Location

Write-Host "Engine started (PID: $($engineProcess.Id))" -ForegroundColor Green
Write-Host ""
Write-Host "Running for $Duration seconds..." -ForegroundColor Yellow
Write-Host "Watch for flickering during chunk loading!" -ForegroundColor Cyan
Write-Host ""
Write-Host "If flickering STOPS with HZB disabled, then HZB temporal" -ForegroundColor Yellow
Write-Host "desynchronization is the remaining bug to fix." -ForegroundColor Yellow
Write-Host ""

Start-Sleep -Seconds $Duration

Write-Host ""
Write-Host "Stopping engine..." -ForegroundColor Yellow
try {
    Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
    Write-Host "[OK] Engine stopped" -ForegroundColor Green
} catch {
    Write-Host "[WARNING] Engine may have already exited" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Manual Verification Required" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Did you observe flickering during the test?" -ForegroundColor Yellow
Write-Host ""
Write-Host "  [YES] = HZB is NOT the cause, need deeper investigation" -ForegroundColor Gray
Write-Host "  [NO]  = HZB is the cause, implement HZB pipeline reordering fix" -ForegroundColor Gray
Write-Host ""
