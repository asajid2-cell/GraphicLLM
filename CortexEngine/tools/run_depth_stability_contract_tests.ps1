param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-Text([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Required source file not found: $Path"
    }
    return Get-Content -Path $Path -Raw
}

$geometryPath = Join-Path $root "src/Graphics/RendererGeometryUtils.cpp"
$waterPath = Join-Path $root "src/Graphics/Renderer_WaterSurfaces.cpp"
$waterShaderPath = Join-Path $root "assets/shaders/Water.hlsl"
$scenePath = Join-Path $root "src/Core/Engine_Scenes.cpp"
$releasePath = Join-Path $root "tools/run_release_validation.ps1"

$geometry = Read-Text $geometryPath
$water = Read-Text $waterPath
$waterShader = Read-Text $waterShaderPath
$scenes = Read-Text $scenePath
$release = Read-Text $releasePath

if ($geometry -match "kUpDot" -or $geometry -match "glm::vec3\(0\.0f,\s*1\.0f,\s*0\.0f\)\)\s*<\s*kUpDot") {
    Add-Failure "thin-surface separation must not reject non-horizontal planes"
}
if ($geometry -match "alphaMode\s*==\s*Scene::RenderableComponent::AlphaMode::Blend\)\s*\{\s*return sep;") {
    Add-Failure "thin-surface separation must not skip blended glass/water planes"
}
foreach ($needle in @(
    "renderable.mesh->boundsCenter",
    "axisWS * (direction * eps * layerScale)",
    "RenderableComponent::RenderLayer::Overlay",
    "RenderableComponent::AlphaMode::Blend",
    "sep.depthBiasNdc"
)) {
    if ($geometry -notmatch [regex]::Escape($needle)) {
        Add-Failure "RendererGeometryUtils.cpp missing expected depth-stability marker '$needle'"
    }
}

foreach ($needle in @(
    "ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey)",
    "ApplyAutoDepthOffset(modelMatrix, sep.worldOffset)",
    "objectData.depthBiasNdc = sep.depthBiasNdc"
)) {
    if ($water -notmatch [regex]::Escape($needle)) {
        Add-Failure "Renderer_WaterSurfaces.cpp missing expected water depth-stability marker '$needle'"
    }
}

foreach ($needle in @(
    "float g_DepthBiasNdc",
    "clipPos.z += g_DepthBiasNdc * clipPos.w"
)) {
    if ($waterShader -notmatch [regex]::Escape($needle)) {
        Add-Failure "Water.hlsl missing expected depth-bias marker '$needle'"
    }
}

$clipCallCount = ([regex]::Matches($scenes, "ConfigureShowcaseCameraClip\(")).Count - 1
if ($clipCallCount -lt 8) {
    Add-Failure "public showcase cameras should use tight clip ranges; found $clipCallCount ConfigureShowcaseCameraClip calls"
}
if ($scenes -notmatch "camera\.nearPlane\s*=\s*0\.25f" -or $scenes -notmatch "camera\.farPlane\s*=\s*farPlane") {
    Add-Failure "ConfigureShowcaseCameraClip must set nearPlane=0.25f and scene-specific farPlane"
}

if ($release -notmatch "depth_stability_contract" -or $release -notmatch "run_depth_stability_contract_tests\.ps1") {
    Add-Failure "release validation must include the depth stability contract"
}
if ($release -notmatch "camera_motion_stability" -or $release -notmatch "run_camera_motion_stability_smoke\.ps1") {
    Add-Failure "release validation must include the moving-camera stability smoke"
}

if ($failures.Count -gt 0) {
    Write-Host "Depth stability contract failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Depth stability contract passed"
