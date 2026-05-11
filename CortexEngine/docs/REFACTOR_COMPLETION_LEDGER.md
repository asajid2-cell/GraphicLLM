# Cortex Refactor Completion Ledger

Audit date: 2026-05-10

This ledger audits `phase2.md` and `phase3.md` against the current repository.
It does not mark the renderer refactor complete. It separates runtime-verified
implementation from metadata-only coverage, older pass-log evidence, partial
foundations, and future/deferred work.

Status values:

- `DONE_VERIFIED`: implementation exists and is covered by a current runtime or
  focused validation command whose latest successful output was inspected.
- `DONE_UNVERIFIED`: source appears present, but the current latest validation
  did not directly exercise the exact claim, or the evidence is only historical
  in `phase2.md`.
- `PARTIAL`: some implementation or contract coverage exists, but the item is
  not fully implemented, not runtime-tested deeply enough, or remains broad.
- `NOT_STARTED`: no concrete source/runtime implementation was found.
- `BLOCKED`: blocked by missing prerequisites, assets, hardware, or a known
  unresolved failure.
- `DEFERRED_BY_USER_ONLY`: explicitly outside the current renderer path by user
  direction, not because the implementation is done.

## Audit Inputs

Read during this audit:

- `phase2.md`
- `phase3.md`
- `CortexEngine/README.md`
- `CortexEngine/RELEASE_READINESS.md`
- `CortexEngine/tools/run_release_validation.ps1`
- all current `CortexEngine/tools/*.ps1` validation/smoke/contract scripts
- recent git history through `git log --oneline -80`

Latest inspected full validation run:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1
logs=CortexEngine/build/bin/logs/runs/release_validation_20260510_235833_231_38436_cedd6bec
```

Key evidence from that run:

- Release build: passed.
- Build entrypoint contract: passed; rebuild uses `vswhere`/`VsDevCmd.bat`
  plus `cmake --build`, release validation calls `rebuild.ps1`, and raw Ninja
  is not used by rebuild/release.
- Repository hygiene: passed; `git diff --check` passed, generated-artifact
  tracked count is 0, and `.gitignore` contains local artifact guards.
- Source-list contract: passed; CMake entries exist, duplicate count is 0,
  renderer split sources are covered, and temporary source count is 0.
- Render-graph boundary contract: passed; transient-validation env wiring,
  validation module integration, and VB staged/legacy graph boundaries are covered.
- Visibility-buffer transition contract: passed; graph-owned VB stage
  transition-skip controls, state snapshots, and final state writeback are covered.
- Debug primitive contract: passed; debug-line state ownership and draw-contract
  counters are covered.
- Editor frame contract: passed; editor renderer hooks and explicit editor frame
  sequence are covered.
- Temporal validation: `gpu_ms=1.267`, `disocclusion=0.006633`,
  `high_motion=0.005166`, `object_motion=0.0731`, `visible=7`, `warnings=0`.
- Temporal camera cut: `frames=53`, `cut_frame=20`,
  `camera=reflection_closeup`, `gpu_ms=3.291`,
  `rt_reflection_reset=camera_cut`, `invalidated_frame=20`.
- RT showcase: `frames=33`, `gpu_ms=2.022/16.7`,
  `dxgi_mb=408.46/512`, `est_mb=190.52/256`, `rt_mb=114.63/160`,
  `write_mb=107.75/128`, `material_issues=0`,
  `rt_refl_ready=True/ready`,
  `rt_signal=0.0225/0.1424/10.3398/0.0084`,
  `rt_hist=0.0314/0.1433/7.3008/0.0089`,
  `transient_delta=0`, `rt_budget=8gb_balanced`, `startup_realloc=0`,
  `temporal_diff=mean=0.009/2.5 changed=0.000/0.08`,
  `surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- VB debug views: `vb_depth` view 34 nonblack `0.851`, colorful `0.001`,
  luma `168.88`; `vb_gbuffer_albedo` view 35 nonblack `0.851`,
  colorful `0.251`, luma `148.49`.
- Descriptor/memory stress: `persistent_descriptors=988/1024`,
  `staging=78/128`, `transient_budget=81920`, `transient_delta=0`,
  `dxgi_mb=408.46/512`, `estimated_mb=190.52/256`,
  `write_mb=107.75/128`, `rt_signal_avg=0.0225`,
  `rt_history_avg=0.0314`.
- Visual probe: all four public baseline cases passed; minimum observed
  edge count was 6060 pixels, minimum edge ratio was 0.0066, maximum pure
  dominant-color ratio was 0.0066, and maximum configured dominant-color
  threshold was 0.120.
- Render-graph transient matrix: aliasing on/off and bloom-transients-off rows
  passed; aliasing-on saved 262144 bytes with 2 alias barriers, aliasing-off
  reported 0 aliased resources/barriers/saved bytes, and bloom-transients-off
  reported 0 final transient resources with validation still run.
- Phase 3 visual matrix: temporal validation, RT showcase, material lab,
  glass/water courtyard, effects showcase, uncapped IBL gallery, and fallback
  matrix passed.
- Renderer ownership, full renderer ownership audit, VB debug views,
  descriptor/memory stress, visual probe, fatal error, advanced graphics catalog, effects gallery,
  environment manifest, IBL asset policy, uncapped IBL gallery, budget profile matrix, and voxel backend
  gates passed.
- New focused gates passed in release validation: temporal camera cut,
  visibility-buffer transition contract,
  render-graph transient matrix, full renderer ownership audit,
  descriptor/memory stress, VB debug views, visual probe, graphics UI interaction, screenshot
  negative gates, particle-disabled zero-cost, six-case Phase 3 fallback matrix, RT
  firefly/outlier, LLM renderer command routing, positive Dreamer runtime,
  conductor energy, vegetation state, and local reflection probes.
- LLM renderer command smoke: deterministic mock Architect startup command
  applied exposure `1.35`, disabled shadows, enabled fog with density `0.031`,
  set water amplitude `0.07`, and selected `studio_three_point` lighting rig
  with frame-contract source `renderer_rig`.
- Dreamer positive runtime smoke: deterministic mock Architect startup
  `generate_texture` command ran with Dreamer enabled; Dreamer initialized,
  DiffusionEngine reported its backend/fallback, generated a texture for
  `TemporalLab_RotatingRedSphere`, the renderer created a `Dreamer_*` GPU
  texture, and the result was applied to the target entity.
- Conductor energy contract: shader invariants passed for forward/deferred
  conductor energy split; Material Lab passed with `resolved_conductor=4`,
  `reflection_conductor=4`, `max_metallic=1.0`, `very_bright_albedo=0`, and
  bounded saturated/near-white ratios.
- Lighting energy budget: RT Showcase, Material Lab, Glass/Water Courtyard,
  and Effects Showcase passed scene-level lighting/exposure/bloom and
  near-white/saturation budget checks; summary logs were written under
  `lighting_energy_budget_20260511_000148_909_42292_47e5508e`.
- Particle effect library: advanced graphics catalog passed with fire, smoke,
  dust, sparks, embers, mist, rain, snow, and procedural billboard fallback;
  Effects Gallery passed with `emitters=8`, `particles=77`, and
  particle-disabled zero-cost still passed.
- Vegetation state contract: extracted state bundle, renderer ownership audit
  coverage, frame-contract serialization, and dormant public draw pipeline
  reporting passed in the latest release gate.
- Local reflection probe contract: RT Showcase declares two local probes, VB
  deferred lighting uploads/binds them, the frame contract reports two uploaded
  probes with zero skips, and debug view 42 captures non-empty probe weights.

Recent git history relevant to the audit:

```text
9352b46 Add fallback row to Phase 3 visual matrix
73f77f4 Refresh environment fallback ledger statuses
d3ed555 Add IBL asset policy gate
04a0e9b Add environment fallback matrix coverage
abad454 Run IBL gallery across all enabled environments
63a6200 Add Phase 2 validation entrypoint
0610ec7 Add positive Dreamer runtime validation
97d96ad Add local reflection probe validation
5e7b69f Update Phase 3 release readiness docs
9293e44 Tighten Phase 3 release metadata
```

## Source Map

Rows below reference these exact source files and functions to avoid repeating
long file/function lists in every row.

| Key | Files and functions |
|---|---|
| SRC-RENDER-ORCH | `src/Graphics/Renderer_RenderOrchestration.cpp::Renderer::Render`, `src/Graphics/Renderer_FramePlanning.cpp`, `src/Graphics/Renderer_FramePhases_Prepare.cpp::Renderer::ExecuteRayTracingFramePhase`, `Renderer::ExecuteShadowFramePhase`, `src/Graphics/Renderer_FramePhases_Main.cpp::Renderer::BeginMainSceneFramePhase`, `Renderer::ExecuteGeometryFramePhase`, `Renderer::ExecuteMainSceneEffectsFramePhase`, `src/Graphics/Renderer_FramePhases_Post.cpp::Renderer::ExecutePostProcessingFramePhase` |
| SRC-FEATURE-PLAN | `src/Graphics/FrameFeaturePlan.h::FrameFeaturePlan`, `FrameFeaturePlanInputs`, `FrameExecutionContext`, `src/Graphics/FrameFeaturePlan.cpp::BuildFrameFeaturePlan` |
| SRC-SNAPSHOT | `src/Graphics/RendererSceneSnapshot.h::RendererSceneSnapshot`, `src/Graphics/RendererSceneSnapshot.cpp::BuildRendererSceneSnapshot`, consumers in `Renderer_FramePlanning.cpp`, `Renderer_VisibilityBufferCollection.cpp::Renderer::CollectInstancesForVisibilityBuffer`, `Renderer_RTShadowsGI.cpp`, `Renderer_ForwardPass.cpp`, `Renderer_WaterSurfaces.cpp`, `Renderer_TransparentGeometry.cpp`, `Renderer_ShadowPass.cpp` |
| SRC-RENDERGRAPH | `src/Graphics/RenderGraph.h`, `Renderer_RenderGraphDepthShadow.cpp::Renderer::ExecuteDepthPrepassInRenderGraph`, `Renderer_RenderGraphVisibilityBuffer.cpp::Renderer::ExecuteVisibilityBufferInRenderGraph`, `Renderer_RenderGraphMotionVectors.cpp::Renderer::ExecuteMotionVectorsInRenderGraph`, `Renderer_RenderGraphTAA.cpp::Renderer::ExecuteTAAInRenderGraph`, `Renderer_RenderGraphSSAO.cpp::Renderer::ExecuteSSAOInRenderGraph`, `Renderer_RenderGraphSSR.cpp::Renderer::ExecuteSSRInRenderGraph`, `Renderer_RenderGraphBloom.cpp::Renderer::ExecuteBloomInRenderGraph`, `Renderer_RenderGraphEndFrame.cpp::Renderer::ExecuteEndFrameInRenderGraph`, `Renderer_RenderGraphDiagnostics.cpp` |
| SRC-VB | `src/Graphics/VisibilityBuffer.h::VisibilityBufferRenderer`, `VisibilityBufferRenderer::ResourceStateSnapshot`, `VisibilityBufferRenderer::GetResourceStateSnapshot`, `VisibilityBufferRenderer::ApplyResourceStateSnapshot`, `VisibilityBuffer_Resolve.cpp::VisibilityBufferRenderer::ResolveMaterials`, `Renderer_VisibilityBufferOrchestration.cpp::Renderer::RenderVisibilityBufferPath`, `Renderer_VisibilityBufferStages.cpp`, `Renderer_RenderGraphVisibilityBufferHelpers.h::VisibilityBufferGraphResources` |
| SRC-MATERIAL | `src/Graphics/MaterialModel.h::MaterialModel`, `src/Graphics/MaterialModel.cpp::MaterialResolver::ResolveRenderable`, `MaterialResolver::Validate`, `MaterialResolver::BuildMaterialConstants`, `MaterialResolver::BuildVBMaterialConstants`, `src/Graphics/MaterialPresetRegistry.cpp::MaterialPresetRegistry::Resolve`, `src/Graphics/SurfaceClassification.h::ClassifySurface`, `src/Graphics/RHI/DX12Raytracing_Materials.cpp::DX12RaytracingContext::BuildRTMaterialGPU` |
| SRC-RT | `src/Graphics/RTScheduler.h::RTScheduler`, `src/Graphics/RTScheduler.cpp::RTScheduler::BuildFramePlan`, `src/Graphics/RTDenoiser.cpp::RTDenoiser::Dispatch`, `src/Graphics/Renderer_RTFramePlan.cpp::Renderer::UpdateRTFramePlan`, `Renderer_RTShadowsGI.cpp`, `Renderer_RTReflections.cpp`, `Renderer_RTDenoise.cpp`, `Renderer_RTReflectionSignalStats.cpp::Renderer::CaptureRTReflectionSignalStats`, `Renderer::CaptureRTReflectionHistorySignalStats`, `src/Graphics/RHI/DX12Raytracing_TLAS.cpp::DX12RaytracingContext::BuildTLAS` |
| SRC-TEMPORAL | `src/Graphics/TemporalManager.cpp::TemporalManager::BeginFrame`, `TemporalManager::Invalidate`, `TemporalManager::MarkValid`, `src/Graphics/TemporalRejectionMask.cpp::TemporalRejectionMask::Dispatch`, `TemporalRejectionMask::DispatchStats`, `TemporalRejectionMask::AddToGraph`, `Renderer_TemporalMaskPass.cpp::Renderer::BuildTemporalRejectionMask`, `Renderer::CaptureTemporalRejectionMaskStats`, `Renderer_TemporalReadback.cpp::Renderer::UpdateTemporalRejectionMaskStatsFromReadback` |
| SRC-BUDGET | `src/Graphics/BudgetPlanner.cpp::BudgetPlanner::BuildPlan`, `src/Graphics/TextureAdmission.cpp`, `src/Graphics/TextureSourcePlan.cpp`, `src/Graphics/AssetRegistry.h`, `Renderer_FrameContractMemory.cpp`, `Renderer_RTResources.cpp`, `Renderer_ShadowResources.cpp`, `Renderer_SSAO.cpp` |
| SRC-CONTRACT | `src/Graphics/FrameContract.h::FrameContract`, `src/Graphics/FrameContractJson.cpp::FrameContractToJson`, `src/Graphics/FrameContractValidation.cpp::ValidateFrameContractSnapshot`, `src/Graphics/Renderer_FrameContractSnapshot.cpp::Renderer::UpdateFrameContractSnapshot`, `src/Graphics/Renderer_FrameContractPasses.cpp::Renderer::RecordFramePass`, `src/Core/Engine.cpp` frame report export |
| SRC-STATE | `src/Graphics/Renderer.h`, state headers included by it: `RendererServiceState.h`, `RendererRTState.h`, `RendererTemporalState.h`, `RendererVisibilityBufferState.h`, `RendererFramePlanningState.h`, `RendererFrameDiagnosticsState.h`, `RendererCommandResourceState.h`, `RendererAssetRuntimeState.h`, `RendererBloomState.h`, `RendererSSAOState.h`, `RendererSSRState.h`, `RendererEnvironmentState.h`, `RendererParticleState.h`, and related `Renderer_*State.h` headers |
| SRC-UI-P3 | `src/UI/GraphicsSettingsWindow.cpp`, `src/UI/SceneEditorWindow.cpp`, `src/Core/Engine_UI.cpp`, `src/Core/Engine_Input.cpp`, `src/Graphics/RendererTuningState.cpp`, `src/Graphics/RendererControlApplier_Runtime.cpp`, `src/Graphics/RendererControlApplier_ScenePresets.cpp` |
| SRC-ENV-P3 | `src/Graphics/EnvironmentManifest.cpp::LoadEnvironmentManifest`, `DefaultEnvironmentManifestPath`, `IsEnvironmentAllowedForBudget`, `src/Graphics/Renderer_Environment.cpp`, `src/Graphics/RendererEnvironmentState.h`, `src/Core/StartupPreflight.cpp`, `assets/environments/environments.json` |
| SRC-SCENES | `src/Core/Engine_Scenes.cpp`, `assets/config/showcase_scenes.json`, `assets/config/visual_baselines.json`, `assets/config/advanced_graphics_catalog.json`, `assets/config/graphics_presets.json` |
| SRC-DOCS | `README.md`, `RELEASE_READINESS.md`, `tools/README.md`, `phase2.md`, `.gitignore` |
| SRC-PREFLIGHT | `src/Core/StartupPreflight.cpp`, `src/Core/Engine.cpp` startup path, frame report startup/health export paths |
| SRC-FATAL | `src/Core/Engine.cpp` fatal handling path, renderer failure summary output, `tools/run_fatal_error_contract_tests.ps1` |
| SRC-VISUAL | `src/Core/VisualValidation.h`, smoke scripts that parse `visual_validation.image_stats`, `assets/config/visual_baselines.json` |

## Validation Scripts Audited

| Script | Runtime or static | Current role |
|---|---|---|
| `tools/run_release_validation.ps1` | orchestration | Current top-level release gate. Builds Release, runs all listed checks below, and treats top-level `failed:` sentinel output as a failed step. |
| `tools/run_phase2_validation.ps1` | orchestration | Named `phase2.md` validation entrypoint. Delegates to the current release gate so Phase 2 callers get the broader Phase 3-era validation suite instead of a stale partial suite. |
| `tools/run_build_entrypoint_contract_tests.ps1` | static/contract | Checks that rebuild/release validation use `rebuild.ps1` and `cmake --build`, with VS environment import guards, instead of raw Ninja invocation. |
| `tools/run_repo_hygiene_tests.ps1` | static/contract | Runs `git diff --check`, verifies generated build/cache artifacts are not tracked, and checks required `.gitignore` guards. |
| `tools/run_source_list_contract_tests.ps1` | static/contract | Checks explicit CMake source entries for existence, duplicates, renderer split coverage, and temporary/backup source names. |
| `tools/run_render_graph_boundary_contract_tests.ps1` | static/contract | Checks transient-validation env wiring, `RenderGraphValidationPass` integration, and visibility-buffer staged/legacy graph boundary pass names/fallback accounting. |
| `tools/run_visibility_buffer_transition_contract_tests.ps1` | static/contract | Checks `VisibilityBufferRenderer` transition-skip fields, graph-owned stage skip-control use, resource-state snapshots, and final state writeback from the render graph. |
| `tools/run_debug_primitive_contract_tests.ps1` | static/contract | Checks debug-line state ownership, renderer API surface, and frame-contract debug draw counters. |
| `tools/run_editor_frame_contract_tests.ps1` | static/contract | Checks editor renderer hook declarations/delegation and the explicit `EngineEditorMode` frame sequence. |
| `tools/run_rt_showcase_smoke.ps1` | runtime | Main RT showcase runtime gate for budgets, materials, RT readiness, raw/history signal, descriptor delta, visual stats. |
| `tools/run_vb_debug_views.ps1` | runtime wrapper | RT Showcase debug-view matrix for visibility-buffer depth and material-albedo debug captures. |
| `tools/run_reflection_probe_contract_tests.ps1` | runtime/static | RT Showcase local reflection probe gate for scene probe components, VB deferred probe upload/binding, frame-contract reporting, and debug view 42 probe-weight capture. |
| `tools/run_descriptor_memory_stress_scene.ps1` | runtime | Descriptor-heavy RT showcase stress gate for the historical 1024 persistent-descriptor ceiling, staging budget, transient balance, memory budgets, and raw/history RT signal. |
| `tools/run_visual_probe_validation.ps1` | runtime | Runs every public visual baseline case and probes captured BMPs for edge structure and dominant-color failure modes in addition to baseline metric tolerances. |
| `tools/run_temporal_validation_smoke.ps1` | runtime | Temporal-scene gate for temporal mask, motion vectors, histories, budget, visual capture. |
| `tools/run_temporal_camera_cut_validation.ps1` | runtime | RT Showcase camera-bookmark jump gate that verifies RT shadow/reflection/GI histories report `camera_cut`, reseed, and remain resource-valid. |
| `tools/run_render_graph_transient_matrix.ps1` | runtime wrapper | RT Showcase matrix for render-graph transient validation, aliasing on/off behavior, bloom-transient disable mode, and descriptor delta stability. |
| `tools/run_budget_profile_matrix.ps1` | runtime | RT showcase under budget profiles including low-memory variants. |
| `tools/run_voxel_backend_smoke.ps1` | runtime | Experimental voxel backend smoke. |
| `tools/run_phase3_visual_matrix.ps1` | runtime wrapper | Public scene matrix for Phase 3; relevant to newer release-readiness claims, not proof of all Phase 2 aspirations. |
| `tools/run_renderer_ownership_tests.ps1` | static/contract | Checks release ownership metadata and selected state-boundary markers. Does not prove all pass ownership is complete. |
| `tools/run_renderer_full_ownership_audit.ps1` | static/contract | Exhaustively enumerates `Renderer.h` members, requiring every member to be a named state/service aggregate and rejecting loose GPU resource/descriptors in `Renderer.h`. |
| `tools/run_graphics_ui_contract_tests.ps1` | static/contract | Checks unified graphics UI wiring. Phase 3 only. |
| `tools/run_graphics_settings_persistence_tests.ps1` | runtime/static | Checks tuning persistence path. Phase 3 only. |
| `tools/run_graphics_preset_tests.ps1` | runtime/static | Checks graphics preset schema and optional runtime smoke. |
| `tools/run_hud_mode_contract_tests.ps1` | runtime/static | Checks HUD mode CLI/F7/report behavior. Phase 3 only. |
| `tools/run_showcase_scene_contract_tests.ps1` | static/runtime | Checks showcase metadata, no polish gaps, and one RT runtime bookmark smoke. |
| `tools/run_material_editor_contract_tests.ps1` | static | Checks material editor controls and validation status; not a runtime material rendering test. |
| `tools/run_material_lab_smoke.ps1` | runtime | Runtime material lab visual/material coverage smoke. |
| `tools/run_glass_water_courtyard_smoke.ps1` | runtime | Runtime glass/water/fog/RT/material scene smoke. |
| `tools/run_effects_showcase_smoke.ps1` | runtime | Runtime effects scene smoke for particles, cinematic post, advanced material coverage. |
| `tools/run_effects_gallery_tests.ps1` | runtime wrapper | Effects showcase plus catalog/contract checks, including the public particle effect descriptor library. |
| `tools/run_environment_manifest_tests.ps1` | static/asset | Manifest schema/default/fallback/runtime asset checks; by itself not enough for runtime IBL behavior. |
| `tools/run_ibl_asset_policy_tests.ps1` | static/asset | IBL startup/release-size policy: no startup downloads, source assets optional, legacy scan disabled, runtime format preferences, and budget-class asset size caps. |
| `tools/run_ibl_gallery_tests.ps1` | runtime | Runtime environment loading and IBL visual checks. |
| `tools/run_visual_baseline_contract_tests.ps1` | static/runtime sample | Visual baseline manifest and limited runtime case; not full golden-image comparison. |
| `tools/run_advanced_graphics_catalog_tests.ps1` | static/metadata | Catalog release-foundation coverage; does not prove future advanced features. |
| `tools/run_fatal_error_contract_tests.ps1` | runtime/static | Fatal summary contract. |
| `tools/run_phase3_fallback_matrix.ps1` | runtime | Safe startup, missing selected environment, missing manifest, missing required environment asset, missing optional environment asset, and explicit no-RT profile fallback reporting. |
| `tools/run_graphics_ui_interaction_smoke.ps1` | runtime | Applies `RendererTuningState` through the startup settings path and asserts frame-contract control values. Not native mouse/keyboard automation. |
| `tools/run_dreamer_positive_runtime_tests.ps1` | runtime/static | Runs Architect mock mode with Dreamer enabled, queues a startup `generate_texture`, and verifies Dreamer startup, backend/fallback reporting, generated pixels, GPU texture creation, and material application. |
| `tools/run_screenshot_negative_gates.ps1` | static/generated images | Synthetic black, white, saturated, and edge-heavy BMP negative gates for screenshot metric logic. |
| `tools/run_particle_disabled_zero_cost.ps1` | runtime | Effects Showcase under `safe_startup`; asserts zero particle planning/execution/submission/allocation. |
| `tools/run_rt_firefly_outlier_scene.ps1` | runtime wrapper | RT Showcase with stricter raw/history reflection outlier thresholds. |
| `tools/run_lighting_energy_budget_tests.ps1` | runtime wrapper | Runs public showcase smokes and fails scene-level lighting/exposure/bloom or near-white/saturation budget regressions. |

## Phase 2 Top-Level Requirements

| ID | Requirement or claim from `phase2.md` | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| P2-GLOBAL-01 | Keep RT, visibility buffer, GPU culling, HZB, TAA, SSR, SSAO, bloom, IBL, fog, god-rays, water, transparency, particles, and eventual outdoor scenes first-class. | PARTIAL | SRC-RENDER-ORCH, SRC-RENDERGRAPH, SRC-VB, SRC-RT, SRC-TEMPORAL, SRC-BUDGET, SRC-SCENES | `tools/run_release_validation.ps1` | RT/VB/GPU culling/HZB/TAA/SSR/SSAO/bloom/IBL/fog/water/transparency are exercised by RT showcase and temporal/material/glass/effects smokes. Particles are covered in effects showcase. | Outdoor scenes are not implemented as a Phase 2 runtime gate. Particles are public ECS billboard path, not full GPU particle maturity. Treat "first-class eventual outdoor scenes" as not complete. |
| P2-GLOBAL-02 | Make the renderer correct, measurable, budgeted, and cleanly owned. | PARTIAL | SRC-CONTRACT, SRC-BUDGET, SRC-STATE, SRC-RENDERGRAPH | `tools/run_release_validation.ps1`; `tools/run_renderer_ownership_tests.ps1`; `tools/run_renderer_full_ownership_audit.ps1` | Full release gate passed; selected ownership gate passed; full ownership audit passed with 48/48 renderer members covered by named state/service aggregates. | Renderer still orchestrates many cross-cutting systems. "Cleanly owned" remains broad and should not be marked complete until pass/function ownership is reduced further or explicitly scoped. |
| P2-SNAPSHOT-01 | Current build state is reproducible through checked-in rebuild script. | DONE_VERIFIED | `rebuild.ps1`, `tools/run_release_validation.ps1`, SRC-DOCS | `tools/run_release_validation.ps1` | `build_release` step passed in latest release run. | None for current local machine; still hardware/toolchain dependent. |
| P2-SNAPSHOT-02 | Raw Ninja should not be used from an unprepared shell. | DONE_VERIFIED | `rebuild.ps1`, `setup.ps1`, `tools/run_release_validation.ps1`, `BUILD.md` | `tools/run_build_entrypoint_contract_tests.ps1`; release gate | Targeted contract passed: `rebuild.ps1` imports the Visual Studio environment through `vswhere`/`VsDevCmd.bat`, uses `cmake --build`, release validation calls `rebuild.ps1`, and neither rebuild nor release validation invokes raw Ninja directly. | None for the current scripted build entrypoint contract. |
| P2-SNAPSHOT-03 | Bloom transient validation passes in alias/no-alias modes and bloom transients are default-on. | DONE_VERIFIED | SRC-RENDERGRAPH, `Renderer_RenderGraphBloom.cpp::Renderer::ExecuteBloomInRenderGraph`, `Renderer_RenderGraphEndFrame.cpp::Renderer::ExecuteEndFrameInRenderGraph`, `Renderer_RenderGraphDiagnostics.cpp::Renderer::RunRenderGraphTransientValidation` | `tools/run_render_graph_transient_matrix.ps1 -NoBuild -IsolatedLogs`; release gate | Focused matrix passed: aliasing-on transients=6, aliased=2, barriers=2, saved=262144; aliasing-off transients=6, aliased=0, barriers=0, saved=0; bloom-transients-off transients=0 and `transient_validation_ran=true`. | None for alias/no-alias bloom-transient coverage. |

## Progress Ledger Passes From `phase2.md`

These are the explicit pass entries in the `Progress Ledger` section at the top
of `phase2.md`.

| ID | Pass | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| P2-PASS-01 | Pass 1 - Finish Renderer Split | DONE_VERIFIED | SRC-RENDER-ORCH, SRC-STATE | `tools/run_release_validation.ps1` | Build and release gate passed. `Renderer_RenderOrchestration.cpp::Renderer::Render` is the active orchestrator. | Further splitting remains under ownership cleanup but the original `Renderer.cpp` facade goal is met. |
| P2-PASS-02 | Pass 2 - Frame Feature Plan And Render Context | DONE_VERIFIED | SRC-FEATURE-PLAN, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` via release gate | RT showcase frame report is exported and release gate checks planned/executed feature behavior through smokes. | None for current contract; future features must keep using the plan. |
| P2-PASS-03A | Pass 3A - Renderer Scene Snapshot Foundation | DONE_VERIFIED | SRC-SNAPSHOT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase and temporal scenes build deterministic scene snapshots through `Renderer_FramePlanning.cpp`. | Snapshot coverage is runtime-smoke based, not exhaustive for editor/debug fallbacks. |
| P2-PASS-03B | Pass 3B - Snapshot-Driven Visibility Collection | DONE_VERIFIED | SRC-SNAPSHOT, SRC-VB | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase log: VB rendered with `instances=27 meshes=13`; smoke passed with warnings=0. | Add focused tests for snapshot/VB drift if regressions appear. |
| P2-PASS-03C | Pass 3C - Snapshot-Driven RT TLAS Input | DONE_VERIFIED | SRC-SNAPSHOT, SRC-RT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase: `rt_refl_ready=True/ready`, `rt_parity=True/0`, nonzero RT signal/history. | Editor/debug fallback path is not separately validated. |
| P2-PASS-03D | Pass 3D - Snapshot-Driven Forward And Fallback Draw Collection | DONE_VERIFIED | SRC-SNAPSHOT, `Renderer_ForwardPass.cpp`, `Renderer_OverlayGeometry.cpp`, `Renderer_WaterSurfaces.cpp`, `Renderer_TransparentGeometry.cpp` | `tools/run_release_validation.ps1` | RT/material/glass/effects smokes pass and exercise forward/transparent/water paths. | Fallback draw paths are not exhaustively tested under every feature toggle. |
| P2-PASS-04A | Pass 4A - Render Graph Adapter And Frame Diagnostics | DONE_VERIFIED | SRC-RENDERGRAPH, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase reports graph pass/budget fields and passes with warnings=0. | None for adapter-level smoke; deeper graph invariants remain covered by targeted scripts only. |
| P2-PASS-04B | Pass 4B - End-Frame Graph Adapter Extraction | DONE_VERIFIED | `Renderer_RenderGraphEndFrame.cpp::Renderer::ExecuteEndFrameInRenderGraph`, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase passes post-process/bloom/TAA/end-frame path. | Add explicit failure gate for graph fallback count if it is not already in smoke output. |
| P2-PASS-04C | Pass 4C - Depth Prepass Render Graph Adapter | DONE_VERIFIED | `Renderer_RenderGraphDepthShadow.cpp::Renderer::ExecuteDepthPrepassInRenderGraph` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase passes and uses depth/RT readiness. | Legacy barrier fallback remains; not all fallback paths are proven. |
| P2-PASS-04D | Pass 4D - Visibility Buffer Graph Boundary | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBuffer.cpp::Renderer::ExecuteVisibilityBufferInRenderGraph`, SRC-VB | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase log shows VB enabled/rendered and smoke passes. | Debug/fallback combinations remain not fully covered in latest release gate. |
| P2-PASS-04E | Pass 4E - Visibility Buffer Stage Split | DONE_VERIFIED | `Renderer_VisibilityBufferOrchestration.cpp::Renderer::RenderVisibilityBufferPath`, `Renderer_VisibilityBufferStages.cpp` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase VB path runs successfully. | No current targeted stage-level test beyond smoke. |
| P2-PASS-04F | Pass 4F - Visibility Buffer Resource State Snapshot | DONE_VERIFIED | SRC-VB | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase and temporal smokes pass with VB enabled. | Specific state restore assertions are indirect. |
| P2-PASS-04G | Pass 4G - Visibility Buffer Transition Skip Controls | DONE_VERIFIED | `VisibilityBuffer.h::VisibilityBufferRenderer::TransitionSkipControls`, `VisibilityBuffer.h::GetTransitionSkipControls`, `VisibilityBuffer.h::SetTransitionSkipControls`, `Renderer_RenderGraphVisibilityBuffer.cpp::Renderer::ExecuteVisibilityBufferInRenderGraph`, `Renderer_RenderGraphVisibilityBufferHelpers.h::ImportVisibilityBufferGraphResources`, `VisibilityBuffer_VisibilityPass.cpp`, `VisibilityBuffer_Resolve.cpp`, `VisibilityBuffer_DebugBlit.cpp`, `VisibilityBuffer_DeferredLighting.cpp` | `tools/run_visibility_buffer_transition_contract_tests.ps1`; `tools/run_release_validation.ps1` | Targeted gate passed with `transition_skip_fields=covered`, `graph_owned_state_snapshots=covered`, `graph_final_state_writeback=covered`; release run `release_validation_20260510_212145_379_150620_04ddc79a` passed with `visibility_buffer_transition_contract`. | None for current static transition-skip ownership contract; runtime VB debug behavior remains covered by P2-PASS-04J. |
| P2-PASS-04H | Pass 4H - Visibility Buffer Internal Graph Nodes | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBuffer.cpp` records `VBClear`, `VBVisibility`, `VBMaterialResolve`, `VBDeferredLighting` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase passes through VB path with frame contract pass records. | Debug path/fallback still not exhaustively tested. |
| P2-PASS-04I | Pass 4I - VB Lighting Graph Resources And Subpass Records | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBuffer.cpp`, SRC-VB | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | Current frame contract checks pass; VB initialization log includes clustered light/BRDF LUT pipelines. | No separate assertion in top-level output for every VB subpass name. |
| P2-PASS-04J | Pass 4J - VB Debug Graph Paths And Mesh Table Contract | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBuffer.cpp`, `Renderer_VisibilityBufferDiagnostics.cpp`, SRC-VB | `tools/run_vb_debug_views.ps1 -NoBuild` | Release-gated targeted run passed: `vb_depth` view 34 nonblack `0.851`, colorful `0.001`, luma `168.88`; `vb_gbuffer_albedo` view 35 nonblack `0.851`, colorful `0.251`, luma `148.49`. Logs: `CortexEngine/build/bin/logs/runs/vb_debug_views_20260510_203407_586_156864_3da88568`. | None for the current depth/albedo VB debug-view runtime contract; transition skip controls remain tracked separately in P2-PASS-04G. |
| P2-PASS-04K | Pass 4K - VB Graph Helper And Motion Vector Graph Adapter | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBufferHelpers.h::VisibilityBufferGraphResources`, `Renderer_RenderGraphMotionVectors.cpp::Renderer::ExecuteMotionVectorsInRenderGraph` | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | Temporal validation passed with `object_motion=0.0731`, `visible=7`, `warnings=0`. | Add explicit report of camera-only fallback absence for more scenes. |
| P2-PASS-04L | Pass 4L - TAA And SSAO Graph Ownership | DONE_VERIFIED | `Renderer_RenderGraphTAA.cpp::Renderer::ExecuteTAAInRenderGraph`, `Renderer_RenderGraphSSAO.cpp::Renderer::ExecuteSSAOInRenderGraph` | `tools/run_release_validation.ps1` | Temporal and RT showcase smokes pass with TAA/SSAO enabled. | No dedicated SSAO visual comparison. |
| P2-PASS-04M | Pass 4M - SSR Graph Ownership | DONE_VERIFIED | `Renderer_RenderGraphSSR.cpp::Renderer::ExecuteSSRInRenderGraph` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase smoke passes with SSR enabled in feature set. | SSR visual correctness is not deeply measured beyond luma/scene gates. |
| P2-PASS-04N | Pass 4N - Bloom Stage Split And Graph Ownership | DONE_VERIFIED | `Renderer_RenderGraphBloom.cpp::Renderer::ExecuteBloomInRenderGraph`, `RendererBloomState.h` | `tools/run_release_validation.ps1` | RT showcase/effects/material smokes pass; current logs show bloom graph intermediates enabled. | No golden bloom shape comparison. |
| P2-PASS-04O | Pass 4O - End-Frame Resource Contract Tightening | DONE_VERIFIED | `Renderer_RenderGraphEndFrame.cpp`, SRC-VB, SRC-CONTRACT | `tools/run_release_validation.ps1` | Release gate passes without frame-contract warnings. | Resource alias edge cases still need focused tests. |
| P2-PASS-05A | Pass 5A - Material Model Foundation | DONE_VERIFIED | SRC-MATERIAL | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs`; release gate | Material Lab passed; RT showcase reports `material_issues=0`, advanced material counts. | None for current foundation. |
| P2-PASS-05B | Pass 5B - Material Validation And RT Material Bridge | DONE_VERIFIED | SRC-MATERIAL, SRC-RT, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase: `rt_parity=True/0`, `material_issues=0`. | More material presets can be added, but bridge is validated. |
| P2-PASS-06A | Pass 6A - RT Scheduler Scaffold And Denoise Contract | DONE_VERIFIED | SRC-RT, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_budget_profile_matrix.ps1 -NoBuild` | RT showcase reports `rt_budget=8gb_balanced`, readiness, raw/history signal. Budget matrix passed. | UI explanation added in Phase 3, but scheduler core is done. |
| P2-PASS-06B | Pass 6B - RT Denoiser And Low-Memory Validation | DONE_VERIFIED | SRC-RT, `assets/shaders/RTDenoise*.hlsl` if present | `tools/run_budget_profile_matrix.ps1 -NoBuild -TemporalRuns 1 -MaxParallel 1` | Latest budget matrix passed 4 GB and 2 GB profiles; RT history signal nonzero in RT showcase. | Denoiser quality tuning remains future work. |
| P2-PASS-07A | Pass 7A - Temporal Manager And RT Reprojection Correctness | DONE_VERIFIED | SRC-TEMPORAL, SRC-RT, `src/Core/Engine.cpp::Engine::Update`, `src/Graphics/Renderer_FrameTemporalConstants.cpp::Renderer::PublishFrameConstants` | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_temporal_camera_cut_validation.ps1 -NoBuild -IsolatedLogs` | Temporal validation passed with nonzero disocclusion/high-motion/object-motion and warnings=0. Camera-cut gate passed with RT histories reset at frame 20 and reseeded by frame 53. | Additional long-duration ghosting analysis remains quality work, not a blocker for this pass. |
| P2-PASS-07B | Pass 7B - Shared Temporal Rejection Mask | DONE_VERIFIED | SRC-TEMPORAL | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | Temporal script checks `temporal_rejection_mask` and stats resources/passes; latest temporal smoke passed. | Additional scenes could improve coverage. |
| P2-PASS-07C | Pass 7C - Motion Vector History Contract | DONE_VERIFIED | SRC-TEMPORAL, `Renderer_RenderGraphMotionVectors.cpp`, `RendererGPUCullingState.h`, `Renderer_FrameTemporalConstants.cpp` | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_temporal_camera_cut_validation.ps1 -NoBuild -IsolatedLogs` | Latest temporal validation reports object motion and visible count with warnings=0. Camera-cut gate verifies RT shadow/reflection/GI history invalidation reporting and recovery. | Add more non-RT/TAA camera-cut coverage only if broader temporal UI/debug goals require it. |
| P2-PASS-07D | Pass 7D - Temporal Mask RenderGraph Ownership | DONE_VERIFIED | SRC-TEMPORAL, `Renderer_RenderGraphTemporalMask.cpp` | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | Temporal script requires `TemporalRejectionMask` pass to be render graph and no fallback; passed. | None for current runtime path. |
| P2-PASS-07E | Pass 7E - Temporal Mask Statistics Contract | DONE_VERIFIED | SRC-TEMPORAL, SRC-CONTRACT | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | Temporal script requires valid temporal stats and ratios; passed. | None for current scene. |
| P2-PASS-07F | Pass 7F - Temporal Motion Validation Scene | DONE_VERIFIED | `src/Core/Engine_Scenes.cpp`, SRC-TEMPORAL | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | Latest temporal validation passed and scene log lists validation objects. | Add more temporal stress scenes for camera cuts, alpha, and high-speed geometry. |
| P2-PASS-08A | Pass 8A - Shared Renderer Budget Planner | DONE_VERIFIED | SRC-BUDGET | `tools/run_budget_profile_matrix.ps1 -NoBuild` | Budget matrix passed under latest release gate. | None for current budget profiles. |
| P2-PASS-08B | Pass 8B - Budget-Applied Render Target Sizing | DONE_VERIFIED | SRC-BUDGET, `Renderer_RenderTargets.cpp`, `Renderer_RTResources.cpp`, `Renderer_SSAO.cpp` | `tools/run_budget_profile_matrix.ps1 -NoBuild` | 4 GB and 2 GB profile runs passed with lower memory values. | Not all possible viewport/resizing combinations are tested. |
| P2-PASS-08C | Pass 8C - Asset Residency Budget Enforcement | DONE_VERIFIED | SRC-BUDGET, `TextureAdmission.cpp`, `TextureSourcePlan.cpp`, `AssetRegistry.h` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_environment_manifest_tests.ps1` | RT showcase reports texture upload receipts and memory budget within limits; environment manifest passed. | Optional/missing asset runtime fallback coverage is partial. |
| P2-PASS-08D | Pass 8D - Isolated Smoke Artifact Runs | DONE_VERIFIED | all smoke scripts using `-LogDir`/`-IsolatedLogs` | `tools/run_release_validation.ps1` | Latest release gate wrote isolated logs under `build/bin/logs/runs/...`. | None. |

## Work Remaining By System From `phase2.md`

| ID | Concrete checkpoint derived from broad Phase 2 text | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| P2-SYS-01 | Renderer orchestration: `Renderer.cpp` thin, explicit frame plan, stable named phases. | DONE_VERIFIED | SRC-RENDER-ORCH, SRC-FEATURE-PLAN | `tools/run_release_validation.ps1` | Build and runtime gate passed through named frame phases. | Renderer class still owns orchestration across many modules; deeper host split remains possible. |
| P2-SYS-02 | Render graph owns pass ordering/resource transitions where migrated. | PARTIAL | SRC-RENDERGRAPH | `tools/run_release_validation.ps1`; historical transient validation | Major passes are graph-owned; release gate passes. | Manual/fallback paths remain. Not every pass/resource is graph-owned. |
| P2-SYS-03 | Passes declare resources rather than reaching into renderer state. | PARTIAL | SRC-RENDERGRAPH, SRC-CONTRACT, SRC-STATE | `tools/run_renderer_ownership_tests.ps1`; `tools/run_renderer_full_ownership_audit.ps1` | Ownership test validates selected boundaries. Full ownership audit passed with 48/48 `Renderer` members in named state/service aggregates and no loose GPU resource/descriptors in `Renderer.h`. | Many pass callbacks still access renderer aggregates directly. This is not full pass-owned resource declaration for every pass. |
| P2-SYS-04 | Renderer scene snapshot is deterministic and drives main consumers. | DONE_VERIFIED | SRC-SNAPSHOT | `tools/run_release_validation.ps1` | RT/VB/shadow/material smokes passed. | Editor/debug fallback paths not exhaustive. |
| P2-SYS-05 | Material system maturity: shared raster/VB/RT model and validation warnings. | DONE_VERIFIED | SRC-MATERIAL | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_rt_showcase_smoke.ps1` | Material Lab passed; RT showcase `material_issues=0`, `rt_parity=True/0`. | More authoring UI/preset depth belongs to Phase 3+ and is partial. |
| P2-SYS-06 | Material gallery passes visual validation. | PARTIAL | SRC-SCENES, SRC-MATERIAL | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` | Material Lab runtime smoke passed. | It is a smoke/luma/material coverage gate, not a full golden gallery review. |
| P2-SYS-07 | RT scheduler, denoising, low-memory behavior, and diagnostics. | DONE_VERIFIED | SRC-RT | `tools/run_release_validation.ps1` | RT showcase plus budget matrix passed with raw/history signal and readiness. | Quality tuning remains future work. |
| P2-SYS-08 | Temporal filters expose invalidation/reprojection and do not hide ghosting. | PARTIAL | SRC-TEMPORAL, SRC-RT | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_temporal_camera_cut_validation.ps1 -NoBuild -IsolatedLogs` | Temporal validation passed with temporal mask stats. Camera-cut gate passed and proves RT histories invalidate/reseed on a large camera jump. | "Never hide popping/create ghost silhouettes" is a visual-quality claim; current validation is necessary but not sufficient. |
| P2-SYS-09 | Explicit camera-cut invalidation coverage. | DONE_VERIFIED | `src/Core/Engine.cpp::Engine::Initialize`, `src/Core/Engine.cpp::Engine::Update`, `src/Graphics/Renderer_FrameTemporalConstants.cpp::Renderer::PublishFrameConstants`, SRC-TEMPORAL | `tools/run_temporal_camera_cut_validation.ps1 -NoBuild -IsolatedLogs` | Targeted run passed: frames=53, cut_frame=20, camera=`reflection_closeup`, `rt_reflection_reset=camera_cut`, `invalidated_frame=20`, RT shadow/reflection/GI histories reseeded and resource-valid. | None for RT history camera-cut coverage. TAA-specific camera-cut policy can be separately tightened if needed. |
| P2-SYS-10 | Visibility buffer correctness for opaque depth, material resolve, deferred lighting, local lights, probes, debug blits. | PARTIAL | SRC-VB, SRC-RENDERGRAPH | `tools/run_rt_showcase_smoke.ps1`; `tools/run_vb_debug_views.ps1 -NoBuild`; `tools/run_reflection_probe_contract_tests.ps1 -NoBuild` | Runtime VB path passes. Targeted VB debug gate passed for depth view 34 and material-albedo view 35 with non-empty image statistics. Local reflection probe gate passed with two RT Showcase probes uploaded, zero skips, a valid probe table, and non-empty probe-weight debug capture. | Broad VB "correctness" is still smoke/contract based rather than exhaustive visual equivalence for every deferred-lighting input. |
| P2-SYS-11 | GPU culling and HZB diagnostics are visible and budgeted. | DONE_VERIFIED | `Renderer_GPUDriven.cpp`, `Renderer_GPUCulling*.cpp`, `Renderer_HZB*.cpp`, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase reports GPU culling and HZB logs; smoke budget passed. | More occlusion-correctness scene tests could be added. |
| P2-SYS-12 | Memory, descriptor, energy budgets, and transient aliasing. | DONE_VERIFIED | SRC-BUDGET, SRC-RENDERGRAPH, `tools/run_lighting_energy_budget_tests.ps1` | `tools/run_rt_showcase_smoke.ps1`; `tools/run_budget_profile_matrix.ps1`; `tools/run_render_graph_transient_matrix.ps1 -NoBuild -IsolatedLogs`; `tools/run_descriptor_memory_stress_scene.ps1 -NoBuild`; `tools/run_lighting_energy_budget_tests.ps1 -NoBuild` | Memory/descriptor budgets passed; render-graph transient matrix validates aliasing on/off and bloom-transient disabled behavior. Descriptor/memory stress passed with `persistent_descriptors=988/1024`, `staging=78/128`, `transient_delta=0`, `dxgi_mb=408.46/512`, `estimated_mb=190.52/256`. Lighting energy budget passed four public scenes with bounded total/max light intensity, exposure, bloom, near-white ratio, and saturation. | None for the current memory/descriptor/transient/lighting-energy gate set; broader feature-specific budgets remain tracked by Phase 3 particle/post rows. |
| P2-SYS-13 | Lighting/atmosphere/visual quality proof scenes: reflective, glass/water, emissive, outdoor/sunset beach. | PARTIAL | SRC-SCENES | `tools/run_release_validation.ps1`; `tools/run_reflection_probe_contract_tests.ps1 -NoBuild` | RT showcase, material lab, glass/water courtyard, effects showcase, and local reflection-probe validation pass. | Outdoor/sunset beach remains deferred by user direction. |
| P2-SYS-14 | Visual validation captures compare luma, saturation, edge stability, temporal stability. | PARTIAL | smoke scripts, `FrameContractJson.cpp`, visual baseline scripts | `tools/run_release_validation.ps1`; `tools/run_visual_probe_validation.ps1 -NoBuild` | Luma/saturation/nonblack/temporal diff checks pass in current smokes. Visual probe passed all four public baseline cases with edge/dominant-color BMP checks. | No full image-diff golden comparison; temporal stability remains metric-smoke based. |
| P2-SYS-15 | Single script runs the Phase 2 validation suite. | DONE_VERIFIED | `tools/run_phase2_validation.ps1`, `tools/run_release_validation.ps1` | `tools/run_phase2_validation.ps1` | Named Phase 2 entrypoint exists and delegates to the current release validation gate so Phase 2 coverage cannot drift behind the broader suite. Latest wrapper run passed with logs under `phase2_validation_20260510_230853_029_26268_faa40115`. | None for the named entrypoint; keep the wrapper thin as release validation evolves. |
| P2-SYS-16 | `rt_showcase` final renderer scene. | DONE_VERIFIED | SRC-SCENES | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase runtime passed with visual validation and RT signal. | Subjective polish can continue. |
| P2-SYS-17 | `material_gallery` / material lab scene. | PARTIAL | SRC-SCENES | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` | Material Lab passed. | Name differs from plan; current scene is `material_lab`, not a fully exhaustive gallery. |
| P2-SYS-18 | `temporal_validation` scene. | DONE_VERIFIED | SRC-SCENES | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | Latest temporal validation passed. | Add camera-cut and more motion edge cases. |
| P2-SYS-19 | `stress_memory` / descriptor stress scene. | DONE_VERIFIED | `tools/run_descriptor_memory_stress_scene.ps1`, SRC-BUDGET, SRC-RT | `tools/run_descriptor_memory_stress_scene.ps1 -NoBuild` | Runtime stress passed with `persistent_descriptors=988/1024`, `staging=78/128`, `transient_budget=81920`, `transient_delta=0`, `dxgi_mb=408.46/512`, `estimated_mb=190.52/256`, `write_mb=107.75/128`, raw/history RT signal nonzero. | None for the historical descriptor/memory stress gate. |
| P2-SYS-20 | `sunset_beach` final visual target scene / eventual outdoor scenes. | DEFERRED_BY_USER_ONLY | none found in current validated path | none | User direction de-scoped infinite/outdoor/world work from main path. | Only revive if user explicitly wants outdoor/world scene work. |

## Ordered Implementation Passes And Later Phase 2 Pass Log

The following rows cover every explicit ordered pass heading found in
`phase2.md` after the initial progress ledger. Rows marked `DONE_VERIFIED` are
covered by current runtime/static validation. Rows marked `DONE_UNVERIFIED`
have source evidence and historical `phase2.md` evidence but no exact current
focused gate in the latest run. Rows marked `PARTIAL` are implemented only as a
foundation or contract rather than the full broad feature.

| ID | Pass/milestone heading | Status | Source/functions | Validation command | Evidence / remaining work |
|---|---|---|---|---|---|
| P2-ORDER-01 | Pass 1: Finish Renderer Split | DONE_VERIFIED | SRC-RENDER-ORCH, SRC-STATE | `tools/run_release_validation.ps1` | Current build/release gate passed. |
| P2-ORDER-02 | Pass 2: Frame Feature Plan And Render Context | DONE_VERIFIED | SRC-FEATURE-PLAN | `tools/run_release_validation.ps1` | Current smokes export frame contracts. |
| P2-ORDER-03 | Pass 3: Renderer Scene Snapshot | DONE_VERIFIED | SRC-SNAPSHOT | `tools/run_release_validation.ps1` | Snapshot consumers active in RT/VB/shadow/draw paths. |
| P2-ORDER-04 | Pass 4: Render Graph Migration | PARTIAL | SRC-RENDERGRAPH | `tools/run_release_validation.ps1` | Many passes graph-owned; not every manual/fallback path is eliminated. |
| P2-ORDER-05 | Pass 5: Material Model Unification | DONE_VERIFIED | SRC-MATERIAL | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` | Runtime material coverage passed. |
| P2-ORDER-06 | Pass 6: RT Scheduler And Denoising | DONE_VERIFIED | SRC-RT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT readiness and raw/history signal passed. |
| P2-ORDER-07 | Pass 7: Temporal Manager | PARTIAL | SRC-TEMPORAL | `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_temporal_camera_cut_validation.ps1 -NoBuild -IsolatedLogs` | Core manager/mask/stats done; camera-cut RT history invalidation coverage passed. | Broader visual ghosting claims remain quality-sensitive and not fully golden-tested. |
| P2-ORDER-08 | Pass 8: Budget Profiles | DONE_VERIFIED | SRC-BUDGET | `tools/run_budget_profile_matrix.ps1 -NoBuild`; `tools/run_descriptor_memory_stress_scene.ps1 -NoBuild`; `tools/run_render_graph_transient_matrix.ps1 -NoBuild -IsolatedLogs`; `tools/run_lighting_energy_budget_tests.ps1 -NoBuild` | Budget matrix, descriptor/memory stress, render-graph transient matrix, and lighting energy budget pass in the current release gate. | None for current budget profiles; no-RT physical hardware coverage remains tracked under Phase 3 fallback/no-RT rows. |
| P2-ORDER-09 | Pass 9: Visual Demo Maturity | PARTIAL | SRC-SCENES, SRC-DOCS | `tools/run_release_validation.ps1` | Public scenes pass, but visual maturity is subjective and outdoor/sunset target is missing/deferred. |
| P2-08AK | Pass 8AK - Visibility Buffer Graph Module | DONE_VERIFIED | SRC-RENDERGRAPH, SRC-VB | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | VB graph path active in RT showcase. |
| P2-08AL | Pass 8AL - Visibility Buffer Legacy Graph Boundary | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBuffer.cpp`, `Passes/VisibilityBufferGraphPass.cpp`, SRC-RENDERGRAPH, SRC-VB | `tools/run_render_graph_boundary_contract_tests.ps1`; `tools/run_render_graph_transient_matrix.ps1`; release gate | Boundary contract passed: `LegacyPathContext`, `AddLegacyPath`, `VisibilityBufferPath`, `fallbackExecutions`, and render-graph execution-stat accumulation are present. Runtime transient matrix also passes with render-graph fallback executions required to be 0. | None for current static/runtime legacy-boundary contract; full removal of legacy path remains tracked separately. |
| P2-08AM | Pass 8AM - Render Graph Validation Module | DONE_VERIFIED | `Renderer_RenderGraphDiagnostics.cpp`, `Passes/RenderGraphValidationPass.cpp`, `tools/run_render_graph_transient_matrix.ps1` | `tools/run_render_graph_boundary_contract_tests.ps1`; `tools/run_render_graph_transient_matrix.ps1`; release gate | Boundary contract passed: `CORTEX_RG_TRANSIENT_VALIDATE`, heap dump, aliasing disable, bloom-transient disable env paths, `RenderGraphValidationPass::AddTransientValidation`, and fallback accounting are all present. Runtime matrix validates aliasing-on, aliasing-off, and bloom-transients-off modes. | None for the current transient-validation module and matrix coverage. |
| P2-09A | Pass 9A - Frame Feature Plan Contract | DONE_VERIFIED | SRC-FEATURE-PLAN, SRC-CONTRACT | `tools/run_release_validation.ps1` | Current frame reports validated by smokes. |
| P2-09B | Pass 9B - Frame Execution Context Setup | DONE_VERIFIED | SRC-FEATURE-PLAN, SRC-RENDER-ORCH | `tools/run_release_validation.ps1` | Frame phases execute through `FrameExecutionContext`. |
| P2-09C | Pass 9C - Pre-Frame Services Extraction | DONE_VERIFIED | SRC-RENDER-ORCH, SRC-STATE | `tools/run_release_validation.ps1` | Release build and smokes pass. |
| P2-09D | Pass 9D - Begin-Frame Execution Boundary | DONE_VERIFIED | `Renderer_FrameBegin.cpp::Renderer::BeginFrame`, SRC-RENDER-ORCH | `tools/run_release_validation.ps1` | Runtime passes through begin-frame path. |
| P2-09E | Pass 9E - Special Frame Path Extraction | DONE_VERIFIED | `Renderer_EditorHooks.cpp`, `Renderer.h`, `EngineEditorMode.cpp`, `tools/run_editor_frame_contract_tests.ps1` | `tools/run_editor_frame_contract_tests.ps1`; release gate | Targeted contract passed: editor hook declarations exist, `Renderer_EditorHooks.cpp` delegates to the explicit renderer frame/pass functions, and `EngineEditorMode` uses the extracted frame sequence from begin/update/prewarm/main-pass through post/debug/end. | None for current static editor-frame boundary; interactive editor rendering remains outside public release smokes. |
| P2-09F | Pass 9F - Ray-Tracing Frame Phase Extraction | DONE_VERIFIED | `Renderer_FramePhases_Prepare.cpp::Renderer::ExecuteRayTracingFramePhase` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT showcase passes. |
| P2-09G | Pass 9G - Shadow Frame Phase Extraction | DONE_VERIFIED | `Renderer_FramePhases_Prepare.cpp::Renderer::ExecuteShadowFramePhase` | `tools/run_release_validation.ps1` | Shadows enabled in runtime scenes; no warnings. |
| P2-09H | Pass 9H - Main Scene Setup Extraction | DONE_VERIFIED | `Renderer_FramePhases_Main.cpp::Renderer::BeginMainSceneFramePhase` | `tools/run_release_validation.ps1` | Runtime smokes pass main setup. |
| P2-09I | Pass 9I - Geometry Frame Phase Extraction | DONE_VERIFIED | `Renderer_FramePhases_Main.cpp::Renderer::ExecuteGeometryFramePhase` | `tools/run_release_validation.ps1` | RT/material/glass scenes pass geometry path. |
| P2-09J | Pass 9J - Main Scene Effects Phase Extraction | DONE_VERIFIED | `Renderer_FramePhases_Main.cpp::Renderer::ExecuteMainSceneEffectsFramePhase` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT reflections/temporal/HZB/TAA path passes. |
| P2-09K | Pass 9K - Post-Processing Frame Phase Extraction | DONE_VERIFIED | `Renderer_FramePhases_Post.cpp::Renderer::ExecutePostProcessingFramePhase` | `tools/run_release_validation.ps1` | Bloom/post/particles/effects paths pass. |
| P2-09L | Pass 9L - Frame Completion Phase Extraction | DONE_VERIFIED | `Renderer_FrameEnd.cpp`, SRC-CONTRACT | `tools/run_release_validation.ps1` | Frame report export and latest logs exist. |
| P2-09M | Pass 9M - Frame Phase Module Split | DONE_VERIFIED | SRC-RENDER-ORCH | `tools/run_release_validation.ps1` | Split frame phase files compile and run. |
| P2-09N | Pass 9N - HZB Renderer Boundary | DONE_VERIFIED | `Renderer_HZB*.cpp`, SRC-RENDERGRAPH | `tools/run_release_validation.ps1` | Runtime logs show HZB enabled and graph passes. |
| P2-09O | Pass 9O - RT Resource Boundary | DONE_VERIFIED | `RendererRTState.h`, `Renderer_RTResources.cpp`, SRC-RT | `tools/run_renderer_ownership_tests.ps1`; RT smoke | Ownership and RT smoke pass. |
| P2-09P | Pass 9P - Shadow Resource Boundary | DONE_VERIFIED | `RendererShadowState.h`, `Renderer_ShadowResources.cpp` | `tools/run_release_validation.ps1` | Runtime shadow path passes. |
| P2-09Q | Pass 9Q - Render Graph Diagnostics And Depth/Shadow Boundaries | DONE_VERIFIED | SRC-RENDERGRAPH, SRC-CONTRACT | `tools/run_release_validation.ps1` | Frame contracts pass with warnings=0. |
| P2-09R | Pass 9R - Temporal And Screen-Space Render Graph Boundary | DONE_VERIFIED | SRC-TEMPORAL, `Renderer_RenderGraphTAA.cpp`, `Renderer_RenderGraphSSAO.cpp`, `Renderer_RenderGraphSSR.cpp` | `tools/run_release_validation.ps1` | Temporal and RT showcase smokes pass. |
| P2-09S | Pass 9S - Visibility Buffer Render Graph Boundary | DONE_VERIFIED | SRC-VB, SRC-RENDERGRAPH | `tools/run_release_validation.ps1` | VB path active and validated. |
| P2-09T | Pass 9T - Standalone Bloom Render Graph Boundary | DONE_VERIFIED | `Renderer_RenderGraphBloom.cpp`, `RendererBloomState.h` | `tools/run_release_validation.ps1` | Bloom path active and runtime smokes pass. |
| P2-09U | Pass 9U - End-Frame Render Graph Owner | DONE_VERIFIED | `Renderer_RenderGraphEndFrame.cpp` | `tools/run_release_validation.ps1` | End-frame path active in smokes. |
| P2-09V | Pass 9V - GPU-Driven Renderer Ownership Boundary | DONE_VERIFIED | `Renderer_GPUDriven.cpp`, `RendererGPUCullingState.h` | `tools/run_release_validation.ps1` | GPU culling enabled in logs and smokes pass. |
| P2-09W | Pass 9W - Visibility Buffer Pipeline Ownership Boundary | DONE_VERIFIED | SRC-VB | `tools/run_release_validation.ps1` | VB pipelines initialize and runtime smokes pass. |
| P2-09X | Pass 9X - Visibility Buffer Resource Ownership Boundary | DONE_VERIFIED | SRC-VB | `tools/run_release_validation.ps1` | VB resources initialize and runtime smokes pass. |
| P2-09Y | Pass 9Y - Visibility Buffer Upload Ownership Boundary | DONE_VERIFIED | SRC-VB | `tools/run_release_validation.ps1` | VB instance/material buffers initialize; smokes pass. |
| P2-09Z | Pass 9Z - Visibility Buffer Runtime Stage Boundaries | DONE_VERIFIED | SRC-VB | `tools/run_release_validation.ps1` | Stage files compile/run; VB smoke path passes. |
| P2-10A | Pass 10A - GPU Culling Ownership Boundaries | DONE_VERIFIED | `RendererGPUCullingState.h`, `Renderer_GPUCulling*.cpp` | `tools/run_release_validation.ps1` | GPU culling visible in diagnostics. |
| P2-10B | Pass 10B - Debug Primitive System Boundaries | DONE_VERIFIED | `RendererDebugLineState.h`, `Renderer_DebugLines.cpp`, `Renderer.h`, `tools/run_debug_primitive_contract_tests.ps1` | `tools/run_debug_primitive_contract_tests.ps1`; release gate | Targeted contract passed: debug-line storage/GPU resource state live in `RendererDebugLineState`, `Renderer` exposes only debug-line methods and state aggregate members, and debug draw counts are written to the frame contract. | None for current debug primitive ownership contract; runtime visual debug-line rendering is not a public-scene gate. |
| P2-10C | Pass 10C - Asset Upload Boundaries | DONE_VERIFIED | `RendererAssetRuntimeState.h`, `Renderer_TextureUploads.cpp`, SRC-BUDGET | `tools/run_rt_showcase_smoke.ps1` | Texture upload receipts reported; pending=0. |
| P2-10D | Pass 10D - Source List Stub Cleanup | DONE_VERIFIED | `CMakeLists.txt`, `tools/run_source_list_contract_tests.ps1` | `tools/run_source_list_contract_tests.ps1`; release gate | Targeted source-list contract passed: CMake source entries exist, duplicate count is 0, every current `src/Graphics/Renderer*.cpp` split file is covered, and temporary/backup source entries count is 0. | None for current explicit source-list hygiene; future split files must remain listed. |
| P2-10E | Pass 10E - Frame Setup Boundaries | DONE_VERIFIED | SRC-RENDER-ORCH, SRC-FEATURE-PLAN | `tools/run_release_validation.ps1` | Runtime frame setup passes. |
| P2-10F | Pass 10F - Runtime Settings Boundaries | DONE_VERIFIED | SRC-UI-P3, `Renderer_FeatureSettings.cpp`, `Renderer_QualitySettings.cpp` | `tools/run_graphics_ui_contract_tests.ps1`; release gate | Contract and smokes pass. |
| P2-10G | Pass 10G - Frame Lifecycle Boundaries | DONE_VERIFIED | `RendererFrameLifecycleState.h`, `Renderer_FrameBegin.cpp`, `Renderer_FrameEnd.cpp` | `tools/run_release_validation.ps1` | Runtime lifecycle passes. |
| P2-10H | Pass 10H - Frame Contract Boundaries | DONE_VERIFIED | SRC-CONTRACT | `tools/run_release_validation.ps1` | Frame reports consumed by smokes. |
| P2-10I | Pass 10I - Render Target Resource Boundaries | DONE_VERIFIED | `RendererMainTargetState.h`, `RendererDepthState.h`, `Renderer_RenderTargets.cpp` | `tools/run_release_validation.ps1` | Render target budgets pass. |
| P2-10J | Pass 10J - Screen-Space Pass Boundaries | DONE_VERIFIED | `RendererSSAOState.h`, `RendererSSRState.h`, `RendererTemporalScreenState.h` | `tools/run_release_validation.ps1` | SSR/SSAO/TAA runtime path passes. |
| P2-10K | Pass 10K - Late Geometry Boundaries | DONE_VERIFIED | `Renderer_WaterSurfaces.cpp`, `Renderer_TransparentGeometry.cpp`, `Renderer_Particles.cpp` | `tools/run_release_validation.ps1` | Glass/water/effects smokes pass. |
| P2-10L | Pass 10L - Visibility Buffer Path Boundaries | DONE_VERIFIED | SRC-VB | `tools/run_release_validation.ps1` | VB path passes. |
| P2-10M | Pass 10M - Ray-Tracing Runtime Boundaries | DONE_VERIFIED | SRC-RT, `RendererRTState.h` | `tools/run_rt_showcase_smoke.ps1` | RT readiness and signal pass. |
| P2-10N | Pass 10N - Visibility Buffer Pipeline Boundaries | DONE_VERIFIED | SRC-VB | `tools/run_release_validation.ps1` | VB pipelines created successfully in runtime logs. |
| P2-10O | Pass 10O - Renderer Pipeline Setup Boundaries | DONE_VERIFIED | `RendererPipelineState.h`, `Renderer_*PipelineSetup.cpp` | `tools/run_release_validation.ps1` | Runtime logs show pipelines created. |
| P2-10P | Pass 10P - Render Graph Internal Boundaries | PARTIAL | SRC-RENDERGRAPH | `tools/run_release_validation.ps1` | Runtime passes; not every internal graph mode is exhaustively tested. |
| P2-10Q | Pass 10Q - Visibility Buffer PSO Internals | DONE_VERIFIED | SRC-VB, `VisibilityBuffer_*Pipeline*.cpp` | `tools/run_release_validation.ps1` | VB pipeline creation logged and smokes pass. |
| P2-10R | Pass 10R - Temporal Screen Render Graph Boundaries | DONE_VERIFIED | `RendererTemporalScreenState.h`, SRC-TEMPORAL | `tools/run_release_validation.ps1` | TAA/temporal paths pass. |
| P2-10S | Pass 10S - Visibility Buffer Graph And Material-Key Helpers | DONE_VERIFIED | `Renderer_RenderGraphVisibilityBufferHelpers.h`, `Renderer_VisibilityBufferMaterialKey.h` | `tools/run_release_validation.ps1` | VB/material smokes pass. |
| P2-10T | Pass 10T - Frame Constant Phase Boundaries | DONE_VERIFIED | `RendererFrameConstantsState.h`, `Renderer_FramePostConstants.cpp`, `Renderer_FrameConstants*.cpp` | `tools/run_release_validation.ps1` | Runtime smokes pass. |
| P2-10U | Pass 10U - DXR RHI Ownership Boundaries | DONE_VERIFIED | `src/Graphics/RHI/DX12Raytracing*.cpp/h` | `tools/run_rt_showcase_smoke.ps1` | DXR pipelines initialize and RT smoke passes. |
| P2-10V | Pass 10V - Renderer Header Support Boundary | DONE_VERIFIED | `src/Graphics/Renderer.h`, SRC-STATE | `tools/run_renderer_ownership_tests.ps1` | Ownership test checks state members and files. |
| P2-10W | Pass 10W - Renderer Diagnostics Type Boundary | DONE_VERIFIED | `Renderer_DiagnosticsTypes.h`, `Renderer_Diagnostics.cpp` | `tools/run_release_validation.ps1` | Runtime diagnostics dumped and frame report exported. |
| P2-10X | Pass 10X - Texture RHI Ownership Boundaries | DONE_VERIFIED | `DX12Texture*`, texture upload/admission files | `tools/run_rt_showcase_smoke.ps1` | Texture receipts and uploads pass. |
| P2-10Y | Pass 10Y - Texture Residency Receipts | DONE_VERIFIED | `TextureAdmission.cpp`, `TextureSourcePlan.cpp`, `Renderer_TextureUploads.cpp` | `tools/run_rt_showcase_smoke.ps1` | Texture receipts logged with resident sizes/budget. |
| P2-10Z | Pass 10Z - Texture Admission Policy Module | DONE_VERIFIED | `TextureAdmission.cpp::PlanTextureAdmission`, `TextureSourcePlan.cpp` | `tools/run_rt_showcase_smoke.ps1` | Upload/admission path passes under budgets. |
| P2-11A | Pass 11A - Texture Publication Boundary | DONE_VERIFIED | texture upload/publication files | `tools/run_rt_showcase_smoke.ps1` | Textures publish and bindless handles used. |
| P2-11B | Pass 11B - Texture Upload Ticket Contract | DONE_VERIFIED | texture upload state/files | `tools/run_rt_showcase_smoke.ps1` | Receipts show submitted/completed/failed/pending. |
| P2-11C | Pass 11C - Texture Source Planning | DONE_VERIFIED | `TextureSourcePlan.cpp` | `tools/run_rt_showcase_smoke.ps1` | DDS textures load with planned residency. |
| P2-11D | Pass 11D - Texture Upload Execution Boundary | DONE_VERIFIED | texture upload execution files | `tools/run_rt_showcase_smoke.ps1` | Upload queue completes with pending=0. |
| P2-11E | Pass 11E - Observable Texture Upload Queue Contract | DONE_VERIFIED | `RendererTextureUploadState.h`, diagnostics | `tools/run_rt_showcase_smoke.ps1` | Texture upload diagnostics in latest RT log. |
| P2-11F | Pass 11F - Deferred Material Texture Queue | DONE_VERIFIED | `Renderer_Materials.cpp`, texture queue files | `tools/run_rt_showcase_smoke.ps1` | Deferred RT showcase textures load and smoke passes. |
| P2-11G | Pass 11G - Structured Texture Queue Diagnostics | DONE_VERIFIED | diagnostics/texture upload files | `tools/run_rt_showcase_smoke.ps1` | Structured receipts visible in logs. |
| P2-11H | Pass 11H - Texture Queue State Ownership | DONE_VERIFIED | `RendererTextureUploadState.h` | `tools/run_release_validation.ps1` | Build/runtime pass. |
| P2-11I | Pass 11I - GPU Job Queue State Ownership | DONE_VERIFIED | `RendererAssetRuntimeState.h` | `tools/run_release_validation.ps1` | Build/runtime pass. |
| P2-11J | Pass 11J - Renderer Header Behavior Extraction | DONE_VERIFIED | `Renderer.h` plus extracted implementation files | `tools/run_release_validation.ps1` | Build passes and header compiles. |
| P2-11K | Pass 11K - Renderer Accessor Implementation Split | DONE_VERIFIED | `Renderer_Accessors*.cpp` if present, `Renderer.h` | `tools/run_release_validation.ps1` | Build passes. |
| P2-11L | Pass 11L - Renderer Subsystem Accessor Extraction | DONE_VERIFIED | accessor/subsystem files, SRC-STATE | `tools/run_release_validation.ps1` | Build passes. |
| P2-11M | Pass 11M - Renderer State Snapshots | DONE_VERIFIED | renderer state snapshot/control files | `tools/run_release_validation.ps1` | Build/runtime pass. |
| P2-11N | Pass 11N - Renderer Command Applier | DONE_VERIFIED | `RendererControlApplier*.cpp` | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke` | Preset/control path passed. |
| P2-11O | Pass 11O - Renderer Control Applier | DONE_VERIFIED | `RendererControlApplier_Runtime.cpp`, `RendererControlApplier_ScenePresets.cpp` | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke` | Preset runtime smoke passed. |
| P2-11P | Pass 11P - Smoke-Covered Scene Preset Controls | DONE_VERIFIED | SRC-SCENES, control appliers | `tools/run_release_validation.ps1` | Showcase/material/glass/effects smokes pass. |
| P2-11Q | Pass 11Q - Remaining Scene and Editor Control Extraction | PARTIAL | `src/UI/SceneEditorWindow.cpp`, SRC-UI-P3 | `tools/run_material_editor_contract_tests.ps1` | Contract passes, but editor control behavior is mostly static-tested. |
| P2-11R | Pass 11R - Interactive UI Renderer Controls | PARTIAL | SRC-UI-P3 | `tools/run_graphics_ui_contract_tests.ps1` | Static UI contract passes; no interactive browser/UI automation was run. |
| P2-11S | Pass 11S - Core Runtime Renderer Controls | DONE_VERIFIED | `src/Core/Engine.cpp`, `Engine_Input.cpp`, `RendererTuningState.cpp` | `tools/run_graphics_settings_persistence_tests.ps1 -NoBuild` | Persistence/control tests passed. |
| P2-11T | Pass 11T - Keyboard Debug Renderer Controls | DONE_VERIFIED | `src/Core/Engine_Input.cpp`, `src/Core/Engine_UI.cpp` | `tools/run_hud_mode_contract_tests.ps1 -NoBuild` | HUD/F7/CLI contract passed. |
| P2-11U | Pass 11U - Renderer Control Facade Split | DONE_VERIFIED | `RendererControlApplier*.cpp`, `RendererTuningState.cpp` | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke` | Preset/control path passed. |
| P2-11V | Pass 11V - Core Debug and Backend Controls | DONE_VERIFIED | `Engine_Input.cpp`, `Renderer_Voxel.cpp` | `tools/run_voxel_backend_smoke.ps1 -NoBuild -IsolatedLogs` | Voxel backend smoke passed. |
| P2-11W | Pass 11W - LLM Renderer Command Control Routing | DONE_VERIFIED | `src/Core/Engine.cpp::Update`, `src/main.cpp`, `LLM::CommandParser::ParseJSON`, `LLM::CommandQueue::ExecuteModifyRenderer`, `LLM::ApplyModifyRendererCommand`, renderer control appliers | `tools/run_llm_renderer_command_smoke.ps1`; `tools/run_release_validation.ps1` | Targeted smoke passed: startup Architect command queued, `modify_renderer` applied `exposure=1.35`, `shadows=off`, fog, water amplitude, and frame contract state. | None for current deterministic routing gate. |
| P2-11X | Pass 11X - Lighting-Rig Command Boundary | DONE_VERIFIED | `RendererLightingRigControl.cpp`, `LLM::ApplyModifyRendererCommand`, SRC-SCENES | `tools/run_llm_renderer_command_smoke.ps1`; `tools/run_release_validation.ps1` | LLM command route applies `lighting_rig=studio_three_point`; frame contract reports `rig_id=studio_three_point` and `rig_source=renderer_rig`. Scene lighting rigs remain validated by visual/showcase gates. | None for current command boundary. |
| P2-11Y | Pass 11Y - Shared Material Preset Resolution | DONE_VERIFIED | SRC-MATERIAL | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` | Material lab passed; material editor contract reuses registry. |
| P2-11Z | Pass 11Z - Resolved Material Frame Contract | DONE_VERIFIED | SRC-MATERIAL, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1` | RT showcase material counts and issues pass. |
| P2-11AA | Pass 11AA - Resolved Surface Classification | DONE_VERIFIED | `SurfaceClassification.h`, SRC-MATERIAL | `tools/run_material_lab_smoke.ps1` | Material Lab checks surface coverage. |
| P2-11AB | Pass 11AB - Forward Conductor Energy Correction | DONE_VERIFIED | `assets/shaders/Basic.hlsl`, `assets/shaders/DeferredLighting.hlsl`, `assets/shaders/PBR_Lighting.hlsli`, `assets/shaders/MaterialResolve.hlsl` | `tools/run_conductor_energy_contract_tests.ps1`; `tools/run_release_validation.ps1` | Targeted gate passed: forward/deferred shader energy-split invariants present, forced metallic clamp absent, Material Lab reports `resolved_conductor=4`, `reflection_conductor=4`, `max_metallic=1.0`, `very_bright_albedo=0`, and bounded saturated/near-white ratios. | None for the current conductor energy contract. |
| P2-11AC | Pass 11AC - Material-Aware Reflection Policy Diagnostics | DONE_VERIFIED | SRC-MATERIAL, SRC-RT, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1` | RT showcase reflection/material diagnostics pass. |
| P2-11AD | Pass 11AD - RT Material Surface Parity Contract | DONE_VERIFIED | SRC-RT, SRC-MATERIAL | `tools/run_rt_showcase_smoke.ps1` | `rt_parity=True/0`. |
| P2-11AE | Pass 11AE - RT Reflection Dispatch Readiness Contract | DONE_VERIFIED | `Renderer_RTReflections.cpp`, `RendererRTState.h`, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1` | `rt_refl_ready=True/ready`. |
| P2-11AF | Pass 11AF - RT Reflection Signal Quality Contract | DONE_VERIFIED | `Renderer_RTReflectionSignalStats.cpp`, SRC-RT | `tools/run_rt_showcase_smoke.ps1` | `rt_signal=0.0225/0.1424/10.3398/0.0084`. |
| P2-11AG | Pass 11AG - Denoised Reflection History Signal Contract | DONE_VERIFIED | `Renderer_RTReflectionSignalStats.cpp::CaptureRTReflectionHistorySignalStats`, SRC-RT | `tools/run_rt_showcase_smoke.ps1` | `rt_hist=0.0314/0.1433/7.3008/0.0089`. |
| P2-11AH | Pass 11AH - RT Denoise and Signal State Ownership | DONE_VERIFIED | `RendererRTState.h`, `RTReflectionSignalStats.h` | `tools/run_renderer_ownership_tests.ps1`; RT smoke | Ownership test passed; RT history signal passed. |
| P2-11AI | Pass 11AI - RT Target Resource Ownership | DONE_VERIFIED | `RendererRTState.h`, `Renderer_RTResources.cpp` | `tools/run_renderer_ownership_tests.ps1`; RT smoke | Ownership and runtime pass. |
| P2-11AJ | Pass 11AJ - RT Reflection Readiness State Ownership | DONE_VERIFIED | `RendererRTState.h`, `Renderer_RTReflections.cpp` | `tools/run_rt_showcase_smoke.ps1` | Readiness passes. |
| P2-11AK | Pass 11AK - RT Reflection Outlier Composition Guard | PARTIAL | `Renderer_RTReflections.cpp`, RT shaders | `tools/run_rt_showcase_smoke.ps1` | Visual bright/saturation gates pass. | Need focused outlier/clamp test with known overbright reflection. |
| P2-11AL | Pass 11AL - RT Reflection Luma-Aware History Blend | PARTIAL | RT denoise/reflection shaders, SRC-RT | `tools/run_rt_showcase_smoke.ps1` | Raw/history luma delta reported and gates pass. | Need quality tuning/scene-specific validation. |
| P2-11AM through P2-11AZ | State header extractions: RT, temporal mask, HZB, SSAO, SSR, bloom, temporal screen, shadow map, depth, main target, debug line, material fallback texture, environment/IBL, particle, vegetation, upload command pool, texture upload queue, voxel. | DONE_VERIFIED | SRC-STATE, relevant `Renderer*State.h` files | `tools/run_renderer_ownership_tests.ps1`; `tools/run_release_validation.ps1` | Build/release and selected ownership gates pass; state headers exist and are included by `Renderer.h`. | Ownership test is selective; add exhaustive state-member audit if needed. |
| P2-11BA | Pass 11BA - Vegetation Runtime State Header Extraction | DONE_VERIFIED | `RendererVegetationState.h`, `Renderer_Vegetation.cpp`, `FrameContract.h`, `FrameContractJson.cpp`, `Renderer_FrameContractSnapshot.cpp` | `tools/run_vegetation_state_contract_tests.ps1`; `tools/run_release_validation.ps1` | Targeted gate passed: vegetation state bundle exists, full ownership audit accounts for `m_vegetationState`, frame contract serializes `vegetation`, temporal validation reports zero vegetation instances and dormant pipelines. | None for extracted state ownership/reporting. |
| P2-11BB | Pass 11BB - Upload Command Pool State Header Extraction | DONE_VERIFIED | `RendererUploadState.h`, `RendererCommandResourceState.h` | `tools/run_release_validation.ps1` | Build/runtime pass. |
| P2-11BC | Pass 11BC - Texture Upload Queue State Header Extraction | DONE_VERIFIED | `RendererTextureUploadState.h` | `tools/run_rt_showcase_smoke.ps1` | Texture upload diagnostics pass. |
| P2-11BD | Pass 11BD - Voxel Runtime State Header Extraction | DONE_VERIFIED | `RendererVoxelState.h`, `Renderer_Voxel.cpp` | `tools/run_voxel_backend_smoke.ps1 -NoBuild -IsolatedLogs` | Voxel backend smoke passed. |
| P2-11BE | Pass 11BE - Voxel Backend CLI Smoke Gate | DONE_VERIFIED | `Renderer_Voxel.cpp`, `src/Core/main.cpp` backend CLI parsing | `tools/run_voxel_backend_smoke.ps1 -NoBuild -IsolatedLogs` | Latest release gate voxel backend passed: `gpu_ms=15.371 visible=7 avg_luma=116.9 nonblack=1`. |
| P2-11BF | Pass 11BF - Vegetation Runtime State Completion | DEFERRED_BY_USER_ONLY | `RendererVegetationState.h`, `Renderer_Vegetation.cpp`, vegetation shaders/types | `tools/run_vegetation_state_contract_tests.ps1` | User direction de-scoped infinite/outdoor/world systems from the active renderer path. The gate now verifies the state boundary and explicitly detects the current dormant public draw path; mesh/billboard/grass/shadow vegetation rendering remains TODO-marked. | Reopen only if outdoor/world vegetation becomes a current public renderer requirement. |
| P2-11BG through P2-11CU | Renderer runtime/state boundaries: frame runtime/timing, pipeline, pipeline readiness, render graph transition/runtime, debug overlay, water, fractal surface, post grade, SSAO, bloom, fog, lighting, shadow, SSR, PCSS, post feature, debug view, RT runtime, TAA, GPU culling, camera frame, local shadow, VB frame, quality, shadow cascades, frame lifecycle, GPU culling entity history, frame runtime submission, VB enable, frame contract, constant buffer, breadcrumb, command resources, asset runtime, frame constants CPU, diagnostics, planning, temporal history, services, host services. | DONE_VERIFIED | SRC-STATE plus relevant files named by state headers and SRC-RENDER-ORCH/SRC-CONTRACT | `tools/run_release_validation.ps1`; `tools/run_renderer_ownership_tests.ps1`; `tools/run_renderer_full_ownership_audit.ps1` | Latest release gate passed; `Renderer.h` has state/service aggregates, renderer ownership test passed, and full ownership audit covered all 48 renderer members. | The broad state-boundary family is validated by build/runtime smoke plus static member audit, not by one focused runtime test per state. |
| P2-11CV | Pass 11CV - Release Validation Gate | DONE_VERIFIED | `tools/run_release_validation.ps1`, `tools/README.md` | `tools/run_release_validation.ps1` | Latest full release validation passed. |
| P2-11CW | Pass 11CW - Public Renderer README Refresh | DONE_VERIFIED | SRC-DOCS | documentation inspection plus latest commit `5e7b69f` | README now names Phase 3 gate and current renderer capabilities. | None for docs currentness; keep updated as scope changes. |
| P2-11CX | Pass 11CX - Dreamer Startup Release Polish | DONE_VERIFIED | `src/Core/Engine.cpp`, `DreamerService.cpp`, `DiffusionEngine.cpp`, startup Architect command path | `tools/run_dreamer_positive_runtime_tests.ps1`; `tools/run_release_validation.ps1` | Positive runtime gate passed: Dreamer enabled, service initialized, backend/fallback reported, startup `generate_texture` routed directly to Dreamer, generated texture uploaded as `Dreamer_TemporalLab_RotatingRedSphere`, and applied to the target entity. | TensorRT engine execution is hardware/build dependent; current positive gate covers the supported CPU procedural fallback path. |
| P2-11CY | Pass 11CY - Public Repository Hygiene | DONE_VERIFIED | `.gitignore`, `tools/run_repo_hygiene_tests.ps1` | `tools/run_repo_hygiene_tests.ps1`; release gate | Targeted hygiene gate passed: `git diff --check` passed, generated build/cache artifacts tracked count is 0, and `.gitignore` contains build/log/model guards. | None for current whitespace/generated-artifact hygiene contract; packaging review remains tracked separately. |
| P2-11CZ | Pass 11CZ - Release Readiness Note | DONE_VERIFIED | `CortexEngine/RELEASE_READINESS.md`, README link | documentation inspection plus latest commit `5e7b69f` | Release readiness note points at latest run and lists current gate. | It now includes Phase 3 claims; keep this ledger separate from completion claims. |
| P2-11DA | Pass 11DA - RT Showcase Lighting Polish | DONE_VERIFIED | `src/Core/Engine_Scenes.cpp`, `RendererControlApplier_ScenePresets.cpp`, `RELEASE_READINESS.md` | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` via release gate | Latest RT showcase passes visual stats: `luma=68.99 center_luma=70.19 dark=0.677/0.68 sat=0.004/0.12`. | Subjective presentation polish can continue; no objective blocker. |

## Release-Readiness Claims From README And RELEASE_READINESS

| ID | Claim | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| REL-01 | Hybrid raster + ray tracing rendering on DX12. | DONE_VERIFIED | SRC-RT, SRC-RENDER-ORCH | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | DXR initialized; RT showcase passed with RT reflections ready and signal. | Hardware-dependent; fallback behavior on non-DXR GPUs should remain tested separately. |
| REL-02 | Physically based material and surface classification. | DONE_VERIFIED | SRC-MATERIAL | `tools/run_material_lab_smoke.ps1 -NoBuild -IsolatedLogs` | Material Lab and RT showcase material checks passed. | More material coverage can improve confidence. |
| REL-03 | Frame/resource contracts and repeatable smoke validation. | DONE_VERIFIED | SRC-CONTRACT, tools scripts | `tools/run_release_validation.ps1` | Latest release gate passed and wrote frame reports/logs. | None for current suite. |
| REL-04 | Visibility-buffer rendering, GPU culling, TAA, SSAO, SSR, bloom, and IBL. | DONE_VERIFIED | SRC-VB, SRC-RENDERGRAPH, SRC-BUDGET, SRC-ENV-P3 | `tools/run_release_validation.ps1` | Runtime logs show VB/GPU culling/IBL; release gate passed. | Add focused visual comparisons for each effect if public quality bar rises. |
| REL-05 | RT shadows/reflections/GI targets, denoising, and signal diagnostics. | PARTIAL | SRC-RT | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs` | RT reflections and history signal are validated; RT shadow/GI contracts are present. | GI visual correctness is not deeply validated by current public metrics. |
| REL-06 | Phase 3 public showcase scenes, graphics presets, HUD modes, and renderer UI controls. | PARTIAL | SRC-UI-P3, SRC-SCENES | `tools/run_release_validation.ps1` | Contracts/smokes pass. | UI tests are mostly static/contract, not interactive end-to-end. |
| REL-07 | Environment/IBL manifests with procedural fallback behavior. | DONE_VERIFIED | SRC-ENV-P3 | `tools/run_environment_manifest_tests.ps1`; `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest policy passed, uncapped IBL gallery passed all enabled runtime IBLs, and fallback matrix passed missing selected environment, missing manifest, missing required asset, and missing optional asset cases. | None for current manifest/fallback contract. |
| REL-08 | Advanced material, particle, lighting-rig, and cinematic-post release foundations. | PARTIAL | SRC-SCENES, SRC-UI-P3, SRC-MATERIAL | `tools/run_effects_gallery_tests.ps1 -NoBuild`; `tools/run_advanced_graphics_catalog_tests.ps1` | Effects gallery passed with particles and cinematic post active. | These are foundations, not full advanced feature completion. |
| REL-09 | Optional Dreamer service is honest in startup and has both disabled and positive runtime coverage. | DONE_VERIFIED | `src/Core/Engine.cpp`, `DreamerService.cpp`, `DiffusionEngine.cpp`, `tools/run_dreamer_positive_runtime_tests.ps1` | `tools/run_release_validation.ps1`; `tools/run_dreamer_positive_runtime_tests.ps1 -NoBuild` | Standard smokes still show `Dreamer disabled by configuration` when requested; positive gate shows Dreamer enabled, generated pixels, GPU texture creation, and application to a scene entity. | TensorRT-specific execution remains hardware/build dependent. |
| REL-10 | Voxel backend is smoke-tested but experimental. | DONE_VERIFIED | `Renderer_Voxel.cpp`, `RendererVoxelState.h` | `tools/run_voxel_backend_smoke.ps1 -NoBuild -IsolatedLogs` | Voxel smoke passed in release gate. | Not primary renderer path. |

## Known Remaining Items

These items remain after the audit and should not be collapsed into
"release-gated" or "foundation complete."

| ID | Item | Status | Why it remains |
|---|---|---|---|
| REM-01 | Full render-graph ownership for every pass/resource and removal of all temporary legacy manual-barrier/fallback paths. | PARTIAL | Major passes are graph-owned, but `phase2.md` explicitly treats full ownership as the ideal; code still has fallback/manual paths. |
| REM-02 | Exhaustive renderer state ownership audit, beyond selected ownership manifest checks. | DONE_VERIFIED | `tools/run_renderer_full_ownership_audit.ps1` passed with 48/48 renderer members covered and no loose GPU resources/descriptors in `Renderer.h`; selected ownership manifest gate also passed. |
| REM-03 | Camera-cut temporal invalidation validation. | DONE_VERIFIED | `tools/run_temporal_camera_cut_validation.ps1` is wired into release validation and passed, verifying RT shadow/reflection/GI history invalidation on the configured camera cut. |
| REM-04 | Stress-memory / descriptor stress scene. | DONE_VERIFIED | `tools/run_descriptor_memory_stress_scene.ps1 -NoBuild` passed with 988/1024 persistent descriptors, 78/128 staging descriptors, zero transient descriptor delta, and memory/write budgets within limits. |
| REM-05 | Full render-graph transient alias validation matrix. | DONE_VERIFIED | `tools/run_render_graph_transient_matrix.ps1 -NoBuild -IsolatedLogs` passed and is wired into release validation. |
| REM-06 | Local reflection probes and probe blending validation. | DONE_VERIFIED | `Scene::ReflectionProbeComponent`, `Engine_Scenes.cpp`, `Renderer_VisibilityBufferDeferredLighting.cpp`, `VisibilityBuffer_Uploads.cpp`, `VisibilityBuffer.h`, `DeferredLighting.hlsl`, frame-contract environment fields, `tools/run_reflection_probe_contract_tests.ps1` | Targeted gate passed after implementation: RT Showcase declares `RTGallery_LocalProbe_Left` and `RTGallery_LocalProbe_Right`; VB deferred lighting collects `ReflectionProbeComponent`, uploads `VBReflectionProbe` data, binds `reflectionProbeParams`, and shader debug view 42 captures probe weights. Runtime evidence: `local_reflection_probe_count=2`, `local_reflection_probe_skipped=0`, `local_reflection_probe_table_valid=true`. |
| REM-07 | Outdoor/sunset beach final visual target. | DEFERRED_BY_USER_ONLY | User direction previously moved Cortex away from infinite/outdoor/world work. Not done. |
| REM-08 | Full GPU particle system as public path. | PARTIAL | Effects showcase validates ECS billboard particles; GPU particle path remains future/experimental. |
| REM-09 | Full cinematic post stack including DOF, motion blur, and color-grade presets. | PARTIAL | Current release validates bloom threshold/soft knee/vignette/lens dirt foundations. |
| REM-10 | LLM renderer command routing runtime test. | DONE_VERIFIED | `tools/run_llm_renderer_command_smoke.ps1` runs Architect in deterministic mock mode and verifies `modify_renderer` side effects in runtime logs and the frame contract. |
| REM-11 | Positive Dreamer startup/runtime test. | DONE_VERIFIED | `tools/run_dreamer_positive_runtime_tests.ps1` runs Architect mock mode with Dreamer enabled, queues `generate_texture`, verifies Dreamer online, DiffusionEngine backend/fallback reporting, generated texture logs, GPU texture creation, and application to `TemporalLab_RotatingRedSphere`. |
| REM-12 | Full golden-image visual baseline comparisons. | PARTIAL | Visual probe validation now runs all public baseline cases and checks captured BMP structure, but committed golden-image diff comparisons remain intentionally absent. |

## Phase 3 Top-Level Requirements

These rows are derived from the Phase 3 north star, non-goals, design
principles, repo-native rules, workstreams, slider inventory, frame-contract
additions, release gates, readiness review, immediate implementation order, and
definition of done in `phase3.md`.

| ID | Requirement or claim from `phase3.md` | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-GLOBAL-01 | Mature public renderer: robust, tunable, visually presentable, easy to demo, hard to silently break. | PARTIAL | SRC-CONTRACT, SRC-UI-P3, SRC-SCENES, SRC-DOCS | `tools/run_release_validation.ps1` | Full release gate and Phase 3 visual matrix passed. | "Mature" and "public" require remaining interactive UI, full effects, fallback, and packaging work before claiming complete. |
| P3-GLOBAL-02 | Reviewer can launch without setup surprises. | PARTIAL | SRC-PREFLIGHT, SRC-DOCS | `tools/run_release_validation.ps1`; `tools/run_fatal_error_contract_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Startup preflight logs `passed=true`; fatal contract passed; fallback matrix covers missing manifest and missing required/optional environment assets. | Positive install/setup path outside local smoke scripts is not fully covered. |
| P3-GLOBAL-03 | Reviewer can switch environments and IBLs safely. | PARTIAL | SRC-ENV-P3, SRC-UI-P3 | `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_environment_manifest_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | IBL gallery passed all five enabled runtime IBLs; fallback matrix covers missing selected, missing manifest, and missing required/optional asset selection. | Native interactive environment dropdown/reload behavior is not fully automated. |
| P3-GLOBAL-04 | Reviewer can tune graphics using clear sliders and presets. | PARTIAL | SRC-UI-P3 | `tools/run_graphics_ui_contract_tests.ps1`; `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke` | UI/preset contracts passed. | Many listed sliders are not present; tests are mostly static/contract, not interactive. |
| P3-GLOBAL-05 | Reviewer can inspect RT/raster/temporal state without guessing. | PARTIAL | SRC-CONTRACT, SRC-UI-P3, SRC-RT, SRC-TEMPORAL | `tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs`; `tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs` | RT readiness/signal and temporal stats passed; RT scheduler UI contract exists. | Raster/temporal debug UI is not fully unified or interactively tested. |
| P3-GLOBAL-06 | Reviewer can run release validation and get useful pass/fail output. | DONE_VERIFIED | `tools/run_release_validation.ps1`, `tools/README.md` | `tools/run_release_validation.ps1` | Latest release gate passed and printed per-step summaries/log dirs. | None for current suite. |
| P3-GLOBAL-07 | Public scenes demonstrate renderer strengths. | PARTIAL | SRC-SCENES | `tools/run_phase3_visual_matrix.ps1 -NoBuild`; `tools/run_showcase_scene_contract_tests.ps1 -NoBuild -RuntimeSmoke` | Public scene metadata has no polish gaps and visual matrix passed. | Subjective composition and final asset/polish review remain. |
| P3-GLOBAL-08 | Complex shaders, lighting, and particles used in composed scenes. | PARTIAL | SRC-SCENES, SRC-MATERIAL, `Renderer_Particles.cpp`, `RendererPostProcessState.h`, `src/Scene/ParticleEffectLibrary.cpp` | `tools/run_effects_gallery_tests.ps1 -NoBuild`; `tools/run_material_lab_smoke.ps1 -NoBuild` | Effects showcase passed with particles and cinematic post; material lab covers advanced material counts. Particle effect library now validates fire, smoke, dust, sparks, embers, mist, rain, snow, and procedural billboard fallback; runtime effects gallery passed with `emitters=8`, `particles=77`. | GPU particles, DOF/motion blur/color grading, and richer shader controls remain incomplete. |
| P3-GLOBAL-09 | Degraded behavior on low-memory or no-RT hardware is understandable. | PARTIAL | SRC-BUDGET, SRC-RT, SRC-UI-P3 | `tools/run_budget_profile_matrix.ps1 -NoBuild`; `tools/run_graphics_ui_contract_tests.ps1` | Budget matrix passed; RT scheduler UI contract exists. | No-RT hardware path is not proven by current run on RTX 3070 Ti. |
| P3-GLOBAL-10 | Same scene/profile can be reproduced from config files. | DONE_VERIFIED | SRC-UI-P3, SRC-SCENES, `assets/config/graphics_presets.json` | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke`; `tools/run_phase3_visual_matrix.ps1 -NoBuild` | Preset runtime smoke and visual matrix passed. | More profiles can be added. |
| P3-NONGOAL-01 | Do not restart infinite-world effort in Phase 3. | DEFERRED_BY_USER_ONLY | SRC-DOCS, git history | documentation/history inspection | Cortex release path focuses renderer; voxel backend is experimental smoke only. | Keep infinite-world/outdoor work out unless explicitly reopened by user. |
| P3-NONGOAL-02 | Do not replace renderer architecture wholesale. | DONE_VERIFIED | SRC-STATE, SRC-RENDER-ORCH | `tools/run_release_validation.ps1` | Changes extend existing renderer modules/state; build passes. | Continue avoiding parallel systems. |
| P3-NONGOAL-03 | Do not add large assets without policy/fallback/release-size awareness. | DONE_VERIFIED | SRC-ENV-P3, `assets/environments/environments.json`, `tools/run_ibl_asset_policy_tests.ps1` | `tools/run_ibl_asset_policy_tests.ps1`; `tools/run_environment_manifest_tests.ps1` | IBL asset policy gate passed with `runtime_assets=5`, startup downloads disabled, source assets optional, legacy scan fallback disabled, runtime format preference enforced, and budget-class asset size caps checked. | None for committed IBL startup/release-size policy. |
| P3-DESIGN-01 | Every public visual feature has a control, preset, fallback, and validation path. | PARTIAL | SRC-UI-P3, SRC-SCENES, SRC-CONTRACT | `tools/run_release_validation.ps1` | Release foundations have controls/presets/contracts. | Advanced materials, particles, cinematic post, SSR/RT tuning controls are incomplete. |
| P3-DESIGN-02 | UI settings clamp and serialize cleanly. | PARTIAL | `RendererTuningState.cpp`, `RendererControlApplier_Runtime.cpp`, `GraphicsSettingsWindow.cpp` | `tools/run_graphics_settings_persistence_tests.ps1 -NoBuild`; `tools/run_graphics_ui_contract_tests.ps1` | Persistence and UI contracts passed. | Not every planned slider exists or is round-tripped. |
| P3-DESIGN-03 | Environment assets have metadata, budget class, and fallback behavior. | DONE_VERIFIED | SRC-ENV-P3 | `tools/run_environment_manifest_tests.ps1`; `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest metadata/budget policy passed, all enabled runtime IBLs loaded, and missing required asset fallback reports `procedural_sky` plus `requested_environment_load_failed`. | None for current committed environment assets and fallback behavior. |
| P3-DESIGN-04 | Performance-sensitive features report scheduling and budget state. | PARTIAL | SRC-CONTRACT, SRC-BUDGET, SRC-RT | `tools/run_rt_showcase_smoke.ps1`; `tools/run_budget_profile_matrix.ps1`; `tools/run_lighting_energy_budget_tests.ps1 -NoBuild` | RT scheduler/budget metrics reported; budget matrix passed; public scene lighting energy budgets are now release-gated. | Particles/cinematic post budgets are only foundation-level. |
| P3-DESIGN-05 | Default view is clean enough for public capture while debug info remains available. | PARTIAL | `Engine_UI.cpp`, `Engine_Input.cpp`, `FrameContractJson.cpp` | `tools/run_hud_mode_contract_tests.ps1 -NoBuild` | HUD mode contract passed. | Full visual review of public default capture remains subjective. |
| P3-DESIGN-06 | Renderer state moves into named structs as Phase 3 touches each area. | DONE_VERIFIED | SRC-STATE, `assets/config/renderer_ownership_targets.json` | `tools/run_renderer_ownership_tests.ps1`; `tools/run_renderer_full_ownership_audit.ps1` | Ownership test passed. Full ownership audit passed with every `Renderer.h` member in a named state/service aggregate. | Future new renderer resources must extend the audit expectations. |

## Phase 3 Workstreams

| ID | Workstream | Status | Source/functions | Validation command | Evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-WS-01 | Robustness foundation. | PARTIAL | SRC-PREFLIGHT, SRC-FATAL, SRC-CONTRACT | `tools/run_release_validation.ps1`; `tools/run_fatal_error_contract_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Startup preflight, fatal contract, missing environment/asset fallback, and explicit no-RT profile gates pass. | Real device-removed/no-RT hardware behavior and end-user package setup remain incomplete. |
| P3-WS-02 | Validation and visual test matrix. | PARTIAL | `tools/run_phase3_visual_matrix.ps1`, SRC-VISUAL | `tools/run_phase3_visual_matrix.ps1 -NoBuild`; `tools/run_visual_probe_validation.ps1 -NoBuild` | Visual matrix passed. Visual probe passed all four public baseline cases with edge/dominant-color BMP checks. | Full committed golden-image comparison remains incomplete by policy. |
| P3-WS-03 | UI control surface. | PARTIAL | SRC-UI-P3 | `tools/run_graphics_ui_contract_tests.ps1`; `tools/run_graphics_settings_persistence_tests.ps1 -NoBuild` | Contracts passed. | Not all planned controls/sliders are implemented. |
| P3-WS-04 | Backgrounds, skies, and IBL library. | PARTIAL | SRC-ENV-P3 | `tools/run_environment_manifest_tests.ps1`; `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest passed; uncapped IBL gallery passed all five enabled runtime IBLs with nonblack visual captures; missing required asset fallback activates `procedural_sky`. | Asset generation/conversion tooling and broader background authoring workflow remain incomplete. |
| P3-WS-05 | Showcase scene polish. | PARTIAL | SRC-SCENES | `tools/run_phase3_visual_matrix.ps1 -NoBuild`; `tools/run_showcase_scene_contract_tests.ps1 -NoBuild -RuntimeSmoke` | Four public scenes passed and have hero bookmarks. | Human composition polish and full baseline comparison remain. |
| P3-WS-06 | Material and surface correctness. | PARTIAL | SRC-MATERIAL, SRC-SCENES | `tools/run_material_lab_smoke.ps1 -NoBuild` | Material Lab passed. | Author-facing advanced material editor/sliders incomplete. |
| P3-WS-07 | RT and temporal tuning. | PARTIAL | SRC-RT, SRC-TEMPORAL, SRC-UI-P3 | `tools/run_rt_showcase_smoke.ps1`; `tools/run_temporal_validation_smoke.ps1`; `tools/run_graphics_ui_contract_tests.ps1` | RT readiness/signal, temporal stats, scheduler UI passed. | RT tuning sliders, firefly scenes, and luma-aware tuning are incomplete. |
| P3-WS-08 | User experience polish. | PARTIAL | SRC-UI-P3, SRC-DOCS | `tools/run_hud_mode_contract_tests.ps1`; `tools/run_graphics_preset_tests.ps1` | HUD/preset/docs passed. | Launcher/profile UX and interactive UI automation incomplete. |
| P3-WS-09 | Renderer architecture cleanup. | PARTIAL | SRC-STATE, `assets/config/renderer_ownership_targets.json` | `tools/run_renderer_ownership_tests.ps1`; `tools/run_renderer_full_ownership_audit.ps1`; `tools/run_release_validation.ps1` | Ownership gate and full member audit passed. | Full pass-owned resource extraction remains incomplete; renderer orchestration still centralizes pass execution. |
| P3-WS-10 | Advanced shaders, lighting, and particles. | PARTIAL | SRC-MATERIAL, SRC-SCENES, `Renderer_Particles.cpp`, `RendererPostProcessState.h`, `src/Scene/ParticleEffectLibrary.cpp` | `tools/run_effects_gallery_tests.ps1 -NoBuild`; `tools/run_advanced_graphics_catalog_tests.ps1` | Effects gallery and catalog passed. Particle descriptor library covers the planned public ECS effects set and procedural fallback. | Planned advanced material, GPU particle, and full cinematic-post feature projects are not complete. |

## Phase 3 Pass Ledger

| ID | Pass | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-3A | Startup Preflight Contract. | DONE_VERIFIED | SRC-PREFLIGHT, SRC-CONTRACT | `tools/run_release_validation.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Latest release shows default preflight `passed=true warnings=0 errors=0`; fallback matrix covers safe startup, missing selected environment, missing manifest, missing required/optional environment assets, and explicit no-RT profile. | None for current startup preflight contract fields. |
| P3-3B | Runtime Health State. | PARTIAL | SRC-CONTRACT, SRC-UI-P3 | `tools/run_release_validation.ps1`; `tools/run_hud_mode_contract_tests.ps1 -NoBuild` | Frame reports include health/preset fields; HUD contract passed. | Debug UI health panel and release summary coverage are partial. |
| P3-3C | Device Removed and Fatal Error UX. | PARTIAL | SRC-FATAL | `tools/run_fatal_error_contract_tests.ps1 -NoBuild` | Fatal error contract passed. | User-facing message box and real device-removed path not fully runtime-tested. |
| P3-3D | Phase 3 Visual Matrix Script. | DONE_VERIFIED | `tools/run_phase3_visual_matrix.ps1` | `tools/run_phase3_visual_matrix.ps1 -NoBuild -TemporalSmokeFrames 90 -RTSmokeFrames 180 -SkipSurfaceDebug` | Latest matrix passed temporal validation, RT showcase, material lab, glass/water courtyard, effects showcase, uncapped IBL gallery, and fallback matrix rows. Logs: `phase3_visual_matrix_20260510_233548_239_31856_bb145491`. | Additional optional visual rows can be added as new public scenes are stabilized. |
| P3-3E | Screenshot Contract. | PARTIAL | SRC-VISUAL, smoke scripts | `tools/run_release_validation.ps1`; `tools/run_visual_baseline_contract_tests.ps1 -NoBuild -RuntimeSmoke -MaxRuntimeCases 1` | Visual stats gates pass in public smokes. | Edge occupancy, dominant hue warnings, forced black/overexposed failure tests, and object coverage masks are incomplete. |
| P3-3F | Visual Gate Stabilization; no early golden churn. | PARTIAL | SRC-VISUAL, `assets/config/visual_baselines.json` | `tools/run_visual_baseline_contract_tests.ps1`; `tools/run_visual_probe_validation.ps1`; `tools/run_screenshot_negative_gates.ps1` | Visual baseline contract, all-case visual probe, and synthetic screenshot negative gates passed. | Golden/tolerant baselines exist as metadata and runtime probes; no committed full image-diff golden comparison. |
| P3-3G | Renderer Tuning State. | DONE_VERIFIED | `RendererTuningState.h/cpp`, `RendererControlApplier_Runtime.cpp` | `tools/run_graphics_settings_persistence_tests.ps1 -NoBuild`; `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke` | Persistence and preset runtime smoke passed. | Additional slider fields remain future work. |
| P3-3H | Graphics Settings Window. | PARTIAL | `src/UI/GraphicsSettingsWindow.cpp` | `tools/run_graphics_ui_contract_tests.ps1` | Static UI contract passed; F8/ESC/tabs/control bindings checked. | No live interactive UI automation; many planned tabs/sliders are not present. |
| P3-3I | Preset Save, Load, and Reset / Graphics Presets. | DONE_VERIFIED | `assets/config/graphics_presets.json`, `RendererTuningState.cpp`, `Engine.cpp` preset load | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke`; release gate | Preset tests passed with default `release_showcase`. | More version migration tests would improve coverage. |
| P3-3I5 | IBL Asset Policy Lock. | DONE_VERIFIED | `assets/environments/environments.json`, SRC-ENV-P3 | `tools/run_environment_manifest_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest policy now enforces `normal_startup_downloads=false` and `legacy_scan_fallback=false`; fallback matrix proves missing manifest and missing required runtime asset behavior without mutating committed assets. | Optional source conversion/download tooling is intentionally outside startup policy and remains tracked separately under asset tooling rows. |
| P3-3J | Environment Manifest. | DONE_VERIFIED | `EnvironmentManifest.cpp::LoadEnvironmentManifest`, `assets/environments/environments.json` | `tools/run_environment_manifest_tests.ps1` | Environment manifest tests passed: environments=6, default=studio, fallback=procedural_sky. | Runtime fallback variants handled under 3K/3L. |
| P3-3K | Environment Library Runtime. | DONE_VERIFIED | `Renderer_Environment.cpp`, `RendererEnvironmentState.h`, SRC-ENV-P3 | `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild`; release gate | IBL gallery passed for all enabled runtime IBLs: `studio`, `warm_gallery`, `sunset_courtyard`, `cool_overcast`, `night_city`; missing required physical asset fallback passed. | None for the current runtime environment library contract. |
| P3-3L | Procedural Sky and Fallback Background. | DONE_VERIFIED | `assets/shaders/ProceduralSky.hlsl`, SRC-ENV-P3 | `tools/run_phase3_fallback_matrix.ps1 -NoBuild`; `tools/run_environment_manifest_tests.ps1` | Missing required runtime asset case reports active environment `procedural_sky`, `environment.fallback=true`, and `requested_environment_load_failed`; procedural sky pipeline is created during startup. | None for the current fallback background contract. |
| P3-3M | IBL Review Scene / IBL Gallery. | DONE_VERIFIED | `tools/run_ibl_gallery_tests.ps1`, SRC-SCENES, SRC-ENV-P3 | `tools/run_ibl_gallery_tests.ps1 -NoBuild` | IBL gallery passed all five enabled runtime IBLs with valid frame reports and nonblack captures. Logs: `ibl_gallery_20260510_231326_969_32952_7f9fb9d9`. | None for the current enabled IBL gallery contract. |
| P3-3N | Scene Composition Rules. | PARTIAL | `assets/config/showcase_scenes.json`, SRC-SCENES | `tools/run_showcase_scene_contract_tests.ps1 -NoBuild -RuntimeSmoke` | Scene contract passed, no `polish_gaps`. | Composition is not fully machine-verifiable. |
| P3-3O | Public Showcase Scenes. | PARTIAL | SRC-SCENES, `showcase_scenes.json` | `tools/run_phase3_visual_matrix.ps1 -NoBuild` | RT showcase, material lab, glass/water courtyard, effects showcase passed. | Low-memory safe-mode and dedicated IBL gallery scene coverage is partial; subjective polish remains. |
| P3-3P | Camera Bookmarks and Capture Paths. | DONE_VERIFIED | `showcase_scenes.json`, `Engine_Scenes.cpp` bookmark loading | `tools/run_showcase_scene_contract_tests.ps1 -NoBuild -RuntimeSmoke`; `tools/run_phase3_visual_matrix.ps1 -NoBuild` | Hero bookmarks validated and applied in logs. | Add more bookmark runtime cases beyond hero where needed. |
| P3-3Q | Material Preset Library. | PARTIAL | SRC-MATERIAL, `MaterialPresetRegistry.cpp`, SRC-SCENES | `tools/run_material_lab_smoke.ps1 -NoBuild`; `tools/run_material_editor_contract_tests.ps1` | Material Lab and editor contract passed. | Full advanced preset library and authoring controls incomplete. |
| P3-3R | Material Editor Improvements. | PARTIAL | `src/UI/SceneEditorWindow.cpp` | `tools/run_material_editor_contract_tests.ps1` | Contract checks preset dropdown, sliders, validation status, command path. | Runtime UI editing behavior is not interactively tested; advanced material sliders incomplete. |
| P3-3S | RT Reflection Tuning Controls. | PARTIAL | SRC-RT, SRC-UI-P3 | `tools/run_rt_showcase_smoke.ps1`; `tools/run_graphics_ui_contract_tests.ps1` | RT signal metrics and scheduler UI pass. | Specific reflection scale, roughness, denoise blend, history clamp, firefly clamp, composition strength sliders are not fully implemented. |
| P3-3T | Outlier and Firefly Handling. | PARTIAL | SRC-RT, RT shaders | `tools/run_rt_showcase_smoke.ps1` | RT showcase bright/saturation/near-white gates pass. | No dedicated overbright/firefly stress scene or clamp validation. |
| P3-3U | RT Scheduler Explanation UI. | DONE_VERIFIED | `GraphicsSettingsWindow.cpp`, SRC-CONTRACT | `tools/run_graphics_ui_contract_tests.ps1`; RT smoke | UI contract checks scheduler panel/reasons; RT smoke reports readiness. | Interactive visual UI inspection not performed. |
| P3-3V | Launcher and CLI Profiles. | PARTIAL | `src/main.cpp`, `Engine.cpp`, graphics preset startup | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke`; release gate | CLI scene/preset paths are used by smokes. | Dedicated launcher UI/profile selector not complete. |
| P3-3W | Settings Persistence. | DONE_VERIFIED | `RendererTuningState.cpp`, `GraphicsSettingsWindow.cpp` | `tools/run_graphics_settings_persistence_tests.ps1 -NoBuild` | Persistence tests passed. | Schema migration coverage can be added. |
| P3-3X | Clean HUD Modes. | DONE_VERIFIED | `Engine.h`, `Engine_UI.cpp`, `Engine_Input.cpp`, `main.cpp` | `tools/run_hud_mode_contract_tests.ps1 -NoBuild` | HUD mode contract passed for CLI/env/F7/reporting. | None for current contract. |
| P3-3Y | Presentation State Structs. | PARTIAL | SRC-STATE, `RendererPostProcessState.h`, `RendererEnvironmentState.h`, `RendererParticleState.h` | `tools/run_renderer_ownership_tests.ps1`; release gate | Ownership metadata and selected state structs validated. | Not every presentation control has a complete pass-owned bundle. |
| P3-3Z | Pass-Owned Resource Bundles. | PARTIAL | SRC-STATE, `renderer_ownership_targets.json` | `tools/run_renderer_ownership_tests.ps1`; `tools/run_renderer_full_ownership_audit.ps1` | Ownership test passed for RT stats/environment/post/particles. Full ownership audit verifies all current `Renderer` members are state/service aggregates. | Manifest itself preserves future extensions; per-pass resource ownership remains incomplete because many passes still reach through renderer aggregates. |
| P3-3AA | Advanced Material Shader Framework. | PARTIAL | SRC-MATERIAL, `assets/shaders/Basic.hlsl`, `DeferredLighting.hlsl`, `MaterialResolve.hlsl` | `tools/run_material_lab_smoke.ps1`; `tools/run_advanced_graphics_catalog_tests.ps1` | Material Lab checks advanced material counts; catalog says foundation validated. | Procedural masks and author-facing advanced material sliders incomplete. |
| P3-3AB | Cinematic Lighting Rigs. | PARTIAL | `RendererLightingRigControl.cpp`, `RendererControlApplier_ScenePresets.cpp`, SRC-SCENES | `tools/run_phase3_visual_matrix.ps1 -NoBuild`; `tools/run_showcase_scene_contract_tests.ps1` | Public scenes report explicit rigs: material_lab_review, sunset_rim, night_emissive, RT gallery. | UI selection/command routing for rigs not fully implemented/tested. |
| P3-3AC | GPU Particle System Foundation. | PARTIAL | `Renderer_Particles.cpp`, `RendererParticleState.h`, `GPUParticles.h`, particle shaders | `tools/run_effects_showcase_smoke.ps1 -NoBuild`; `tools/run_effects_gallery_tests.ps1 -NoBuild` | Effects showcase passes with live/submitted ECS billboard particles. | Public path is CPU/ECS billboards; GPU particle simulation/sort/render path is not public validated. |
| P3-3AD | Particle Effect Library. | DONE_VERIFIED | `src/Scene/ParticleEffectLibrary.h`, `src/Scene/ParticleEffectLibrary.cpp`, `src/Scene/Components.h`, SRC-SCENES, `RendererParticleState.h` | `tools/run_advanced_graphics_catalog_tests.ps1`; `tools/run_effects_gallery_tests.ps1 -NoBuild`; `tools/run_phase3_visual_matrix.ps1 -NoBuild` | Static catalog validates fire, smoke, dust, sparks, embers, mist, rain, snow, and `procedural_billboard` fallback policy. Runtime effects gallery passed with `emitters=8`, `particles=77`; particle-disabled zero-cost gate still passed; Phase 3 visual matrix passed. | None for the public ECS billboard particle effect library. GPU particle migration remains tracked separately. |
| P3-3AE | Volumetric and Atmospheric Polish. | PARTIAL | fog/god-ray controls, `Engine_Scenes.cpp`, shaders | `tools/run_glass_water_courtyard_smoke.ps1 -NoBuild`; `tools/run_effects_showcase_smoke.ps1 -NoBuild` | Glass/water/effects scenes pass with fog/cinematic settings. | Volumetric shafts, environment-matched fog, depth consistency beyond current smokes incomplete. |
| P3-3AF | Cinematic Post Stack. | PARTIAL | `RendererPostProcessState.h`, `Renderer_QualitySettings.cpp`, `PostProcess.hlsl`, `RendererTuningState.cpp` | `tools/run_effects_showcase_smoke.ps1 -NoBuild`; `tools/run_effects_gallery_tests.ps1 -NoBuild` | Effects showcase validates cinematic post enabled, vignette and lens dirt active. | DOF, motion blur, tone mapper/color grade presets incomplete. |
| P3-3AG | Effects Showcase Scene. | DONE_VERIFIED | SRC-SCENES, `run_effects_showcase_smoke.ps1` | `tools/run_effects_showcase_smoke.ps1 -NoBuild`; release gate | Effects showcase passed with particles, cinematic post, advanced material coverage, visual stats. | Scene can still be polished, but current runtime gate passes. |

## Phase 3 Graphics Slider Inventory

| ID | Slider/control group from `phase3.md` | Status | Source/functions | Validation command | Evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-SLIDER-QUALITY | Quality controls: preset, render scale, TAA, FXAA, GPU culling, safe lighting. | PARTIAL | SRC-UI-P3, `Renderer_QualitySettings.cpp`, `Renderer_FeatureSettings.cpp` | `tools/run_graphics_ui_contract_tests.ps1`; `tools/run_graphics_preset_tests.ps1` | Core quality/preset controls are contract-covered. | Complete inventory and interactive slider automation missing. |
| P3-SLIDER-TONE | Tone/lighting controls: exposure, bloom, contrast, saturation, warmth, sun/god rays/area light/rig. | PARTIAL | SRC-UI-P3, lighting/post state | `tools/run_graphics_ui_contract_tests.ps1`; `tools/run_phase3_visual_matrix.ps1` | Exposure/bloom/god-ray/rig presets are runtime-used. | Contrast/saturation/warmth and full rig UI not fully proven. |
| P3-SLIDER-ENV | Environment/IBL controls: dropdown, IBL toggle/intensity, rotation, background exposure/blur, budget class, reload. | PARTIAL | SRC-ENV-P3, SRC-UI-P3 | `tools/run_ibl_gallery_tests.ps1`; `tools/run_graphics_ui_contract_tests.ps1` | IBL runtime and some UI controls exist. | Full dropdown/reload/rotation/background blur interactive behavior not fully validated. |
| P3-SLIDER-RT | RT controls: RT, reflections, GI, reflection scale, roughness, denoise blend, feedback, clamp, firefly, composition strength. | PARTIAL | SRC-RT, SRC-UI-P3 | `tools/run_rt_showcase_smoke.ps1`; `tools/run_graphics_ui_contract_tests.ps1` | Core toggles/readiness/scheduler visible. | Most tuning sliders are missing or not validated. |
| P3-SLIDER-SS | Screen-space controls: SSAO and SSR parameters. | PARTIAL | `Renderer_SSAO.cpp`, `Renderer_RenderGraphSSR.cpp`, SRC-UI-P3 | `tools/run_release_validation.ps1` | SSAO/SSR active in smokes. | SSR strength/max distance/thickness UI/tuning not fully implemented. |
| P3-SLIDER-ATM | Atmosphere controls: fog and god rays. | PARTIAL | fog/god-ray renderer controls, SRC-SCENES | `tools/run_glass_water_courtyard_smoke.ps1` | Fog/god rays used in scenes and smokes pass. | Full UI inventory and volumetric polish incomplete. |
| P3-SLIDER-WATER | Water controls: wave amplitude/wavelength/speed/Fresnel/roughness. | PARTIAL | `Renderer_WaterSurfaces.cpp`, water state | `tools/run_glass_water_courtyard_smoke.ps1` | Water scene smoke passes. | Planned water sliders are not fully validated. |
| P3-SLIDER-MAT | Advanced material controls: clearcoat, anisotropy, sheen, subsurface, wetness, procedural, emissive bloom. | PARTIAL | SRC-MATERIAL, `SceneEditorWindow.cpp` | `tools/run_material_lab_smoke.ps1`; `tools/run_material_editor_contract_tests.ps1` | Advanced material coverage and basic editor validation pass. | Advanced material sliders/procedural controls incomplete. |
| P3-SLIDER-PARTICLES | Particle controls: enable, quality, density, effect preset, bloom, soft depth, wind. | PARTIAL | `RendererParticleState.h`, `Renderer_Particles.cpp`, SRC-UI-P3 | `tools/run_effects_gallery_tests.ps1`; `tools/run_graphics_ui_contract_tests.ps1` | Density/enable foundation validated; particles run in effects scene. | Effect preset, bloom contribution, soft depth, wind, GPU path incomplete. |
| P3-SLIDER-POST | Cinematic post controls: tone mapper, bloom shape, vignette, lens dirt, DOF, motion blur, color grade. | PARTIAL | `RendererPostProcessState.h`, `PostProcess.hlsl`, SRC-UI-P3 | `tools/run_effects_gallery_tests.ps1`; `tools/run_graphics_preset_tests.ps1` | Bloom threshold/soft knee/vignette/lens dirt foundations validated. | DOF, motion blur, color grade presets incomplete. |

## Phase 3 Frame Contract Additions

| ID | Requested frame-contract section/rule | Status | Source/functions | Validation command | Evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-FC-STARTUP | `startup` section. | DONE_VERIFIED | SRC-PREFLIGHT, SRC-CONTRACT | `tools/run_release_validation.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Startup preflight logs pass; fallback matrix covers safe mode, missing selected environment, missing manifest, missing required environment asset, missing optional environment asset, and explicit no-RT profile. | None for current startup frame-contract fields. |
| P3-FC-HEALTH | `health` section. | DONE_VERIFIED | SRC-CONTRACT, `FrameContractJson.cpp` | `tools/run_release_validation.ps1`; `tools/run_hud_mode_contract_tests.ps1` | Release gate and HUD contracts pass. | None for current fields. |
| P3-FC-PRESET | `graphics_preset` section and unknown preset reporting. | DONE_VERIFIED | SRC-UI-P3, SRC-CONTRACT | `tools/run_graphics_preset_tests.ps1 -NoBuild -RuntimeSmoke` | Preset tests passed. | Add explicit unknown-preset negative test if absent. |
| P3-FC-ENV | `environment` active/fallback section. | DONE_VERIFIED | SRC-ENV-P3, SRC-CONTRACT | `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_environment_manifest_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | IBL gallery reports active environments. Fallback matrix verifies `environment.requested`, `environment.fallback=true`, `requested_environment_not_found`, missing required asset `requested_environment_load_failed` with active `procedural_sky`, and missing optional asset fallback to `studio`. | None for the current active/fallback frame-contract fields. |
| P3-FC-LIGHTING | `lighting_rig` explicit for public scenes. | DONE_VERIFIED | SRC-SCENES, SRC-CONTRACT | `tools/run_phase3_visual_matrix.ps1 -NoBuild` | Visual matrix summary shows explicit lighting rigs. | UI/command rig selection remains partial. |
| P3-FC-UI | `ui_state` dirty/open/HUD behavior. | PARTIAL | SRC-UI-P3, SRC-CONTRACT | `tools/run_hud_mode_contract_tests.ps1`; `tools/run_graphics_ui_contract_tests.ps1`; `tools/run_graphics_ui_interaction_smoke.ps1 -NoBuild` | HUD mode report behavior validated. Graphics interaction smoke passed and verifies runtime-applied dirty graphics state in the frame contract. | Native Win32 open/close and widget mouse/keyboard automation is still not implemented. |
| P3-FC-SCREENSHOT | `screenshot_stats` section required only when capture enabled. | PARTIAL | SRC-VISUAL, smoke scripts | `tools/run_release_validation.ps1`; `tools/run_screenshot_negative_gates.ps1 -NoBuild`; `tools/run_visual_probe_validation.ps1 -NoBuild` | Visual stats parsed by smokes. Synthetic black/white/saturated/edge negative gates passed. All public visual baseline captures passed edge/dominant-color probes. | Exact optionality rules and full golden-image comparisons remain incomplete. |
| P3-FC-RTTUNE | `rt_reflection_tuning` section. | PARTIAL | SRC-RT, SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1` | RT signal/readiness stats exist. | Tuning-specific fields/sliders incomplete. |
| P3-FC-ADV-MAT | `advanced_materials` counts. | DONE_VERIFIED | SRC-MATERIAL, SRC-CONTRACT | `tools/run_material_lab_smoke.ps1`; `tools/run_effects_showcase_smoke.ps1` | Advanced material counts validated. | More feature controls remain partial. |
| P3-FC-PARTICLES | `particles` stats. | DONE_VERIFIED | `RendererParticleState.h`, `Renderer_FrameContractSnapshot.cpp` | `tools/run_effects_showcase_smoke.ps1`; `tools/run_effects_gallery_tests.ps1` | Effects gallery passed with particles/submitted instances. | GPU particle stats incomplete. |
| P3-FC-CINE | `cinematic_post` stats. | DONE_VERIFIED | `RendererPostProcessState.h`, SRC-CONTRACT | `tools/run_effects_showcase_smoke.ps1`; `tools/run_effects_gallery_tests.ps1` | Effects showcase validates cinematic post enabled/vignette/lens dirt. | Full post stack fields incomplete. |
| P3-FC-SHOWCASE | `showcase_scene` section with scene/bookmark/profile. | DONE_VERIFIED | SRC-SCENES, SRC-CONTRACT | `tools/run_showcase_scene_contract_tests.ps1`; `tools/run_phase3_visual_matrix.ps1` | Showcase contracts and visual matrix passed. | More bookmark cases can be added. |

## Phase 3 Release Gates And Acceptance Thresholds

| ID | Gate or threshold from `phase3.md` | Status | Source/functions | Validation command | Latest evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-GATE-01 | Engine build. | DONE_VERIFIED | build scripts | `tools/run_release_validation.ps1` | `build_release` passed. | None. |
| P3-GATE-02 | Existing release validation. | DONE_VERIFIED | `tools/run_release_validation.ps1` | `tools/run_release_validation.ps1` | Latest full release gate passed. | None for current suite. |
| P3-GATE-03 | Phase 3 visual matrix. | DONE_VERIFIED | `tools/run_phase3_visual_matrix.ps1` | release gate matrix step | Matrix passed with public scenes, uncapped IBL gallery, and fallback matrix rows. | Add more rows only as new public scenes/features stabilize. |
| P3-GATE-04 | Graphics preset round-trip test. | DONE_VERIFIED | `tools/run_graphics_preset_tests.ps1`, `RendererTuningState.cpp` | release gate preset step | Preset tests passed. | Add migration/unknown negative coverage. |
| P3-GATE-05 | IBL asset policy test. | DONE_VERIFIED | `tools/run_ibl_asset_policy_tests.ps1`, `tools/run_environment_manifest_tests.ps1`, manifest | `tools/run_ibl_asset_policy_tests.ps1` | Standalone policy gate passed: `runtime_assets=5`; startup downloads disabled; source assets optional; legacy scan fallback disabled; runtime format preferences and budget-class size caps enforced. | None for current IBL asset policy gate. |
| P3-GATE-06 | Environment manifest test. | DONE_VERIFIED | `EnvironmentManifest.cpp`, `environments.json` | `tools/run_environment_manifest_tests.ps1` | Passed. | None for current manifest. |
| P3-GATE-07 | IBL gallery matrix. | DONE_VERIFIED | `tools/run_ibl_gallery_tests.ps1`; `tools/run_release_validation.ps1`; `tools/run_phase3_visual_matrix.ps1` | `tools/run_ibl_gallery_tests.ps1 -NoBuild`; `tools/run_phase3_visual_matrix.ps1 -NoBuild -TemporalSmokeFrames 90 -RTSmokeFrames 180 -SkipSurfaceDebug` | Direct IBL gallery passed all five enabled runtime IBLs; uncapped Phase 3 matrix passed and included the full gallery. | None for all enabled runtime IBLs; physical missing-asset variants remain tracked under fallback policy rows. |
| P3-GATE-08 | UI smoke for graphics settings. | DONE_VERIFIED | `tools/run_graphics_ui_contract_tests.ps1`, `tools/run_graphics_ui_interaction_smoke.ps1`, `GraphicsSettingsWindow.cpp` | release gate UI steps; `tools/run_graphics_ui_interaction_smoke.ps1 -NoBuild` | Static contract passed. Runtime interaction smoke passed and verifies settings application through `RendererTuningState`. | Native mouse/keyboard widget automation remains tracked separately. |
| P3-GATE-09 | Material preset validation. | DONE_VERIFIED | SRC-MATERIAL, `tools/run_material_lab_smoke.ps1` | release gate material lab/editor steps | Material Lab and editor contracts passed. | None for current public scenes. |
| P3-GATE-10 | RT reflection tuning validation. | PARTIAL | SRC-RT | `tools/run_rt_showcase_smoke.ps1`; `tools/run_rt_firefly_outlier_scene.ps1 -NoBuild` | RT signal/readiness validated. Firefly/outlier gate passed with raw/history outlier ratios under stricter thresholds. | Tuning sliders and a deliberately overbright/clamped stress scene remain incomplete. |
| P3-GATE-11 | Low-memory safe profile validation. | DONE_VERIFIED | SRC-BUDGET, budget script | `tools/run_budget_profile_matrix.ps1 -NoBuild` | Budget matrix passed 4 GB/2 GB. | No-RT hardware path still unproven. |
| P3-GATE-12 | Advanced material shader validation. | PARTIAL | SRC-MATERIAL, shaders | `tools/run_material_lab_smoke.ps1`; `tools/run_advanced_graphics_catalog_tests.ps1` | Material Lab/catalog pass. | Full advanced shader framework still partial. |
| P3-GATE-13 | Particle effect gallery validation. | DONE_VERIFIED | `tools/run_effects_gallery_tests.ps1`, `tools/run_advanced_graphics_catalog_tests.ps1`, `src/Scene/ParticleEffectLibrary.cpp`, particles | `tools/run_effects_gallery_tests.ps1 -NoBuild`; release gate effects gallery step | Effects gallery passed after the full public effect-library implementation with `emitters=8`, `particles=77`; catalog validates all public effect descriptors and fallback policy. | None for the public ECS billboard gallery validation. GPU particle public path remains tracked separately. |
| P3-GATE-14 | Effects showcase visual validation. | DONE_VERIFIED | `tools/run_effects_showcase_smoke.ps1`, SRC-SCENES | release gate effects/gallery/matrix steps | Effects showcase passed. | Further polish possible. |
| P3-THRESH-01 | No fatal startup warnings in default profile. | DONE_VERIFIED | SRC-PREFLIGHT | `tools/run_release_validation.ps1` | Startup preflight warnings=0/errors=0 in inspected logs. | None for default profile. |
| P3-THRESH-02 | No frame-contract warnings in release showcase. | DONE_VERIFIED | SRC-CONTRACT | `tools/run_rt_showcase_smoke.ps1` | RT showcase passed with warnings=0. | None for current scene. |
| P3-THRESH-03 | No descriptor/DXGI memory budget regression. | DONE_VERIFIED | SRC-BUDGET, smoke scripts | `tools/run_rt_showcase_smoke.ps1`; `tools/run_budget_profile_matrix.ps1` | Latest RT showcase within descriptor/memory budgets. | Add stress scene for broader confidence. |
| P3-THRESH-04 | RT reflection readiness true on supported profile. | DONE_VERIFIED | SRC-RT | `tools/run_rt_showcase_smoke.ps1` | `rt_refl_ready=True/ready`. | None for supported profile. |
| P3-THRESH-05 | Raw and history reflection signals nonzero. | DONE_VERIFIED | SRC-RT | `tools/run_rt_showcase_smoke.ps1` | `rt_signal` and `rt_hist` nonzero. | None for current showcase. |
| P3-THRESH-06 | Fallback profile runs without RT. | DONE_VERIFIED | SRC-BUDGET, SRC-RT | `tools/run_phase3_fallback_matrix.ps1 -NoBuild`; `tools/run_budget_profile_matrix.ps1` | Fallback matrix passed `explicit_no_rt_profile` and verifies RT features disabled with scheduler reason `not_requested`. Budget matrix also passes low-memory profiles. | No-RT hardware path still unproven on this RTX 3070 Ti machine. |
| P3-THRESH-07 | IBL gallery screenshots nonblack/not overexposed. | DONE_VERIFIED | SRC-ENV-P3, IBL gallery script | `tools/run_ibl_gallery_tests.ps1 -NoBuild` | IBL gallery passed all five enabled runtime IBLs with valid captures; avg luma ranged from `72.46` to `108.58`, warnings were `0` for every environment. | None for current enabled runtime IBL screenshots. |
| P3-THRESH-08 | Default IBL policy documented/enforced before manifest loading. | DONE_VERIFIED | SRC-ENV-P3, SRC-DOCS | `tools/run_environment_manifest_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest policy test enforces no startup downloads and no legacy scan fallback; startup fallback matrix proves missing manifest and missing required runtime asset behavior before environment loading. | None for current startup policy enforcement. |
| P3-THRESH-09 | No golden baselines required until scene/camera/lighting stability. | PARTIAL | `visual_baselines.json`, visual baseline tests | `tools/run_visual_baseline_contract_tests.ps1`; `tools/run_visual_probe_validation.ps1` | Baseline contract exists and passed; visual probe validates runtime captures for every baseline case without committing churn-prone images. | Full golden image comparison not implemented; policy changed from deferred to metric/runtime probes. |
| P3-THRESH-10 | Effects showcase has nonzero advanced materials and live particles. | DONE_VERIFIED | SRC-SCENES, particles, material contract | `tools/run_effects_showcase_smoke.ps1` | Effects showcase smoke passed with particles and advanced material coverage. | None for current scene. |
| P3-THRESH-11 | Particle disabled profile has no measurable particle cost. | DONE_VERIFIED | particles, graphics presets | `tools/run_particle_disabled_zero_cost.ps1 -NoBuild` | Particle-disabled zero-cost gate passed; `safe_startup` Effects Showcase reports zero planned/executed/live/submitted particle work and zero instance-buffer bytes. | None for current ECS billboard path. |
| P3-THRESH-12 | UI settings save/load round trip passes. | DONE_VERIFIED | `RendererTuningState.cpp` | `tools/run_graphics_settings_persistence_tests.ps1 -NoBuild` | Persistence tests passed. | None for current fields. |
| P3-THRESH-13 | README/release notes match actual CLI/scripts. | DONE_VERIFIED | SRC-DOCS | documentation inspection plus latest commit | README/release readiness updated to current gate. | Keep current as scripts change. |

## Phase 3 Pre-Readiness Review Items

| ID | Known gap from `phase3.md` | Status | Source/functions | Validation command | Evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-READY-01 | Renderer is still large and cross-cutting. | PARTIAL | SRC-STATE, SRC-RENDER-ORCH | `tools/run_renderer_ownership_tests.ps1` | Selected ownership boundaries pass. | Full state/resource audit and deeper pass-owned extraction remain. |
| P3-READY-02 | Graphics settings UI is not yet a unified control surface. | PARTIAL | SRC-UI-P3 | `tools/run_graphics_ui_contract_tests.ps1`; `tools/run_graphics_ui_interaction_smoke.ps1 -NoBuild` | Unified window contract and runtime settings-application smoke pass. | The full slider inventory and native widget automation remain incomplete. |
| P3-READY-03 | Environment/IBL policy and manifest are not implemented. | DONE_VERIFIED | SRC-ENV-P3 | `tools/run_environment_manifest_tests.ps1`; `tools/run_ibl_gallery_tests.ps1`; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest, IBL runtime, all enabled IBL gallery, missing selected environment fallback, missing manifest warning, and missing required physical asset fallback all pass. | None for the policy/manifest implementation gap. |
| P3-READY-04 | Showcase scenes still need polish. | PARTIAL | SRC-SCENES | `tools/run_phase3_visual_matrix.ps1` | Public scene matrix passes. | Human polish, full baselines, and more demo composition remain. |
| P3-READY-05 | Advanced shaders, lighting, particles, and cinematic post are planned, not complete. | PARTIAL | SRC-MATERIAL, SRC-SCENES, particles/post state | `tools/run_effects_gallery_tests.ps1`; `tools/run_advanced_graphics_catalog_tests.ps1` | Foundations validated. | Feature projects remain incomplete. |
| P3-READY-06 | More pass-owned resource/state extraction is needed. | PARTIAL | SRC-STATE, ownership manifest | `tools/run_renderer_ownership_tests.ps1` | Selected release boundaries pass. | More extraction needed as features grow. |

## Phase 3 Immediate Implementation Order

| ID | Ordered item | Status | Source/functions | Validation command | Evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-ORDER-01 | Keep `phase3.md` as blueprint and repo-native. | DONE_VERIFIED | `phase3.md`, this ledger | documentation inspection | File exists and uses repo paths. | Update as reality changes. |
| P3-ORDER-02 | Extend `RendererControlApplier` before adding sliders. | PARTIAL | `RendererControlApplier*.cpp`, SRC-UI-P3 | graphics UI/preset tests | Existing controls route through appliers. | More planned controls/sliders remain. |
| P3-ORDER-03 | Add `RendererTuningState` and route apply/capture through appliers. | DONE_VERIFIED | `RendererTuningState.cpp`, `GraphicsSettingsWindow.cpp` | persistence/preset tests | Tests passed. | None for current fields. |
| P3-ORDER-04 | Add graphics preset JSON schema and round-trip tests. | DONE_VERIFIED | `graphics_presets.json`, preset tests | `tools/run_graphics_preset_tests.ps1` | Passed. | Migration tests optional. |
| P3-ORDER-05 | Add startup preflight contract for assets/config/environment. | DONE_VERIFIED | SRC-PREFLIGHT | release gate; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Default startup preflight and fallback matrix pass safe mode, missing selected environment, missing manifest, missing required asset, missing optional asset, and explicit no-RT profile. | None for current startup preflight contract. |
| P3-ORDER-06 | Lock IBL asset policy. | DONE_VERIFIED | `environments.json`, SRC-ENV-P3 | environment manifest test; `tools/run_phase3_fallback_matrix.ps1 -NoBuild` | Manifest policy passes and runtime fallback honors strict `legacy_scan_fallback=false`. | Optional offline conversion tooling remains separate. |
| P3-ORDER-07 | Add environment manifest parser and procedural fallback. | DONE_VERIFIED | `EnvironmentManifest.cpp`, procedural sky shader, environment fallback contract fields | manifest/IBL/fallback tests | Parser, fallback declaration, missing selected environment fallback, missing manifest warning, missing required physical asset fallback, and missing optional physical asset fallback all pass. | None for current parser/fallback contract. |
| P3-ORDER-08 | Add frame contract environment/health/preset sections. | DONE_VERIFIED | SRC-CONTRACT | release gate plus focused tests | Current smokes consume these fields. | Startup/fallback negative variants partial. |
| P3-ORDER-09 | Add Graphics Settings window tabs. | PARTIAL | `GraphicsSettingsWindow.cpp` | graphics UI contract | Contract passes. | Full planned tab/control inventory incomplete. |
| P3-ORDER-10 | Add Phase 3 visual matrix. | DONE_VERIFIED | `run_phase3_visual_matrix.ps1` | release gate | Passed with temporal, RT, material, glass/water, effects, IBL, and fallback rows. | Add more rows only as new public scenes/features stabilize. |
| P3-ORDER-11 | Stabilize public scene cameras, lighting rigs, IBL defaults before golden baselines. | PARTIAL | SRC-SCENES | showcase/visual matrix tests | Hero cameras/rigs/default envs pass. | Full golden image workflow incomplete. |
| P3-ORDER-12 | Add IBL gallery scene and screenshot stats gates. | PARTIAL | IBL gallery tests, visual stats scripts | IBL gallery tests; `tools/run_screenshot_negative_gates.ps1 -NoBuild`; `tools/run_visual_probe_validation.ps1 -NoBuild` | All five enabled runtime IBL environments pass. Screenshot negative gates pass black/white/saturation/edge synthetic cases. Visual probe adds dominant-color and edge checks across all public baseline captures. | Full screenshot comparison set remains incomplete. |
| P3-ORDER-13 | Add RT reflection tuning controls and outlier handling. | PARTIAL | SRC-RT, SRC-UI-P3 | RT smoke/UI contract; `tools/run_rt_firefly_outlier_scene.ps1 -NoBuild` | RT stats/scheduler pass and firefly/outlier gate passes. | Tuning sliders and deliberately overbright clamp scene incomplete. |
| P3-ORDER-14 | Extend material registry and validation for material lab. | DONE_VERIFIED | SRC-MATERIAL | material lab/editor tests | Passed. | More advanced authoring remains. |
| P3-ORDER-15 | Extend advanced material features and lab shader rows. | PARTIAL | SRC-MATERIAL, shaders, scenes | material lab/catalog tests | Foundations pass. | Procedural/authoring controls incomplete. |
| P3-ORDER-16 | Extend lighting rigs and scene rig selection. | PARTIAL | lighting rig controls, scenes | visual matrix/showcase tests | Public scenes report explicit rigs. | UI/command rig selection incomplete. |
| P3-ORDER-17 | Consolidate particle paths, descriptors, budget controls. | PARTIAL | particles, ownership manifest, `src/Scene/ParticleEffectLibrary.cpp` | effects gallery/ownership tests | ECS billboard path, reusable effect descriptors, procedural fallback policy, density scaling, max-instance cap, and particle-disabled zero-cost behavior are validated. | GPU particle public path remains incomplete. |
| P3-ORDER-18 | Add effects showcase scene with particles, emissives, advanced materials, cinematic post. | DONE_VERIFIED | SRC-SCENES | effects showcase smoke | Passed. | Further polish possible. |
| P3-ORDER-19 | Polish public scenes and bookmarks. | PARTIAL | SRC-SCENES | visual matrix/showcase tests | Public scenes pass. | Human polish remains. |
| P3-ORDER-20 | Add tolerant golden baselines after stability. | PARTIAL | `visual_baselines.json`, visual baseline tests | visual baseline contract; visual probe validation | Metric-tolerance manifest, runtime baseline contract, and all-case visual probe pass. | Full committed image comparison is not implemented. |
| P3-ORDER-21 | Extract pass-owned resource bundles touched by work. | PARTIAL | SRC-STATE, ownership manifest | renderer ownership tests | Selected boundaries pass. | More bundles remain. |
| P3-ORDER-22 | Run full release, visual, effects validation. | DONE_VERIFIED | tools scripts | release gate | Latest full gate passed. | None for current suite. |
| P3-ORDER-23 | Update README/release readiness with exact commands/logs. | DONE_VERIFIED | SRC-DOCS | documentation inspection | README and release readiness point at latest gate. | Keep updated. |

## Phase 3 Definition Of Done Audit

| ID | Definition of done item | Status | Source/functions | Validation command | Evidence | Remaining work |
|---|---|---|---|---|---|---|
| P3-DOD-01 | Launches cleanly from documented instructions. | PARTIAL | SRC-DOCS, SRC-PREFLIGHT | release gate | Release gate launches. | End-user setup path not fully tested. |
| P3-DOD-02 | Clear graphics settings UI with working sliders and presets. | PARTIAL | SRC-UI-P3 | graphics UI/preset/interaction tests | Contracts, presets, persistence, and runtime settings application pass. | Full slider inventory and native live widget automation incomplete. |
| P3-DOD-03 | Robust environment/IBL library with fallback behavior. | DONE_VERIFIED | SRC-ENV-P3 | manifest/IBL/fallback tests | Manifest/gallery pass; fallback matrix covers safe profile, missing selected environment, missing manifest, missing required physical asset, missing optional physical asset, and explicit no-RT profile. | None for current environment/IBL fallback behavior. |
| P3-DOD-04 | Polished public showcase scenes with camera bookmarks. | PARTIAL | SRC-SCENES | visual matrix/showcase tests | Scene gates pass. | Subjective polish/final baselines incomplete. |
| P3-DOD-05 | Material presets and validation strong enough for public scenes. | DONE_VERIFIED | SRC-MATERIAL | material lab/editor/RT smokes | Public scene material validation passes. | More presets optional. |
| P3-DOD-06 | Advanced material shaders for clearcoat, anisotropy, wetness, sheen/subsurface approximation, emissive bloom. | PARTIAL | SRC-MATERIAL, shaders | material lab/catalog tests | Foundation features are detected/validated. | Full framework/controls/procedural masks incomplete. |
| P3-DOD-07 | Cinematic lighting rigs selectable from scenes, UI, and validation. | PARTIAL | scenes/lighting controls | visual matrix/showcase tests | Scene/validation selection works. | UI/command selection incomplete. |
| P3-DOD-08 | Controlled particle system with reusable effects and budget-aware quality scaling. | DONE_VERIFIED | `Renderer_Particles.cpp`, `RendererParticleState.h`, `src/Scene/ParticleEffectLibrary.cpp`, `tools/run_effects_gallery_tests.ps1`, `tools/run_particle_disabled_zero_cost.ps1` | `tools/run_effects_gallery_tests.ps1 -NoBuild`; `tools/run_particle_disabled_zero_cost.ps1 -NoBuild`; `tools/run_phase3_visual_matrix.ps1 -NoBuild` | Runtime effects gallery passed with eight reusable public effects and live submitted particles; density/max-instance budget controls are active; particle-disabled profile reports zero planned/executed/live/submitted/allocation cost; Phase 3 visual matrix passed. | None for the public ECS billboard particle system. GPU particle simulation remains tracked separately. |
| P3-DOD-09 | Cinematic post controls improve scenes without hiding correctness. | PARTIAL | post state/shaders/effects scene | effects showcase smoke | Vignette/lens dirt foundations pass. | DOF/motion blur/color grade and quality review incomplete. |
| P3-DOD-10 | RT reflection tuning measurable and stable. | PARTIAL | SRC-RT | RT showcase smoke; `tools/run_rt_firefly_outlier_scene.ps1 -NoBuild` | Raw/history metrics stable in current showcase; firefly/outlier gate passes strict raw/history outlier thresholds. | Tuning controls and explicit overbright clamp scene incomplete. |
| P3-DOD-11 | RT scheduling/fallback explained in UI and contracts. | DONE_VERIFIED | GraphicsSettingsWindow, frame contract | graphics UI contract; RT smoke | Scheduler panel/reasons and contract fields pass. | Interactive UI inspection optional. |
| P3-DOD-12 | Settings persist safely. | DONE_VERIFIED | RendererTuningState | persistence tests | Passed. | Migration tests optional. |
| P3-DOD-13 | Passes release validation and Phase 3 visual matrix from clean build. | DONE_VERIFIED | release/matrix scripts | `tools/run_release_validation.ps1` | Latest full clean release gate passed. | None for current suite. |
| P3-DOD-14 | README/release notes match actual scripts and launch flow. | DONE_VERIFIED | SRC-DOCS | documentation inspection | Latest docs updated. | Keep current. |
| P3-DOD-15 | No known descriptor, memory, startup, screenshot regression hidden by test suite. | PARTIAL | tools scripts, SRC-BUDGET, SRC-VISUAL | release gate plus focused gates | Current suite catches many regressions; fallback, screenshot negative, visual probe, particle-disabled, RT outlier, camera-cut, VB debug views, render-graph transient matrix, conductor energy, lighting energy, positive Dreamer runtime, full ownership audit, descriptor/memory stress, and release stdout failure-sentinel detection now exist and pass. | Full committed golden-image comparison and public packaging remain incomplete. |

## Phase 3 Remaining Items

| ID | Item | Status | Why it remains |
|---|---|---|---|
| P3-REM-01 | Full graphics slider inventory. | PARTIAL | Current UI/presets cover foundations, not every slider in the blueprint. |
| P3-REM-02 | Interactive UI automation for graphics settings/material editor/HUD beyond static contracts. | PARTIAL | `run_graphics_ui_interaction_smoke.ps1` now verifies runtime settings application through the same state file used by the UI, but native widget mouse/keyboard automation for graphics/material/HUD is still missing. |
| P3-REM-03 | Missing optional/selected environment runtime fallback matrix. | DONE_VERIFIED | Missing selected environment, missing manifest, missing required physical asset, and missing optional physical asset fallback are runtime-tested and frame-contract-visible. |
| P3-REM-04 | Safe-startup/fallback-sky/no-RT visual matrix rows. | DONE_VERIFIED | Phase 3 visual matrix now includes `fallback_matrix`, which runs safe startup, missing selected environment, missing manifest, missing required/optional IBL asset, and explicit no-RT visual fallback cases. |
| P3-REM-05 | Forced black/overexposed screenshot negative tests and edge/dominant-hue metrics. | DONE_VERIFIED | Synthetic black, white, saturated, and edge-heavy negative gates pass; visual probe validates edge structure and dominant-color ratios across all public baseline captures. |
| P3-REM-06 | Full golden/tolerant image comparison workflow. | PARTIAL | Baseline contracts and all-case visual probes exist; committed full image comparison is not implemented by policy. |
| P3-REM-07 | RT reflection tuning sliders and firefly/outlier stress scenes. | PARTIAL | Firefly/outlier gate now passes strict RT showcase raw/history outlier thresholds. Tuning sliders and a deliberately overbright clamp stress scene remain incomplete. |
| P3-REM-08 | GPU particle system as public validated path. | PARTIAL | Current public path is ECS billboard particles. |
| P3-REM-09 | Particle effect descriptor library for dust, sparks, embers, mist, rain, snow, fallback texture. | DONE_VERIFIED | `src/Scene/ParticleEffectLibrary.cpp`, `assets/config/advanced_graphics_catalog.json`, `tools/run_advanced_graphics_catalog_tests.ps1`, `tools/run_effects_gallery_tests.ps1` | `tools/run_advanced_graphics_catalog_tests.ps1`; `tools/run_effects_gallery_tests.ps1 -NoBuild` | Descriptor library and catalog cover dust, sparks, embers, mist, rain, snow, fire, smoke, and procedural billboard fallback; runtime effects gallery passed with the complete public emitter set. | None for the descriptor library. |
| P3-REM-10 | Full cinematic post stack: DOF, motion blur, tone mapper/color-grade presets. | PARTIAL | Bloom/vignette/lens dirt foundations only. |
| P3-REM-11 | Volumetric/atmospheric polish beyond current fog/god-ray settings. | PARTIAL | Current scenes pass, but advanced atmosphere pass is not complete. |
| P3-REM-12 | Lighting rig selection from UI and commands. | PARTIAL | Scene/validation rigs and LLM command selection now work and are runtime-tested; native UI rig selection remains incomplete. |
| P3-REM-13 | Pass-owned resource bundles for every new persistent GPU resource. | PARTIAL | Selected boundaries and exhaustive renderer-member ownership pass; deeper per-pass resource extraction remains incomplete. |
| P3-REM-14 | Public release packaging with setup/install validation outside local smoke scripts. | PARTIAL | README/release gate exists; end-user package flow is not fully tested. |

## Completion Gate

The refactor is not marked complete by this ledger.

Minimum gate before claiming `phase2.md` and `phase3.md` complete:

1. Run the current full release gate from a clean build:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1
   ```

2. Keep the focused gates that closed the previously missing script coverage
   passing:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_visual_probe_validation.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_build_entrypoint_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_repo_hygiene_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_source_list_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_render_graph_boundary_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_visibility_buffer_transition_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_debug_primitive_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_editor_frame_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_llm_renderer_command_smoke.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_dreamer_positive_runtime_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_conductor_energy_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_lighting_energy_budget_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_vegetation_state_contract_tests.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_reflection_probe_contract_tests.ps1
   ```

   No focused gate script is currently missing from this section.

3. Keep the added Phase 3 focused gates passing:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_renderer_full_ownership_audit.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_descriptor_memory_stress_scene.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_vb_debug_views.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_visual_probe_validation.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_temporal_camera_cut_validation.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_render_graph_transient_matrix.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_phase3_fallback_matrix.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_graphics_ui_interaction_smoke.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_screenshot_negative_gates.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_particle_disabled_zero_cost.ps1
   powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_rt_firefly_outlier_scene.ps1
   ```

   These scripts now exist, are wired into `run_release_validation.ps1`, and
   passed individually after the fallback-reporting, camera-cut, render-graph
   transient, full renderer ownership, descriptor/memory stress, lighting-energy,
   VB debug view, and visual probe checkpoints.

4. Decide explicitly whether the following are still Phase 2 requirements or
   are user-deferred:

   - outdoor/sunset beach scene;
   - GPU particles as the public particle path;
   - full Phase 3 graphics slider inventory;
   - full cinematic post stack;

5. Only after the missing gates exist and pass should the status move from
   "release-gated foundation" to "`phase2.md` and `phase3.md` complete."
