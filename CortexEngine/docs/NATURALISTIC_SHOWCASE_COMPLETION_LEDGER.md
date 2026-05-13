# Naturalistic Showcase Completion Ledger

This ledger tracks the asset-driven naturalistic showcase phase. The goal is to move public scenes away from primitive/procedural-looking placeholders by using licensed scanned geometry, texture/source manifests, stricter package policy, and runtime validation.

## Status Definitions

- `DONE_VERIFIED`: implemented and proven by the listed validation command and evidence.
- `DONE_UNVERIFIED`: implemented but not yet proven by validation.
- `PARTIAL`: some implementation or metadata exists, but visual/runtime coverage is incomplete.
- `NOT_STARTED`: no meaningful implementation yet.
- `BLOCKED`: cannot continue without user decision or unavailable dependency.
- `DEFERRED_BY_USER_ONLY`: explicitly deferred by the user.

## Completion Gate

Status: `DONE_VERIFIED`

The phase is complete only when every item below is `DONE_VERIFIED` or `DEFERRED_BY_USER_ONLY`, public scenes use manifest-tracked licensed assets instead of only primitive geometry, high-resolution gallery captures are refreshed and inspected, package/release policy includes the required runtime assets, full release validation passes, and the checkpoint is committed and pushed.

Required final commands:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1
powershell -ExecutionPolicy Bypass -File CortexEngine/rebuild.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild -IsolatedLogs
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1 -NoBuild -RuntimeSmoke
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_visual_probe_validation.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080 -SmokeFrames 220
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild
git -c submodule.recurse=false diff --check --ignore-submodules=all
git status --short --branch --ignore-submodules=all
```

Evidence:

- `CortexEngine/rebuild.ps1 -Config Release` passed on 2026-05-12 after the dragon/beach source changes.
- `CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080 -SmokeFrames 220` passed, `captures=19`, logs `public_capture_gallery_20260512_233044_809_69436_1a4fca12`.
- `CortexEngine/tools/run_visual_probe_validation.ps1 -NoBuild` passed, `cases=7`, logs `visual_probe_validation_20260512_233246_915_59140_7ed16720`.
- `CortexEngine/tools/run_release_validation.ps1 -NoBuild` passed, logs `release_validation_20260512_233445_908_70788_3941a126`.

Remaining work: none for this ledger gate.

## NAT-001: Asset Policy And Source Restrictions

Status: `DONE_VERIFIED`

Requirement: Define a public asset policy before importing anything. Assets must be redistributable, have explicit license metadata, source URLs, expected file sizes/hashes where practical, runtime budget class, and scene usage. Normal startup must not download assets. Public release packaging must include required runtime files and exclude raw/high-risk source formats.

Files and functions:

- `CortexEngine/assets/models/naturalistic_showcase/asset_manifest.json`
- `CortexEngine/assets/config/release_package_manifest.json`
- `CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`
- `CortexEngine/tools/run_release_validation.ps1`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1
```

Evidence from last successful run: `run_naturalistic_asset_policy_tests.ps1` passed with `assets=8 bytes=19075769/52428800`; release validation also passed its `naturalistic_asset_policy` step.

Remaining work: none.

## NAT-002: Licensed Natural Asset Acquisition

Status: `DONE_VERIFIED`

Requirement: Acquire a small but useful set of naturalistic assets from a trusted public source, prioritizing CC0 scanned geometry. The initial set must include coastal rock/boulder geometry, driftwood/dead trunk geometry, and plant/branch/grass geometry. Downloaded assets must remain within package budget and avoid `.blend` or other forbidden source formats.

Files and functions:

- `CortexEngine/assets/models/naturalistic_showcase/**`
- `CortexEngine/tools/fetch_naturalistic_assets.ps1`
- `CortexEngine/assets/models/naturalistic_showcase/asset_manifest.json`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/fetch_naturalistic_assets.ps1
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1
```

Evidence from last successful run: `fetch_naturalistic_assets.ps1` downloaded the manifest asset set; `run_naturalistic_asset_policy_tests.ps1` passed with `assets=8 bytes=19075769/52428800`.

Remaining work: none.

## NAT-003: Engine Runtime Asset Loading

Status: `DONE_VERIFIED`

Requirement: Add a scoped scene-side helper for naturalistic asset meshes. The helper must load glTF geometry from the committed asset manifest paths, upload each mesh once, expose clean fallback behavior when an asset is missing, and avoid duplicating per-instance GPU uploads.

Files and functions:

- `CortexEngine/src/Core/Engine_Scenes.cpp`
  - naturalistic mesh helper inside scene construction TU
  - `Cortex::Engine::BuildOutdoorSunsetBeachScene`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/rebuild.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild -IsolatedLogs
```

Evidence from last successful run: Release rebuild passed; `run_outdoor_sunset_beach_smoke.ps1 -NoBuild -IsolatedLogs` passed with `gpu_ms=2.340/16.7 luma=207.03 water_draws=1`.

Remaining work: none.

## NAT-004: Beach Naturalism Upgrade

Status: `DONE_VERIFIED`

Requirement: Replace the beach's most procedural-looking rocks/driftwood/plant cues with scanned assets while preserving the existing validated water/lighting. The beach hero and waterline captures must show natural silhouettes, varied scale/rotation, and no obvious primitive-only foreground.

Files and functions:

- `CortexEngine/src/Core/Engine_Scenes.cpp`
  - `Cortex::Engine::BuildOutdoorSunsetBeachScene`
- `CortexEngine/assets/config/showcase_scenes.json`
- `CortexEngine/assets/config/visual_baselines.json`
- `CortexEngine/docs/media/outdoor_sunset_beach_hero.png`
- `CortexEngine/docs/media/outdoor_sunset_beach_waterline.png`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_outdoor_sunset_beach_smoke.ps1 -NoBuild -IsolatedLogs
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_visual_probe_validation.ps1 -NoBuild
```

Evidence from last successful run: `run_outdoor_sunset_beach_smoke.ps1 -NoBuild -IsolatedLogs` passed after the brightness fix; `run_visual_probe_validation.ps1 -NoBuild` passed with `outdoor_sunset_beach_hero_release gpu_ms=2.232 luma=207.04 edge=4123/0.0045`.

Remaining work: none.

## NAT-005: Wider Showcase Variety

Status: `DONE_VERIFIED`

Requirement: Use the naturalistic asset foundation to improve scene variety beyond the beach. At minimum, add manifest-tracked real props to one interior/material scene and one effects/gallery scene so the public gallery demonstrates the renderer on real asset silhouettes, not only primitives.

Files and functions:

- `CortexEngine/src/Core/Engine_Scenes.cpp`
  - `Cortex::Engine::BuildMaterialLabScene`
  - `Cortex::Engine::BuildEffectsShowcaseScene`
- `CortexEngine/assets/config/showcase_scenes.json`
- `CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_effects_gallery_tests.ps1 -NoBuild
```

Evidence from last successful run: `run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` passed; `run_effects_gallery_tests.ps1 -NoBuild` passed with `particles=954 emitters=8`; release validation passed both `material_lab` and `effects_gallery`.

Remaining work: none.

## NAT-006: Public Gallery Refresh And Visual Review

Status: `DONE_VERIFIED`

Requirement: Recapture the public gallery at high resolution, inspect the images manually, and iterate on anything visibly awkward: bad scale, clipping, floating assets, overbright backgrounds, flat empty frames, or assets that read as randomly dumped into the scene.

Files and functions:

- `CortexEngine/docs/media/*.png`
- `CortexEngine/docs/media/gallery_manifest.json`
- `CortexEngine/assets/config/visual_baselines.json`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080 -SmokeFrames 220
```

Evidence from last successful run: `run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080 -SmokeFrames 220` passed with `captures=19`; updated gallery images were manually inspected, including RT dragon hero/closeup and beach hero/waterline.

Remaining work: none.

## NAT-007: Release Validation, Commit, Push

Status: `DONE_VERIFIED`

Requirement: The final checkpoint must pass targeted asset/scenes tests, broad visual validation, full release validation, and repository hygiene, then be committed and pushed.

Files and functions:

- `CortexEngine/tools/run_release_validation.ps1`
- `CortexEngine/assets/config/release_package_manifest.json`
- `CortexEngine/docs/media/*.png`
- `CortexEngine/assets/models/naturalistic_showcase/**`

Validation command:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild
git -c submodule.recurse=false diff --check --ignore-submodules=all
git status --short --branch --ignore-submodules=all
```

Evidence from last successful run: `run_release_validation.ps1 -NoBuild` passed; release package contract reported `files=128 bytes=118404185/536870912`; release package launch smoke passed; budget profile matrix passed.

Remaining work: commit and push this checkpoint after final git hygiene.
