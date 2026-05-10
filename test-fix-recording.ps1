# Test Flickering Fix with Screen Recording
# Records video showing the fix works during camera movement

param(
    [int]$Duration = 22
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$OUTPUT_DIR = Join-Path $SCRIPT_DIR "fix_verification"
$TIMESTAMP = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$OUTPUT_FILE = Join-Path $OUTPUT_DIR "flickering_FIXED_$TIMESTAMP.mp4"

# Create output directory
New-Item -ItemType Directory -Force -Path $OUTPUT_DIR | Out-Null

Write-Host "==================================================" -ForegroundColor Green
Write-Host "  Flickering Fix Verification Test" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Fix Applied:" -ForegroundColor Cyan
Write-Host "  - Removed CPU-side 'visible' flag checks" -ForegroundColor White
Write-Host "  - GPU culling is now sole visibility authority" -ForegroundColor White
Write-Host "  - Fixes temporal desync during chunk loading" -ForegroundColor White
Write-Host ""

# Check FFmpeg
try {
    $null = ffmpeg -version 2>$null
    Write-Host "[OK] FFmpeg found" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] FFmpeg not found!" -ForegroundColor Red
    Write-Host "Install: winget install ffmpeg" -ForegroundColor Yellow
    exit 1
}

# Get screen resolution
Add-Type -AssemblyName System.Windows.Forms
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$width = $screen.Width
$height = $screen.Height

Write-Host ""
Write-Host "[1/4] Starting screen recording..." -ForegroundColor Yellow
Write-Host "Recording: ${width}x${height} @ 60fps" -ForegroundColor Cyan
Write-Host "Output: $OUTPUT_FILE" -ForegroundColor Cyan

# Start FFmpeg recording with stdin redirection for graceful shutdown
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

# Start FFmpeg with stdin redirection so we can send 'q' to gracefully stop
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "ffmpeg"
$psi.Arguments = ($ffmpegArgs -join " ")
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardError = $false
$psi.CreateNoWindow = $false

$ffmpegProcess = New-Object System.Diagnostics.Process
$ffmpegProcess.StartInfo = $psi
$ffmpegProcess.Start() | Out-Null

Start-Sleep -Seconds 2

if ($ffmpegProcess.HasExited) {
    Write-Host "[ERROR] FFmpeg failed to start!" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Recording started (PID: $($ffmpegProcess.Id))" -ForegroundColor Green

# Launch engine
Write-Host ""
Write-Host "[2/4] Launching CortexEngine..." -ForegroundColor Yellow

# Try multiple possible exe locations
$exePaths = @(
    (Join-Path $ENGINE_DIR "build\bin\CortexEngine.exe"),
    (Join-Path $ENGINE_DIR "build\bin\Release\CortexEngine.exe")
)

$exe = $null
foreach ($path in $exePaths) {
    if (Test-Path $path) {
        $exe = $path
        Write-Host "Found exe: $exe" -ForegroundColor Gray
        break
    }
}

if (-not $exe) {
    Write-Host "[ERROR] Engine executable not found in any location" -ForegroundColor Red
    Write-Host "Searched:" -ForegroundColor Yellow
    foreach ($path in $exePaths) {
        Write-Host "  $path" -ForegroundColor Gray
    }
    Stop-Process -Id $ffmpegProcess.Id -Force
    exit 1
}

# Launch engine directly into infinite terrain world
$exeDir = Split-Path $exe -Parent
Write-Host "Launching from: $exeDir" -ForegroundColor Gray
Write-Host "Using --scene engine_editor to load infinite terrain world directly" -ForegroundColor Gray
Write-Host "Setting CORTEX_NO_HELP=1 to skip camera help dialog" -ForegroundColor Gray
Write-Host "Setting CORTEX_AUTO_CAMERA=1 to enable automated camera movement" -ForegroundColor Gray

Push-Location $exeDir

# Set environment variables before launching
$env:CORTEX_NO_HELP = "1"
$env:CORTEX_AUTO_CAMERA = "1"

# Launch engine with environment variables inherited
$engineProcess = Start-Process -FilePath $exe -ArgumentList "--scene","engine_editor" -PassThru -WindowStyle Normal

Pop-Location

Write-Host "Waiting for engine to initialize..." -ForegroundColor Gray
Start-Sleep -Seconds 8

if ($engineProcess.HasExited) {
    Write-Host "[ERROR] Engine crashed on startup!" -ForegroundColor Red
    Write-Host "Check engine logs for details" -ForegroundColor Yellow
    Stop-Process -Id $ffmpegProcess.Id -Force
    exit 1
}

Write-Host "[OK] Engine started (PID: $($engineProcess.Id))" -ForegroundColor Green

# Automated camera movement for flickering test
Write-Host ""
Write-Host "[3/4] Testing automated camera movement (triggers chunk loading)..." -ForegroundColor Yellow
Write-Host "  - Engine will automatically move camera forward continuously" -ForegroundColor Gray
Write-Host "  - This tests dynamic chunk loading/unloading for flickering" -ForegroundColor Gray

# Let the engine run with automated movement
$movementDuration = [math]::Min(15, $Duration - 5)
Write-Host "  - Recording camera movement for $movementDuration seconds..." -ForegroundColor Gray
Start-Sleep -Seconds $movementDuration

Write-Host "[OK] Automated camera movement test complete" -ForegroundColor Green

# Continue recording for remaining time
$remaining = $Duration - $movementDuration
if ($remaining -gt 0) {
    Write-Host ""
    Write-Host "[4/4] Recording for $remaining more seconds..." -ForegroundColor Yellow
    Start-Sleep -Seconds $remaining
}

# Stop engine
Write-Host ""
Write-Host "Stopping engine..." -ForegroundColor Yellow
try {
    Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
    Write-Host "[OK] Engine stopped" -ForegroundColor Green
} catch {
    Write-Host "[WARNING] Engine may have already exited" -ForegroundColor Yellow
}

# Stop recording gracefully by sending 'q' to FFmpeg stdin
Write-Host ""
Write-Host "Stopping recording gracefully..." -ForegroundColor Yellow
Start-Sleep -Seconds 1

try {
    # Send 'q' to FFmpeg to trigger graceful shutdown and moov atom write
    $ffmpegProcess.StandardInput.WriteLine("q")
    Write-Host "  - Sent 'q' command to FFmpeg" -ForegroundColor Gray

    # Wait up to 10 seconds for FFmpeg to finish encoding and write moov atom
    Write-Host "  - Waiting for FFmpeg to finalize video file..." -ForegroundColor Gray
    $exited = $ffmpegProcess.WaitForExit(10000)

    if ($exited) {
        Write-Host "[OK] Recording stopped and finalized" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] FFmpeg taking too long, forcing stop" -ForegroundColor Yellow
        Stop-Process -Id $ffmpegProcess.Id -Force
    }
} catch {
    Write-Host "[WARNING] Error stopping FFmpeg: $_" -ForegroundColor Yellow
    Stop-Process -Id $ffmpegProcess.Id -Force -ErrorAction SilentlyContinue
}

# Verify output and validate video integrity
Write-Host ""
if (Test-Path $OUTPUT_FILE) {
    $fileSize = (Get-Item $OUTPUT_FILE).Length / 1MB

    # Validate video is not corrupt using ffprobe
    Write-Host "Validating video integrity..." -ForegroundColor Yellow
    $ffprobeResult = & ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 $OUTPUT_FILE 2>&1

    if ($LASTEXITCODE -eq 0 -and $ffprobeResult) {
        $duration = [math]::Round([double]$ffprobeResult, 1)
        Write-Host "[OK] Video is valid (duration: ${duration}s)" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] Video file is CORRUPT!" -ForegroundColor Red
        Write-Host "FFprobe error: $ffprobeResult" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "This means FFmpeg did not finalize the MP4 properly." -ForegroundColor Yellow
        Write-Host "The moov atom is missing - file cannot be played." -ForegroundColor Yellow
        exit 1
    }

    # Analyze video for flickering using frame-by-frame difference analysis
    Write-Host ""
    Write-Host "Analyzing video for flickering patterns..." -ForegroundColor Yellow
    Write-Host "  - Extracting frame-to-frame differences..." -ForegroundColor Gray

    # Use ffmpeg to calculate inter-frame differences
    # The 'mpdecimate' filter detects scene changes and duplicates
    # We'll use a custom approach: extract frame differences using select filter
    $ANALYSIS_DIR = Join-Path $OUTPUT_DIR "analysis_$TIMESTAMP"
    New-Item -ItemType Directory -Force -Path $ANALYSIS_DIR | Out-Null

    # Extract scene change scores using ffmpeg's select filter with scene detection
    $sceneFile = Join-Path $ANALYSIS_DIR "scene_changes.txt"

    # Run ffmpeg to detect scene changes (sudden frame differences)
    # The 'select' filter with 'gt(scene,0.3)' detects frames with >30% difference
    $ffmpegAnalysis = @(
        "-i", $OUTPUT_FILE,
        "-vf", "select='gt(scene\,0.4)',showinfo",
        "-vsync", "vfr",
        "-f", "null",
        "-"
    )

    Write-Host "  - Detecting sudden frame changes (flickering indicators)..." -ForegroundColor Gray

    # Capture stderr which contains showinfo output
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "ffmpeg"
    $psi.Arguments = ($ffmpegAnalysis -join " ")
    $psi.UseShellExecute = $false
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $process.Start() | Out-Null

    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    # Parse scene changes from stderr
    $sceneChanges = @()
    $matches = [regex]::Matches($stderr, "pts_time:([\d.]+)")
    foreach ($match in $matches) {
        $sceneChanges += [double]$match.Groups[1].Value
    }

    # Calculate statistics
    $totalFrames = [int]($duration * 60)  # Assuming 60 fps
    $sceneChangeCount = $sceneChanges.Count

    Write-Host "  - Total frames analyzed: ~$totalFrames" -ForegroundColor Gray
    Write-Host "  - Sudden changes detected: $sceneChangeCount" -ForegroundColor Gray

    # Determine if flickering is present
    # Flickering would show as many rapid scene changes
    # For a moving camera with chunk loading, we expect some changes but not excessive
    $flickerThreshold = [int]($totalFrames * 0.05)  # More than 5% sudden changes = flickering

    $hasFlickering = $sceneChangeCount -gt $flickerThreshold

    Write-Host ""
    if ($hasFlickering) {
        Write-Host "[WARNING] Potential flickering detected!" -ForegroundColor Red
        Write-Host "  Scene changes: $sceneChangeCount (threshold: $flickerThreshold)" -ForegroundColor Yellow
        Write-Host "  This indicates possible visibility issues during chunk loading" -ForegroundColor Yellow
        Write-Host "  Manual review of video is recommended" -ForegroundColor Yellow
        $analysisResult = "FLICKERING DETECTED"
    } else {
        Write-Host "[OK] No significant flickering detected" -ForegroundColor Green
        Write-Host "  Scene changes: $sceneChangeCount (threshold: $flickerThreshold)" -ForegroundColor Cyan
        Write-Host "  Video shows smooth rendering during chunk loading" -ForegroundColor Green
        $analysisResult = "PASS - No Flickering"
    }

    # Clean up analysis directory
    Remove-Item -Path $ANALYSIS_DIR -Recurse -Force -ErrorAction SilentlyContinue

    Write-Host ""
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host "  Fix Verification Complete!" -ForegroundColor Green
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Analysis Result: $analysisResult" -ForegroundColor $(if ($hasFlickering) { "Red" } else { "Green" })
    Write-Host ""
    Write-Host "Video: $OUTPUT_FILE" -ForegroundColor Cyan
    Write-Host "Size: $([math]::Round($fileSize, 2)) MB" -ForegroundColor Cyan
    Write-Host "Duration: ${duration}s" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Expected Result:" -ForegroundColor Yellow
    Write-Host "  - NO flickering during camera movement" -ForegroundColor White
    Write-Host "  - Smooth chunk loading/unloading" -ForegroundColor White
    Write-Host "  - Stable visibility throughout" -ForegroundColor White
    Write-Host ""
    Write-Host "Opening video for manual verification..." -ForegroundColor Gray
    Start-Process $OUTPUT_FILE
} else {
    Write-Host "[ERROR] Output file not found!" -ForegroundColor Red
    exit 1
}
