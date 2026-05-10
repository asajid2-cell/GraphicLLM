# Test flickering fix and capture engine logs
# This script runs the engine with automated camera movement and captures debug logs

param(
    [int]$Duration = 15
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$LOG_DIR = Join-Path $SCRIPT_DIR "debug_logs"
$TIMESTAMP = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$LOG_FILE = Join-Path $LOG_DIR "engine_log_$TIMESTAMP.txt"

# Create log directory
New-Item -ItemType Directory -Force -Path $LOG_DIR | Out-Null

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Flickering Debug Test with Log Capture" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
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
    Write-Host "[ERROR] Engine executable not found in any location" -ForegroundColor Red
    Write-Host "Searched:" -ForegroundColor Yellow
    foreach ($path in $exePaths) {
        Write-Host "  $path" -ForegroundColor Gray
    }
    exit 1
}

# Set environment variables for debugging and automation
$env:CORTEX_NO_HELP = "1"
$env:CORTEX_AUTO_CAMERA = "1"

# Launch engine with output redirection
$exeDir = Split-Path $exe -Parent
Write-Host "Launching from: $exeDir" -ForegroundColor Gray
Write-Host "Environment:" -ForegroundColor Yellow
Write-Host "  CORTEX_NO_HELP=1" -ForegroundColor Gray
Write-Host "  CORTEX_AUTO_CAMERA=1" -ForegroundColor Gray
Write-Host "  Logging to: $LOG_FILE" -ForegroundColor Gray
Write-Host ""

Push-Location $exeDir

# Start engine and capture output
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = "--scene engine_editor"
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.CreateNoWindow = $false
$psi.WorkingDirectory = $exeDir

$engineProcess = New-Object System.Diagnostics.Process
$engineProcess.StartInfo = $psi

# Event handlers to capture output
$outputBuilder = New-Object System.Text.StringBuilder
$errorBuilder = New-Object System.Text.StringBuilder

$outputHandler = {
    if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
        $Event.MessageData.AppendLine($EventArgs.Data)
        Write-Host $EventArgs.Data -ForegroundColor Gray
    }
}

$errorHandler = {
    if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
        $Event.MessageData.AppendLine("[STDERR] " + $EventArgs.Data)
        Write-Host $EventArgs.Data -ForegroundColor Yellow
    }
}

$outputEvent = Register-ObjectEvent -InputObject $engineProcess -EventName OutputDataReceived -Action $outputHandler -MessageData $outputBuilder
$errorEvent = Register-ObjectEvent -InputObject $engineProcess -EventName ErrorDataReceived -Action $errorHandler -MessageData $errorBuilder

$engineProcess.Start() | Out-Null
$engineProcess.BeginOutputReadLine()
$engineProcess.BeginErrorReadLine()

Pop-Location

Write-Host "[OK] Engine started (PID: $($engineProcess.Id))" -ForegroundColor Green
Write-Host ""
Write-Host "Running for $Duration seconds with automated camera movement..." -ForegroundColor Yellow
Write-Host "Watch for [FLICKERING DEBUG] log lines below:" -ForegroundColor Cyan
Write-Host ""

# Wait for test duration
Start-Sleep -Seconds $Duration

# Stop engine
Write-Host ""
Write-Host "Stopping engine..." -ForegroundColor Yellow
try {
    $engineProcess.Kill()
    $engineProcess.WaitForExit(5000)
    Write-Host "[OK] Engine stopped" -ForegroundColor Green
} catch {
    Write-Host "[WARNING] Error stopping engine: $_" -ForegroundColor Yellow
}

# Clean up event handlers
Unregister-Event -SourceIdentifier $outputEvent.Name
Unregister-Event -SourceIdentifier $errorEvent.Name

# Save logs to file
$combinedLog = $outputBuilder.ToString() + "`n" + $errorBuilder.ToString()
Set-Content -Path $LOG_FILE -Value $combinedLog

Write-Host ""
Write-Host "==================================================" -ForegroundColor Green
Write-Host "  Test Complete" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Logs saved to: $LOG_FILE" -ForegroundColor Cyan
Write-Host ""

# Parse debug logs for key information
Write-Host "Analyzing debug logs..." -ForegroundColor Yellow
Write-Host ""

$gpuCullingLines = $combinedLog -split "`n" | Where-Object { $_ -match "\[FLICKERING DEBUG\]" }

if ($gpuCullingLines.Count -gt 0) {
    Write-Host "Found $($gpuCullingLines.Count) debug log lines:" -ForegroundColor Green
    Write-Host ""
    foreach ($line in $gpuCullingLines) {
        Write-Host $line -ForegroundColor Cyan
    }
    Write-Host ""
} else {
    Write-Host "[WARNING] No [FLICKERING DEBUG] lines found in logs!" -ForegroundColor Red
    Write-Host "This may indicate:" -ForegroundColor Yellow
    Write-Host "  - The debug code wasn't compiled in" -ForegroundColor Gray
    Write-Host "  - The engine crashed before logging" -ForegroundColor Gray
    Write-Host "  - Output redirection didn't capture logs" -ForegroundColor Gray
    Write-Host ""
}

# Check for specific indicators
if ($combinedLog -match "GPU Culling Enabled=(\w+)") {
    $enabled = $Matches[1]
    if ($enabled -eq "true" -or $enabled -eq "1") {
        Write-Host "[INFO] GPU Culling is ENABLED" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] GPU Culling is DISABLED!" -ForegroundColor Red
        Write-Host "This would explain why flickering persists" -ForegroundColor Yellow
    }
}

if ($combinedLog -match "Bindless=(\w+)") {
    $bindless = $Matches[1]
    if ($bindless -eq "true" -or $bindless -eq "1") {
        Write-Host "[INFO] Bindless rendering is ENABLED" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] Bindless rendering is DISABLED!" -ForegroundColor Red
        Write-Host "GPU culling requires bindless to be enabled" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Open log file to see full engine output:" -ForegroundColor Gray
Write-Host $LOG_FILE -ForegroundColor Cyan
