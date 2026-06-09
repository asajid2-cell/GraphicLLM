param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v3_promotion_suite",
    [string]$Scenarios = "reflection_static,reflection_mouse_jitter,enclosed_static,enclosed_mouse_jitter,enclosed_camera_sweep,heavy_light_sweep",
    [string]$RequiredFamilies = "stress_rt_showcase_reflection_closeup,gallery,kitchen,office,gym,concert,red_room,stadium",
    [string]$RequiredMotionModes = "static,mouse_jitter,camera_sweep,light_sweep",
    [string]$ViewFilter = "",
    [int]$SmokeFrames = 30,
    [int]$CaptureFrame = 15,
    [int]$CaptureSequenceCount = 2,
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

function New-Scenario([string]$Name, [string]$FamilyFilter, [string]$MotionMode, [string]$StressSceneFilter, [bool]$StressSceneOnly, [bool]$NoStressScene) {
    return [pscustomobject]@{
        name = $Name
        family_filter = $FamilyFilter
        motion_mode = $MotionMode
        stress_scene_filter = $StressSceneFilter
        stress_scene_only = $StressSceneOnly
        no_stress_scene = $NoStressScene
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
        -NoStressScene $false
    $catalog["reflection_mouse_jitter"] = New-Scenario `
        -Name "reflection_mouse_jitter" `
        -FamilyFilter "" `
        -MotionMode "mouse_jitter" `
        -StressSceneFilter "rt_showcase:reflection_closeup" `
        -StressSceneOnly $true `
        -NoStressScene $false
    $catalog["enclosed_static"] = New-Scenario `
        -Name "enclosed_static" `
        -FamilyFilter "gallery,kitchen,office,gym" `
        -MotionMode "static" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true
    $catalog["enclosed_mouse_jitter"] = New-Scenario `
        -Name "enclosed_mouse_jitter" `
        -FamilyFilter "gallery,kitchen,office,gym" `
        -MotionMode "mouse_jitter" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true
    $catalog["enclosed_camera_sweep"] = New-Scenario `
        -Name "enclosed_camera_sweep" `
        -FamilyFilter "gallery,kitchen,office,gym" `
        -MotionMode "camera_sweep" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true
    $catalog["heavy_static"] = New-Scenario `
        -Name "heavy_static" `
        -FamilyFilter "concert,red_room,stadium" `
        -MotionMode "static" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true
    $catalog["heavy_light_sweep"] = New-Scenario `
        -Name "heavy_light_sweep" `
        -FamilyFilter "concert,red_room,stadium" `
        -MotionMode "light_sweep" `
        -StressSceneFilter "" `
        -StressSceneOnly $false `
        -NoStressScene $true
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

foreach ($scenario in $selectedScenarios) {
    $packetRoots += $scenario.packet_root
    $packetExit = $null
    $ranPacket = $false
    $continuedAfterFailure = $false
    $packetPath = Join-Path $root $scenario.packet_root
    $manifestPath = Join-Path $packetPath "manifest.json"

    if (-not $PlanOnly -and -not $SummarizeExisting) {
        $packetArgs = @(
            "-OutputRoot", $scenario.packet_root,
            "-FamilyFilter", $scenario.family_filter,
            "-SmokeFrames", [string]$SmokeFrames,
            "-CaptureFrame", [string]$CaptureFrame,
            "-CaptureSequenceCount", [string]$CaptureSequenceCount,
            "-StabilityMotionMode", $scenario.motion_mode
        )
        if (-not [string]::IsNullOrWhiteSpace($ViewFilter)) {
            $packetArgs += @("-ViewFilter", $ViewFilter)
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
    continue_on_packet_failure = [bool]$ContinueOnPacketFailure
    summarize_existing = [bool]$SummarizeExisting
    plan_only = [bool]$PlanOnly
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
    "",
    "| Scenario | Packet | Families | Motion | Stress | Ran | Exit | Continued |",
    "|---|---|---|---|---|---:|---:|---:|"
)
foreach ($row in $statusRows) {
    $statusLines += "| $($row.scenario) | $($row.packet_root) | $($row.family_filter) | $($row.motion_mode) | $($row.stress_scene_filter) | $($row.ran_packet) | $($row.exit_code) | $($row.continued_after_failure) |"
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
