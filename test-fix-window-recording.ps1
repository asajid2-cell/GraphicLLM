# Test Flickering - Record Specific Window
# Records ONLY the CortexEngine window, not the entire desktop

param(
    [int]$Duration = 22
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$OUTPUT_DIR = Join-Path $SCRIPT_DIR "fix_verification"
$TIMESTAMP = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$OUTPUT_FILE = Join-Path $OUTPUT_DIR "flickering_WINDOW_$TIMESTAMP.mp4"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Flickering Test: Window-Specific Recording" -ForegroundColor Cyan
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

Write-Host "[1/5] Launching CortexEngine..." -ForegroundColor Yellow

$exeDir = Split-Path $exe -Parent
Push-Location $exeDir

# Set environment variables
$env:CORTEX_NO_HELP = "1"
$env:CORTEX_AUTO_CAMERA = "1"

Write-Host "Environment: CORTEX_NO_HELP=1, CORTEX_AUTO_CAMERA=1" -ForegroundColor Gray

$engineProcess = Start-Process -FilePath $exe -ArgumentList "--scene","engine_editor" -PassThru -WindowStyle Normal

Pop-Location

# Wait for window to appear and stabilize
Write-Host "Waiting for window to initialize..." -ForegroundColor Gray
Start-Sleep -Seconds 5

# Get window handle and position using Windows API
Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    public class Win32 {
        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int count);

        [DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }
    }
"@

# Find window by process ID
$windowHandle = [IntPtr]::Zero
$foundWindow = $false

$callback = {
    param($hwnd, $lParam)

    $processId = 0
    [Win32]::GetWindowThreadProcessId($hwnd, [ref]$processId) | Out-Null

    if ($processId -eq $engineProcess.Id) {
        $title = New-Object System.Text.StringBuilder 256
        [Win32]::GetWindowText($hwnd, $title, 256) | Out-Null

        if ($title.ToString() -ne "") {
            $script:windowHandle = $hwnd
            $script:foundWindow = $true
            return $false  # Stop enumeration
        }
    }
    return $true  # Continue enumeration
}

$callbackDelegate = [Win32+EnumWindowsProc]$callback
[Win32]::EnumWindows($callbackDelegate, [IntPtr]::Zero) | Out-Null

if (-not $foundWindow) {
    Write-Host "[ERROR] Could not find CortexEngine window" -ForegroundColor Red
    Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

# Get window position and size
$rect = New-Object Win32+RECT
[Win32]::GetWindowRect($windowHandle, [ref]$rect) | Out-Null

$windowX = $rect.Left
$windowY = $rect.Top
$windowWidth = $rect.Right - $rect.Left
$windowHeight = $rect.Bottom - $rect.Top

Write-Host "[OK] Found window at position ($windowX, $windowY) size ${windowWidth}x${windowHeight}" -ForegroundColor Green

Write-Host ""
Write-Host "[2/5] Starting window-specific recording..." -ForegroundColor Yellow
Write-Host "Recording: ${windowWidth}x${windowHeight} @ 60fps" -ForegroundColor Cyan
Write-Host "Position: offset_x=$windowX, offset_y=$windowY" -ForegroundColor Cyan

# Start FFmpeg recording with window offset
$ffmpegArgs = @(
    "-f", "gdigrab",
    "-framerate", "60",
    "-offset_x", "$windowX",
    "-offset_y", "$windowY",
    "-video_size", "${windowWidth}x${windowHeight}",
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
$psi.RedirectStandardError = $true
$psi.RedirectStandardOutput = $true
$psi.CreateNoWindow = $true

$ffmpegProcess = New-Object System.Diagnostics.Process
$ffmpegProcess.StartInfo = $psi
$ffmpegProcess.Start() | Out-Null

Start-Sleep -Seconds 2
Write-Host "[OK] Recording started" -ForegroundColor Green

# Record for specified duration
Write-Host ""
Write-Host "[3/5] Recording automated camera movement..." -ForegroundColor Yellow
$movementDuration = [math]::Min(15, $Duration - 5)
Start-Sleep -Seconds $movementDuration
Write-Host "[OK] Movement recorded" -ForegroundColor Green

$remaining = $Duration - $movementDuration
if ($remaining -gt 0) {
    Start-Sleep -Seconds $remaining
}

# Stop engine
Write-Host ""
Write-Host "[4/5] Stopping engine..." -ForegroundColor Yellow
Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue

# Stop recording gracefully
Write-Host "Stopping recording..." -ForegroundColor Yellow
Start-Sleep -Seconds 1
$ffmpegProcess.StandardInput.WriteLine("q")
$exited = $ffmpegProcess.WaitForExit(10000)
if (!$exited) {
    Stop-Process -Id $ffmpegProcess.Id -Force
}

# Validate video
Write-Host ""
Write-Host "[5/5] Validating video..." -ForegroundColor Yellow

if (Test-Path $OUTPUT_FILE) {
    $fileSize = (Get-Item $OUTPUT_FILE).Length / 1MB
    $ffprobeResult = & ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 $OUTPUT_FILE 2>&1

    if ($LASTEXITCODE -eq 0 -and $ffprobeResult) {
        $duration = [math]::Round([double]$ffprobeResult, 1)
        Write-Host "[OK] Video is valid (duration: ${duration}s, size: $([math]::Round($fileSize, 2)) MB)" -ForegroundColor Green

        # Analyze video
        Write-Host ""
        Write-Host "Analyzing for flickering..." -ForegroundColor Yellow

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
        Write-Host "  Window Recording Complete" -ForegroundColor Cyan
        Write-Host "==================================================" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Video: $OUTPUT_FILE" -ForegroundColor White
        Write-Host "Size: $([math]::Round($fileSize, 2)) MB" -ForegroundColor White
        Write-Host "Window: ${windowWidth}x${windowHeight} at ($windowX, $windowY)" -ForegroundColor White
        Write-Host ""
        Write-Host "Analysis Results:" -ForegroundColor Yellow
        Write-Host "  Total frames: ~$totalFrames" -ForegroundColor White
        Write-Host "  Scene changes: $sceneChangeCount (threshold: $flickerThreshold)" -ForegroundColor White
        Write-Host ""

        if ($sceneChangeCount -gt $flickerThreshold) {
            Write-Host "[WARNING] Possible flickering detected!" -ForegroundColor Red
            Write-Host "  Scene changes: $sceneChangeCount > threshold: $flickerThreshold" -ForegroundColor Yellow
        } else {
            Write-Host "[INFO] Low scene change count - appears stable" -ForegroundColor Green
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
