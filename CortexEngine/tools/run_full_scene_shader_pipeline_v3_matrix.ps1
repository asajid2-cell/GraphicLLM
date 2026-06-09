param(
    [string]$OutputRoot = "build/captures/full_scene_shader_pipeline_v3_matrix",
    [string]$PacketRoots = "",
    [string]$RequiredFamilies = "gallery,kitchen,office,gym,concert,red_room,stadium",
    [string]$RequiredMotionModes = "static,mouse_jitter,camera_sweep",
    [switch]$RunPackets,
    [string]$FamilyFilter = "gallery",
    [string]$MotionModes = "static,mouse_jitter,camera_sweep",
    [string]$ViewFilter = "",
    [string]$StressSceneFilter = "rt_showcase:reflection_closeup",
    [int]$SmokeFrames = 16,
    [int]$CaptureFrame = 8,
    [int]$CaptureSequenceCount = 2,
    [switch]$StressSceneOnly,
    [switch]$NoStressScene,
    [switch]$NoBuild,
    [switch]$SkipSceneAnalyzers,
    [switch]$ContinueOnPacketFailure
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$packetRunner = Join-Path $root "tools/run_full_scene_shader_pipeline_v3_packet.ps1"
$matrixAnalyzer = Join-Path $root "tools/build_full_scene_shader_v3_matrix_decision.py"
$outputPath = Join-Path $root $OutputRoot
$matrixJson = Join-Path $outputPath "v3_matrix_decision.json"
$matrixMd = Join-Path $outputPath "v3_matrix_decision.md"
$packetStatusJson = Join-Path $outputPath "packet_run_status.json"
$packetStatusMd = Join-Path $outputPath "packet_run_status.md"

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

function Get-SafeName([string]$Value) {
    $safe = ($Value.ToLowerInvariant() -replace '[^a-z0-9]+', '_').Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "unnamed"
    }
    return $safe
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$packetRootList = @()
$packetStatusRows = @()

foreach ($packetRoot in (Split-Csv $PacketRoots)) {
    $packetRootList += $packetRoot
    $packetStatusRows += [pscustomobject]@{
        packet_root = $packetRoot
        motion_mode = "provided"
        ran_packet = $false
        exit_code = $null
        continued_after_failure = $false
    }
}

if ($RunPackets) {
    $firstRun = $true
    foreach ($mode in (Split-Csv $MotionModes)) {
        $modeName = Get-SafeName $mode
        $packetOutputRoot = Join-Path $OutputRoot ("packet_" + $modeName)
        $packetArgs = @(
            "-OutputRoot", $packetOutputRoot,
            "-FamilyFilter", $FamilyFilter,
            "-SmokeFrames", "$SmokeFrames",
            "-CaptureFrame", "$CaptureFrame",
            "-CaptureSequenceCount", "$CaptureSequenceCount",
            "-StabilityMotionMode", $mode
        )
        if (-not [string]::IsNullOrWhiteSpace($ViewFilter)) {
            $packetArgs += @("-ViewFilter", $ViewFilter)
        }
        if (-not $NoStressScene) {
            $packetArgs += @("-StressSceneFilter", $StressSceneFilter)
        }
        if ($NoBuild -or -not $firstRun) {
            $packetArgs += "-NoBuild"
        }
        if ($SkipSceneAnalyzers) {
            $packetArgs += "-SkipSceneAnalyzers"
        }
        if ($StressSceneOnly) {
            $packetArgs += "-StressSceneOnly"
        }
        if ($NoStressScene) {
            $packetArgs += "-NoStressScene"
        }

        & powershell -NoProfile -ExecutionPolicy Bypass -File $packetRunner @packetArgs
        $packetExit = $LASTEXITCODE
        $packetRootList += $packetOutputRoot
        $packetStatusRows += [pscustomobject]@{
            packet_root = $packetOutputRoot
            motion_mode = $mode
            ran_packet = $true
            exit_code = $packetExit
            continued_after_failure = ($packetExit -ne 0 -and [bool]$ContinueOnPacketFailure)
        }
        if ($packetExit -ne 0 -and -not $ContinueOnPacketFailure) {
            $status = [pscustomobject]@{
                schema = "cortex.full_scene_shader_pipeline_v3.matrix_packet_run_status.v1"
                output_root = $outputPath
                continue_on_packet_failure = [bool]$ContinueOnPacketFailure
                packet_roots = @($packetRootList)
                packet_status = @($packetStatusRows)
            }
            $status | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $packetStatusJson
            exit $packetExit
        }
        $firstRun = $false
    }
}

if ($packetRootList.Count -eq 0) {
    throw "No packet roots were provided. Pass -PacketRoots for existing packets or use -RunPackets to render a matrix."
}

$statusDoc = [pscustomobject]@{
    schema = "cortex.full_scene_shader_pipeline_v3.matrix_packet_run_status.v1"
    output_root = $outputPath
    continue_on_packet_failure = [bool]$ContinueOnPacketFailure
    packet_roots = @($packetRootList)
    packet_status = @($packetStatusRows)
}
$statusDoc | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $packetStatusJson

$statusLines = @(
    "# Full Scene Shader Pipeline V3 Matrix Packet Run Status",
    "",
    "Continue on packet failure: ``$([bool]$ContinueOnPacketFailure)``",
    "",
    "| Packet | Motion | Ran | Exit | Continued |",
    "|---|---|---:|---:|---:|"
)
foreach ($row in $packetStatusRows) {
    $statusLines += "| $($row.packet_root) | $($row.motion_mode) | $($row.ran_packet) | $($row.exit_code) | $($row.continued_after_failure) |"
}
$statusLines | Set-Content -Encoding UTF8 $packetStatusMd

$analyzerArgs = @(
    "--required-families", $RequiredFamilies,
    "--required-motion-modes", $RequiredMotionModes,
    "--packet-list-json", $packetStatusJson,
    "--output-json", $matrixJson,
    "--output-md", $matrixMd
)

& python $matrixAnalyzer @analyzerArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Full Scene Shader Pipeline V3 matrix decision written."
Write-Host "matrix=$matrixJson"
