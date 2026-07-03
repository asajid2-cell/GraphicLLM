# Headless render of a scene described by a Scene-IR JSON file (the generative
# pipeline's engine entry point). The IR (room + solver-placed objects + lights) is
# built by the "generative" recipe onto a BLANK room, reusing the showcase lighting +
# hero camera, so any valid IR yields a valid, well-lit scene.
#
#   tools\render_ir.ps1 -JsonFile scene.json -OutName mygen [-Night] [-Camera "dolly,lift,yaw,fov,exp"]
#
# Prints the absolute PNG path on success (last line).
param(
    [Parameter(Mandatory=$true)][string]$JsonFile,
    [string]$OutName = "ir_render",
    [int]$Frames = 200,
    [int]$TimeoutSec = 200,
    [switch]$Night,
    [switch]$Fast,      # native-res render (no 1.5x SSAA): much lighter on the GPU/desktop
    [string]$Camera = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin  = Join-Path $root "build\bin"
$exe  = Join-Path $bin "CortexEngine.exe"
$logs = Join-Path $bin "logs"
$bmp  = Join-Path $logs "visual_validation_rt_showcase.bmp"
$vcpkgBin = Join-Path $root "build\vcpkg_installed\x64-windows\bin"
if (-not (Test-Path $exe)) { Write-Error "engine not built: $exe"; exit 2 }
if (-not (Test-Path $JsonFile)) { Write-Error "no json: $JsonFile"; exit 2 }
if (Test-Path $bmp) { Remove-Item $bmp -Force }

$json = (Get-Content -Raw -Path $JsonFile)
$env:PATH = "$vcpkgBin;$env:PATH"
$env:CORTEX_SUPPRESS_CAMERA_HELP = "1"
$env:CORTEX_SUPPRESS_FATAL_DIALOG = "1"
$env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
$env:CORTEX_EXIT_AFTER_VISUAL_VALIDATION = "1"
$env:CORTEX_SMOKE_FRAMES = "$Frames"
$env:CORTEX_HEADLESS = "1"
$env:CORTEX_SHOWCASE = "1"               # showcase lighting (sun/window/fog) + hero camera + 1.5x SSAA
if ($Night) { $env:CORTEX_SHOWCASE_NIGHT = "1" } else { Remove-Item Env:CORTEX_SHOWCASE_NIGHT -ErrorAction SilentlyContinue }
if ($Fast) { $env:CORTEX_RENDER_SCALE = "1.0" } else { Remove-Item Env:CORTEX_RENDER_SCALE -ErrorAction SilentlyContinue }
$env:CORTEX_SCENE_IR_JSON = $json        # the generative scene
Remove-Item Env:CORTEX_SCENE_PROMPT   -ErrorAction SilentlyContinue
Remove-Item Env:CORTEX_SCENE_RECIPE   -ErrorAction SilentlyContinue
Remove-Item Env:CORTEX_AUTOCAM_DOLLY,Env:CORTEX_AUTOCAM_LIFT,Env:CORTEX_AUTOCAM_YAW,Env:CORTEX_AUTOCAM_FOV_ADD,Env:CORTEX_AUTOEXPOSURE_MULT -ErrorAction SilentlyContinue
if ($Camera) {
    $p = $Camera.Split(",")
    if ($p.Count -ge 1 -and $p[0]) { $env:CORTEX_AUTOCAM_DOLLY     = $p[0] }
    if ($p.Count -ge 2 -and $p[1]) { $env:CORTEX_AUTOCAM_LIFT      = $p[1] }
    if ($p.Count -ge 3 -and $p[2]) { $env:CORTEX_AUTOCAM_YAW       = $p[2] }
    if ($p.Count -ge 4 -and $p[3]) { $env:CORTEX_AUTOCAM_FOV_ADD   = $p[3] }
    if ($p.Count -ge 5 -and $p[4]) { $env:CORTEX_AUTOEXPOSURE_MULT = $p[4] }
}

Push-Location $bin
try {
    $p = Start-Process -FilePath $exe -ArgumentList @("--no-llm","--no-launcher") -NoNewWindow -PassThru -RedirectStandardOutput "ir_harness.out" -RedirectStandardError "ir_harness.err"
    # keep the desktop responsive: asset/texture decode is CPU-heavy and the render
    # saturates the GPU -- run the engine below normal priority
    try { $p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch {}
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Write-Error "engine timed out after ${TimeoutSec}s"; exit 3 }
} finally { Pop-Location }
if (-not (Test-Path $bmp)) { Write-Error "no capture produced (see $bin\ir_harness.out/.err)"; exit 4 }

$png = Join-Path $logs "$OutName.png"
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile($bmp)
if ($img.Width -gt 1600) {
    $sc = 1600.0 / $img.Width
    $resized = New-Object System.Drawing.Bitmap($img, [int]($img.Width*$sc), [int]($img.Height*$sc))
    $resized.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $resized.Dispose()
} else { $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) }
$img.Dispose()
Write-Output $png
