# Materials and Graphics Robustness Completion Ledger

Audit date: 2026-05-12

This ledger is the source of truth for the next graphics maturity phase after
the Phase 2/3 renderer-refactor gate. It focuses on material correctness,
visual robustness, motion stability, shader invariants, controlled effects, and
release validation. It does not replace `REFACTOR_COMPLETION_LEDGER.md`; it
extends it with the next set of implementation checkpoints.

Status values:

- `DONE_VERIFIED`: implementation exists and the listed validation command has
  passed with inspected evidence.
- `DONE_UNVERIFIED`: implementation appears present, but current proof is only
  static, stale, or indirect.
- `PARTIAL`: part of the implementation exists, but behavior, runtime coverage,
  docs, or release gates are incomplete.
- `NOT_STARTED`: no concrete implementation found.
- `BLOCKED`: missing assets, hardware, or product decisions prevent completion.
- `DEFERRED_BY_USER_ONLY`: explicitly deferred by user decision.

## Completion Gate

This phase is not complete until every item below is either `DONE_VERIFIED` or
`DEFERRED_BY_USER_ONLY`, and these commands pass from a current Release build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_material_robustness_contract_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_conductor_energy_contract_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_camera_motion_stability_smoke.ps1 -NoBuild -IsolatedLogs
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_phase3_visual_matrix.ps1 -NoBuild -TemporalSmokeFrames 90 -RTSmokeFrames 180 -IBLGalleryMaxEnvironments 1 -SkipSurfaceDebug
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1
```

Current integrated validation:

- 2026-05-12: current Release binary passed integrated release validation with
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild`.
  Summary: `CortexEngine/build/bin/logs/runs/release_validation_20260512_020250_531_184976_bf618209/release_validation_summary.json`,
  `status=passed`, `step_count=64`, `failure_count=0`.
- The native graphics widget render-scale assertion is deterministic in
  targeted validation: the script requests `render_scale=0.70` explicitly and
  disables only the perf quality governor during UI automation via
  `CORTEX_DISABLE_PERF_QUALITY_GOVERNOR=1`. Integrated release validation still
  uses its standard retry wrapper for occasional native-window startup timing.

## Source Map

| Key | Files and functions |
|---|---|
| MAT-REGISTRY | `src/Graphics/MaterialPresetRegistry.h`, `MaterialPresetRegistry.cpp::CanonicalPresets`, `Normalize`, `Canonicalize`, `Resolve` |
| MAT-RESOLVE | `src/Graphics/MaterialModel.h`, `MaterialModel.cpp::MaterialResolver::ResolveRenderable`, `Validate`, `BuildMaterialConstants`, `BuildVBMaterialConstants` |
| MAT-SHADERS | `assets/shaders/Basic.hlsl`, `MaterialResolve.hlsl`, `DeferredLighting.hlsl`, `PBR_Lighting.hlsli`, `Water.hlsl` |
| MAT-RT | `src/Graphics/RHI/DX12Raytracing_Materials.cpp::DX12RaytracingContext::BuildRTMaterialGPU`, frame-contract RT material parity fields |
| MAT-CONTRACT | `src/Graphics/FrameContract.h`, `FrameContractJson.cpp`, `Renderer_FrameContractSnapshot.cpp`, material sections in smoke scripts |
| ROBUST-MOTION | `src/Graphics/RendererGeometryUtils.cpp::ComputeAutoDepthSeparationForThinSurfaces`, `Renderer_WaterSurfaces.cpp::RenderWaterSurfaces`, `src/Core/Engine_Camera.cpp::UpdateCameraMotionAutomation` |
| ROBUST-UI | `src/UI/GraphicsSettingsWindow.cpp`, `src/Graphics/RendererTuningState.cpp`, `RendererControlApplier_Runtime.cpp`, `tools/run_graphics_native_widget_smoke.ps1` |
| ROBUST-VISUAL | `src/Core/VisualValidation.h`, `assets/config/visual_baselines.json`, `tools/run_visual_probe_validation.ps1`, `tools/run_phase3_visual_matrix.ps1` |
| ROBUST-EFFECTS | `src/Graphics/Renderer_Particles.cpp`, `src/Graphics/Passes/ParticleGpu*`, `src/Scene/ParticleEffectLibrary.cpp`, `assets/config/advanced_graphics_catalog.json` |

## Ledger Items

| ID | Requirement | Status | Source | Validation | Last Evidence | Remaining Work |
|---|---|---|---|---|---|---|
| MR-01 | Canonical material presets must resolve identically from ids, display names, spaced names, hyphenated names, and mixed case. | DONE_VERIFIED | MAT-REGISTRY | `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_material_robustness_contract_tests.ps1` | 2026-05-12: `Material robustness contract tests passed` after `MaterialPresetRegistry::Canonicalize`; command exited 0. | Add aliases only when new public preset names are added. |
| MR-02 | Public material presets must have physically plausible defaults: metallic/transmission conflict removed, roughness clamped, specular bounded, emissive bounded by validation policy. | DONE_VERIFIED | MAT-REGISTRY, MAT-RESOLVE | `run_material_robustness_contract_tests.ps1`; `run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` | 2026-05-12: material robustness contract exited 0; Material Lab smoke passed with logs `CortexEngine/build/bin/logs/runs/material_lab_20260512_014429_217_219700_c3b8abd1`; conductor energy contract passed with logs `CortexEngine/build/bin/logs/runs/conductor_energy_20260512_014429_198_216432_49d0d5e9`. | Extend with runtime per-preset probe scene if preset count grows significantly. |
| MR-03 | Forward, visibility-buffer, and RT material paths must preserve surface class parity for mirror, glass, water, conductor, emissive, and advanced material flags. | PARTIAL | MAT-RESOLVE, MAT-SHADERS, MAT-RT, MAT-CONTRACT | `run_rt_showcase_smoke.ps1`, `run_material_path_equivalence_tests.ps1`, `run_conductor_energy_contract_tests.ps1` | 2026-05-12 release validation passed `material_path_equivalence`, `rt_showcase`, and `conductor_energy_contract`; equivalence currently compares VB and forward material counts and visual stats in Material Lab, while RT parity is covered by RT showcase scene contracts. | Add a dedicated material-path parity matrix that instantiates every public preset in one scene and compares forward/VB/RT classification. |
| MR-04 | Material authoring UI must expose advanced material controls through existing command/applier paths and report validation state. | PARTIAL | ROBUST-UI, MAT-RESOLVE | `run_material_editor_contract_tests.ps1`; `run_material_editor_native_widget_smoke.ps1` | 2026-05-12 release validation passed `material_editor_contract` and `material_editor_native_widget`. | Add round-trip checks for canonical preset display names and aliases. |
| MR-05 | Shader invariants must prevent conductor energy blowout, overbright albedo, missing emissive bloom controls, and invalid wetness/procedural masks. | PARTIAL | MAT-SHADERS | `run_conductor_energy_contract_tests.ps1`; `run_material_robustness_contract_tests.ps1` | Conductor energy gate exists and passes; new robustness gate checks shader markers. | Add runtime overbright material stress scene separate from RT reflection outlier scene. |
| GR-01 | Moving-camera surface stability must be runtime-tested, not only object-motion or camera-cut tested. | DONE_VERIFIED | ROBUST-MOTION | `run_camera_motion_stability_smoke.ps1 -NoBuild -IsolatedLogs` | 2026-05-11: smoke passed with camera moved ~1.01 world units and valid visual capture. | Add a second moving-camera path through RT showcase mirror/glass panels. |
| GR-02 | Thin planes, vertical panels, blended glass, and water must receive deterministic depth-stability treatment. | DONE_VERIFIED | ROBUST-MOTION | `run_depth_stability_contract_tests.ps1`; `run_glass_water_courtyard_smoke.ps1 -NoBuild -IsolatedLogs` | 2026-05-11: depth contract and glass/water smoke passed after water path used `depthBiasNdc`. | Add debug visualization for auto depth separation if artifacts recur. |
| GR-03 | Graphics settings native UI must be deterministic enough for release validation. | DONE_VERIFIED | ROBUST-UI; `src/Core/Engine_UI.cpp::ApplyPerfQualityGovernor` | `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_graphics_native_widget_smoke.ps1 -NoBuild`; `run_release_validation.ps1 -NoBuild` | 2026-05-12: targeted smoke passed with logs `CortexEngine/build/bin/logs/runs/graphics_native_widget_20260512_015811_162_219236_c5a2de7e`; release validation passed `graphics_native_widget_retry2` with logs `CortexEngine/build/bin/logs/runs/release_validation_20260512_020250_531_184976_bf618209/graphics_native_widget_retry2`. Fix: script requests slider value 0.70 and sets `CORTEX_DISABLE_PERF_QUALITY_GOVERNOR=1`, preventing UI automation frame hitches from retuning render scale while preserving normal runtime governor behavior. | Reduce native-window startup timing flakiness if it recurs outside release retry policy. |
| GR-04 | Visual validation must catch black, overexposed, monochrome, low-detail, and unstable captures across public scenes. | PARTIAL | ROBUST-VISUAL | `run_visual_probe_validation.ps1`; `run_screenshot_negative_gates.ps1`; `run_phase3_visual_matrix.ps1` | 2026-05-12 release validation passed `visual_probe_validation`, `screenshot_negative_gates`, and `phase3_visual_matrix`. Existing gates cover edge structure, luma, nonblack, saturation, and negative screenshots. | Add scene-specific material-preset coverage metrics once material parity matrix exists. |
| GR-05 | Environment/IBL fallback policy must remain robust while material lighting work expands. | DONE_VERIFIED | `src/Graphics/EnvironmentManifest.cpp`, `assets/environments/environments.json` | `run_environment_manifest_tests.ps1`; `run_ibl_asset_policy_tests.ps1`; `run_phase3_fallback_matrix.ps1 -NoBuild` | Existing Phase 3 release gate evidence shows manifest, asset policy, and fallback matrix passed. | Re-run after adding any new environment assets. |
| GR-06 | Particles and cinematic effects must remain budgeted and optional, with zero-cost disabled path. | PARTIAL | ROBUST-EFFECTS | `run_effects_gallery_tests.ps1`; `run_particle_disabled_zero_cost.ps1`; `run_gpu_particle_contract_tests.ps1` | 2026-05-12 release validation passed `advanced_graphics_catalog`, `gpu_particle_contract`, `effects_gallery`, and `particle_disabled_zero_cost`. | Add motion-camera effects capture and stricter smoke/fog readability gate. |
| GR-07 | Release validation must include material robustness and moving-camera robustness gates. | DONE_VERIFIED | `tools/run_release_validation.ps1` | `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1 -NoBuild` | 2026-05-12: release validation summary `CortexEngine/build/bin/logs/runs/release_validation_20260512_020250_531_184976_bf618209/release_validation_summary.json`, `status=passed`, `step_count=64`, `failure_count=0`; included `camera_motion_stability`, `material_robustness_contract`, `material_path_equivalence`, and `phase3_visual_matrix`. | Re-run after new ledger items are implemented or release validation changes. |

## Immediate Implementation Queue

1. `MR-01`: implement canonical material preset normalization and alias-safe
   resolution. Add `run_material_robustness_contract_tests.ps1`.
2. `MR-02`: make the same contract check preset default invariants and run
   Material Lab after the source change.
3. `GR-07`: wire the material robustness contract into release validation.
4. `GR-03`: inspect and fix the native graphics widget render-scale mismatch.
5. `MR-03`: add a full material-path parity matrix scene/script.
6. `GR-04`: extend visual probes with material-preset coverage metrics.
