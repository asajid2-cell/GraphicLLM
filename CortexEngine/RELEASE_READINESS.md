# Cortex Engine Release Readiness

This note summarizes the current public-release posture of Cortex after the
Phase 3 renderer overhaul.

## Status

The renderer is ready for a local public-review build when the release
validation gate passes on the target machine:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The current verified local gate covers:

- Release rebuild,
- build entrypoint contract for the scripted CMake rebuild path,
- repository hygiene checks for whitespace and generated artifacts,
- source-list contract checks for CMake renderer split coverage,
- render-graph boundary contract checks,
- debug primitive ownership contract checks,
- editor frame path contract checks,
- temporal validation smoke,
- temporal camera-cut RT history invalidation smoke,
- full RT showcase smoke,
- visibility-buffer debug view runtime checks,
- render-graph transient alias/no-alias matrix,
- graphics settings persistence, unified graphics UI contracts, and runtime
  graphics settings application,
- HUD mode, graphics preset, material editor, and showcase scene contracts,
- Material Lab, Glass and Water Courtyard, Effects Showcase, visual baseline
  smokes, and screenshot negative gates,
- visual probe validation across all public baseline cases,
- Phase 3 visual matrix,
- descriptor/memory stress for the historical persistent-descriptor ceiling,
- renderer ownership, full ownership audit, and fatal error contracts,
- advanced graphics catalog and effects gallery contracts,
- environment manifest, IBL gallery validation, and Phase 3 fallback matrix,
- particle-disabled zero-cost and RT firefly/outlier gates,
- RT budget profile matrix,
- voxel backend smoke.

## Latest Verified Gate

Latest local release validation:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

Result:

- build_release: passed;
- build entrypoint contract: passed; `rebuild.ps1` / `cmake --build` path
  verified, with no raw Ninja call in rebuild/release validation;
- repository hygiene: passed; `git diff --check` and generated-artifact guards
  verified;
- source-list contract: passed; explicit CMake sources and renderer split
  coverage verified;
- render-graph boundary contract: passed; validation module and VB graph
  boundaries verified;
- debug primitive contract: passed; debug-line state ownership and draw-contract
  counters verified;
- editor frame contract: passed; editor renderer hooks and explicit frame
  sequence verified;
- temporal validation: `gpu_ms=1.989`, `warnings=0`,
  `object_motion=0.0731`;
- temporal camera cut: `frames=53`, `cut_frame=20`,
  `camera=reflection_closeup`, `rt_reflection_reset=camera_cut`,
  `invalidated_frame=20`;
- RT showcase: `gpu_ms=2.261/16.7`, `material_issues=0`,
  `rt_refl_ready=True/ready`,
  `rt_signal=0.0225/0.1424/10.3398/0.0084`,
  `rt_hist=0.0314/0.1433/7.3008/0.0089`,
  `transient_delta=0`, `temporal_diff=mean=0.047/2.5 changed=0.001/0.08`;
- VB debug views: depth view 34 and material-albedo view 35 passed with
  nonblack ratio `0.851` for both captures;
- descriptor/memory stress: `persistent_descriptors=988/1024`,
  `staging=78/128`, `transient_delta=0`, `dxgi_mb=408.46/512`,
  `estimated_mb=190.52/256`;
- visual probe: 4/4 public baseline cases passed; minimum edge ratio
  `0.0066`, maximum pure dominant-color ratio `0.0066`;
- render-graph transient matrix: aliasing-on, aliasing-off, and bloom-transients-off rows passed;
- temporal camera-cut, render-graph transient, graphics UI contract/runtime interaction, HUD, preset, material editor,
  showcase, ownership, fatal error,
  environment manifest, advanced graphics catalog, and effects gallery contracts passed;
- renderer full ownership audit: 48/48 `Renderer` members are named
  state/service aggregates, with no loose GPU resource/descriptors in
  `Renderer.h`;
- screenshot negative gates, particle-disabled zero-cost, Phase 3 fallback
  matrix, and RT firefly/outlier gates passed;
- Phase 3 visual matrix passed across temporal validation, RT Showcase,
  Material Lab, Glass and Water Courtyard, Effects Showcase, and IBL Gallery;
- budget matrix: 4 GB and 2 GB RT compatibility profiles passed inside the
  release gate, with RT Showcase covering the balanced profile;
- voxel backend: `gpu_ms=17.925`, `avg_luma=116.9`, `nonblack=1`.

Aggregate logs:

```text
CortexEngine/build/bin/logs/runs/release_validation_20260510_203347_600_159192_fc3ab321
```

## Renderer Scope

Cortex is currently best presented as a real-time hybrid renderer, not an
infinite-world engine. The validated path emphasizes:

- explicit renderer service/state ownership,
- frame and resource contracts,
- render graph diagnostics,
- material and surface classification,
- RT scheduling and readiness contracts,
- raw and denoised RT reflection signal metrics,
- temporal validation and RT budget profiles,
- unified graphics settings and preset persistence,
- environment/IBL manifest policy with procedural fallback,
- public showcase scenes with stable camera bookmarks and lighting rigs,
- advanced material, particle, and cinematic-post release foundations.

## Known Limitations

- Validation numbers are hardware dependent. The latest local run used an
  NVIDIA GeForce RTX 3070 Ti.
- The Dreamer texture-generation service is optional. Smoke tests run with
  `--no-dreamer`; normal startup honors the configuration and can use the CPU
  procedural fallback when no TensorRT engines are present.
- The voxel backend is a smoke-tested experimental backend, not the primary
  renderer path.
- Future effects work should extend the validated Phase 3 foundations rather
  than creating parallel systems. The current validation gates protect the
  renderer from silent RT reflection, material parity, visual metric, budget,
  descriptor, environment, graphics UI, particle, and post-process regressions.
