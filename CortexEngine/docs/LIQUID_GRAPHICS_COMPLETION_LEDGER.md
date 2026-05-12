# Liquid Graphics Completion Ledger

Source goal: make liquid rendering convincing and releasable, starting with clearer blue water and extending the renderer to lava, honey, and molasses without bypassing the existing Cortex material, scene, frame-contract, and release-validation systems.

Status values:
- DONE_VERIFIED: implemented and proven by the listed validation command.
- DONE_UNVERIFIED: implemented in source but not yet proven.
- PARTIAL: some support exists, but the requirement is not satisfied.
- NOT_STARTED: no meaningful implementation exists.
- BLOCKED: cannot continue without a user or environment decision.
- DEFERRED_BY_USER_ONLY: explicitly deferred by the user.

## Completion Gate

The liquid overhaul is complete only when every ledger item below is DONE_VERIFIED or DEFERRED_BY_USER_ONLY, and the final validation set passes:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/rebuild.ps1 -Config Release`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_glass_water_courtyard_smoke.ps1 -NoBuild`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -Width 1920 -Height 1080`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild`
- `git diff --check`

Current gate status: DONE_VERIFIED.

Final evidence:
- Release rebuild passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/rebuild.ps1 -Config Release`
- Liquid contract passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`
- Liquid runtime smoke passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild -IsolatedLogs`
- Existing water scenes passed: `run_glass_water_courtyard_smoke.ps1 -NoBuild -IsolatedLogs`, `run_outdoor_sunset_beach_smoke.ps1 -NoBuild -IsolatedLogs`
- High-resolution gallery passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -Width 1920 -Height 1080`
- Full release validation passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild`

## Ledger Items

### LIQ-001: Existing Water Audit and Failure Mode Capture

Status: DONE_VERIFIED

Requirement: Identify why existing water reads as strange, non-blue, and poorly framed in the courtyard/beach scenes.

Source files/functions:
- `CortexEngine/assets/shaders/Water.hlsl`, `WaterPS`, `WaterVS`
- `CortexEngine/src/Graphics/Renderer_WaterSurfaces.cpp`, `Renderer::RenderWaterSurfaces`
- `CortexEngine/src/Core/Engine_Scenes.cpp`, `Engine::BuildGlassWaterCourtyardScene`, `Engine::BuildOutdoorSunsetBeachScene`

Validation command:
- Manual source audit.

Evidence:
- Existing water shader derived color from a dark neutral deep tint plus material albedo, had no liquid profile, used camera-distance depth instead of surface/shore cues, and treated every liquid as one global water material.
- Runtime screenshots from the new liquid gallery and beach/courtyard smokes show the corrected blue water and typed liquid coverage.

Remaining work:
- None.

### LIQ-002: Liquid Material Schema

Status: DONE_VERIFIED

Requirement: Extend water surfaces with explicit liquid profiles for water, lava, honey, and molasses while reusing the existing scene component and material preset systems.

Source files/functions:
- `CortexEngine/src/Scene/Components.h`, `Scene::WaterSurfaceComponent`
- `CortexEngine/src/Graphics/MaterialPresetRegistry.cpp`, `MaterialPresetRegistry::CanonicalPresets`, `MaterialPresetRegistry::Resolve`
- `CortexEngine/src/Graphics/SurfaceClassification.h`, `ClassifySurface`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`

Evidence:
- `WaterSurfaceComponent` now carries `LiquidType`, absorption, foam, viscosity, emissive heat, shallow tint, and deep tint.
- `MaterialPresetRegistry` exposes `lava`, `honey`, and `molasses`; `SurfaceClassification` treats liquid presets as water-class reflective liquids.

Remaining work:
- None.

### LIQ-003: Renderer Material Constants for Liquids

Status: DONE_VERIFIED

Requirement: Pass per-surface liquid profile, absorption, foam, viscosity, emissive heat, and tint values into the existing water overlay shader without increasing persistent descriptor pressure.

Source files/functions:
- `CortexEngine/src/Graphics/Renderer_WaterSurfaces.cpp`, `Renderer::RenderWaterSurfaces`
- `CortexEngine/src/Graphics/ShaderTypes.h`, `MaterialConstants`
- `CortexEngine/assets/shaders/Water.hlsl`, `MaterialConstants`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`

Evidence:
- `Renderer::RenderWaterSurfaces` packs liquid profile data into existing material constants without adding descriptors.
- Verified by `run_liquid_graphics_contract_tests.ps1` and `run_liquid_gallery_smoke.ps1`.

Remaining work:
- None.

### LIQ-004: Convincing Blue Water Shader

Status: DONE_VERIFIED

Requirement: Improve water readability with blue shallow/deep tinting, edge foam, crest foam, clearer Fresnel/specular response, and less white/scrunched visual ambiguity in pool/beach views.

Source files/functions:
- `CortexEngine/assets/shaders/Water.hlsl`, `WaterPS`, `WaterVS`
- `CortexEngine/src/Core/Engine_Scenes.cpp`, existing water scene builders

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_glass_water_courtyard_smoke.ps1 -NoBuild`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild`

Evidence:
- `Water.hlsl` now uses profile-aware shallow/deep blue tinting, edge foam, crest foam, Fresnel tint, and stronger liquid alpha for water readability.
- `run_glass_water_courtyard_smoke.ps1` and `run_outdoor_sunset_beach_smoke.ps1` passed after the shader changes.

Remaining work:
- None.

### LIQ-005: Lava, Honey, and Molasses Shader Profiles

Status: DONE_VERIFIED

Requirement: Add distinct liquid looks beyond water: emissive lava with crust/hot veins, translucent golden honey, and dark glossy molasses with high viscosity.

Source files/functions:
- `CortexEngine/assets/shaders/Water.hlsl`, `WaterPS`, `WaterVS`
- `CortexEngine/src/Graphics/MaterialPresetRegistry.cpp`, `MaterialPresetRegistry::Resolve`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild`

Evidence:
- `Water.hlsl` contains distinct lava, honey, and molasses branches.
- Liquid gallery smoke reports typed counts `water/lava/honey/molasses=1/1/1/1`, emissive liquid coverage, and `water_draws=4`.

Remaining work:
- None.

### LIQ-006: Liquid Gallery Scene and Camera Bookmarks

Status: DONE_VERIFIED

Requirement: Add a curated liquid gallery scene with four readable vats/basins and bookmarks for hero, water/lava, and viscous liquid closeups.

Source files/functions:
- `CortexEngine/src/Core/Engine.h`, `ScenePreset`
- `CortexEngine/src/Core/Engine.cpp`, startup scene parsing and report scene name
- `CortexEngine/src/Core/Engine_Camera.cpp`, `Engine::ApplyShowcaseCameraBookmark`
- `CortexEngine/src/Core/Engine_Scenes.cpp`, new `Engine::BuildLiquidGalleryScene`
- `CortexEngine/assets/config/showcase_scenes.json`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild`

Evidence:
- `liquid_gallery` scene, hero/water_lava/viscous_pair bookmarks, startup parsing, frame report scene name, and camera bookmark routing are implemented.
- `run_showcase_scene_contract_tests.ps1 -NoBuild` passed with `scenes=7`.

Remaining work:
- None.

### LIQ-007: Liquid Frame Contract Metrics

Status: DONE_VERIFIED

Requirement: Report liquid coverage in the frame contract: draw count, typed liquid counts, average absorption, average viscosity, foam strength, and emissive liquid count.

Source files/functions:
- `CortexEngine/src/Graphics/FrameContract.h`, `FrameContract::WaterInfo`
- `CortexEngine/src/Graphics/Renderer_FrameContractSnapshot.cpp`, water contract snapshot
- `CortexEngine/src/Graphics/FrameContractJson.cpp`, `ToJson`
- `CortexEngine/src/Graphics/Renderer_WaterSurfaces.cpp`, per-frame water diagnostics

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild`

Evidence:
- Frame contract now reports surface count, typed liquid counts, emissive liquid count, average absorption, average foam, average viscosity, and max emissive heat.
- Liquid gallery smoke validates these JSON fields at runtime.

Remaining work:
- None.

### LIQ-008: Beach and Courtyard Polish

Status: DONE_VERIFIED

Requirement: Make the existing beach/courtyard water views communicate water and shore/pool shape clearly, with less visual ambiguity and better waterline framing.

Source files/functions:
- `CortexEngine/src/Core/Engine_Scenes.cpp`, `Engine::BuildGlassWaterCourtyardScene`, `Engine::BuildOutdoorSunsetBeachScene`
- `CortexEngine/assets/config/showcase_scenes.json`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_glass_water_courtyard_smoke.ps1 -NoBuild`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild`

Evidence:
- Courtyard and beach water profiles were tuned with stronger blue tint/alpha and per-entity surface height support.
- Both existing smoke tests pass after the change.

Remaining work:
- None.

### LIQ-009: Liquid Validation Scripts

Status: DONE_VERIFIED

Requirement: Add static contract tests and runtime smoke tests that prove the new liquid profiles are implemented, reported, and rendered.

Source files/functions:
- `CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`
- `CortexEngine/tools/run_liquid_gallery_smoke.ps1`
- `CortexEngine/tools/run_release_validation.ps1`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild`

Evidence:
- Added `run_liquid_graphics_contract_tests.ps1` and `run_liquid_gallery_smoke.ps1`.
- Wired both into `run_release_validation.ps1`; full release validation passed.

Remaining work:
- None.

### LIQ-010: Public Gallery and README Integration

Status: DONE_VERIFIED

Requirement: Capture and publish liquid gallery examples, include them in public manifests, and describe the liquid capability without verbose marketing language.

Source files/functions:
- `CortexEngine/tools/run_public_capture_gallery.ps1`
- `CortexEngine/assets/config/release_package_manifest.json`
- `CortexEngine/docs/media/gallery_manifest.json`
- `CortexEngine/README.md`

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -Width 1920 -Height 1080`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_readme_contract_tests.ps1`

Evidence:
- Public gallery now includes `liquid_gallery_hero.png`, `liquid_gallery_water_lava.png`, and `liquid_gallery_viscous_pair.png`.
- README files and `release_package_manifest.json` reference the liquid captures; public README contract passed.

Remaining work:
- None.

### LIQ-011: Release Validation and Packaging

Status: DONE_VERIFIED

Requirement: The liquid overhaul must pass release validation and keep the public tree clean.

Source files/functions:
- `CortexEngine/tools/run_release_validation.ps1`
- `CortexEngine/assets/config/release_package_manifest.json`
- `CortexEngine/tools/run_public_tree_hygiene_tests.ps1` if present, otherwise release package checks

Validation command:
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild`
- `git diff --check`

Evidence:
- Full `run_release_validation.ps1 -NoBuild` passed, including package contract and launch smoke.
- Release package contract reported `files=87 bytes=99290853/536870912`.

Remaining work:
- None.
