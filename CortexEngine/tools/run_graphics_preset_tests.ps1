param(
    [string]$PresetPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($PresetPath)) {
    $PresetPath = Join-Path $root "assets/config/graphics_presets.json"
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Assert-Range([string]$Name, [double]$Value, [double]$Min, [double]$Max) {
    if ($Value -lt $Min -or $Value -gt $Max) {
        Add-Failure "$Name=$Value outside [$Min, $Max]"
    }
}

if (-not (Test-Path $PresetPath)) {
    throw "Graphics preset file not found: $PresetPath"
}

$raw = Get-Content $PresetPath -Raw
$presetDoc = $raw | ConvertFrom-Json

if ([int]$presetDoc.schema -ne 1) {
    Add-Failure "graphics preset schema must be 1"
}
if ([string]::IsNullOrWhiteSpace([string]$presetDoc.default)) {
    Add-Failure "graphics preset default id is missing"
}
if ($null -eq $presetDoc.presets -or $presetDoc.presets.Count -lt 1) {
    Add-Failure "graphics preset list is empty"
}

$ids = @{}
$defaultFound = $false
foreach ($preset in $presetDoc.presets) {
    $id = [string]$preset.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "preset id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate preset id '$id'"
    }
    $ids[$id] = $true
    if ($id -eq [string]$presetDoc.default) {
        $defaultFound = $true
    }

    Assert-Range "$id.quality.render_scale" ([double]$preset.quality.render_scale) 0.5 1.5
    Assert-Range "$id.environment.diffuse_intensity" ([double]$preset.environment.diffuse_intensity) 0.0 4.0
    Assert-Range "$id.environment.specular_intensity" ([double]$preset.environment.specular_intensity) 0.0 4.0
    Assert-Range "$id.lighting.exposure" ([double]$preset.lighting.exposure) 0.01 8.0
    Assert-Range "$id.lighting.bloom_intensity" ([double]$preset.lighting.bloom_intensity) 0.0 5.0
    Assert-Range "$id.screen_space.ssao_radius" ([double]$preset.screen_space.ssao_radius) 0.05 5.0
    Assert-Range "$id.screen_space.ssao_bias" ([double]$preset.screen_space.ssao_bias) 0.0 0.2
    Assert-Range "$id.screen_space.ssao_intensity" ([double]$preset.screen_space.ssao_intensity) 0.0 3.0
    Assert-Range "$id.atmosphere.fog_density" ([double]$preset.atmosphere.fog_density) 0.0 0.2
    Assert-Range "$id.particles.density" ([double]$preset.particles.density) 0.0 2.0
    Assert-Range "$id.cinematic_post.vignette" ([double]$preset.cinematic_post.vignette) 0.0 1.0
    Assert-Range "$id.cinematic_post.motion_blur" ([double]$preset.cinematic_post.motion_blur) 0.0 1.0
}

if (-not $defaultFound) {
    Add-Failure "default preset '$($presetDoc.default)' is not present"
}

$roundTripPath = Join-Path ([System.IO.Path]::GetTempPath()) ("cortex_graphics_presets_roundtrip_{0}.json" -f ([Guid]::NewGuid().ToString("N")))
try {
    $presetDoc | ConvertTo-Json -Depth 16 | Set-Content -Encoding UTF8 $roundTripPath
    $roundTrip = Get-Content $roundTripPath -Raw | ConvertFrom-Json
    if ([int]$roundTrip.schema -ne [int]$presetDoc.schema) {
        Add-Failure "round-trip schema changed"
    }
    if ([string]$roundTrip.default -ne [string]$presetDoc.default) {
        Add-Failure "round-trip default changed"
    }
    if ($roundTrip.presets.Count -ne $presetDoc.presets.Count) {
        Add-Failure "round-trip preset count changed"
    }
} finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $roundTripPath
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics preset tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Graphics preset tests passed: presets=$($presetDoc.presets.Count) default=$($presetDoc.default)" -ForegroundColor Green
