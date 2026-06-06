param(
    [string]$OutputRoot = "build/captures/scene_local_cinematic_renderer_v1",
    [int]$SmokeFrames = 140,
    [int]$CaptureFrame = 60,
    [int]$CaptureSequenceCount = 1,
    [ValidateSet("static", "mouse_jitter", "camera_sweep")]
    [string]$StabilityMotionMode = "static",
    [int]$MotionFrames = 120,
    [double]$MotionLookAmplitude = 0.025,
    [double]$MotionSideAmplitude = 0.08,
    [double]$MotionForwardAmplitude = 0.03,
    [double]$MotionLiftAmplitude = 0.0,
    [double]$MotionLookCycles = 8.0,
    [double]$FixedDeltaTime = 0.008333333,
    [switch]$NoBuild,
    [switch]$SkipGallery,
    [switch]$OnlyGallery,
    [switch]$SkipOwnerAnalysis,
    [switch]$SkipMaterialAnalysis,
    [switch]$SkipStabilityAnalysis,
    [switch]$SkipVisualQualityAnalysis,
    [switch]$VisualQualityFailOnReview,
    [switch]$StressSceneOnly,
    [string]$FamilyFilter = "",
    [string]$ViewFilter = "",
    [string]$StressSceneFilter = "",
    [string]$KitchenSeed = "",
    [string]$OfficeSeed = "",
    [string]$GymSeed = "",
    [string]$ConcertSeed = "",
    [string]$RedRoomSeed = "",
    [string]$StadiumSeed = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$outRootAbs = Join-Path $root $OutputRoot

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe"
}

New-Item -ItemType Directory -Force -Path $outRootAbs | Out-Null

$views = @(
    [pscustomobject]@{ Name = "beauty"; DebugView = $null },
    [pscustomobject]@{ Name = "candidate_beauty_v3"; DebugView = $null; CandidateBeautyV3 = $true },
    [pscustomobject]@{ Name = "roughness"; DebugView = 2 },
    [pscustomobject]@{ Name = "metallic"; DebugView = 3 },
    [pscustomobject]@{ Name = "surface_class"; DebugView = 41 },
    [pscustomobject]@{ Name = "surface_policy"; DebugView = 47 },
    [pscustomobject]@{ Name = "reflection_probe_weight"; DebugView = 42 },
    [pscustomobject]@{ Name = "reflection_owner"; DebugView = 46 },
    [pscustomobject]@{ Name = "reflection_source_weights"; DebugView = 56 },
    [pscustomobject]@{ Name = "reflection_source_authority"; DebugView = 60 },
    [pscustomobject]@{ Name = "local_reflection_radiance"; DebugView = 61 },
    [pscustomobject]@{ Name = "reflection_stability_policy"; DebugView = 57 },
    [pscustomobject]@{ Name = "reflection_resolver_candidate"; DebugView = 58 },
    [pscustomobject]@{ Name = "reflection_resolver_candidate_delta"; DebugView = 59 },
    [pscustomobject]@{ Name = "shadow_factor"; DebugView = 43 },
    [pscustomobject]@{ Name = "direct_light"; DebugView = 44 },
    [pscustomobject]@{ Name = "direct_light_unshadowed"; DebugView = 54 },
    [pscustomobject]@{ Name = "direct_light_shadow_loss"; DebugView = 55 },
    [pscustomobject]@{ Name = "ambient_ibl"; DebugView = 45 },
    [pscustomobject]@{ Name = "v3_direct_lighting"; DebugView = 62 },
    [pscustomobject]@{ Name = "v3_direct_lighting_unshadowed"; DebugView = 63 },
    [pscustomobject]@{ Name = "v3_shadow_visibility"; DebugView = 64 },
    [pscustomobject]@{ Name = "v3_shadow_loss"; DebugView = 65 },
    [pscustomobject]@{ Name = "v3_indirect_lighting"; DebugView = 66 },
    [pscustomobject]@{ Name = "candidate_hdr_scene_color"; DebugView = 67; CandidateBeautyV3 = $true },
    [pscustomobject]@{ Name = "reflection_radiance"; DebugView = 68 },
    [pscustomobject]@{ Name = "reflection_confidence"; DebugView = 69 },
    [pscustomobject]@{ Name = "reflection_source_id"; DebugView = 70 },
    [pscustomobject]@{ Name = "reflection_rejected_source_mask"; DebugView = 71 },
    [pscustomobject]@{ Name = "reflection_temporal_delta"; DebugView = 72 },
    [pscustomobject]@{ Name = "reflection_ssr_source_signal"; DebugView = 73 },
    [pscustomobject]@{ Name = "reflection_rt_source_signal"; DebugView = 74 },
    [pscustomobject]@{ Name = "material_family"; DebugView = 50 },
    [pscustomobject]@{ Name = "reflection_policy"; DebugView = 51 },
    [pscustomobject]@{ Name = "temporal_policy"; DebugView = 52 },
    [pscustomobject]@{ Name = "post_sensitivity"; DebugView = 53 },
    [pscustomobject]@{ Name = "material_id"; DebugView = 48 },
    [pscustomobject]@{ Name = "object_id"; DebugView = 49 },
    [pscustomobject]@{ Name = "taa_blend"; DebugView = 25 }
)

function Split-FilterSet([string]$Filter) {
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    if ([string]::IsNullOrWhiteSpace($Filter)) {
        return $set
    }
    foreach ($item in ($Filter -split ",")) {
        $trimmed = $item.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            [void]$set.Add($trimmed)
        }
    }
    return $set
}

function Get-SafePacketName([string]$Name) {
    $safe = ($Name.ToLowerInvariant() -replace '[^a-z0-9]+', '_').Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "unnamed"
    }
    return $safe
}

function Get-ShowcaseSceneMap {
    $path = Join-Path $script:root "assets/config/showcase_scenes.json"
    $scenes = @{}
    if (-not (Test-Path $path)) {
        return $scenes
    }
    $doc = Get-Content $path -Raw | ConvertFrom-Json
    foreach ($scene in @($doc.scenes)) {
        $sceneId = [string]$scene.id
        if ([string]::IsNullOrWhiteSpace($sceneId)) {
            continue
        }
        $bookmarks = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
        foreach ($bookmark in @($scene.camera_bookmarks)) {
            $bookmarkId = [string]$bookmark.id
            if (-not [string]::IsNullOrWhiteSpace($bookmarkId)) {
                [void]$bookmarks.Add($bookmarkId)
            }
        }
        $scenes[$sceneId] = $bookmarks
    }
    return $scenes
}

function Parse-StressSceneTargets([string]$Filter) {
    $targets = New-Object System.Collections.Generic.List[object]
    if ([string]::IsNullOrWhiteSpace($Filter)) {
        return $targets
    }

    $showcaseScenes = Get-ShowcaseSceneMap
    foreach ($item in ($Filter -split ",")) {
        $trimmed = $item.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) {
            continue
        }
        $parts = @($trimmed -split ":", 3)
        if ($parts.Count -lt 2 -or [string]::IsNullOrWhiteSpace($parts[0]) -or [string]::IsNullOrWhiteSpace($parts[1])) {
            throw "StressSceneFilter entry '$trimmed' must use 'scene:camera_bookmark'."
        }
        $scene = $parts[0].Trim()
        $bookmark = $parts[1].Trim()
        if ($showcaseScenes.Count -gt 0) {
            if (-not $showcaseScenes.ContainsKey($scene)) {
                throw "StressSceneFilter scene '$scene' is not present in assets/config/showcase_scenes.json."
            }
            if (-not $showcaseScenes[$scene].Contains($bookmark)) {
                throw "StressSceneFilter bookmark '$bookmark' is not present on scene '$scene'."
            }
        }
        $family = "stress_{0}_{1}" -f (Get-SafePacketName $scene), (Get-SafePacketName $bookmark)
        $targets.Add([pscustomobject]@{
            Family = $family
            Scene = $scene
            CameraBookmark = $bookmark
            Seed = ""
        }) | Out-Null
    }
    return $targets
}

$familyFilterSet = Split-FilterSet $FamilyFilter
$viewFilterSet = Split-FilterSet $ViewFilter
if ($viewFilterSet.Count -gt 0) {
    $knownViews = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($view in $views) {
        [void]$knownViews.Add($view.Name)
    }
    foreach ($requestedView in $viewFilterSet) {
        if (-not $knownViews.Contains($requestedView)) {
            throw "Unknown ViewFilter entry '$requestedView'. Known views: $($knownViews -join ', ')"
        }
    }
    $views = @($views | Where-Object { $viewFilterSet.Contains($_.Name) })
}

$oldEnv = @{
    CORTEX_LOG_DIR = $env:CORTEX_LOG_DIR
    CORTEX_CAPTURE_VISUAL_VALIDATION = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
    CORTEX_VISUAL_VALIDATION_MIN_FRAME = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
    CORTEX_VISUAL_VALIDATION_SEQUENCE_COUNT = $env:CORTEX_VISUAL_VALIDATION_SEQUENCE_COUNT
    CORTEX_DISABLE_USER_GRAPHICS_SETTINGS = $env:CORTEX_DISABLE_USER_GRAPHICS_SETTINGS
    CORTEX_DEBUG_VIEW = $env:CORTEX_DEBUG_VIEW
    CORTEX_MODEL_AUTHORED_SCENE_SEED = $env:CORTEX_MODEL_AUTHORED_SCENE_SEED
    CORTEX_FIXED_DELTA_TIME = $env:CORTEX_FIXED_DELTA_TIME
    CORTEX_CAMERA_MOTION_FRAMES = $env:CORTEX_CAMERA_MOTION_FRAMES
    CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE
    CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE
    CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE
    CORTEX_CAMERA_MOTION_LOOK_CYCLES = $env:CORTEX_CAMERA_MOTION_LOOK_CYCLES
    CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE = $env:CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE
    CORTEX_CAMERA_MOUSE_JITTER_FRAMES = $env:CORTEX_CAMERA_MOUSE_JITTER_FRAMES
    CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE = $env:CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE
    CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE = $env:CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE
    CORTEX_CAMERA_MOUSE_JITTER_CYCLES = $env:CORTEX_CAMERA_MOUSE_JITTER_CYCLES
    CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3 = $env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3
}

function Restore-Env {
    foreach ($key in $script:oldEnv.Keys) {
        if ($null -eq $script:oldEnv[$key]) {
            Remove-Item "Env:\$key" -ErrorAction SilentlyContinue
        } else {
            Set-Item "Env:\$key" $script:oldEnv[$key]
        }
    }
}

function Resolve-SeedPath([string]$seed) {
    if ([string]::IsNullOrWhiteSpace($seed)) {
        return ""
    }
    $path = [System.IO.Path]::GetFullPath((Join-Path $script:root $seed))
    if ([System.IO.Path]::IsPathRooted($seed)) {
        $path = $seed
    }
    if (-not (Test-Path $path)) {
        throw "Seed path does not exist: $seed"
    }
    return $path
}

function Resolve-FirstExistingSeed([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        $path = [System.IO.Path]::GetFullPath((Join-Path $script:root $candidate))
        if (Test-Path $path) {
            return $path
        }
    }
    return ""
}

function Resolve-FamilySeed([string]$Family, [string]$ExplicitSeed) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitSeed)) {
        return Resolve-SeedPath $ExplicitSeed
    }
    switch ($Family) {
        "kitchen" {
            return Resolve-FirstExistingSeed @(
                "assets/scenes/model_authored/scene_authoring_admitted_v1/admitted_kitchen/scene_seed.json",
                "assets/scenes/model_authored/scene_graph_kernel_v1/home_kitchen_lantern_v50_sgk/scene_seed.json",
                "assets/scenes/model_authored/scene_authoring_closed_loop_v1/novel_kitchen/scene_seed.json"
            )
        }
        "office" {
            return Resolve-FirstExistingSeed @(
                "assets/scenes/model_authored/scene_authoring_admitted_v1/admitted_office/scene_seed.json",
                "assets/scenes/model_authored/scene_graph_kernel_v1/home_office_evening_v50_sgk/scene_seed.json",
                "assets/scenes/model_authored/scene_authoring_closed_loop_v1/novel_office/scene_seed.json"
            )
        }
        "gym" {
            return Resolve-FirstExistingSeed @(
                "assets/scenes/model_authored/scene_authoring_admitted_v1/admitted_basketball_gym/scene_seed.json",
                "assets/scenes/model_authored/scene_graph_kernel_v1/basketball_gym_v387_sgk/scene_seed.json",
                "assets/scenes/model_authored/scene_authoring_closed_loop_v1/novel_basketball_gym/scene_seed.json"
            )
        }
        "concert" {
            return Resolve-FirstExistingSeed @(
                "assets/scenes/model_authored/scene_authoring_admitted_v1/admitted_concert/scene_seed.json",
                "assets/scenes/model_authored/scene_graph_kernel_v1/neon_streamer_concert_v375_sgk/scene_seed.json",
                "assets/scenes/model_authored/scene_authoring_closed_loop_v1/novel_concert/scene_seed.json"
            )
        }
        "red_room" {
            return Resolve-FirstExistingSeed @(
                "assets/scenes/model_authored/architecture_lighting_planner_v24_20260515/red_light_room/scene_seed.json",
                "assets/scenes/model_authored/asset_density_v44_20260515/red_light_room/scene_seed.json"
            )
        }
        "stadium" {
            return Resolve-FirstExistingSeed @(
                "assets/scenes/model_authored/architecture_lighting_planner_v24_20260515/stadium_night_match/scene_seed.json"
            )
        }
        default {
            return ""
        }
    }
}

function Set-PacketMotionEnv {
    if ($script:StabilityMotionMode -eq "mouse_jitter") {
        Remove-Item Env:\CORTEX_CAMERA_MOTION_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_CYCLES -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE -ErrorAction SilentlyContinue
        $env:CORTEX_FIXED_DELTA_TIME = [string]$script:FixedDeltaTime
        $env:CORTEX_CAMERA_MOUSE_JITTER_FRAMES = [string]$script:MotionFrames
        $env:CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE = [string]$script:MotionLookAmplitude
        $env:CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE = "0.0"
        $env:CORTEX_CAMERA_MOUSE_JITTER_CYCLES = [string]$script:MotionLookCycles
        return
    }

    if ($script:StabilityMotionMode -eq "camera_sweep") {
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_FRAMES -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_CYCLES -ErrorAction SilentlyContinue
        $env:CORTEX_FIXED_DELTA_TIME = [string]$script:FixedDeltaTime
        $env:CORTEX_CAMERA_MOTION_FRAMES = [string]$script:MotionFrames
        $env:CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE = [string]$script:MotionSideAmplitude
        $env:CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE = [string]$script:MotionForwardAmplitude
        $env:CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE = [string]$script:MotionLookAmplitude
        $env:CORTEX_CAMERA_MOTION_LOOK_CYCLES = [string]$script:MotionLookCycles
        $env:CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE = [string]$script:MotionLiftAmplitude
        return
    }

    Remove-Item Env:\CORTEX_FIXED_DELTA_TIME -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_FRAMES -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_SIDE_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_FORWARD_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_LOOK_CYCLES -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOTION_LIFT_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_FRAMES -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_YAW_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_PITCH_AMPLITUDE -ErrorAction SilentlyContinue
    Remove-Item Env:\CORTEX_CAMERA_MOUSE_JITTER_CYCLES -ErrorAction SilentlyContinue
}

function Invoke-PacketCapture([string]$Family,
                              [string]$Scene,
                              [string]$CameraBookmark,
                              [string]$SeedPath,
                              [object]$View) {
    $viewDir = Join-Path $script:outRootAbs (Join-Path $Family $View.Name)
    New-Item -ItemType Directory -Force -Path $viewDir | Out-Null
    Remove-Item -Force -ErrorAction SilentlyContinue `
        (Join-Path $viewDir "visual_validation_frame_*.bmp"),
        (Join-Path $viewDir "frame_report_last.json"),
        (Join-Path $viewDir "frame_report_shutdown.json"),
        (Join-Path $viewDir "engine_stdout.txt")

    $env:CORTEX_LOG_DIR = $viewDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = [string]$script:CaptureFrame
    $env:CORTEX_VISUAL_VALIDATION_SEQUENCE_COUNT = [string]$script:CaptureSequenceCount
    $env:CORTEX_DISABLE_USER_GRAPHICS_SETTINGS = "1"
    if ($null -eq $View.DebugView) {
        Remove-Item Env:\CORTEX_DEBUG_VIEW -ErrorAction SilentlyContinue
    } else {
        $env:CORTEX_DEBUG_VIEW = [string]$View.DebugView
    }
    if ($View.PSObject.Properties.Name -contains "CandidateBeautyV3" -and $View.CandidateBeautyV3) {
        $env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3 = "1"
    } else {
        Remove-Item Env:\CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3 -ErrorAction SilentlyContinue
    }
    if ([string]::IsNullOrWhiteSpace($SeedPath)) {
        Remove-Item Env:\CORTEX_MODEL_AUTHORED_SCENE_SEED -ErrorAction SilentlyContinue
    } else {
        $env:CORTEX_MODEL_AUTHORED_SCENE_SEED = $SeedPath
    }
    Set-PacketMotionEnv

    $args = @(
        "--scene", $Scene,
        "--mode=default",
        "--no-llm",
        "--no-dreamer",
        "--no-launcher",
        "--smoke-frames=$script:SmokeFrames"
    )
    if (-not [string]::IsNullOrWhiteSpace($CameraBookmark)) {
        $args += @("--camera-bookmark", $CameraBookmark)
    }

    Push-Location (Split-Path -Parent $script:exe)
    try {
        $output = & $script:exe @args 2>&1
        $exitCode = $LASTEXITCODE
        $output | Set-Content -Encoding UTF8 (Join-Path $viewDir "engine_stdout.txt")
    } finally {
        Pop-Location
    }

    $captures = @(Get-ChildItem -Path $viewDir -Filter "visual_validation_frame_*.bmp" |
        Sort-Object Name)
    $capture = $captures | Select-Object -First 1
    $reportPath = Join-Path $viewDir "frame_report_shutdown.json"
    if (-not (Test-Path $reportPath)) {
        $reportPath = Join-Path $viewDir "frame_report_last.json"
    }
    $sceneVisualContract = $null
    $contractWarnings = @()
    if (Test-Path $reportPath) {
        try {
            $report = Get-Content $reportPath -Raw | ConvertFrom-Json
            $sceneVisualContract = $report.frame_contract.scene_visual_contract
            $contractWarnings = @($report.frame_contract.warnings)
        } catch {
            $contractWarnings = @("packet_report_parse_failed:$($_.Exception.Message)")
        }
    }

    return [pscustomobject]@{
        family = $Family
        view = $View.Name
        debug_view = $View.DebugView
        scene = $Scene
        camera_bookmark = $CameraBookmark
        seed = $SeedPath
        exit_code = $exitCode
        capture = if ($capture) { $capture.FullName } else { "" }
        capture_sequence = @($captures | ForEach-Object { $_.FullName })
        stability_motion_mode = $script:StabilityMotionMode
        motion_frames = $script:MotionFrames
        motion_look_amplitude = $script:MotionLookAmplitude
        motion_side_amplitude = $script:MotionSideAmplitude
        motion_forward_amplitude = $script:MotionForwardAmplitude
        motion_lift_amplitude = $script:MotionLiftAmplitude
        motion_look_cycles = $script:MotionLookCycles
        fixed_delta_time = $script:FixedDeltaTime
        report = $reportPath
        scene_visual_contract = $sceneVisualContract
        frame_contract_warnings = $contractWarnings
        log_dir = $viewDir
        passed = ($exitCode -eq 0 -and $captures.Count -ge [Math]::Max(1, $script:CaptureSequenceCount))
    }
}

$families = New-Object System.Collections.Generic.List[object]
$stressTargets = Parse-StressSceneTargets $StressSceneFilter
if ($familyFilterSet.Count -gt 0) {
    $knownFamilies = @("gallery", "kitchen", "office", "gym", "concert", "red_room", "stadium")
    foreach ($requestedFamily in $familyFilterSet) {
        if ($knownFamilies -notcontains $requestedFamily.ToLowerInvariant()) {
            throw "Unknown FamilyFilter entry '$requestedFamily'. Known families: $($knownFamilies -join ', ')"
        }
    }
}
$includeGallery = (-not $StressSceneOnly) -and
    (-not $SkipGallery) -and
    ($familyFilterSet.Count -eq 0 -or $familyFilterSet.Contains("gallery"))
$includeModelFamilies = (-not $StressSceneOnly) -and (-not $OnlyGallery)
if ($familyFilterSet.Count -gt 0) {
    $includeModelFamilies = $includeModelFamilies -and (
        $familyFilterSet.Contains("kitchen") -or
        $familyFilterSet.Contains("office") -or
        $familyFilterSet.Contains("gym") -or
        $familyFilterSet.Contains("concert") -or
        $familyFilterSet.Contains("red_room") -or
        $familyFilterSet.Contains("stadium"))
}

if ($includeGallery) {
    $families.Add([pscustomobject]@{
        Family = "gallery"
        Scene = "rt_showcase"
        CameraBookmark = "hero"
        Seed = ""
    }) | Out-Null
}
$resolvedSeeds = [ordered]@{}
if ($includeModelFamilies) {
    foreach ($entry in @(
        [pscustomobject]@{ Family = "kitchen"; ExplicitSeed = $KitchenSeed },
        [pscustomobject]@{ Family = "office"; ExplicitSeed = $OfficeSeed },
        [pscustomobject]@{ Family = "gym"; ExplicitSeed = $GymSeed },
        [pscustomobject]@{ Family = "concert"; ExplicitSeed = $ConcertSeed },
        [pscustomobject]@{ Family = "red_room"; ExplicitSeed = $RedRoomSeed },
        [pscustomobject]@{ Family = "stadium"; ExplicitSeed = $StadiumSeed }
    )) {
        if ($familyFilterSet.Count -gt 0 -and -not $familyFilterSet.Contains($entry.Family)) {
            $resolvedSeeds[$entry.Family] = ""
            continue
        }
        $seed = Resolve-FamilySeed $entry.Family $entry.ExplicitSeed
        $resolvedSeeds[$entry.Family] = $seed
        if (-not [string]::IsNullOrWhiteSpace($seed)) {
            $families.Add([pscustomobject]@{
                Family = $entry.Family
                Scene = "model_authored_scene"
                CameraBookmark = ""
                Seed = $seed
            }) | Out-Null
        }
    }
} else {
    foreach ($key in @("kitchen", "office", "gym", "concert", "red_room", "stadium")) {
        $resolvedSeeds[$key] = ""
    }
}
foreach ($target in $stressTargets) {
    $families.Add($target) | Out-Null
}

if ($families.Count -eq 0) {
    throw "No families or stress scenes selected. Check SkipGallery, OnlyGallery, FamilyFilter, and StressSceneFilter."
}
if ($views.Count -eq 0) {
    throw "No views selected. Check ViewFilter."
}

$results = New-Object System.Collections.Generic.List[object]
try {
    foreach ($family in $families) {
        foreach ($view in $views) {
            $results.Add((Invoke-PacketCapture `
                -Family $family.Family `
                -Scene $family.Scene `
                -CameraBookmark $family.CameraBookmark `
                -SeedPath $family.Seed `
                -View $view)) | Out-Null
        }
    }
} finally {
    Restore-Env
}

$manifest = [ordered]@{
    schema = "cortex.scene_local_cinematic_renderer_v1.packet_manifest"
    output_root = $outRootAbs
    smoke_frames = $SmokeFrames
    capture_frame = $CaptureFrame
    capture_sequence_count = $CaptureSequenceCount
    stability_motion_mode = $StabilityMotionMode
    motion_frames = $MotionFrames
    motion_look_amplitude = $MotionLookAmplitude
    motion_side_amplitude = $MotionSideAmplitude
    motion_forward_amplitude = $MotionForwardAmplitude
    motion_lift_amplitude = $MotionLiftAmplitude
    motion_look_cycles = $MotionLookCycles
    fixed_delta_time = $FixedDeltaTime
    family_filter = $FamilyFilter
    view_filter = $ViewFilter
    stress_scene_filter = $StressSceneFilter
    requested_stress_scene_count = $stressTargets.Count
    requested_family_count = $families.Count
    captured_view_count = $results.Count
    resolved_seeds = $resolvedSeeds
    results = $results
    missing_seed_families = @(
        foreach ($key in @("kitchen", "office", "gym", "concert", "red_room", "stadium")) {
            if ([string]::IsNullOrWhiteSpace($resolvedSeeds[$key])) { $key }
        }
    )
}

$manifestPath = Join-Path $outRootAbs "manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $manifestPath

$ownerAnalysisExitCode = 0
if (-not $SkipOwnerAnalysis) {
    $ownerAnalyzer = Join-Path $root "tools/analyze_scene_local_reflection_owner.py"
    if (-not (Test-Path $ownerAnalyzer)) {
        throw "Reflection-owner analyzer missing: $ownerAnalyzer"
    }
    $ownerAnalysisOutput = & python $ownerAnalyzer --manifest $manifestPath --write-manifest 2>&1
    $ownerAnalysisExitCode = $LASTEXITCODE
    $ownerAnalysisOutput | Set-Content -Encoding UTF8 (Join-Path $outRootAbs "reflection_owner_analysis_stdout.txt")
}

$materialAnalysisExitCode = 0
if (-not $SkipMaterialAnalysis) {
    $materialAnalyzer = Join-Path $root "tools/analyze_scene_local_material_classes.py"
    if (-not (Test-Path $materialAnalyzer)) {
        throw "Material-class analyzer missing: $materialAnalyzer"
    }
    $materialAnalysisOutput = & python $materialAnalyzer --manifest $manifestPath --write-manifest 2>&1
    $materialAnalysisExitCode = $LASTEXITCODE
    $materialAnalysisOutput | Set-Content -Encoding UTF8 (Join-Path $outRootAbs "material_class_analysis_stdout.txt")
}

$stabilityAnalysisExitCode = 0
if (-not $SkipStabilityAnalysis -and $CaptureSequenceCount -ge 2) {
    $stabilityAnalyzer = Join-Path $root "tools/analyze_scene_local_packet_stability.py"
    if (-not (Test-Path $stabilityAnalyzer)) {
        throw "Packet stability analyzer missing: $stabilityAnalyzer"
    }
    $stabilityArgs = @("--manifest", $manifestPath, "--write-manifest")
    if ($StabilityMotionMode -eq "mouse_jitter") {
        $stabilityArgs += @(
            "--max-mean-abs-luma-delta", "40.0",
            "--max-changed-pixel-ratio", "0.70",
            "--max-large-changed-pixel-ratio", "0.35"
        )
    }
    $stabilityAnalysisOutput = & python $stabilityAnalyzer @stabilityArgs 2>&1
    $stabilityAnalysisExitCode = $LASTEXITCODE
    $stabilityAnalysisOutput | Set-Content -Encoding UTF8 (Join-Path $outRootAbs "packet_stability_analysis_stdout.txt")
}

$visualQualityAnalysisExitCode = 0
if (-not $SkipVisualQualityAnalysis) {
    $visualQualityAnalyzer = Join-Path $root "tools/analyze_scene_local_visual_quality.py"
    if (-not (Test-Path $visualQualityAnalyzer)) {
        throw "Visual-quality analyzer missing: $visualQualityAnalyzer"
    }
    $visualQualityArgs = @("--manifest", $manifestPath, "--write-manifest")
    if ($VisualQualityFailOnReview) {
        $visualQualityArgs += "--fail-on-review"
    }
    $visualQualityAnalysisOutput = & python $visualQualityAnalyzer @visualQualityArgs 2>&1
    $visualQualityAnalysisExitCode = $LASTEXITCODE
    $visualQualityAnalysisOutput | Set-Content -Encoding UTF8 (Join-Path $outRootAbs "visual_quality_analysis_stdout.txt")
}

$failed = @($results | Where-Object { -not $_.passed })
if ($failed.Count -gt 0) {
    Write-Host "Scene-local cinematic renderer packet run failed:" -ForegroundColor Red
    $failed | Format-Table family,view,exit_code,log_dir -AutoSize
    Write-Host "manifest=$manifestPath"
    exit 1
}
if ($ownerAnalysisExitCode -ne 0) {
    Write-Host "Scene-local cinematic renderer reflection-owner analysis failed:" -ForegroundColor Red
    Get-Content (Join-Path $outRootAbs "reflection_owner_analysis_stdout.txt") -ErrorAction SilentlyContinue
    Write-Host "manifest=$manifestPath"
    exit $ownerAnalysisExitCode
}
if ($materialAnalysisExitCode -ne 0) {
    Write-Host "Scene-local cinematic renderer material-class analysis failed:" -ForegroundColor Red
    Get-Content (Join-Path $outRootAbs "material_class_analysis_stdout.txt") -ErrorAction SilentlyContinue
    Write-Host "manifest=$manifestPath"
    exit $materialAnalysisExitCode
}
if ($stabilityAnalysisExitCode -ne 0) {
    Write-Host "Scene-local cinematic renderer packet stability analysis failed:" -ForegroundColor Red
    Get-Content (Join-Path $outRootAbs "packet_stability_analysis_stdout.txt") -ErrorAction SilentlyContinue
    Write-Host "manifest=$manifestPath"
    exit $stabilityAnalysisExitCode
}
if ($visualQualityAnalysisExitCode -ne 0) {
    Write-Host "Scene-local cinematic renderer visual-quality analysis failed:" -ForegroundColor Red
    Get-Content (Join-Path $outRootAbs "visual_quality_analysis_stdout.txt") -ErrorAction SilentlyContinue
    Write-Host "manifest=$manifestPath"
    exit $visualQualityAnalysisExitCode
}

Write-Host "Scene-local cinematic renderer packet run passed."
Write-Host "manifest=$manifestPath"
