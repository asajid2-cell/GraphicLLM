# Headless render-and-capture harness for the autonomous scene-creator loop.
# Boots the engine with blocking dialogs suppressed (so it runs without an
# interactive desktop), renders a scene or recipe, captures the validation
# frame, and converts it to a PNG that can be viewed/analyzed.
#
#   tools\render_scene.ps1 -Scene beach
#   tools\render_scene.ps1 -Recipe living_room
#   tools\render_scene.ps1 -Prompt "build a kitchen"
#
# Prints the absolute PNG path on success (last line).
param(
    [string]$Scene = "",
    [string]$Recipe = "",
    [string]$Prompt = "",
    [string]$OutName = "",
    [int]$Frames = 200,
    [int]$TimeoutSec = 170
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot           # CortexEngine/
$bin  = Join-Path $root "build\bin"
$exe  = Join-Path $bin "CortexEngine.exe"
$logs = Join-Path $bin "logs"
$bmp  = Join-Path $logs "visual_validation_rt_showcase.bmp"
$vcpkgBin = Join-Path $root "build\vcpkg_installed\x64-windows\bin"

if (-not (Test-Path $exe)) { Write-Error "engine not built: $exe"; exit 2 }
if (Test-Path $bmp) { Remove-Item $bmp -Force }

# Env: suppress dialogs (the headless unlock) + capture + exit-after-capture.
$env:PATH = "$vcpkgBin;$env:PATH"
$env:CORTEX_SUPPRESS_CAMERA_HELP = "1"
$env:CORTEX_SUPPRESS_FATAL_DIALOG = "1"
$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_EXIT_AFTER_VISUAL_VALIDATION = "1"
$env:CORTEX_SMOKE_FRAMES = "$Frames"
if ($Recipe) { $env:CORTEX_SCENE_RECIPE = $Recipe } else { Remove-Item Env:CORTEX_SCENE_RECIPE -ErrorAction SilentlyContinue }
if ($Prompt) { $env:CORTEX_SCENE_PROMPT = $Prompt } else { Remove-Item Env:CORTEX_SCENE_PROMPT -ErrorAction SilentlyContinue }

# --scene is omitted when a free-text -Prompt is given, so the engine's
# prompt router (CORTEX_SCENE_PROMPT) selects the scene.
$cliArgs = @("--no-llm", "--no-launcher")
if ($Scene)  { $cliArgs += "--scene=$Scene" }

Push-Location $bin
try {
    $p = Start-Process -FilePath $exe -ArgumentList $cliArgs -NoNewWindow -PassThru -RedirectStandardOutput "render_harness.out" -RedirectStandardError "render_harness.err"
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        $p.Kill(); Write-Error "engine timed out after ${TimeoutSec}s"; exit 3
    }
} finally { Pop-Location }

if (-not (Test-Path $bmp)) { Write-Error "no capture produced (see $bin\render_harness.out/.err)"; exit 4 }

if (-not $OutName) {
    $tag = if ($Prompt) { ($Prompt -replace '[^a-zA-Z0-9]+','_').Trim('_') } elseif ($Recipe) { $Recipe } elseif ($Scene) { $Scene } else { "scene" }
    $OutName = "render_$tag"
}
$png = Join-Path $logs "$OutName.png"
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile($bmp)
# Save at NATIVE resolution (judging crispness from a half-scale thumbnail hides
# pixelation / aliasing). Only downscale if the frame is very large (>1600 wide).
if ($img.Width -gt 1600) {
    $sc = 1600.0 / $img.Width
    $resized = New-Object System.Drawing.Bitmap($img, [int]($img.Width*$sc), [int]($img.Height*$sc))
    $resized.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $resized.Dispose()
} else {
    $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
}
$img.Dispose()
Write-Output $png
