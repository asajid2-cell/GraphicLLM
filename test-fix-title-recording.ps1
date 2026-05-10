# Test Flickering - Record by Window Title
# Uses FFmpeg's title parameter for cleaner window capture

param(
    [int]$Duration = 22
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$OUTPUT_DIR = Join-Path $SCRIPT_DIR "fix_verification"
$TIMESTAMP = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$OUTPUT_FILE = Join-Path $OUTPUT_DIR "flickering_TITLE_$TIMESTAMP.mp4"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Flickering Test: Title-Based Window Recording" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""

New-Item -ItemType Directory -Force -Path $OUTPUT_DIR | Out-Null

# Find engine executable
$exePaths = @(
    (Join-Path $ENGINE_DIR "build\bin\CortexEngine.exe"),
    (Join-Path $ENGINE_DIR "build\bin\Release\CortexEngine.exe")
)

$exe = $null
foreach ($path in $exePaths) {
    if (Test-Path $path) {
        $exe = $path
        break
    }
}

if (-not $exe) {
    Write-Host "[ERROR] Engine executable not found" -ForegroundColor Red
    exit 1
}

Write-Host "[1/4] Launching CortexEngine..." -ForegroundColor Yellow

$exeDir = Split-Path $exe -Parent
Push-Location $exeDir

# Set environment variables
$env:CORTEX_NO_HELP = "1"
$env:CORTEX_AUTO_CAMERA = "1"

$engineProcess = Start-Process -FilePath $exe -ArgumentList "--scene","engine_editor" -PassThru -WindowStyle Normal

Pop-Location

# Wait for window
Write-Host "Waiting for window..." -ForegroundColor Gray
Start-Sleep -Seconds 6

Write-Host "[OK] Engine started" -ForegroundColor Green

Write-Host ""
Write-Host "[2/4] Starting recording (using window title)..." -ForegroundColor Yellow

# Try recording by window title - this is often more reliable than coordinates
# The window title is likely "CortexEngine" or "Project Cortex"
$ffmpegArgs = @(
    "-f", "gdigrab",
    "-framerate", "60",
    "-i", "title=CortexEngine",
    "-c:v", "libx264",
    "-preset", "ultrafast",
    "-crf", "18",
    "-pix_fmt", "yuv420p",
    "-y",
    $OUTPUT_FILE
)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "ffmpeg"
$psi.Arguments = ($ffmpegArgs -join " ")
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardError = $true
$psi.RedirectStandardOutput = $true
$psi.CreateNoWindow = $true

$ffmpegProcess = New-Object System.Diagnostics.Process
$ffmpegProcess.StartInfo = $psi

# Capture stderr to check for errors
$stderrBuilder = New-Object System.Text.StringBuilder
$stderrHandler = {
    if (-not [String]::IsNullOrEmpty($EventArgs.Data)) {
        $Event.MessageData.AppendLine($EventArgs.Data)
    }
}
$stderrEvent = Register-ObjectEvent -InputObject $ffmpegProcess -EventName ErrorDataReceived -Action $stderrHandler -MessageData $stderrBuilder

$ffmpegProcess.Start() | Out-Null
$ffmpegProcess.BeginErrorReadLine()

Start-Sleep -Seconds 3

# Check if ffmpeg started successfully
if ($ffmpegProcess.HasExited) {
    Write-Host "[ERROR] FFmpeg failed to start. Trying fallback method..." -ForegroundColor Red
    Unregister-Event -SourceIdentifier $stderrEvent.Name

    # Fallback to desktop recording
    Write-Host "Falling back to full desktop recording..." -ForegroundColor Yellow

    Add-Type -AssemblyName System.Windows.Forms
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $width = $screen.Width
    $height = $screen.Height

    $ffmpegArgs = @(
        "-f", "gdigrab",
        "-framerate", "60",
        "-video_size", "${width}x${height}",
        "-i", "desktop",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-crf", "18",
        "-pix_fmt", "yuv420p",
        "-y",
        $OUTPUT_FILE
    )

    $psi.Arguments = ($ffmpegArgs -join " ")
    $ffmpegProcess = New-Object System.Diagnostics.Process
    $ffmpegProcess.StartInfo = $psi
    $ffmpegProcess.Start() | Out-Null
    Start-Sleep -Seconds 2
}

Write-Host "[OK] Recording started" -ForegroundColor Green

# Record
Write-Host ""
Write-Host "[3/4] Recording for $Duration seconds..." -ForegroundColor Yellow
Start-Sleep -Seconds $Duration

# Stop engine
Write-Host ""
Write-Host "Stopping engine..." -ForegroundColor Yellow
Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue

# Stop recording
Write-Host "Stopping recording..." -ForegroundColor Yellow
Start-Sleep -Seconds 1
$ffmpegProcess.StandardInput.WriteLine("q")
$exited = $ffmpegProcess.WaitForExit(10000)
if (!$exited) {
    Stop-Process -Id $ffmpegProcess.Id -Force
}

if ($stderrEvent) {
    Unregister-Event -SourceIdentifier $stderrEvent.Name -ErrorAction SilentlyContinue
}

# Validate
Write-Host ""
Write-Host "[4/4] Validating video..." -ForegroundColor Yellow

if (Test-Path $OUTPUT_FILE) {
    $fileSize = (Get-Item $OUTPUT_FILE).Length / 1MB

    if ($fileSize -lt 0.1) {
        Write-Host "[ERROR] Video file is too small ($([math]::Round($fileSize, 3)) MB) - likely corrupt" -ForegroundColor Red
        exit 1
    }

    $ffprobeResult = & ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 $OUTPUT_FILE 2>&1

    if ($LASTEXITCODE -eq 0 -and $ffprobeResult) {
        $duration = [math]::Round([double]$ffprobeResult, 1)
        Write-Host "[OK] Video is valid (duration: ${duration}s, size: $([math]::Round($fileSize, 2)) MB)" -ForegroundColor Green

        Write-Host ""
        Write-Host "==================================================" -ForegroundColor Cyan
        Write-Host "  Recording Complete" -ForegroundColor Cyan
        Write-Host "==================================================" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Video: $OUTPUT_FILE" -ForegroundColor White
        Write-Host ""
        Write-Host "Opening video for manual review..." -ForegroundColor Gray
        Write-Host "Please watch for flickering during chunk loading!" -ForegroundColor Yellow
        Write-Host ""
        Start-Process $OUTPUT_FILE
    } else {
        Write-Host "[ERROR] Video file is CORRUPT!" -ForegroundColor Red
        Write-Host "FFprobe output: $ffprobeResult" -ForegroundColor Gray
    }
} else {
    Write-Host "[ERROR] Output file not found!" -ForegroundColor Red
}
