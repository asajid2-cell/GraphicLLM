# Force rebuild of modified files in Release mode

Write-Host "Forcing recompile by touching modified source files..." -ForegroundColor Yellow

$engineDir = Join-Path $PSScriptRoot "CortexEngine"

# Touch source files to update timestamps
$files = @(
    (Join-Path $engineDir "src\Graphics\Renderer.cpp"),
    (Join-Path $engineDir "src\Graphics\GPUCulling.cpp")
)

foreach ($file in $files) {
    if (Test-Path $file) {
        Write-Host "  Touching: $file" -ForegroundColor Gray
        (Get-Item $file).LastWriteTime = Get-Date
    } else {
        Write-Host "  [WARNING] File not found: $file" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Rebuilding Release configuration..." -ForegroundColor Cyan
Push-Location $engineDir

& powershell.exe -ExecutionPolicy Bypass -File "rebuild.ps1" -Config Release

$exitCode = $LASTEXITCODE
Pop-Location

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "Build succeeded!" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "Build failed with exit code: $exitCode" -ForegroundColor Red
}

exit $exitCode
