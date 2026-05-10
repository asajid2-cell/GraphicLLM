# Cortex Engine Release Readiness

This note summarizes the current public-release posture of Cortex after the
Phase 2 renderer refactor.

## Status

The renderer is ready for a local public-review build when the release
validation gate passes on the target machine:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The current verified local gate covers:

- Release rebuild,
- temporal validation smoke,
- full RT showcase smoke,
- RT budget profile matrix,
- voxel backend smoke.

## Latest Verified Gate

Latest local release validation:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild
```

Result:

- temporal validation: `gpu_ms=2.044`, `warnings=0`,
  `object_motion=0.0731`;
- RT showcase: `gpu_ms=1.868/16.7`, `material_issues=0`,
  `rt_refl_ready=True/ready`,
  `rt_signal=0.0225/0.1424/10.3398/0.0084`,
  `rt_hist=0.0314/0.1433/7.3008/0.0089`,
  `transient_delta=0`;
- budget matrix: 8 GB, 4 GB, and 2 GB RT profiles passed;
- voxel backend: `gpu_ms=21.87`, `avg_luma=116.9`, `nonblack=1`.

Aggregate logs:

```text
CortexEngine/build/bin/logs/runs/release_validation_20260509_190246_577_70480_b012d0d6
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
- temporal validation and RT budget profiles.

## Known Limitations

- Validation numbers are hardware dependent. The latest local run used an
  NVIDIA GeForce RTX 3070 Ti.
- The Dreamer texture-generation service is optional. Smoke tests run with
  `--no-dreamer`; normal startup honors the configuration and can use the CPU
  procedural fallback when no TensorRT engines are present.
- The voxel backend is a smoke-tested experimental backend, not the primary
  renderer path.
- Material and showcase polish can continue, but the current validation gates
  protect the renderer from silent RT reflection, material parity, visual
  metric, budget, and descriptor regressions.
