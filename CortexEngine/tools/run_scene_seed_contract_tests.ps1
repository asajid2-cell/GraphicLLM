param(
    [string]$SeedRoot = "",
    [string]$SchemaPath = "",
    [string]$SceneId = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($SeedRoot)) {
    $SeedRoot = Join-Path $root "assets/scenes/hand_authored"
}
if ([string]::IsNullOrWhiteSpace($SchemaPath)) {
    $SchemaPath = Join-Path $SeedRoot "schema/scene_seed.schema.json"
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

function Require-String([object]$Object, [string]$Name, [string]$Context) {
    if (-not (Require-Property $Object $Name $Context)) { return }
    if ([string]::IsNullOrWhiteSpace([string]$Object.$Name)) {
        Add-Failure "$Context property '$Name' must be a non-empty string"
    }
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

if (-not (Test-Path $SchemaPath)) {
    throw "Scene seed schema missing: $SchemaPath"
}
if (-not (Test-Path $SeedRoot)) {
    throw "Scene seed root missing: $SeedRoot"
}

$schema = Get-Content $SchemaPath -Raw | ConvertFrom-Json
if ([int]$schema.schema -ne 1) { Add-Failure "scene seed schema must be version 1" }

$seedFiles = @(Get-ChildItem -Path $SeedRoot -Filter scene_seed.json -Recurse | Where-Object { $_.FullName -notmatch "\\schema\\" })
if (-not [string]::IsNullOrWhiteSpace($SceneId)) {
    $seedFiles = @($seedFiles | Where-Object { $_.Directory.Name -eq $SceneId })
}
if ($seedFiles.Count -lt 1) {
    Add-Failure "no scene_seed.json files found under $SeedRoot"
}

$ids = @{}
foreach ($file in $seedFiles) {
    $seed = Get-Content $file.FullName -Raw | ConvertFrom-Json
    $ctx = $file.FullName

    foreach ($prop in @($schema.required_top_level)) {
        [void](Require-Property $seed ([string]$prop) $ctx)
    }
    if ([int]$seed.schema -ne 1) { Add-Failure "$ctx schema must be 1" }
    Require-String $seed "id" $ctx
    if ($ids.ContainsKey([string]$seed.id)) {
        Add-Failure "duplicate scene seed id '$($seed.id)'"
    }
    $ids[[string]$seed.id] = $true
    if ($file.Directory.Name -ne [string]$seed.id) {
        Add-Failure "$ctx id '$($seed.id)' must match directory '$($file.Directory.Name)'"
    }
    if ([string]$seed.status -notin @($schema.allowed_status)) {
        Add-Failure "$ctx has unsupported status '$($seed.status)'"
    }
    if ([string]$seed.units -ne "meters") {
        Add-Failure "$ctx units must be meters"
    }
    if ([string]$seed.coordinate_system -ne "y_up_right_handed") {
        Add-Failure "$ctx coordinate_system must be y_up_right_handed"
    }

    foreach ($prop in @($schema.required_intent)) {
        [void](Require-Property $seed.intent ([string]$prop) "$ctx intent")
    }
    if (@($seed.intent.forbidden_reads).Count -lt 3) {
        Add-Failure "$ctx intent.forbidden_reads must name at least three visual failure modes"
    }
    foreach ($layer in @("foreground", "midground", "background")) {
        Require-String $seed.intent.foreground_midground_background $layer "$ctx intent.foreground_midground_background"
    }

    foreach ($prop in @($schema.required_lighting)) {
        [void](Require-Property $seed.lighting ([string]$prop) "$ctx lighting")
    }
    if ([double]$seed.lighting.warm_cool_contrast -le 0.0) {
        Add-Failure "$ctx lighting.warm_cool_contrast must be positive"
    }
    if ([double]$seed.lighting.bloom_ceiling -le 0.0) {
        Add-Failure "$ctx lighting.bloom_ceiling must be positive"
    }

    if (@($seed.cameras).Count -lt 3) { Add-Failure "$ctx must define at least three authored cameras" }
    $cameraIds = @{}
    foreach ($camera in @($seed.cameras)) {
        $camCtx = "$ctx camera '$($camera.id)'"
        foreach ($prop in @($schema.required_camera)) {
            [void](Require-Property $camera ([string]$prop) $camCtx)
        }
        if ($cameraIds.ContainsKey([string]$camera.id)) { Add-Failure "$ctx duplicate camera id '$($camera.id)'" }
        $cameraIds[[string]$camera.id] = $true
        Require-Vector3 $camera.position "$camCtx position"
        Require-Vector3 $camera.target "$camCtx target"
        if ([double]$camera.fov -lt 25.0 -or [double]$camera.fov -gt 60.0) {
            Add-Failure "$camCtx fov must be in a composed showcase range [25,60]"
        }
    }

    if (@($seed.material_palette).Count -lt 4) { Add-Failure "$ctx must define at least four material palette entries" }
    $paletteIds = @{}
    foreach ($mat in @($seed.material_palette)) {
        Require-String $mat "id" "$ctx material"
        Require-String $mat "surface_class" "$ctx material '$($mat.id)'"
        Require-String $mat "shader_mode" "$ctx material '$($mat.id)'"
        Require-String $mat "description" "$ctx material '$($mat.id)'"
        $paletteIds[[string]$mat.id] = $true
    }

    if (@($seed.assets).Count -lt 3) { Add-Failure "$ctx must define at least three authored assets" }
    foreach ($asset in @($seed.assets)) {
        $assetCtx = "$ctx asset '$($asset.id)'"
        foreach ($prop in @($schema.required_asset)) {
            [void](Require-Property $asset ([string]$prop) $assetCtx)
        }
        foreach ($prop in @($schema.required_transform)) {
            [void](Require-Property $asset.transform ([string]$prop) "$assetCtx transform")
        }
        Require-Vector3 $asset.transform.position "$assetCtx transform.position"
        Require-Vector3 $asset.transform.rotation_degrees "$assetCtx transform.rotation_degrees"
        Require-Vector3 $asset.transform.scale "$assetCtx transform.scale"
        if ([string]$asset.contact -notin @($schema.allowed_contacts)) {
            Add-Failure "$assetCtx has unsupported contact '$($asset.contact)'"
        }
        if (($asset.contact -eq "grounded" -or $asset.contact -eq "contained_liquid") -and -not (Has-Property $asset "expected_surface_y")) {
            Add-Failure "$assetCtx contact '$($asset.contact)' requires expected_surface_y"
        }
        if ($asset.contact -eq "mounted" -and -not (Has-Property $asset "anchor")) {
            Add-Failure "$assetCtx mounted contact requires anchor"
        }
        if ((Has-Property $asset "material_override") -and -not $paletteIds.ContainsKey([string]$asset.material_override)) {
            Add-Failure "$assetCtx material_override '$($asset.material_override)' is not in material_palette"
        }
    }

    if (@($seed.authored_groups).Count -lt [int]$seed.validation.min_authored_groups) {
        Add-Failure "$ctx authored_groups count is below validation.min_authored_groups"
    }
    $primaryCount = 0
    foreach ($group in @($seed.authored_groups)) {
        $groupCtx = "$ctx authored_group '$($group.id)'"
        foreach ($prop in @($schema.required_authored_group)) {
            [void](Require-Property $group ([string]$prop) $groupCtx)
        }
        if ([bool]$group.primary) { $primaryCount++ }
        if (@($group.parts).Count -lt 3) { Add-Failure "$groupCtx must list at least three parts" }
        if (-not (Has-Property $group.contact_rules "max_gap_m")) {
            Add-Failure "$groupCtx contact_rules must include max_gap_m"
        }
    }
    if ($primaryCount -lt 2) { Add-Failure "$ctx must have at least two primary authored groups" }

    foreach ($randomGroup in @($seed.seeded_random_groups)) {
        $randCtx = "$ctx seeded_random_group '$($randomGroup.id)'"
        foreach ($prop in @($schema.seeded_random_group_policy.required_fields)) {
            [void](Require-Property $randomGroup ([string]$prop) $randCtx)
        }
        if ([int]$randomGroup.count -lt 1) { Add-Failure "$randCtx count must be positive" }
        if ([double]$randomGroup.min_spacing -le 0.0) { Add-Failure "$randCtx min_spacing must be positive" }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Scene seed contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) { Write-Host " - $failure" -ForegroundColor Red }
    exit 1
}

Write-Host "Scene seed contract tests passed: seeds=$($seedFiles.Count)" -ForegroundColor Green
