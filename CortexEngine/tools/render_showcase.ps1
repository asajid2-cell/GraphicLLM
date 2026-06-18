# Render every scene type the text-to-scene creator supports into one labeled
# montage (a contact sheet of the whole tool). Reuses render_scene.ps1 per scene.
#
#   pwsh -File tools/render_showcase.ps1 [-OutName showcase] [-Style ""]
#
# -Style prepends a style word to every prompt (e.g. "modern", "rustic", "bright")
# to render the same set in a different mood. The montage is written to
# build/bin/logs/<OutName>.png.
param(
    [string]$OutName = "showcase",
    [string]$Style = ""
)
$ErrorActionPreference = "Stop"
$engine = Split-Path $PSScriptRoot -Parent
$render = Join-Path $PSScriptRoot "render_scene.ps1"
$logs = Join-Path $engine "build\bin\logs"

$prefix = if ($Style) { "$Style " } else { "" }
$scenes = @(
    @{ tag = "living";   prompt = "a ${prefix}cozy living room";       label = "living room" },
    @{ tag = "kitchen";  prompt = "a ${prefix}modern kitchen";         label = "kitchen" },
    @{ tag = "bedroom";  prompt = "a ${prefix}small bedroom";          label = "bedroom" },
    @{ tag = "office";   prompt = "a ${prefix}home office with a desk"; label = "office" },
    @{ tag = "dining";   prompt = "a ${prefix}dining room";            label = "dining room" },
    @{ tag = "bathroom"; prompt = "a ${prefix}bathroom";               label = "bathroom" },
    @{ tag = "garden";   prompt = "a ${prefix}garden with a patio";    label = "garden" }
)

$tag = "show_" + $OutName
foreach ($s in $scenes) {
    Write-Host "Rendering $($s.label)..."
    & $render -Prompt $s.prompt -OutName ($tag + "_" + $s.tag) | Out-Null
}
Write-Host "Rendering beach (sunset)..."
& $render -Scene beach -OutName ($tag + "_beach") | Out-Null
$scenes += @{ tag = "beach"; label = "beach (sunset)" }

Add-Type -AssemblyName System.Drawing
$imgs = @(); $labels = @()
foreach ($s in $scenes) {
    $p = Join-Path $logs ($tag + "_" + $s.tag + ".png")
    if (Test-Path $p) { $imgs += [System.Drawing.Image]::FromFile($p); $labels += $s.label }
}
if ($imgs.Count -eq 0) { throw "No scene PNGs were produced." }
$cw = 400; $ch = 225; $cols = 2; $rows = [math]::Ceiling($imgs.Count / $cols)
$mont = New-Object System.Drawing.Bitmap (($cw * $cols), ($ch * $rows))
$g = [System.Drawing.Graphics]::FromImage($mont)
$g.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font("Arial", 15, [System.Drawing.FontStyle]::Bold)
for ($i = 0; $i -lt $imgs.Count; $i++) {
    $x = ($i % $cols) * $cw; $y = [math]::Floor($i / $cols) * $ch
    $g.DrawImage($imgs[$i], $x, $y, $cw, $ch)
    $g.DrawString($labels[$i], $font, [System.Drawing.Brushes]::Yellow, ($x + 6), ($y + 6))
    $imgs[$i].Dispose()
}
$out = Join-Path $logs ("$OutName.png")
$mont.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $mont.Dispose()
Write-Host "Saved $out"
