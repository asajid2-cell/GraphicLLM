# Test Flickering with HZB DISABLED - Video Recording
# This will help isolate if HZB temporal desynchronization is the remaining cause

param(
    [int]$Duration = 22
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$OUTPUT_DIR = Join-Path $SCRIPT_DIR "fix_verification"
$TIMESTAMP = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$OUTPUT_FILE = Join-Path $OUTPUT_DIR "flickering_NO_HZB_$TIMESTAMP.mp4"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Flickering Test: HZB DISABLED" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "This test records video with HZB occlusion culling" -ForegroundColor Yellow
Write-Host "DISABLED to check if HZB is the flickering cause." -ForegroundColor Yellow
Write-Host ""

New-Item -ItemType Directory -Force -Path $OUTPUT_DIR | Out-Null

# Get screen resolution
Add-Type -AssemblyName System.Windows.Forms
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$width = $screen.Width
$height = $screen.Height

Write-Host "[1/4] Starting screen recording..." -ForegroundColor Yellow
Write-Host "Recording: ${width}x${height} @ 60fps" -ForegroundColor Cyan

# Start FFmpeg recording
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

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "ffmpeg"
$psi.Arguments = ($ffmpegArgs -join " ")
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.CreateNoWindow = $false

$ffmpegProcess = New-Object System.Diagnostics.Process
$ffmpegProcess.StartInfo = $psi
$ffmpegProcess.Start() | Out-Null

Start-Sleep -Seconds 2

Write-Host "[OK] Recording started" -ForegroundColor Green

# Launch engine
Write-Host ""
Write-Host "[2/4] Launching CortexEngine with HZB DISABLED..." -ForegroundColor Yellow

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
    Stop-Process -Id $ffmpegProcess.Id -Force
    exit 1
}

$exeDir = Split-Path $exe -Parent
Push-Location $exeDir

# Set environment variables - DISABLE HZB
$env:CORTEX_NO_HELP = "1"
$env:CORTEX_AUTO_CAMERA = "1"
$env:CORTEX_DISABLE_GPUCULL_HZB = "1"  # <-- KEY: Disable HZB

Write-Host "Environment:" -ForegroundColor Gray
Write-Host "  CORTEX_NO_HELP=1" -ForegroundColor Gray
Write-Host "  CORTEX_AUTO_CAMERA=1" -ForegroundColor Gray
Write-Host "  CORTEX_DISABLE_GPUCULL_HZB=1 (HZB DISABLED)" -ForegroundColor Red

$engineProcess = Start-Process -FilePath $exe -ArgumentList "--scene","engine_editor" -PassThru -WindowStyle Normal

Pop-Location

Start-Sleep -Seconds 8
Write-Host "[OK] Engine started" -ForegroundColor Green

# Automated camera movement
Write-Host ""
Write-Host "[3/4] Recording with HZB disabled..." -ForegroundColor Yellow
$movementDuration = [math]::Min(15, $Duration - 5)
Start-Sleep -Seconds $movementDuration
Write-Host "[OK] Movement recorded" -ForegroundColor Green

$remaining = $Duration - $movementDuration
if ($remaining -gt 0) {
    Start-Sleep -Seconds $remaining
}

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

# Validate video
Write-Host ""
if (Test-Path $OUTPUT_FILE) {
    $fileSize = (Get-Item $OUTPUT_FILE).Length / 1MB
    $ffprobeResult = & ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 $OUTPUT_FILE 2>&1

    if ($LASTEXITCODE -eq 0 -and $ffprobeResult) {
        $duration = [math]::Round([double]$ffprobeResult, 1)
        Write-Host "[OK] Video is valid (duration: ${duration}s)" -ForegroundColor Green

        # Analyze video
        Write-Host ""
        Write-Host "[4/4] Analyzing video for flickering..." -ForegroundColor Yellow

        $psi2 = New-Object System.Diagnostics.ProcessStartInfo
        $psi2.FileName = "ffmpeg"
        $psi2.Arguments = "-i `"$OUTPUT_FILE`" -vf `"select='gt(scene\,0.4)',showinfo`" -vsync vfr -f null -"
        $psi2.UseShellExecute = $false
        $psi2.RedirectStandardError = $true
        $psi2.RedirectStandardOutput = $true
        $psi2.CreateNoWindow = $true

        $process2 = New-Object System.Diagnostics.Process
        $process2.StartInfo = $psi2
        $process2.Start() | Out-Null

        $stderr2 = $process2.StandardError.ReadToEnd()
        $process2.WaitForExit()

        $sceneChanges = @()
        $matches = [regex]::Matches($stderr2, "pts_time:([\d.]+)")
        foreach ($match in $matches) {
            $sceneChanges += [double]$match.Groups[1].Value
        }

        $totalFrames = [int]($duration * 60)
        $sceneChangeCount = $sceneChanges.Count
        $flickerThreshold = [int]($totalFrames * 0.05)

        Write-Host ""
        Write-Host "==================================================" -ForegroundColor Cyan
        Write-Host "  Test Complete: HZB DISABLED" -ForegroundColor Cyan
        Write-Host "==================================================" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Video: $OUTPUT_FILE" -ForegroundColor White
        Write-Host "Size: $([math]::Round($fileSize, 2)) MB" -ForegroundColor White
        Write-Host ""
        Write-Host "Analysis Results:" -ForegroundColor Yellow
        Write-Host "  Total frames: ~$totalFrames" -ForegroundColor White
        Write-Host "  Scene changes: $sceneChangeCount (threshold: $flickerThreshold)" -ForegroundColor White
        Write-Host ""

        if ($sceneChangeCount -gt $flickerThreshold) {
            Write-Host "[WARNING] Flickering still detected with HZB disabled!" -ForegroundColor Red
            Write-Host "  This means HZB is NOT the primary cause." -ForegroundColor Yellow
            Write-Host "  Need deeper investigation of remaining issues." -ForegroundColor Yellow
        } else {
            Write-Host "[INFO] Low scene change count with HZB disabled" -ForegroundColor Green
            Write-Host "  Compare with HZB-enabled video to see difference" -ForegroundColor Yellow
        }

        Write-Host ""
        Write-Host "Opening video for manual review..." -ForegroundColor Gray
        Start-Process $OUTPUT_FILE
    } else {
        Write-Host "[ERROR] Video file is CORRUPT!" -ForegroundColor Red
    }
} else {
    Write-Host "[ERROR] Output file not found!" -ForegroundColor Red
}
