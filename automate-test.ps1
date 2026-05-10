# Automated Test Recording Script for CortexEngine
# Records screen while running the project and executing preset commands

param(
    [int]$Duration = 0,             # Recording duration in seconds (0 = use config)
    [string]$OutputDir = "",        # Output directory (empty = use config)
    [switch]$SkipBuild,             # Skip build step
    [string]$ConfigFile = "test-config.json"
)

# Configuration
$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$BUILD_BAT = Join-Path $ENGINE_DIR "build.bat"
$RUN_BAT = Join-Path $ENGINE_DIR "run.bat"
$CONFIG_PATH = Join-Path $SCRIPT_DIR $ConfigFile

# Load configuration from JSON if it exists
$config = $null
if (Test-Path $CONFIG_PATH) {
    try {
        $config = Get-Content $CONFIG_PATH -Raw | ConvertFrom-Json
        Write-Host "[OK] Loaded config from $ConfigFile" -ForegroundColor Green
    } catch {
        Write-Host "[WARNING] Failed to load config file: $_" -ForegroundColor Yellow
        Write-Host "Using default settings..." -ForegroundColor Yellow
    }
}

# Apply configuration with parameter overrides
if ($config) {
    if ($Duration -eq 0) { $Duration = $config.recording.duration }
    if ($OutputDir -eq "") { $OutputDir = $config.recording.output_directory }
    if (-not $SkipBuild) { $SkipBuild = $config.build.skip_build }
    $framerate = $config.recording.framerate
    $quality = $config.recording.quality
} else {
    if ($Duration -eq 0) { $Duration = 60 }
    if ($OutputDir -eq "") { $OutputDir = "test_recordings" }
    $framerate = 30
    $quality = "ultrafast"
}

$TIMESTAMP = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$OUTPUT_FILE = Join-Path $SCRIPT_DIR $OutputDir "test_$TIMESTAMP.mp4"

# Ensure output directory exists
New-Item -ItemType Directory -Force -Path (Join-Path $SCRIPT_DIR $OutputDir) | Out-Null

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  CortexEngine Automated Test Recording" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""

# Check if FFmpeg is installed
try {
    $null = ffmpeg -version 2>$null
    Write-Host "[OK] FFmpeg found" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] FFmpeg not found in PATH!" -ForegroundColor Red
    Write-Host "Please install FFmpeg: https://ffmpeg.org/download.html" -ForegroundColor Yellow
    Write-Host "Or use: winget install ffmpeg" -ForegroundColor Yellow
    exit 1
}

# Build the project (optional)
if (-not $SkipBuild) {
    Write-Host ""
    Write-Host "[1/5] Building project..." -ForegroundColor Yellow
    Push-Location $ENGINE_DIR
    & cmd /c "build.bat"
    $buildResult = $LASTEXITCODE
    Pop-Location

    if ($buildResult -ne 0) {
        Write-Host "[ERROR] Build failed! Aborting test." -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] Build complete" -ForegroundColor Green
} else {
    Write-Host "[1/5] Skipping build (--SkipBuild flag)" -ForegroundColor Yellow
}

# Start FFmpeg screen recording
Write-Host ""
Write-Host "[2/5] Starting screen recording..." -ForegroundColor Yellow
Write-Host "Output: $OUTPUT_FILE" -ForegroundColor Cyan

# Get primary monitor resolution
Add-Type -AssemblyName System.Windows.Forms
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$width = $screen.Width
$height = $screen.Height

Write-Host "Recording resolution: ${width}x${height}" -ForegroundColor Cyan

# Start FFmpeg in background
$ffmpegArgs = @(
    "-f", "gdigrab",
    "-framerate", "$framerate",
    "-video_size", "${width}x${height}",
    "-i", "desktop",
    "-c:v", "libx264",
    "-preset", "$quality",
    "-crf", "23",
    "-pix_fmt", "yuv420p",
    "-y",
    $OUTPUT_FILE
)

$ffmpegProcess = Start-Process -FilePath "ffmpeg" -ArgumentList $ffmpegArgs -NoNewWindow -PassThru
Start-Sleep -Seconds 2

if ($ffmpegProcess.HasExited) {
    Write-Host "[ERROR] FFmpeg failed to start!" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Recording started (PID: $($ffmpegProcess.Id))" -ForegroundColor Green

# Start the CortexEngine
Write-Host ""
Write-Host "[3/5] Launching CortexEngine..." -ForegroundColor Yellow

# Find the executable
$exe1 = Join-Path $ENGINE_DIR "build\bin\Release\CortexEngine.exe"
$exe2 = Join-Path $ENGINE_DIR "build\CortexEngine.exe"

if (Test-Path $exe1) {
    $exePath = $exe1
} elseif (Test-Path $exe2) {
    $exePath = $exe2
} else {
    Write-Host "[ERROR] CortexEngine.exe not found!" -ForegroundColor Red
    Stop-Process -Id $ffmpegProcess.Id -Force
    exit 1
}

# Start the engine
Push-Location $ENGINE_DIR
$engineProcess = Start-Process -FilePath "cmd" -ArgumentList "/c run.bat" -PassThru -WindowStyle Normal
Pop-Location

Start-Sleep -Seconds 3

if ($engineProcess.HasExited) {
    Write-Host "[ERROR] Engine failed to start!" -ForegroundColor Red
    Stop-Process -Id $ffmpegProcess.Id -Force
    exit 1
}

Write-Host "[OK] Engine started (PID: $($engineProcess.Id))" -ForegroundColor Green

# Execute preset commands / keyboard inputs
Write-Host ""
Write-Host "[4/5] Executing preset commands..." -ForegroundColor Yellow

# Add your custom keyboard inputs here
# This function sends keys to the active window
function Send-Keys {
    param([string]$Keys)

    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
}

# Wait for engine to load
$waitTime = 5
if ($config -and $config.commands.wait_before_commands) {
    $waitTime = $config.commands.wait_before_commands
}
Write-Host "  - Waiting $waitTime seconds for engine to load..." -ForegroundColor Gray
Start-Sleep -Seconds $waitTime

# Execute commands from config or use defaults
try {
    if ($config -and $config.commands.key_sequences) {
        Write-Host "  - Executing configured command sequences..." -ForegroundColor Gray

        foreach ($sequence in $config.commands.key_sequences) {
            if ($sequence.description) {
                Write-Host "    > $($sequence.description)" -ForegroundColor Cyan
            }

            foreach ($key in $sequence.keys) {
                Send-Keys $key
                if ($sequence.delay_between_keys) {
                    Start-Sleep -Milliseconds $sequence.delay_between_keys
                }
            }

            if ($sequence.delay_after_sequence) {
                Start-Sleep -Milliseconds $sequence.delay_after_sequence
            }
        }
    } else {
        # Default commands if no config
        Write-Host "  - Executing default test inputs..." -ForegroundColor Gray

        # Example: Press WASD keys to test movement
        Send-Keys "w"
        Start-Sleep -Milliseconds 500
        Send-Keys "a"
        Start-Sleep -Milliseconds 500
        Send-Keys "s"
        Start-Sleep -Milliseconds 500
        Send-Keys "d"
        Start-Sleep -Milliseconds 500

        # Example: Press Space to jump/interact
        Send-Keys " "
        Start-Sleep -Seconds 1

        # Example: Press ESC to open menu
        Send-Keys "{ESC}"
        Start-Sleep -Seconds 1
    }

    Write-Host "[OK] Commands executed" -ForegroundColor Green
} catch {
    Write-Host "[WARNING] Some commands may have failed: $_" -ForegroundColor Yellow
}

# Wait for the remaining duration
$remainingTime = $Duration - 10  # We already waited ~10 seconds
if ($remainingTime -gt 0) {
    Write-Host ""
    Write-Host "[5/5] Recording for $remainingTime more seconds..." -ForegroundColor Yellow

    for ($i = $remainingTime; $i -gt 0; $i--) {
        Write-Host -NoNewline "`rTime remaining: $i seconds   "
        Start-Sleep -Seconds 1
    }
    Write-Host ""
}

# Stop the engine
Write-Host ""
Write-Host "Stopping CortexEngine..." -ForegroundColor Yellow
try {
    Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
    Write-Host "[OK] Engine stopped" -ForegroundColor Green
} catch {
    Write-Host "[WARNING] Engine process may have already exited" -ForegroundColor Yellow
}

# Stop recording
Write-Host ""
Write-Host "Stopping recording..." -ForegroundColor Yellow
Start-Sleep -Seconds 1

# Send 'q' to FFmpeg to stop gracefully
try {
    Stop-Process -Id $ffmpegProcess.Id
    Start-Sleep -Seconds 3
    Write-Host "[OK] Recording stopped" -ForegroundColor Green
} catch {
    Write-Host "[WARNING] FFmpeg process may have already exited" -ForegroundColor Yellow
}

# Verify output file
Write-Host ""
if (Test-Path $OUTPUT_FILE) {
    $fileSize = (Get-Item $OUTPUT_FILE).Length / 1MB
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host "  Test recording complete!" -ForegroundColor Green
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Video saved to: $OUTPUT_FILE" -ForegroundColor Cyan
    Write-Host "File size: $([math]::Round($fileSize, 2)) MB" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Opening video..." -ForegroundColor Yellow
    Start-Process $OUTPUT_FILE
} else {
    Write-Host "[ERROR] Output file not found!" -ForegroundColor Red
    exit 1
}
