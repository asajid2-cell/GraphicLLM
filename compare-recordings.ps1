# Compare two test recordings side-by-side
# Useful for verifying bug fixes by comparing before/after videos

param(
    [string]$Video1,
    [string]$Video2,
    [string]$OutputFile = "comparison.mp4"
)

# Check if FFmpeg is installed
try {
    $null = ffmpeg -version 2>$null
} catch {
    Write-Host "[ERROR] FFmpeg not found!" -ForegroundColor Red
    Write-Host "Install with: winget install ffmpeg" -ForegroundColor Yellow
    exit 1
}

# If no videos specified, show available recordings
if (-not $Video1 -or -not $Video2) {
    Write-Host "Available recordings:" -ForegroundColor Cyan
    Write-Host ""

    $recordings = Get-ChildItem -Path "test_recordings" -Filter "*.mp4" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending

    if ($recordings.Count -eq 0) {
        Write-Host "No recordings found in test_recordings folder" -ForegroundColor Yellow
        exit 1
    }

    for ($i = 0; $i -lt $recordings.Count; $i++) {
        $date = $recordings[$i].LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
        $size = [math]::Round($recordings[$i].Length / 1MB, 2)
        Write-Host "[$i] $($recordings[$i].Name) ($date, $size MB)" -ForegroundColor White
    }

    Write-Host ""
    Write-Host "Usage:" -ForegroundColor Yellow
    Write-Host '  .\compare-recordings.ps1 -Video1 "test_recordings\video1.mp4" -Video2 "test_recordings\video2.mp4"' -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or select by index:" -ForegroundColor Yellow
    Write-Host "  Enter first video index:" -ForegroundColor Gray
    $idx1 = Read-Host
    Write-Host "  Enter second video index:" -ForegroundColor Gray
    $idx2 = Read-Host

    if ($idx1 -match '^\d+$' -and $idx2 -match '^\d+$') {
        $Video1 = $recordings[[int]$idx1].FullName
        $Video2 = $recordings[[int]$idx2].FullName
    } else {
        Write-Host "[ERROR] Invalid selection" -ForegroundColor Red
        exit 1
    }
}

# Verify files exist
if (-not (Test-Path $Video1)) {
    Write-Host "[ERROR] Video 1 not found: $Video1" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Video2)) {
    Write-Host "[ERROR] Video 2 not found: $Video2" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Creating Side-by-Side Comparison" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Video 1: $Video1" -ForegroundColor White
Write-Host "Video 2: $Video2" -ForegroundColor White
Write-Host "Output:  test_recordings\$OutputFile" -ForegroundColor White
Write-Host ""

$outputPath = Join-Path "test_recordings" $OutputFile

# Create side-by-side comparison using FFmpeg
Write-Host "Creating comparison video..." -ForegroundColor Yellow
Write-Host "(This may take a minute...)" -ForegroundColor Gray

$ffmpegArgs = @(
    "-i", $Video1,
    "-i", $Video2,
    "-filter_complex", "[0:v]scale=iw/2:ih/2[v0];[1:v]scale=iw/2:ih/2[v1];[v0][v1]hstack[out]",
    "-map", "[out]",
    "-c:v", "libx264",
    "-preset", "medium",
    "-crf", "23",
    "-y",
    $outputPath
)

try {
    $process = Start-Process -FilePath "ffmpeg" -ArgumentList $ffmpegArgs -NoNewWindow -Wait -PassThru

    if ($process.ExitCode -eq 0) {
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Green
        Write-Host "  Comparison video created!" -ForegroundColor Green
        Write-Host "========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Output: $outputPath" -ForegroundColor Cyan

        $fileSize = (Get-Item $outputPath).Length / 1MB
        Write-Host "Size: $([math]::Round($fileSize, 2)) MB" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Opening video..." -ForegroundColor Yellow

        Start-Process $outputPath
    } else {
        Write-Host "[ERROR] FFmpeg failed with exit code: $($process.ExitCode)" -ForegroundColor Red
    }
} catch {
    Write-Host "[ERROR] Failed to create comparison: $_" -ForegroundColor Red
    exit 1
}
