param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v3_promotion_suite",
    [string]$Scenarios = "reflection_static,reflection_mouse_jitter,enclosed_static,enclosed_mouse_jitter,enclosed_camera_sweep,heavy_light_sweep",
    [string]$RequiredFamilies = "stress_rt_showcase_reflection_closeup,gallery,kitchen,office,gym,concert,red_room,stadium",
    [string]$RequiredMotionModes = "static,mouse_jitter,camera_sweep,light_sweep",
    [ValidateSet("promotion_core", "full")]
    [string]$ViewProfile = "promotion_core",
    [string]$ViewFilter = "",
    [int]$SmokeFrames = 30,
    [int]$CaptureFrame = 15,
    [int]$CaptureSequenceCount = 2,
    [int]$MotionWarmupCaptureFrame = 60,
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers,
    [switch]$ContinueOnPacketFailure,
    [switch]$SummarizeExisting,
    [switch]$PlanOnly
)

# run_full_scene_shader_pipeline_v3_promotion_suite.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$packetRunner = Join-Path $root "tools/run_full_scene_shader_pipeline_v3_packet.ps1"
$matrixAnalyzer = Join-Path $root "tools/build_full_scene_shader_v3_matrix_decision.py"
$outputPath = Join-Path $root $OutputRoot
$suiteStatusJson = Join-Path $outputPath "suite_packet_status.json"
$suiteStatusMd = Join-Path $outputPath "suite_packet_status.md"
$matrixJson = Join-Path $outputPath "v3_matrix_decision.json"
$matrixMd = Join-Path $outputPath "v3_matrix_decision.md"

function Split-Csv([string]$Value) {
    $items = @()
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $items
    }
    foreach ($item in ($Value -split ",")) {
        $trimmed = $item.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $items += $trimmed
        }
    }
    return $items
}

function Join-ViewPack([string[]]$Views) {
    $ordered = New-Object System.Collections.Generic.List[string]
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($view in $Views) {
        if (-not [string]::IsNullOrWhiteSpace($view) -and $seen.Add($view)) {
            $ordered.Add($view) | Out-Null
        }
    }
    return ($ordered.ToArray() -join ",")
}

function Get-ViewPacks {
    $material = @(
        "material_base_color",
        "material_normal",
        "material_missing_channel_mask",
        "roughness",
        "metallic",
        "surface_class",
        "surface_policy",
        "material_family",
        "reflection_policy",
        "temporal_policy",
        "post_sensitivity",
        "material_id",
        "object_id"
    )
    $composite = @(
        "beauty",
        "candidate_beauty_v3",
        "candidate_hdr_scene_color",
        "energy_clamp_policy",
        "overbright_diagnostics",
        "composite_contribution_map",
        "legacy_rescue_usage"
    )
    $environment = @(
        "scene_local_environment",
        "ambient_lighting",
        "visible_background",
        "reflection_background",
        "atmosphere"
    )
    $lighting = @(
        "direct_light",
        "direct_light_unshadowed",
        "direct_light_shadow_loss",
        "shadow_factor",
        "ambient_ibl",
        "v3_direct_lighting",
        "v3_direct_lighting_unshadowed",
        "v3_shadow_visibility",
        "v3_shadow_loss",
        "v3_indirect_lighting",
        "v3_lighting_energy_budget",
        "v3_shadow_source_attribution"
    )
    $reflection = @(
        "reflection_probe_weight",
        "reflection_owner",
        "local_reflection_radiance",
        "reflection_stability_policy",
        "reflection_radiance",
        "reflection_confidence",
        "reflection_source_id",
        "reflection_rejected_source_mask",
        "reflection_temporal_delta",
        "reflection_ssr_source_signal",
        "reflection_rt_source_signal",
        "reflection_source_suppression",
        "reflection_history_v3_curr",
        "reflection_history_v3_prev",
        "reflection_history_v3_validity",
        "reflection_history_v3_rejection",
        "reflection_source_authority",
        "reflection_source_weights",
        "reflection_resolver_candidate",
        "reflection_resolver_candidate_delta"
    )
    return @{
        material = $material
        composite = $composite
        environment = $environment
        lighting = $lighting
        reflection = $reflection
        full = Join-ViewPack @($material + $composite + $environment + $lighting + $reflection)
        reflection_core = Join-ViewPack @($material + $composite + $environment + $lighting + $reflection)
        enclosed_core = Join-ViewPack @($material + $composite + $environment + $lighting + @(
            "reflection_owner",
            "local_reflection_radiance",
            "reflection_radiance",
            "reflection_confidence",
            "reflection_source_id",
            "reflection_source_suppression",
            "reflection_temporal_delta"
        ))
        heavy_light_core = Join-ViewPack @($material + $composite + $environment + $lighting + @(
            "reflection_owner",
            "local_reflection_radiance",
            "reflection_radiance",
            "reflection_confidence",
            "reflection_source_id",
            "reflection_source_suppression",
            "reflection_temporal_delta"
        ))
    }
}

function Resolve-ScenarioViewFilter([object]$Scenario, [hashtable]$ViewPacks) {
    if (-not [string]::IsNullOrWhiteSpace($script:ViewFilter)) {
        return $script:ViewFilter
    }
    if ($script:ViewProfile -eq "full") {
        return $ViewPacks.full
    }
    if ($Scenario.view_pack -eq "reflection") {
        return $ViewPacks.reflection_core
    }
    if ($Scenario.view_pack -eq "heavy_light") {
        return $ViewPacks.heavy_light_core
    }
    return $ViewPacks.enclosed_core
}

function Count-FilterItems([string]$Filter) {
    return @(Split-Csv $Filter).Count
}

function Count-FamiliesForScenario([object]$Scenario) {
    if ($Scenario.stress_scene_only) {
        return 1
    }
    return [Math]::Max(1, @(Split-Csv $Scenario.family_filter).Count)
}

function Get-EffectiveCaptureContract([object]$Scenario) {
    $effectiveCaptureFrame = $script:CaptureFrame
    $effectiveSmokeFrames = $script:SmokeFrames
    $reason = "requested"
    $motionSequence = (
        $Scenario.motion_mode -ne "static" -and
        $script:CaptureSequenceCount -gt 1
    )

    if ($motionSequence -and $script:MotionWarmupCaptureFrame -gt $effectiveCaptureFrame) {
        $effectiveCaptureFrame = $script:MotionWarmupCaptureFrame
        $reason = "motion_warmup"
    }

    $requiredSmokeFrames = $effectiveCaptureFrame + [Math]::Max(0, $script:CaptureSequenceCount - 1) + 1
    if ($effectiveSmokeFrames -lt $requiredSmokeFrames) {
        $effectiveSmokeFrames = $requiredSmokeFrames
        if ($reason -eq "requested") {
            $reason = "sequence_extent"
        } else {
            $reason = "$reason+sequence_extent"
        }
    }

    return [pscustomobject]@{
        requested_capture_frame = $script:CaptureFrame
        requested_smoke_frames = $script:SmokeFrames
        effective_capture_frame = $effectiveCaptureFrame
        effective_smoke_frames = $effectiveSmokeFrames
        capture_sequence_count = $script:CaptureSequenceCount
        motion_warmup_capture_frame = $script:MotionWarmupCaptureFrame
        adjusted = ($effectiveCaptureFrame -ne $script:CaptureFrame -or $effectiveSmokeFrames -ne $script:SmokeFrames)
        reason = $reason
    }
}

function New-Scenario([string]$Name, [string]$FamilyFilter, [string]$MotionMode, [string]$StressSceneFilter, [bool]$StressSceneOnly, [bool]$NoStressScene, [string]$ViewPack) {
    return [pscustomobject]@{
        name = $Name
        family_filter = $FamilyFilter
        motion_mode = $MotionMode
        stress_scene_filter = $StressSceneFilter
        stress_scene_only = $StressSceneOnly
        no_stress_scene = $NoStressScene
        view_pack = $ViewPack
        packet_root = (Join-Path $OutputRoot $Name)
    }
}

function Get-ScenarioCatalog {
    $catalog = @{}
    $catalog["reflection_static"] = New-Scenario `
        -Name "reflection_static" `
        -FamilyFilter "" `
        -MotionMode "static" `
        -StressSceneFilter "rt_showcase:reflection_closeup" `
        -StressSceneOnly $true `
        -NoStressScene $false `
        -ViewPack "reflection"
    $catalog["reflection_mouse_jitter"] = New-Scenario `
        -Name "reflection_mouse_jitter" `
        -FamilyFilter "" `
        -MotionMode "mouse_jitter" `
        -StressSceneFilter "rt_showcase:reflection_closeup" `
        -StressSceneOnly $true `
        -NoStressScene $false `
        -ViewPack "reflection"
    $catalog["enclosed_static"] = New-Scenario `
        -Name "enclosed_static" `
        -FamilyFilter "gallery,kitchen,office,gym" `
        -MotionMode "static" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true `
        -ViewPack "enclosed"
    $catalog["enclosed_mouse_jitter"] = New-Scenario `
        -Name "enclosed_mouse_jitter" `
        -FamilyFilter "gallery,kitchen,office,gym" `
        -MotionMode "mouse_jitter" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true `
        -ViewPack "enclosed"
    $catalog["enclosed_camera_sweep"] = New-Scenario `
        -Name "enclosed_camera_sweep" `
        -FamilyFilter "gallery,kitchen,office,gym" `
        -MotionMode "camera_sweep" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true `
        -ViewPack "enclosed"
    $catalog["heavy_static"] = New-Scenario `
        -Name "heavy_static" `
        -FamilyFilter "concert,red_room,stadium" `
        -MotionMode "static" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true `
        -ViewPack "heavy_light"
    $catalog["heavy_light_sweep"] = New-Scenario `
        -Name "heavy_light_sweep" `
        -FamilyFilter "concert,red_room,stadium" `
        -MotionMode "light_sweep" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true `
        -ViewPack "heavy_light"
    return $catalog
}

if (-not (Test-Path $packetRunner)) {
    throw "V3 packet runner missing: $packetRunner"
}
if (-not (Test-Path $matrixAnalyzer)) {
    throw "V3 matrix analyzer missing: $matrixAnalyzer"
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$catalog = Get-ScenarioCatalog
$viewPacks = Get-ViewPacks
$selectedScenarios = @()
foreach ($scenarioName in (Split-Csv $Scenarios)) {
    if (-not $catalog.ContainsKey($scenarioName)) {
        throw "Unknown scenario '$scenarioName'. Known scenarios: $($catalog.Keys -join ', ')"
    }
    $selectedScenarios += $catalog[$scenarioName]
}
if ($selectedScenarios.Count -eq 0) {
    throw "No scenarios selected."
}

$packetRoots = @()
$statusRows = @()
$ranAnyPacket = $false
$estimatedEngineRuns = 0

foreach ($scenario in $selectedScenarios) {
    $packetRoots += $scenario.packet_root
    $packetExit = $null
    $ranPacket = $false
    $continuedAfterFailure = $false
    $packetPath = Join-Path $root $scenario.packet_root
    $manifestPath = Join-Path $packetPath "manifest.json"
    $scenarioViewFilter = Resolve-ScenarioViewFilter $scenario $viewPacks
    $scenarioViewCount = Count-FilterItems $scenarioViewFilter
    $scenarioFamilyCount = Count-FamiliesForScenario $scenario
    $scenarioEstimatedRuns = $scenarioViewCount * $scenarioFamilyCount
    $captureContract = Get-EffectiveCaptureContract $scenario
    $estimatedEngineRuns += $scenarioEstimatedRuns

    if (-not $PlanOnly -and -not $SummarizeExisting) {
        $packetArgs = @(
            "-OutputRoot", $scenario.packet_root,
            "-ViewFilter", $scenarioViewFilter,
            "-SmokeFrames", [string]$captureContract.effective_smoke_frames,
            "-CaptureFrame", [string]$captureContract.effective_capture_frame,
            "-CaptureSequenceCount", [string]$CaptureSequenceCount,
            "-StabilityMotionMode", $scenario.motion_mode
        )
        if (-not [string]::IsNullOrWhiteSpace($scenario.family_filter)) {
            $packetArgs += @("-FamilyFilter", $scenario.family_filter)
        }
        if (-not [string]::IsNullOrWhiteSpace($scenario.stress_scene_filter)) {
            $packetArgs += @("-StressSceneFilter", $scenario.stress_scene_filter)
        }
        if ($scenario.stress_scene_only) {
            $packetArgs += "-StressSceneOnly"
        }
        if ($scenario.no_stress_scene) {
            $packetArgs += "-NoStressScene"
        }
        if ($NoBuild -or $ranAnyPacket) {
            $packetArgs += "-NoBuild"
        }
        if ($SkipSceneAnalyzers) {
            $packetArgs += "-SkipSceneAnalyzers"
        }

        & powershell -NoProfile -ExecutionPolicy Bypass -File $packetRunner @packetArgs
        $packetExit = $LASTEXITCODE
        $ranPacket = $true
        $ranAnyPacket = $true
        $continuedAfterFailure = ($packetExit -ne 0 -and [bool]$ContinueOnPacketFailure)
    } elseif ($SummarizeExisting) {
        $packetExit = if (Test-Path $manifestPath) { 0 } else { 1 }
        $continuedAfterFailure = ($packetExit -ne 0)
    }

    $statusRows += [pscustomobject]@{
        packet_root = $scenario.packet_root
        scenario = $scenario.name
        family_filter = $scenario.family_filter
        motion_mode = $scenario.motion_mode
        stress_scene_filter = $scenario.stress_scene_filter
        stress_scene_only = $scenario.stress_scene_only
        no_stress_scene = $scenario.no_stress_scene
        view_profile = $ViewProfile
        view_pack = $scenario.view_pack
        view_filter = $scenarioViewFilter
        view_count = $scenarioViewCount
        family_count = $scenarioFamilyCount
        estimated_engine_runs = $scenarioEstimatedRuns
        requested_smoke_frames = $captureContract.requested_smoke_frames
        requested_capture_frame = $captureContract.requested_capture_frame
        effective_smoke_frames = $captureContract.effective_smoke_frames
        effective_capture_frame = $captureContract.effective_capture_frame
        capture_sequence_count = $captureContract.capture_sequence_count
        capture_contract_adjusted = $captureContract.adjusted
        capture_contract_reason = $captureContract.reason
        ran_packet = $ranPacket
        exit_code = $packetExit
        continued_after_failure = $continuedAfterFailure
        manifest = $manifestPath
    }

    if ($packetExit -ne $null -and $packetExit -ne 0 -and -not $ContinueOnPacketFailure -and -not $PlanOnly) {
        break
    }
}

$statusDoc = [pscustomobject]@{
    schema = "cortex.full_scene_shader_pipeline_v3.promotion_suite.v1"
    output_root = $outputPath
    scenarios = @($selectedScenarios)
    required_families = @(Split-Csv $RequiredFamilies)
    required_motion_modes = @(Split-Csv $RequiredMotionModes)
    motion_warmup_capture_frame = $MotionWarmupCaptureFrame
    continue_on_packet_failure = [bool]$ContinueOnPacketFailure
    summarize_existing = [bool]$SummarizeExisting
    plan_only = [bool]$PlanOnly
    view_profile = $ViewProfile
    override_view_filter = $ViewFilter
    estimated_engine_runs = $estimatedEngineRuns
    packet_roots = @($packetRoots)
    packet_status = @($statusRows)
}
$statusDoc | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $suiteStatusJson

$statusLines = @(
    "# Full Scene Shader Pipeline V3 Promotion Suite",
    "",
    "Plan only: ``$([bool]$PlanOnly)``",
    "Summarize existing: ``$([bool]$SummarizeExisting)``",
    "Continue on packet failure: ``$([bool]$ContinueOnPacketFailure)``",
    "View profile: ``$ViewProfile``",
    "Motion warmup capture frame: ``$MotionWarmupCaptureFrame``",
    "Estimated engine runs: ``$estimatedEngineRuns``",
    "",
    "| Scenario | Packet | Families | Motion | View Pack | Views | Est. Runs | Capture | Smoke | Capture Contract | Stress | Ran | Exit | Continued |",
    "|---|---|---|---|---|---:|---:|---:|---:|---|---|---:|---:|---:|"
)
foreach ($row in $statusRows) {
    $contractLabel = if ($row.capture_contract_adjusted) {
        "$($row.capture_contract_reason) ($($row.requested_capture_frame)->$($row.effective_capture_frame))"
    } else {
        $row.capture_contract_reason
    }
    $statusLines += "| $($row.scenario) | $($row.packet_root) | $($row.family_filter) | $($row.motion_mode) | $($row.view_pack) | $($row.view_count) | $($row.estimated_engine_runs) | $($row.effective_capture_frame) | $($row.effective_smoke_frames) | $contractLabel | $($row.stress_scene_filter) | $($row.ran_packet) | $($row.exit_code) | $($row.continued_after_failure) |"
}
$statusLines | Set-Content -Encoding UTF8 $suiteStatusMd

if ($PlanOnly) {
    Write-Host "Full Scene Shader Pipeline V3 promotion suite plan written."
    Write-Host "status=$suiteStatusJson"
    exit 0
}

& python $matrixAnalyzer `
    --required-families $RequiredFamilies `
    --required-motion-modes $RequiredMotionModes `
    --packet-list-json $suiteStatusJson `
    --output-json $matrixJson `
    --output-md $matrixMd
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$failedRows = @($statusRows | Where-Object { $_.exit_code -ne $null -and $_.exit_code -ne 0 })
if ($failedRows.Count -gt 0 -and -not $ContinueOnPacketFailure) {
    Write-Host "Full Scene Shader Pipeline V3 promotion suite stopped after packet failure." -ForegroundColor Red
    $failedRows | Format-Table scenario,packet_root,exit_code -AutoSize
    exit ([int]$failedRows[0].exit_code)
}

Write-Host "Full Scene Shader Pipeline V3 promotion suite report written."
Write-Host "status=$suiteStatusJson"
Write-Host "matrix=$matrixJson"
