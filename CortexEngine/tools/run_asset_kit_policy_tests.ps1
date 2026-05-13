param(
    [string]$ManifestPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root "assets/models/naturalistic_showcase/asset_manifest.json"
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) | Out-Null }

function Has-Property([object]$Object, [string]$Name) {
    return $null -ne $Object -and ($Object.PSObject.Properties.Name -contains $Name)
}

function Require-Property([object]$Object, [string]$Name, [string]$Context) {
    if (-not (Has-Property $Object $Name) -or $null -eq $Object.$Name) {
        Add-Failure "$Context missing required property '$Name'"
        return $false
    }
    return $true
}

function Require-Vector3([object]$Value, [string]$Context) {
    $items = @($Value)
    if ($items.Count -ne 3) {
        Add-Failure "$Context must have exactly 3 numeric values"
        return
    }
    foreach ($item in $items) {
        try { [void][double]$item } catch { Add-Failure "$Context contains non-numeric value '$item'" }
    }
}

if (-not (Test-Path $ManifestPath)) { throw "Asset manifest missing: $ManifestPath" }
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$assetRoot = Split-Path -Parent $ManifestPath
$engineScenesPath = Join-Path $root "src/Core/Engine_Scenes.cpp"
$engineScenesSource = ""
if (Test-Path $engineScenesPath) {
    $engineScenesSource = Get-Content $engineScenesPath -Raw
} else {
    Add-Failure "Engine_Scenes.cpp missing: $engineScenesPath"
}

if ([int]$manifest.schema -ne 1) { Add-Failure "asset manifest schema must be 1" }
if (-not [bool]$manifest.policy.orientation_contract_required) { Add-Failure "policy.orientation_contract_required must be true" }
if (-not [bool]$manifest.policy.contact_policy_required) { Add-Failure "policy.contact_policy_required must be true" }
if (-not [bool]$manifest.policy.pbr_texture_status_required) { Add-Failure "policy.pbr_texture_status_required must be true" }
if ($manifest.policy.notes -match "future PBR material hookup|geometry only") {
    Add-Failure "policy.notes still describe naturalistic assets as geometry-only/future PBR hookup"
}

foreach ($bindingToken in @("GetNaturalisticAssetTextureSet", "ApplyNaturalisticAssetTextures", "AddAssetLedNaturalisticRenderable")) {
    if ($engineScenesSource -notmatch [regex]::Escape($bindingToken)) {
        Add-Failure "Engine_Scenes.cpp missing runtime naturalistic texture binding token '$bindingToken'"
    }
}

$allowedUp = @($manifest.policy.allowed_up_axes)
$allowedForward = @($manifest.policy.allowed_forward_axes)
$allowedPivot = @($manifest.policy.allowed_pivot_policies)
$ids = @{}

foreach ($asset in @($manifest.assets)) {
    $id = [string]$asset.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "asset id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) { Add-Failure "duplicate asset id '$id'" }
    $ids[$id] = $true
    $ctx = "asset '$id'"

    foreach ($prop in @("license", "source_url", "runtime_gltf", "orientation", "scale_to_meters", "pivot_policy", "floor_y", "bounds_meters", "thumbnail_capture", "intended_scene_roles", "material_textures")) {
        [void](Require-Property $asset $prop $ctx)
    }
    if ([string]$asset.license -ne [string]$manifest.policy.license_required) {
        Add-Failure "$ctx license must match manifest policy"
    }
    if ([double]$asset.scale_to_meters -le 0.0) {
        Add-Failure "$ctx scale_to_meters must be positive"
    }
    if ([string]$asset.orientation.up_axis -notin $allowedUp) {
        Add-Failure "$ctx orientation.up_axis '$($asset.orientation.up_axis)' is not allowed"
    }
    if ([string]$asset.orientation.forward_axis -notin $allowedForward) {
        Add-Failure "$ctx orientation.forward_axis '$($asset.orientation.forward_axis)' is not allowed"
    }
    if ([string]$asset.pivot_policy -notin $allowedPivot) {
        Add-Failure "$ctx pivot_policy '$($asset.pivot_policy)' is not allowed"
    }
    Require-Vector3 $asset.orientation.correction_euler_degrees "$ctx orientation.correction_euler_degrees"
    Require-Vector3 $asset.bounds_meters.min "$ctx bounds_meters.min"
    Require-Vector3 $asset.bounds_meters.max "$ctx bounds_meters.max"
    if (@($asset.intended_scene_roles).Count -lt 1) {
        Add-Failure "$ctx intended_scene_roles must not be empty"
    }

    $gltfPath = Join-Path $assetRoot ([string]$asset.runtime_gltf -replace "/", "\")
    if (-not (Test-Path $gltfPath)) {
        Add-Failure "$ctx runtime glTF missing: $gltfPath"
        continue
    }
    $assetDir = Split-Path -Parent $gltfPath

    foreach ($texProp in @("base_color", "normal", "roughness_metallic_ao")) {
        if (-not (Require-Property $asset.material_textures $texProp "$ctx material_textures")) { continue }
        $texRel = [string]$asset.material_textures.$texProp
        $texPath = Join-Path $assetDir ($texRel -replace "/", "\")
        if (-not (Test-Path $texPath)) {
            Add-Failure "$ctx material texture '$texProp' missing: $texPath"
        }

        $runtimeTexPath = "assets/models/naturalistic_showcase/$id/$texRel"
        if ($engineScenesSource -notmatch [regex]::Escape($runtimeTexPath)) {
            Add-Failure "$ctx runtime binding missing texture path '$runtimeTexPath' in Engine_Scenes.cpp"
        }
    }
    if ([string]::IsNullOrWhiteSpace([string]$asset.material_textures.status)) {
        Add-Failure "$ctx material_textures.status must be set"
    } elseif ([string]$asset.material_textures.status -ne "available_bound_runtime") {
        Add-Failure "$ctx material_textures.status must be available_bound_runtime"
    }
    if ([string]::IsNullOrWhiteSpace([string]$asset.material_textures.fallback)) {
        Add-Failure "$ctx material_textures.fallback must be set"
    }
    if ($engineScenesSource -notmatch [regex]::Escape("id == `"$id`"")) {
        Add-Failure "$ctx missing asset id runtime binding in Engine_Scenes.cpp"
    }
}

if ($ids.Count -lt 8) { Add-Failure "asset kit should include at least eight curated assets" }

if ($failures.Count -gt 0) {
    Write-Host "Asset kit policy tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host " - $failure" -ForegroundColor Red }
    exit 1
}

Write-Host "Asset kit policy tests passed: assets=$($ids.Count)" -ForegroundColor Green
