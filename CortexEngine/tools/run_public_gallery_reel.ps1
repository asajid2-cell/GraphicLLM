param(
    [string]$GalleryManifest = "",
    [string]$OutputPath = "",
    [double]$SecondsPerImage = 1.6,
    [int]$Width = 1920,
    [int]$Height = 1080
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoRoot = Resolve-Path (Join-Path $root "..")
if ([string]::IsNullOrWhiteSpace($GalleryManifest)) {
    $GalleryManifest = Join-Path $root "docs/media/gallery_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $root "docs/media/cortex_gallery_reel.mp4"
}

if (-not (Test-Path $GalleryManifest -PathType Leaf)) {
    throw "Gallery manifest missing: $GalleryManifest"
}

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue)
if ($null -eq $ffmpeg) {
    throw "ffmpeg was not found on PATH. Install ffmpeg or generate screenshots only."
}

$manifest = Get-Content $GalleryManifest -Raw | ConvertFrom-Json
$entries = @($manifest.entries)
if ($entries.Count -lt 2) {
    throw "Gallery manifest needs at least two entries for a reel."
}

$mediaDir = Split-Path -Parent (Resolve-Path $GalleryManifest)
$workDir = Join-Path $mediaDir ".reel_tmp"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null
$concatPath = Join-Path $workDir "ffmpeg_concat.txt"

$lines = New-Object System.Collections.Generic.List[string]
$sourceImages = New-Object System.Collections.Generic.List[string]
foreach ($entry in $entries) {
    $relative = ([string]$entry.image) -replace "/", "\"
    $imagePath = Join-Path $root $relative
    if (-not (Test-Path $imagePath -PathType Leaf)) {
        throw "Gallery image missing: $($entry.image)"
    }
    $resolved = (Resolve-Path $imagePath).Path
    $escaped = $resolved.Replace("'", "'\''")
    $lines.Add("file '$escaped'") | Out-Null
    $lines.Add(("duration {0:0.###}" -f $SecondsPerImage)) | Out-Null
    $sourceImages.Add([string]$entry.image) | Out-Null
}

$lastPath = (Resolve-Path (Join-Path $root (([string]$entries[-1].image) -replace "/", "\"))).Path
$lines.Add("file '$($lastPath.Replace("'", "'\''"))'") | Out-Null
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($concatPath, (($lines -join "`n") + "`n"), $utf8NoBom)

$filter = "scale=${Width}:${Height}:force_original_aspect_ratio=decrease,pad=${Width}:${Height}:(ow-iw)/2:(oh-ih)/2,fps=30,format=yuv420p"
& $ffmpeg.Source -y -hide_banner -loglevel warning -f concat -safe 0 -i $concatPath -vf $filter -movflags +faststart $OutputPath
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg failed while creating public gallery reel"
}

$commit = (& git -C $repoRoot rev-parse --short HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    $commit = "unknown"
}

$outputItem = Get-Item -LiteralPath $OutputPath
$videoManifest = [ordered]@{
    schema = 1
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    commit = $commit.Trim()
    source_gallery_manifest = "docs/media/gallery_manifest.json"
    output = "docs/media/" + (Split-Path -Leaf $OutputPath)
    width = $Width
    height = $Height
    seconds_per_image = $SecondsPerImage
    source_image_count = $sourceImages.Count
    source_images = @($sourceImages.ToArray())
    bytes = [int64]$outputItem.Length
    generator = $ffmpeg.Source
}
$videoManifestPath = Join-Path $mediaDir "video_manifest.json"
$json = ($videoManifest | ConvertTo-Json -Depth 6) -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($videoManifestPath, $json + "`n", $utf8NoBom)

Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Public gallery reel created" -ForegroundColor Green
Write-Host " video=$OutputPath"
Write-Host " manifest=$videoManifestPath"
Write-Host " images=$($sourceImages.Count) bytes=$($outputItem.Length)"
