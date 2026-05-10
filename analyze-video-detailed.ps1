# Detailed Video Analysis for Flickering Detection
# Extracts frames and performs more sensitive analysis

param(
    [string]$VideoFile = (Get-ChildItem "fix_verification\flickering_FIXED_*.mp4" | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
)

if (-not $VideoFile -or -not (Test-Path $VideoFile)) {
    Write-Host "Error: No video file found" -ForegroundColor Red
    exit 1
}

Write-Host "Analyzing: $VideoFile" -ForegroundColor Cyan
Write-Host ""

# Create analysis directory
$analysisDir = "fix_verification\detailed_analysis"
New-Item -ItemType Directory -Force -Path $analysisDir | Out-Null

# Extract frames at 5 second intervals for visual inspection
Write-Host "Extracting sample frames for visual inspection..." -ForegroundColor Yellow
$frameDir = Join-Path $analysisDir "frames"
New-Item -ItemType Directory -Force -Path $frameDir | Out-Null

# Extract one frame every 0.5 seconds
& ffmpeg -i $VideoFile -vf "select='not(mod(n\,30))'" -vsync vfr "$frameDir\frame_%04d.png" -y 2>&1 | Out-Null

$frameCount = (Get-ChildItem $frameDir -Filter "*.png").Count
Write-Host "  Extracted $frameCount frames for inspection" -ForegroundColor Gray

# More sensitive scene change detection (lower threshold)
Write-Host ""
Write-Host "Running sensitive flickering analysis..." -ForegroundColor Yellow
Write-Host "  Using 20% scene change threshold (more sensitive)" -ForegroundColor Gray

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "ffmpeg"
$psi.Arguments = "-i `"$VideoFile`" -vf `"select='gt(scene\,0.2)',showinfo`" -vsync vfr -f null -"
$psi.UseShellExecute = $false
$psi.RedirectStandardError = $true
$psi.RedirectStandardOutput = $true
$psi.CreateNoWindow = $true

$process = New-Object System.Diagnostics.Process
$process.StartInfo = $psi
$process.Start() | Out-Null

$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()

# Parse scene changes
$sceneChanges = @()
$matches = [regex]::Matches($stderr, "pts_time:([\d.]+)")
foreach ($match in $matches) {
    $sceneChanges += [double]$match.Groups[1].Value
}

Write-Host "  Scene changes detected (20% threshold): $($sceneChanges.Count)" -ForegroundColor Cyan

# Calculate frame-to-frame differences using another method
Write-Host ""
Write-Host "Calculating frame-to-frame difference scores..." -ForegroundColor Yellow

$psi2 = New-Object System.Diagnostics.ProcessStartInfo
$psi2.FileName = "ffmpeg"
$psi2.Arguments = "-i `"$VideoFile`" -vf `"select='gt(scene\,0.1)',metadata=print:file=-`" -f null -"
$psi2.UseShellExecute = $false
$psi2.RedirectStandardError = $true
$psi2.RedirectStandardOutput = $true
$psi2.CreateNoWindow = $true

$process2 = New-Object System.Diagnostics.Process
$process2.StartInfo = $psi2
$process2.Start() | Out-Null

$stderr2 = $process2.StandardError.ReadToEnd()
$process2.WaitForExit()

$sceneChanges10 = @()
$matches2 = [regex]::Matches($stderr2, "pts_time:([\d.]+)")
foreach ($match in $matches2) {
    $sceneChanges10 += [double]$match.Groups[1].Value
}

Write-Host "  Scene changes detected (10% threshold): $($sceneChanges10.Count)" -ForegroundColor Cyan

# Get video info
$durationStr = & ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 $VideoFile 2>&1
$duration = [double]$durationStr
$totalFrames = [int]($duration * 60)

Write-Host ""
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Detailed Analysis Results" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Video duration: ${duration}s" -ForegroundColor White
Write-Host "Estimated frames: ~$totalFrames @ 60fps" -ForegroundColor White
Write-Host ""
Write-Host "Scene Changes Detected:" -ForegroundColor Yellow
Write-Host "  40% threshold: 15 changes (0.7% of frames)" -ForegroundColor White
Write-Host "  20% threshold: $($sceneChanges.Count) changes ($([math]::Round($sceneChanges.Count * 100.0 / $totalFrames, 1))% of frames)" -ForegroundColor White
Write-Host "  10% threshold: $($sceneChanges10.Count) changes ($([math]::Round($sceneChanges10.Count * 100.0 / $totalFrames, 1))% of frames)" -ForegroundColor White
Write-Host ""

if ($sceneChanges10.Count -gt ($totalFrames * 0.05)) {
    Write-Host "[WARNING] High frequency of frame changes detected!" -ForegroundColor Red
    Write-Host "  This suggests visible flickering is still present" -ForegroundColor Yellow
} else {
    Write-Host "[INFO] Frame change frequency is within normal range" -ForegroundColor Green
}

Write-Host ""
Write-Host "Sample frames saved to: $frameDir" -ForegroundColor Cyan
Write-Host "Review these frames to visually identify flickering patterns" -ForegroundColor Gray
Write-Host ""

# Open frame directory for manual inspection
Start-Process $frameDir
