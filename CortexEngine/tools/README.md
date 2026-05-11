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
- build entrypoint contract for `rebuild.ps1` / `cmake --build`,
- repository hygiene checks for whitespace and generated artifacts,
- source-list contract checks for CMake renderer split coverage,
- render-graph boundary contract checks,
- visibility-buffer transition-skip ownership checks,
- debug primitive ownership contract checks,
- editor frame path contract checks,
- temporal camera-cut history invalidation smoke,
- full RT showcase smoke,
- visibility-buffer debug view runtime checks,
- render-graph transient alias/no-alias matrix,
- graphics settings persistence, UI contract, and HUD mode checks,
- graphics settings runtime-application smoke,
- LLM/Architect renderer-command runtime smoke,
- graphics preset, showcase scene, material editor, conductor-energy, vegetation-state, and visual baseline contracts,
- descriptor/memory stress scene for the old 1024 persistent-descriptor ceiling,
- visual probe validation across all public baseline cases,
- screenshot negative gates for black/white/saturation/edge regressions,
- Phase 3 visual matrix summary generation,
- renderer ownership, full ownership audit, and fatal error contract checks,
- advanced graphics catalog and effects gallery checks,
- particle-disabled zero-cost runtime check,
- environment manifest and IBL gallery checks,
- Phase 3 environment/RT fallback matrix,
- RT reflection firefly/outlier check,
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
powershell -ExecutionPolicy Bypass -File tools/run_graphics_ui_interaction_smoke.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_llm_renderer_command_smoke.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_hud_mode_contract_tests.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_material_editor_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_conductor_energy_contract_tests.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_vegetation_state_contract_tests.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_build_entrypoint_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_repo_hygiene_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_source_list_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_render_graph_boundary_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_visibility_buffer_transition_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_debug_primitive_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_editor_frame_contract_tests.ps1
powershell -ExecutionPolicy Bypass -File tools/run_temporal_camera_cut_validation.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_vb_debug_views.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_render_graph_transient_matrix.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_phase3_visual_matrix.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_effects_gallery_tests.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_phase3_fallback_matrix.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_descriptor_memory_stress_scene.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_visual_probe_validation.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_particle_disabled_zero_cost.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_screenshot_negative_gates.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_rt_firefly_outlier_scene.ps1 -NoBuild
powershell -ExecutionPolicy Bypass -File tools/run_renderer_full_ownership_audit.ps1
```

The UI contract test verifies the unified graphics settings window is compiled,
initialized, bound to F8/ESC, and backed by `RendererTuningState`. The visual
matrix wraps temporal validation, RT showcase, Material Lab, Glass and Water
Courtyard, Effects Showcase, and the IBL gallery, then writes JSON and Markdown
summaries under `build/bin/logs/runs`. Add new public scenes to this matrix only
after their camera, lighting, and IBL choices are stable enough to avoid churn.
The LLM renderer-command smoke runs Architect in deterministic mock mode, queues
a startup `modify_renderer` command, and verifies the renderer applier updates
exposure, shadows, fog, water, and lighting-rig state in the frame contract.
The HUD mode contract verifies the Phase 3 clean-HUD modes, F7 cycling, and
`--hud` / `CORTEX_HUD_MODE` automation path; it runs short off/full-debug cases
and checks the generated frame report.
The material editor contract verifies the focused-entity material preset
dropdown, metallic/roughness sliders, validation status, and
`ModifyMaterialCommand` apply path.
The conductor-energy contract verifies forward/deferred shader energy split
invariants and reruns Material Lab to prove full-metal conductors remain present
without overbright visual statistics.
The vegetation-state contract verifies the extracted vegetation state bundle,
frame-contract reporting, and the explicitly dormant public draw path after
world/outdoor vegetation rendering was deferred.

The build entrypoint contract verifies that local rebuilds and release
validation use `rebuild.ps1` / `cmake --build`, with Visual Studio environment
import guarded by `vswhere` and `VsDevCmd.bat`, instead of calling raw Ninja from
an unprepared shell.

The repository hygiene check runs `git diff --check`, verifies that generated
build/cache/log directories are not tracked, and checks that `.gitignore`
contains the required local artifact guards.

The source-list contract verifies that explicit CMake source entries exist,
are not duplicated, cover every current `src/Graphics/Renderer*.cpp` split
file, and do not pull temporary/backup source files into the engine target.

The render-graph boundary contract verifies that the transient validation
matrix still exercises its environment controls, that the validation module is
wired through `RenderGraphValidationPass`, and that visibility-buffer staged and
legacy graph boundaries still expose their expected graph passes and fallback
accounting.

The visibility-buffer transition contract verifies that graph-owned VB stages
use the `VisibilityBufferRenderer` resource-state snapshot and transition-skip
controls consistently, then write final states back from the render graph.

The debug primitive ownership contract verifies that debug-line storage and GPU
resources live in `RendererDebugLineState`, while `Renderer` exposes only the
debug-line API surface and frame-contract draw counters.

The editor frame contract verifies that `Renderer_EditorHooks.cpp` remains the
editor-specific renderer boundary and that `EngineEditorMode` uses the explicit
frame sequence instead of calling the monolithic renderer path.

The temporal camera-cut validation smoke uses the RT Showcase camera bookmarks
to jump from `hero` to `reflection_closeup` during the run, then asserts that
RT shadow, reflection, and GI histories report `camera_cut`, reseed, and remain
resource-valid in the final frame contract.

The render-graph transient matrix reruns RT Showcase with the graph transient
validation pass enabled, then compares aliasing-on, aliasing-off, and
bloom-transients-disabled modes through frame-contract render-graph counters.

The VB debug view validation reruns RT Showcase through targeted visibility
buffer debug modes for depth and material albedo, then checks that each capture
uses the requested debug view and produces non-empty image statistics.

The descriptor/memory stress scene reruns the descriptor-heavy RT showcase path
and asserts the historical 1024 persistent-descriptor ceiling, staging budget,
transient descriptor balance, render memory budgets, and raw/history RT signal.

The visual probe validation runs every public visual baseline case, then checks
the captured BMPs for nontrivial edge structure and dominant-color failure
modes in addition to the baseline luma/saturation metrics.

The full renderer ownership audit enumerates every `Renderer` member and
requires it to remain a named state/service aggregate, with no loose GPU
resources or descriptor handles reintroduced directly in `Renderer.h`.

The graphics UI interaction smoke uses the same `RendererTuningState` file
format as the graphics window save/load path, forces that state into a runtime
smoke via `CORTEX_LOAD_USER_GRAPHICS_SETTINGS=1`, and asserts that the frame
contract reflects the applied controls. It is a runtime control-surface gate,
not a mouse/keyboard automation test for the native Win32 widgets.

The effects gallery test uses the Effects Showcase scene and asserts that the
advanced graphics catalog is release-foundation validated, the particle
contract is present, the particles pass executed, particles were submitted, and
cinematic post was active without breaking visual or budget gates.

The Phase 3 fallback matrix verifies safe startup, no-RT startup, and explicit
missing-environment fallback reporting. The particle-disabled zero-cost check
launches Effects Showcase under `safe_startup` and requires zero planned,
executed, live, submitted, and allocated particle work. The screenshot negative
gate generates synthetic black, white, saturated, and edge-heavy BMPs to prove
the image-stat thresholds catch common broken captures. The RT firefly/outlier
gate reruns RT Showcase with strict raw/history reflection outlier limits.

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
