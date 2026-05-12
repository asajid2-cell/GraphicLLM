# Cortex Engine Release Readiness

This document records the current public-review posture for Project Cortex:
a real-time DirectX 12 hybrid renderer with validation evidence,
high-resolution screenshots, a short gallery reel, package checks, and a staged
launch smoke.

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

Latest full integrated gate:

```text
CortexEngine/build/bin/logs/runs/release_validation_20260512_153337_590_21416_37e45f02/release_validation_summary.json
```

Result:

- `status=passed`
- `step_count=62`
- `failure_count=0`
- public README contract, release package contract, and staged package launch
  smoke passed
- RT showcase, temporal validation, material lab, visual probes, IBL gallery,
  effects gallery, renderer ownership, budget matrix, and voxel backend smoke
  passed

Previous package artifact from the cleanup state:

```text
CortexEngine/release/cortex-public-review_6de5ffd_20260512_134536.zip
```

Package summary:

```text
CortexEngine/release/cortex-public-review_6de5ffd_20260512_134536_summary.json
```

Package size: `83,395,208` bytes, below the `536,870,912` byte manifest cap.

## Public Capture Evidence

High-resolution public captures and the public reel are generated with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_gallery_reel.ps1
```

Media manifests:

```text
CortexEngine/docs/media/gallery_manifest.json
CortexEngine/docs/media/video_manifest.json
```

Capture run:

```text
CortexEngine/build/bin/logs/runs/public_capture_gallery_20260512_151920_699_36360_0cb5c88e
```

The gallery uses the `public_high` graphics preset and records scene, camera,
environment, render scale, capture size, GPU frame time, luma metrics, and RT
reflection signal/history values for each public screenshot. The current public
gallery has 16 committed 1920x1080 screenshots. The MP4 reel is a derived
public media asset generated from those committed screenshots.

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
- Public docs, release readiness notes, screenshot gallery media, and the
  generated public reel.

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
- Larger authored content sets, deeper material-path parity matrices, and
  longer camera-motion videos are still future renderer/content work.
