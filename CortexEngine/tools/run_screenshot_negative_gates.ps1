param(
    [switch]$NoBuild,
    [switch]$RuntimeSmoke,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "screenshot_negative_gates_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function New-Bmp24([string]$Path, [int]$Width, [int]$Height, [scriptblock]$Pixel) {
    $bytesPerPixel = 3
    $rowStride = [int]([Math]::Floor(((($Width * $bytesPerPixel) + 3) / 4.0))) * 4
    $pixelBytes = $rowStride * $Height
    $fileSize = 54 + $pixelBytes
    $bytes = New-Object byte[] $fileSize
    $bytes[0] = 0x42
    $bytes[1] = 0x4d
    [BitConverter]::GetBytes([uint32]$fileSize).CopyTo($bytes, 2)
    [BitConverter]::GetBytes([uint32]54).CopyTo($bytes, 10)
    [BitConverter]::GetBytes([uint32]40).CopyTo($bytes, 14)
    [BitConverter]::GetBytes([int32]$Width).CopyTo($bytes, 18)
    [BitConverter]::GetBytes([int32]$Height).CopyTo($bytes, 22)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 26)
    [BitConverter]::GetBytes([uint16]24).CopyTo($bytes, 28)
    [BitConverter]::GetBytes([uint32]$pixelBytes).CopyTo($bytes, 34)

    for ($y = 0; $y -lt $Height; ++$y) {
        $row = 54 + ($y * $rowStride)
        for ($x = 0; $x -lt $Width; ++$x) {
            $rgb = & $Pixel $x $y
            $p = $row + ($x * 3)
            $bytes[$p + 0] = [byte]$rgb[2]
            $bytes[$p + 1] = [byte]$rgb[1]
            $bytes[$p + 2] = [byte]$rgb[0]
        }
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Measure-BmpStats([string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    $dataOffset = [BitConverter]::ToUInt32($bytes, 10)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $height = [Math]::Abs([BitConverter]::ToInt32($bytes, 22))
    $bpp = [BitConverter]::ToUInt16($bytes, 28)
    $bytesPerPixel = [int]($bpp / 8)
    $rowStride = [int]([Math]::Floor(((($width * $bytesPerPixel) + 3) / 4.0))) * 4
    $pixelCount = [int64]$width * [int64]$height
    $sumLuma = 0.0
    $nonblack = 0
    $nearWhite = 0
    $saturated = 0
    $edge = 0

    for ($y = 0; $y -lt $height; ++$y) {
        $row = [int]$dataOffset + ($y * $rowStride)
        for ($x = 0; $x -lt $width; ++$x) {
            $p = $row + ($x * $bytesPerPixel)
            $b = [double]$bytes[$p]
            $g = [double]$bytes[$p + 1]
            $r = [double]$bytes[$p + 2]
            $luma = (0.2126 * $r) + (0.7152 * $g) + (0.0722 * $b)
            $sumLuma += $luma
            if ($luma -gt 2.0) { ++$nonblack }
            if ($r -gt 245 -and $g -gt 245 -and $b -gt 245) { ++$nearWhite }
            if (($r -gt 250 -and ($g -lt 20 -or $b -lt 20)) -or
                ($g -gt 250 -and ($r -lt 20 -or $b -lt 20)) -or
                ($b -gt 250 -and ($r -lt 20 -or $g -lt 20))) { ++$saturated }
            if ($x -gt 0) {
                $q = $p - $bytesPerPixel
                $prev = (0.2126 * [double]$bytes[$q + 2]) + (0.7152 * [double]$bytes[$q + 1]) + (0.0722 * [double]$bytes[$q])
                if ([Math]::Abs($luma - $prev) -gt 24.0) { ++$edge }
            }
        }
    }

    return [pscustomobject]@{
        avg_luma = $sumLuma / [double]$pixelCount
        nonblack_ratio = [double]$nonblack / [double]$pixelCount
        near_white_ratio = [double]$nearWhite / [double]$pixelCount
        saturated_ratio = [double]$saturated / [double]$pixelCount
        edge_ratio = [double]$edge / [double]$pixelCount
    }
}

$blackPath = Join-Path $LogDir "negative_black.bmp"
$whitePath = Join-Path $LogDir "negative_white.bmp"
$redPath = Join-Path $LogDir "negative_saturated_red.bmp"
$edgePath = Join-Path $LogDir "negative_edge_stripes.bmp"

New-Bmp24 $blackPath 64 64 { param($x, $y) @(0, 0, 0) }
New-Bmp24 $whitePath 64 64 { param($x, $y) @(255, 255, 255) }
New-Bmp24 $redPath 64 64 { param($x, $y) @(255, 0, 0) }
New-Bmp24 $edgePath 64 64 { param($x, $y) if (($x % 2) -eq 0) { @(0, 0, 0) } else { @(255, 255, 255) } }

$black = Measure-BmpStats $blackPath
$white = Measure-BmpStats $whitePath
$red = Measure-BmpStats $redPath
$edge = Measure-BmpStats $edgePath

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) }

if ($black.nonblack_ratio -gt 0.01 -or $black.avg_luma -gt 2.0) {
    Add-Failure "black negative was not detected as black enough"
}
if ($white.near_white_ratio -lt 0.95 -or $white.avg_luma -lt 250.0) {
    Add-Failure "white negative was not detected as near-white enough"
}
if ($red.saturated_ratio -lt 0.95) {
    Add-Failure "saturated red negative was not detected as saturated enough"
}
if ($edge.edge_ratio -lt 0.45) {
    Add-Failure "edge stripe negative was not detected as edge-heavy enough"
}

if ($RuntimeSmoke) {
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_visual_baseline_contract_tests.ps1"),
        "-RuntimeSmoke",
        "-MaxRuntimeCases", "1",
        "-LogDir", (Join-Path $LogDir "runtime_visual_baseline")
    )
    if ($NoBuild) {
        $args += "-NoBuild"
    }
    & powershell @args
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "runtime visual baseline sample failed"
    }
}

@{
    black = $black
    white = $white
    saturated_red = $red
    edge_stripes = $edge
} | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $LogDir "screenshot_negative_summary.json")

if ($failures.Count -gt 0) {
    Write-Host "Screenshot negative gates failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Screenshot negative gates passed logs=$LogDir" -ForegroundColor Green
