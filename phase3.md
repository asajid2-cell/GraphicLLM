# Cortex Phase 3 Blueprint

## Starting Point

Phase 3 begins after the Phase 2 release candidate checkpoint.

Last known release validation:

- Pass: Phase 2 Pass 11DA, RT showcase lighting polish
- Release validation command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1 -NoBuild`
- Latest validation log:
  - `CortexEngine\build\bin\logs\runs\release_validation_20260509_190246_577_70480_b012d0d6`
- Result:
  - release validation passed
  - temporal validation passed
  - RT showcase passed
  - RT budget matrix passed
  - voxel smoke passed

Phase 2 made Cortex measurable. It added frame contracts, resource contracts, RT readiness checks, material parity checks, raw and denoised reflection signal metrics, release validation scripts, texture and memory budgeting, public README polish, and a safe default startup path.

Phase 3 should make Cortex feel like a mature graphics engine: robust, tunable, visually presentable, easy to demo, and hard to silently break.

The later Phase 3 milestones also start the transition from correctness-focused rendering into richer graphics: complex material shaders, stronger lighting rigs, controlled particles, and cinematic effects. These should be built on top of the same controls, contracts, presets, and validation system rather than added as isolated visual experiments.

## North Star

Cortex should reach a public-release quality bar where a reviewer can:

- launch the engine without setup surprises
- switch environments and IBLs safely
- tune graphics using clear sliders and presets
- inspect RT/raster/temporal state without guessing
- run release validation and get useful pass/fail output
- see polished showcase scenes that demonstrate the renderer's strengths
- see complex shaders, lighting, and particles used in composed scenes
- understand degraded behavior on low-memory or no-RT hardware
- reproduce the same scene/profile from config files

The target is not just more rendering features. The target is a clean control surface over the renderer, with stable diagnostics and strong fallbacks.

## Non-Goals

Do not restart the infinite-world effort in Phase 3.

Do not replace the renderer architecture wholesale.

Do not chase an offline path tracer comparison. Cortex is a real-time hybrid raster plus DXR renderer.

Do not introduce a large UI framework migration unless the existing Win32/native UI cannot support the required controls.

Do not add large asset dependencies without admission rules, fallback paths, and release-size awareness.

Do not make visual changes that are not measured by screenshots, frame contracts, or validation gates.

## Design Principles

1. Every public-facing visual feature needs a control, a preset, a fallback, and a validation path.
2. Every setting exposed in UI must clamp inputs and serialize cleanly.
3. Every environment asset must have metadata, budget classification, and a missing-asset fallback.
4. Every performance-sensitive feature must report its scheduling and budget state.
5. Showcase scenes should be composed, not random collections of features.
6. Debug information should be available, but the default view should be clean enough for public capture.
7. Renderer state should move out of scattered members and into named state structs as Phase 3 touches each area.

## Repo-Native Implementation Rules

Phase 3 must extend the engine that exists, not create parallel systems.

Actual repo layout:

- renderer/runtime code: `CortexEngine/src/Graphics`
- UI windows: `CortexEngine/src/UI`
- engine orchestration: `CortexEngine/src/Core`
- scene components and systems: `CortexEngine/src/Scene`
- shaders: `CortexEngine/assets/shaders`
- config-style assets: `CortexEngine/assets/config`
- current loose IBL assets: `CortexEngine/assets`

Existing systems to reuse and extend:

- `Graphics/FrameContract.h`, `FrameContractJson.cpp`, and `FrameContractValidation.cpp`
- `Graphics/BudgetPlanner.h`
- `Graphics/RTScheduler.h`
- `Graphics/RendererControlApplier.h` and its runtime/debug/scene preset implementation files
- `Graphics/MaterialPresetRegistry.h`
- `Graphics/RendererEnvironmentState.h` and `Renderer_Environment.cpp`
- `Graphics/AssetRegistry.h`, `TextureAdmission.h`, and `TextureSourcePlan.h`
- `Core/VisualValidation.h`
- `Graphics/GPUParticles.h`, `RendererParticleState.h`, `Renderer_Particles.cpp`, and particle shaders in `assets/shaders`
- existing UI windows such as `QualitySettingsWindow`, `LightingWindow`, `PerformanceWindow`, `QuickSettingsWindow`, and `DebugMenu`

Implementation rule:

- if a Phase 3 feature sounds like a new subsystem, first check whether the repo already has a partial version and extend that.
- new files are allowed only when they wrap or clarify existing ownership; they should not duplicate `FrameContract`, `BudgetPlanner`, `RTScheduler`, `VisualValidation`, `MaterialPresetRegistry`, or `RendererControlApplier`.
- planned file paths in this document are implementation hints, not permission to create a new parallel architecture.

## Phase 3 Workstreams

Phase 3 is split into ten workstreams:

1. Robustness foundation
2. Validation and visual test matrix
3. UI control surface
4. Backgrounds, skies, and IBL library
5. Showcase scene polish
6. Material and surface correctness
7. RT and temporal tuning
8. User experience polish
9. Renderer architecture cleanup
10. Advanced shaders, lighting, and particles

Each workstream should produce source changes, diagnostics, and tests. A pass is not complete until the relevant smoke or release gate proves it.

---

# Workstream 1: Robustness Foundation

## Goal

Make startup, runtime failure handling, configuration, and shutdown more predictable.

Phase 2 already added release validation and safe default startup behavior. Phase 3 should harden the engine against missing assets, unsupported hardware features, invalid settings, and broken profiles.

## Pass 3A: Startup Preflight Contract

Add a startup preflight layer that checks essential resources before renderer initialization finishes.

The preflight should validate:

- executable working directory
- required shader directory
- required default textures
- required default environment or fallback sky
- DX12 adapter selection
- DXR support level
- minimum descriptor budget estimate
- default scene availability
- config parse success
- release/demo profile availability

The preflight should not make startup fragile. Missing optional assets should downgrade cleanly.

Pseudocode:

```cpp
struct StartupPreflightIssue
{
    enum class Severity { Info, Warning, Error };
    Severity severity;
    std::string code;
    std::string message;
    std::string path;
    std::string fallback;
};

struct StartupPreflightResult
{
    bool canLaunch = true;
    bool usedSafeMode = false;
    bool dxrAvailable = false;
    bool environmentFallbackUsed = false;
    std::vector<StartupPreflightIssue> issues;
};

StartupPreflightResult RunStartupPreflight(const StartupConfig& config)
{
    StartupPreflightResult result;

    CheckDirectory(result, "Shaders", config.shaderRoot, Required::Yes);
    CheckDirectory(result, "Textures", config.textureRoot, Required::No);
    CheckFile(result, "DefaultWhiteTexture", config.defaultWhiteTexture, Required::Yes);
    CheckFile(result, "DefaultNormalTexture", config.defaultNormalTexture, Required::Yes);
    CheckEnvironmentManifest(result, config.environmentManifest, Required::No);
    CheckSceneManifest(result, config.sceneManifest, Required::No);

    AdapterCaps caps = ProbeAdapter(config.adapterPreference);
    result.dxrAvailable = caps.supportsDXR;

    if (!caps.supportsDXR)
        AddWarning(result, "DXR_UNAVAILABLE", "Ray tracing will be disabled.", "raster fallback");

    if (!HasEnoughDescriptorBudget(caps, EstimateStartupDescriptors(config)))
        AddWarning(result, "LOW_DESCRIPTOR_HEADROOM", "Safe quality preset will be used.", "safe mode");

    result.canLaunch = !HasError(result);
    result.usedSafeMode = ShouldForceSafeMode(result, caps);
    return result;
}
```

Frame contract additions:

```json
{
  "startup": {
    "preflight_passed": true,
    "safe_mode": false,
    "asset_fallback_count": 0,
    "dxr_available": true,
    "config_profile": "release_showcase"
  }
}
```

Validation:

- launch with full assets
- launch with optional environment manifest missing
- launch with one IBL texture missing
- launch with RT disabled
- launch with safe profile
- verify frame contract reports fallback state

## Pass 3B: Runtime Health State

Add one renderer health object that summarizes runtime state for UI, logs, and smoke tests.

It should include:

- adapter name
- active quality preset
- RT enabled/requested/effective
- IBL loaded/fallback state
- shader hot-reload state if present
- GPU memory pressure
- descriptor pressure
- last device removal reason
- last recoverable render warning
- frame contract warning count

Pseudocode:

```cpp
struct RendererHealthState
{
    std::string adapterName;
    std::string qualityPreset;
    bool rayTracingRequested = false;
    bool rayTracingEffective = false;
    bool environmentLoaded = false;
    bool environmentFallback = false;
    uint32_t frameWarnings = 0;
    uint32_t assetFallbacks = 0;
    BudgetState descriptorBudget;
    BudgetState transientDescriptorBudget;
    BudgetState dxgiMemoryBudget;
    BudgetState rtMemoryBudget;
    std::string lastWarningCode;
    std::string lastWarningMessage;
};

RendererHealthState Renderer::BuildHealthState() const
{
    RendererHealthState state;
    state.adapterName = m_adapterName;
    state.qualityPreset = m_qualityState.activePreset;
    state.rayTracingRequested = m_rayTracingState.requested;
    state.rayTracingEffective = m_rayTracingState.active && m_rayTracingState.ready;
    state.environmentLoaded = m_environmentState.loaded;
    state.environmentFallback = m_environmentState.usingFallback;
    state.descriptorBudget = BuildBudgetState(m_persistentDescriptorCount, m_persistentDescriptorBudget);
    state.transientDescriptorBudget = BuildBudgetState(m_transientDescriptorPeak, m_transientDescriptorBudget);
    return state;
}
```

Validation:

- health state visible in debug UI
- health state serialized into frame contract
- health state included in release validation summary

## Pass 3C: Device Removed and Fatal Error UX

Do not let device removal or fatal renderer setup failures look like silent hangs.

Add:

- final log flush
- compact crash summary file
- user-facing message box in interactive runs
- clear nonzero exit code in validation runs
- adapter/device/failing pass info if available

Pseudocode:

```cpp
void Engine::HandleFatalRendererError(const RendererFatalError& error)
{
    RendererCrashSummary summary;
    summary.time = NowUtc();
    summary.adapter = renderer.GetAdapterName();
    summary.profile = config.profileName;
    summary.errorCode = error.code;
    summary.message = error.message;
    summary.lastPass = renderer.GetLastPassName();
    summary.deviceRemovedReason = renderer.GetDeviceRemovedReasonString();

    WriteCrashSummary(summary, logDirectory / "last_renderer_failure.json");
    FlushLogs();

    if (config.interactive)
        ShowFatalErrorDialog(summary);

    RequestShutdown(ExitCode::RendererFatalError);
}
```

Validation:

- forced invalid shader path produces readable failure
- forced invalid IBL path falls back instead of fatal
- validation mode exits nonzero on forced fatal setup error

---

# Workstream 2: Validation and Visual Test Matrix

## Goal

Phase 2 validation proves the engine can run important scenes. Phase 3 validation should prove visual controls, environments, fallbacks, and profiles behave correctly.

## Pass 3D: Phase 3 Visual Matrix Script

Add a script:

- `CortexEngine\tools\run_phase3_visual_matrix.ps1`

It should run a curated set of scenes, environments, and quality profiles.

Initial matrix:

| Scene | Profile | Environment | Required Checks |
|---|---|---|---|
| temporal_validation | balanced | default_studio | temporal diff, warnings |
| rt_showcase | 8gb_balanced | studio_softbox | RT readiness, raw/history signal |
| rt_showcase | 4gb_low | studio_softbox | graceful RT scheduling |
| material_lab | 8gb_balanced | warm_gallery | material validation |
| glass_water | 8gb_balanced | sunset_courtyard | water/glass metrics |
| ibl_gallery | 8gb_balanced | all enabled IBLs | no black backgrounds |
| safe_startup | 2gb_ultra_low | fallback_sky | no crash, no missing required assets |

Pseudocode:

```powershell
$matrix = @(
  @{ Scene="temporal_validation"; Profile="balanced"; Env="default_studio"; Frames=96 },
  @{ Scene="rt_showcase"; Profile="8gb_balanced"; Env="studio_softbox"; Frames=96 },
  @{ Scene="rt_showcase"; Profile="4gb_low"; Env="studio_softbox"; Frames=96 },
  @{ Scene="material_lab"; Profile="8gb_balanced"; Env="warm_gallery"; Frames=64 },
  @{ Scene="glass_water"; Profile="8gb_balanced"; Env="sunset_courtyard"; Frames=64 },
  @{ Scene="ibl_gallery"; Profile="8gb_balanced"; Env="all"; Frames=48 },
  @{ Scene="safe_startup"; Profile="2gb_ultra_low"; Env="fallback_sky"; Frames=32 }
)

foreach ($case in $matrix) {
  $result = Invoke-CortexSmoke `
    -Scene $case.Scene `
    -Profile $case.Profile `
    -Environment $case.Env `
    -Frames $case.Frames `
    -CaptureFrameContract `
    -CaptureScreenshot

  Assert-NoFatalWarnings $result
  Assert-BudgetContracts $result
  Assert-VisualContracts $result
  Write-MatrixRow $result
}
```

Outputs:

- JSON summary
- Markdown summary
- screenshot folder
- per-run frame contract
- final pass/fail status

## Pass 3E: Screenshot Contract

Add screenshot-level statistics for validation.

Metrics:

- average luma
- center luma
- dark pixel ratio
- saturated pixel ratio
- near-white ratio
- nonblack coverage
- colorfulness
- edge occupancy
- dominant hue warning
- aspect ratio
- expected object coverage if scene provides mask/heuristic

Pseudocode:

```cpp
struct ScreenshotStats
{
    float avgLuma;
    float centerLuma;
    float darkRatio;
    float saturatedRatio;
    float nearWhiteRatio;
    float nonBlackRatio;
    float colorfulness;
    float edgeOccupancy;
    float dominantHueRatio;
};

bool ValidateScreenshotStats(const ScreenshotStats& stats, const ScreenshotGate& gate)
{
    return stats.avgLuma >= gate.minAvgLuma &&
           stats.avgLuma <= gate.maxAvgLuma &&
           stats.nonBlackRatio >= gate.minNonBlackRatio &&
           stats.saturatedRatio <= gate.maxSaturatedRatio &&
           stats.dominantHueRatio <= gate.maxDominantHueRatio;
}
```

Validation:

- RT showcase cannot pass if screenshot is black
- IBL gallery cannot pass if background coverage is missing
- material lab cannot pass if scene is overexposed or monochrome

## Pass 3F: Visual Gate Stabilization

Do not add golden baselines yet.

Phase 3 should first stabilize scenes, camera bookmarks, lighting rigs, environment selection, and visual capture timing. Golden baselines added before that point will churn constantly and train the team to ignore validation noise.

This pass should instead harden the existing `Core/VisualValidation` and smoke-script gates.

Use the metrics that already exist:

- average luma
- center luma
- nonblack ratio
- colorful ratio
- saturated ratio
- near-white ratio
- dark-detail ratio

Extend only where necessary:

- edge occupancy if it catches empty composition
- dominant hue ratio if one-note color grading becomes a real issue
- scene-specific expected ranges after scenes are stable enough to justify them

Temporary gate example:

```json
{
  "scene": "rt_showcase",
  "profile": "8gb_balanced",
  "environment": "studio_softbox",
  "metrics": {
    "gpu_ms": { "max": 16.7 },
    "avg_luma": { "min": 45.0, "max": 125.0 },
    "dark_ratio": { "max": 0.70 },
    "saturated_ratio": { "max": 0.12 },
    "rt_reflection_ready": { "equals": true },
    "rt_reflection_signal_avg_luma": { "min": 0.005 },
    "rt_reflection_history_avg_luma": { "min": 0.005 },
    "frame_warnings": { "equals": 0 }
  }
}
```

Validation:

- current RT showcase and temporal validation pass existing visual gates
- forced black screenshot fails clearly
- forced overexposed screenshot fails clearly
- output names exact failed metric and observed value
- no committed golden baselines are introduced until after Phase 3P/3AG scene and camera stabilization

Deferred golden-baseline rule:

- golden/tolerance baseline files belong after public scenes, camera bookmarks, IBL defaults, and lighting rigs are stable
- when added, they should compare tolerant metrics, not exact pixels
- they should live beside validation tooling, not inside ad hoc scene code

---

# Workstream 3: UI Control Surface

## Goal

Make graphics tuning discoverable and safe.

Cortex already has many renderer control APIs and several UI windows. Phase 3 should turn those into a coherent graphics control surface with presets, sliders, reset buttons, live metrics, and serialization.

Existing renderer control areas observed:

- environment and IBL:
  - `SetEnvironmentPreset`
  - `LoadAdditionalEnvironmentMaps`
  - `SetIBLLimitEnabled`
  - `SetIBLIntensity`
  - `SetIBLEnabled`
  - `CycleEnvironmentPreset`
  - `AddEnvironmentFromTexture`
- lighting and post:
  - `SetExposure`
  - `SetBloomIntensity`
  - `SetColorGrade`
  - `SetSunDirection`
  - `SetSunColor`
  - `SetSunIntensity`
  - `SetGodRayIntensity`
  - `SetAreaLightSizeScale`
- screen-space features:
  - `SetSSAOEnabled`
  - `SetSSAOParams`
  - `SetSSREnabled`
  - `SetFogEnabled`
  - `SetFogParams`
  - `SetWaterParams`
  - `SetPCSS`
  - `SetFXAAEnabled`
  - `SetTAAEnabled`
- ray tracing:
  - `SetRayTracingEnabled`
  - `SetRTReflectionsEnabled`
  - `SetRTGIEnabled`
- quality:
  - `SetRenderScale`
  - `SetGPUCullingEnabled`
  - `SetVoxelBackendEnabled`
  - `ApplySafeQualityPreset`
  - `SetUseSafeLightingRigOnLowVRAM`

## Pass 3G: Renderer Tuning State

Create a serializable settings model that represents the UI-facing graphics state, but route application through the existing `Graphics/RendererControlApplier` layer wherever possible.

This should not become a second renderer-control API. The model captures presets and UI state; `RendererControlApplier` remains the main place where controls clamp values and call `Renderer`.

Suggested files:

- `CortexEngine/src/Graphics/RendererTuningState.h`
- `CortexEngine/src/Graphics/RendererTuningState.cpp`
- `CortexEngine/src/Graphics/RendererTuningController.h`
- `CortexEngine/src/Graphics/RendererTuningController.cpp`
- extensions to `CortexEngine/src/Graphics/RendererControlApplier.h`
- extensions to `CortexEngine/src/Graphics/RendererControlApplier_Runtime.cpp`

Pseudocode:

```cpp
struct ToneMappingTuning
{
    float exposure = 1.0f;
    float bloomIntensity = 0.08f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float warmth = 0.0f;
};

struct EnvironmentTuning
{
    std::string environmentId = "studio_softbox";
    bool iblEnabled = true;
    bool backgroundVisible = true;
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
    float backgroundExposure = 1.0f;
    float backgroundBlur = 0.0f;
    float rotationDegrees = 0.0f;
};

struct RayTracingTuning
{
    bool enabled = true;
    bool reflectionsEnabled = true;
    bool giEnabled = false;
    float reflectionScale = 1.0f;
    float maxReflectionRoughness = 0.65f;
    float denoiseBlend = 0.92f;
    float historyClamp = 4.0f;
    float fireflyClamp = 10.0f;
};

struct ScreenSpaceTuning
{
    bool ssaoEnabled = true;
    float ssaoRadius = 1.5f;
    float ssaoBias = 0.02f;
    float ssaoIntensity = 1.0f;
    bool ssrEnabled = true;
    float ssrStrength = 0.75f;
    float ssrMaxDistance = 40.0f;
    float ssrThickness = 0.12f;
};

struct AtmosphereTuning
{
    bool fogEnabled = true;
    float fogDensity = 0.015f;
    float fogHeightFalloff = 0.25f;
    float fogStart = 8.0f;
    float godRayIntensity = 0.15f;
};

struct WaterTuning
{
    float waveAmplitude = 0.08f;
    float waveLength = 4.0f;
    float waveSpeed = 0.45f;
    float fresnelStrength = 1.0f;
    float roughness = 0.05f;
};

struct ShadowTuning
{
    float shadowBias = 0.0008f;
    float normalBias = 0.02f;
    int pcfRadius = 2;
    float cascadeLambda = 0.65f;
};

struct QualityTuning
{
    std::string preset = "8gb_balanced";
    float renderScale = 1.0f;
    bool taaEnabled = true;
    bool fxaaEnabled = false;
    bool gpuCullingEnabled = true;
};

struct RendererTuningState
{
    ToneMappingTuning tone;
    EnvironmentTuning environment;
    RayTracingTuning rayTracing;
    ScreenSpaceTuning screenSpace;
    AtmosphereTuning atmosphere;
    WaterTuning water;
    ShadowTuning shadows;
    QualityTuning quality;
};
```

Controller pseudocode:

```cpp
class RendererTuningController
{
public:
    RendererTuningState CaptureFromRenderer(const Renderer& renderer) const;
    void ApplyToRenderer(Renderer& renderer, const RendererTuningState& state);
    RendererTuningState Clamp(const RendererTuningState& state) const;
    RendererTuningState LoadPreset(std::string_view id) const;
    bool SaveUserPreset(std::string_view path, const RendererTuningState& state) const;
};

void RendererTuningController::ApplyToRenderer(Renderer& renderer, const RendererTuningState& rawState)
{
    RendererTuningState state = Clamp(rawState);

    ApplyExposureControl(renderer, state.tone.exposure);
    ApplyBloomIntensityControl(renderer, state.tone.bloomIntensity);
    ApplyColorGradeControl(renderer, state.tone.warmth, -state.tone.warmth);

    ApplyEnvironmentPresetControl(renderer, state.environment.environmentId);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::IBL, state.environment.iblEnabled);
    ApplyIBLIntensityControl(renderer, state.environment.diffuseIntensity, state.environment.specularIntensity);
    renderer.SetEnvironmentRotation(state.environment.rotationDegrees);
    renderer.SetBackgroundPresentation(state.environment.backgroundVisible,
                                       state.environment.backgroundExposure,
                                       state.environment.backgroundBlur);

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RayTracing, state.rayTracing.enabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RTReflections, state.rayTracing.reflectionsEnabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RTGI, state.rayTracing.giEnabled);
    renderer.SetRTReflectionTuning(BuildReflectionTuning(state.rayTracing));

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::SSAO, state.screenSpace.ssaoEnabled);
    ApplySSAOParamsControl(renderer,
                           state.screenSpace.ssaoRadius,
                           state.screenSpace.ssaoBias,
                           state.screenSpace.ssaoIntensity);
    renderer.SetSSRParams(BuildSSRTuning(state.screenSpace));

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::Fog, state.atmosphere.fogEnabled);
    ApplyFogParamsControl(renderer,
                          state.atmosphere.fogDensity,
                          state.atmosphere.fogStart,
                          state.atmosphere.fogHeightFalloff);
    ApplyWaterStateControl(renderer,
                           /*levelY*/ 0.0f,
                           state.water.waveAmplitude,
                           state.water.waveLength,
                           state.water.waveSpeed,
                           /*secondaryAmplitude*/ 0.0f);
    ApplyShadowBiasControl(renderer, state.shadows.shadowBias);
    ApplyShadowPCFRadiusControl(renderer, static_cast<float>(state.shadows.pcfRadius));
    ApplyCascadeSplitLambdaControl(renderer, state.shadows.cascadeLambda);

    ApplyRenderScaleControl(renderer, state.quality.renderScale);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::TAA, state.quality.taaEnabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::FXAA, state.quality.fxaaEnabled);
    ApplyGPUCullingEnabledControl(renderer, state.quality.gpuCullingEnabled);
}
```

## Pass 3H: Graphics Settings Window

Build or extend a single Graphics Settings window.

It should have tabs:

- Quality
- Lighting
- Environment
- Ray Tracing
- Screen Space
- Atmosphere
- Water
- Debug

Each control should have:

- label
- min/max/default
- current value
- reset button
- optional tooltip
- dirty-state tracking
- immediate apply mode
- profile apply mode for expensive changes

Control definition pseudocode:

```cpp
struct SliderDef
{
    const char* id;
    const char* label;
    float minValue;
    float maxValue;
    float defaultValue;
    float step;
    const char* tooltip;
};

static constexpr SliderDef kToneSliders[] =
{
    { "exposure", "Exposure", 0.10f, 4.00f, 1.00f, 0.01f, "Scene exposure before tone mapping." },
    { "bloom", "Bloom", 0.00f, 1.50f, 0.08f, 0.01f, "Intensity of bright highlight bloom." },
    { "contrast", "Contrast", 0.50f, 1.75f, 1.00f, 0.01f, "Post color contrast." },
    { "saturation", "Saturation", 0.00f, 2.00f, 1.00f, 0.01f, "Post color saturation." },
    { "warmth", "Warmth", -1.00f, 1.00f, 0.00f, 0.01f, "Warm/cool color balance." }
};
```

Window pseudocode:

```cpp
void GraphicsSettingsWindow::Draw()
{
    RendererTuningState edited = m_state;

    DrawPresetHeader(edited);

    if (BeginTabBar("GraphicsTabs")) {
        if (BeginTab("Quality")) DrawQualityTab(edited.quality);
        if (BeginTab("Lighting")) DrawLightingTab(edited.tone, edited.shadows);
        if (BeginTab("Environment")) DrawEnvironmentTab(edited.environment);
        if (BeginTab("Ray Tracing")) DrawRayTracingTab(edited.rayTracing);
        if (BeginTab("Screen Space")) DrawScreenSpaceTab(edited.screenSpace);
        if (BeginTab("Atmosphere")) DrawAtmosphereTab(edited.atmosphere);
        if (BeginTab("Water")) DrawWaterTab(edited.water);
        if (BeginTab("Debug")) DrawDebugTab();
        EndTabBar();
    }

    DrawLiveBudgetStrip(m_renderer.BuildHealthState());
    DrawApplyResetSaveButtons(edited);

    if (m_immediateApply && edited != m_state)
        Apply(edited);
}
```

Required controls:

Quality:

- quality preset dropdown
- render scale slider
- TAA toggle
- FXAA toggle
- GPU culling toggle
- safe lighting rig toggle
- RT budget profile dropdown

Lighting:

- exposure
- bloom intensity
- contrast
- saturation
- warmth
- sun intensity
- sun color
- sun direction
- area light size scale
- shadow bias
- PCF radius
- cascade lambda

Environment:

- environment dropdown
- IBL enabled
- diffuse IBL intensity
- specular IBL intensity
- background visible
- background exposure
- background blur
- environment rotation
- environment budget class
- reload environments button

Ray Tracing:

- RT master toggle
- reflections toggle
- GI toggle
- reflection resolution scale
- max reflection roughness
- denoise blend
- history clamp
- firefly clamp
- show readiness status
- show raw/history signal metrics

Screen Space:

- SSAO toggle
- SSAO radius
- SSAO bias
- SSAO intensity
- SSR toggle
- SSR strength
- SSR max distance
- SSR thickness

Atmosphere:

- fog toggle
- fog density
- fog start
- fog height falloff
- god ray intensity

Water:

- wave amplitude
- wavelength
- speed
- fresnel strength
- roughness
- reflection mix

Debug:

- show pass timings
- show frame contract status
- show resource budget strip
- show material validation overlay
- show RT readiness overlay
- capture screenshot
- open latest validation folder

Validation:

- UI smoke opens Graphics Settings
- all tabs render
- sliders clamp correctly
- toggles call renderer APIs
- reset restores defaults
- save/load preset round-trips exactly
- release validation can run with a named graphics preset

## Pass 3I: Preset Save, Load, and Reset

Add graphics profile files.

Suggested location:

- `CortexEngine/config/graphics_presets/`

Initial presets:

- `release_showcase.json`
- `balanced_default.json`
- `low_memory_safe.json`
- `rt_gallery.json`
- `material_lab.json`
- `ibl_review.json`

Preset pseudocode:

```json
{
  "schema": 1,
  "id": "release_showcase",
  "display_name": "Release Showcase",
  "quality": {
    "preset": "8gb_balanced",
    "render_scale": 1.0,
    "taa": true,
    "fxaa": false
  },
  "tone": {
    "exposure": 1.0,
    "bloom": 0.08,
    "contrast": 1.0,
    "saturation": 1.0,
    "warmth": 0.0
  },
  "environment": {
    "id": "studio_softbox",
    "ibl": true,
    "diffuse_intensity": 1.0,
    "specular_intensity": 1.0,
    "background_exposure": 0.9,
    "background_blur": 0.15,
    "rotation_degrees": 0.0
  },
  "ray_tracing": {
    "enabled": true,
    "reflections": true,
    "gi": false,
    "reflection_scale": 1.0,
    "max_reflection_roughness": 0.65,
    "denoise_blend": 0.92,
    "history_clamp": 4.0,
    "firefly_clamp": 10.0
  }
}
```

Validation:

- preset parser rejects unknown schema with clear error
- missing optional fields get defaults
- invalid values clamp and report warnings
- UI can save user preset
- CLI can run with `--graphics-preset release_showcase`

---

# Workstream 4: Backgrounds, Skies, and IBL Library

## Goal

Improve visual presentation using controlled environments, not ad hoc texture loading.

The renderer already supports environment maps, pending IBL loading, residency limits, asset memory tracking, and IBL controls through `RendererEnvironmentState`, `Renderer_Environment.cpp`, `AssetRegistry`, and `BudgetPlanner`.

Phase 3 should formalize that into a manifest-backed environment policy. It should not create a second unrelated environment loader.

## Pass 3I.5: IBL Asset Policy Lock

Before implementing the manifest/runtime changes, lock the asset policy.

Decisions to record:

- exact default committed environment
- exact fallback when the default is missing
- allowed runtime formats
- max resolution per budget class
- max committed asset size
- whether thumbnails are committed or generated
- whether optional environments are committed, generated, or downloaded
- naming convention for environment IDs
- whether EXR/HDR files are source assets only and DDS files are runtime assets

Recommended initial policy:

- default committed runtime IBL: existing studio DDS if available, currently `CortexEngine/assets/MR_INT-001_NaturalStudio_NAD.dds`
- source HDR/EXR files may remain optional source assets, but runtime validation should prefer DDS
- fallback if no runtime IBL loads: existing procedural sky shader path in `CortexEngine/assets/shaders/ProceduralSky.hlsl`
- small environment: max 1024 or 2048 runtime dimension, one resident under low profiles
- medium environment: max 2048 runtime dimension, balanced/high profiles only
- large environment: optional only, not required for public release validation
- thumbnails: small PNG/JPG under the environment manifest tree, optional warning only
- optional environment downloads: allowed only through explicit tools/scripts, never during normal engine startup

Implementation targets:

- extend `Graphics/RendererEnvironmentState.h`
- extend `Graphics/Renderer_Environment.cpp`
- reuse `Graphics/AssetRegistry.h`
- reuse `Graphics/BudgetPlanner.h`
- reuse `Graphics/TextureAdmission.h` and `TextureSourcePlan.h` where possible

Validation:

- default committed environment exists or procedural fallback is selected
- environment budget class maps to `RendererBudgetPlan::environmentBudgetBytes`
- low-memory profile does not load medium/large optional environments eagerly
- missing optional environment produces warning, not fatal startup failure

## Pass 3J: Environment Manifest

Create an environment manifest that describes and constrains the existing environment loading path.

Suggested location:

- `CortexEngine/assets/environments/environments.json`

Manifest schema:

```json
{
  "schema": 1,
  "default": "studio_softbox",
  "fallback": "procedural_sky",
  "environments": [
    {
      "id": "studio_softbox",
      "display_name": "Studio Softbox",
      "type": "cubemap",
      "budget_class": "small",
      "radiance": "studio_softbox/radiance.dds",
      "irradiance": "studio_softbox/irradiance.dds",
      "thumbnail": "studio_softbox/thumb.png",
      "default_diffuse": 1.0,
      "default_specular": 1.0,
      "default_background_exposure": 0.9,
      "default_background_blur": 0.15,
      "default_rotation_degrees": 0.0,
      "tags": ["studio", "neutral", "material_review"]
    },
    {
      "id": "warm_gallery",
      "display_name": "Warm Gallery",
      "type": "cubemap",
      "budget_class": "medium",
      "radiance": "warm_gallery/radiance.dds",
      "irradiance": "warm_gallery/irradiance.dds",
      "thumbnail": "warm_gallery/thumb.png",
      "default_diffuse": 0.9,
      "default_specular": 1.15,
      "default_background_exposure": 0.85,
      "default_background_blur": 0.25,
      "default_rotation_degrees": 35.0,
      "tags": ["gallery", "warm", "showcase"]
    }
  ]
}
```

Required initial environment IDs:

- `procedural_sky`
- `studio_softbox`
- `warm_gallery`
- `cool_overcast`
- `sunset_courtyard`
- `night_city`
- `warehouse_lowkey`
- `ibl_debug_checker`

Asset policy:

- DDS for runtime radiance/irradiance where possible
- HDR/EXR source assets are optional source inputs unless explicitly admitted by the runtime policy
- small thumbnails for UI
- manifest paths are relative to environment root
- each asset has a budget class
- missing optional thumbnail is a warning
- missing selected radiance/irradiance falls back to procedural sky
- normal startup must not download environment assets

## Pass 3K: Environment Library Runtime

Add a thin environment manifest/runtime layer around the existing environment state and texture loader.

Suggested files:

- `CortexEngine/src/Graphics/EnvironmentManifest.h`
- `CortexEngine/src/Graphics/EnvironmentManifest.cpp`
- `CortexEngine/src/Graphics/RendererEnvironmentState.h` extensions
- `CortexEngine/src/Graphics/Renderer_Environment.cpp` extensions
- `CortexEngine/src/Graphics/Renderer_LightingSettings.cpp` extensions

Pseudocode:

```cpp
enum class EnvironmentType
{
    ProceduralSky,
    Cubemap,
    Equirectangular
};

enum class EnvironmentBudgetClass
{
    Tiny,
    Small,
    Medium,
    Large
};

struct EnvironmentDesc
{
    std::string id;
    std::string displayName;
    EnvironmentType type;
    EnvironmentBudgetClass budgetClass;
    std::filesystem::path radiancePath;
    std::filesystem::path irradiancePath;
    std::filesystem::path thumbnailPath;
    float defaultDiffuse = 1.0f;
    float defaultSpecular = 1.0f;
    float defaultBackgroundExposure = 1.0f;
    float defaultBackgroundBlur = 0.0f;
    float defaultRotationDegrees = 0.0f;
    std::vector<std::string> tags;
};

struct EnvironmentLoadResult
{
    bool loaded = false;
    bool fallbackUsed = false;
    std::string activeId;
    std::string requestedId;
    std::string warningCode;
};

class EnvironmentManifestRuntime
{
public:
    bool LoadManifest(const std::filesystem::path& path);
    const EnvironmentDesc* Find(std::string_view id) const;
    EnvironmentLoadResult LoadEnvironment(Renderer& renderer, std::string_view id);
    std::vector<EnvironmentDesc> ListAvailable(EnvironmentBudgetClass maxBudget) const;
};
```

Load flow:

```cpp
EnvironmentLoadResult EnvironmentManifestRuntime::LoadEnvironment(Renderer& renderer, std::string_view id)
{
    const EnvironmentDesc* desc = Find(id);
    if (!desc)
        return LoadFallback(renderer, id, "ENVIRONMENT_ID_NOT_FOUND");

    if (!BudgetAllows(desc->budgetClass, renderer.GetEnvironmentBudget()))
        return LoadFallback(renderer, id, "ENVIRONMENT_OVER_BUDGET");

    if (!FilesExist(*desc))
        return LoadFallback(renderer, id, "ENVIRONMENT_ASSET_MISSING");

    bool ok = renderer.LoadEnvironmentTextures(desc->radiancePath, desc->irradiancePath);
    if (!ok)
        return LoadFallback(renderer, id, "ENVIRONMENT_UPLOAD_FAILED");

    renderer.SetIBLIntensity(desc->defaultDiffuse, desc->defaultSpecular);
    renderer.SetBackgroundPresentation(true,
                                       desc->defaultBackgroundExposure,
                                       desc->defaultBackgroundBlur);
    renderer.SetEnvironmentRotation(desc->defaultRotationDegrees);

    return { true, false, desc->id, std::string(id), "" };
}
```

Frame contract additions:

```json
{
  "environment": {
    "requested": "warm_gallery",
    "active": "warm_gallery",
    "fallback_used": false,
    "budget_class": "medium",
    "diffuse_intensity": 0.9,
    "specular_intensity": 1.15,
    "background_exposure": 0.85,
    "background_blur": 0.25,
    "rotation_degrees": 35.0
  }
}
```

Validation:

- all manifest entries parse
- missing environment falls back
- all enabled IBLs can load under 8 GB profile
- large environment skipped under low-memory profile
- frame contract reports active/fallback environment

## Pass 3L: Procedural Sky and Fallback Background

The fallback background must look acceptable enough for public demo when assets are missing.

Add a procedural sky fallback:

- horizon gradient
- sun disk optional
- ambient color pair
- exposure matched to default tone mapping
- no external texture dependency

Pseudocode:

```hlsl
float3 EvaluateProceduralSky(float3 dir, ProceduralSkyParams params)
{
    float t = saturate(dir.y * 0.5f + 0.5f);
    float3 sky = lerp(params.horizonColor, params.zenithColor, pow(t, params.gradientPower));

    float sunAmount = pow(saturate(dot(dir, params.sunDirection)), params.sunSharpness);
    sky += params.sunColor * sunAmount * params.sunIntensity;

    return sky * params.exposure;
}
```

Validation:

- fallback sky has nonblack coverage
- fallback sky does not saturate most pixels
- default materials remain readable under fallback

## Pass 3M: IBL Review Scene

Add a scene that exercises every environment.

The scene should include:

- mirror sphere
- brushed metal sphere
- rough plastic sphere
- glass object
- water patch
- emissive object
- matte wall/floor
- reference gray card
- camera bookmarks

Scene loop pseudocode:

```cpp
for (const EnvironmentDesc& env : library.ListAvailable(maxBudget)) {
    renderer.SetEnvironmentPreset(env.id);
    RunFrames(48);
    CaptureScreenshot("ibl_gallery_" + env.id + ".png");
    CaptureFrameContract("ibl_gallery_" + env.id + ".json");
    AssertEnvironmentVisible();
    AssertMaterialReadability();
    AssertNoFrameWarnings();
}
```

Validation:

- each environment produces a screenshot
- each screenshot passes luma/colorfulness gates
- RT reflection signal remains nonzero for reflective test objects

---

# Workstream 5: Showcase Scene Polish

## Goal

Create showcase scenes that communicate renderer capability clearly.

Phase 2 made the RT showcase measurable. Phase 3 should make it look intentional.

## Pass 3N: Scene Composition Rules

Apply these rules to every public showcase:

- one clear focal area
- visible background or sky contribution
- materials arranged by purpose
- reflective objects have something useful to reflect
- glass/water objects have readable silhouettes
- emissive objects are present but not overexposed
- camera starts from a strong default composition
- debug overlays are off by default
- validation can turn overlays on

Composition checklist:

```text
Scene has foreground, midground, and background.
Scene has at least one neutral reference material.
Scene has at least one high-reflectance material.
Scene has at least one rough material.
Scene has at least one nonmetal material.
Scene has environment-visible pixels.
Scene has no accidental black void unless intentionally low-key.
Scene remains readable at 1280x720.
Scene remains readable at 1920x1080.
```

## Pass 3O: Public Showcase Scenes

Create or polish these scenes:

1. RT Gallery
   - mirror, brushed metal, glass, emissive accents, floor reflections
   - validates RT reflection readiness and signal

2. Material Lab
   - grid of known material presets
   - validates material classification and albedo/roughness ranges

3. Glass and Water Courtyard
   - glass objects, water plane, sun/sky background
   - validates transparency, water params, reflection mix

4. IBL Gallery
   - same object layout under each environment
   - validates environment loading and IBL response

5. Low-Memory Safe Mode
   - visually acceptable with RT mostly disabled
   - validates fallback behavior

6. Temporal Stress
   - moving camera/object with reflective and emissive surfaces
   - validates TAA/denoiser stability

Scene descriptor pseudocode:

```json
{
  "schema": 1,
  "id": "rt_gallery",
  "display_name": "RT Gallery",
  "default_environment": "studio_softbox",
  "default_graphics_preset": "rt_gallery",
  "camera_bookmarks": [
    {
      "id": "hero",
      "position": [0.0, 1.6, -5.5],
      "target": [0.0, 1.1, 0.0],
      "fov_degrees": 55.0
    },
    {
      "id": "reflection_closeup",
      "position": [-1.8, 1.2, -2.2],
      "target": [0.0, 0.8, 0.0],
      "fov_degrees": 42.0
    }
  ],
  "validation": {
    "min_avg_luma": 45.0,
    "max_dark_ratio": 0.70,
    "requires_rt_reflection_ready": true,
    "min_rt_history_luma": 0.005
  }
}
```

## Pass 3P: Camera Bookmarks and Capture Paths

Add named camera bookmarks.

Required behavior:

- user can switch bookmark from UI
- validation can request bookmark by CLI
- screenshot names include scene/profile/environment/bookmark
- camera state can serialize to scene descriptor

Pseudocode:

```cpp
struct CameraBookmark
{
    std::string id;
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 target;
    float fovDegrees = 60.0f;
};

void SceneCameraController::ApplyBookmark(const CameraBookmark& bookmark)
{
    camera.SetPosition(bookmark.position);
    camera.LookAt(bookmark.target);
    camera.SetFovDegrees(bookmark.fovDegrees);
    camera.ResetTemporalJitterHistory();
}
```

Validation:

- all public scenes have a `hero` bookmark
- screenshot capture from bookmark is stable
- applying bookmark resets temporal history or warms up frames before capture

---

# Workstream 6: Material and Surface Correctness

## Goal

Continue the material correctness direction from Phase 2 and make it visible in UI and showcase scenes.

## Pass 3Q: Material Preset Library

Extend the existing `Graphics/MaterialPresetRegistry` into a more complete first-class material preset library.

Required presets:

- mirror chrome
- brushed aluminum
- rough iron
- polished gold
- black plastic
- white ceramic
- clear glass
- tinted glass
- shallow water
- deep water
- wet stone
- dry masonry
- varnished wood
- matte paint
- emissive panel

Preset schema:

```json
{
  "schema": 1,
  "materials": [
    {
      "id": "mirror_chrome",
      "display_name": "Mirror Chrome",
      "surface_class": "mirror",
      "base_color": [0.85, 0.85, 0.85],
      "metallic": 1.0,
      "roughness": 0.02,
      "specular": 1.0,
      "transmission": 0.0,
      "emissive": [0.0, 0.0, 0.0],
      "validation": {
        "requires_reflection": true,
        "max_roughness": 0.05
      }
    }
  ]
}
```

Validation:

- all material presets parse
- material buffer parity remains true
- preset defaults are not silently used for known showcase materials
- material lab screenshot passes readability gates

## Pass 3R: Material Editor Improvements

Expose material controls in Scene Editor:

- material preset dropdown
- base color
- metallic
- roughness
- specular
- emissive color/intensity
- transmission/alpha if supported
- surface class
- validation status

Pseudocode:

```cpp
void MaterialEditor::Draw(EntityId entity)
{
    Material& material = scene.GetMaterial(entity);
    MaterialValidationResult validation = ValidateMaterial(material);

    DrawPresetDropdown(material);
    DrawColorControl("Base Color", material.baseColor);
    DrawSlider("Metallic", material.metallic, 0.0f, 1.0f);
    DrawSlider("Roughness", material.roughness, 0.02f, 1.0f);
    DrawSlider("Specular", material.specular, 0.0f, 1.0f);
    DrawEmissiveControls(material);
    DrawSurfaceClassDropdown(material.surfaceClass);
    DrawValidationStatus(validation);

    if (Changed())
        renderer.MarkMaterialBufferDirty(entity);
}
```

Validation:

- editing roughness updates material buffer
- changing surface class updates frame contract material counts
- invalid material combinations show warning but do not crash

---

# Workstream 7: RT and Temporal Tuning

## Goal

Use the raw-vs-history signal metrics from Phase 2 to tune reflections and denoising.

## Pass 3S: RT Reflection Tuning Controls

Expose denoiser and composition controls safely.

Controls:

- reflection resolution scale
- max roughness for RT reflection
- denoise blend
- temporal feedback
- history clamp
- firefly clamp
- normal rejection threshold
- roughness rejection threshold
- disocclusion rejection threshold
- composition strength

Pseudocode:

```cpp
struct RTReflectionTuning
{
    float resolutionScale = 1.0f;
    float maxRoughness = 0.65f;
    float denoiseBlend = 0.92f;
    float temporalFeedback = 0.90f;
    float historyClamp = 4.0f;
    float fireflyClamp = 10.0f;
    float normalRejectRadians = 0.35f;
    float roughnessReject = 0.20f;
    float disocclusionReject = 0.02f;
    float compositionStrength = 1.0f;
};
```

Frame contract additions:

```json
{
  "rt_reflection_tuning": {
    "resolution_scale": 1.0,
    "max_roughness": 0.65,
    "denoise_blend": 0.92,
    "history_clamp": 4.0,
    "firefly_clamp": 10.0,
    "composition_strength": 1.0
  },
  "rt_reflection_signal_delta": {
    "raw_avg_luma": 0.0225,
    "history_avg_luma": 0.0314,
    "avg_luma_delta": 0.0089,
    "raw_outlier_ratio": 0.0084,
    "history_outlier_ratio": 0.0089
  }
}
```

Validation:

- denoiser controls are reflected in frame contract
- invalid values clamp
- outlier ratio stays below gate in RT showcase
- history signal does not collapse to zero when raw signal is present

## Pass 3T: Outlier and Firefly Handling

Add robust handling for overbright reflection samples.

Approach:

- clamp reflection radiance before history accumulation
- report raw outlier ratio
- report history outlier ratio
- optionally tint debug overlay for clamped pixels
- avoid hiding real bright emissive reflections entirely

Pseudocode:

```hlsl
float3 ClampReflectionRadiance(float3 radiance, float fireflyClamp)
{
    float luma = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    if (luma <= fireflyClamp)
        return radiance;

    float scale = fireflyClamp / max(luma, 1e-4);
    return radiance * scale;
}
```

Validation:

- emissive scene still shows highlights
- pathological bright material does not blow out the frame
- clamped pixel ratio appears in debug contract

## Pass 3U: RT Scheduler Explanation UI

Add a compact RT scheduler panel.

Show:

- requested features
- scheduled features this frame
- readiness reason
- skipped reason
- resolution scale
- RT memory estimate
- TLAS/BLAS state
- reflection raw/history signal
- current cadence

Pseudocode:

```cpp
void RTSchedulerPanel::Draw(const RayTracingState& rt, const FrameContract& contract)
{
    DrawStatus("DXR", rt.dxrSupported);
    DrawStatus("RT Requested", rt.requested);
    DrawStatus("RT Active", rt.active);
    DrawStatus("Reflections Ready", contract.rtReflection.ready, contract.rtReflection.reason);
    DrawMetric("RT Memory", contract.budgets.rtMemoryMb);
    DrawMetric("Raw Reflection Luma", contract.rtReflectionSignal.rawAvgLuma);
    DrawMetric("History Reflection Luma", contract.rtReflectionSignal.historyAvgLuma);
    DrawMetric("Outlier Ratio", contract.rtReflectionSignal.rawOutlierRatio);
}
```

Validation:

- low-memory profile explains skipped RT features
- no-RT adapter explains DXR unavailable
- ready path reports ready without stale skip reason

---

# Workstream 8: User Experience Polish

## Goal

Make Cortex easier to run, demo, and debug.

## Pass 3V: Launcher and CLI Profiles

Add clear CLI/profile support.

Required flags:

- `--scene <id>`
- `--graphics-preset <id>`
- `--environment <id>`
- `--quality <id>`
- `--camera <bookmark>`
- `--frames <count>`
- `--screenshot <path>`
- `--frame-contract <path>`
- `--validation`
- `--safe-mode`
- `--no-rt`
- `--windowed`
- `--width <pixels>`
- `--height <pixels>`

Pseudocode:

```cpp
struct LaunchOptions
{
    std::string sceneId = "rt_showcase";
    std::string graphicsPreset = "release_showcase";
    std::string environmentId;
    std::string qualityPreset;
    std::string cameraBookmark = "hero";
    uint32_t frameCount = 0;
    std::filesystem::path screenshotPath;
    std::filesystem::path frameContractPath;
    bool validation = false;
    bool safeMode = false;
    bool noRT = false;
    bool windowed = true;
    uint32_t width = 1280;
    uint32_t height = 720;
};
```

Validation:

- unknown flag reports usage and exits nonzero
- unknown scene reports available scene IDs
- unknown environment falls back or exits based on validation mode
- validation mode is deterministic

## Pass 3W: Settings Persistence

Persist user settings separately from release presets.

Suggested files:

- `CortexEngine/config/default_graphics.json`
- user override in app data or local `CortexEngine/config/user_graphics.local.json`

Rules:

- release presets are versioned
- user local settings are not required for repo
- invalid local settings are ignored with warning
- reset button restores release default

Pseudocode:

```cpp
RendererTuningState LoadEffectiveSettings()
{
    RendererTuningState state = LoadPreset("balanced_default");

    if (FileExists(UserSettingsPath())) {
        UserSettingsResult user = TryLoadUserSettings(UserSettingsPath());
        if (user.ok)
            state = MergeSettings(state, user.state);
        else
            LogWarning("USER_SETTINGS_INVALID", user.error);
    }

    return ClampRendererTuningState(state);
}
```

Validation:

- corrupt user settings do not prevent startup
- save settings creates valid JSON
- reset deletes or ignores local override

## Pass 3X: Clean HUD Modes

Add HUD modes:

- Off
- Minimal
- Performance
- Renderer Health
- Full Debug

Minimal should show only:

- FPS or frame ms
- active scene
- active preset
- RT status icon/text

Full Debug can show:

- frame contract warnings
- budgets
- pass timings
- RT readiness
- material stats
- environment stats

Pseudocode:

```cpp
enum class HudMode
{
    Off,
    Minimal,
    Performance,
    RendererHealth,
    FullDebug
};

void Hud::Draw(HudMode mode)
{
    switch (mode) {
    case HudMode::Off:
        return;
    case HudMode::Minimal:
        DrawMinimalStatus();
        break;
    case HudMode::Performance:
        DrawFrameTimes();
        DrawGpuTimings();
        break;
    case HudMode::RendererHealth:
        DrawHealthState();
        break;
    case HudMode::FullDebug:
        DrawEverything();
        break;
    }
}
```

Validation:

- default public run uses Minimal or Off
- validation can force Full Debug
- HUD does not overlap screenshots when disabled

---

# Workstream 9: Renderer Architecture Cleanup

## Goal

Continue shrinking renderer sprawl while implementing Phase 3 features.

Do not perform a risky monolithic rewrite. Extract state around the systems Phase 3 touches.

## Pass 3Y: Presentation State Structs

Create named state groups:

- `RendererTuningState`
- `EnvironmentState`
- `PostProcessState`
- `AtmosphereState`
- `WaterPresentationState`
- `RTReflectionPresentationState`
- `RendererHealthState`

Example:

```cpp
struct EnvironmentState
{
    std::string requestedId;
    std::string activeId;
    bool iblEnabled = true;
    bool fallbackUsed = false;
    float diffuseIntensity = 1.0f;
    float specularIntensity = 1.0f;
    float backgroundExposure = 1.0f;
    float backgroundBlur = 0.0f;
    float rotationDegrees = 0.0f;
    EnvironmentBudgetClass budgetClass = EnvironmentBudgetClass::Small;
};

struct PostProcessState
{
    float exposure = 1.0f;
    float bloomIntensity = 0.08f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float warmth = 0.0f;
    bool taaEnabled = true;
    bool fxaaEnabled = false;
};
```

Migration rule:

- when adding a UI control, first ensure it maps to a state field
- renderer API applies state field
- frame contract reads from state field
- validation compares against state field

## Pass 3Z: Pass-Owned Resource Bundles

Keep moving pass-specific resources out of top-level `Renderer`.

Candidate bundles:

- `RTReflectionResources`
- `RTReflectionStatsResources`
- `EnvironmentResources`
- `PostProcessResources`
- `TemporalValidationResources`
- `ShowcaseCaptureResources`

Pattern:

```cpp
struct RTReflectionStatsResources
{
    GpuBuffer rawStats;
    GpuBuffer historyStats;
    ReadbackBuffer rawReadback;
    ReadbackBuffer historyReadback;
    DescriptorTable sharedStatsTable;

    bool Create(RendererDevice& device, DescriptorAllocator& descriptors);
    void Release();
    bool IsValid() const;
};
```

Validation:

- no descriptor budget regression
- resource names appear in resource contract
- smoke tests pass after each extraction

---

# Workstream 10: Advanced Shaders, Lighting, and Particles

## Goal

Make Cortex visually rich while keeping Phase 2's measurement discipline.

This workstream is where the engine grows beyond clean RT reflections and stable material classification into complex shaders, cinematic lighting, and particle graphics. The key rule is that these features must be controllable, budgeted, and validated. They should not become a pile of one-off shader hacks.

This is a major feature-project workstream. Treat it as Phase 3 late-stage work or Phase 3.5/Phase 4 if robustness, controls, environments, and showcase scenes are not stable yet.

Repo reality:

- material preset and advanced material fields already exist in `Graphics/MaterialPresetRegistry.h` and related material code
- clearcoat/sheen/subsurface-style fields are already represented in `MaterialPresetInfo`
- lighting rigs already exist in `Renderer::ApplyLightingRig` and `Graphics/RendererLightingRigControl`
- particle rendering already exists through ECS particle emitters, `Renderer_Particles.cpp`, `RendererParticleState.h`, `GPUParticles.h`, and particle shaders
- post-processing already exists in `assets/shaders/PostProcess.hlsl` and renderer post/bloom paths

The Phase 3 job is to consolidate, expose, validate, and showcase these systems before inventing replacements.

## Pass 3AA: Advanced Material Shader Framework

Extend the existing material model and preset registry into a structured material extension layer for richer shaders.

Target material features:

- clearcoat
- anisotropic metal
- layered roughness
- wet surface response
- cloth/sheen
- simple subsurface approximation for wax/skin-like materials
- emissive bloom contribution
- procedural masks for base color, roughness, normal, and emissive variation

Suggested files:

- `CortexEngine/src/Graphics/MaterialState.h` extensions
- `CortexEngine/src/Graphics/MaterialPresetRegistry.h` extensions
- `CortexEngine/src/Graphics/MaterialModel.cpp` extensions
- `CortexEngine/assets/shaders/PBR_Lighting.hlsli` extensions
- `CortexEngine/assets/shaders/DeferredLighting.hlsl` extensions
- `CortexEngine/assets/shaders/RaytracingMaterials.hlsli` parity updates
- optional new `CortexEngine/assets/shaders/MaterialAdvanced.hlsli`
- optional new `CortexEngine/assets/shaders/ProceduralMaterialNoise.hlsli`

Pseudocode:

```cpp
enum class AdvancedMaterialFeature : uint32_t
{
    Clearcoat       = 1u << 0,
    Anisotropy      = 1u << 1,
    Sheen           = 1u << 2,
    Subsurface      = 1u << 3,
    WetSurface      = 1u << 4,
    ProceduralNoise = 1u << 5,
    EmissiveBloom   = 1u << 6
};

struct AdvancedMaterialState
{
    uint32_t featureMask = 0;

    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.2f;

    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;

    float sheen = 0.0f;
    float sheenRoughness = 0.5f;

    float subsurface = 0.0f;
    DirectX::XMFLOAT3 subsurfaceColor = {1.0f, 0.75f, 0.55f};

    float wetness = 0.0f;
    float proceduralScale = 1.0f;
    float proceduralStrength = 0.0f;

    float emissiveBloomScale = 1.0f;
};

MaterialValidationResult ValidateAdvancedMaterial(const Material& base,
                                                  const AdvancedMaterialState& advanced)
{
    MaterialValidationResult result;

    if (advanced.clearcoat > 0.0f && base.roughness > 0.9f)
        result.AddWarning("CLEARCOAT_ON_VERY_ROUGH_BASE");

    if (advanced.subsurface > 0.0f && base.metallic > 0.0f)
        result.AddError("SUBSURFACE_METAL_INVALID");

    if (advanced.emissiveBloomScale > 8.0f)
        result.AddWarning("EMISSIVE_BLOOM_HIGH");

    return result;
}
```

Shader pseudocode:

```hlsl
MaterialLighting EvaluateAdvancedMaterial(MaterialInput input,
                                          AdvancedMaterialParams advanced,
                                          LightingContext lighting)
{
    MaterialLighting result = EvaluateBasePBR(input, lighting);

    if (HasFeature(advanced.features, MATERIAL_FEATURE_CLEARCOAT)) {
        result.specular += EvaluateClearcoatLobe(input, advanced, lighting);
    }

    if (HasFeature(advanced.features, MATERIAL_FEATURE_ANISOTROPY)) {
        result.specular = ApplyAnisotropicSpecular(input, advanced, lighting);
    }

    if (HasFeature(advanced.features, MATERIAL_FEATURE_SHEEN)) {
        result.diffuse += EvaluateSheen(input, advanced, lighting);
    }

    if (HasFeature(advanced.features, MATERIAL_FEATURE_WET_SURFACE)) {
        result = ApplyWetSurfaceResponse(result, input, advanced);
    }

    if (HasFeature(advanced.features, MATERIAL_FEATURE_EMISSIVE_BLOOM)) {
        result.emissive *= advanced.emissiveBloomScale;
    }

    return result;
}
```

Validation:

- material buffer parity still passes
- advanced material defaults are zero-cost visually
- invalid feature combinations are reported
- material lab has rows for advanced shaders
- RT path either supports the feature or reports an approximation/fallback

## Pass 3AB: Cinematic Lighting Rigs

Extend the existing lighting rig path into named rigs that can be selected from UI, scene descriptors, and CLI.

Initial rigs:

- neutral studio
- warm gallery
- cool overcast
- sunset rim
- night emissive
- warehouse low-key
- high-contrast product
- soft material review

Pseudocode:

```cpp
struct LightRigDesc
{
    std::string id;
    std::string displayName;
    DirectionalLight sun;
    std::vector<AreaLightDesc> areaLights;
    DirectX::XMFLOAT3 ambientTint = {1.0f, 1.0f, 1.0f};
    float iblDiffuseScale = 1.0f;
    float iblSpecularScale = 1.0f;
    float fogDensity = 0.0f;
    float godRayIntensity = 0.0f;
    float exposure = 1.0f;
    float bloom = 0.08f;
};

void RendererTuningController::ApplyLightRig(Renderer& renderer, const LightRigDesc& rig)
{
    renderer.SetSunDirection(rig.sun.direction);
    renderer.SetSunColor(rig.sun.color);
    renderer.SetSunIntensity(rig.sun.intensity);
    renderer.SetAreaLights(rig.areaLights);
    renderer.SetIBLIntensity(rig.iblDiffuseScale, rig.iblSpecularScale);
    renderer.SetFogParams(BuildFogParams(rig));
    renderer.SetGodRayIntensity(rig.godRayIntensity);
    renderer.SetExposure(rig.exposure);
    renderer.SetBloomIntensity(rig.bloom);
}
```

Frame contract additions:

```json
{
  "lighting_rig": {
    "id": "warm_gallery",
    "area_light_count": 3,
    "sun_intensity": 2.5,
    "ibl_diffuse": 0.9,
    "ibl_specular": 1.15,
    "fog_density": 0.01,
    "bloom": 0.08
  }
}
```

Validation:

- all rig descriptors parse
- every public showcase has an explicit rig
- switching rigs updates frame contract
- lighting rigs do not break screenshot exposure gates

## Pass 3AC: GPU Particle System Foundation

Consolidate the existing particle work into a dependable, budgeted effects path.

The repo already has CPU/ECS billboard rendering, a `GPUParticleSystem` implementation, and particle shaders. Start by deciding which path is the public Phase 3 path, then make that path measurable and controllable. Avoid a full VFX editor in Phase 3.

Particle modes:

- additive sparks
- alpha-blended smoke
- soft particles against depth
- emissive particles contributing to bloom
- lit particles for dust/mist

Suggested files:

- `CortexEngine/src/Graphics/GPUParticles.h` extensions
- `CortexEngine/src/Graphics/GPUParticles.cpp` extensions
- `CortexEngine/src/Graphics/RendererParticleState.h` extensions
- `CortexEngine/src/Graphics/Renderer_Particles.cpp` extensions
- `CortexEngine/src/Scene/Components.h` particle component extensions
- `CortexEngine/assets/shaders/ParticleSimulate.hlsl`
- `CortexEngine/assets/shaders/ParticleRender.hlsl`
- `CortexEngine/assets/shaders/Particles.hlsl`

Pseudocode:

```cpp
enum class ParticleBlendMode
{
    Additive,
    AlphaBlend,
    Premultiplied,
    LitAlpha
};

struct ParticleEffectEmitterDesc
{
    std::string id;
    uint32_t maxParticles = 1024;
    float spawnRate = 64.0f;
    float lifetime = 2.0f;
    DirectX::XMFLOAT3 origin = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 initialVelocity = {0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 velocityRandomness = {0.5f, 0.5f, 0.5f};
    DirectX::XMFLOAT4 startColor = {1.0f, 0.6f, 0.2f, 1.0f};
    DirectX::XMFLOAT4 endColor = {0.1f, 0.1f, 0.1f, 0.0f};
    float startSize = 0.05f;
    float endSize = 0.25f;
    ParticleBlendMode blendMode = ParticleBlendMode::Additive;
    bool depthSoftening = true;
    bool contributesToBloom = true;
};

class PublicParticleEffectController
{
public:
    bool Initialize(GPUParticleSystem& gpuParticles, uint32_t maxParticles);
    void AddEmitter(const ParticleEffectEmitterDesc& emitter);
    void SetQualityScale(float scale);
    void Update(float dt);
    void ApplyToSceneEmitters(Scene::ECS_Registry& registry);
    ParticleStats BuildStats() const;
};
```

Simulation shader pseudocode:

```hlsl
[numthreads(128, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    Particle p = particles[id.x];

    if (!p.alive) {
        TrySpawnParticle(p, emitter, frameRandom);
    } else {
        p.age += dt;
        p.velocity += emitter.gravity * dt;
        p.position += p.velocity * dt;
        p.normalizedAge = saturate(p.age / p.lifetime);

        if (p.age >= p.lifetime)
            p.alive = false;
    }

    particles[id.x] = p;
}
```

Frame contract additions:

```json
{
  "particles": {
    "enabled": true,
    "quality_scale": 1.0,
    "emitter_count": 4,
    "live_particles": 3840,
    "max_particles": 8192,
    "gpu_ms": 0.22,
    "budget_class": "balanced"
  }
}
```

Validation:

- particles disabled path has zero cost
- particle smoke scene produces nonzero live particles
- particle pass reports timings and resource usage
- low-memory profile reduces max particles
- particles do not break depth, bloom, or screenshot gates

## Pass 3AD: Particle Effect Library

Create reusable particle effects as data on top of the existing particle paths.

Initial effects:

- dust motes
- sparks
- embers
- smoke wisps
- mist
- rain
- snow
- energy arcs
- waterfall spray

Effect schema:

```json
{
  "schema": 1,
  "effects": [
    {
      "id": "dust_motes",
      "display_name": "Dust Motes",
      "budget_class": "tiny",
      "emitters": [
        {
          "max_particles": 512,
          "spawn_rate": 32.0,
          "lifetime": 6.0,
          "blend": "lit_alpha",
          "start_size": 0.01,
          "end_size": 0.03,
          "depth_softening": true,
          "bloom": false
        }
      ]
    }
  ]
}
```

Validation:

- all effect descriptors parse
- missing particle texture falls back to default soft circle
- each public effect can run in an effect gallery scene
- effect budget class controls spawn count

## Pass 3AE: Volumetric and Atmospheric Polish

Improve fog, god rays, and atmosphere enough to support cinematic scenes.

Target features:

- height fog controls
- directional light shafts
- local fog volumes if practical
- particle/fog depth consistency
- environment-matched fog color
- low-cost fallback for safe profile

Pseudocode:

```cpp
struct AtmosphereAdvancedTuning
{
    bool volumetricEnabled = false;
    float heightFogDensity = 0.015f;
    float heightFogFalloff = 0.25f;
    float lightShaftIntensity = 0.15f;
    float anisotropy = 0.2f;
    DirectX::XMFLOAT3 fogAlbedo = {0.72f, 0.78f, 0.85f};
};
```

Validation:

- fog does not hide focal objects in public scenes
- atmosphere can be disabled cleanly
- low-memory profile uses cheap fog
- screenshot luma gates catch over-fogging

## Pass 3AF: Cinematic Post Stack

Extend the existing post-processing path with controlled polish.

Features:

- filmic tone mapping preset control
- bloom threshold and soft knee
- subtle vignette
- optional lens dirt
- optional depth of field for screenshots
- optional motion blur only if stable
- color grading presets

Defaults should remain conservative. Public scenes should look polished without heavy artifacts.

Pseudocode:

```cpp
struct CinematicPostTuning
{
    std::string toneMapper = "filmic";
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float vignette = 0.0f;
    float lensDirt = 0.0f;
    bool depthOfFieldEnabled = false;
    float focusDistance = 4.0f;
    float aperture = 0.0f;
    bool motionBlurEnabled = false;
    float motionBlurStrength = 0.0f;
    std::string colorGradePreset = "neutral";
};
```

Validation:

- post stack can be disabled
- release preset avoids excessive vignette/lens dirt
- DOF only affects screenshot/showcase profiles by default
- screenshot stats catch over-bloom and saturation

## Pass 3AG: Effects Showcase Scene

Add a scene dedicated to the advanced effects layer.

Scene requirements:

- complex materials row
- warm/cool lighting rig comparison
- emissive panels
- dust motes in light shafts
- sparks/embers area
- mist or smoke volume
- reflective floor or metal object
- screenshot bookmarks

Validation:

- scene launches under balanced profile
- particles live count is nonzero
- advanced material count is nonzero
- screenshot is not black, overexposed, or fully fogged
- GPU time remains under configured showcase budget

---

# Detailed Pass Sequence

## Phase 3A - Startup Preflight

Deliverables:

- startup preflight result type
- required/optional asset checks
- config parse check
- frame contract startup section
- validation case with missing optional environment

Tests:

- build
- launch default
- launch with optional IBL missing
- release validation

## Phase 3B - Renderer Health State

Deliverables:

- `RendererHealthState`
- UI health summary
- frame contract health section
- release validation summary line

Tests:

- health state smoke
- budget matrix
- release validation

## Phase 3C - Fatal Error UX

Deliverables:

- crash summary JSON
- validation-mode nonzero exit
- interactive message path

Tests:

- forced invalid shader path
- forced validation fatal

## Phase 3D - Visual Matrix Script

Deliverables:

- `run_phase3_visual_matrix.ps1`
- JSON/Markdown matrix output
- screenshot output folder

Tests:

- matrix dry run
- matrix full run

## Phase 3E - Screenshot Contract

Deliverables:

- extend existing `Core/VisualValidation` screenshot stats collector
- screenshot gates
- scene-aware gate reporting

Tests:

- black screenshot forced failure
- current showcase pass

## Phase 3F - Visual Gate Stabilization

Deliverables:

- hardened visual metric thresholds
- forced black/overbright failure cases
- clear failure messages naming metric and observed value
- written rule that golden baselines are deferred until scene/camera/lighting stability

Tests:

- forced black screenshot failure
- forced overexposure failure
- current RT showcase pass
- current temporal validation pass

## Phase 3G - Renderer Tuning State

Deliverables:

- serializable settings structs
- clamp functions
- apply/capture controller that routes through `RendererControlApplier`
- initial unit/smoke test

Tests:

- clamp test
- save/load round trip
- release validation

## Phase 3H - Graphics Settings Window

Deliverables:

- tabbed graphics window
- sliders/toggles/dropdowns
- live budget strip
- reset/apply buttons

Tests:

- UI smoke
- slider clamp smoke
- no crash on all tabs

## Phase 3I - Graphics Presets

Deliverables:

- preset directory
- release presets
- CLI preset load
- user preset save

Tests:

- preset round trip
- invalid preset fallback
- release validation with `release_showcase`

## Phase 3I.5 - IBL Asset Policy Lock

Deliverables:

- exact committed default IBL decision
- max runtime resolution and size per budget class
- DDS/HDR/EXR runtime/source policy
- optional download/generation policy
- procedural sky fallback rule
- mapping to `BudgetPlanner` and `AssetRegistry`

Tests:

- default IBL exists or fallback path is selected
- low-memory profile does not eagerly load medium/large optional IBLs
- missing optional IBL warns without fatal startup failure

## Phase 3J - Environment Manifest

Deliverables:

- environment schema under `assets/environments`
- initial manifest
- manifest parser extending existing `Renderer_Environment` flow

Tests:

- all entries parse
- missing optional thumbnail warning

## Phase 3K - Environment Library Runtime

Deliverables:

- thin manifest/runtime layer around `RendererEnvironmentState`
- budget filtering
- fallback load result
- frame contract environment section

Tests:

- load each environment
- low-memory skip
- missing selected environment fallback

## Phase 3L - Procedural Sky Fallback

Deliverables:

- procedural sky shader/path
- fallback selection
- screenshot gates

Tests:

- run with environment assets unavailable
- visual stats pass

## Phase 3M - IBL Review Scene

Deliverables:

- IBL gallery scene
- camera bookmarks
- matrix capture

Tests:

- all environments screenshot
- no black/overbright captures

## Phase 3N - Scene Composition Cleanup

Deliverables:

- composition checklist applied to public scenes
- debug overlays off by default
- updated default cameras

Tests:

- visual matrix
- screenshot review

## Phase 3O - Public Showcase Scenes

Deliverables:

- RT Gallery
- Material Lab
- Glass and Water Courtyard
- IBL Gallery
- Low-Memory Safe Mode
- Temporal Stress

Tests:

- each scene launch
- each scene screenshot
- scene-specific contracts

## Phase 3P - Camera Bookmarks

Deliverables:

- scene bookmark schema
- CLI bookmark selection
- UI bookmark dropdown
- temporal warmup after bookmark

Tests:

- all public scenes have hero bookmark
- screenshot from bookmark stable

## Phase 3Q - Material Preset Library

Deliverables:

- material preset schema
- initial preset list
- parser
- material validation integration

Tests:

- all presets parse
- material parity
- material lab screenshot

## Phase 3R - Material Editor

Deliverables:

- preset dropdown
- material sliders
- validation status
- renderer dirty update

Tests:

- edit material smoke
- material buffer parity

## Phase 3S - RT Reflection Tuning

Deliverables:

- tuning struct
- UI controls
- frame contract fields
- denoiser apply path

Tests:

- clamp test
- RT showcase signal gates
- temporal validation

## Phase 3T - Outlier Handling

Deliverables:

- firefly clamp
- outlier metrics
- debug overlay optional

Tests:

- bright emissive test
- RT showcase
- overbright gate

## Phase 3U - RT Scheduler Explanation UI

Deliverables:

- scheduler panel
- skipped/ready reason display
- low-memory explanation

Tests:

- 8 GB ready path
- 4 GB scheduled/skipped path
- no-RT fallback path

## Phase 3V - Launcher and CLI Profiles

Deliverables:

- launch option parser
- usage output
- scene/profile/environment/camera flags

Tests:

- unknown flag
- unknown scene
- validation CLI cases

## Phase 3W - Settings Persistence

Deliverables:

- default settings load
- user local override
- reset path

Tests:

- corrupt settings ignored
- save/load round trip

## Phase 3X - Clean HUD Modes

Deliverables:

- HUD mode enum
- minimal HUD
- full debug HUD
- UI selector

Tests:

- screenshot with HUD off
- debug mode visible

## Phase 3Y - Presentation State Structs

Deliverables:

- state struct extraction for touched features
- frame contract reads from state
- UI applies to state

Tests:

- build
- release validation
- settings round trip

## Phase 3Z - Pass-Owned Resource Bundles

Deliverables:

- resource bundle extraction for RT stats/environment/postprocess where touched
- descriptor contract unchanged or improved

Tests:

- descriptor budget matrix
- release validation

## Phase 3AA - Advanced Material Shader Framework

Deliverables:

- advanced material feature mask
- clearcoat, anisotropy, sheen, wetness, subsurface approximation fields
- procedural material noise helpers
- validation for invalid feature combinations
- material lab advanced shader row

Tests:

- material validation
- material buffer parity
- RT showcase
- material lab screenshot

## Phase 3AB - Cinematic Lighting Rigs

Deliverables:

- lighting rig descriptor schema
- named rig library
- UI rig dropdown
- scene descriptor rig selection
- frame contract lighting rig section

Tests:

- all rigs parse
- all public scenes use explicit rigs
- screenshot gates pass under each public rig

## Phase 3AC - GPU Particle System Foundation

Deliverables:

- particle simulation pass
- particle render pass
- additive, alpha, lit-alpha, and soft particle modes
- quality scaling
- particle stats in frame contract

Tests:

- particles disabled zero-cost path
- particle smoke scene
- low-memory particle scale
- bloom/depth integration smoke

## Phase 3AD - Particle Effect Library

Deliverables:

- particle effect descriptor schema
- initial effects for dust, sparks, embers, smoke, mist, rain, snow, energy arcs, spray
- fallback particle texture
- effect budget classes

Tests:

- all effects parse
- effect gallery smoke
- missing texture fallback
- budget class spawn scaling

## Phase 3AE - Volumetric and Atmospheric Polish

Deliverables:

- advanced atmosphere tuning state
- height fog polish
- light shaft controls
- environment-matched fog color
- low-cost atmosphere fallback

Tests:

- glass/water courtyard
- effects showcase
- low-memory safe profile
- screenshot over-fogging gates

## Phase 3AF - Cinematic Post Stack

Deliverables:

- filmic tone mapping preset controls
- bloom threshold/knee
- subtle vignette control
- optional lens dirt
- optional depth of field for showcase capture
- color grading presets

Tests:

- post stack disable path
- release preset screenshot gates
- over-bloom forced failure
- showcase capture with DOF warmup

## Phase 3AG - Effects Showcase Scene

Deliverables:

- advanced effects showcase scene
- complex material row
- particles and emissives
- cinematic lighting rig
- camera bookmarks
- scene-specific validation gates

Tests:

- effects showcase launch
- particle live count nonzero
- advanced material count nonzero
- screenshot gates
- GPU budget gate

---

# Graphics Slider Inventory

This is the target UI control inventory. Implement incrementally, but keep names and ranges stable once public.

## Quality

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Quality Preset | dropdown | preset ids | 8gb_balanced | quality profile apply |
| Render Scale | slider | 0.50-1.00 | 1.00 | `SetRenderScale` |
| TAA | toggle | off/on | on | `SetTAAEnabled` |
| FXAA | toggle | off/on | off | `SetFXAAEnabled` |
| GPU Culling | toggle | off/on | on | `SetGPUCullingEnabled` |
| Safe Lighting Rig | toggle | off/on | auto | `SetUseSafeLightingRigOnLowVRAM` |

## Tone and Lighting

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Lighting Rig | dropdown | rig ids | scene | new light rig library |
| Exposure | slider | 0.10-4.00 | 1.00 | `SetExposure` |
| Bloom | slider | 0.00-1.50 | 0.08 | `SetBloomIntensity` |
| Contrast | slider | 0.50-1.75 | 1.00 | `SetColorGrade` |
| Saturation | slider | 0.00-2.00 | 1.00 | `SetColorGrade` |
| Warmth | slider | -1.00-1.00 | 0.00 | `SetColorGrade` |
| Sun Intensity | slider | 0.00-20.00 | scene | `SetSunIntensity` |
| Sun Color | color | RGB | scene | `SetSunColor` |
| Sun Direction | vector/orbit | unit sphere | scene | `SetSunDirection` |
| Area Light Size | slider | 0.10-10.00 | 1.00 | `SetAreaLightSizeScale` |

## Environment and IBL

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Environment | dropdown | manifest ids | studio_softbox | `SetEnvironmentPreset` |
| IBL Enabled | toggle | off/on | on | `SetIBLEnabled` |
| Diffuse IBL | slider | 0.00-3.00 | 1.00 | `SetIBLIntensity` |
| Specular IBL | slider | 0.00-3.00 | 1.00 | `SetIBLIntensity` |
| Background Visible | toggle | off/on | on | new background state |
| Background Exposure | slider | 0.00-3.00 | 1.00 | new background state |
| Background Blur | slider | 0.00-1.00 | 0.00 | new background state |
| Rotation | slider | 0-360 deg | 0 | new environment rotation |

## Ray Tracing

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| RT Master | toggle | off/on | on | `SetRayTracingEnabled` |
| RT Reflections | toggle | off/on | on | `SetRTReflectionsEnabled` |
| RT GI | toggle | off/on | off | `SetRTGIEnabled` |
| Reflection Scale | slider | 0.25-1.00 | 1.00 | new RT reflection tuning |
| Max Roughness | slider | 0.05-1.00 | 0.65 | new RT reflection tuning |
| Denoise Blend | slider | 0.00-1.00 | 0.92 | new RT reflection tuning |
| Temporal Feedback | slider | 0.00-0.98 | 0.90 | new RT reflection tuning |
| History Clamp | slider | 0.50-16.00 | 4.00 | new RT reflection tuning |
| Firefly Clamp | slider | 1.00-64.00 | 10.00 | new RT reflection tuning |
| Composition Strength | slider | 0.00-2.00 | 1.00 | new RT reflection tuning |

## Screen Space

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| SSAO | toggle | off/on | on | `SetSSAOEnabled` |
| SSAO Radius | slider | 0.10-5.00 | 1.50 | `SetSSAOParams` |
| SSAO Bias | slider | 0.00-0.20 | 0.02 | `SetSSAOParams` |
| SSAO Intensity | slider | 0.00-3.00 | 1.00 | `SetSSAOParams` |
| SSR | toggle | off/on | on | `SetSSREnabled` |
| SSR Strength | slider | 0.00-2.00 | 0.75 | new SSR tuning if missing |
| SSR Max Distance | slider | 1.00-100.00 | 40.00 | new SSR tuning if missing |
| SSR Thickness | slider | 0.01-1.00 | 0.12 | new SSR tuning if missing |

## Atmosphere

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Fog | toggle | off/on | on | `SetFogEnabled` |
| Fog Density | slider | 0.00-0.20 | 0.015 | `SetFogParams` |
| Fog Start | slider | 0.00-100.00 | 8.00 | `SetFogParams` |
| Height Falloff | slider | 0.00-2.00 | 0.25 | `SetFogParams` |
| God Rays | slider | 0.00-2.00 | 0.15 | `SetGodRayIntensity` |

## Water

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Wave Amplitude | slider | 0.00-1.00 | 0.08 | `SetWaterParams` |
| Wavelength | slider | 0.25-20.00 | 4.00 | `SetWaterParams` |
| Wave Speed | slider | 0.00-3.00 | 0.45 | `SetWaterParams` |
| Fresnel Strength | slider | 0.00-4.00 | 1.00 | `SetWaterParams` |
| Water Roughness | slider | 0.00-0.50 | 0.05 | `SetWaterParams` |

## Advanced Materials

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Material Feature Preset | dropdown | preset ids | material | advanced material state |
| Clearcoat | slider | 0.00-1.00 | 0.00 | advanced material state |
| Clearcoat Roughness | slider | 0.02-1.00 | 0.20 | advanced material state |
| Anisotropy | slider | -1.00-1.00 | 0.00 | advanced material state |
| Anisotropy Rotation | slider | 0-360 deg | 0 | advanced material state |
| Sheen | slider | 0.00-1.00 | 0.00 | advanced material state |
| Subsurface | slider | 0.00-1.00 | 0.00 | advanced material state |
| Wetness | slider | 0.00-1.00 | 0.00 | advanced material state |
| Procedural Scale | slider | 0.10-64.00 | 1.00 | procedural material state |
| Procedural Strength | slider | 0.00-1.00 | 0.00 | procedural material state |
| Emissive Bloom Scale | slider | 0.00-8.00 | 1.00 | advanced material state |

## Particles

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Particles Enabled | toggle | off/on | on | particle system state |
| Effects Quality | dropdown | low/balanced/high | balanced | particle budget class |
| Particle Density | slider | 0.00-2.00 | 1.00 | particle quality scale |
| Effect Preset | dropdown | effect ids | scene | particle effect library |
| Bloom Contribution | slider | 0.00-2.00 | 1.00 | particle render state |
| Soft Particle Depth | slider | 0.00-2.00 | 1.00 | particle render state |
| Wind Strength | slider | 0.00-5.00 | 0.50 | particle simulation state |

## Cinematic Post

| Control | Type | Range | Default | Renderer Mapping |
|---|---|---:|---:|---|
| Tone Mapper | dropdown | preset ids | filmic | cinematic post state |
| Bloom Threshold | slider | 0.10-10.00 | 1.00 | cinematic post state |
| Bloom Soft Knee | slider | 0.00-1.00 | 0.50 | cinematic post state |
| Vignette | slider | 0.00-1.00 | 0.00 | cinematic post state |
| Lens Dirt | slider | 0.00-1.00 | 0.00 | cinematic post state |
| Depth of Field | toggle | off/on | off | cinematic post state |
| Focus Distance | slider | 0.10-100.00 | 4.00 | cinematic post state |
| Aperture | slider | 0.00-8.00 | 0.00 | cinematic post state |
| Motion Blur | toggle | off/on | off | cinematic post state |
| Motion Blur Strength | slider | 0.00-1.00 | 0.00 | cinematic post state |
| Color Grade Preset | dropdown | preset ids | neutral | cinematic post state |

---

# Frame Contract Additions

Phase 3 should extend the frame contract without making it noisy.

Add these top-level sections:

```json
{
  "startup": {},
  "health": {},
  "graphics_preset": {},
  "environment": {},
  "lighting_rig": {},
  "ui_state": {},
  "screenshot_stats": {},
  "rt_reflection_tuning": {},
  "advanced_materials": {},
  "particles": {},
  "cinematic_post": {},
  "showcase_scene": {}
}
```

Suggested compact shape:

```json
{
  "graphics_preset": {
    "id": "release_showcase",
    "schema": 1,
    "dirty_from_ui": false
  },
  "ui_state": {
    "graphics_window_open": false,
    "hud_mode": "minimal"
  },
  "showcase_scene": {
    "id": "rt_gallery",
    "camera_bookmark": "hero",
    "validation_profile": "public_release"
  }
}
```

Validation rules:

- unknown preset cannot be silently reported as active
- active environment must match requested or explicitly report fallback
- active lighting rig must be explicit for public showcase scenes
- advanced material and particle counts must match scene expectations when validation enables them
- UI dirty state should not affect validation unless requested
- screenshot stats should only be required when screenshot capture is enabled

---

# Release Gates

Phase 3 is complete only when these pass from a clean build:

1. Engine build
2. Existing release validation
3. Phase 3 visual matrix
4. Graphics preset round-trip test
5. IBL asset policy test
6. Environment manifest test
7. IBL gallery matrix
8. UI smoke for graphics settings
9. Material preset validation
10. RT reflection tuning validation
11. Low-memory safe profile validation
12. Advanced material shader validation
13. Particle effect gallery validation
14. Effects showcase visual validation

Required commands by end of Phase 3:

```powershell
cmake --build CortexEngine\build --config Release
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_phase3_visual_matrix.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_graphics_preset_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_environment_manifest_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_effects_gallery_tests.ps1
```

Acceptance thresholds:

- no fatal startup warnings in default profile
- no frame contract warnings in release showcase
- no descriptor budget regression
- no DXGI memory budget regression
- RT showcase reflection readiness is true on supported profile
- raw and history reflection signals are nonzero in RT showcase
- fallback profile runs without RT
- IBL gallery screenshots are nonblack and not overexposed
- default IBL policy is documented and enforced before manifest loading
- no golden baseline files are required until scene/camera/lighting stability is reached
- effects showcase has nonzero advanced materials and live particles
- particle disabled profile has no measurable particle cost
- UI settings save/load round trip passes
- public README instructions match actual CLI/scripts

---

# Risk Register

| Risk | Why It Matters | Mitigation |
|---|---|---|
| UI sliders expose unstable internals | Users can create broken states | Clamp every setting, add reset, serialize only supported fields |
| Environment assets bloat repo | Public release becomes heavy | Use manifest budget classes and small default set |
| IBL loading increases descriptor pressure | Phase 2 had descriptor budget regressions | Reuse descriptor tables, add environment budget tests |
| Visual baselines become flaky | GPU variance can cause false failures | Use tolerant metric gates, not exact pixel matching |
| Renderer state extraction causes regressions | Renderer is still large | Extract only touched systems and validate after each pass |
| RT tuning hides real bugs | Sliders can mask bad defaults | Keep release preset strict and frame contract explicit |
| Missing assets break demos | Public reviewers may run incomplete checkout | Procedural sky and default material fallbacks |
| Low-end hardware looks broken | RT/IBL may not fit budget | Safe profile, clear scheduler explanations, fallback screenshots |
| Settings files drift | Old presets may parse incorrectly | Schema versioning and migration warnings |
| Advanced shaders break RT/material parity | Raster and RT paths may disagree | Feature masks, approximation reporting, material parity gates |
| Particles hide scene readability | Smoke/fog can obscure the actual renderer showcase | Screenshot gates, density presets, public-scene composition rules |
| Cinematic post becomes gimmicky | Heavy vignette/blur/lens dirt can look amateur or hide artifacts | Conservative defaults, post disable path, visual matrix thresholds |
| Particle simulation adds GPU cost spikes | Effects can destabilize frame time | Budget classes, live particle caps, GPU timing gates |

---

# Pre-Phase 3 Readiness Review

Before starting implementation, these are the known gaps that need disciplined planning. They are not small polish items; each one can create churn if attacked without boundaries.

## Renderer Is Still Large And Cross-Cutting

What is wrong:

- `Renderer` still owns too many pass-specific resources, feature states, diagnostics, scheduling decisions, and presentation controls.
- Many files have been split out, but ownership is still centered on the renderer object rather than on pass/state modules.
- Cross-cutting changes are risky because a small feature can touch descriptors, frame contracts, render graph records, budget state, pass execution, UI controls, and validation.
- New Phase 3 features will make this worse unless each one comes with state/resource ownership rules.

Plan:

1. Do not start with a giant renderer rewrite.
2. For every Phase 3 feature, identify its owner before implementation:
   - state owner
   - resource owner
   - frame contract writer
   - UI control applier
   - validation gate
3. Extract only touched areas into repo-native state/resource bundles under `src/Graphics`.
4. Prioritize extraction candidates:
   - environment/IBL state
   - RT reflection tuning/stats state
   - post-process/cinematic state
   - particle presentation/effect state
   - graphics preset/tuning state
5. After each extraction, run release validation or the smallest relevant smoke.

Milestone gate:

- no Phase 3 feature should add new unrelated top-level `Renderer` members without a reason recorded in the pass notes.

## Graphics Settings UI Is Not Yet A Unified Control Surface

What is wrong:

- Controls are spread across debug, quality, lighting, performance, and editor windows.
- Some controls call `Renderer` directly while others go through `RendererControlApplier`.
- There is not yet one stable graphics settings model that can be saved, loaded, reset, and used by validation.
- Sliders without preset/serialization support will make visual tuning hard to reproduce.

Plan:

1. Extend `RendererControlApplier` first so all UI controls share clamping and renderer-call behavior.
2. Add `RendererTuningState` under `src/Graphics` as the serializable settings model.
3. Build the unified Graphics Settings window as a consumer of that state, not as a pile of direct renderer calls.
4. Keep existing windows during migration, but make the unified window the public control surface.
5. Add preset round-trip tests before expanding the slider count.

Milestone gate:

- a release showcase run should be reproducible from a named graphics preset without manually touching UI.

## Environment/IBL Policy And Manifest Are Not Implemented

What is wrong:

- Environment loading exists, but the asset policy is implicit.
- Runtime assets, source HDR/EXR assets, committed assets, optional assets, and generated assets are not clearly separated.
- IBL residency and budget behavior exists, but there is no manifest-level contract for default/fallback choices.
- Adding more backgrounds without policy will create repo bloat, descriptor pressure, and validation instability.

Plan:

1. Lock the IBL asset policy before writing the manifest parser.
2. Choose the default committed runtime environment and procedural fallback.
3. Define per-budget max dimensions, file size expectations, and eager/deferred loading rules.
4. Build the manifest around the existing `RendererEnvironmentState`, `Renderer_Environment.cpp`, `AssetRegistry`, and `BudgetPlanner`.
5. Add validation for missing optional assets, missing selected assets, low-memory residency, and fallback sky.

Milestone gate:

- deleting any optional environment should not break startup or release validation.

## Showcase Scenes Still Need Polish

What is wrong:

- Current showcase validation proves the engine runs and produces signal, but public presentation still depends on scene composition.
- Camera placement, lighting rigs, background choice, material arrangement, and screenshot timing are not yet stable enough for golden baselines.
- A technically valid frame can still look unconvincing if the scene has poor silhouettes, bad reflections, over-dark backgrounds, or cluttered materials.

Plan:

1. Stabilize scene descriptors, camera bookmarks, lighting rigs, and IBL defaults before adding golden baselines.
2. Treat each public scene as a composed demo:
   - RT Gallery
   - Material Lab
   - IBL Gallery
   - Glass/Water Courtyard
   - Low-Memory Safe Mode
   - Temporal Stress
   - Effects Showcase
3. Give every public scene a `hero` camera bookmark and validation profile.
4. Use visual stats to catch black/overexposed/fogged frames, but use human review for composition until scenes stabilize.
5. Add golden/tolerance baselines only after camera and lighting churn slows down.

Milestone gate:

- every public scene should have a stable hero capture and a reason to exist.

## Advanced Shaders, Lighting, Particles, And Cinematic Post Are Planned, Not Complete

What is wrong:

- These are feature projects, not quick polish tasks.
- The repo already has partial material, lighting, particle, and post-process systems; new work must consolidate those instead of replacing them.
- Advanced visuals can hide correctness bugs if they are added before validation and controls exist.
- Particles and cinematic post can easily break budget and readability.

Plan:

1. Do not start advanced effects until the control/preset/validation base is usable.
2. Extend existing systems:
   - `MaterialPresetRegistry` and material shaders for advanced material work
   - `Renderer::ApplyLightingRig` and `RendererLightingRigControl` for lighting
   - ECS particles, `GPUParticles`, `Renderer_Particles`, and particle shaders for effects
   - existing bloom/post-process paths for cinematic post
3. Add effects as controlled presets with budget classes.
4. Use the Effects Showcase scene as the integration target.
5. Keep release defaults conservative so effects enhance the renderer instead of masking it.

Milestone gate:

- advanced effects are not considered complete until they have UI controls, frame-contract stats, budget behavior, and visual validation.

## More Pass-Owned Resource/State Extraction Is Needed

What is wrong:

- Phase 2 improved state grouping, but many pass resources still live too close to central renderer state.
- Descriptor pressure bugs become harder to reason about when pass ownership is unclear.
- Phase 3 systems will add more resources unless ownership is explicit.

Plan:

1. Extract pass-owned bundles as each touched feature is implemented.
2. Start with high-value bundles:
   - `RTReflectionStatsResources`
   - `EnvironmentRuntimeState`
   - `GraphicsPresetRuntimeState`
   - `CinematicPostState`
   - `ParticleEffectRuntimeState`
3. Each bundle should own creation, release, validity checks, and contract reporting helpers where practical.
4. Resource extraction must preserve descriptor budget contracts.
5. Do not extract purely for aesthetics; extract where it reduces real implementation risk.

Milestone gate:

- every new persistent GPU resource should have an owner, budget category, and frame-contract/resource-contract visibility.

---

# Immediate Implementation Order

Start with infrastructure before visual tuning.

1. Treat `phase3.md` as the public blueprint and keep it repo-native as implementation discovers details.
2. Extend `RendererControlApplier` with any missing clamped controls before adding new UI sliders.
3. Add `RendererTuningState` under `src/Graphics` and route apply/capture through existing control appliers.
4. Add graphics preset JSON schema and round-trip tests.
5. Add startup preflight contract for assets/config/environment.
6. Lock the IBL asset policy: default committed asset, runtime format, size limits, optional download/generation rules, and fallback behavior.
7. Add environment manifest parser around existing `Renderer_Environment` and procedural sky fallback.
8. Add frame contract environment/health/preset sections by extending the existing frame contract path.
9. Add Graphics Settings window tabs over existing UI windows and renderer APIs.
10. Add Phase 3 visual matrix script with current RT showcase and temporal scenes.
11. Stabilize public scene cameras, lighting rigs, and IBL defaults before adding golden baselines.
12. Add IBL gallery scene and screenshot stats gates.
13. Add RT reflection tuning controls and outlier handling.
14. Extend `MaterialPresetRegistry` and material validation for the material lab.
15. Extend advanced material features and material lab shader rows only after basic material lab validation is stable.
16. Extend existing lighting rigs and scene rig selection.
17. Consolidate existing particle paths, then add effect descriptors and budget controls.
18. Add effects showcase scene with particles, emissives, advanced materials, and cinematic post.
19. Polish public showcase scenes and camera bookmarks.
20. Add tolerant golden baselines only after scene/camera/lighting/IBL stability.
21. Extract pass-owned resource bundles touched by the above work.
22. Run full release, visual, and effects validation.
23. Update README/release readiness with exact commands and current logs.

---

# Definition of Done

Phase 3 is done when Cortex satisfies all of these:

- launches cleanly from documented instructions
- has a clear graphics settings UI with working sliders and presets
- has a robust environment/IBL library with fallback behavior
- has polished public showcase scenes with camera bookmarks
- has material presets and validation strong enough for public scenes
- has advanced material shaders for clearcoat, anisotropy, wetness, sheen/subsurface-style approximations, and emissive bloom
- has cinematic lighting rigs that can be selected from scenes, UI, and validation
- has a controlled particle system with reusable effects and budget-aware quality scaling
- has cinematic post controls that improve scenes without hiding renderer correctness
- has RT reflection tuning that is measurable and stable
- explains RT scheduling and fallback behavior in UI and contracts
- persists settings safely
- passes release validation and Phase 3 visual matrix from a clean build
- has README/release notes that match the actual scripts and launch flow
- no known descriptor, memory, startup, or screenshot-regression issue is hidden by the test suite

At the end of Phase 3, Cortex should feel less like a research renderer that can produce good frames and more like a graphics engine with a dependable public demo surface.
