# Asset-Led Showcase Completion Ledger

Purpose: plan the next real jump for Cortex from validated procedural showcase scenes to asset-led, hand-authored scene art. This ledger is the implementation source of truth for the next pass. It is intentionally stored under `tools/plans/` because repository hygiene rejects public `docs/*LEDGER.md` planning files.

Status values:

- `DONE_VERIFIED`: implemented, covered by a named validation command, and evidence recorded.
- `DONE_UNVERIFIED`: implemented but not proven by validation.
- `PARTIAL`: some infrastructure exists, but runtime behavior, authored content, or validation is incomplete.
- `NOT_STARTED`: no meaningful implementation exists yet.
- `BLOCKED`: cannot proceed without a user/product/hardware/asset decision.
- `DEFERRED_BY_USER_ONLY`: explicitly deferred by the user, not by convenience.

Current base evidence:

- `DONE_VERIFIED`: current public showcase foundation passed release validation before this ledger was created.
- Evidence: `CortexEngine/tools/run_release_validation.ps1` passed in the previous cleanup/public-package pass; latest committed foundation includes scene polish, naturalistic assets, public gallery captures, release package contracts, RT showcase, liquid gallery, visual matrix, and budget matrix.
- Existing public scenes in `CortexEngine/assets/config/showcase_scenes.json`: `rt_showcase`, `material_lab`, `ibl_gallery`, `glass_water_courtyard`, `outdoor_sunset_beach`, `liquid_gallery`, `effects_showcase`.
- Existing naturalistic asset manifest: `CortexEngine/assets/models/naturalistic_showcase/asset_manifest.json`.

## Completion Gate

Current status: `NOT COMPLETE`. The current implementation has seed/schema/asset-kit/world-palette/composition contracts, runtime builders for the five asset-led scenes, focused asset-led capture filtering, runtime PBR texture binding for naturalistic assets, and a first scene-art tightening pass. The scenes are not public-release complete because visual baselines, high-quality public captures, gallery media, and harsh screenshot review still show unresolved art defects.

The asset-led showcase pass is complete only when every ledger item below is `DONE_VERIFIED` or `DEFERRED_BY_USER_ONLY`.

Required final validation:

1. `cmake --build CortexEngine/build --config Release`
2. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1`
3. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_kit_policy_tests.ps1`
4. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
5. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
6. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_world_shader_contract_tests.ps1`
7. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
8. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_polish_contract_tests.ps1`
9. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`
10. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_material_robustness_contract_tests.ps1`
11. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_graphics_contract_tests.ps1`
12. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_gpu_particle_contract_tests.ps1`
13. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_lighting_energy_budget_tests.ps1`
14. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_visual_baseline_contract_tests.ps1 -RunRuntime`
15. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media`
16. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_gallery_reel.ps1`
17. `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
18. `git diff --check --ignore-submodules=all`
19. `git status --ignore-submodules=all --short`

Final release condition: public screenshots and gallery reel must show intentional, cohesive scene art with no disconnected rails, floating panels, oversized gaps, wrong model orientation, obvious placeholder cylinders, squashed props, incoherent lighting, or purely procedural scatter standing in for final composition.

## Global Art Standard

The screenshot critique applies to every public scene, not only the liquid gallery:

- Geometry must read as constructed: rails attach to supports, ledges meet walls, props sit on floors or plinths, and scene elements do not hover or intersect accidentally.
- Scene layout must have foreground, midground, and background intent. Empty gaps are allowed only when they serve framing.
- Lighting must have direction: key, fill, rim, environment, exposure, bloom, and fog should fit a named art goal.
- Materials must be legible from the camera bookmarks. Water, lava, glass, chrome, wet ground, stone, sand, vegetation, emissive signs, and cloth-like rough surfaces need distinguishable shader response.
- Procedural variation can support detail, but final hero shots must be hand-authored. Random or loop-generated scatter is not acceptable as the primary composition.
- Camera bookmarks are part of the art. Every bookmark must be composed, not just aimed at available content.

## Hand-Authored Scene Seed Policy

All new asset-led scenes use explicit scene seeds, not final random placement.

Target files:

- `CortexEngine/assets/config/showcase_scenes.json`
- `CortexEngine/assets/scenes/hand_authored/<scene_id>/scene_seed.json`
- `CortexEngine/assets/scenes/hand_authored/<scene_id>/art_bible.md`
- `CortexEngine/src/Core/Engine.h`
- `CortexEngine/src/Core/Engine.cpp`
- `CortexEngine/src/Core/Engine_Camera.cpp`
- `CortexEngine/src/Core/Engine_Scenes.cpp`

Runtime entry points to extend:

- `Engine::BuildSceneFromPreset`
- `Engine::BuildMaterialLabScene`
- `Engine::BuildGlassWaterCourtyardScene`
- `Engine::BuildOutdoorSunsetBeachScene`
- `Engine::BuildLiquidGalleryScene`
- `Engine::BuildEffectsShowcaseScene`
- `Engine::BuildRTShowcaseScene`
- New builders: `Engine::BuildCoastalCliffFoundryScene`, `Engine::BuildRainGlassPavilionScene`, `Engine::BuildDesertRelicGalleryScene`, `Engine::BuildNeonAlleyMaterialMarketScene`, and optionally `Engine::BuildForestCreekShrineScene`.

Seed pseudocode:

```text
scene_seed:
  id: coastal_cliff_foundry
  version: 1
  intent:
    one_sentence: "Cold ocean cliff meets hot foundry runoff at dusk."
    forbidden_reads:
      - disconnected rails
      - lava trays floating above supports
      - random prop scatter
      - flat white gallery lighting
  units: meters
  coordinate_system: y_up_right_handed
  lighting:
    rig: coastal_foundry_dusk
    key: warm low sun, left rear
    fill: cool sky, low intensity
    rim: lava and furnace emissive
    fog: salt mist over ocean, warm smoke over lava
  environment:
    ibl: outdoor_sunset
    fallback: procedural_sky
  cameras:
    - id: hero
      role: foreground/midground/background overview
    - id: material_closeup
      role: wet rock, chrome, lava, water contrast
    - id: atmosphere
      role: particles and tinted lighting
  assets:
    - id: basalt_cliff_01
      transform: explicit position/rotation/scale
      contact: grounded
      material_override: wet_basalt
  authored_groups:
    - id: foundry_channel
      parts: rails, trough, supports, lava mesh, sparks
      contact_rules: no floating bars, no gaps wider than threshold
```

Validation rule: deterministic randomness may be used only for secondary detail after an explicit anchor group exists. Each seeded random group must declare `seed`, `count`, `bounds`, `min_spacing`, `anchor`, and `art_reason`.

## Ledger Items

### ALS-001: Asset-Led Scene Schema

- Status: `DONE_VERIFIED`
- Requirement: define a structured scene seed schema for hand-authored showcase scenes.
- Source files/functions:
  - New: `CortexEngine/assets/scenes/hand_authored/schema/scene_seed.schema.json`
  - New: `CortexEngine/tools/run_scene_seed_contract_tests.ps1`
  - Existing: `CortexEngine/assets/config/showcase_scenes.json`
  - Future C++ loader target: `Engine::BuildSceneFromPreset`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- Evidence: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1` passed with `seeds=5`.
- Remaining work: none for the schema/data contract. Runtime scene loading remains tracked by ALS-006 through ALS-013.

### ALS-002: Asset Kit Manifest and Orientation Policy

- Status: `DONE_VERIFIED`
- Requirement: expand the existing naturalistic asset manifest into a real asset-kit policy with scale, orientation, pivot, floor contact, bounds, material texture status, and license/source records.
- Source files/functions:
  - Existing: `CortexEngine/assets/models/naturalistic_showcase/asset_manifest.json`
  - Existing: `CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`
  - New: `CortexEngine/tools/run_asset_kit_policy_tests.ps1`
  - Existing loader: `LoadNaturalisticShowcaseMesh` in `CortexEngine/src/Core/Engine_Scenes.cpp`
  - Existing glTF loader: `Utils::LoadGLTFMesh`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_kit_policy_tests.ps1`
- Evidence:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_kit_policy_tests.ps1` passed with `assets=11`.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1` passed with `assets=11 bytes=27417004/52428800`.
- Remaining work: none for manifest/orientation policy. Runtime material binding is now tracked as verified by ALS-003.

### ALS-003: PBR Texture Binding for Imported Assets

- Status: `DONE_VERIFIED`
- Requirement: imported glTF assets should use their committed PBR textures where available instead of geometry-only placeholder material overrides.
- Source files/functions:
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `LoadNaturalisticShowcaseMesh`, `GetNaturalisticAssetTextureSet`, `ApplyNaturalisticAssetTextures`, `AddAssetLedNaturalisticRenderable`, `Engine::BuildMaterialLabScene`, `Engine::BuildOutdoorSunsetBeachScene`, `Engine::BuildEffectsShowcaseScene`, `Engine::BuildCoastalCliffFoundryScene`, `Engine::BuildRainGlassPavilionScene`, `Engine::BuildNeonAlleyMaterialMarketScene`, `Engine::BuildForestCreekShrineScene`.
  - `CortexEngine/assets/models/naturalistic_showcase/asset_manifest.json`
  - `CortexEngine/tools/run_asset_kit_policy_tests.ps1`
  - `CortexEngine/src/Graphics/Renderer_Materials.cpp`
  - `CortexEngine/src/Graphics/Renderer_TextureCreation.cpp`
  - `CortexEngine/src/Graphics/Renderer_TexturePublication.cpp`
  - `CortexEngine/src/Graphics/MaterialModel.cpp`
  - `CortexEngine/src/Graphics/MaterialPresetRegistry.cpp`
  - `CortexEngine/src/Graphics/RHI/DX12Raytracing_Materials.cpp`
- Validation commands:
  - `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_kit_policy_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_material_path_equivalence_tests.ps1 -NoBuild -SmokeFrames 80`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/asset_led_pbr_review -SmokeFrames 90`
- Evidence:
  - Release target rebuilt successfully after binding changes.
  - Asset kit policy passed with `assets=11`; it now rejects unbound material status, stale geometry-only notes, missing binding helpers, missing asset IDs, and missing runtime texture-path strings.
  - Naturalistic asset policy passed with `assets=11 bytes=27417004/52428800`.
  - Material path equivalence passed with `vb_luma=179.62 forward_luma=178.80 vb_rendered=True/False`.
  - Asset-led runtime smoke passed with `scenes=5`.
  - Focused high-quality asset-led capture passed with `captures=15 size=1920x1080 preset=public_high`.
- Remaining work: none for binding committed naturalistic asset textures. Public visual quality remains tracked by scene-specific ALS-006 through ALS-014 items.

### ALS-004: Scene Composition Stability Contracts

- Status: `DONE_VERIFIED`
- Requirement: automated tests must catch disconnected geometry, floating bars, oversized gaps, squashed props, and wrong orientation before screenshots are published.
- Source files/functions:
  - `CortexEngine/assets/scenes/hand_authored/runtime_layout_contracts.json`
  - `CortexEngine/tools/run_scene_composition_stability_tests.ps1`
  - `CortexEngine/tools/run_asset_led_scene_contract_tests.ps1`
  - `CortexEngine/tools/run_screenshot_negative_gates.ps1`
  - Existing: `CortexEngine/tools/run_scene_polish_contract_tests.ps1`
  - Existing: `CortexEngine/src/Core/Engine_Scenes.cpp`
  - Existing: `CortexEngine/assets/config/showcase_scenes.json`
  - Existing: `CortexEngine/assets/scenes/hand_authored/*/scene_seed.json`
- Validation commands:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_screenshot_negative_gates.ps1 -NoBuild -RuntimeSmoke`
- Evidence:
  - Scene composition stability passed with `seeds=5`.
  - Asset-led scene contracts passed with `scenes=5`; runtime smoke now enforces layout-contract minimum renderable counts in addition to scene identity.
  - Screenshot negative gates passed and ran a runtime visual baseline sample with `cases=7`.
  - Runtime layout contracts now require constructed support/mounted/liquid tag groups per scene and parse round-prop scale from `Engine_Scenes.cpp` to reject horizontally squashed round props.
- Remaining work: none for automated composition stability gates. Scene-specific art quality fixes remain tracked by ALS-006 through ALS-014.

### ALS-005: Lighting Direction and Tinted World Shaders

- Status: `DONE_VERIFIED`
- Requirement: each public and new scene needs deliberate lighting and world shader direction, not generic white gallery light.
- Source files/functions:
  - `CortexEngine/assets/config/asset_led_world_palettes.json`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildCoastalCliffFoundryScene`, `Engine::BuildRainGlassPavilionScene`, `Engine::BuildDesertRelicGalleryScene`, `Engine::BuildNeonAlleyMaterialMarketScene`, `Engine::BuildForestCreekShrineScene`.
  - `CortexEngine/src/Graphics/Renderer.h`: `Renderer::SetWorldShaderPaletteContract`
  - `CortexEngine/src/Graphics/Renderer_LightingSettings.cpp`: `Renderer::SetWorldShaderPaletteContract`
  - `CortexEngine/src/Graphics/Renderer_DiagnosticsTypes.h`: `RendererLightingState`
  - `CortexEngine/src/Graphics/FrameContract.h`: `FrameContract::LightingInfo`
  - `CortexEngine/src/Graphics/FrameContractJson.cpp`
  - `CortexEngine/src/Graphics/Renderer_FrameContractSnapshot.cpp`
  - `CortexEngine/src/Graphics/FrameContractValidation.cpp`
  - `CortexEngine/src/Graphics/RendererControlApplier_ScenePresets.cpp`
  - `CortexEngine/src/Graphics/Renderer_Environment.cpp`
  - `CortexEngine/assets/config/graphics_presets.json`
  - `CortexEngine/assets/config/showcase_scenes.json`
  - `CortexEngine/assets/shaders/ForwardPass.hlsl`
  - `CortexEngine/assets/shaders/MaterialResolve.hlsl`
  - `CortexEngine/assets/shaders/RaytracingMaterials.hlsli`
  - `CortexEngine/tools/run_world_shader_contract_tests.ps1`
  - `CortexEngine/tools/run_asset_led_scene_contract_tests.ps1`
- Validation commands:
  - `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_world_shader_contract_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Evidence:
  - Release target rebuilt successfully after frame-contract and scene-control changes.
  - `run_world_shader_contract_tests.ps1` passed with `palettes=5 modes=9`; it now requires `SetWorldShaderPaletteContract`, frame-contract JSON fields, per-palette runtime contract calls, and authored scene exposure values.
  - `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed with `scenes=5`; runtime smoke now compares each scene report's `frame_contract.lighting.world_shader_palette_id` and `lighting_script_id` against `asset_led_world_palettes.json`.
- Remaining work: none for palette/script reporting and scene runtime use. Further lighting art direction remains scene-specific polish under ALS-006 through ALS-014.

### ALS-006: Coastal Cliff Foundry Scene

- Status: `PARTIAL`
- Requirement: hand-author a new scene where ocean water, wet cliffs, lava/foundry glow, metal rails, sparks, smoke, and dusk lighting form one cohesive composition.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/coastal_cliff_foundry/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/coastal_cliff_foundry/art_bible.md`
  - `CortexEngine/src/Core/Engine.h`: add scene preset and builder declaration.
  - `CortexEngine/src/Core/Engine.cpp`: command-line scene alias.
  - `CortexEngine/src/Core/Engine_Camera.cpp`: bookmark loading/fallback.
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildCoastalCliffFoundryScene`
  - `CortexEngine/assets/config/showcase_scenes.json`: scene metadata/bookmarks.
  - `CortexEngine/assets/config/visual_baselines.json`: runtime baseline case.
- Validation commands:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId coastal_cliff_foundry`
  - `CortexEngine/build/bin/Release/CortexEngine.exe --scene coastal_cliff_foundry --smoke-frames 180 --visual-validation`
- Evidence:
  - `scene_seed.json` and `art_bible.md` created.
  - `Engine::BuildCoastalCliffFoundryScene` implemented and registered.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/asset_led_review2 -SmokeFrames 90` passed with `captures=15 size=1920x1080 preset=public_high`.
  - Visual baseline case `coastal_cliff_foundry_hero_release` added and validated by full 12-case runtime visual baseline and probe validation.
  - Harsh review recorded: rail contact improved, but the scene still reads as rails on a platform in front of an HDRI.
  - Mesh-variety pass added extra coastal boulder anchors and `asset_led_review3` capture passed, but harsh review still rejected the scene as public media.
  - Startup-reapply pass prevents public graphics/environment presets from overriding asset-led lighting/background controls; `asset_led_review8` capture passed, but harsh review still rejected the coastal hero because the HDRI dependency and flat wall/beam silhouettes remain visible.
  - Coastal backdrop/grounding iteration moved the scene to the committed `cool_overcast` environment, reduced upper cliff mass scale, lowered the barrel, rebuilt Release, and passed scene-seed, composition-stability, and asset-led runtime contracts.
  - Focused capture `coastal_review11` was manually reviewed: the city HDRI mismatch is fixed, but box-platform construction, rail/post repetition, and flat cliff blocks remain visible, so the scene is still rejected for public media.
  - Coastal rail/furnace iteration aligned public metadata and visual baseline to `cool_overcast`, darkened furnace metals, added diagonal braces and channel grate slats, reduced rail repetition, tightened the hero camera, rebuilt Release, and passed scene-seed, composition-stability, showcase-scene, and asset-led runtime contracts.
  - Focused captures `coastal_review12` and `coastal_review13` were manually reviewed: the top beam/rail defect is reduced, but flat rear walls, repeated rail pieces, a floating-looking upper boulder, and rectangular industrial silhouettes still reject the scene for public media.
  - Coastal crown-grounding iteration lowered/scaled upper cliff crown boulders, shortened diagonal braces, reduced the right industrial silhouette, reduced the foreground rock mass, and adjusted the hero camera to avoid swallowing the scene with foreground rocks.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed after the coastal crown-grounding iteration.
  - Focused capture `CortexEngine/build/bin/logs/coastal_review15_rebalanced_rocks/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=0.850`, `gpu_frame_ms=3.511`, `avg_luma=93.709`, `rt_reflection_signal_avg_luma=0.08756`, `rt_reflection_history_signal_avg_luma=0.08772`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.
  - Manual review rejected `coastal_review15_rebalanced_rocks` for public media: the floating-crown and foreground-rock failures are reduced, but oversized rail/platform construction, flat wall silhouettes, and sky/HDRI dependence remain.
  - Coastal short-rail/lowered-cliff iteration tightened the hero bookmark, shortened lava/channel/rail/furnace crossbeam proportions, and lowered/scaled the upper cliff/notch tokens so they sit closer to the rear rock masses instead of hovering against the sky.
  - Release build passed with `cmake --build CortexEngine\build --config Release --target CortexEngine`; `run_scene_seed_contract_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1 -SceneId coastal_cliff_foundry`, and `run_asset_led_scene_contract_tests.ps1 -SceneId coastal_cliff_foundry -RuntimeSmoke -SmokeFrames 45` passed after the short-rail/lowered-cliff iteration.
  - High asset-led capture passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/coastal_lowered_cliff_review -Width 1920 -Height 1080 -SmokeFrames 90` reported `captures=15 size=1920x1080 preset=public_high`.
  - Manual review of `coastal_lowered_cliff_review` rejected final acceptance: the atmosphere frame no longer shows the worst floating cliff slabs, but the hero still exposes oversized rail/channel construction and a box-built platform/backdrop.
- Remaining work:
  - Add high-quality public captures after final art acceptance.
  - Replace the remaining flat cliff/industrial box backdrops and oversized rail/platform construction with better authored cliff/structure geometry or a redesigned close-up composition.
  - Fix composition/shader defects found in screenshots.
  - Capture high-resolution public screenshots after harsh review.

### ALS-007: Rain Glass Pavilion Scene

- Status: `PARTIAL`
- Requirement: hand-author a night/rain scene centered on glass, chrome, wet pavement, puddles, reflections, refraction, and cool/warm tinted lighting.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/rain_glass_pavilion/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/rain_glass_pavilion/art_bible.md`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildRainGlassPavilionScene`
  - `CortexEngine/src/Graphics/Renderer_TransparentGeometry.cpp`
  - `CortexEngine/src/Graphics/Renderer_WaterSurfaces.cpp`
  - `CortexEngine/assets/shaders/ForwardPass.hlsl`
  - `CortexEngine/assets/shaders/RaytracedReflections.hlsl`
- Validation commands:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId rain_glass_pavilion`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_rt_showcase_smoke.ps1`
- Evidence:
  - `scene_seed.json` and `art_bible.md` created.
  - `Engine::BuildRainGlassPavilionScene` implemented and registered.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed.
  - Focused asset-led capture passed with `rain_glass_pavilion` using the original `night_city` environment.
  - Visual baseline case `rain_glass_pavilion_hero_release` added and validated by full 12-case runtime visual baseline and probe validation.
  - Harsh review recorded: glass/reflection read is stronger, but flat translucent walls, over-bright strip lighting, and weak exterior grounding remain.
  - Rain pavilion IBL/garden iteration moved the scene to `cool_overcast`, added thinner framed glass structure, rear mullions, corner posts, rear planters, scanned `wild_rooibos_bush` and `fern_02` placements, lower garden enclosure, and garden-screen slats.
  - Release build, scene seed, composition stability, showcase-scene, asset-led, and asset-led runtime smoke contracts passed after the rain iteration.
  - Focused capture `rain_review11` was manually reviewed: the city-HDRI billboard failure is fixed and the glass pavilion reads more intentional, but the rear enclosure/slats are still procedural and foliage remains too dark/sparse, so the scene is still rejected for public media.
  - Rain interior focal-point iteration added scanned `WoodenTable_01`, tightened the hero camera, rebuilt Release, and passed seed, composition, showcase-scene, and asset-led runtime contracts.
  - Focused capture `rain_review12` was manually reviewed: the table/lantern interior focal point and tighter camera are stronger, but rear screens/garden panels still read as procedural dark slabs/slats, so the scene remains rejected for public media.
  - Rain segmented-screen iteration replaced the single large rear wood slab with two smaller low panels, shorter asymmetric slats, a warmer table mat, a glass roof panel, larger scanned `WoodenTable_01` placement, and a lower 1920x1080 hero camera.
  - `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'` passed after the segmented-screen iteration.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed after the segmented-screen iteration.
  - Focused capture `CortexEngine/build/bin/logs/rain_review20_final_checkpoint/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=4.118`, `avg_luma=71.066`, `rt_reflection_signal_avg_luma=0.05278`, `rt_reflection_history_signal_avg_luma=0.05276`, and texture uploads `submitted=14 completed=14 failed=0 pending=0 uploaded=109.33MB`.
  - Manual review rejected `rain_review20_final_checkpoint` for public media: the giant slab regression is gone and the table/floor/reflection focal read is better, but the cloudy HDRI band, procedural rear posts/slats, and blockout side panel remain visible.
  - Rain downward-interior iteration changed the hero to a higher downward camera to prioritize wet floor, table, lantern, glass rails/channels, and reflection detail over the cloudy horizon.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed after the downward-interior iteration.
  - Focused capture `CortexEngine/build/bin/logs/rain_review21_downward_interior/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=4.043`, `avg_luma=41.498`, `rt_reflection_signal_avg_luma=0.07714`, `rt_reflection_history_signal_avg_luma=0.07711`, and texture uploads `submitted=14 completed=14 failed=0 pending=0 uploaded=109.33MB`.
  - Manual review rejected `rain_review21_downward_interior` for public media: the sky/HDRI band is substantially reduced, but the table now dominates the composition and rear/blockout elements remain visible.
  - Rain tabletop/studio-vignette iteration added contract-covered chrome/glass tabletop accents, a chrome puddle ring, tighter material-led public bookmarks, and moved the rain public/default environment to `studio` to remove the cloudy overcast reflection band.
  - Release build passed with `cmake --build CortexEngine\build --config Release --target CortexEngine`; `run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1 -SceneId rain_glass_pavilion`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -SceneId rain_glass_pavilion -RuntimeSmoke -SmokeFrames 45` passed after the studio-vignette iteration.
  - High asset-led capture passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/rain_vignette_studio_review -Width 1920 -Height 1080 -SmokeFrames 90` reported `captures=15 size=1920x1080 preset=public_high`.
  - Manual review of `rain_vignette_studio_review` rejected final acceptance: `rain_glass_pavilion_hero` and `rain_glass_pavilion_glass_closeup` now read as intentional tabletop/glass material vignettes with the cloudy reflection failure removed, but blockout rear panels/slats remain visible and `rain_glass_pavilion_puddle_chrome` is too extreme/abstract for publication.
- Remaining work:
  - Add high-quality public captures after final art acceptance.
  - Replace the remaining procedural rear enclosure/slat silhouette and table-heavy composition with stronger authored garden/architectural geometry, a better rain-specific environment, or a more decisive close-up composition.
  - Run visual review and fix glass/refraction/material defects found in screenshots.
  - Add material controls for glass tint/refraction readability without hiding background objects.

### ALS-008: Desert Relic Gallery Scene

- Status: `PARTIAL`
- Requirement: hand-author a warm exterior material scene with stone, sand, brushed metals, ceramic/glass accents, strong sun, long shadows, and visible world-shader variation.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/desert_relic_gallery/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/desert_relic_gallery/art_bible.md`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildDesertRelicGalleryScene`
  - `CortexEngine/src/Graphics/MaterialPresetRegistry.cpp`
  - `CortexEngine/assets/shaders/MaterialResolve.hlsl`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId desert_relic_gallery`
- Evidence:
  - `scene_seed.json` and `art_bible.md` created.
  - `Engine::BuildDesertRelicGalleryScene` implemented and registered.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed.
  - Focused asset-led capture passed.
  - Visual baseline case `desert_relic_gallery_hero_release` added and validated by full 12-case runtime visual baseline and probe validation.
  - Harsh review recorded: closer framing helps the relic read, but it is still a blocky tan plinth scene.
  - Mesh-variety pass added cylinder columns, cones, ceramic vessels, and bronze pedestal detail; `asset_led_review3` capture passed, but hero composition remains rejected.
  - Startup-reapply/high-ruin pass restored authored lighting under public-high capture and added required high ruin/lintel geometry; `asset_led_review8` capture passed, but harsh review still rejected the scene as a tan block construction with weak material/art direction.
  - Desert asset-breakup pass added scanned `boulder_01` clusters, `dry_branches_medium_01` debris, darker plinth/ground shadow strips, and moved the scene to `cool_overcast` to avoid the city/courtyard HDRI mismatch.
  - Release build, seed, composition, showcase-scene, and asset-led runtime contracts passed after the desert pass.
  - Focused captures `desert_review9` and `desert_review10` were manually reviewed: `desert_review10` fixes the city backdrop and adds better scale anchors, but large flat block walls/lintels and a rectangular plinth still dominate, so the scene remains rejected for public media.
  - Desert relic-focus iteration tightened the hero camera, reduced the high ruin/lintel massing, split the blue plinth accent into individual tile blocks, added front stone chips, added a warmer plinth step material, added `DesertRelic_BackHighLintelBrokenRight`, and added scanned `DesertRelic_PlinthStoneAnchor`.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed after the desert relic-focus iteration.
  - Focused capture `CortexEngine/build/bin/logs/desert_review13_lintel_grounding/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=8.181`, `avg_luma=130.359`, `rt_reflection_signal_avg_luma=0.01525`, `rt_reflection_history_signal_avg_luma=0.01523`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.
  - Manual review rejected `desert_review13_lintel_grounding` for public media: the relic/material focal read and tile breakup are stronger than `desert_review10`, but the scene still has primitive cap/lintel pieces, a large flat right wall, and blockout foreground slabs.
  - Desert blockout-reduction iteration widened/raised the hero camera, reduced the foreground sand lip, shortened the left/right ruin returns, shrank rear wall/lintel/cap masses, reduced sand ramps, and added front plinth chip overlays.
  - Release build, seed, composition, showcase-scene, and asset-led runtime contracts passed after the blockout-reduction iteration.
  - Focused capture `CortexEngine/build/bin/logs/desert_review16_recessed_blockout/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=3.644`, `avg_luma=137.017`, `rt_reflection_signal_avg_luma=0.01323`, `rt_reflection_history_signal_avg_luma=0.01321`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.
  - Manual review rejected `desert_review16_recessed_blockout` for public media: the giant right wall and foreground sand plank are reduced compared with `desert_review13`, but the scene still reads as primitive plinth/blockout architecture with weak authored ruin mesh quality.
  - Desert relic close-crop iteration tightened the hero camera around the bronze ring/glass/mosaic/chipped-stone focal area and reduced the main plinth, front step, rear step, and front chip proportions.
  - Release build passed with `cmake --build CortexEngine\build --config Release --target CortexEngine`; `run_scene_seed_contract_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1 -SceneId desert_relic_gallery`, and `run_asset_led_scene_contract_tests.ps1 -SceneId desert_relic_gallery -RuntimeSmoke -SmokeFrames 45` passed after the relic close-crop iteration.
  - High asset-led capture passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/desert_relic_close_crop_review -Width 1920 -Height 1080 -SmokeFrames 90` reported `captures=15 size=1920x1080 preset=public_high`.
  - Manual review of `desert_relic_close_crop_review` rejected final acceptance: the ring/material focal read is stronger and the plinth face is smaller, but giant primitive columns/blocks and reflective spheres still dominate the scene.
- Remaining work:
  - Add high-quality public captures after final art acceptance.
  - Add authored ruin meshes/stone breakup/sand piles and fix the remaining primitive cap/lintel, right-wall, foreground-slab, material, and framing defects found in screenshots.
  - Prove material palette is not a single-color theme.

### ALS-009: Neon Alley Material Market Scene

- Status: `PARTIAL`
- Requirement: hand-author a dense night scene with emissive signage, glass, chrome, wet asphalt, particles, bloom, and cinematic post as a cohesive street-market shot.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/neon_alley_material_market/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/neon_alley_material_market/art_bible.md`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildNeonAlleyMaterialMarketScene`
  - `CortexEngine/src/Graphics/Renderer_Particles.cpp`
  - `CortexEngine/src/Scene/ParticleEffectLibrary.cpp`
  - `CortexEngine/src/Graphics/RendererControlApplier_ScenePresets.cpp`
  - `CortexEngine/assets/shaders/ParticleRender.hlsl`
- Validation commands:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_effects_showcase_smoke.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_gpu_particle_contract_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId neon_alley_material_market`
- Evidence:
  - `scene_seed.json` and `art_bible.md` created.
  - `Engine::BuildNeonAlleyMaterialMarketScene` implemented and registered.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed.
  - Focused asset-led capture passed.
  - Visual baseline case `neon_alley_material_market_hero_release` added and validated by full 12-case runtime visual baseline and probe validation.
  - Harsh review recorded: this is currently the strongest new scene, but the market still needs denser storefront assets and signage that reads as designed graphics rather than blank glowing panels.
  - Neon signage/environment iteration added sign glyph masks, amber blade sign detail, display shelf/posts, awnings, rear service door, pipe stacks, overhead beams, and floor breakup patches; required layout tokens now cover the new authored detail pieces.
  - `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'` passed after the neon iteration.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed after the neon iteration.
  - Focused captures `neon_review8`, `neon_review9`, `neon_review10`, and `neon_review11` were manually reviewed. `neon_review11` reduced the city-HDRI billboard failure and proved the new authored tokens render, but the scene remains rejected for public media because it is too dark, primitive-wall heavy, and the rain/glass/sign composition still lacks authored storefront quality.
  - Neon lit-market iteration raised the authored exposure to `0.84`, tightened the world-palette contract to match it, widened the hero camera, reduced the glass display case, moved the rain effect behind the focal plane, added a rear magenta menu board/glyphs, added left-stall amber/cyan accent strips, and strengthened the runtime layout contract to require the new tokens.
  - `run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, `run_world_shader_contract_tests.ps1`, `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`, `run_gpu_particle_contract_tests.ps1`, and `run_effects_showcase_smoke.ps1` passed after the lit-market iteration.
  - Focused capture `CortexEngine/build/bin/logs/neon_review13_lit_market/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=14.053`, `avg_luma=35.128`, `rt_reflection_signal_avg_luma=0.02781`, `rt_reflection_history_signal_avg_luma=0.02840`, and texture uploads `submitted=7 completed=7 failed=0 pending=0 uploaded=34.67MB`.
  - Manual review rejected `neon_review13_lit_market` for public media: the left/rear signage and smaller case improve the shot, but the scene is still too dark, rain particles still distract from the display case, and the storefront architecture remains primitive.
  - Neon right-market detail iteration added contract-covered right-side inset/shelf/price-tab/display pieces and shifted the particles bookmark away from the near right wall.
  - Release build passed with `cmake --build CortexEngine\build --config Release --target CortexEngine`; `run_scene_seed_contract_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1 -SceneId neon_alley_material_market`, and `run_asset_led_scene_contract_tests.ps1 -SceneId neon_alley_material_market -RuntimeSmoke -SmokeFrames 45` passed after the right-market detail iteration.
  - High asset-led capture passed: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/neon_right_market_detail_review -Width 1920 -Height 1080 -SmokeFrames 90` reported `captures=15 size=1920x1080 preset=public_high`.
  - Manual review of `neon_right_market_detail_review` rejected final acceptance: the right-side cyan/amber shelf/display detail is visible and `neon_alley_material_market_particles` no longer frames a near-field black slab, but the scene still reads as a primitive box-built alley rather than release-grade authored storefront media.
- Remaining work:
  - Add high-quality public captures after final art acceptance.
  - Replace primitive storefront/display geometry with authored meshes or stronger modular facade pieces.
  - Rebalance magenta/cyan lighting, display-case glass, rain particles, and foreground props so the scene reads as a deliberate wet market rather than a dark box set.

### ALS-010: Forest Creek Shrine Scene

- Status: `PARTIAL`
- Requirement: optionally hand-author a natural scene that proves vegetation, creek water, rock, moss, wood, soft fog, and filtered light can be cohesive without looking randomly scattered.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/forest_creek_shrine/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/forest_creek_shrine/art_bible.md`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildForestCreekShrineScene`
  - `CortexEngine/src/Graphics/Renderer_Vegetation.cpp`
  - `CortexEngine/src/Graphics/Renderer_WaterSurfaces.cpp`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId forest_creek_shrine`
- Evidence:
  - `scene_seed.json` and `art_bible.md` created.
  - `Engine::BuildForestCreekShrineScene` implemented and registered.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed.
  - Focused asset-led capture passed.
  - Visual baseline case `forest_creek_shrine_hero_release` added and validated by full 12-case runtime visual baseline and probe validation.
  - Harsh review recorded: closer framing hides some edges, but the shrine remains box-built and vegetation/background are still flat walls.
  - Mesh-variety pass added shrine posts, cone roof, branch assets, and grass clusters; `asset_led_review3` capture passed, but hero composition remains rejected.
  - Startup-reapply/canopy pass removed the foreground mist streaks and split the canopy into smaller masses; `asset_led_review8` capture passed, but harsh review still rejected the scene as procedural primitives around a box shrine.
  - Forest creek detail iteration rebuilt Release successfully and passed `run_scene_composition_stability_tests.ps1` plus `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`.
  - Focused captures `forest_redesign_review2`, `forest_redesign_review3`, and `forest_redesign_review4` were manually reviewed and rejected: the scene still exposes floating-looking scanned boulders, hard creek-edge strips, sky/HDRI dependency, and flat background support geometry.
  - Forest asset-kit iteration added committed CC0 `tree_stump_01`, `rock_moss_set_01`, and `wild_rooibos_bush` assets, added runtime texture bindings, rebuilt Release, and passed asset-kit, naturalistic-asset, scene-seed, composition, world-shader, and asset-led runtime contracts.
  - Oversized `root_cluster_01` was rejected and removed because `run_naturalistic_asset_policy_tests.ps1` correctly failed it for exceeding `max_single_asset_bytes`.
  - Focused capture `forest_assetkit_review5` loaded the new forest textures with `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`, but manual review still rejected the scene for rectangular banks/platforms, weak shrine silhouette, sparse bush silhouettes, and procedural sky/background exposure.
  - Forest shrine-focus iteration enlarged the shrine base/capstone/posts/roof, strengthened the rear bush mass, and changed the hero camera to a higher downward creek composition.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_showcase_scene_contract_tests.ps1`, and `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed after the forest shrine-focus iteration.
  - Focused capture `CortexEngine/build/bin/logs/forest_review7_downward/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=10.195`, `avg_luma=92.704`, `rt_reflection_signal_avg_luma=0.08617`, `rt_reflection_history_signal_avg_luma=0.08613`, and texture uploads `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`.
  - Manual review rejected `forest_review7_downward` for public media: the shrine is larger and water/rock focus is better than `forest_assetkit_review5`, but the sky wall, rectangular platform banks, and block-built shrine are still public-release blockers.
  - Forest low-creek iteration lowered the hero camera, reduced broad bank/platform geometry, narrowed foam/creek strips, reduced the toy roof and tree-pole heights, added required shrine moss cladding stones, and tightened the runtime layout contract for those cladding tokens.
  - Release build, seed, composition, showcase-scene, and asset-led runtime contracts passed after the low-creek iteration.
  - Focused capture `CortexEngine/build/bin/logs/forest_review12_creek_material/visual_validation_rt_showcase.bmp` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=5.153`, `avg_luma=83.475`, `rt_reflection_signal_avg_luma=0.07028`, `rt_reflection_history_signal_avg_luma=0.07024`, and texture uploads `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`.
  - Manual review rejected `forest_review12_creek_material` for public media: the lower creek/material frame reduces some wide-platform exposure, but the scene still reads as a procedural diorama with sky-wall dependency, rectangular bank edges, and a block-built shrine.
- Remaining work:
  - Add high-quality public captures after final art acceptance.
  - Replace the shrine and creek banks with real authored terrain/shrine meshes or a fundamentally different macro-material scene composition.
  - Replace box shrine and flat vegetation walls with organic banks, a stronger non-blockout shrine silhouette, denser tree massing/background coverage, and better water readability.

### ALS-011: Existing Scene Re-Authoring Pass

- Status: `PARTIAL`
- Requirement: every existing public scene must be upgraded against the asset-led standard, not merely token-polished.
- Source files/functions:
  - `CortexEngine/src/Core/Engine_Scenes.cpp`
  - `CortexEngine/assets/config/showcase_scenes.json`
  - `CortexEngine/assets/config/visual_baselines.json`
  - `CortexEngine/docs/media/gallery_manifest.json`
  - `CortexEngine/docs/media/video_manifest.json`
- Validation commands:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_polish_contract_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -EstablishedOnly -OutputDir CortexEngine/build/bin/logs/established_scene_review -Width 1920 -Height 1080 -SmokeFrames 90`
- Evidence:
  - `run_public_capture_gallery.ps1` now has an `-EstablishedOnly` filter so existing public scenes can be validated without publishing unfinished asset-led WIP screenshots.
  - `run_scene_polish_contract_tests.ps1` now checks the established-scene filter and still passed.
  - Established-scene high capture passed with `captures=23 size=1920x1080 preset=public_high`, manifest `CortexEngine/build/bin/logs/established_scene_review/gallery_manifest.json`, logs `CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_174449_832_46388_ac1b4bcd`.
  - Manual spot review rejected marking ALS-011 done: `outdoor_sunset_beach_hero` still reads as a procedural beach diorama, `glass_water_courtyard_hero` remains boxy and over-bright, and `liquid_gallery_hero` still exposes disconnected tabletop/room-backdrop construction.
  - Public-scene composition checkpoint added stricter scene-polish contract tokens for authored beach cove/heads/dune clumps, courtyard wall niche/planters/canopy baffles, and liquid-gallery continuous vat deck/back/side returns.
  - Release rebuild passed after the public-scene composition/framing slice with `cmake --build CortexEngine/build --config Release --target CortexEngine` under `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`.
  - Focused checks passed:
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_polish_contract_tests.ps1`;
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_glass_water_courtyard_smoke.ps1 -NoBuild -SmokeFrames 90 -IsolatedLogs`;
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild -SmokeFrames 90 -IsolatedLogs`;
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild -SmokeFrames 90 -IsolatedLogs`.
  - Established-scene high capture passed after the framing slice with `captures=23 size=1920x1080 preset=public_high`, manifest `CortexEngine/build/bin/logs/established_scene_review_framed_public_polish/gallery_manifest.json`, logs `CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_180749_062_45068_faf014da`.
  - Manual review still rejects ALS-011 completion: `outdoor_sunset_beach_hero` has better framing but still reads as primitive beach props and flat horizon, `glass_water_courtyard_hero` is less wide but still boxy/over-bright, and `liquid_gallery_hero` is closer but still shows a constructed tabletop/gallery set rather than a fully authored space.
  - Asset set-dressing checkpoint added imported naturalistic ferns and lanterns to `glass_water_courtyard`, plus imported barrels and lanterns to `liquid_gallery`; `run_scene_polish_contract_tests.ps1` now requires those tokens.
  - Release rebuild passed after the asset set-dressing slice with `cmake --build CortexEngine/build --config Release --target CortexEngine` under `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`.
  - Focused checks passed:
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_polish_contract_tests.ps1`;
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_glass_water_courtyard_smoke.ps1 -NoBuild -SmokeFrames 90 -IsolatedLogs`;
    `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_liquid_gallery_smoke.ps1 -NoBuild -SmokeFrames 90 -IsolatedLogs`.
  - Established-scene high capture passed after the asset-prop slice with `captures=23 size=1920x1080 preset=public_high`, manifest `CortexEngine/build/bin/logs/established_scene_review_asset_props/gallery_manifest.json`, logs `CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_181430_052_33520_8afd433f`.
  - Manual review still rejects ALS-011 completion: imported props are visible and improve set dressing, but `glass_water_courtyard_hero` is still box-built and `liquid_gallery_hero/context` still read as a synthetic tabletop set with a mismatched room backdrop.
- Remaining work:
  - Review each scene from every bookmark.
  - Convert procedural-looking sets into cohesive authored spaces.
  - Replace weak placeholders, tighten cameras, remove gaps/floating elements, and update baselines/screenshots.

### ALS-012: Public Gallery Expansion

- Status: `PARTIAL`
- Requirement: update the public README/gallery after the new hand-authored scenes exist, with high-resolution screenshots and a gallery reel that shows rendering strengths.
- Source files/functions:
  - `CortexEngine/README.md`
  - Root `README.md`
  - `CortexEngine/docs/media/gallery_manifest.json`
  - `CortexEngine/docs/media/video_manifest.json`
  - `CortexEngine/assets/config/release_package_manifest.json`
  - `CortexEngine/tools/run_public_capture_gallery.ps1`
  - `CortexEngine/tools/run_public_gallery_reel.ps1`
  - `CortexEngine/tools/run_public_readme_contract_tests.ps1`
- Validation commands:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_readme_contract_tests.ps1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_package_contract_tests.ps1`
- Evidence: existing public gallery passes for current scenes only.
- Remaining work:
  - Capture each new scene from hero and detail bookmarks at high quality.
  - Update README without verbose marketing or planning-language clutter.
  - Add package manifest entries only after assets/screenshots are stable.

### ALS-013: Runtime Validation Integration

- Status: `PARTIAL`
- Requirement: new asset-led tests must become part of release validation once the first asset-led scene lands.
- Source files/functions:
  - `CortexEngine/tools/run_release_validation.ps1`
  - `CortexEngine/assets/config/release_package_manifest.json`
  - New scripts: `run_asset_led_scene_contract_tests.ps1`, `run_asset_kit_policy_tests.ps1`, `run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_world_shader_contract_tests.ps1`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
- Evidence:
  - `run_asset_led_scene_contract_tests.ps1`, `run_asset_kit_policy_tests.ps1`, `run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, and `run_world_shader_contract_tests.ps1` now exist and pass targeted validation.
  - `run_release_validation.ps1` now invokes `asset_kit_policy`, `scene_seed_contract`, `scene_composition_stability`, `world_shader_contract`, and `asset_led_scene_contract -RuntimeSmoke -SmokeFrames 30` before the public showcase scene gate.
  - `cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'` passed.
  - Asset-led startup reapply is implemented in `CortexEngine/src/Core/Engine.cpp` so public capture presets no longer silently override asset-led scene renderer controls; targeted asset-led runtime contracts passed after the change.
- Remaining work:
  - Run full release validation after public-capture polish gaps are resolved.
  - Keep no-skip required gates for public release readiness.

### ALS-014: Manual Harsh Review Loop

- Status: `PARTIAL`
- Requirement: every captured public scene must be visually inspected after screenshots are generated, with fixes made before baselines are accepted.
- Source files/functions:
  - `CortexEngine/docs/media/*.png`
  - `CortexEngine/docs/media/gallery_manifest.json`
  - New review notes: `CortexEngine/tools/plans/asset_led_showcase_review_notes.md`
- Validation command: manual review plus `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_screenshot_negative_gates.ps1`
- Evidence:
  - High-quality capture command passed with `captures=38 size=1920x1080 preset=public_high`.
  - Manual review recorded in `CortexEngine/tools/plans/asset_led_showcase_review_notes.md`.
  - Focused asset-led capture command passed with `captures=15 size=1920x1080 preset=public_high`.
  - Manual review recorded the second-pass results and explicitly rejected the WIP screenshots for public media.
  - Mesh-variety focused capture command passed with `captures=15 size=1920x1080 preset=public_high`.
  - Manual review recorded that the third-pass screenshots remain WIP and should not be published.
  - Startup-reapply/backdrop focused capture command passed with `captures=15 size=1920x1080 preset=public_high`.
  - Manual review recorded that `asset_led_review8` screenshots remain WIP and should not be published.
  - Manual review recorded the focused forest captures `forest_redesign_review2`, `forest_redesign_review3`, and `forest_redesign_review4`; all remain WIP and should not be published.
  - Manual review recorded focused forest captures `forest_assetkit_review3`, `forest_assetkit_review4`, and `forest_assetkit_review5`; the asset scale/camera pass improved the giant rock/log failure but remains WIP and should not be published.
  - Manual review recorded focused coastal captures `coastal_review9`, `coastal_review10`, and `coastal_review11`; the environment mismatch improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused rain pavilion captures `rain_review9`, `rain_review10`, and `rain_review11`; the city-HDRI mismatch improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused rain pavilion capture `rain_review12`; the interior focal point improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused rain pavilion captures `rain_review13` through `rain_review20_final_checkpoint`; the final checkpoint removes the giant slab regression and keeps contracts green, but the scene remains WIP and should not be published.
  - Manual review recorded focused neon captures `neon_review8`, `neon_review9`, `neon_review10`, and `neon_review11`; the city-HDRI billboard failure is reduced and the signage/detail tokens render, but the scene remains WIP and should not be published.
  - Manual review recorded focused neon captures `neon_review12_current_prepass` and `neon_review13_lit_market`; the added left/rear signage, smaller case, and brighter exposure improve the composition, but the scene remains WIP and should not be published.
  - Manual review recorded focused coastal captures `coastal_review12` and `coastal_review13`; the rail/beam defect is reduced, but the scene remains WIP and should not be published.
  - Manual review recorded focused desert captures `desert_review9` and `desert_review10`; the environment mismatch improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused desert captures `desert_review11`, `desert_review12`, and `desert_review13_lintel_grounding`; the relic focal read and tile breakup improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused desert captures `desert_review14_wall_breakup`, `desert_review15_plinth_breakup`, and `desert_review16_recessed_blockout`; oversized right wall, sand plank, and lintel failures were reduced, but the scene remains WIP and should not be published.
  - Manual review recorded focused forest captures `forest_review6` and `forest_review7_downward`; shrine scale and creek focus improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused forest captures `forest_review8_current_prepass`, `forest_review9_low_creek`, `forest_review10_low_material`, `forest_review11_shrine_attachment`, and `forest_review12_creek_material`; the lower creek/material attempt reduced some wide-platform exposure but confirmed that real terrain/shrine assets are needed before publication.
  - Manual review recorded focused coastal captures `coastal_review14` and `coastal_review15_rebalanced_rocks`; the floating crown and foreground-rock failures improved, but the scene remains WIP and should not be published.
  - Manual review recorded focused rain capture `rain_review21_downward_interior`; the sky/HDRI band improved, but the scene remains WIP and should not be published.
  - Manual review recorded asset-led public capture `rain_vignette_studio_review`; the studio environment, tabletop accents, and chrome puddle ring improve the rain material read, but rear blockout panels/slats and an overly extreme puddle close-up keep the scene WIP and unsuitable for publication.
  - Manual review recorded asset-led public capture `neon_right_market_detail_review`; the right-side shelf/detail and particle-camera adjustment improve the Neon shots, but the alley remains box-built and unsuitable for publication.
  - Manual review recorded asset-led public capture `coastal_lowered_cliff_review`; lowered cliff tokens reduce the floating-slab defect, but oversized rail/channel construction keeps Coastal WIP and unsuitable for publication.
  - Manual review recorded asset-led public capture `desert_relic_close_crop_review`; the tighter relic crop improves material focus, but primitive columns/blocks and reflective spheres keep Desert WIP and unsuitable for publication.
  - Manual review spot-checked established-scene captures from `established_scene_review`; old public scenes pass legacy contracts but still show procedural/boxy art issues, so ALS-011 remains WIP and docs/media was not refreshed.
  - Manual review spot-checked `established_scene_review_framed_public_polish` after a public-scene composition/framing slice. The affected hero shots improved enough to be useful checkpoints, but all three remain rejected for release media: beach still looks primitive, courtyard still looks box-built, and liquid gallery still reads as a tabletop set.
  - Manual review spot-checked `established_scene_review_asset_props` after the imported-prop slice. The new props are visible and validate the asset hookup, but they do not yet make the scenes release-grade; larger scene re-authoring and backdrop/material corrections are still required.
- Remaining work:
  - Fix recorded defects before committing asset-led screenshots to `docs/media`.
  - Fix the scene, shader, material, lighting, or camera before marking the item verified.

## Implementation Order

1. Add seed schema and asset-kit policy tests before authoring new scenes.
2. Expand asset manifest policy for orientation, scale, floor contact, material texture status, and scene roles.
3. Add the first hand-authored scene seed and runtime builder for `coastal_cliff_foundry`.
4. Add composition stability tests and wire them to the first scene.
5. Add world-shader/lighting palette reporting and tests.
6. Build `rain_glass_pavilion`, then `desert_relic_gallery`, then `neon_alley_material_market`.
7. Decide whether `forest_creek_shrine` is in this pass or explicitly deferred by the user.
8. Re-author existing public scenes against the same contact/composition/art-standard rules.
9. Capture high-quality screenshots and gallery reel.
10. Update public README and release package manifest.
11. Run final validation and mark the Completion Gate only when all items are verified or user-deferred.

## Implementation Blueprint

Scene loading blueprint:

```text
for each public scene request:
  normalize scene id
  if scene id has hand-authored seed:
    load scene_seed.json
    validate schema version and required art fields
    load asset kit records
    for authored group in seed:
      create parent group transform
      create named geometry parts with explicit transforms
      apply material palette entries
      record contact/gap validation metadata
    apply lighting script
    apply environment and post controls
    register camera bookmarks
  else:
    use existing builder
```

Asset placement blueprint:

```text
for each asset placement:
  read asset kit record
  transform = seed transform * asset orientation correction * scale_to_meters
  bounds = transformed asset bounds
  if placement.contact == grounded:
    assert abs(bounds.min.y - expected_surface_y) <= tolerance
  if placement.contact == mounted:
    assert placement.anchor exists
    assert distance_to_anchor <= tolerance
  if placement.role == round_prop:
    assert scale axes ratio <= max_nonuniform_ratio
```

Lighting blueprint:

```text
lighting_script:
  apply environment selection
  apply directional light color/intensity/direction
  apply fill/rim/emissive controls
  apply fog/exposure/bloom/tone mapper
  publish frame contract:
    lighting_script_id
    world_shader_palette_id
    exposure
    bloom_ceiling
    warm_cool_contrast
```

Shader blueprint:

```text
material resolve:
  if world_shader_palette != none:
    sample authored masks or procedural support maps
    blend base material with wetness, grime, sand, moss, soot, lava crust, or puddle response
    keep RT material classification compatible with raster material output
```

Capture blueprint:

```text
for each scene bookmark:
  launch high-quality preset
  warm up temporal histories
  capture screenshot
  compute luma/saturation/nonblack/signature metrics
  run negative gates
  open screenshot for human review
  if defects found:
    fix scene/material/lighting/camera
    recapture
```
