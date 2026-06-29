# Restore untracked binary assets (furniture, textures, HDRIs) for CortexEngine.
# Reads tools/assets_manifest.json and downloads anything missing.
#
#   tools\restore_assets.ps1                 # restore everything missing (foreground)
#   tools\restore_assets.ps1 -Background     # download in the background
#   tools\restore_assets.ps1 -List           # show status only
#   tools\restore_assets.ps1 -Only sketchfab_furniture,hdris_polyhaven
#
# Sketchfab furniture needs an API token. If SKETCHFAB_TOKEN is not set and this
# is an interactive shell, you'll be prompted (the token is never stored/committed).
param(
  [switch]$Background,
  [switch]$List,
  [string[]]$Only
)
$ErrorActionPreference = "Stop"
$engineDir = Split-Path $PSScriptRoot -Parent      # CortexEngine/

$nodeArgs = @("tools/restore_assets.mjs")
if ($List) { $nodeArgs += "--list" }
if ($Only) { $nodeArgs += "--only"; $nodeArgs += $Only }

# Offer to collect the Sketchfab token if we'll need it and it's missing.
$needsSketchfab = (-not $List) -and ((-not $Only) -or ($Only -contains "sketchfab_furniture"))
if ($needsSketchfab -and -not $env:SKETCHFAB_TOKEN) {
  if ([Environment]::UserInteractive -and -not $Background) {
    $sec = Read-Host "SKETCHFAB_TOKEN (blank to skip Sketchfab furniture)" -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec)
    $tok = [Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr)
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    if ($tok) { $env:SKETCHFAB_TOKEN = $tok }
  } else {
    Write-Host "note: SKETCHFAB_TOKEN not set - Sketchfab furniture will be skipped. Set it and re-run to fetch those."
  }
}

if ($Background) {
  $job = Start-Job -ScriptBlock {
    param($dir, $a, $tok)
    Set-Location $dir
    if ($tok) { $env:SKETCHFAB_TOKEN = $tok }
    node @a
  } -ArgumentList $engineDir, $nodeArgs, $env:SKETCHFAB_TOKEN
  Write-Host "asset restore running in background as job $($job.Id). Check with: Receive-Job $($job.Id) -Keep"
} else {
  Push-Location $engineDir
  try { & node @nodeArgs } finally { Pop-Location }
}
