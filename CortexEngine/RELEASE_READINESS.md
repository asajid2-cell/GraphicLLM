# Cortex Engine Release Readiness

This document records the current public-review posture for Project Cortex.
Cortex is presented as a real-time DirectX 12 hybrid renderer with validation
evidence, high-resolution screenshots, package checks, and a staged launch
smoke.

## Status

The renderer is release-ready for local public review only when the full gate
passes from the committed state:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The release package is manifest-driven:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_package_contract_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_package_launch_smoke.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_create_release_package.ps1 -NoBuild
```

## Latest Verified Gate

Latest full integrated gate from the renderer refactor checkpoint:

```text
CortexEngine/build/bin/logs/runs/release_validation_20260512_020250_531_184976_bf618209/release_validation_summary.json
```

Result:

- `status=passed`
- `step_count=64`
- `failure_count=0`
- release package contract and staged package launch smoke passed
- RT showcase, temporal validation, material lab, visual probes, IBL gallery,
  effects gallery, renderer ownership, budget matrix, and voxel backend smoke
  passed

This cleanup phase adds public-facing documentation, high-resolution screenshot
evidence, README/package contracts, and final package creation. After those
changes are committed, the full gate must be rerun and this section must be
updated to the new final run path.

## Public Capture Evidence

High-resolution public captures were generated with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080
```

Capture manifest:

```text
CortexEngine/docs/media/gallery_manifest.json
```

Capture run:

```text
CortexEngine/build/bin/logs/runs/public_capture_gallery_20260512_132733_943_27296_0d962fbc
```

The gallery uses the `public_high` graphics preset and records scene, camera,
environment, render scale, capture size, GPU frame time, luma metrics, and RT
reflection signal/history values for each public screenshot.

## Release Gate Coverage

The full gate covers:

- Release rebuild and build-entrypoint contract.
- Repository hygiene and source-list coverage.
- Render-graph boundary, declaration, transient, and ownership contracts.
- Visibility-buffer, debug primitive, editor frame, and depth-stability
  contracts.
- Temporal validation and temporal camera-cut history invalidation.
- RT showcase, RT reflection history quality, RT GI signal, RT firefly, and RT
  overbright clamp tests.
- Graphics settings persistence, native graphics UI, material controls, HUD,
  presets, and launcher smokes.
- Material editor, material robustness, Material Lab, material path
  equivalence, conductor-energy, lighting-energy, and reflection-probe gates.
- Showcase scene contracts, visual baseline contract, visual probe validation,
  screenshot negative gates, and Phase 3 visual matrix.
- Environment manifest, IBL asset policy, fallback matrix, and IBL gallery.
- Advanced graphics catalog, GPU particle contract, effects gallery, and
  particle-disabled zero-cost path.
- Public package manifest contract, staged package launch smoke, budget profile
  matrix, fatal error contract, and voxel backend smoke.

## Package Policy

The public-review payload is controlled by
`assets/config/release_package_manifest.json`.

Included:

- Runtime executable, required DLLs, shaders, config files, compressed runtime
  textures, and environment manifests.
- Public docs, release readiness notes, cleanup ledger, and screenshot gallery
  media.

Excluded:

- Local models, generated logs, build directories, cache directories, debug
  artifacts, PDB/OBJ/LIB files, source HDR/EXR environment files, and local
  Blender assets.

## Renderer Scope

Cortex is best presented as a real-time hybrid renderer, not as an
infinite-world engine. The validated path emphasizes:

- frame and resource contracts,
- explicit renderer state ownership,
- render graph diagnostics,
- material and surface classification,
- RT scheduling and signal-quality reporting,
- visual validation and public showcase scenes,
- graphics UI/preset persistence,
- environment/IBL manifest policy,
- package creation and staged launch validation.

## Known Limitations

- Validation numbers depend on the GPU, driver, Windows SDK, and display mode.
  The current local evidence was produced on an NVIDIA RTX 3070 Ti.
- The LLM and Dreamer texture-generation paths are optional. Most renderer
  validation runs with `--no-llm --no-dreamer`.
- The voxel backend is an experimental backend with a smoke test, not the
  primary renderer path.
- The separate materials/graphics robustness ledger still tracks deeper future
  work such as a full material-path parity matrix, advanced stress scenes, and
  motion-camera effects readability.
