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

- Status: `NOT_STARTED`
- Requirement: define a structured scene seed schema for hand-authored showcase scenes.
- Source files/functions:
  - New: `CortexEngine/assets/scenes/hand_authored/schema/scene_seed.schema.json`
  - New: `CortexEngine/tools/run_scene_seed_contract_tests.ps1`
  - Existing: `CortexEngine/assets/config/showcase_scenes.json`
  - Future C++ loader target: `Engine::BuildSceneFromPreset`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- Evidence: none yet.
- Remaining work:
  - Add schema fields for scene intent, asset groups, transforms, contacts, lighting, materials, cameras, and validation tolerances.
  - Require explicit `art_reason` on each authored group and camera.
  - Reject seed files that rely on unnamed random scatter for primary scene construction.

### ALS-002: Asset Kit Manifest and Orientation Policy

- Status: `PARTIAL`
- Requirement: expand the existing naturalistic asset manifest into a real asset-kit policy with scale, orientation, pivot, floor contact, bounds, material texture status, and license/source records.
- Source files/functions:
  - Existing: `CortexEngine/assets/models/naturalistic_showcase/asset_manifest.json`
  - Existing: `CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`
  - New: `CortexEngine/tools/run_asset_kit_policy_tests.ps1`
  - Existing loader: `LoadNaturalisticShowcaseMesh` in `CortexEngine/src/Core/Engine_Scenes.cpp`
  - Existing glTF loader: `Utils::LoadGLTFMesh`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_kit_policy_tests.ps1`
- Evidence: `run_naturalistic_asset_policy_tests.ps1` currently validates the original naturalistic asset policy, but does not validate pivot/orientation/contact readiness.
- Remaining work:
  - Add per-asset `up_axis`, `forward_axis`, `scale_to_meters`, `pivot_policy`, `floor_y`, `bounds`, `thumbnail_capture`, and `intended_scene_roles`.
  - Add policy rejecting model usage unless orientation and ground contact are declared.
  - Prevent another dragon-on-its-back class of failure by requiring per-scene orientation notes for non-trivial meshes.

### ALS-003: PBR Texture Binding for Imported Assets

- Status: `PARTIAL`
- Requirement: imported glTF assets should use their committed PBR textures where available instead of geometry-only placeholder material overrides.
- Source files/functions:
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `LoadNaturalisticShowcaseMesh`, asset upload call sites.
  - `CortexEngine/src/Graphics/Renderer_Materials.cpp`
  - `CortexEngine/src/Graphics/Renderer_TextureCreation.cpp`
  - `CortexEngine/src/Graphics/Renderer_TexturePublication.cpp`
  - `CortexEngine/src/Graphics/MaterialModel.cpp`
  - `CortexEngine/src/Graphics/MaterialPresetRegistry.cpp`
  - `CortexEngine/src/Graphics/RHI/DX12Raytracing_Materials.cpp`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_material_path_equivalence_tests.ps1`
- Evidence: existing material and RT parity tests cover material constants and surface classes, but current naturalistic asset manifest notes geometry-only loader behavior.
- Remaining work:
  - Bind base color, normal, roughness, metallic, AO, and emissive maps from asset kit metadata.
  - Require fallback textures if a channel is missing.
  - Prove raster and RT material paths see compatible imported material data.

### ALS-004: Scene Composition Stability Contracts

- Status: `NOT_STARTED`
- Requirement: automated tests must catch disconnected geometry, floating bars, oversized gaps, squashed props, and wrong orientation before screenshots are published.
- Source files/functions:
  - New: `CortexEngine/tools/run_scene_composition_stability_tests.ps1`
  - Existing: `CortexEngine/tools/run_scene_polish_contract_tests.ps1`
  - Existing: `CortexEngine/src/Core/Engine_Scenes.cpp`
  - Existing: `CortexEngine/assets/config/showcase_scenes.json`
  - Future: `CortexEngine/assets/scenes/hand_authored/*/scene_seed.json`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- Evidence: existing scene polish contract checks named tokens and some camera framing, but not general authored contact/gap rules.
- Remaining work:
  - Add declarative `contact_rules` to scene seeds.
  - Validate group adjacency: rails have posts, countertops have supports, panels attach to frames, water/lava surfaces sit inside containers.
  - Add scale sanity checks for props that should be round, upright, or grounded.

### ALS-005: Lighting Direction and Tinted World Shaders

- Status: `PARTIAL`
- Requirement: each public and new scene needs deliberate lighting and world shader direction, not generic white gallery light.
- Source files/functions:
  - `CortexEngine/src/Graphics/RendererControlApplier_ScenePresets.cpp`
  - `CortexEngine/src/Graphics/Renderer_Environment.cpp`
  - `CortexEngine/src/Graphics/Renderer_FrameContract.cpp`
  - `CortexEngine/assets/config/graphics_presets.json`
  - `CortexEngine/assets/config/showcase_scenes.json`
  - `CortexEngine/assets/shaders/ForwardPass.hlsl`
  - `CortexEngine/assets/shaders/MaterialResolve.hlsl`
  - `CortexEngine/assets/shaders/RaytracingMaterials.hlsli`
  - New: `CortexEngine/tools/run_world_shader_contract_tests.ps1`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_world_shader_contract_tests.ps1`
- Evidence: `run_lighting_energy_budget_tests.ps1` exists, and scene presets apply named lighting rigs, but there is no authored world-shader palette contract.
- Remaining work:
  - Add world material modes: wet ground, dry sand, basalt rock, moss/vegetation undergrowth, stained tile, oxidized metal, soot, grime, glass tint, lava crust.
  - Add per-scene color keys: warm/cool contrast, fog tint, IBL choice, exposure, bloom ceiling.
  - Add frame-contract reporting for active world shader palette and lighting script id.

### ALS-006: Coastal Cliff Foundry Scene

- Status: `NOT_STARTED`
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
- Evidence: none yet.
- Remaining work:
  - Author blockout with cliffs, foundry trough, attached railings/supports, ocean plane, wet rocks, sparks, smoke, and camera bookmarks.
  - Use actual asset-kit rocks/wood/metal props where appropriate.
  - Capture high-resolution public screenshots after harsh review.

### ALS-007: Rain Glass Pavilion Scene

- Status: `NOT_STARTED`
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
- Evidence: none yet.
- Remaining work:
  - Author pavilion frame, connected glass panes, wet ground, puddles, chrome fixtures, rain/mist particles, and reflection-view cameras.
  - Add material controls for glass tint/refraction readability without hiding background objects.

### ALS-008: Desert Relic Gallery Scene

- Status: `NOT_STARTED`
- Requirement: hand-author a warm exterior material scene with stone, sand, brushed metals, ceramic/glass accents, strong sun, long shadows, and visible world-shader variation.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/desert_relic_gallery/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/desert_relic_gallery/art_bible.md`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildDesertRelicGalleryScene`
  - `CortexEngine/src/Graphics/MaterialPresetRegistry.cpp`
  - `CortexEngine/assets/shaders/MaterialResolve.hlsl`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId desert_relic_gallery`
- Evidence: none yet.
- Remaining work:
  - Author ruins/plinths with real supports, clear contact, natural prop grouping, sand buildup, stone wear, and camera bookmarks.
  - Prove material palette is not a single-color theme.

### ALS-009: Neon Alley Material Market Scene

- Status: `NOT_STARTED`
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
- Evidence: none yet.
- Remaining work:
  - Author alley geometry with readable storefronts, sign mounts, cabling/supports, wet surfaces, smoke/steam/rain particles, and multiple detail cameras.

### ALS-010: Forest Creek Shrine Scene

- Status: `NOT_STARTED`
- Requirement: optionally hand-author a natural scene that proves vegetation, creek water, rock, moss, wood, soft fog, and filtered light can be cohesive without looking randomly scattered.
- Source files/functions:
  - New seed: `CortexEngine/assets/scenes/hand_authored/forest_creek_shrine/scene_seed.json`
  - New art bible: `CortexEngine/assets/scenes/hand_authored/forest_creek_shrine/art_bible.md`
  - `CortexEngine/src/Core/Engine_Scenes.cpp`: `Engine::BuildForestCreekShrineScene`
  - `CortexEngine/src/Graphics/Renderer_Vegetation.cpp`
  - `CortexEngine/src/Graphics/Renderer_WaterSurfaces.cpp`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId forest_creek_shrine`
- Evidence: none yet.
- Remaining work:
  - Confirm whether this scene is in scope for the next pass or should be `DEFERRED_BY_USER_ONLY`.
  - If included, author creek banks, rocks, foliage clusters, shrine focal object, and low-angle cameras.

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
  - `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media`
- Evidence: prior scene polish contracts and screenshots exist, but the latest critique shows the standard must become stricter and more holistic.
- Remaining work:
  - Review each scene from every bookmark.
  - Convert procedural-looking sets into cohesive authored spaces.
  - Replace weak placeholders, tighten cameras, remove gaps/floating elements, and update baselines/screenshots.

### ALS-012: Public Gallery Expansion

- Status: `NOT_STARTED`
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

- Status: `NOT_STARTED`
- Requirement: new asset-led tests must become part of release validation once the first asset-led scene lands.
- Source files/functions:
  - `CortexEngine/tools/run_release_validation.ps1`
  - `CortexEngine/assets/config/release_package_manifest.json`
  - New scripts: `run_asset_led_scene_contract_tests.ps1`, `run_asset_kit_policy_tests.ps1`, `run_scene_seed_contract_tests.ps1`, `run_scene_composition_stability_tests.ps1`, `run_world_shader_contract_tests.ps1`
- Validation command: `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1`
- Evidence: current release validation includes scene polish and naturalistic asset policy, but not asset-led seed/schema/composition/world-shader contracts.
- Remaining work:
  - Add new release validation steps when scripts are meaningful.
  - Do not add empty placeholder scripts just to satisfy the ledger.
  - Keep no-skip required gates for public release readiness.

### ALS-014: Manual Harsh Review Loop

- Status: `NOT_STARTED`
- Requirement: every captured public scene must be visually inspected after screenshots are generated, with fixes made before baselines are accepted.
- Source files/functions:
  - `CortexEngine/docs/media/*.png`
  - `CortexEngine/docs/media/gallery_manifest.json`
  - New review notes: `CortexEngine/tools/plans/asset_led_showcase_review_notes.md`
- Validation command: manual review plus `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_screenshot_negative_gates.ps1`
- Evidence: none for the new asset-led standard.
- Remaining work:
  - Open every new screenshot.
  - Record defects by scene and bookmark.
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
