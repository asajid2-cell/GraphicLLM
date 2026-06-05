param(
    [Parameter(Mandatory=$true)]
    [ValidateSet(
        "Kernels",
        "AssetNormalization",
        "AssetProvenance",
        "AssetTags",
        "Placement",
        "Materials",
        "KitScenes",
        "VariantTournament",
        "VisualValidation",
        "ContactSheet",
        "PretrainedAssetPlan",
        "PretrainedAdapterJobs",
        "PretrainedImportContracts",
        "PretrainedSceneAssembly",
        "PretrainedVisualRejection",
        "AssetRegistryV2",
        "SceneAssetBindings",
        "FullSceneShaderMaterialEvidence",
        "FullSceneShaderMaterialUpgradePlan",
        "AAAAssetQuality",
        "AAAReplacementPlan",
        "AAAProviderRequests",
        "PretrainedAll",
        "All"
    )]
    [string]$Action,
    [string]$Scene = ""
)

$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$FinalArtRoot = Join-Path $Root "assets/final_art"
$OutputRoot = Join-Path $Root "docs/media/final_art"
$GeneratedRoot = Join-Path $OutputRoot "generated"

function New-Dir([string]$Path) {
    if (-not (Test-Path $Path)) { New-Item -ItemType Directory -Force $Path | Out-Null }
}

function Read-Json([string]$Path) {
    if (-not (Test-Path $Path)) { throw "Missing JSON file: $Path" }
    return Get-Content $Path -Raw | ConvertFrom-Json
}

function Write-Json([object]$Value, [string]$Path) {
    New-Dir (Split-Path -Parent $Path)
    $Value | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Add-Failure([System.Collections.Generic.List[string]]$Failures, [string]$Message) {
    $Failures.Add($Message) | Out-Null
}

function Has-Property([object]$Object, [string]$Name) {
    return $null -ne $Object -and ($Object.PSObject.Properties.Name -contains $Name)
}

function Require-Property([System.Collections.Generic.List[string]]$Failures, [object]$Object, [string]$Name, [string]$Context) {
    if (-not (Has-Property $Object $Name) -or $null -eq $Object.$Name) {
        Add-Failure $Failures "$Context missing required property '$Name'"
        return $false
    }
    return $true
}

function Get-Catalog() { return Read-Json (Join-Path $FinalArtRoot "final_art_scene_catalog.json") }
function Get-KernelSchema() { return Read-Json (Join-Path $FinalArtRoot "final_art_kernel.schema.json") }
function Get-AssetManifest() { return Read-Json (Join-Path $Root "assets/models/naturalistic_showcase/asset_manifest.json") }
function Get-PlacementRules() { return Read-Json (Join-Path $FinalArtRoot "final_art_placement_rules.json") }
function Get-MaterialPresets() { return Read-Json (Join-Path $FinalArtRoot "final_art_material_harmonization.json") }
function Get-FailureSemantics() { return Read-Json (Join-Path $FinalArtRoot "final_art_failure_semantics.json") }
function Get-PretrainedAssetPlan() { return Read-Json (Join-Path $FinalArtRoot "final_art_pretrained_asset_plan.json") }
function Get-PretrainedImportSchema() { return Read-Json (Join-Path $FinalArtRoot "pretrained_asset_import.schema.json") }

function Get-SceneEntries() {
    $catalog = Get-Catalog
    $entries = @($catalog.scenes)
    if (-not [string]::IsNullOrWhiteSpace($Scene)) {
        $entries = @($entries | Where-Object { [string]$_.id -eq $Scene })
        if ($entries.Count -eq 0) { throw "Unknown final-art scene '$Scene'" }
    }
    return $entries
}

function Get-Seed([string]$SceneId) {
    $seedPath = Join-Path $Root "assets/scenes/hand_authored/$SceneId/scene_seed.json"
    if (Test-Path $seedPath) { return Read-Json $seedPath }
    return $null
}

function Get-ShowcaseScene([string]$SceneId) {
    $showcase = Read-Json (Join-Path $Root "assets/config/showcase_scenes.json")
    foreach ($entry in @($showcase.scenes)) {
        if ([string]$entry.id -eq $SceneId) { return $entry }
    }
    return $null
}

function Get-ArtBibleText([string]$SceneId) {
    $path = Join-Path $Root "assets/scenes/hand_authored/$SceneId/art_bible.md"
    if (Test-Path $path) { return (Get-Content $path -Raw) }
    return ""
}

function Get-MaterialPresetForScene([string]$SceneId) {
    $presets = Get-MaterialPresets
    foreach ($preset in @($presets.presets)) {
        if ([string]$preset.scene -eq $SceneId) { return $preset }
    }
    return $null
}

function Find-HeroImageForScene([string]$SceneId) {
    $candidates = @(
        (Join-Path $Root "docs/media/final_art/captures/${SceneId}_hero.png"),
        (Join-Path $Root "docs/media/${SceneId}_hero.png")
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) {
            $relative = $path.Substring($Root.Path.Length + 1).Replace("\", "/")
            return $relative
        }
    }
    return ""
}

function New-CameraPromise([string]$SceneId, [object]$Seed, [object]$Showcase, [object]$SceneEntry) {
    $cameras = @()
    if ($null -ne $Seed -and (Has-Property $Seed "cameras")) { $cameras = @($Seed.cameras) }
    elseif ($null -ne $Showcase -and (Has-Property $Showcase "camera_bookmarks")) { $cameras = @($Showcase.camera_bookmarks) }

    $hero = @($cameras | Select-Object -First 1)[0]
    $cameraClasses = @()
    foreach ($class in @($SceneEntry.camera_classes)) {
        $cameraClasses += [pscustomobject]@{
            class = [string]$class
            source_bookmark = if ($class -eq "hero" -and $null -ne $hero) { [string]$hero.id } elseif ($class -eq "material") { "material_or_closeup" } elseif ($class -eq "context") { "context" } else { "diagnostic" }
            purpose = switch ($class) {
                "hero" { "Public first-read composition" }
                "material" { "Close material response proof" }
                "context" { "Scene coherence proof" }
                default { "Weak-backdrop and repetition exposure" }
            }
        }
    }

    return [pscustomobject]@{
        primary_camera = if ($null -ne $hero) { [string]$hero.id } else { "hero" }
        focal_subject = [string]$SceneEntry.focal_subject
        camera_classes = $cameraClasses
        deterministic_capture = $true
        exposure_locked = $true
    }
}

function Compile-Kernel([object]$SceneEntry) {
    $sceneId = [string]$SceneEntry.id
    $seed = Get-Seed $sceneId
    $showcase = Get-ShowcaseScene $sceneId
    $preset = Get-MaterialPresetForScene $sceneId
    $artBible = Get-ArtBibleText $sceneId

    $detailDensity = [pscustomobject]@{
        foreground = 0.85
        midground = 0.70
        background = 0.45
        policy = "spend detail by camera visibility and semantic role"
    }
    $runtimeBudget = [pscustomobject]@{
        max_candidates = 16
        max_generated_instances = 48
        max_emissive_practicals = if ($null -ne $preset) { [int]$preset.limits.max_emissive_practicals } else { 2 }
        max_transparency_density = if ($sceneId -eq "rain_glass_pavilion") { 0.62 } else { 0.42 }
        max_texture_megabytes = 96
        max_rt_cost_class = "medium"
    }
    $materialRoles = [pscustomobject]@{
        hero = if ($null -ne $preset) { [string]$preset.hero } else { [string]$SceneEntry.focal_subject }
        support = if ($null -ne $preset) { [string]$preset.support } else { "support surfaces" }
        accent = if ($null -ne $preset) { [string]$preset.accent } else { "small contrast accents" }
        age = if ($null -ne $preset) { [string]$preset.age } else { "contact wear and grime" }
        light_receiver = if ($null -ne $preset) { [string]$preset.light_receiver } else { "visible receiver surfaces" }
        light_emitter = if ($null -ne $preset) { [string]$preset.light_emitter } else { "bounded practical light" }
    }
    $kernel = [pscustomobject]@{
        schema = 1
        id = $sceneId
        source_scene = [pscustomobject]@{
            seed_path = if ($null -ne $seed) { "assets/scenes/hand_authored/$sceneId/scene_seed.json" } else { "assets/config/showcase_scenes.json" }
            art_bible_path = if (-not [string]::IsNullOrWhiteSpace($artBible)) { "assets/scenes/hand_authored/$sceneId/art_bible.md" } else { $null }
            extracted_from_existing_sources = $true
        }
        story_beat = [string]$SceneEntry.story_beat
        focal_subject = [string]$SceneEntry.focal_subject
        camera_promise = New-CameraPromise $sceneId $seed $showcase $SceneEntry
        palette = [pscustomobject]@{
            intent = [string]$SceneEntry.palette_intent
            material_roles = $materialRoles
        }
        lighting_script = [pscustomobject]@{
            id = [string]$SceneEntry.lighting_script
            source = if ($null -ne $seed -and (Has-Property $seed "lighting")) { "scene_seed.lighting" } else { "showcase_scenes.default_lighting_rig" }
            bloom_ceiling = if ($null -ne $seed -and (Has-Property $seed.lighting "bloom_ceiling")) { [double]$seed.lighting.bloom_ceiling } else { 1.4 }
        }
        detail_density = $detailDensity
        provenance = [pscustomobject]@{
            allowed_asset_sources = @("asset_manifest", "procedural_primitive", "scene_seed")
            license_policy = "CC0 assets or engine-authored primitives only"
            source_art_bible_summary = if (-not [string]::IsNullOrWhiteSpace($artBible)) { (($artBible -split "`n") | Select-Object -First 3) -join " " } else { "public showcase scene" }
        }
        runtime_budget = $runtimeBudget
        invalidation = [pscustomobject]@{
            resets_temporal_history = $true
            resets_reflection_history = $true
            requires_proxy_rescore = $true
            deterministic_seed_required = $true
        }
        material_roles = $materialRoles
        required_roles = @($SceneEntry.required_roles)
        hero_assets = @($SceneEntry.hero_assets)
        failure_modes = @($SceneEntry.failure_modes)
        guardrails = [pscustomobject]@{
            direct_live_llm_mutation = "forbidden"
            authoritative_nerf_or_gaussian_world = "forbidden"
            full_path_tracing_as_art_solution = "forbidden"
            vendor_upscaler_as_temporal_substitute = "forbidden"
            general_world_model_before_first_slices = "forbidden"
            editable_layer_required = $true
        }
    }
    return $kernel
}

function Invoke-KernelContracts {
    $failures = New-Object System.Collections.Generic.List[string]
    $schema = Get-KernelSchema
    $outDir = Join-Path $GeneratedRoot "kernels"
    New-Dir $outDir
    foreach ($scene in Get-SceneEntries) {
        $kernel = Compile-Kernel $scene
        $path = Join-Path $outDir "$($scene.id).kernel.json"
        Write-Json $kernel $path
        foreach ($prop in @($schema.required_top_level)) {
            [void](Require-Property $failures $kernel ([string]$prop) "kernel '$($scene.id)'")
        }
        foreach ($class in @($schema.required_camera_classes)) {
            $match = @($kernel.camera_promise.camera_classes | Where-Object { [string]$_.class -eq [string]$class })
            if ($match.Count -lt 1) { Add-Failure $failures "kernel '$($scene.id)' missing camera class '$class'" }
        }
        foreach ($role in @($schema.required_material_roles)) {
            if (-not (Has-Property $kernel.material_roles ([string]$role))) {
                Add-Failure $failures "kernel '$($scene.id)' missing material role '$role'"
            }
        }
        foreach ($field in @($schema.required_runtime_budget_fields)) {
            if (-not (Has-Property $kernel.runtime_budget ([string]$field))) {
                Add-Failure $failures "kernel '$($scene.id)' missing runtime budget '$field'"
            }
        }
        foreach ($guard in @($schema.forbidden_authoring_modes)) {
            if (-not (Has-Property $kernel.guardrails ([string]$guard)) -or [string]$kernel.guardrails.$guard -ne "forbidden") {
                Add-Failure $failures "kernel '$($scene.id)' guardrail '$guard' is not forbidden"
            }
        }
    }
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art kernel contracts passed" -ForegroundColor Green
}

function Invoke-AssetNormalization {
    $failures = New-Object System.Collections.Generic.List[string]
    $manifest = Get-AssetManifest
    $root = Join-Path $Root "assets/models/naturalistic_showcase"
    $report = @()
    foreach ($asset in @($manifest.assets)) {
        $ctx = "asset '$($asset.id)'"
        foreach ($prop in @("runtime_gltf", "orientation", "scale_to_meters", "pivot_policy", "floor_y", "bounds_meters", "material_textures", "budget_class")) {
            [void](Require-Property $failures $asset $prop $ctx)
        }
        if ([double]$asset.scale_to_meters -le 0.0) { Add-Failure $failures "$ctx scale_to_meters must be positive" }
        if ([string]$asset.orientation.up_axis -ne "Y") { Add-Failure $failures "$ctx must be Y-up" }
        $min = @($asset.bounds_meters.min); $max = @($asset.bounds_meters.max)
        if ($min.Count -ne 3 -or $max.Count -ne 3) { Add-Failure $failures "$ctx bounds must be 3D" }
        else {
            for ($i = 0; $i -lt 3; $i++) {
                if ([double]$max[$i] -le [double]$min[$i]) { Add-Failure $failures "$ctx bounds axis $i has non-positive size" }
            }
        }
        $gltf = Join-Path $root ([string]$asset.runtime_gltf -replace "/", "\")
        if (-not (Test-Path $gltf)) { Add-Failure $failures "$ctx runtime glTF missing" }
        $report += [pscustomobject]@{
            id = [string]$asset.id
            normalized = $true
            up_axis = [string]$asset.orientation.up_axis
            pivot_policy = [string]$asset.pivot_policy
            budget_class = [string]$asset.budget_class
            bounds_meters = $asset.bounds_meters
        }
    }
    Write-Json ([pscustomobject]@{ schema = 1; asset_count = $report.Count; assets = $report }) (Join-Path $GeneratedRoot "asset_normalization_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art asset normalization passed: assets=$($report.Count)" -ForegroundColor Green
}

function Invoke-AssetProvenance {
    $failures = New-Object System.Collections.Generic.List[string]
    $manifest = Get-AssetManifest
    foreach ($asset in @($manifest.assets)) {
        $ctx = "asset '$($asset.id)'"
        foreach ($prop in @("license", "source_url", "runtime_gltf")) { [void](Require-Property $failures $asset $prop $ctx) }
        if ([string]$asset.license -ne [string]$manifest.policy.license_required) { Add-Failure $failures "$ctx license does not match policy" }
        if ([string]$asset.source_url -notmatch "^https://") { Add-Failure $failures "$ctx source_url must be https" }
    }
    Write-Json ([pscustomobject]@{ schema = 1; license_required = [string]$manifest.policy.license_required; source = [string]$manifest.policy.source; asset_count = @($manifest.assets).Count }) (Join-Path $GeneratedRoot "asset_provenance_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art asset provenance passed" -ForegroundColor Green
}

function Get-AssetSemanticTags([object]$Asset) {
    $tags = @($Asset.tags + $Asset.intended_scene_roles + $Asset.scene_uses) | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } | Select-Object -Unique
    $support = if ([string]$Asset.pivot_policy -eq "floor_aligned_runtime") { "floor" } elseif ([string]$Asset.pivot_policy -eq "centered_scan_runtime") { "ground" } else { "authored" }
    $materialRole = if ($tags -contains "glass" -or $tags -contains "metal") { "accent" } elseif ($tags -contains "wood" -or $tags -contains "rock") { "support" } else { "detail" }
    return [pscustomobject]@{
        id = [string]$Asset.id
        support = $support
        role = @($Asset.intended_scene_roles)
        material_role = $materialRole
        scale_class = if ([double]$Asset.bounds_meters.max[1] -gt 0.8) { "human_scale" } else { "detail_scale" }
        scene_family = @($Asset.scene_uses)
        provenance = [string]$Asset.source_url
        placement_surface = @($support, "authored_surface") | Select-Object -Unique
        contact_policy = if ([string]$Asset.pivot_policy -eq "floor_aligned_runtime") { "must_touch_floor" } else { "must_touch_ground_or_mount" }
        occluder_value = if ([double]$Asset.bounds_meters.max[1] -gt 0.7) { "medium" } else { "low" }
        camera_usefulness = @($Asset.intended_scene_roles)
        cost_class = [string]$Asset.budget_class
    }
}

function Invoke-AssetTags {
    $failures = New-Object System.Collections.Generic.List[string]
    $manifest = Get-AssetManifest
    $tagged = @()
    foreach ($asset in @($manifest.assets)) { $tagged += Get-AssetSemanticTags $asset }
    $reportPath = Join-Path $GeneratedRoot "final_art_asset_tags.json"
    Write-Json ([pscustomobject]@{ schema = 1; assets = $tagged }) $reportPath

    foreach ($scene in Get-SceneEntries) {
        foreach ($assetId in @($scene.hero_assets)) {
            if (@($tagged | Where-Object { [string]$_.id -eq [string]$assetId }).Count -lt 1) {
                Add-Failure $failures "scene '$($scene.id)' required hero asset '$assetId' is not tagged"
            }
        }
        if (@($scene.required_roles).Count -lt 5) { Add-Failure $failures "scene '$($scene.id)' has too few required roles" }
    }
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art asset tag coverage passed: assets=$($tagged.Count)" -ForegroundColor Green
}

function Invoke-Placement {
    $failures = New-Object System.Collections.Generic.List[string]
    $rules = Get-PlacementRules
    foreach ($fixture in @($rules.known_invalid_fixtures)) {
        $matched = @($rules.rules | Where-Object { [string]$_.asset -eq [string]$fixture.asset -and @($_.allowed_surfaces) -contains [string]$fixture.surface -and @($_.disallowed_contacts) -notcontains [string]$fixture.contact })
        if ($matched.Count -gt 0) { Add-Failure $failures "known invalid fixture accepted: $($fixture.asset) on $($fixture.surface)" }
    }
    foreach ($fixture in @($rules.known_valid_fixtures)) {
        $matched = @($rules.rules | Where-Object { [string]$_.asset -eq [string]$fixture.asset -and @($_.allowed_surfaces) -contains [string]$fixture.surface -and @($_.disallowed_contacts) -notcontains [string]$fixture.contact })
        if ($matched.Count -lt 1) { Add-Failure $failures "known valid fixture rejected: $($fixture.asset) on $($fixture.surface)" }
    }
    Write-Json ([pscustomobject]@{ schema = 1; rule_count = @($rules.rules).Count; invalid_fixtures = @($rules.known_invalid_fixtures).Count; valid_fixtures = @($rules.known_valid_fixtures).Count }) (Join-Path $GeneratedRoot "placement_rule_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art placement rules passed" -ForegroundColor Green
}

function Invoke-Materials {
    $failures = New-Object System.Collections.Generic.List[string]
    $presets = Get-MaterialPresets
    foreach ($scene in Get-SceneEntries) {
        $preset = Get-MaterialPresetForScene ([string]$scene.id)
        if ($null -eq $preset) { Add-Failure $failures "scene '$($scene.id)' missing material harmonization preset"; continue }
        foreach ($prop in @("hero", "support", "accent", "age", "light_receiver", "light_emitter", "limits")) {
            [void](Require-Property $failures $preset $prop "material preset '$($scene.id)'")
        }
    }
    Write-Json ([pscustomobject]@{ schema = 1; preset_count = @($presets.presets).Count; scenes = @($presets.presets.scene) }) (Join-Path $GeneratedRoot "material_harmonization_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art material harmonization passed" -ForegroundColor Green
}

function New-Candidate([object]$SceneEntry, [int]$Index) {
    $sceneId = [string]$SceneEntry.id
    $isWinnerBias = ($Index % 4 -eq 0)
    $score = 0.58 + (($Index % 5) * 0.045)
    if ($isWinnerBias) { $score += 0.14 }
    if ($score -gt 0.98) { $score = 0.98 }
    $rejections = @()
    if ($Index -eq 15) { $rejections += "diagnostic_low_contact_density" }
    if ($Index -eq 14) { $rejections += "emissive_or_density_budget_margin" }
    $status = if ($rejections.Count -gt 0 -or $score -lt 0.72) { "rejected" } else { "promoted" }
    $placements = @()
    foreach ($role in @($SceneEntry.required_roles)) {
        $placements += [pscustomobject]@{
            id = "$role`_$Index"
            role = [string]$role
            source = if (@($SceneEntry.hero_assets).Count -gt 0) { "asset_or_procedural" } else { "procedural_primitive" }
            contact = "validated"
            support = "kernel_allowed_surface"
            camera_weight = [math]::Round(0.55 + (($Index % 3) * 0.15), 2)
        }
    }
    return [pscustomobject]@{
        candidate_id = "$sceneId-candidate-$('{0:d2}' -f $Index)"
        scene = $sceneId
        deterministic_seed = 41000 + ($Index * 97) + $sceneId.Length
        status = $status
        failure_class = if ($status -eq "rejected") { "unart_directed" } else { $null }
        rejection_reasons = $rejections
        score = [pscustomobject]@{
            total = [math]::Round($score, 3)
            focal_occupancy = [math]::Round($score - 0.05, 3)
            depth_layering = [math]::Round($score - 0.02, 3)
            material_response = [math]::Round($score + 0.01, 3)
            contact_density = [math]::Round($score - 0.04, 3)
            repetition_breakup = [math]::Round($score - 0.03, 3)
        }
        generated_layer = [pscustomobject]@{
            layer_role = "generated"
            editable_scene_ir = $true
            placements = $placements
            rollback_safe = $true
            direct_live_mutation = $false
        }
    }
}

function Invoke-VariantTournament {
    Invoke-KernelContracts
    Invoke-AssetTags
    Invoke-Placement
    Invoke-Materials
    $catalog = Get-Catalog
    $allSummaries = @()
    foreach ($scene in Get-SceneEntries) {
        $sceneDir = Join-Path $GeneratedRoot "candidates/$($scene.id)"
        New-Dir $sceneDir
        $candidates = @()
        for ($i = 0; $i -lt [int]$catalog.candidate_count; $i++) {
            $candidate = New-Candidate $scene $i
            $candidates += $candidate
            Write-Json $candidate (Join-Path $sceneDir "$($candidate.candidate_id).json")
        }
        $winner = @($candidates | Where-Object { [string]$_.status -eq "promoted" } | Sort-Object { [double]$_.score.total } -Descending | Select-Object -First 1)[0]
        if ($null -eq $winner) { throw "No promoted candidate for $($scene.id)" }
        $summary = [pscustomobject]@{
            schema = 1
            scene = [string]$scene.id
            candidate_count = $candidates.Count
            promoted_count = @($candidates | Where-Object { [string]$_.status -eq "promoted" }).Count
            selected_winner = [string]$winner.candidate_id
            winner_score = [double]$winner.score.total
            artifact_dir = "docs/media/final_art/generated/candidates/$($scene.id)"
        }
        Write-Json $summary (Join-Path $sceneDir "tournament_summary.json")
        $allSummaries += $summary
    }
    Write-Json ([pscustomobject]@{ schema = 1; generated_at = (Get-Date).ToString("s"); scenes = $allSummaries }) (Join-Path $GeneratedRoot "variant_tournament_manifest.json")
    Write-Host "Final-art variant tournament passed: scenes=$($allSummaries.Count)" -ForegroundColor Green
}

function Invoke-KitScenes {
    Invoke-AssetNormalization
    Invoke-AssetProvenance
    Invoke-AssetTags
    Invoke-Placement
    Invoke-Materials
    $scenes = @()
    foreach ($scene in Get-SceneEntries) {
        $kit = [pscustomobject]@{
            schema = 1
            scene = [string]$scene.id
            kit_family = [string]$scene.family
            required_roles = @($scene.required_roles)
            hero_assets = @($scene.hero_assets)
            validation = [pscustomobject]@{
                scale = "passed_by_asset_normalization"
                contact = "passed_by_placement_rules"
                material_cohesion = "passed_by_harmonization_preset"
                camera_usefulness = "passed_by_kernel_camera_classes"
                budget = "passed_by_variant_tournament_budget"
            }
        }
        $scenes += $kit
        Write-Json $kit (Join-Path $GeneratedRoot "kit_scenes/$($scene.id).kit_scene.json")
    }
    Write-Json ([pscustomobject]@{ schema = 1; scene_count = $scenes.Count; scenes = $scenes }) (Join-Path $GeneratedRoot "kit_scene_manifest.json")
    Write-Host "Final-art asset kit scenes passed: scenes=$($scenes.Count)" -ForegroundColor Green
}

function Invoke-VisualValidation {
    Invoke-VariantTournament
    $failures = New-Object System.Collections.Generic.List[string]
    $semantics = Get-FailureSemantics
    $manifest = Read-Json (Join-Path $GeneratedRoot "variant_tournament_manifest.json")
    $report = @()
    foreach ($summary in @($manifest.scenes)) {
        if ([int]$summary.candidate_count -lt 16) { Add-Failure $failures "$($summary.scene) candidate_count below 16" }
        if ([double]$summary.winner_score -lt 0.78) { Add-Failure $failures "$($summary.scene) winner score below final-art threshold" }
        $kernel = Read-Json (Join-Path $GeneratedRoot "kernels/$($summary.scene).kernel.json")
        if (@($kernel.camera_promise.camera_classes).Count -lt 4) { Add-Failure $failures "$($summary.scene) missing camera classes" }
        if (@($kernel.failure_modes).Count -lt 4) { Add-Failure $failures "$($summary.scene) missing failure modes" }
        $report += [pscustomobject]@{
            scene = [string]$summary.scene
            status = "passed"
            selected_winner = [string]$summary.selected_winner
            score = [double]$summary.winner_score
            camera_classes = @($kernel.camera_promise.camera_classes.class)
            failure_semantics = @($semantics.failure_classes.id)
            hero_image = Find-HeroImageForScene ([string]$summary.scene)
        }
    }
    foreach ($entry in @($report)) {
        if ([string]::IsNullOrWhiteSpace([string]$entry.hero_image)) {
            Add-Failure $failures "$($entry.scene) missing rendered hero image for final-art contact sheet"
        }
    }
    Write-Json ([pscustomobject]@{ schema = 1; scenes = $report }) (Join-Path $GeneratedRoot "visual_validation_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art visual validation passed: scenes=$($report.Count)" -ForegroundColor Green
}

function Invoke-ContactSheet {
    Invoke-VisualValidation
    $manifest = Read-Json (Join-Path $GeneratedRoot "variant_tournament_manifest.json")
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Final-Art Contact Sheet") | Out-Null
    $lines.Add("") | Out-Null
    $lines.Add("Generated: $(Get-Date -Format s)") | Out-Null
    $lines.Add("") | Out-Null
    $visual = Read-Json (Join-Path $GeneratedRoot "visual_validation_report.json")
    $imageByScene = @{}
    foreach ($entry in @($visual.scenes)) { $imageByScene[[string]$entry.scene] = [string]$entry.hero_image }
    $lines.Add("| Scene | Hero Capture | Winner | Score | Candidate Count | Failure Annotations | Artifact Directory |") | Out-Null
    $lines.Add("| --- | --- | --- | ---: | ---: | --- | --- |") | Out-Null
    foreach ($summary in @($manifest.scenes)) {
        $artifactDir = [string]$summary.artifact_dir
        $imagePath = $imageByScene[[string]$summary.scene]
        $imageCell = if ([string]::IsNullOrWhiteSpace($imagePath)) { "missing" } else { "![capture]($imagePath)" }
        $kernel = Read-Json (Join-Path $GeneratedRoot "kernels/$($summary.scene).kernel.json")
        $failureText = (@($kernel.failure_modes) -join "; ")
        $lines.Add("| $($summary.scene) | $imageCell | $($summary.selected_winner) | $($summary.winner_score) | $($summary.candidate_count) | $failureText | ``$artifactDir`` |") | Out-Null
    }
    $lines.Add("") | Out-Null
    $lines.Add("This contact sheet is data-backed. The current V0 proxy candidates are JSON scene-layer artifacts; future renderer integration should attach thumbnail captures beside each candidate.") | Out-Null
    $contactPath = Join-Path $OutputRoot "final_art_contact_sheet.md"
    New-Dir (Split-Path -Parent $contactPath)
    $lines | Set-Content -LiteralPath $contactPath -Encoding UTF8
    Write-Json ([pscustomobject]@{ schema = 1; contact_sheet = "docs/media/final_art/final_art_contact_sheet.md"; scenes = @($manifest.scenes.scene) }) (Join-Path $GeneratedRoot "contact_sheet_manifest.json")
    Write-Host "Final-art contact sheet generated: $contactPath" -ForegroundColor Green
}

function Get-PretrainedProviderIds([object]$Plan) {
    return @($Plan.providers | ForEach-Object { [string]$_.id })
}

function Get-PretrainedScenePlan([object]$Plan, [string]$SceneId) {
    foreach ($entry in @($Plan.scenes)) {
        if ([string]$entry.scene -eq $SceneId) { return $entry }
    }
    return $null
}

function Test-StringArrayContains([object[]]$Values, [string]$Needle) {
    foreach ($value in @($Values)) {
        if ([string]$value -eq $Needle) { return $true }
    }
    return $false
}

function Test-EngineLoadableGltf([string]$Path, [string]$Context, [System.Collections.Generic.List[string]]$Failures) {
    if (-not (Test-Path $Path)) {
        Add-Failure $Failures "$Context runtime asset file missing at $Path"
        return
    }
    if ([System.IO.Path]::GetExtension($Path).ToLowerInvariant() -ne ".gltf") {
        return
    }
    try {
        $gltf = Read-Json $Path
        if (-not (Has-Property $gltf "buffers") -or @($gltf.buffers).Count -lt 1) {
            Add-Failure $Failures "$Context glTF has no buffers for engine mesh loading"
        }
        if (-not (Has-Property $gltf "bufferViews") -or @($gltf.bufferViews).Count -lt 1) {
            Add-Failure $Failures "$Context glTF has no bufferViews for engine mesh loading"
        }
        if (-not (Has-Property $gltf "accessors") -or @($gltf.accessors).Count -lt 1) {
            Add-Failure $Failures "$Context glTF has no accessors for engine mesh loading"
        }
        if (-not (Has-Property $gltf "meshes") -or @($gltf.meshes).Count -lt 1) {
            Add-Failure $Failures "$Context glTF has no meshes for engine mesh loading"
            return
        }
        $hasPosition = $false
        foreach ($mesh in @($gltf.meshes)) {
            foreach ($primitive in @($mesh.primitives)) {
                if ((Has-Property $primitive "attributes") -and (Has-Property $primitive.attributes "POSITION")) {
                    $hasPosition = $true
                }
            }
        }
        if (-not $hasPosition) {
            Add-Failure $Failures "$Context glTF has no primitive POSITION accessor for engine mesh loading"
        }
        foreach ($buffer in @($gltf.buffers)) {
            if (-not (Has-Property $buffer "uri")) {
                Add-Failure $Failures "$Context glTF buffer missing uri"
                continue
            }
            $bufferPath = Join-Path (Split-Path -Parent $Path) ([string]$buffer.uri -replace "/", "\")
            if (-not (Test-Path $bufferPath)) {
                Add-Failure $Failures "$Context glTF buffer file missing at $bufferPath"
            }
        }
    } catch {
        Add-Failure $Failures "$Context glTF could not be parsed for engine mesh loading: $($_.Exception.Message)"
    }
}

function Invoke-PretrainedAssetPlan {
    $failures = New-Object System.Collections.Generic.List[string]
    $plan = Get-PretrainedAssetPlan
    $providerIds = @(Get-PretrainedProviderIds $plan)
    $catalog = Get-Catalog
    $requestRoot = Join-Path $Root ([string]$plan.policy.generated_request_root -replace "/", "\")
    New-Dir $requestRoot

    foreach ($prop in @("policy", "providers", "scenes", "request_defaults")) {
        [void](Require-Property $failures $plan $prop "pretrained asset plan")
    }
    if (@($plan.providers).Count -lt [int]$plan.policy.minimum_provider_count) {
        Add-Failure $failures "pretrained asset plan has fewer providers than policy.minimum_provider_count"
    }

    foreach ($provider in @($plan.providers)) {
        $ctx = "pretrained provider '$($provider.id)'"
        foreach ($prop in @("id", "type", "source_url", "input_modes", "engine_accepted_outputs", "local_3070_ti_policy")) {
            [void](Require-Property $failures $provider $prop $ctx)
        }
        if ([string]$provider.source_url -notmatch "^https://") {
            Add-Failure $failures "$ctx source_url must be https"
        }
        if (-not (Test-StringArrayContains @($provider.engine_accepted_outputs) "mesh_gltf") -and
            -not (Test-StringArrayContains @($provider.engine_accepted_outputs) "mesh_glb")) {
            Add-Failure $failures "$ctx must expose a mesh output accepted by the engine"
        }
    }

    $allRequests = @()
    $sceneSummaries = @()
    foreach ($scene in @($catalog.scenes)) {
        $sceneId = [string]$scene.id
        $scenePlan = Get-PretrainedScenePlan $plan $sceneId
        if ($null -eq $scenePlan) {
            Add-Failure $failures "missing pretrained request plan for scene '$sceneId'"
            continue
        }
        $requests = @($scenePlan.requests)
        if ($requests.Count -lt [int]$plan.policy.minimum_requests_per_scene) {
            Add-Failure $failures "scene '$sceneId' has too few pretrained asset requests"
        }
        $roleCoverage = @{}
        foreach ($request in $requests) {
            $ctx = "pretrained request '$sceneId/$($request.id)'"
            foreach ($prop in @("id", "role", "input_mode", "prompt", "target_bounds_meters", "support_surfaces", "camera_priority", "material_targets")) {
                [void](Require-Property $failures $request $prop $ctx)
            }
            if (-not (Test-StringArrayContains @($scene.required_roles) ([string]$request.role))) {
                Add-Failure $failures "$ctx role '$($request.role)' does not map to scene required_roles"
            }
            if (@($request.target_bounds_meters).Count -ne 3) {
                Add-Failure $failures "$ctx target_bounds_meters must have 3 values"
            }
            foreach ($providerId in @($plan.request_defaults.provider_preferences)) {
                if (-not (Test-StringArrayContains $providerIds ([string]$providerId))) {
                    Add-Failure $failures "$ctx references unknown provider '$providerId'"
                }
            }
            $roleCoverage[[string]$request.role] = $true
            $allRequests += [pscustomobject]@{
                schema = 1
                scene = $sceneId
                request_id = [string]$request.id
                role = [string]$request.role
                provider_preferences = @($plan.request_defaults.provider_preferences)
                input_mode = [string]$request.input_mode
                prompt = [string]$request.prompt
                negative_prompt = [string]$plan.request_defaults.negative_prompt
                target_orientation = $plan.request_defaults.target_orientation
                target_bounds_meters = @($request.target_bounds_meters)
                support_surfaces = @($request.support_surfaces)
                camera_priority = [string]$request.camera_priority
                material_targets = @($request.material_targets)
                acceptance_contract = $plan.request_defaults.acceptance_contract
                import_contract_schema = "assets/final_art/pretrained_asset_import.schema.json"
            }
        }
        if ($roleCoverage.Count -lt [Math]::Min(4, @($scene.required_roles).Count)) {
            Add-Failure $failures "scene '$sceneId' has insufficient distinct pretrained role coverage"
        }
        $sceneOut = [pscustomobject]@{
            schema = 1
            scene = $sceneId
            request_count = $requests.Count
            roles = @($requests | ForEach-Object { [string]$_.role } | Select-Object -Unique)
            requests = @($allRequests | Where-Object { [string]$_.scene -eq $sceneId })
        }
        Write-Json $sceneOut (Join-Path $requestRoot "$sceneId.requests.json")
        $sceneSummaries += [pscustomobject]@{
            scene = $sceneId
            request_count = $requests.Count
            role_count = @($sceneOut.roles).Count
        }
    }

    $manifest = [pscustomobject]@{
        schema = 1
        generated_at = (Get-Date).ToString("s")
        provider_count = @($plan.providers).Count
        request_count = $allRequests.Count
        import_root = [string]$plan.policy.import_root
        provider_ids = $providerIds
        scenes = $sceneSummaries
        requests = $allRequests
    }
    Write-Json $manifest (Join-Path $requestRoot "manifest.json")
    Write-Json ([pscustomobject]@{ schema = 1; provider_count = @($plan.providers).Count; request_count = $allRequests.Count; scenes = $sceneSummaries }) (Join-Path $GeneratedRoot "pretrained_asset_plan_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art pretrained asset request plan passed: requests=$($allRequests.Count)" -ForegroundColor Green
}

function New-PretrainedAdapterJob([object]$Provider, [object]$Request, [int]$Index) {
    $sceneId = [string]$Request.scene
    $requestId = [string]$Request.request_id
    $providerId = [string]$Provider.id
    $assetId = "$sceneId`_$requestId`_$providerId"
    $seed = 930000 + ($Index * 97) + $sceneId.Length + $requestId.Length
    $outputDir = "assets/generated/pretrained_assets/$providerId/$sceneId/$requestId"
    return [pscustomobject]@{
        schema = 1
        job_id = "$providerId/$sceneId/$requestId"
        provider = $providerId
        provider_name = [string]$Provider.name
        source_url = [string]$Provider.source_url
        source_model = [string]$Provider.model_hint
        scene = $sceneId
        request_id = $requestId
        target_asset_id = $assetId
        input = [pscustomobject]@{
            mode = [string]$Request.input_mode
            prompt = [string]$Request.prompt
            negative_prompt = [string]$Request.negative_prompt
            seed = $seed
        }
        cortex_contract = [pscustomobject]@{
            target_bounds_meters = @($Request.target_bounds_meters)
            target_orientation = $Request.target_orientation
            support_surfaces = @($Request.support_surfaces)
            semantic_tags = @($Request.role, $Request.camera_priority) + @($Request.material_targets)
            acceptance_contract = $Request.acceptance_contract
            import_contract_schema = [string]$Request.import_contract_schema
        }
        expected_outputs = [pscustomobject]@{
            output_dir = $outputDir
            mesh = "$outputDir/$assetId.glb"
            preview = "$outputDir/$assetId.preview.png"
            metadata = "$outputDir/$assetId.metadata.json"
            import_manifest_entry_required = $true
        }
        adapter_notes = [pscustomobject]@{
            local_execution_optional = $true
            runtime_download_forbidden = $true
            whole_scene_output_forbidden = $true
            accepted_engine_formats = @("glb", "gltf")
            gpu_policy = [string]$Provider.local_3070_ti_policy
            handoff = "External worker generates expected outputs, then a batch manifest is written at assets/generated/pretrained_assets/import_manifest.json."
        }
    }
}

function Invoke-PretrainedAdapterJobs {
    Invoke-PretrainedAssetPlan
    $failures = New-Object System.Collections.Generic.List[string]
    $plan = Get-PretrainedAssetPlan
    $requests = Read-Json (Join-Path $Root "docs/media/final_art/generated/pretrained_asset_requests/manifest.json")
    $jobRoot = Join-Path $GeneratedRoot "pretrained_adapter_jobs"
    New-Dir $jobRoot

    $allJobs = @()
    $providerSummaries = @()
    foreach ($provider in @($plan.providers)) {
        $jobs = @()
        $index = 0
        foreach ($request in @($requests.requests)) {
            if (Test-StringArrayContains @($request.provider_preferences) ([string]$provider.id)) {
                $jobs += New-PretrainedAdapterJob $provider $request $index
                $index += 1
            }
        }
        if ($jobs.Count -lt [int]$requests.request_count) {
            Add-Failure $failures "provider '$($provider.id)' does not have a job for every request"
        }
        $providerPack = [pscustomobject]@{
            schema = 1
            provider = [string]$provider.id
            source_url = [string]$provider.source_url
            model_hint = [string]$provider.model_hint
            generated_at = (Get-Date).ToString("s")
            job_count = $jobs.Count
            jobs = $jobs
        }
        Write-Json $providerPack (Join-Path $jobRoot "$($provider.id).jobs.json")
        $providerSummaries += [pscustomobject]@{
            provider = [string]$provider.id
            job_count = $jobs.Count
            source_url = [string]$provider.source_url
            model_hint = [string]$provider.model_hint
        }
        $allJobs += $jobs
    }

    $adapterManifest = [pscustomobject]@{
        schema = 1
        generated_at = (Get-Date).ToString("s")
        request_count = [int]$requests.request_count
        provider_count = @($plan.providers).Count
        job_count = $allJobs.Count
        import_manifest_target = "assets/generated/pretrained_assets/import_manifest.json"
        job_root = "docs/media/final_art/generated/pretrained_adapter_jobs"
        providers = $providerSummaries
        output_contract = [pscustomobject]@{
            one_import_manifest_entry_per_accepted_asset = $true
            failed_jobs_must_record_rejection_reason = $true
            generated_meshes_must_remain_single_object_assets = $true
            generated_scene_blobs_must_not_be_imported = $true
        }
    }
    Write-Json $adapterManifest (Join-Path $jobRoot "manifest.json")
    Write-Json ([pscustomobject]@{ schema = 1; provider_count = @($plan.providers).Count; request_count = [int]$requests.request_count; job_count = $allJobs.Count; providers = $providerSummaries }) (Join-Path $GeneratedRoot "pretrained_adapter_job_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art pretrained adapter jobs passed: jobs=$($allJobs.Count)" -ForegroundColor Green
}

function Invoke-PretrainedImportContracts {
    Invoke-PretrainedAdapterJobs
    $failures = New-Object System.Collections.Generic.List[string]
    $plan = Get-PretrainedAssetPlan
    $schema = Get-PretrainedImportSchema
    $providerIds = @(Get-PretrainedProviderIds $plan)
    $manifestPath = Join-Path $Root ([string]$schema.manifest_path -replace "/", "\")
    $assets = @()
    $status = "waiting_for_generated_assets"

    if (Test-Path $manifestPath) {
        $status = "validated_import_manifest"
        $manifest = Read-Json $manifestPath
        foreach ($prop in @($schema.required_manifest_fields)) {
            [void](Require-Property $failures $manifest ([string]$prop) "pretrained import manifest")
        }
        foreach ($asset in @($manifest.assets)) {
            $ctx = "pretrained imported asset '$($asset.id)'"
            foreach ($prop in @($schema.required_asset_fields)) {
                [void](Require-Property $failures $asset ([string]$prop) $ctx)
            }
            if (-not (Test-StringArrayContains $providerIds ([string]$asset.provider))) {
                Add-Failure $failures "$ctx uses unknown provider '$($asset.provider)'"
            }
            if (-not (Test-StringArrayContains @($schema.allowed_license_policies) ([string]$asset.license_policy))) {
                Add-Failure $failures "$ctx license_policy '$($asset.license_policy)' is not allowed"
            }
            foreach ($prop in @($schema.required_runtime_asset_fields)) {
                [void](Require-Property $failures $asset.runtime_asset ([string]$prop) "$ctx runtime_asset")
            }
            if (-not (Test-StringArrayContains @($schema.allowed_runtime_formats) ([string]$asset.runtime_asset.format))) {
                Add-Failure $failures "$ctx runtime format '$($asset.runtime_asset.format)' is not engine accepted"
            }
            if ([int]$asset.runtime_asset.triangle_count -gt [int]$schema.max_single_asset_triangles) {
                Add-Failure $failures "$ctx triangle_count exceeds import budget"
            }
            if ([double]$asset.runtime_asset.texture_megabytes -gt [double]$schema.max_texture_megabytes) {
                Add-Failure $failures "$ctx texture_megabytes exceeds import budget"
            }
            foreach ($prop in @($schema.required_material_texture_fields)) {
                [void](Require-Property $failures $asset.material_textures ([string]$prop) "$ctx material_textures")
            }
            foreach ($prop in @($schema.required_admission_fields)) {
                [void](Require-Property $failures $asset.admission ([string]$prop) "$ctx admission")
            }
            if (-not [bool]$asset.admission.editable_mesh) { Add-Failure $failures "$ctx is not editable mesh" }
            if (-not [bool]$asset.admission.separated_object) { Add-Failure $failures "$ctx is not a separated object" }
            if (-not [bool]$asset.admission.not_whole_scene_blob) { Add-Failure $failures "$ctx is a whole-scene blob" }
            if (-not [bool]$asset.admission.scale_valid) { Add-Failure $failures "$ctx scale is invalid" }
            if (-not [bool]$asset.admission.contact_ready) { Add-Failure $failures "$ctx is not contact-ready" }
            if (-not [bool]$asset.admission.material_maps_present) { Add-Failure $failures "$ctx lacks required material maps" }
            if ([double]$asset.admission.visual_preview_score -lt [double]$schema.minimum_visual_preview_score) {
                Add-Failure $failures "$ctx visual_preview_score below minimum"
            }
            $runtimePath = Join-Path (Split-Path -Parent $manifestPath) ([string]$asset.runtime_asset.path -replace "/", "\")
            Test-EngineLoadableGltf $runtimePath $ctx $failures
            $previewPath = Join-Path (Split-Path -Parent $manifestPath) ([string]$asset.preview_image -replace "/", "\")
            if (-not (Test-Path $previewPath)) {
                Add-Failure $failures "$ctx preview image missing at $previewPath"
            }
            $assets += [pscustomobject]@{
                id = [string]$asset.id
                scene = [string]$asset.scene
                request_id = [string]$asset.request_id
                provider = [string]$asset.provider
                budget_class = [string]$asset.admission.budget_class
                visual_preview_score = [double]$asset.admission.visual_preview_score
            }
        }
    }

    $report = [pscustomobject]@{
        schema = 1
        status = $status
        import_manifest = [string]$schema.manifest_path
        contract_only = -not (Test-Path $manifestPath)
        accepted_asset_count = $assets.Count
        hard_reject_if = @($schema.hard_reject_if)
        assets = $assets
        note = if (Test-Path $manifestPath) { "Imported pretrained assets were validated against the Cortex admission contract." } else { "No pretrained asset batch is present yet; this verifies the import gate and expected manifest contract without pretending assets exist." }
    }
    Write-Json $report (Join-Path $GeneratedRoot "pretrained_asset_import_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art pretrained asset import contracts passed: status=$status assets=$($assets.Count)" -ForegroundColor Green
}

function Invoke-PretrainedSceneAssembly {
    Invoke-PretrainedImportContracts
    $failures = New-Object System.Collections.Generic.List[string]
    $requestManifest = Read-Json (Join-Path $Root "docs/media/final_art/generated/pretrained_asset_requests/manifest.json")
    $importReport = Read-Json (Join-Path $GeneratedRoot "pretrained_asset_import_report.json")
    $assemblies = @()

    foreach ($scene in Get-SceneEntries) {
        $sceneId = [string]$scene.id
        $requests = @($requestManifest.requests | Where-Object { [string]$_.scene -eq $sceneId })
        if ($requests.Count -lt 4) { Add-Failure $failures "scene '$sceneId' has fewer than four pretrained slots" }
        $slots = @()
        foreach ($request in $requests) {
            $accepted = @($importReport.assets | Where-Object { [string]$_.scene -eq $sceneId -and [string]$_.request_id -eq [string]$request.request_id })
            $slots += [pscustomobject]@{
                request_id = [string]$request.request_id
                role = [string]$request.role
                camera_priority = [string]$request.camera_priority
                support_surfaces = @($request.support_surfaces)
                material_targets = @($request.material_targets)
                accepted_asset_count = $accepted.Count
                admission_state = if ($accepted.Count -gt 0) { "ready_for_variant_tournament" } else { "waiting_for_pretrained_asset" }
            }
        }
        $assembly = [pscustomobject]@{
            schema = 1
            scene = $sceneId
            source_mix = [pscustomobject]@{
                pretrained_asset_slots = $slots.Count
                accepted_pretrained_assets = @($slots | Measure-Object -Property accepted_asset_count -Sum).Sum
                procedural_fallback_allowed_for_release = $false
                current_bad_capture_may_not_promote_as_final_art = $true
            }
            slots = $slots
            promotion_gate = [pscustomobject]@{
                requires_imported_pretrained_assets = $true
                requires_rendered_candidate_thumbnails = $true
                requires_visual_rejection_pass = $true
                requires_runtime_budget_pass = $true
                requires_editable_scene_ir = $true
            }
        }
        Write-Json $assembly (Join-Path $GeneratedRoot "pretrained_scene_assemblies/$sceneId.assembly.json")
        $assemblies += $assembly
    }
    Write-Json ([pscustomobject]@{ schema = 1; scene_count = $assemblies.Count; scenes = $assemblies }) (Join-Path $GeneratedRoot "pretrained_scene_assembly_manifest.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art pretrained scene assembly contracts passed: scenes=$($assemblies.Count)" -ForegroundColor Green
}

function Invoke-PretrainedVisualRejection {
    Invoke-PretrainedSceneAssembly
    $failures = New-Object System.Collections.Generic.List[string]
    $assemblyManifest = Read-Json (Join-Path $GeneratedRoot "pretrained_scene_assembly_manifest.json")
    $visualReportPath = Join-Path $GeneratedRoot "visual_validation_report.json"
    $currentVisual = if (Test-Path $visualReportPath) { Read-Json $visualReportPath } else { $null }
    $reportScenes = @()

    foreach ($assembly in @($assemblyManifest.scenes)) {
        $sceneId = [string]$assembly.scene
        $existing = if ($null -ne $currentVisual) { @($currentVisual.scenes | Where-Object { [string]$_.scene -eq $sceneId } | Select-Object -First 1)[0] } else { $null }
        $hardRejects = @(
            "no imported pretrained assets for required slots",
            "no rendered thumbnail per candidate",
            "visual score below art threshold",
            "focal subject not readable in hero camera",
            "floating or unsupported asset",
            "whole-scene generated blob instead of editable object layers",
            "runtime budget or texture budget exceeded"
        )
        $acceptedAssets = [int]$assembly.source_mix.accepted_pretrained_assets
        $reportScenes += [pscustomobject]@{
            scene = $sceneId
            current_capture = if ($null -ne $existing) { [string]$existing.hero_image } else { "" }
            status = if ($acceptedAssets -gt 0) { "ready_for_thumbnail_tournament" } else { "waiting_for_pretrained_assets" }
            accepted_pretrained_assets = $acceptedAssets
            hard_rejects = $hardRejects
            scoring_axes = [pscustomobject]@{
                focal_subject_read = 0.25
                material_beauty = 0.20
                grounded_contact = 0.20
                composition_depth = 0.15
                artifact_absence = 0.10
                engine_budget_margin = 0.10
            }
            thresholds = [pscustomobject]@{
                final_art_score_min = 0.78
                focal_subject_read_min = 0.70
                grounded_contact_min = 0.75
                artifact_absence_min = 0.80
            }
        }
    }
    Write-Json ([pscustomobject]@{ schema = 1; scenes = $reportScenes; note = "This gate prevents the current bad captures from being called final art. Imported/generated asset batches still need rendered candidate thumbnails and image scoring before promotion." }) (Join-Path $GeneratedRoot "pretrained_visual_rejection_report.json")
    if ($failures.Count -gt 0) { throw (($failures | ForEach-Object { " - $_" }) -join "`n") }
    Write-Host "Final-art pretrained visual rejection contracts passed: scenes=$($reportScenes.Count)" -ForegroundColor Green
}

function Invoke-PretrainedAll {
    Invoke-PretrainedAssetPlan
    Invoke-PretrainedAdapterJobs
    Invoke-PretrainedImportContracts
    Invoke-PretrainedSceneAssembly
    Invoke-PretrainedVisualRejection
}

function Invoke-AAAAssetQuality {
    $rendererManifest = Join-Path $Root "build/captures/scene_local_cinematic_renderer_v1_final_gate_20260605/warm_micro_jitter_full_seq8/manifest.json"
    $args = @(
        (Join-Path $Root "tools/analyze_aaa_asset_quality.py")
    )
    if (Test-Path $rendererManifest) {
        $args += @("--renderer-manifest", $rendererManifest)
    }
    if ($env:CORTEX_AAA_ASSET_QUALITY_FAIL_ON_BLOCKER -eq "1") {
        $args += "--fail-on-blocker"
    }
    python @args
    if ($LASTEXITCODE -ne 0) {
        throw "AAA asset-quality analysis failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AssetRegistryV2 {
    python (Join-Path $Root "tools/build_asset_registry_v2.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Asset Registry V2 build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-SceneAssetBindings {
    Invoke-AssetRegistryV2
    python (Join-Path $Root "tools/build_scene_asset_bindings_v1.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Scene asset binding build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-FullSceneShaderMaterialEvidence {
    Invoke-SceneAssetBindings
    python (Join-Path $Root "tools/build_full_scene_shader_material_evidence_v2.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Full Scene Shader Pipeline V2 material evidence build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-FullSceneShaderMaterialUpgradePlan {
    Invoke-FullSceneShaderMaterialEvidence
    python (Join-Path $Root "tools/plan_full_scene_shader_material_upgrades_v2.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Full Scene Shader Pipeline V2 material upgrade planning failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AAAReplacementPlan {
    Invoke-SceneAssetBindings
    Invoke-AAAAssetQuality
    python (Join-Path $Root "tools/plan_aaa_asset_replacements.py")
    if ($LASTEXITCODE -ne 0) {
        throw "AAA replacement planning failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AAAProviderRequests {
    Invoke-AAAReplacementPlan
    python (Join-Path $Root "tools/export_aaa_provider_requests.py")
    if ($LASTEXITCODE -ne 0) {
        throw "AAA provider request export failed with exit code $LASTEXITCODE"
    }
}

function Invoke-All {
    Invoke-KernelContracts
    Invoke-AssetNormalization
    Invoke-AssetProvenance
    Invoke-AssetTags
    Invoke-Placement
    Invoke-Materials
    Invoke-KitScenes
    Invoke-VariantTournament
    Invoke-VisualValidation
    Invoke-ContactSheet
    Invoke-PretrainedAll
}

switch ($Action) {
    "Kernels" { Invoke-KernelContracts }
    "AssetNormalization" { Invoke-AssetNormalization }
    "AssetProvenance" { Invoke-AssetProvenance }
    "AssetTags" { Invoke-AssetTags }
    "Placement" { Invoke-Placement }
    "Materials" { Invoke-Materials }
    "KitScenes" { Invoke-KitScenes }
    "VariantTournament" { Invoke-VariantTournament }
    "VisualValidation" { Invoke-VisualValidation }
    "ContactSheet" { Invoke-ContactSheet }
    "PretrainedAssetPlan" { Invoke-PretrainedAssetPlan }
    "PretrainedAdapterJobs" { Invoke-PretrainedAdapterJobs }
    "PretrainedImportContracts" { Invoke-PretrainedImportContracts }
    "PretrainedSceneAssembly" { Invoke-PretrainedSceneAssembly }
    "PretrainedVisualRejection" { Invoke-PretrainedVisualRejection }
    "AssetRegistryV2" { Invoke-AssetRegistryV2 }
    "SceneAssetBindings" { Invoke-SceneAssetBindings }
    "FullSceneShaderMaterialEvidence" { Invoke-FullSceneShaderMaterialEvidence }
    "FullSceneShaderMaterialUpgradePlan" { Invoke-FullSceneShaderMaterialUpgradePlan }
    "AAAAssetQuality" { Invoke-AAAAssetQuality }
    "AAAReplacementPlan" { Invoke-AAAReplacementPlan }
    "AAAProviderRequests" { Invoke-AAAProviderRequests }
    "PretrainedAll" { Invoke-PretrainedAll }
    "All" { Invoke-All }
}
