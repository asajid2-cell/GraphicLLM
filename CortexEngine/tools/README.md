# Cortex Engine Tools

This folder contains repeatable local validation tools for the engine.

## Release Validation

Run the full local release gate from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The release gate builds Release once, then runs the core Phase 3 public
validation suite:

- temporal validation smoke,
- full RT showcase smoke,
- graphics settings persistence, UI contract, and HUD mode checks,
- graphics preset, showcase scene, material editor, and visual baseline contracts,
- Phase 3 visual matrix summary generation,
- renderer ownership and fatal error contract checks,
- advanced graphics catalog and effects gallery checks,
- environment manifest and IBL gallery checks,
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

## Phase 3 Validation

Run the Phase 3 foundation checks:

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_graphics_preset_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_environment_manifest_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_graphics_ui_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_hud_mode_contract_tests.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_material_editor_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_phase3_visual_matrix.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_effects_gallery_tests.ps1 -NoBuild
```

The UI contract test verifies the unified graphics settings window is compiled,
initialized, bound to F8/ESC, and backed by `RendererTuningState`. The visual
matrix wraps temporal validation, RT showcase, Material Lab, Glass and Water
Courtyard, Effects Showcase, and the IBL gallery, then writes JSON and Markdown
summaries under `build/bin/logs/runs`. Add new public scenes to this matrix only
after their camera, lighting, and IBL choices are stable enough to avoid churn.
The HUD mode contract verifies the Phase 3 clean-HUD modes, F7 cycling, and
`--hud` / `CORTEX_HUD_MODE` automation path; it runs short off/full-debug cases
and checks the generated frame report.
The material editor contract verifies the focused-entity material preset
dropdown, metallic/roughness sliders, validation status, and
`ModifyMaterialCommand` apply path.

The effects gallery test currently uses the RT showcase as the first public
effects scene and asserts that the particle contract is present, the particles
pass executed, particles were submitted, and the pass stayed within its cap.

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
