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
- temporal validation smoke,
- temporal camera-cut RT history invalidation smoke,
- full RT showcase smoke,
- render-graph transient alias/no-alias matrix,
- graphics settings persistence, unified graphics UI contracts, and runtime
  graphics settings application,
- HUD mode, graphics preset, material editor, and showcase scene contracts,
- Material Lab, Glass and Water Courtyard, Effects Showcase, visual baseline
  smokes, and screenshot negative gates,
- Phase 3 visual matrix,
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
- temporal validation: `gpu_ms=1.271`, `warnings=0`,
  `object_motion=0.0731`;
- temporal camera cut: `frames=53`, `cut_frame=20`,
  `camera=reflection_closeup`, `rt_reflection_reset=camera_cut`,
  `invalidated_frame=20`;
- RT showcase: `gpu_ms=1.621/16.7`, `material_issues=0`,
  `rt_refl_ready=True/ready`,
  `rt_signal=0.0225/0.1424/10.3398/0.0084`,
  `rt_hist=0.0314/0.1433/7.3008/0.0089`,
  `transient_delta=0`;
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
- voxel backend: `gpu_ms=19.685`, `avg_luma=116.9`, `nonblack=1`.

Aggregate logs:

```text
CortexEngine/build/bin/logs/runs/release_validation_20260510_193615_690_146580_bb588849
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
