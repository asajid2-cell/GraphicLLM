param(
    [string]$PalettePath = "",
    [string]$SeedRoot = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($PalettePath)) {
    $PalettePath = Join-Path $root "assets/config/asset_led_world_palettes.json"
}
if ([string]::IsNullOrWhiteSpace($SeedRoot)) {
    $SeedRoot = Join-Path $root "assets/scenes/hand_authored"
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) | Out-Null }

if (-not (Test-Path $PalettePath)) { throw "World palette manifest missing: $PalettePath" }
$palettes = Get-Content $PalettePath -Raw | ConvertFrom-Json
if ([int]$palettes.schema -ne 1) { Add-Failure "world palette schema must be 1" }

$coveredModes = @{}
$paletteById = @{}
foreach ($palette in @($palettes.palettes)) {
    $id = [string]$palette.id
    if ([string]::IsNullOrWhiteSpace($id)) { Add-Failure "world palette id is missing"; continue }
    $paletteById[$id] = $palette
    foreach ($mode in @($palette.shader_modes)) { $coveredModes[[string]$mode] = $true }
    foreach ($field in @("key_color", "fill_color", "rim_color", "fog_tint")) {
        if (@($palette.$field).Count -ne 3) { Add-Failure "palette '$id' $field must be RGB vector" }
    }
    if ([double]$palette.exposure -le 0.0) { Add-Failure "palette '$id' exposure must be positive" }
    if ([double]$palette.bloom_ceiling -le 0.0) { Add-Failure "palette '$id' bloom_ceiling must be positive" }
    if ([double]$palette.warm_cool_contrast -le 0.0) { Add-Failure "palette '$id' warm_cool_contrast must be positive" }
}

foreach ($required in @($palettes.required_shader_modes)) {
    if (-not $coveredModes.ContainsKey([string]$required)) {
        Add-Failure "required world shader mode '$required' is not covered by any palette"
    }
}

$seedFiles = @(Get-ChildItem -Path $SeedRoot -Filter scene_seed.json -Recurse | Where-Object { $_.FullName -notmatch "\\schema\\" })
foreach ($file in $seedFiles) {
    $seed = Get-Content $file.FullName -Raw | ConvertFrom-Json
    $paletteId = [string]$seed.lighting.world_shader_palette
    if (-not $paletteById.ContainsKey($paletteId)) {
        Add-Failure "scene '$($seed.id)' references missing world shader palette '$paletteId'"
        continue
    }
    $palette = $paletteById[$paletteId]
    if ([string]$palette.scene -ne [string]$seed.id) {
        Add-Failure "palette '$paletteId' scene '$($palette.scene)' does not match seed '$($seed.id)'"
    }
    $seedModes = @{}
    foreach ($mat in @($seed.material_palette)) { $seedModes[[string]$mat.shader_mode] = $true }
    foreach ($mode in @($seedModes.Keys)) {
        if ([string]$mode -notin @($palette.shader_modes)) {
            Add-Failure "scene '$($seed.id)' material shader_mode '$mode' not listed by palette '$paletteId'"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "World shader contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host " - $failure" -ForegroundColor Red }
    exit 1
}

Write-Host "World shader contract tests passed: palettes=$(@($palettes.palettes).Count) modes=$($coveredModes.Count)" -ForegroundColor Green
