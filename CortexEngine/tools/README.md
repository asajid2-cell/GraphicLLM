# Cortex Engine Tools

This folder contains repeatable local validation tools for the engine.

## Release Validation

Run the full local release gate from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The release gate builds Release once, then runs:

- temporal validation smoke,
- full RT showcase smoke,
- RT budget profile matrix,
- voxel backend smoke.

Use `-NoBuild` only when the Release executable is already current.
Each step writes isolated logs under `CortexEngine/build/bin/logs/runs`.

## RT Showcase Smoke

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1
```

The script builds the Release target, runs the RT showcase scene with visual
validation enabled, and checks the generated frame report for:

- zero health warnings,
- zero frame-contract warnings,
- zero transient descriptor delta,
- valid GPU culling telemetry,
- valid visual validation image statistics,
- available GPU frame timing,
- bounded smoke frame count,
- bounded GPU frame time, DXGI memory, estimated renderer memory, render-target
  memory, pass write bandwidth, and descriptor usage,
- no first-frame render-target reallocation in the RT showcase startup path.

Use `-NoBuild` when the Release executable is already current.

Default budgets are intentionally strict for the RT showcase scene:

- GPU frame time: 16.7 ms,
- DXGI current usage: 512 MB,
- estimated renderer memory: 256 MB,
- render-target memory: 160 MB,
- estimated pass write bandwidth: 128 MB/frame,
- persistent descriptors: 1024,
- staging descriptors: 128.

Override the budget parameters from the command line when testing a deliberately
heavier scene or a lower-quality compatibility profile.
Use `-AllowStartupRenderTargetReallocation` only when intentionally testing a
render-scale or resize path.
