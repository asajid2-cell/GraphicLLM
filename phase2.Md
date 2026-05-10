# Cortex Renderer Phase 2 Plan

This document is the live continuation plan for Cortex as a rendering engine.
It replaces the older stale roadmap entries that described bindless rendering,
GPU culling, render graph, visibility buffer rendering, and async compute as
not started. Those systems now exist in the codebase, but most are still in a
partial integration state. Phase 2 is the work required to turn the current
feature set into a coherent, mature renderer.

The target is not to remove advanced features. Ray tracing, visibility-buffer
rendering, GPU culling, HZB occlusion, TAA, SSR, SSAO, bloom, IBL, fog,
god-rays, water, transparency, particles, and eventual outdoor scenes should
all remain first-class. The work left is to make them correct, measurable,
budgeted, and cleanly owned.

## Current State Snapshot

Last verified checkpoint:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/rebuild.ps1 -Config Release
C:\Program Files\Ninja\ninja.exe -C CortexEngine/build -n bin/CortexEngine.exe
D3D12 debug-layer default RT showcase smoke
Forced minimal frame smoke with CORTEX_FORCE_MINIMAL_FRAME=1
tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs
tools/run_budget_profile_matrix.ps1 -NoBuild -TemporalRuns 1 -MaxParallel 1
tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
Default-on bloom transient debug-layer smoke
```

RT showcase smoke passed with:

```text
frames=33
debug_layer_errors=0
gpu_ms=1.571/16.7
dxgi_mb=403.95/512
est_mb=190.52/256
rt_mb=114.63/160
write_mb=107.75/128
luma=71.55 center_luma=75.62
startup_realloc=0
temporal_mask=valid pixels=665856 accepted=1.000
temporal_diff=mean=0.048/2.5 changed=0.001/0.08
surface_debug=view=41 colorful=0.358 nonblack=1.000
material_issues=0
rt_budget=8gb_balanced
```

Budget profile matrix passed after the reproducible script rebuild:

```text
default: gpu_ms=1.588/16.7 dxgi_mb=403.95/512 est_mb=190.52/256 rt_mb=114.63/160 material_issues=0
4gb_low: gpu_ms=1.872/16.7 dxgi_mb=361.95/512 est_mb=148.52/256 rt_mb=72.63/160 material_issues=0
2gb_ultra_low: gpu_ms=1.202/16.7 dxgi_mb=277.86/512 est_mb=78.94/256 rt_mb=36.63/160 material_issues=0
```

Temporal validation passed with `gpu_ms=1.521`, `disocclusion=0.006919`,
`high_motion=0.005242`, `object_motion=0.0731`, `visible=7`, and `warnings=0`.

Bloom transient validation now passes in both no-alias and aliasing modes. The
root bug was not bloom shading; graph-owned placed resources were released on
the next render-graph scope even though multiple graph scopes are recorded into
one still-open frame command list. Transient resources are now retired through
the deferred GPU deletion queue. With aliasing enabled, the bloom transient
heap reports `transients=5`, `heapUsed=1.44 MB`, `heapSize=16.00 MB`,
`aliasBarriers=2`, and clean shutdown. Bloom transient intermediates are now
default-on; `CORTEX_DISABLE_BLOOM_TRANSIENTS=1` is the compatibility escape
hatch.

Current build state: the renderer validation build is reproducible through the
checked-in rebuild script, with the local llama.cpp backend disabled by default
for renderer-focused builds. The CMake cache is pinned to
`C:\Program Files\Ninja\ninja.exe`; using that same Ninja reports
`ninja: no work to do` for `bin/CortexEngine.exe` after the script rebuild.
Raw Ninja should not be used from an unprepared shell because it lacks the
Visual Studio include/lib environment imported by the project scripts.

## Progress Ledger

```text
Pass 1 - Finish Renderer Split: COMPLETE
Verified by Release build and RT showcase smoke.
Renderer.cpp reduced from 1938 lines to 5 lines.

Pass 2 - Frame Feature Plan And Render Context: COMPLETE
Verified by Release build and RT showcase smoke.
Frame execution now uses a single FrameFeaturePlan, and FrameContract reports
features, planned_features, and executed_features separately.

Pass 3A - Renderer Scene Snapshot Foundation: COMPLETE
Verified by Release build and RT showcase smoke.
Renderer now builds a deterministic per-frame RendererSceneSnapshot; frame
contract renderable/material stats, depth prepass, and shadow pass consume the
snapshot.

Pass 3B - Snapshot-Driven Visibility Collection: COMPLETE
Verified by Release build and RT showcase smoke.
Visibility-buffer collection now uses RendererSceneSnapshot ordering and depth
classes, and frame contract warnings report snapshot/VB/GPU-culling instance
count drift.

Pass 3C - Snapshot-Driven RT TLAS Input: COMPLETE
Verified by Release build and RT showcase smoke.
RT TLAS construction now consumes snapshot-derived build inputs during normal
frames, keeps the old registry path as an editor/debug fallback, refreshes
end-of-frame RT stats, and warns on snapshot/RT candidate drift.

Pass 3D - Snapshot-Driven Forward And Fallback Draw Collection: COMPLETE
Verified by Release build and RT showcase smoke.
Forward, overlay, water, transparent, GPU-culling, and indirect fallback draw
collection now consume RendererSceneSnapshot buckets instead of rewalking and
reclassifying renderable ECS views during normal frame execution.

Pass 4A - Render Graph Adapter And Frame Diagnostics: COMPLETE
Verified by Release build and RT showcase smoke.
Shadow render-graph setup moved out of `Renderer::Render` into a reusable
adapter method, and frame contracts now expose render-graph execution counts,
pass counts, barrier counts, culled pass counts, and fallback counts.

Pass 4B - End-Frame Graph Adapter Extraction: COMPLETE
Verified by Release build and RT showcase smoke.
End-of-frame HZB/post-process graph setup moved out of `Renderer::Render` into
`ExecuteEndFrameInRenderGraph`, with explicit input/result structures and
frame-contract warnings for graph pass records without active graph execution
or graph fallback execution.

Pass 4C - Depth Prepass Render Graph Adapter: COMPLETE
Verified by Release build and RT showcase smoke.
Depth prepass now runs through `ExecuteDepthPrepassInRenderGraph`, imports the
persistent depth buffer with the renderer's tracked state, exports the final
depth state back to the renderer, and reports `DepthPrepass` as graph-owned in
the frame contract with legacy barrier fallback preserved.

Pass 4D - Visibility Buffer Graph Boundary: COMPLETE
Verified by Release build and RT showcase smoke.
Visibility-buffer frame execution now runs through
`ExecuteVisibilityBufferInRenderGraph`, declares the external depth/HDR/shadow
and RT inputs at the graph boundary, preserves the existing direct path as a
fallback, and reports `VisibilityBuffer` as graph-owned with no fallback in the
RT showcase. The VB renderer still owns its internal visibility/resolve/light
resource transitions; splitting those into separate graph nodes is the next
render-graph task.

Pass 4E - Visibility Buffer Stage Split: COMPLETE
Verified by Release build and RT showcase smoke.
`RenderVisibilityBufferPath` now delegates to named stages for debug-view
selection, GPU cull-mask resolution, first-frame diagnostics, visibility
raster, material resolve/debug blits, and deferred lighting. The monolithic VB
body was removed after the staged path built and passed smoke. This preserves
behavior while creating the function boundaries needed for separate graph
nodes.

Pass 4F - Visibility Buffer Resource State Snapshot: COMPLETE
Verified by Release build and RT showcase smoke.
`VisibilityBufferRenderer` now exposes its persistent output resources for BRDF
LUT and clustered lighting plus a `ResourceStateSnapshot` API that captures and
restores visibility, G-buffer, BRDF LUT, and cluster buffer states. This is the
resource contract needed before the renderer graph can import/export VB
internals directly.

Pass 4G - Visibility Buffer Transition Skip Controls: COMPLETE
Verified by Release build and RT showcase smoke.
`VisibilityBufferRenderer` now exposes explicit transition skip controls for
visibility, material resolve, debug blits, clustered-light buffers, BRDF LUT,
and deferred lighting. This lets graph-owned passes suppress duplicate internal
barriers only when the graph has already moved resources into the required
state.

Pass 4H - Visibility Buffer Internal Graph Nodes: COMPLETE
Verified by Release build and RT showcase smoke.
The normal non-debug visibility-buffer path now runs as graph nodes for
`VBClear`, `VBVisibility`, `VBMaterialResolve`, and `VBDeferredLighting`.
The graph imports/exports the visibility buffer, depth buffer, HDR target, and
VB G-buffer resources directly, while VB debug blits remain on the legacy graph
boundary until their resources are declared explicitly. BRDF LUT generation and
clustered light-list construction are still internal to deferred lighting and
are the next VB graph ownership target.

Pass 4I - VB Lighting Graph Resources And Subpass Records: COMPLETE
Verified by Release build and RT showcase smoke.
BRDF LUT generation is now exposed as a graph-scheduled compute node when the
lookup texture is not ready, and clustered light-list construction now runs as
`VBClusteredLights` before deferred lighting. `VBDeferredLighting` consumes the
BRDF LUT and cluster buffers as graph reads instead of hiding those transitions
inside the lighting callback. Frame contracts now report the VB subpasses
(`VBClear`, `VBVisibility`, `VBMaterialResolve`, `VBClusteredLights`, and
`VBDeferredLighting`) as separate graph-owned pass records, raising the last
verified pass-record count from 6 to 11.

Pass 4J - VB Debug Graph Paths And Mesh Table Contract: COMPLETE
Verified by Release build, targeted VB debug captures, and RT showcase smoke.
Visibility, depth, and G-buffer VB debug views now use graph-owned
`VBDebugBlit` execution instead of leaving debug presentation on the legacy VB
boundary. The pass declares HDR, depth, visibility, and the selected G-buffer
resource explicitly. The debug capture sweep found a stale VB motion-vector
mesh-table dependency when material resolve was skipped; the mesh table upload
is now a first-class VB contract shared by material resolve and motion vectors.
Targeted debug views 34 and 35 captured successfully with zero graph fallbacks
and zero health warnings, and the normal RT showcase smoke remains at
`fallbacks=0`, `gpu_ms=1.375`, `dxgi_mb=382.63`, and `est_mb=187.85`.

Pass 4K - VB Graph Helper And Motion Vector Graph Adapter: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
The staged VB graph path now imports its common resource surface through
`VisibilityBufferGraphResources`, reducing duplicate normal/debug declarations
and making VB resource ownership easier to audit. Motion vectors now execute
through `ExecuteMotionVectorsInRenderGraph`: the VB path is a graph-owned
compute pass that reads the visibility buffer and writes the velocity UAV, and
the camera-only path is a graph-owned graphics pass that reads depth and writes
the velocity target. Normal RT showcase smoke reports `executions=5`,
`graph_passes=22`, `barriers=58`, `pass_records=12`, and `fallbacks=0`.
Targeted VB depth debug reports `executions=5`, `graph_passes=20`,
`barriers=40`, `pass_records=10`, `fallbacks=0`, and `warnings=0`.

Pass 4L - TAA And SSAO Graph Ownership: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
TAA is now split into explicit seed, resolve, HDR copy-back, and history
copy stages, and `ExecuteTAAInRenderGraph` declares HDR, history,
intermediate, velocity, depth, and normal resources. SSAO now executes through
`ExecuteSSAOInRenderGraph` with graph-owned depth and SSAO target transitions
for both graphics and compute paths. Normal RT showcase smoke reports
`executions=7`, `graph_passes=28`, `barriers=68`, `pass_records=14`, and
`fallbacks=0`.

Pass 4M - SSR Graph Ownership: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
Screen-space reflections now execute through `ExecuteSSRInRenderGraph`,
declaring HDR, depth, normal-roughness, and SSR color resources explicitly.
The graph finalizes SSR color as a shader resource for post-process instead
of leaving that transition hidden at the composition boundary. Normal RT
showcase smoke reports `executions=8`, `graph_passes=30`, `barriers=69`,
`pass_records=15`, `fallbacks=0`, and `warnings=0`. Targeted VB depth debug
reports `executions=8`, `graph_passes=28`, `barriers=51`, `pass_records=13`,
`fallbacks=0`, and `warnings=0`.

Pass 4N - Bloom Stage Split And Graph Ownership: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
Bloom is now split into explicit downsample, blur, composite, copy-combined,
and finalize stages. The composite pass no longer samples from the same
texture it renders into; it accumulates into the ping-pong target and copies
the combined result back to the expected post-process texture. Bloom now
executes through `ExecuteBloomInRenderGraph` with separate graph nodes for
each pyramid step. Normal RT showcase smoke reports `executions=9`,
`graph_passes=42`, `barriers=90`, `pass_records=16`, `fallbacks=0`, and
`warnings=0`. Targeted VB depth debug reports `executions=9`,
`graph_passes=40`, `barriers=72`, `pass_records=14`, `fallbacks=0`, and
`warnings=0`.

Pass 4O - End-Frame Resource Contract Tightening: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
The end-frame render graph now reuses the existing depth resource handle when
HZB and post-process run in the same graph, preventing duplicate graph
resources for the same D3D12 depth texture. Post-process VB G-buffer imports
now use `VisibilityBufferRenderer::ResourceStateSnapshot` and write the final
states back to the visibility-buffer tracker instead of assuming those
resources are already shader-readable. Normal RT showcase smoke remains at
`executions=9`, `graph_passes=42`, `barriers=90`, `pass_records=16`,
`fallbacks=0`, and `warnings=0`. Targeted VB depth debug remains at
`executions=9`, `graph_passes=40`, `barriers=72`, `pass_records=14`,
`fallbacks=0`, and `warnings=0`.

Pass 5A - Material Model Foundation: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
Forward, GPU-driven, and visibility-buffer material uploads now use a shared
CPU-side `MaterialModel` and resolver in compatibility mode.

Pass 5B - Material Validation And RT Material Bridge: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB debug capture.
Renderable classification, surface classification, forward/VB material
constants, and compact RT material records now share the same preset registry
and material resolver. Frame contracts report material validation counters and
the smoke gate fails on validation warnings/errors.

Pass 6A - RT Scheduler Scaffold And Denoise Contract: COMPLETE
Verified by Release build, RT showcase smoke, and targeted VB depth debug
capture. RT dispatch decisions now come from an explicit `RTFramePlan` built by
`RTScheduler`. The scheduler records budget profile, BLAS build budgets, TLAS
instance caps, reduced-resolution targets, update cadence, dispatch enablement,
and denoise intent in the frame contract. The DXR context consumes the
scheduled BLAS total and per-frame build budgets instead of fixed local
constants. The frame pass list now includes an explicit `RTDenoise` contract
for shadow, reflection, and GI history resolve ownership.
Normal RT showcase smoke reports `rt_budget=8gb_balanced`, `gpu_ms=1.406`,
`dxgi_mb=382.63`, `est_mb=187.85`, `fallbacks=0`, and `warnings=0`.
Targeted VB depth debug reports `rt_scheduler=true`,
`rt_dispatches=Strue/Rtrue/GItrue`, `rt_denoise=Strue/Rtrue/GItrue`,
`graph_passes=40`, `barriers=72`, `fallbacks=0`, and `warnings=0`.

Pass 6B - RT Denoiser And Low-Memory Validation: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, and forced 2 GB RT budget smoke. RT history ownership moved from simple
end-frame copies to a dedicated `RTDenoiser` compute module. Shadow,
reflection, and GI histories now have UAV descriptors and are written through
spatial/temporal denoise shaders using depth, normal/roughness, velocity,
current RT buffers, and prior histories. Frame contracts report denoiser
execution, pass count, depth/normal rejection, velocity reprojection, and per
signal accumulation alpha. Smoke validation now fails if the RT denoiser is
missing, skipped, or leaks transient descriptors. Default smoke reports
`gpu_ms=1.869`, `rt_budget=8gb_balanced`, `transient_delta=0`, `fallbacks=0`,
and `warnings=0`. Forced low-memory smoke reports `rt_budget=4gb_low` and
`rt_budget=2gb_ultra_low` without disabling RT features.

Pass 7A - Temporal Manager And RT Reprojection Correctness: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, and forced 2 GB RT budget smoke. TAA, RT shadow, RT reflection, and RT
GI histories now flow through `TemporalManager` records instead of duplicated
renderer booleans. Frame contracts report whether each history is valid,
seeded, why it was invalidated, which rejection mode it uses, its accumulation
alpha, and whether it uses depth/normal rejection, velocity reprojection, and
disocclusion rejection. The RT denoiser now uses the same velocity sign as TAA
(`current_uv + velocity`) and suppresses history on depth, normal, out-of-bounds
reprojection, and high-motion disocclusion. Smoke validation now fails if a
reprojecting history does not report disocclusion rejection. Default smoke
reports `gpu_ms=1.476`, `rt_budget=8gb_balanced`, `transient_delta=0`,
`temporal_diff=mean=0.002/2.5 changed=0.000/0.08`, `fallbacks=0`, and
`warnings=0`. Forced low-memory smoke reports `rt_budget=4gb_low` at
`gpu_ms=1.525` and `rt_budget=2gb_ultra_low` at `gpu_ms=2.072` without
disabling RT features.

Pass 7B - Shared Temporal Rejection Mask: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, and forced 2 GB RT budget smoke. The renderer now creates a full
resolution `temporal_rejection_mask` resource and builds it after motion
vectors from depth, normal/roughness, and velocity. TAA and RT denoising now
consume the same accepted-history multiplier instead of deriving fully separate
disocclusion heuristics. The mask stores accepted history, disocclusion
rejection, high-motion rejection, and in-bounds reprojection channels, and debug
view 42 visualizes the mask. Frame contracts now report a
`TemporalRejectionMask` pass and a `temporal_rejection_mask` resource, and smoke
validation fails if the pass/resource is missing or if RT denoising does not
read the mask. Default smoke reports `gpu_ms=1.964`,
`rt_budget=8gb_balanced`, `transient_delta=0`,
`temporal_diff=mean=0.124/2.5 changed=0.002/0.08`, `fallbacks=0`, and
`warnings=0`. Forced low-memory smoke reports `rt_budget=4gb_low` at
`gpu_ms=1.514` and `rt_budget=2gb_ultra_low` at `gpu_ms=1.372` without
disabling RT features.

Pass 7C - Motion Vector History Contract: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, and forced 2 GB RT budget smoke. Visibility-buffer previous world
matrices are now renderer-owned state instead of a function-static map. Scene
switches clear previous transform history, entity lifetime pruning removes
stale transform records, and the frame contract reports whether motion vectors
were planned, executed, visibility-buffer per-object, or camera-only fallback.
The contract also reports VB motion-vector instance/mesh counts, previous vs
seeded previous-world matrices, pruned transform records, and max object
translation delta. Validation now warns if VB rendering runs without VB motion
vectors or if previous-transform accounting does not match submitted instances.
RT showcase smoke now fails if motion vectors silently fall back to camera-only
mode. Default smoke reports `gpu_ms=1.496`, `rt_budget=8gb_balanced`,
`transient_delta=0`, `temporal_diff=mean=0.076/2.5 changed=0.001/0.08`,
`motion_vectors=vb_per_object`, `fallbacks=0`, and `warnings=0`. Forced
low-memory smoke reports `rt_budget=4gb_low` at `gpu_ms=1.517` and
`rt_budget=2gb_ultra_low` at `gpu_ms=1.382` without disabling RT features.

Pass 7D - Temporal Mask RenderGraph Ownership: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, and forced 2 GB RT budget smoke. Temporal rejection mask construction
now executes through `ExecuteTemporalRejectionMaskInRenderGraph`, importing
depth, velocity, the selected normal/roughness source, and the
`temporal_rejection_mask` resource into the graph. The old manual barrier path
remains as fallback, but smoke validation now fails if the mask pass is not
graph-owned or if it uses fallback. The pass record reports
`render_graph=true`, reads `depth`, `vb_gbuffer_normal_roughness`, and
`velocity`, and writes `temporal_rejection_mask`. Default smoke reports
`gpu_ms=1.520`, `rt_budget=8gb_balanced`, `graph_executions=10`,
`graph_passes=43`, `pass_records=17`, `transient_delta=0`,
`temporal_diff=mean=0.036/2.5 changed=0.001/0.08`, `fallbacks=0`, and
`warnings=0`. Forced low-memory smoke reports `rt_budget=4gb_low` at
`gpu_ms=1.357` and `rt_budget=2gb_ultra_low` at `gpu_ms=1.951` without
disabling RT features.

Pass 7E - Temporal Mask Statistics Contract: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, and forced 2 GB RT budget smoke. The temporal rejection mask now has a
GPU reduction path with clear/reduce compute entries, a tiny UAV stats buffer,
triple-buffered readback, and delayed fence-safe contract publication. Frame
contracts now report `temporal_mask.valid`, `built`, `sample_frame`,
`pixel_count`, `accepted_ratio`, `disocclusion_ratio`, `high_motion_ratio`,
`out_of_bounds_ratio`, and `readback_latency_frames`. Smoke validation now
fails if temporal mask statistics or the `temporal_rejection_mask_stats`
resource are missing, invalid, out of range, or leaking transient descriptors.
The stats dispatch uses persistent per-frame descriptor tables so the
diagnostic path preserves `transient_delta=0`. Default smoke reports
`gpu_ms=2.133`, `rt_budget=8gb_balanced`, `temporal_mask=valid`,
`accepted=1.000`, `pixel_count=665856`, `latency=3`, `transient_delta=0`,
`fallbacks=0`, and `warnings=0`. Forced low-memory smoke reports
`rt_budget=4gb_low` at `gpu_ms=2.092` and `rt_budget=2gb_ultra_low` at
`gpu_ms=1.465` without disabling RT features.

Pass 7F - Temporal Motion Validation Scene: COMPLETE
Verified by Release build, dedicated temporal validation smoke, and default RT
showcase smoke. The engine now has a `temporal_validation` scene that exercises
VB per-object motion vectors, RT, TAA, temporal rejection, water, emissive
surfaces, metallic surfaces, and double-sided alpha-tested geometry under
deterministic motion. The temporal rejection mask high-motion ramp now begins
at 2 pixels instead of 8 pixels so TAA and RT denoising reject moving history
before ghosting becomes obvious. `tools/run_temporal_validation_smoke.ps1`
fails if the scene loses temporal mask stats, motion-vector execution,
required surface classes, graph-owned mask construction, stats pass execution,
or warning-free frame contracts. Temporal validation smoke reports
`gpu_ms=1.848`, `disocclusion=0.006788`, `high_motion=0.005213`,
`object_motion=0.0731`, `visible=7`, and `warnings=0`. Default RT showcase
smoke still passes after the mask threshold change at `gpu_ms=1.463`,
`temporal_diff=mean=0.154/2.5 changed=0.002/0.08`,
`transient_delta=0`, and `warnings=0`.

Pass 8A - Shared Renderer Budget Planner: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB RT budget
smoke, forced 2 GB RT budget smoke, and temporal validation smoke. Budget
profile selection moved into `BudgetPlanner`, which now resolves
`2gb_ultra_low`, `4gb_low`, `8gb_balanced`, and `high` profiles from adapter
memory or explicit environment override. `RTScheduler` now consumes the shared
renderer budget instead of owning profile selection internally. Frame contracts
now report a top-level `renderer_budget` block with profile, override state,
dedicated memory, target render scale, render extents, SSAO divisor, shadow map
size, bloom levels, IBL residency limit, asset budgets, RT resolution scale,
and RT cadence. Contract validation warns on missing or invalid budget plans,
and the RT/temporal smoke scripts now fail if renderer budget reporting
regresses. Default RT showcase smoke reports `gpu_ms=1.563`,
`rt_budget=8gb_balanced`, `transient_delta=0`, and `warnings=0`. Forced
low-memory smokes report `rt_budget=4gb_low` at `gpu_ms=1.569` and
`rt_budget=2gb_ultra_low` at `gpu_ms=1.513` without disabling RT. Temporal
validation smoke reports `gpu_ms=1.867`, `disocclusion=0.006736`,
`high_motion=0.005204`, and `warnings=0`.

Pass 8B - Budget-Applied Render Target Sizing: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB budget
smoke, forced 2 GB budget smoke, and temporal validation smoke. Render-scale
requests are now capped by `BudgetPlanner`, so scene setup cannot push a
low-memory profile above its intended internal resolution. Shadow map
allocation, SSAO resolution, bloom pyramid level count, RT reflection target
size, and RT GI target size now come from the shared renderer budget instead
of independent constants. The 2 GB profile keeps RT, SSAO, bloom, shadows,
TAA, SSR, and VB enabled while allocating a 0.70 scale frame, `1024x1024`
shadow map, quarter-resolution SSAO, two bloom levels, and one-third
resolution RT reflection/GI. The 4 GB profile allocates a `1536x1536` shadow
map and keeps the 0.85 scale RT showcase path. Default smoke reports
`rt_budget=8gb_balanced`, `gpu_ms=1.881`, `est_mb=187.85`,
`startup_realloc=0`, and `warnings=0`. Forced 4 GB smoke reports
`rt_budget=4gb_low`, `gpu_ms=1.547`, `est_mb=145.85`,
`startup_realloc=0`, and `warnings=0`. Forced 2 GB smoke reports
`rt_budget=2gb_ultra_low`, `gpu_ms=1.160`, `est_mb=100.27`,
`rt_mb=36.63`, `write_mb=68.52`, `startup_realloc=0`, and `warnings=0`.
Temporal validation smoke reports `gpu_ms=2.067`, `disocclusion=0.006774`,
`high_motion=0.005211`, and `warnings=0`.

Pass 8C - Asset Residency Budget Enforcement: COMPLETE
Verified by Release build, default RT showcase smoke, forced 4 GB budget
smoke, forced 2 GB budget smoke, temporal validation smoke, and frame-contract
JSON inspection. The asset registry now consumes texture, environment,
geometry, and RT-structure budgets directly from `BudgetPlanner` instead of
only reporting static soft limits. Frame contracts now include an
`asset_memory` block with resident texture, environment, geometry, and
RT-structure byte estimates plus per-category budget-exceeded flags. Contract
validation warns on texture, environment, geometry, or RT-structure over-budget
conditions. Environment startup and deferred loading now obey
`iblResidentEnvironmentLimit` for the active profile, so the renderer no
longer eagerly loads multiple large HDR environments on default/low-memory
profiles. Scene reference rebuilds no longer erase global environment-map
memory from diagnostics; environment maps are unregistered only when the IBL
residency policy evicts them or the environment set is rebuilt.

Default smoke reports `rt_budget=8gb_balanced`, `gpu_ms=1.546`,
`dxgi_mb=387.95`, `est_mb=190.52`, `startup_realloc=0`, and `warnings=0`.
Forced 4 GB smoke reports `rt_budget=4gb_low`, `gpu_ms=1.416`,
`dxgi_mb=345.95`, `est_mb=148.52`, `startup_realloc=0`, and `warnings=0`.
Forced 2 GB smoke reports `rt_budget=2gb_ultra_low`, `gpu_ms=1.901`,
`dxgi_mb=285.86`, `est_mb=102.94`, `rt_mb=36.63`, `write_mb=68.52`,
`startup_realloc=0`, and `warnings=0`. Temporal validation smoke reports
`gpu_ms=1.953`, `disocclusion=0.006765`, `high_motion=0.00521`,
`object_motion=0.0731`, and `warnings=0`. Frame-contract inspection reports
resident IBL memory (`environment_bytes=2796240`) under the active
`environment_budget_bytes=134217728`.

Pass 8D - Isolated Smoke Artifact Runs: COMPLETE
Verified by Release build, isolated RT showcase smoke, parallel budget-profile
matrix smoke, and isolated temporal validation smoke. The engine now honors
`CORTEX_LOG_DIR` for the main run log, frame reports, frame-report history, and
visual validation captures, so harnesses can run without overwriting shared
`build/bin/logs` artifacts. `tools/run_rt_showcase_smoke.ps1` and
`tools/run_temporal_validation_smoke.ps1` now accept explicit `-LogDir` paths
and generated `-IsolatedLogs` paths. RT showcase smoke also accepts
`-SkipSurfaceDebug` for budget-matrix runs that only need profile validation.
`tools/run_budget_profile_matrix.ps1` runs default, forced 4 GB, and forced
2 GB RT showcase smokes with separate per-profile artifact directories under
`build/bin/logs/runs`.

Manual isolated RT showcase smoke reports `rt_budget=8gb_balanced`,
`gpu_ms=1.560`, `dxgi_mb=387.95`, `est_mb=190.52`, `startup_realloc=0`, and
`warnings=0`. The parallel budget-profile matrix passes for `default`,
`4gb_low`, and `2gb_ultra_low`, with each profile writing independent logs,
reports, and captures. Isolated temporal validation smoke reports
`gpu_ms=1.945`, `disocclusion=0.00683`, `high_motion=0.005219`,
`object_motion=0.0731`, `visible=7`, and `warnings=0`.
```

Current renderer shape:

```text
Renderer.cpp                            5 lines
Renderer_RenderGraph.cpp             1866 lines
Renderer_RenderTargets.cpp           1674 lines
Renderer_GPUDriven.cpp               1525 lines
Renderer_PipelineSetup.cpp           1196 lines
Renderer_DebugPrimitives.cpp          933 lines
Renderer_AssetUpload.cpp              913 lines
Renderer_FrameSetup.cpp               876 lines
Renderer_Settings.cpp                 874 lines
Renderer_RenderOrchestration.cpp      858 lines
Renderer_FrameContracts.cpp           780 lines
Renderer_FrameLifecycle.cpp           744 lines
Renderer_LateGeometry.cpp             718 lines
Renderer_ScreenSpace.cpp              658 lines
Renderer_Bloom.cpp                    581 lines
Renderer_Environment.cpp              511 lines
Renderer_Initialization.cpp           497 lines
Renderer_RayTracing.cpp               580 lines
Renderer_Voxel.cpp                    419 lines
Renderer_SSAO.cpp                     355 lines
Renderer_Materials.cpp                349 lines
Renderer_Descriptors.cpp              339 lines
Renderer_Diagnostics.cpp              327 lines
Renderer_PostProcess.cpp              265 lines
RendererSceneSnapshot.cpp             272 lines
Renderer_ShadowPass.cpp               234 lines
Renderer_ForwardPass.cpp              179 lines
Renderer_DepthPasses.cpp              167 lines
RTDenoiser.cpp                        156 lines
RTScheduler.cpp                       155 lines
Renderer_AssetRegistry.cpp            117 lines
Renderer_Shutdown.cpp                 116 lines
Renderer_Synchronization.cpp           84 lines
Renderer_EditorHooks.cpp              63 lines
RTDenoiser.h                           55 lines
```

Major systems that already exist:

- DX12 renderer with triple buffering.
- Bindless resource manager and persistent material descriptor tables.
- Visibility buffer renderer with visibility pass, material resolve, motion
  vectors, clustered lighting, deferred lighting, and debug blits.
- GPU culling and indirect drawing.
- HZB depth pyramid and occlusion culling hooks.
- Render graph with imported resources, pass declarations, automatic barriers,
  subresource state tracking, and partial end-of-frame usage.
- Frame contract reporting, pass records, resource sizing, descriptor deltas,
  renderable class counts, RT stats, culling stats, and validation warnings.
- DXR shadows, RT reflections, and RT diffuse GI buffers.
- SSAO, SSR, TAA, FXAA, bloom, fog, god-ray controls, IBL, water, particles,
  vegetation prototypes, and voxel prototype path.

Main remaining problem:

The renderer has many advanced pieces, but the architecture is still hybrid and
uneven. Some paths are explicit and measured; others are still implicit,
monolithic, or fallback-heavy. Phase 2 is about completing the contracts between
these systems.

## Target State

The mature renderer should look like this:

```text
Engine / Scene
    -> RendererSceneSnapshot
    -> Renderable classification and material resolution
    -> Resource residency and budget planning
    -> Frame graph construction
    -> GPU-driven visibility preparation
    -> Depth, visibility, and G-buffer passes
    -> Lighting, shadows, RT effects, volumetrics
    -> Transparency, water, particles, decals
    -> Temporal reconstruction
    -> Post-process and presentation
    -> Frame report, validation, capture, and budget feedback
```

The renderer should be able to explain every frame:

- what passes ran
- which resources each pass read and wrote
- which queue each pass used
- which histories were valid
- what each feature cost in GPU time and memory
- what renderables were accepted, skipped, culled, or downgraded
- why a fallback path ran
- whether the frame met its visual and memory budgets

## Completion Estimate

Current maturity estimate:

```text
Architecture split and containment:        55%
Frame contracts and diagnostics:           86%
Render graph ownership:                    77%
Visibility-buffer correctness:             64%
Material model correctness:                60%
Ray tracing integration:                   58%
Temporal stability:                        82%
Memory/descriptor budget discipline:       76%
Visual demo maturity:                      28%
Overall Phase 2 completion:                86%
```

The renderer is past the "rough prototype" stage, but it is not yet a mature
engine. The next stretch is mostly correctness, ownership, and quality rather than
just adding more features.

## Work Remaining By System

### 1. Renderer Orchestration

Current state:

- `Renderer.cpp` is much smaller than before but still owns initialization,
  render flow, shutdown, diagnostics, and scene-change reset.
- Pass modules exist, but `Renderer::Render` still directly decides most of
  the frame sequence.
- Feature toggles and environment-variable debug paths are still mixed into the
  render function.

Target state:

- `Renderer.cpp` should become a thin owner of renderer subsystems.
- Frame execution should be represented by an explicit plan object.
- Feature toggles should be evaluated once into a `FrameFeaturePlan`.
- Render orchestration should be readable as a stable sequence of named phases.

Implementation plan:

```cpp
struct FrameFeaturePlan {
    bool useVoxelBackend;
    bool useRayTracing;
    bool useVisibilityBuffer;
    bool useGpuCulling;
    bool runDepthPrepass;
    bool runShadowPass;
    bool runRTShadows;
    bool runRTGI;
    bool runRTReflections;
    bool runMotionVectors;
    bool runTAA;
    bool runSSR;
    bool runSSAO;
    bool runBloom;
    bool runPostProcess;
    bool runDebugLines;
};

struct FrameExecutionContext {
    Scene::ECS_Registry* registry;
    float deltaTime;
    uint64_t frameNumber;
    uint32_t frameIndex;
    FrameFeaturePlan features;
    FrameContract* contract;
};

void Renderer::Render(Scene::ECS_Registry* registry, float deltaTime) {
    FrameExecutionContext ctx = BeginRendererFrame(registry, deltaTime);
    if (!ctx.features.useVoxelBackend) {
        ExecuteClassicFrame(ctx);
    } else {
        ExecuteVoxelFrame(ctx);
    }
    FinishRendererFrame(ctx);
}

void Renderer::ExecuteClassicFrame(FrameExecutionContext& ctx) {
    ExecuteFrameSetup(ctx);
    ExecuteDepthAndRTWarmup(ctx);
    ExecuteShadowPhase(ctx);
    ExecuteMainVisibilityPhase(ctx);
    ExecuteLateGeometryPhase(ctx);
    ExecuteScreenSpacePhase(ctx);
    ExecutePostAndPresentationPhase(ctx);
}
```

Files:

- Create `Renderer_Initialization.cpp`.
- Create `Renderer_Diagnostics.cpp`.
- Create `Renderer_RenderOrchestration.cpp`.
- Keep `Renderer.cpp` as constructor/destructor and thin public API only.

Acceptance gates:

- `Renderer.cpp` under 500 lines.
- `Renderer::Render` under 100 lines.
- No environment-variable parsing inside the central render function.
- Frame contract still records the same pass list as before.
- RT showcase smoke passes.

### 2. Render Graph Ownership

Current state:

- Render graph exists and can import resources, declare dependencies, generate
  barriers, and execute passes.
- It is currently used incrementally for shadows, HZB, and post-process.
- Many passes still perform their own manual barriers and mutate renderer-owned
  state directly.
- Transient resource lifetime and aliasing are not yet the main ownership model.

Target state:

- Render graph should own pass ordering, resource transitions, and eventually
  transient render targets.
- Passes should declare resources rather than reaching into renderer state
  whenever possible.
- Legacy manual barrier paths should remain only as temporary fallback during
  migration.

Implementation plan:

```cpp
class RenderPass {
public:
    virtual const char* Name() const = 0;
    virtual void Declare(RenderGraph& graph, FrameExecutionContext& ctx) = 0;
    virtual void Execute(RGRenderContext& rg, FrameExecutionContext& ctx) = 0;
};

struct RGRenderContext {
    ID3D12GraphicsCommandList* cmd;
    ResourceViewRegistry* views;
    DescriptorFrameAllocator* descriptors;
    FrameConstantsGPU frameConstants;
};

void Renderer::BuildFrameGraph(FrameExecutionContext& ctx) {
    m_renderGraph->BeginFrame();
    ImportPersistentResources(ctx);

    if (ctx.features.runShadowPass) AddPass<ShadowPass>(ctx);
    if (ctx.features.runDepthPrepass) AddPass<DepthPrepass>(ctx);
    if (ctx.features.useVisibilityBuffer) AddPass<VisibilityBufferPass>(ctx);
    else AddPass<ForwardOpaquePass>(ctx);
    if (ctx.features.runRTReflections) AddPass<RTReflectionPass>(ctx);
    if (ctx.features.runMotionVectors) AddPass<MotionVectorPass>(ctx);
    if (ctx.features.runTAA) AddPass<TAAPass>(ctx);
    if (ctx.features.runSSR) AddPass<SSRPass>(ctx);
    if (ctx.features.runSSAO) AddPass<SSAOPass>(ctx);
    if (ctx.features.runBloom) AddPass<BloomPass>(ctx);
    AddPass<PostProcessPass>(ctx);

    m_renderGraph->Compile();
    m_renderGraph->Execute(m_commandList.Get());
    m_renderGraph->EndFrame();
}
```

Migration order:

1. Import all persistent frame resources at the start of the graph.
2. Move depth prepass to graph.
3. Move visibility-buffer path to graph.
4. Move screen-space passes to graph.
5. Move late geometry and transparency to graph.
6. Move RT dispatches into graph nodes with UAV/SRV declarations.
7. Replace manual state members with graph-returned final states.
8. Introduce transient resource descriptors for SSAO, bloom, velocity, RT
   half-resolution buffers, and TAA intermediate.

Acceptance gates:

- Every pass in the frame contract has a matching render graph pass record.
- Manual barriers are absent from pass modules except for explicit intra-pass
  UAV barriers or D3D12-required local synchronization.
- RenderGraph reports pass count, barrier count, queue ownership, transient
  memory, and culled passes.
- Disabling the legacy barrier fallback does not change the RT showcase smoke
  result.

### 3. Renderer Scene Snapshot

Current state:

- Most passes still query the ECS directly.
- Classification exists for depth classes and surface classes.
- GPU culling and visibility buffer rebuild instance lists from ECS each frame.
- Material descriptor prewarm also walks ECS each frame.

Target state:

- Build one immutable per-frame renderer snapshot.
- All passes consume the snapshot rather than independently walking the ECS.
- Snapshot contains stable IDs, transforms, previous transforms, material
  references, mesh references, bounds, render layer, depth class, surface class,
  and RT eligibility.

Implementation plan:

```cpp
struct RenderItem {
    entt::entity entity;
    uint32_t stableId;
    Scene::MeshData* mesh;
    Scene::RenderableComponent* renderable;
    glm::mat4 world;
    glm::mat4 prevWorld;
    Bounds boundsWS;
    RenderableDepthClass depthClass;
    SurfaceClass surfaceClass;
    uint32_t materialId;
    bool visible;
    bool castsShadow;
    bool rtEligible;
};

struct RendererSceneSnapshot {
    std::vector<RenderItem> opaque;
    std::vector<RenderItem> alphaTested;
    std::vector<RenderItem> doubleSided;
    std::vector<RenderItem> transparent;
    std::vector<RenderItem> water;
    std::vector<RenderItem> overlay;
    std::vector<LightItem> lights;
    std::vector<ReflectionProbeItem> probes;
    CameraFrameData camera;
};

RendererSceneSnapshot RendererSceneBuilder::Build(
    Scene::ECS_Registry& registry,
    const PreviousFrameSceneState& previous);
```

Files:

- Create `RendererSceneSnapshot.h/.cpp`.
- Create `RendererSceneBuilder.h/.cpp`.
- Move stable entity ID and previous-center logic out of `Renderer_GPUDriven.cpp`.
- Move classification calls out of pass modules into snapshot build.

Acceptance gates:

- ECS is walked once per frame for renderable collection.
- GPU culling, visibility buffer, depth prepass, shadow pass, RT TLAS build,
  and forward fallback consume the same snapshot.
- Frame contract renderable counts are derived from the snapshot.
- Scene rebuild resets snapshot history deterministically.

### 4. Material System Maturity

Current state:

- `RenderableComponent` has structured scalar fields for albedo, roughness,
  metallic, AO, emissive, normal scale, transmission, IOR, specular, and
  clearcoat.
- Texture slots and descriptor tables exist.
- Surface classification is still partly preset-name driven.
- Raster, visibility-buffer deferred, and RT paths share some material data but
  not a single authoritative material model.

Target state:

- Runtime shading consumes one canonical `MaterialModel`.
- Presets become authoring conveniences only.
- Raster, visibility-buffer deferred lighting, and RT hit shaders receive
  equivalent material information.
- Material debug views prove parameter flow.

Implementation plan:

```cpp
enum class MaterialDomain : uint8_t {
    OpaqueDielectric,
    Metallic,
    Mirror,
    GlassThin,
    Water,
    Emissive,
    Foliage,
    Decal
};

struct MaterialModel {
    MaterialDomain domain;
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    float alphaCutoff;
    float normalScale;
    float occlusionStrength;
    glm::vec3 emissiveColor;
    float emissiveStrength;
    float transmission;
    float ior;
    float clearcoat;
    float clearcoatRoughness;
    float specularFactor;
    glm::vec3 specularColor;
    MaterialTextureSet textures;
    MaterialValidation validation;
};

MaterialModel MaterialResolver::Resolve(
    const Scene::RenderableComponent& renderable,
    const MaterialPresetRegistry& presets);
```

Shader unification:

```text
MaterialModel CPU
    -> MaterialConstants for forward/legacy path
    -> VBMaterialConstants for visibility-buffer path
    -> RTMaterialGPU for ray tracing hit shaders
    -> FrameContract material stats and debug views
```

Implementation steps:

1. Create `MaterialModel.h/.cpp`.
2. Create `MaterialPresetRegistry` and move preset string interpretation there.
3. Replace ad hoc surface classification with `MaterialDomain`.
4. Generate all GPU material structs from `MaterialModel`.
5. Add material validation warnings:
   - roughness outside 0..1
   - metallic outside 0..1
   - alpha blend without transparency class
   - transmission without glass domain
   - emissive strength with black emissive color
   - texture path failed and fallback used
6. Add a material test scene with spheres/planes for every material domain.

Acceptance gates:

- One CPU material resolver feeds forward, VB, and RT.
- Material ball scene shows all material domains.
- Debug views show base color, normal, roughness, metallic, emissive,
  transmission, material domain, and alpha class.
- RT reflections and raster/VB lighting agree on metallic, roughness, emissive,
  glass, and mirror classifications.

### 5. Ray Tracing Integration

Current state:

- DXR context exists and initializes shadow, reflection, and GI pipelines.
- BLAS/TLAS memory is tracked.
- BLAS builds are partially incremental.
- Large meshes can be skipped for BLAS memory pressure.
- RT stats are reported in the frame contract.
- RT reflection/GI buffers are half-resolution and temporally tracked.

Current gaps:

- RT dispatches are still bolted into the frame rather than scheduled through
  the graph.
- RT material model is not fully unified with raster/VB material model.
- Denoising is basic.
- RT GI is closer to a tech demo approximation than mature real-time GI.
- RT work budget is not controlled through a unified per-frame quality policy.

Target state:

- RT has explicit budgets, scheduling, resource contracts, and denoisers.
- BLAS/TLAS updates are bounded by frame budget and memory budget.
- RT effects degrade gracefully by resolution, ray count, update frequency, or
  temporal reuse rather than by silently failing.

Implementation plan:

```cpp
struct RTBudget {
    uint32_t maxBLASBuildsPerFrame;
    uint32_t maxTLASInstances;
    uint32_t reflectionWidth;
    uint32_t reflectionHeight;
    uint32_t giWidth;
    uint32_t giHeight;
    uint32_t shadowRaysPerPixel;
    uint32_t reflectionRaysPerPixel;
    uint32_t giRaysPerPixel;
    uint32_t maxReflectionBounces;
    uint64_t maxASBytes;
};

struct RTFramePlan {
    bool buildTLAS;
    bool dispatchShadows;
    bool dispatchReflections;
    bool dispatchGI;
    bool reuseShadowHistory;
    bool reuseReflectionHistory;
    bool reuseGIHistory;
    RTBudget budget;
};

RTFramePlan RayTracingScheduler::Plan(
    const RendererSceneSnapshot& scene,
    const FrameBudget& frameBudget,
    const FrameHistoryState& history);
```

Pass structure:

```text
RTPrepareAS
    reads: renderer scene snapshot, mesh buffers
    writes: BLAS/TLAS, RT material buffer

RTShadows
    reads: TLAS, depth, frame constants
    writes: rt_shadow_mask

RTReflectionTrace
    reads: TLAS, depth, normal/roughness, material ext buffers
    writes: rt_reflection_halfres

RTGIDiffuseTrace
    reads: TLAS, depth, normal/roughness, material ext buffers
    writes: rt_gi_halfres

RTDenoise
    reads: current RT outputs, previous histories, velocity, depth, normals
    writes: filtered RT outputs and histories
```

Acceptance gates:

- RT has an explicit per-frame budget object recorded in the frame contract.
- TLAS stats match snapshot counts.
- RT material counts match visible RT-eligible renderables.
- RT reflections/GI can be updated at half-rate without flicker.
- RT history invalidates on camera cut, render scale change, material change,
  TLAS topology change, and feature toggles.
- RT showcase smoke includes explicit RT budget and denoiser metrics.

### 6. Temporal Stability

Current state:

- TAA history exists.
- RT shadow, reflection, and GI histories exist.
- Motion vectors exist for fullscreen/camera and visibility-buffer paths.
- History validity is tracked in the frame contract.

Current gaps:

- History ownership is spread across pass modules.
- Motion vectors are still not a complete per-object deformation/motion model.
- TAA resolve and RT denoising are not part of one temporal policy.
- Disocclusion, alpha surfaces, water, particles, and transparent materials need
  stricter rules.

Target state:

- Every temporal feature has a common history contract.
- The engine knows when a history is valid, stale, wrong-sized, scene-invalid,
  camera-invalid, material-invalid, or topology-invalid.
- Temporal filters should never hide popping or create ghost silhouettes.

Implementation plan:

```cpp
enum class HistoryInvalidationReason {
    None,
    NotSeeded,
    RenderScaleChanged,
    CameraCut,
    SceneChanged,
    MaterialChanged,
    FeatureToggled,
    ResourceRecreated,
    TopologyChanged,
    TooMuchMotion
};

struct TemporalResource {
    std::string name;
    ID3D12Resource* current;
    ID3D12Resource* history;
    glm::uvec2 size;
    bool valid;
    uint64_t lastValidFrame;
    HistoryInvalidationReason reason;
};

class TemporalManager {
public:
    void BeginFrame(const CameraFrameData& camera, const RendererSceneSnapshot& scene);
    void RegisterHistory(TemporalResource& history);
    void Invalidate(HistoryInvalidationReason reason);
    bool CanReproject(const TemporalResource& history) const;
    void MarkValid(TemporalResource& history);
};
```

Implementation steps:

1. Create `TemporalManager.h/.cpp`.
2. Move TAA/RT history invalidation state out of `Renderer`.
3. Add object motion source from `RendererSceneSnapshot`.
4. Make velocity generation consume previous transforms from snapshot.
5. Add disocclusion masks for TAA and RT denoisers.
6. Add material-domain-specific temporal behavior:
   - water: lower history weight
   - emissive: clamp history contribution
   - transparent: no opaque velocity unless refraction pass exists
   - particles: no long-history accumulation

Acceptance gates:

- Frame contract histories are emitted from `TemporalManager`.
- TAA ghosting is bounded in a camera-cut test.
- Moving object test produces correct motion vectors.
- Water and particles do not smear through TAA.
- RT denoiser history invalidation is visible in diagnostics.

### 7. Visibility Buffer Correctness

Current state:

- Visibility buffer path is enabled by default and smoke-tested.
- It supports mesh draw grouping, double-sided, alpha-tested variants, material
  resolve, deferred lighting, local lights, reflection probes, and debug blits.
- It can fall back to forward rendering when it fails to produce a frame.

Current gaps:

- It still coexists with a forward fallback path rather than being the clean
  primary opaque path.
- Some mesh/material readiness checks skip objects until uploads complete.
- Transparent, water, decals, particles, and vegetation are not cleanly
  integrated into a full frame graph around the VB path.
- G-buffer ownership between VB and fallback paths still needs stronger
  contracts.

Target state:

- VB is the primary opaque geometry path.
- Forward path is explicitly legacy/fallback and eventually used only for
  unsupported features.
- VB owns opaque depth, material resolve, deferred lighting, and motion vectors.

Implementation plan:

```cpp
struct VisibilityFrameInputs {
    RendererSceneSnapshot* scene;
    GPUCullResult* culling;
    FrameConstantsGPU frame;
    ResourceHandle depth;
    ResourceHandle visibility;
    ResourceHandle gbufferAlbedo;
    ResourceHandle gbufferNormalRoughness;
    ResourceHandle gbufferEmissiveMetallic;
    ResourceHandle materialExt0;
    ResourceHandle materialExt1;
    ResourceHandle materialExt2;
};

void VisibilityBufferPass::Declare(RenderGraph& graph) {
    graph.Write(depth, DepthStencilWrite);
    graph.Write(visibility, RenderTarget);
}

void MaterialResolvePass::Declare(RenderGraph& graph) {
    graph.Read(visibility, ShaderResource);
    graph.Read(depth, ShaderResource | DepthStencilRead);
    graph.Write(gbufferAlbedo, UnorderedAccess);
    graph.Write(gbufferNormalRoughness, UnorderedAccess);
    graph.Write(gbufferEmissiveMetallic, UnorderedAccess);
    graph.Write(materialExt0, UnorderedAccess);
    graph.Write(materialExt1, UnorderedAccess);
    graph.Write(materialExt2, UnorderedAccess);
}
```

Acceptance gates:

- `ForwardSceneFallback` appears only when VB is disabled or a declared
  unsupported condition occurs.
- VB draw counts equal eligible opaque snapshot counts.
- VB debug views show triangle ID, instance ID, material domain, normals, and
  depth ownership.
- VB and forward fallback produce visually comparable material output in a
  fixed material test scene.

### 8. GPU Culling And HZB

Current state:

- GPU culling exists and reports stats.
- HZB exists and can feed VB and GPU culling.
- Culling has freeze/debug controls and conservative guards.

Current gaps:

- GPU culling still rebuilds instance data directly from ECS.
- HZB is built late and can use previous-frame visibility depending on path.
- False occlusion avoidance is implemented but still needs more formal tests.
- Culling budget and correctness gates should be visible in diagnostics.

Target state:

- GPU culling consumes `RendererSceneSnapshot`.
- HZB capture state is part of a formal culling contract.
- Occlusion is conservative by default and never causes geometry popping.

Implementation plan:

```cpp
struct CullingInput {
    RendererSceneSnapshot* scene;
    CameraFrameData camera;
    HZBState hzb;
    bool allowHZBOcclusion;
    bool freeze;
};

struct CullingResult {
    GPUBuffer visibleMask;
    GPUBuffer indirectArgs;
    uint32_t tested;
    uint32_t frustumCulled;
    uint32_t occluded;
    uint32_t visible;
    bool hzbUsed;
    bool conservativeMode;
};

CullingResult GPUCullingSystem::Cull(const CullingInput& input);
```

Acceptance gates:

- Camera-inside-bounds test never occludes the object.
- Thin wall double-sided test does not flicker with HZB enabled.
- HZB age and camera delta are recorded in frame contract.
- Culling can be run in frustum-only mode, HZB mode, and frozen mode with
  deterministic results.

### 9. Memory, Descriptor, And Energy Budgets

Current state:

- Estimated VRAM breakdown exists.
- DXGI memory usage can be logged.
- Descriptor stats are exposed.
- IBL residency limit exists.
- Render scale clamps on high resolution with heavy effects.
- RT acceleration structure memory is tracked.

Current gaps:

- There is no unified budget planner.
- Passes allocate persistent resources eagerly.
- Descriptor heap size is very large and not yet policy-driven.
- Compression, streaming, and texture mip residency are partial.
- Energy use is not directly measured, but bandwidth and pass count can be
  reduced.

Target state:

- Renderer has profiles for 2 GB, 4 GB, 8 GB, and high-end GPUs.
- Profiles preserve feature availability but tune resolution, update cadence,
  and residency.
- The engine can say which quality decision it made and why.

Implementation plan:

```cpp
enum class MemoryTier {
    Tier2GB,
    Tier4GB,
    Tier8GB,
    TierHigh
};

struct RendererBudgetProfile {
    MemoryTier tier;
    float renderScale;
    uint32_t shadowAtlasSize;
    uint32_t maxResidentIBL;
    uint32_t maxMaterialTextureMip;
    uint32_t maxBLASBuildsPerFrame;
    uint64_t maxRTASBytes;
    bool halfResSSAO;
    bool halfResSSR;
    bool halfResRTReflections;
    bool halfResRTGI;
    uint32_t expensivePassUpdateInterval;
};

RendererBudgetProfile BudgetPlanner::Choose(
    const AdapterInfo& adapter,
    const SceneComplexity& scene,
    const UserQualityRequest& request);
```

Optimization targets:

- Avoid full-resolution effects unless they materially improve the scene.
- Keep SSAO, RT reflection, RT GI, and bloom at reduced resolution by default.
- Use compressed DDS/BC textures where possible.
- Add texture mip caps for 2 GB and 4 GB modes.
- Avoid loading all environment maps on low-memory adapters.
- Reuse transient render-target memory through render graph aliasing.
- Batch descriptor table updates and eliminate redundant descriptor copies.
- Avoid unnecessary queue flushes outside resize/scene teardown.
- Move SSAO/bloom/culling to real async overlap only where fences allow.

Acceptance gates:

- 2 GB profile starts and renders RT showcase with no device removal, lower
  internal resolution, and bounded texture residency.
- 4 GB profile preserves RT shadows and visibility buffer with reduced RT
  reflection/GI cadence.
- 8 GB profile keeps the current high-quality target stable.
- Frame contract records selected budget profile and budget decisions.

### 10. Lighting, Atmosphere, And Visual Quality

Current state:

- IBL, sun, exposure, bloom, fog, god-ray intensity, water, emissive surfaces,
  and reflection probes exist.
- Procedural sky shader exists.
- RT showcase validates technical features.

Current gaps:

- Lighting is not yet artistically robust.
- God-rays are screen-space and can artifact.
- Outdoor scene support exists in pieces but is not a mature presentation path.
- The engine lacks a polished visual demo sequence that proves materials,
  lighting, atmosphere, and performance.

Target state:

- Cortex should ship with several renderer proof scenes:
  - RT material gallery
  - mirror/glass interior
  - sunset beach
  - foggy courtyard
  - water/reflection test
  - low-memory mode test

Implementation plan:

```cpp
struct LightingEnvironment {
    glm::vec3 sunDirection;
    glm::vec3 sunColor;
    float sunIntensity;
    SkyAtmosphereParams sky;
    FogParams fog;
    IBLParams ibl;
    ExposureParams exposure;
};

class SceneLightingRig {
public:
    LightingEnvironment BuildSunsetBeach();
    LightingEnvironment BuildStudioInterior();
    LightingEnvironment BuildMirrorBox();
    LightingEnvironment BuildFoggyCourtyard();
};
```

Atmosphere roadmap:

1. Normalize exposure and tonemapping behavior.
2. Make bloom energy-bounded and scene-referred.
3. Replace ad hoc god-ray behavior with a declared volumetric/light-shaft pass.
4. Add physically motivated sky/sun colors.
5. Add water material integration with reflection/refraction depth contracts.
6. Add local reflection probes and probe blending validation.

Acceptance gates:

- Sunset beach scene renders with stable water, warm sun, sky, fog, bloom, and
  no visible popping.
- Mirror/glass interior demonstrates RT reflections and material correctness.
- Debug views confirm all lighting components.
- Visual validation captures compare luma, saturation, edge stability, temporal
  change, and nonblack coverage.

### 11. Tooling And Tests

Current state:

- RT showcase smoke exists and passes.
- Visual validation capture exists.
- Frame contract JSON exists.
- Diagnostics are logged at shutdown.

Current gaps:

- There are not enough targeted scene tests.
- There is no automated comparison suite for material correctness, culling
  correctness, or temporal stability across camera movement.
- There is no standard "phase 2 gate" command.

Target state:

- Every major renderer contract has a smoke/test scene.
- A single script can run the Phase 2 validation suite.

Implementation plan:

```text
tools/run_phase2_validation.ps1
    1. build Release
    2. run rt_showcase smoke
    3. run material_gallery smoke
    4. run culling_torture smoke
    5. run temporal_motion smoke
    6. run low_memory_profile smoke
    7. collect frame_contract.json and visual_validation.png
    8. fail on budget, black frame, temporal instability, missing pass, or warning threshold
```

Test scenes to add:

- `material_gallery`: controlled material balls and planes.
- `depth_contract`: thin planes, alpha masks, water, transparent glass, decals.
- `culling_torture`: moving camera through occluders and thin geometry.
- `temporal_motion`: moving object, camera cut, camera pan, water, particles.
- `low_memory_rt`: RT showcase under 2 GB/4 GB budget profile.
- `sunset_beach`: final visual target scene.

Acceptance gates:

- Phase 2 validation script returns nonzero on:
  - black frame
  - missing required pass
  - resource size mismatch
  - invalid history used
  - unbounded descriptor delta
  - RT AS budget exceeded
  - excessive temporal flicker
  - fallback path without reason

## Ordered Implementation Passes

### Pass 1: Finish Renderer Split

Status: Complete.

Goal:

Make `Renderer.cpp` a thin facade and move remaining lifecycle/orchestration
into named modules.

Tasks:

- Create `Renderer_Initialization.cpp`.
- Create `Renderer_Diagnostics.cpp`.
- Create `Renderer_RenderOrchestration.cpp`.
- Move shutdown resource release into `Renderer_Shutdown.cpp` or
  `Renderer_Initialization.cpp`.
- Move render feature evaluation into `FrameFeaturePlan`.
- Keep all existing behavior intact.

Done when:

- Build passes. Complete.
- RT showcase smoke passes. Complete.
- `Renderer.cpp` is under 500 lines. Complete; it is now 5 lines.

### Pass 2: Frame Feature Plan And Render Context

Status: Complete.

Goal:

Stop recomputing feature state and environment toggles throughout the render
function.

Tasks:

- Add `FrameFeaturePlan`.
- Add `FrameExecutionContext`.
- Centralize environment-variable debug switches.
- Record feature plan in frame contract.
- Remove direct local `constexpr kEnable...` blocks from orchestration.

Done when:

- Frame contract reports planned features and executed features separately.
  Complete.
- Existing smoke metrics remain stable. Complete.

Implemented:

- Added `FrameFeaturePlan` and `FrameExecutionContext`.
- Moved environment debug switches into one per-process loader used to populate
  each frame plan.
- Replaced scattered render orchestration booleans for RT, VB, GPU culling,
  HZB, TAA, SSR, SSAO, bloom, particles, post-process, and debug lines with
  plan-derived execution flags.
- Extended frame contract JSON with `planned_features` and
  `executed_features`.
- Added validation warnings for executed features that were not present in the
  planned feature set.

### Pass 3: Renderer Scene Snapshot

Status: Complete for frame draw submission. Normal frame rendering now builds
one snapshot and the depth, shadow, VB, GPU-culling, RT TLAS, forward,
overlay, water, and transparent paths consume that snapshot. Some non-draw
maintenance paths still inspect ECS renderables directly for asset/material
work and can be cleaned during later ownership passes.

Goal:

Move ECS traversal and renderable classification into one per-frame snapshot.

Tasks:

- Create `RendererSceneSnapshot`. Complete.
- Create `RendererSceneBuilder`. Implemented as `BuildRendererSceneSnapshot`;
  extract into a class only when more builder state is needed.
- Migrate depth prepass, shadow pass, VB collection, GPU culling, and RT TLAS
  inputs to snapshot. Depth prepass, shadow pass, VB collection, and VB
  GPU-culling input are complete; RT TLAS input is complete; forward,
  late-geometry, and indirect fallback draw collection are complete.
- Preserve editor hooks temporarily by building a snapshot for editor paths.
  Complete for shadow pass and draw-path fallbacks.

Done when:

- ECS renderable view is walked once for renderer frame draw collection.
  Complete for normal draw submission; residual direct ECS scans are limited to
  material prewarming, asset registry, environment, voxel, and editor/debug
  paths.
- Frame contract renderable counts are snapshot-derived. Complete.
- RT/VB/forward paths agree on object counts. Complete for their shared
  candidate source; snapshot, VB, GPU-culling, RT TLAS, forward, and
  late-geometry now consume snapshot buckets, and contract warnings report
  snapshot/VB/GPU-culling/RT drift.

Implemented in Pass 3A:

- Added `RendererSceneSnapshot` and `BuildRendererSceneSnapshot`.
- Stored a current-frame snapshot on `Renderer`.
- Made frame-contract renderable and material stats snapshot-derived.
- Made depth prepass consume `snapshot.depthWritingIndices`.
- Made shadow pass consume `snapshot.depthWritingIndices`, including editor
  fallback snapshot construction outside normal render flow.
- Made visibility-buffer collection consume snapshot stable ordering and depth
  classes.
- Added frame-contract warnings for snapshot/VB/GPU-culling instance count
  drift.

Implemented in Pass 3C:

- Added `RendererSceneSnapshot::rtCandidateIndices`.
- Added a snapshot-build-input TLAS path in `DX12RaytracingContext`.
- Made normal RT rendering submit TLAS inputs from the current renderer
  snapshot, preserving the registry path for fallback/editor use.
- Refreshed TLAS and RT material stats at the end of the frame contract so
  diagnostics report the built frame instead of stale pre-build values.
- Added frame-contract warnings for snapshot/RT TLAS candidate count drift.

Implemented in Pass 3D:

- Made `RenderScene` consume `snapshot.depthWritingIndices`.
- Made overlay, water, and transparent late-geometry passes consume
  `snapshot.overlayIndices`, `snapshot.waterIndices`, and
  `snapshot.transparentIndices`.
- Made legacy GPU-culling collection and indirect fallback rendering consume
  snapshot depth-writing buckets.
- Preserved fallback snapshot construction for entry points that can still be
  called outside normal frame orchestration.

### Pass 4: Render Graph Migration

Status: In progress. Depth prepass, shadows, visibility-buffer frame
execution, and end-of-frame HZB/post-process now execute through render-graph
adapter methods, and frame contracts report graph execution statistics.
Screen-space, late geometry, RT dispatches, transient resource ownership, and
the internal VB visibility/resolve/light subpasses still need to move behind
fully graph-owned pass boundaries.

Goal:

Make graph ownership the default for frame resources and barriers.

Tasks:

- Add pass adapter classes for depth, shadow, visibility, late geometry,
  screen-space, bloom, and post-process.
- Import all persistent resources at graph start.
- Move HZB and post-process existing graph paths into the same builder.
- Move RT dispatches into graph nodes.
- Add transient resource support for SSAO, velocity, TAA intermediate, bloom,
  and half-resolution RT buffers.

Done when:

- Frame contract pass list comes from graph execution. In progress; graph-owned
  records are flagged and frame-level graph counters are reported.
- Manual state tracking is reduced to imported/final external states. In
  progress for depth/shadow/VB-boundary/HZB/post-process, still open for most
  frame resources and for VB-internal resources.
- Legacy fallback barriers can be disabled without smoke failure. Not complete.

Implemented in Pass 4A:

- Added `Renderer_RenderGraph.cpp` as the first adapter file for graph-backed
  renderer pass execution.
- Moved shadow graph setup/fallback execution into
  `ExecuteShadowPassInRenderGraph`.
- Added `FrameContract::RenderGraphInfo`.
- Added frame report JSON for render-graph executions, pass records, graph
  passes, culled passes, barriers, and fallback executions.
- Verified RT showcase with render graph active:
  `executions=2`, `graph_passes=15`, `barriers=34`, `fallbacks=0`.

Implemented in Pass 4B:

- Added `EndFrameGraphInputs` and `EndFrameGraphResult`.
- Moved the HZB/post-process end-of-frame graph builder out of
  `Renderer::Render` into `ExecuteEndFrameInRenderGraph`.
- Kept existing HZB, post-process, and final-state resource declarations
  behaviorally identical during extraction.
- Added frame-contract warnings for graph-owned pass records without active
  graph execution and for graph fallback executions.
- Verified RT showcase with render graph active and no warnings:
  `executions=2`, `graph_passes=15`, `barriers=34`, `fallbacks=0`.

Implemented in Pass 4C:

- Added `ExecuteDepthPrepassInRenderGraph`.
- Added a depth-prepass skip flag so `RenderDepthPrepass` can be executed
  under graph-owned transitions without duplicating its legacy barrier.
- Imported the persistent depth buffer into the graph using `m_depthState`,
  declared `DepthPrepass` as a depth-stencil write pass, and exported the
  graph-returned final depth state back into `m_depthState`.
- Preserved the manual depth prepass as the fallback path with fallback reason
  reporting.
- Marked the frame-contract `DepthPrepass` record as graph-owned on success.
- Verified RT showcase with render graph active and no fallbacks:
  `executions=3`, `graph_passes=16`, `barriers=35`, `fallbacks=0`.

Implemented in Pass 4D:

- Added `ExecuteVisibilityBufferInRenderGraph`.
- Moved the orchestration callsite for `RenderVisibilityBufferPath` behind the
  graph adapter.
- Declared the external depth, HDR, shadow, RT shadow mask, and RT GI resources
  at the graph boundary.
- Kept the direct VB path as a fallback with fallback reason reporting.
- Marked the frame-contract `VisibilityBuffer` record as graph-owned on
  successful graph execution.
- Kept VB-internal transitions in `VisibilityBufferRenderer` for now because
  the current VB implementation changes visibility and G-buffer states several
  times inside one monolithic method.
- Verified RT showcase with render graph active and no fallbacks:
  `executions=4`, `graph_passes=17`, `barriers=37`, `fallbacks=0`.

Implemented in Pass 4E:

- Added `GetVisibilityBufferDebugView`.
- Added `ResolveVisibilityBufferCullMask`.
- Added `LogVisibilityBufferFirstFrame`.
- Added `RenderVisibilityBufferVisibilityStage`.
- Added `RenderVisibilityBufferMaterialResolveStage`.
- Added `RenderVisibilityBufferDeferredLightingStage`.
- Replaced the monolithic `RenderVisibilityBufferPath` body with the staged
  sequence:
  collect/upload, debug selection, cull-mask resolution, visibility raster,
  material resolve, and deferred lighting.
- Removed the old unreachable monolithic VB body after the staged path built.
- Verified RT showcase with render graph active and no fallbacks:
  `executions=4`, `graph_passes=17`, `barriers=37`, `fallbacks=0`.

Implemented in Pass 4F:

- Added `VisibilityBufferRenderer::ResourceStateSnapshot`.
- Added `GetResourceStateSnapshot` and `ApplyResourceStateSnapshot`.
- Exposed BRDF LUT, clustered-light ranges, and clustered-light indices
  resources to the renderer boundary.
- Kept behavior unchanged; this pass only exposes the state contract required
  for graph import/export of VB internals.
- Verified RT showcase with render graph active and no fallbacks:
  `executions=4`, `graph_passes=17`, `barriers=37`, `fallbacks=0`.

Implemented in Pass 4G:

- Added visibility-buffer transition skip controls for visibility, material
  resolve, debug blits, clustered-light buffers, BRDF LUT, and deferred
  lighting.
- Preserved the legacy direct path while allowing graph-owned callbacks to
  suppress duplicate internal resource barriers.
- Verified RT showcase with render graph active and no fallbacks.

Implemented in Pass 4H:

- Split the normal non-debug visibility-buffer path into graph nodes for
  `VBClear`, `VBVisibility`, `VBMaterialResolve`, and `VBDeferredLighting`.
- Imported and exported the visibility buffer, depth buffer, HDR target, and VB
  G-buffer resources directly.
- Kept BRDF LUT generation and clustered light-list construction inside the
  deferred-lighting callback for this step.
- Verified RT showcase with render graph active and no fallbacks.

Implemented in Pass 4I:

- Moved BRDF LUT generation to a graph-scheduled compute node when the LUT is
  not ready.
- Moved clustered light-list construction to `VBClusteredLights`.
- Declared BRDF LUT and cluster buffer reads before `VBDeferredLighting`.
- Added frame-contract records for the VB subpasses.
- Verified RT showcase with render graph active:
  `executions=4`, `graph_passes=21`, `barriers=57`, `fallbacks=0`,
  `pass_records=11`.

Implemented in Pass 4J:

- Moved VB visibility, depth, and G-buffer debug blits onto graph-owned
  `VBDebugBlit` execution.
- Declared HDR, depth, visibility, and selected G-buffer resources explicitly
  for debug presentation.
- Split VB mesh-table upload into a shared contract used by both material
  resolve and motion vectors, fixing stale motion-vector mesh tables when a
  debug path skips material resolve.
- Verified targeted debug view 34:
  `graph_passes=19`, `barriers=38`, `fallbacks=0`, `warnings=0`.
- Verified targeted debug view 35:
  `graph_passes=20`, `barriers=41`, `fallbacks=0`, `warnings=0`.
- Verified normal RT showcase smoke:
  `gpu_ms=1.375`, `dxgi_mb=382.63`, `est_mb=187.85`, `fallbacks=0`.

Implemented in Pass 4K:

- Added `VisibilityBufferGraphResources` and helper import functions so the
  staged VB graph path has one shared resource declaration surface for normal
  rendering and debug blits.
- Added `ExecuteMotionVectorsInRenderGraph`.
- Moved VB motion-vector execution to a graph-owned compute pass that reads the
  visibility buffer and writes the velocity UAV.
- Moved camera-only motion-vector execution to a graph-owned graphics pass that
  reads depth and writes the velocity render target.
- Preserved legacy fallback execution if graph prerequisites or VB compute
  execution fail.
- Verified RT showcase smoke:
  `executions=5`, `graph_passes=22`, `barriers=58`, `pass_records=12`,
  `fallbacks=0`.
- Verified VB depth debug:
  `executions=5`, `graph_passes=20`, `barriers=40`, `pass_records=10`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 4L:

- Split `RenderTAA` into `SeedTAAHistory`, `ResolveTAAIntermediate`,
  `CopyTAAIntermediateToHDR`, and `CopyHDRToTAAHistory`.
- Added `ExecuteTAAInRenderGraph` with explicit HDR, history,
  intermediate, velocity, depth, and normal resource declarations.
- Added `ExecuteSSAOInRenderGraph` with a guarded transition-skip path for
  graph-owned SSAO execution.
- Preserved legacy fallback paths for missing prerequisites or graph
  execution failures.
- Verified RT showcase smoke:
  `executions=7`, `graph_passes=28`, `barriers=68`, `pass_records=14`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 4M:

- Added `ExecuteSSRInRenderGraph`.
- Added an SSR transition-skip guard so the same SSR draw path can be called
  either by the legacy path or by the graph after graph-owned transitions.
- Declared SSR reads and writes explicitly: HDR, depth, normal-roughness, and
  SSR color.
- Finalized SSR color as a shader resource before post-process import.
- Verified RT showcase smoke:
  `executions=8`, `graph_passes=30`, `barriers=69`, `pass_records=15`,
  `fallbacks=0`, `warnings=0`.
- Verified targeted VB depth debug:
  `executions=8`, `graph_passes=28`, `barriers=51`, `pass_records=13`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 4N:

- Split `RenderBloom` into explicit helpers for pass setup, SRV binding,
  base downsample, chain downsample, horizontal blur, vertical blur,
  composite, and combined-result copy-back.
- Added bloom transition-skip ownership so the same stage helpers can run
  either directly or through graph-owned transitions.
- Fixed the bloom composite feedback hazard by rendering the accumulated bloom
  result into the ping-pong target, then copying that result back into the
  existing combined bloom texture consumed by post-process.
- Added `ExecuteBloomInRenderGraph` with graph nodes for every bloom pyramid
  step.
- Verified RT showcase smoke:
  `gpu_ms=1.415`, `executions=9`, `graph_passes=42`, `barriers=90`,
  `pass_records=16`, `fallbacks=0`, `warnings=0`.
- Verified targeted VB depth debug:
  `executions=9`, `graph_passes=40`, `barriers=72`, `pass_records=14`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 4O:

- Fixed the end-frame graph depth contract so HZB and post-process share the
  same imported depth handle when both run in one graph execution.
- Changed post-process VB normal/emissive/material-extension imports to use
  `VisibilityBufferRenderer::ResourceStateSnapshot`.
- Applied final post-process VB resource states back to the visibility-buffer
  tracker after graph execution.
- Verified RT showcase smoke:
  `gpu_ms=1.397`, `executions=9`, `graph_passes=42`, `barriers=90`,
  `pass_records=16`, `fallbacks=0`, `warnings=0`.
- Verified targeted VB depth debug:
  `executions=9`, `graph_passes=40`, `barriers=72`, `pass_records=14`,
  `fallbacks=0`, `warnings=0`.

### Pass 5: Material Model Unification

Goal:

One CPU material model drives all shading paths.

Tasks:

- Add `MaterialModel`.
- Add `MaterialResolver`.
- Add `MaterialPresetRegistry`.
- Generate `MaterialConstants`, `VBMaterialConstants`, and `RTMaterialGPU`
  from one model.
- Add material validation and material debug scene.

Done when:

- Material gallery passes visual validation.
- Forward/VB/RT agree on material domains and scalar parameters.

Implemented in Pass 5A:

- Added `MaterialModel` and `MaterialResolver` as the canonical CPU-side
  material representation.
- Moved preset classification, clearcoat, sheen, subsurface wrap,
  transmission, IOR, specular, emissive, alpha, and texture-presence clamps
  into the resolver.
- Added `MaterialResolver::BuildMaterialConstants` for the forward and
  GPU-driven constant-buffer paths.
- Added `MaterialResolver::BuildVBMaterialConstants` for the visibility-buffer
  material table.
- Routed forward material constants, GPU-driven material constants, and VB
  material uploads through the resolver in compatibility mode.
- Updated VB material deduplication keys to hash the resolved material model
  instead of raw, partially unclamped renderable fields.
- Verified Release build.
- Verified RT showcase smoke:
  `gpu_ms=1.396`, `dxgi_mb=382.63`, `est_mb=187.85`, `fallbacks=0`,
  `warnings=0`.
- Verified targeted VB depth debug:
  `executions=9`, `graph_passes=40`, `barriers=72`, `pass_records=14`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 5B:

- Added `MaterialPresetRegistry` so preset-name token handling is no longer
  duplicated across the resolver, renderable classification, and surface
  classification helpers.
- Routed renderable transparency checks and surface preset matching through
  the registry.
- Added material validation counters to the frame contract and JSON report:
  total issues, warnings, errors, blend+transmission, metallic+transmission,
  and low-roughness normal-map cases.
- Added frame-contract warnings for material validation warnings/errors and
  physically ambiguous material combinations.
- Routed compact RT material records through `MaterialResolver` and
  `MaterialModel`, so forward, GPU-driven, VB, and RT paths now share the same
  scalar material source.
- Extended the RT showcase smoke script to fail on material validation
  warnings/errors and ambiguous blend/transmission or metallic/transmission
  combinations.
- Verified Release build.
- Verified RT showcase smoke:
  `gpu_ms=1.401`, `dxgi_mb=382.63`, `est_mb=187.85`,
  `material_issues=1`, `fallbacks=0`, `warnings=0`.
- Verified targeted VB depth debug:
  `executions=9`, `graph_passes=40`, `barriers=72`, `pass_records=14`,
  `rt_materials=32`, `material_warnings=0`, `material_errors=0`,
  `fallbacks=0`, `warnings=0`.

### Pass 6: RT Scheduler And Denoising

Goal:

Make RT scheduled, budgeted, and temporally stable.

Tasks:

- Add `RTBudget` and `RTFramePlan`.
- Move BLAS/TLAS planning into scheduler.
- Record RT budget decisions in frame contract.
- Add explicit RT denoise pass using depth, normals, velocity, and histories.
- Add update cadence controls for reflections/GI.

Done when:

- RT showcase smoke passes in 8 GB, 4 GB, and 2 GB profiles.
- RT histories are stable under camera motion and scene changes.

Implemented in Pass 6A:

- Added `RTBudget`, `RTFramePlan`, `RTSchedulerInputs`, and `RTScheduler`.
- Added 2 GB, 4 GB, 8 GB, and high-end RT budget profiles with BLAS total
  budget, BLAS per-frame build budget, TLAS instance caps, reduced-resolution
  dispatch sizes, and reflection/GI update cadence.
- Built the RT frame plan after the renderer scene snapshot so scheduling sees
  the current frame's RT candidate count and resource sizes.
- Applied scheduled BLAS budgets to `DX12RaytracingContext` and used the
  scheduled TLAS cap when constructing snapshot-derived TLAS inputs.
- Routed RT shadow, reflection, and GI dispatch gates through `RTFramePlan`
  while preserving the existing RT features on the 8 GB profile.
- Added frame-contract and JSON fields for the scheduler state, budget profile,
  dispatch decisions, denoise decisions, dispatch resolutions, cadence, frame
  phase, TLAS cap, and BLAS budgets.
- Added `RTDenoise` as an explicit pass contract covering the current history
  resolve ownership for RT shadows, reflections, and GI. The current GPU work
  still uses the existing end-frame history copies; the pass record is the
  integration point for the upcoming shader denoiser.
- Extended `tools/run_rt_showcase_smoke.ps1` to fail when the scheduler is
  missing, RT dispatches are not planned, BLAS budgets are invalid, RT
  resolutions are invalid, or the `RTDenoise` contract is missing.
- Verified Release build.
- Verified RT showcase smoke:
  `gpu_ms=1.406`, `dxgi_mb=382.63`, `est_mb=187.85`,
  `rt_budget=8gb_balanced`, `fallbacks=0`, `warnings=0`.
- Verified targeted VB depth debug:
  `rt_scheduler=true`, `rt_dispatches=Strue/Rtrue/GItrue`,
  `rt_denoise=Strue/Rtrue/GItrue`, `graph_passes=40`, `barriers=72`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 6B:

- Added `RTDenoiser` as a dedicated compute module with separate shadow,
  reflection, and GI seed/temporal pipelines.
- Added `assets/shaders/RTDenoise.hlsl` with edge-aware 3x3 spatial filtering,
  depth/normal rejection, velocity reprojection, and per-signal temporal
  accumulation alpha.
- Added UAV descriptors for RT shadow, reflection, and GI histories so history
  resolve is no longer forced through end-frame `CopyResource`.
- Moved RT denoise execution after motion vectors so velocity is available for
  temporal reprojection and before post-process samples RT reflection history.
- Left end-frame history copies as a guarded fallback only when a signal was
  not denoised, and gated GI/shadow fallback copies on actual scheduled
  dispatches.
- Added persistent per-frame RT denoise descriptor tables so denoising does
  not consume transient descriptors or violate descriptor delta validation.
- Added frame-contract and JSON fields for denoiser execution, pass count,
  depth/normal rejection, velocity reprojection, and per-history alpha.
- Extended smoke validation to fail if RT denoising is planned but does not
  execute, if the frame contract omits denoiser diagnostics, or if denoiser
  descriptor binding leaks transient descriptors.
- Verified Release build.
- Verified default RT showcase smoke:
  `gpu_ms=1.869`, `rt_budget=8gb_balanced`, `transient_delta=0`,
  `temporal_diff=mean=0.039/2.5 changed=0.001/0.08`, `fallbacks=0`,
  `warnings=0`.
- Verified forced 4 GB budget smoke:
  `rt_budget=4gb_low`, `gpu_ms=1.481`, `transient_delta=0`, `fallbacks=0`,
  `warnings=0`.
- Verified forced 2 GB budget smoke:
  `rt_budget=2gb_ultra_low`, `gpu_ms=2.424`, `transient_delta=0`,
  `fallbacks=0`, `warnings=0`.

### Pass 7: Temporal Manager

Goal:

Make all temporal resources follow one validity contract.

Tasks:

- Add `TemporalManager`.
- Move TAA and RT history state out of renderer member sprawl.
- Add camera cut, scene change, material change, topology change, and render
  scale invalidation reasons.
- Add disocclusion mask support.
- Integrate with frame contract.

Done when:

- Temporal motion test scene passes.
- Debug report explains every invalid history.

Implemented in Pass 7A:

- Added `TemporalManager` and routed TAA, RT shadow, RT reflection, and RT GI
  validity through central history records.
- Removed the duplicate renderer history booleans and reset-reason mirrors that
  could drift from the frame contract.
- Added per-history diagnostics for seeded state, last invalidated frame,
  rejection mode, alpha, depth/normal rejection, velocity reprojection, and
  disocclusion rejection.
- Fixed RT denoiser temporal reprojection to use the same velocity convention
  as TAA: `history_uv = current_uv + velocity`.
- Added RT denoiser history rejection for out-of-bounds reprojection, depth
  discontinuity, normal mismatch, and excessive screen-space motion.
- Added frame-contract validation and smoke validation for disocclusion-aware
  reprojection.
- Verified Release build.
- Verified default RT showcase smoke:
  `gpu_ms=1.476`, `rt_budget=8gb_balanced`, `transient_delta=0`,
  `temporal_diff=mean=0.002/2.5 changed=0.000/0.08`, `fallbacks=0`,
  `warnings=0`.
- Verified forced 4 GB budget smoke:
  `rt_budget=4gb_low`, `gpu_ms=1.525`, `transient_delta=0`, `fallbacks=0`,
  `warnings=0`.
- Verified forced 2 GB budget smoke:
  `rt_budget=2gb_ultra_low`, `gpu_ms=2.072`, `transient_delta=0`,
  `fallbacks=0`, `warnings=0`.

Remaining Pass 7 work:

- Add explicit camera-cut invalidation coverage to the temporal validation
  suite.
- Move temporal mask stats into a graph-owned readback/pass abstraction instead
  of keeping the stats dispatch as a renderer-side helper after the graph pass.
- Add debug-view capture thresholds for temporal mask visualization, not only
  frame-contract statistics.

Implemented in Pass 7B:

- Added `TemporalRejectionMask` as a compute module and
  `assets/shaders/TemporalRejectionMask.hlsl`.
- Added the full-resolution `temporal_rejection_mask` texture with persistent
  SRV/UAV descriptors.
- Built the mask after motion vectors from depth, normal/roughness, and
  velocity.
- Fed the mask into TAA and RT denoising as a shared accepted-history
  multiplier.
- Added debug view 42 for temporal rejection visualization.
- Added frame-contract resource/pass reporting and smoke validation for the
  mask.
- Verified Release build.
- Verified default RT showcase smoke:
  `gpu_ms=1.964`, `rt_budget=8gb_balanced`, `transient_delta=0`,
  `temporal_diff=mean=0.124/2.5 changed=0.002/0.08`, `fallbacks=0`,
  `warnings=0`.
- Verified forced 4 GB budget smoke:
  `rt_budget=4gb_low`, `gpu_ms=1.514`, `transient_delta=0`, `fallbacks=0`,
  `warnings=0`.
- Verified forced 2 GB budget smoke:
  `rt_budget=2gb_ultra_low`, `gpu_ms=1.372`, `transient_delta=0`,
  `fallbacks=0`, `warnings=0`.

Implemented in Pass 7C through 7F:

- Moved previous-world transform history out of function-static state and into
  renderer-owned frame accounting.
- Added motion-vector frame-contract reporting for planned/executed state,
  VB per-object path usage, camera-only fallback, instance counts, previous
  transform records, seeded records, pruned records, and max object motion.
- Moved temporal rejection mask construction into RenderGraph ownership.
- Added GPU temporal mask statistics, fence-safe readback, and frame-contract
  publication for accepted history, disocclusion, high motion, out-of-bounds
  reprojection, pixel count, sample frame, and readback latency.
- Added `temporal_validation`, a deterministic motion scene with metallic,
  emissive, alpha-tested, water, RT, TAA, and VB motion-vector coverage.
- Added `tools/run_temporal_validation_smoke.ps1` as the behavioral temporal
  regression gate.
- Tightened high-motion temporal rejection to start at 2 pixels.
- Verified Release build, temporal validation smoke, and default RT showcase
  smoke after the threshold change.

### Pass 8: Budget Profiles

Goal:

Make low-memory and low-energy operation first-class.

Tasks:

- Add `BudgetPlanner`.
- Add 2 GB, 4 GB, 8 GB, and high-end profiles.
- Cap texture mips and IBL residency per profile.
- Use reduced-resolution and reduced-cadence effects per profile.
- Add transient resource aliasing in render graph.

Done when:

- Low-memory profile smoke passes without disabling advanced features entirely.
- Frame report explains every quality/budget decision.

Implemented in Pass 8A:

- Added `BudgetPlanner` as the shared profile resolver for renderer budgets.
- Preserved the existing `CORTEX_RT_BUDGET_PROFILE` override and added
  `CORTEX_RENDER_BUDGET_PROFILE` as the broader renderer-level override.
- Moved RT profile selection, BLAS budgets, TLAS caps, RT resolution scale,
  and RT cadence inputs out of `RTScheduler` and into the shared budget plan.
- Added renderer-wide budget fields for target render scale, max render
  dimensions, SSAO divisor, shadow map size, bloom levels, IBL residency, and
  texture/environment/geometry/RT-structure budgets.
- Added top-level `renderer_budget` reporting to the frame contract.
- Added frame-contract validation for missing/invalid renderer budget plans.
- Added smoke validation for renderer budget reporting and profile agreement
  with the RT scheduler.
- Verified Release build.
- Verified default RT showcase smoke:
  `rt_budget=8gb_balanced`, `gpu_ms=1.563`, `transient_delta=0`,
  `warnings=0`.
- Verified forced 4 GB budget smoke:
  `rt_budget=4gb_low`, `gpu_ms=1.569`, `transient_delta=0`, `warnings=0`.
- Verified forced 2 GB budget smoke:
  `rt_budget=2gb_ultra_low`, `gpu_ms=1.513`, `transient_delta=0`,
  `warnings=0`.
- Verified temporal validation smoke:
  `gpu_ms=1.867`, `disocclusion=0.006736`, `high_motion=0.005204`,
  `warnings=0`.

Remaining Pass 8 work:

- Add transient resource aliasing for compatible render graph resources.

Implemented in Pass 8B:

- Applied budget target render scale to renderer initialization and
  `Renderer::SetRenderScale`.
- Applied budget shadow-map size, SSAO divisor, bloom level count, RT
  reflection size, and RT GI size to actual resource allocation.
- Updated frame contracts and startup resize checks to validate the budgeted
  sizes instead of legacy full-size assumptions.
- Verified Release build, default RT showcase smoke, forced 4 GB smoke, forced
  2 GB smoke, and temporal validation smoke.

Implemented in Pass 8C:

- Routed texture, environment, geometry, and RT-structure budgets from
  `BudgetPlanner` into `AssetRegistry`.
- Added `asset_memory` frame-contract reporting and validation warnings for
  over-budget asset categories.
- Made environment eager/deferred loading respect `iblResidentEnvironmentLimit`.
- Preserved environment-map residency accounting across scene ref rebuilds.
- Unregistered environment-map residency when the IBL residency policy evicts
  a loaded environment.
- Set the balanced 8 GB profile to one resident IBL environment by default so
  default smoke remains under the 512 MB DXGI budget while high-end profiles
  can still opt into broader residency.
- Verified Release build, default RT showcase smoke, forced 4 GB smoke, forced
  2 GB smoke, temporal validation smoke, and frame-contract JSON asset-memory
  reporting.

Implemented in Pass 8D:

- Added `CORTEX_LOG_DIR` support to the main run log, frame report writer, and
  renderer visual validation capture path.
- Added `-LogDir` and `-IsolatedLogs` support to RT showcase and temporal
  validation smoke scripts.
- Added `-SkipSurfaceDebug` to RT showcase smoke for faster matrix runs.
- Added `tools/run_budget_profile_matrix.ps1`, which validates default,
  `4gb_low`, and `2gb_ultra_low` profiles with separate per-run artifact
  directories.
- Verified isolated RT showcase smoke, parallel budget-profile matrix smoke,
  and isolated temporal validation smoke.

Implemented in Pass 8E:

- Added material texture mip-admission policy to `BudgetPlanner`:
  `materialTextureMaxDimension` and `materialTextureBudgetFloorDimension`.
- Applied the policy in `Renderer::LoadTextureFromFile` for generic/material
  textures while leaving environment maps on the IBL residency path.
- Material textures now drop top mips when the active profile caps texture
  dimensions or when the generic texture budget is under pressure, without
  reducing below the profile floor unless future policy explicitly permits it.
- Extended renderer-budget frame-contract JSON with the material texture
  dimension cap and budget floor.
- Added render-graph transient heap telemetry to `RenderGraph` and the frame
  contract: transient resources, placed resources, aliased resources, aliasing
  barriers, requested bytes, packed heap bytes, retained heap size, and
  estimated saved bytes.
- Consolidated repeated render-graph execution accounting through
  `Renderer::AccumulateRenderGraphExecutionStats`.
- Reduced budget-profile matrix output to the profile result and smoke summary
  on success; failures still report full captured logs.
- Verified Release build.
- Verified budget-profile matrix smoke. The RT showcase material texture
  bucket stays at `37.33 MB` for default `8gb_balanced` and `4gb_low`, while
  `2gb_ultra_low` admits the 2K wall/floor textures at 1K and reports
  `13.33 MB`; all three profiles report `warnings=0`.
- Verified temporal validation smoke:
  `gpu_ms=1.932`, `disocclusion=0.006802`, `high_motion=0.005215`,
  `object_motion=0.0731`, `warnings=0`.

Remaining Pass 8 work:

- Convert compatible renderer-owned temporary targets into render-graph
  transient resources once their producers and consumers are represented in the
  same graph execution. Current frame-contract telemetry correctly reports
  `transient_resources=0`, which means the allocator exists but the renderer is
  still importing persistent post-process targets across split graph islands.

Implemented in Pass 8F:

- Audited the remaining aliasing candidates after transient telemetry landed.
- Identified bloom as the first practical transient candidate because its
  non-final ping-pong pyramid targets live inside one render-graph execution,
  while TAA, SSAO, SSR, and post-process still cross graph-island boundaries.
- Prototyped graph-owned bloom intermediates with descriptor rebinding and a
  persistent final combined bloom target for post-process.
- The prototype compiled, but the first RT showcase smoke crashed inside the
  NVIDIA driver when the placed transient heap was used. The stable path was
  restored immediately, and the experimental flag now logs a warning and stays
  disabled until placed-resource descriptor lifetime validation is completed.
- Verified Release build after restoring the default path.
- Verified default RT showcase smoke after restoring the default path:
  `gpu_ms=1.552`, `rt_budget=8gb_balanced`, `startup_realloc=0`,
  `warnings=0`.

Remaining Pass 8 work:

- Add a small isolated render-graph placed-resource validation path before
  enabling renderer bloom transients. The validation should prove RTV/SRV
  descriptor rebinding, aliasing barriers, heap compatibility flags, and
  restore ordering independently from the full bloom pass.
- Once that validation passes, re-enable bloom transient intermediates and then
  remove committed resources for non-final bloom pyramid targets so telemetry
  corresponds to actual VRAM savings, not just graph ownership.

Implemented in Pass 8G:

- Added the opt-in `CORTEX_RG_TRANSIENT_VALIDATE` path to exercise the
  render-graph transient allocator independently from bloom before restoring
  the bloom transient prototype.
- Added bounded command-queue fence waits and propagated wait failures through
  frame lifecycle, upload, and shutdown paths. This turns startup hangs into
  diagnosable errors with expected/completed fence values instead of an
  infinite wait.
- Removed `CONFIGURE_DEPENDS` from the asset glob so normal asset changes do
  not force the generated glob verifier into the build-start path. New asset
  files still require a CMake reconfigure.
- Rebuilt all executable source translation units against current headers and
  relinked the executable. This eliminated the invalid frame fence value
  `0x3F800000` and the shutdown access violation that appeared after partial
  relinks.
- Fixed material map semantics so placeholder textures keep descriptor tables
  valid without advertising authored maps to the material model. RT showcase
  validation now reports `material_issues=0` instead of counting placeholder
  normals as authored low-roughness normal maps.
- Hardened `tools/run_budget_profile_matrix.ps1` with a sequential
  `-MaxParallel 1` execution path so Windows `Path`/`PATH` environment
  duplication cannot break low-memory validation.

Verified in Pass 8G:

```text
Forced minimal frame smoke:
  exit=0, no invalid fence wait, clean shutdown

RT showcase smoke:
  frames=33 gpu_ms=2.087/16.7 dxgi_mb=388.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 temporal_diff=mean=0.226/2.5 changed=0.002/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000

Budget profile matrix:
  default material_issues=0 est_mb=190.52 rt_mb=114.63
  4gb_low material_issues=0 est_mb=148.52 rt_mb=72.63
  2gb_ultra_low material_issues=0 est_mb=78.94 rt_mb=36.63

Temporal validation:
  gpu_ms=1.143 disocclusion=0.007811 high_motion=0.005298
  object_motion=0.0731 visible=7 warnings=0

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 RT showcase smoke passed
  transient heap path exercised, clean shutdown
```

Implemented in Pass 8H:

- Added an opt-in `CORTEX_ENABLE_LLM_BACKEND` CMake option. Renderer-focused
  builds now default to a lightweight `LLMServiceStub` and do not schedule
  llama.cpp, ggml, or CUDA backend targets unless the backend is explicitly
  enabled.
- Added `src/LLM/LLMServiceStub.cpp` so the editor command surface can stay
  linked while the local llama backend is omitted from the normal renderer
  validation build.
- Reconfigured and rebuilt through the checked-in project script instead of
  manual targeted object compilation and relinking.
- Confirmed the CMake cache uses `C:\Program Files\Ninja\ninja.exe`; using that
  same Ninja after the script rebuild reports `ninja: no work to do` for
  `bin/CortexEngine.exe`.
- Diagnosed the earlier apparent build failure as a stale/interrupted generic
  build plus mixed Ninja versions and an attempted raw Ninja invocation without
  the Visual Studio environment. The project scripts import the required VS
  environment and are the authoritative build entry point.

Verified in Pass 8H:

```text
Script rebuild:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release
  exit=0, linked build/bin/CortexEngine.exe

No-op executable check:
  C:\Program Files\Ninja\ninja.exe -C CortexEngine/build -n bin/CortexEngine.exe
  ninja: no work to do.

Forced minimal frame smoke:
  exit=0, clean shutdown

RT showcase smoke:
  frames=33 gpu_ms=1.553/16.7 dxgi_mb=387.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 transient_delta=0 startup_realloc=0
  temporal_diff=mean=0.076/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000

Budget profile matrix:
  default gpu_ms=1.548 est_mb=190.52 rt_mb=114.63 material_issues=0
  4gb_low gpu_ms=1.859 est_mb=148.52 rt_mb=72.63 material_issues=0
  2gb_ultra_low gpu_ms=1.716 est_mb=78.94 rt_mb=36.63 material_issues=0

Temporal validation:
  gpu_ms=1.471 disocclusion=0.007456 high_motion=0.005248
  object_motion=0.0731 visible=7 warnings=0

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 RT showcase smoke passed
  transient heap resized to 16 MB, clean shutdown
```

Remaining Pass 8 work:

- Re-enable bloom transient intermediates now that the build target is
  reproducible and the isolated transient allocator path passes.
- Compare transient telemetry before/after bloom transients in one clean
  harness run and keep the persistent final combined bloom target until
  post-process ownership is moved into the same graph island.

Implemented in Pass 8I:

- Re-enabled graph-owned bloom transient intermediates behind
  `CORTEX_ENABLE_BLOOM_TRANSIENTS`.
- Fixed the underlying placed-resource crash. `RenderGraph::BeginFrame()` used
  to clear `m_transientResources` immediately, but Cortex records multiple
  render-graph scopes into one frame command list. That released graph-owned
  bloom resources before later HZB/end-frame work executed the same command
  list. Transient graph resources are now queued through
  `DeferredGPUDeletionQueue` on scope reset and shutdown.
- Added `CORTEX_RG_TRACE_EXECUTION` as an opt-in graph execution trace. This
  isolated the crash to `HZB_DownsampleMip1` pre-barriers after the bloom graph
  had already recorded successfully, proving the failure was resource lifetime,
  not bloom shader execution.
- Added `CORTEX_RG_DISABLE_ALIASING` as an allocator diagnostic switch so
  placed-resource lifetime can be tested independently from heap alias reuse.
- Replaced bloom SRV descriptor copies with direct SRV writes into the active
  descriptor table from the actual source resource. This avoids illegal
  `CopyDescriptorsSimple` usage from shader-visible descriptors and is safer
  for graph-owned transient resources.
- Hardened `tools/run_temporal_validation_smoke.ps1` so it can validate from
  `frame_report_shutdown.json` when the active-frame report is not preserved.

Verified in Pass 8I:

```text
Script rebuild:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release
  exit=0, linked build/bin/CortexEngine.exe

No-op executable check:
  C:\Program Files\Ninja\ninja.exe -C build -n bin\CortexEngine.exe
  ninja: no work to do.

Bloom transients, aliasing disabled:
  CORTEX_ENABLE_BLOOM_TRANSIENTS=1 CORTEX_RG_DISABLE_ALIASING=1 CORTEX_RG_HEAP_DUMP=1
  RT showcase smoke passed
  heapUsed=1.69 MB heapSize=16.00 MB aliasBarriers=0 aliasing=off

Bloom transients, aliasing enabled:
  CORTEX_ENABLE_BLOOM_TRANSIENTS=1 CORTEX_RG_HEAP_DUMP=1
  RT showcase smoke passed
  frames=33 gpu_ms=1.615/16.7 dxgi_mb=403.95/512
  heapUsed=1.44 MB heapSize=16.00 MB aliasBarriers=2 aliasing=on
  material_issues=0 startup_realloc=0 clean shutdown

Default RT showcase smoke:
  frames=33 gpu_ms=1.577/16.7 dxgi_mb=387.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 startup_realloc=0
  temporal_diff=mean=0.231/2.5 changed=0.004/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000

Budget profile matrix:
  default gpu_ms=1.613 est_mb=190.52 rt_mb=114.63 material_issues=0
  4gb_low gpu_ms=1.550 est_mb=148.52 rt_mb=72.63 material_issues=0
  2gb_ultra_low gpu_ms=1.227 est_mb=78.94 rt_mb=36.63 material_issues=0

Temporal validation:
  gpu_ms=1.550 disocclusion=0.006747 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0
```

Remaining Pass 8 work:

- Move the persistent final combined bloom target into the same graph island as
  post-process so the last bloom copy can become a graph edge instead of a
  compatibility copy.
- Extend the same graph-owned transient pattern from bloom to other compatible
  post-processing intermediates once producer/consumer scopes are represented
  in one render-graph execution.

Implemented in Pass 8J:

- Made dynamic descriptor tables explicit by marking per-frame SRV/UAV
  descriptor ranges as `DESCRIPTORS_VOLATILE | DATA_VOLATILE` in the main
  compute root signature, the compact single-SRV/UAV compute root signature,
  GPU culling, and visibility-buffer compute/deferred root signatures.
- Removed remaining default-path shader-visible descriptor copies from
  material fallback binding, reserved bindless placeholder binding,
  environment descriptor table setup, temporal rejection mask dispatch,
  temporal mask stats dispatch, RT denoising, and RT reflection normal/material
  binding.
- Replaced those copies with direct SRV/UAV descriptor creation from the owning
  resources, including typeless-to-view format mapping for depth and material
  classification resources.
- Preserved staging descriptors where they are valid copy sources, such as
  depth, shadow, RT history, RT GI, and RT output descriptors.
- Verified that the default RT showcase path is D3D12 debug-layer clean.

Verified in Pass 8J:

```text
Script rebuild:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release
  passed

Default RT showcase debug-layer smoke:
  .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  frames=33
  D3D12 validation errors=0
  dxgi_mb=388.96
  material_errors=0
  material_warnings=0
  graph_fallbacks=0
  clean shutdown
```

Implemented in Pass 8K:

- Ran the bloom-transient RT showcase path under the D3D12 debug layer after
  the descriptor contract fixes.
- Promoted bloom transient intermediates from opt-in validation to normal
  execution. The renderer now uses graph-owned bloom intermediates by default,
  and `CORTEX_DISABLE_BLOOM_TRANSIENTS=1` is the temporary compatibility
  override.
- Kept `CORTEX_RG_HEAP_DUMP=1` as the explicit diagnostics switch for heap
  telemetry.

Verified in Pass 8K:

```text
Default-on bloom transient debug-layer smoke:
  .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  frames=33
  D3D12 validation errors=0
  dxgi_mb=405.96
  material_errors=0
  material_warnings=0
  graph_fallbacks=0
  transient_resources=5
  heapUsed=1.44 MB
  heapSize=16.00 MB
  aliasBarriers=2
  clean shutdown
```

Implemented in Pass 8L:

- Fixed the temporal rejection mask statistics UAV contract. The stats shader
  writes five raw uint counters: accepted, disocclusion, high motion,
  out-of-bounds, and pixel count. The direct UAV descriptor path incorrectly
  declared only four raw elements, so the pixel-count write was outside the
  declared UAV range and read back as zero.
- Added a shared `kTemporalMaskStatsWords = 5` constant to the temporal mask
  dispatch code and used it for the raw UAV descriptor element count.
- Rebuilt through the checked-in rebuild script instead of relying on a stale
  binary.

Verified in Pass 8L:

```text
Script rebuild:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release
  passed

No-op Ninja:
  "C:\Program Files\Ninja\ninja.exe" -C .\build -n bin\CortexEngine.exe
  no work to do

Forced minimal frame smoke:
  .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=20 --exit-after-visual-validation
  clean shutdown

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs
  frames=33 gpu_ms=1.571/16.7 dxgi_mb=403.95/512
  temporal_mask=valid pixels=665856 accepted=1.000
  temporal_diff=mean=0.048/2.5 changed=0.001/0.08
  material_issues=0
  passed

Budget profile matrix:
  tools/run_budget_profile_matrix.ps1 -NoBuild -TemporalRuns 1 -MaxParallel 1
  default gpu_ms=1.588 dxgi_mb=403.95 rt_mb=114.63
  4gb_low gpu_ms=1.872 dxgi_mb=361.95 rt_mb=72.63
  2gb_ultra_low gpu_ms=1.202 dxgi_mb=277.86 rt_mb=36.63
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.521 disocclusion=0.006919 high_motion=0.005242
  object_motion=0.0731 visible=7 warnings=0
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  passed

Default-on bloom transient debug-layer smoke:
  .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  D3D12 validation errors=0
  temporal_mask=valid pixels=665856 accepted=1.000
  material_errors=0
  material_warnings=0
  graph_fallbacks=0
  transient_resources=5
  heapUsed=1.44 MB
  heapSize=16.00 MB
  aliasBarriers=2
  clean shutdown
```

Implemented in Pass 8M:

- Stabilized the fused bloom/post-process graph path after the graph-owned
  bloom transient path crashed in `BloomDownsample0`.
- Confirmed the failure was runtime descriptor/resource binding, not a stale
  build or a global renderer failure:
  - Release rebuild passed.
  - The compatibility path with `CORTEX_DISABLE_BLOOM_TRANSIENTS=1` passed.
  - The graph-owned transient path still crashed even with aliasing disabled,
    isolating it away from heap aliasing and toward transient bloom descriptor
    rebinding.
- Changed the default fused end-frame path to import the persistent bloom A/B
  targets into the render graph and consume the composite bloom target directly
  in the post-process pass.
- Preserved bloom, RT, HZB, SSAO, SSR, TAA, and post-process execution. This
  does not disable the effect; it removes the extra bloom compatibility copy
  while keeping stable resources in the default path.
- Kept graph-owned fused bloom transients available only behind
  `CORTEX_ENABLE_FUSED_BLOOM_TRANSIENTS=1` until the descriptor binding model
  is made pass-local and debug-layer clean.

Verified in Pass 8M:

```text
Script rebuild:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release
  passed

Focused debug-layer trace:
  CORTEX_RG_TRACE_EXECUTION=1 .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  passed
  observed end-frame sequence:
    HZB_* -> BloomDownsample0/1/2 -> BloomBlurH/V -> BloomComposite -> PostProcess

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.559/16.7 dxgi_mb=387.95/512
  rt_mb=114.63/160 material_issues=0
  passed

No-op build:
  cmake --build build --config Release --target CortexEngine
  ninja: no work to do

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.215 disocclusion=0.006937 high_motion=0.005242
  object_motion=0.0731 visible=7 warnings=0
  passed

Budget profile matrix:
  tools/run_budget_profile_matrix.ps1 -NoBuild -TemporalRuns 1 -MaxParallel 1
  default gpu_ms=1.546 dxgi_mb=387.95 rt_mb=114.63
  4gb_low gpu_ms=1.410 dxgi_mb=345.95 rt_mb=72.63
  2gb_ultra_low gpu_ms=1.529 dxgi_mb=261.86 rt_mb=36.63
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.857/16.7 material_issues=0
  passed

Debug-layer heap/log smoke:
  CORTEX_RG_HEAP_DUMP=1 .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  D3D12 validation errors=0
  critical errors=0
  clean shutdown
```

Implemented in Pass 8N:

- Re-enabled graph-owned fused bloom transients by default through a pass-local
  binding path.
- Removed the fused end-frame path's renderer-wide bloom descriptor/resource
  mutation. The graph path now creates pass-local RTVs for transient bloom
  outputs and writes SRVs into the frame bloom descriptor tables at execution
  time without rebinding `m_bloomTexA/B`, `m_bloomRTV`, or `m_bloomSRV`.
- Added a scoped post-process bloom override so `RenderPostProcess()` can sample
  the graph-owned bloom composite directly while preserving the persistent bloom
  pyramid for legacy and compatibility paths.
- Kept `CORTEX_DISABLE_BLOOM_TRANSIENTS=1` as a compatibility path. With that
  environment variable set, the fused graph imports persistent bloom targets
  instead of creating graph-owned bloom transients.
- Confirmed the remaining older bloom descriptor rewrite/restore machinery is
  limited to the standalone bloom graph helper, not the default fused end-frame
  path.

Verified in Pass 8N:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj
  passed

Link/no-op build:
  ninja -C build -v CortexEngine
  passed
  ninja -C build -n CortexEngine
  ninja: no work to do

Focused debug-layer trace:
  CORTEX_RG_TRACE_EXECUTION=1 .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  passed
  observed end-frame sequence:
    HZB_* -> BloomDownsample0/1/2 -> BloomBlurH/V -> BloomComposite -> PostProcess
  log confirms:
    Bloom RG: fused graph-owned intermediates enabled (default)

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.613/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.218 disocclusion=0.006819 high_motion=0.005220
  object_motion=0.0731 visible=7 warnings=0
  passed

Budget profile matrix:
  tools/run_budget_profile_matrix.ps1 -NoBuild -TemporalRuns 1 -MaxParallel 1
  default gpu_ms=1.571 dxgi_mb=403.95 rt_mb=114.63
  4gb_low gpu_ms=1.542 dxgi_mb=361.95 rt_mb=72.63
  2gb_ultra_low gpu_ms=1.151 dxgi_mb=277.86 rt_mb=36.63
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.945/16.7 material_issues=0
  passed

Debug-layer heap/log smoke:
  CORTEX_RG_HEAP_DUMP=1 .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  D3D12 validation errors=0
  critical errors=0
  graph transients=6 heapUsed=1.62 MB heapSize=16.00 MB aliasBarriers=2
  clean shutdown

Compatibility smoke:
  CORTEX_DISABLE_BLOOM_TRANSIENTS=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.794/16.7 dxgi_mb=387.95/512
  material_issues=0
  passed
```

Implemented in Pass 8O:

- Added a small scoped state guard in `Renderer_RenderGraph.cpp` for temporary
  render-graph execution state.
- Replaced manual skip-transition flag save/restore in depth, shadow, SSR,
  SSAO, standalone bloom, and fused post-process graph callbacks.
- Scoped the post-process bloom override so graph-owned bloom resources are
  bound only for the duration of the post-process callback.
- Reduced hidden renderer state leakage risk when a graph callback exits early
  or a fallback path changes pass mode.
- Moved the scoped guard out of `Renderer_RenderGraph.cpp` into
  `src/Graphics/Passes/RenderPassScope.h` so subsequent pass modules can reuse
  the same temporary-state pattern.

Verified in Pass 8O:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

Focused debug-layer trace:
  CORTEX_RG_TRACE_EXECUTION=1 .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  passed
  observed TAAResolve, SSR, SSAO, BloomComposite, and PostProcess callbacks
  D3D12 validation errors=0
  graph_fallbacks=0

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.570/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.867 disocclusion=0.006570 high_motion=0.005151
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8P:

- Added `src/Graphics/Passes/BloomPass.h/.cpp` as the first physical
  post-process pass helper module.
- Moved shared bloom slot layout, render-graph transient texture descriptors,
  fullscreen viewport setup, graph SRV writes, graph RTV allocation, fullscreen
  draw execution, and multi-level composite execution into the BloomPass module.
- Updated the legacy bloom renderer to use BloomPass slot/viewport helpers
  instead of carrying duplicate local helper functions.
- Updated the fused graph-owned bloom path to call BloomPass fullscreen and
  composite helpers while preserving pass-local SRV/RTV binding for transient
  resources.
- Added BloomPass to the CMake source/header lists.

Verified in Pass 8P:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\BloomPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_Bloom.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=2.073/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=2.121 disocclusion=0.006753 high_motion=0.005176
  object_motion=0.0731 visible=7 warnings=0
  passed

Compatibility smoke:
  CORTEX_DISABLE_BLOOM_TRANSIENTS=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.465/16.7 dxgi_mb=388.97/512
  material_issues=0
  passed

Focused debug-layer graph trace:
  CORTEX_RG_TRACE_EXECUTION=1 CORTEX_RG_HEAP_DUMP=1 .\build\bin\CortexEngine.exe --scene=rt_showcase --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=45 --exit-after-visual-validation
  passed
  D3D12 Debug Layer enabled
  observed HZB_* -> BloomDownsample0/1/2 -> BloomBlurH/V -> BloomComposite -> PostProcess
  heap transients=6 heapUsed=1.62 MB heapSize=16.00 MB aliasBarriers=2
```

Implemented in Pass 8Q:

- Added `src/Graphics/Passes/FullscreenPass.h/.cpp` as a generic fullscreen
  pass helper for screen-space render passes.
- Moved reusable fullscreen viewport/scissor setup, graphics root/descriptor
  binding, and fullscreen triangle draw helpers out of BloomPass.
- Updated BloomPass to depend on FullscreenPass for shared state binding and
  draw execution.
- Updated SSR, TAA resolve, motion vectors, skybox fullscreen draw, and
  post-process draw/viewport setup to use FullscreenPass helpers.
- Added FullscreenPass to the CMake source/header lists.

Verified in Pass 8Q:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\FullscreenPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Passes\BloomPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_PostProcess.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.556/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.204 disocclusion=0.007412 high_motion=0.005253
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8R:

- Added `src/Graphics/Passes/DescriptorTable.h/.cpp` as a small descriptor
  table helper for screen-space and post-process passes.
- Centralized direct Texture2D SRV writes so SSR, motion-vector depth binding,
  TAA resolve, post-process, SSAO, RT denoise, temporal mask, temporal mask
  stats, and bloom table initialization no longer carry duplicate ad hoc SRV
  creation code.
- Centralized contiguous descriptor-table validation so persistent per-frame
  table setup reports contract violations consistently.
- Added DescriptorTable to the CMake source/header lists.
- Preserved the existing persistent descriptor allocation model; this pass
  reduced duplicated binding code and prepared the later descriptor allocator
  split without changing runtime feature policy.

Verified in Pass 8R:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\DescriptorTable.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_Descriptors.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=3.508/16.7 dxgi_mb=404.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.224 disocclusion=0.004386 high_motion=0.004689
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8S:

- Extended DescriptorTable with direct Texture2D UAV writes.
- Converted SSAO graphics and compute descriptor binding from staging
  descriptor copies to direct pass-local SRV/UAV writes from the depth and
  SSAO resources.
- Updated SSAO graphics rendering to use FullscreenPass viewport/scissor and
  fullscreen triangle helpers.
- Updated SSR, TAA resolve, motion vectors, SSAO, and post-process to bind
  root signature, descriptor heap, frame constants, and fullscreen topology
  through `FullscreenPass::BindGraphicsState`.
- Reduced duplicated screen-space pass setup without changing feature policy:
  TAA, SSR, SSAO, bloom, post-process, RT reflections/GI, and graph-owned bloom
  transients remain enabled in the default validation path.

Verified in Pass 8S:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_SSAO.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_PostProcess.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Passes\FullscreenPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Passes\DescriptorTable.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.489/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=2.916 disocclusion=0.006858 high_motion=0.005228
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8T:

- Added `src/Graphics/Passes/SSRPass.h/.cpp` as a physical screen-space
  reflection pass module.
- Moved SSR render-target setup, viewport/scissor binding, clear, fullscreen
  root/descriptor binding, SRV table writes, shadow/environment table binding,
  and fullscreen draw execution out of `Renderer::RenderSSR`.
- Kept `Renderer::RenderSSR` responsible for renderer-owned resource-state
  transitions and normal/roughness source selection until the render graph
  wrapper is split further.
- Added SSRPass to the CMake source/header lists.
- Preserved default SSR, TAA, SSAO, RT reflection/GI, bloom, post-process, and
  graph-owned bloom transient execution.

Verified in Pass 8T:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\SSRPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.613/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.328 disocclusion=0.006913 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8U:

- Added `src/Graphics/Passes/SSAOPass.h/.cpp` as a physical SSAO pass module.
- Moved SSAO graphics target setup, clear, fullscreen binding, depth SRV table
  write, and fullscreen draw into SSAOPass.
- Moved the active compute SSAO dispatch path into SSAOPass, including compute
  root binding, descriptor heap binding, depth SRV table writes, SSAO UAV table
  writes, and 8x8 dispatch sizing.
- Kept `Renderer::RenderSSAO` and `Renderer::RenderSSAOAsync` responsible for
  renderer-owned resource-state transitions and feature-policy checks.
- Added SSAOPass to the CMake source/header lists and cleaned stale includes
  from `Renderer_SSAO.cpp`.

Verified in Pass 8U:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\SSAOPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_SSAO.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=3.935/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.453 disocclusion=0.006829 high_motion=0.005218
  object_motion=0.0731 visible=7 warnings=0
  passed

Include cleanup:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_SSAO.cpp.obj
  ninja -C build -v CortexEngine
  ninja -C build -n CortexEngine
  passed
```

Implemented in Pass 8V:

- Added `src/Graphics/Passes/TAAPass.h/.cpp` as a physical TAA resolve draw
  module.
- Moved TAA resolve render-target binding, viewport/scissor setup, fullscreen
  state binding, TAA SRV table binding, shadow/environment table binding, and
  fullscreen draw execution out of `Renderer::ResolveTAAIntermediate`.
- Added `src/Graphics/Passes/PostProcessPass.h/.cpp` as a physical
  post-process draw module.
- Moved post-process back-buffer RTV binding, viewport/scissor setup,
  fullscreen state binding, post-process SRV table binding,
  shadow/environment table binding, and fullscreen draw execution out of
  `Renderer::RenderPostProcess`.
- Kept renderer-owned transition policy, TAA copy/history policy, and the
  optional RT-reflection debug clear in the renderer until those become
  explicit graph/debug passes.
- Added TAAPass and PostProcessPass to the CMake source/header lists.

Verified in Pass 8V:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\TAAPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Passes\PostProcessPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_PostProcess.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=3.258/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.948 disocclusion=0.006498 high_motion=0.005138
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8W:

- Added `src/Graphics/Passes/MotionVectorPass.h/.cpp` as a physical
  camera-only motion-vector fullscreen pass module.
- Moved fallback motion-vector RTV binding, viewport/scissor setup, clear,
  fullscreen state binding, depth SRV table writes, descriptor table binding,
  and fullscreen draw execution out of `Renderer::RenderMotionVectors`.
- Kept the visibility-buffer object-motion path, resource transitions, fallback
  selection, and final velocity resource-state tracking in the renderer.
- Added MotionVectorPass to the CMake source/header lists.
- Removed the stale DescriptorTable include from `Renderer_ScreenSpace.cpp`.

Verified in Pass 8W:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\MotionVectorPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.798/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.144 disocclusion=0.006793 high_motion=0.005216
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8X:

- Added `src/Graphics/Passes/SkyboxPass.h/.cpp` as a physical fullscreen
  skybox pass module.
- Moved skybox/procedural-sky pipeline selection, root-signature binding,
  frame-constant binding, shadow/environment descriptor binding, topology
  setup, and fullscreen draw execution out of `Renderer::RenderSkybox`.
- Kept skybox resource ownership, environment residency policy, and render
  target/depth transitions in the renderer until sky/environment state becomes
  a graph-owned pass contract.
- Added SkyboxPass to the CMake source/header lists.

Verified in Pass 8X:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\SkyboxPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_ScreenSpace.cpp.obj
  passed

Link:
  ninja -C build -v CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=4.232/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.690 disocclusion=0.006759 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8Y:

- Added `src/Graphics/Passes/PostProcessGraphPass.h/.cpp` as the first
  explicit post-process render-graph declaration module.
- Moved post-process graph input/output declaration out of
  `Renderer_RenderGraph.cpp` into `PostProcessGraphPass::Declare`.
- Fixed a fused bloom graph lifetime bug: the end-of-frame bloom composite
  execute lambda captured the local `baseLevel` by reference even though graph
  execution happens after that local scope ends. The composite target could
  therefore resolve to an invalid transient handle and silently fall back.
- Captured the composite base level by value and kept targeted diagnostic
  warnings in `BloomPass::RenderComposite` for future graph-contract failures.
- Added PostProcessGraphPass to the CMake source/header lists.

Verified in Pass 8Y:

```text
Targeted compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\PostProcessGraphPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj
  passed

Targeted fix compile:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=2.282/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  fused bloom composite warnings cleared
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.218 disocclusion=0.006857 high_motion=0.005227
  object_motion=0.0731 visible=7 warnings=0
  fused bloom composite warnings cleared
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.572/16.7 dxgi_mb=403.95/512
  rt_mb=114.63/160 material_issues=0 transient_delta=0
  passed
```

Implemented in Pass 8Z:

- Added `src/Graphics/Passes/HZBPass.h/.cpp` as the physical pass module for
  graph-owned HZB construction.
- Moved HZB render-graph node construction out of `Renderer_RenderTargets.cpp`.
  `Renderer::AddHZBFromDepthPasses_RG` now only builds an explicit graph
  context and delegates to `HZBPass::AddFromDepth`.
- Captured graph handles, mip indices, and mip dimensions by value inside the
  pass module. This avoids the delayed render-graph callback lifetime class of
  bug that previously appeared in fused bloom.
- Kept the renderer-owned persistent descriptor tables and compute pipeline
  selection explicit in `HZBPass::GraphContext`; a later GPU-driven pass can
  move those descriptors behind a culling/HZB service without changing the
  graph contract.
- Added HZBPass to the CMake source/header lists.

Verified in Pass 8Z:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\HZBPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderTargets.cpp.obj CortexEngine
  passed

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.930/16.7 dxgi_mb=404.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.86 center_luma=73.59 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=4.232 disocclusion=0.006596 high_motion=0.005148
  object_motion=0.0731 visible=7 warnings=0
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.521/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 transient_delta=0
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do
```

Implemented in Pass 8AA:

- Added `src/Graphics/Passes/BloomGraphPass.h/.cpp` as the explicit fused bloom
  render-graph construction module.
- Moved the default end-frame fused bloom graph nodes out of
  `Renderer_RenderGraph.cpp`: downsample chain, horizontal/vertical blur, and
  composite are now declared by `BloomGraphPass::AddFusedBloom`.
- Kept graph-owned transient bloom intermediates enabled by default and kept
  the existing persistent-bloom compatibility path unchanged.
- Encoded the fused bloom contract as `BloomGraphPass::FusedBloomContext`,
  including HDR input, bloom ping-pong handles, template resources for
  transients, persistent graph RTVs, pipelines, fullscreen binding state, and
  stage failure callbacks.
- Uses spans for bloom handles and template resources, so the pass module is
  not hard-wired to the renderer's current bloom level count.
- Preserved the value-capture rule for delayed graph callbacks. Pass levels and
  the composite base level are captured by value, while renderer state mutation
  remains limited to explicit callbacks for HDR state and pass success/failure.
- Added BloomGraphPass to the CMake source/header lists.

Verified in Pass 8AA:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\BloomGraphPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=2.842/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.85 center_luma=73.58 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=3.337 disocclusion=0.006687 high_motion=0.005168
  object_motion=0.0731 visible=7 warnings=0
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.938/16.7 dxgi_mb=404.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 transient_delta=0
  passed

Notes:
  A concurrent RT showcase and temporal validation launch produced one
  non-reproducible RT showcase timing failure at gpu_ms=36.211/16.7. The same
  RT showcase smoke passed when rerun alone, so the result is recorded as
  validation contention rather than a renderer regression.
```

Implemented in Pass 8AB:

- Extended `src/Graphics/Passes/PostProcessGraphPass.h/.cpp` beyond resource
  declaration. The pass module now owns the post-process graph execution
  callback through `PostProcessGraphPass::Execute`.
- Moved the inline renderer callback for post-process execution out of
  `Renderer_RenderGraph.cpp`. The renderer now supplies explicit callbacks for
  bloom override rendering, default rendering, bloom-stage failure marking, and
  pass success marking.
- Kept scoped renderer state mutation at the call site through
  `ScopedRenderPassValue`, so temporary bloom override and transition-skip
  state remain bounded to the actual `RenderPostProcess()` invocation.
- Preserved fused bloom behavior: post-process samples the graph-owned bloom
  override when fused bloom ran, otherwise it uses the default post-process
  path.

Verified in Pass 8AB:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\PostProcessGraphPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.551/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.86 center_luma=73.59 material_issues=0 transient_delta=0
  passed

Temporal validation:
  tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.873 disocclusion=0.006590 high_motion=0.005154
  object_motion=0.0731 visible=7 warnings=0
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=2.142/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 transient_delta=0
  passed
```

Implemented in Pass 8AC:

- Extended `src/Graphics/Passes/BloomGraphPass.h/.cpp` with
  `StandaloneBloomContext` and `AddStandaloneBloom`.
- Moved the standalone bloom render-graph node sequence out of
  `Renderer_RenderGraph.cpp`: base downsample, downsample chain, horizontal
  blur, vertical blur, composite, combined copy, and final read dependency.
- Kept the legacy draw implementation callable through explicit renderer
  callbacks while the pass module now owns graph construction and the resource
  dependency contract.
- Tightened the standalone bloom contract so missing callbacks fail at graph
  construction with `standalone_bloom_contract` instead of producing partially
  wired graph nodes.

Verified in Pass 8AC:

```text
Targeted compile:
  cl.exe direct compile of BloomGraphPass.cpp
  passed

Targeted compile:
  cl.exe direct compile of Renderer_RenderGraph.cpp
  passed

Targeted link:
  cmake -E vs_link_exe with a generated current CortexEngine response file
  passed

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=3.242/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=71.07 center_luma=74.22 material_issues=0 transient_delta=0
  passed

Temporal validation:
  powershell -ExecutionPolicy Bypass -File tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.718 disocclusion=0.007656 high_motion=0.005245
  object_motion=0.0731 visible=7 warnings=0
  passed

Render-graph transient validation:
  CORTEX_RG_TRANSIENT_VALIDATE=1 powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=5.386/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  material_issues=0 transient_delta=0
  passed

Build-driver note:
  The first verification used direct compile/link because an incompatible Ninja
  binary had wiped `.ninja_log`, forcing a broad rebuild and making the old
  asset-copy path look stalled. Pass 8AD fixes the build-driver problem and
  restores normal Ninja verification.
```

Implemented in Pass 8AD:

- Replaced the unconditional `cmake -E copy_directory` asset deployment with
  `tools/sync_assets.cmake`.
- The asset sync now copies only changed files and excludes source `.hdr` and
  `.exr` payloads from the normal runtime deployment path. Runtime smoke scenes
  continue to load the compressed `.dds` siblings used by the budgeted
  environment pipeline.
- Removed the invalid `CONFIGURE_DEPENDS` use from the script-mode asset sync
  path so `cmake -P tools/sync_assets.cmake` can run under Ninja.
- Restored the build log by running one full `CortexEngine` rebuild after the
  earlier incompatible Ninja invocation erased `.ninja_log`.

Verified in Pass 8AD:

```text
CMake regenerate:
  cmake -S CortexEngine -B CortexEngine/build
  passed

Asset sync target:
  ninja -C build -v cortex_assets.stamp
  passed

Targeted compile through Ninja:
  ninja -C build -v -j 1 CMakeFiles\CortexEngine.dir\src\Graphics\Passes\BloomGraphPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj
  passed

Full build:
  ninja -C build -j 8 CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

Post-rebuild RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.549/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.83 center_luma=73.50 material_issues=0 transient_delta=0
  passed
```

Implemented in Pass 8AE:

- Extended `src/Graphics/Passes/SSAOPass.h/.cpp` with `GraphContext` and
  `AddToGraph`.
- Moved SSAO render-graph node construction out of
  `Renderer_RenderGraph.cpp`. The pass module now declares the depth read,
  SSAO target write, compute-versus-graphics pass type, and final
  shader-resource dependency.
- Kept the renderer-side legacy draw/dispatch code behind an explicit
  execution callback while graph contract validation and stage failure naming
  live in the pass module.

Verified in Pass 8AE:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\SSAOPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.823/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.83 center_luma=73.50 material_issues=0 transient_delta=0
  passed

Temporal validation:
  powershell -ExecutionPolicy Bypass -File tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.139 disocclusion=0.006829 high_motion=0.005218
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8AF:

- Extended `src/Graphics/Passes/SSRPass.h/.cpp` with `GraphContext` and
  `AddToGraph`.
- Moved SSR render-graph node construction out of `Renderer_RenderGraph.cpp`.
  The pass module now declares HDR, depth, normal/roughness, SSR target, and
  final shader-resource dependencies.
- Preserved the existing renderer draw path behind an explicit execution
  callback while the pass module owns contract validation and stage failure
  reporting.

Verified in Pass 8AF:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\SSRPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.516/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.83 center_luma=73.49 material_issues=0 transient_delta=0
  passed

Temporal validation:
  powershell -ExecutionPolicy Bypass -File tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.142 disocclusion=0.006843 high_motion=0.005220
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8AG:

- Extended `src/Graphics/Passes/TAAPass.h/.cpp` with `GraphContext` and
  `AddToGraph`.
- Moved TAA render-graph node construction out of
  `Renderer_RenderGraph.cpp`, including both graph shapes:
  seed-only history copy and full resolve/copy-to-HDR/copy-to-history/finalize.
- Kept the existing renderer operations behind explicit callbacks for
  `seedHistory`, `resolve`, `copyToHDR`, and `copyToHistory`, while the pass
  module now owns dependency declaration and contract validation.

Verified in Pass 8AG:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\TAAPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.489/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.82 center_luma=73.46 material_issues=0 transient_delta=0
  passed

Temporal validation:
  powershell -ExecutionPolicy Bypass -File tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=2.762 disocclusion=0.006878 high_motion=0.005229
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8AH:

- Extended `src/Graphics/Passes/MotionVectorPass.h/.cpp` with a
  render-graph `GraphContext` and `AddToGraph`.
- Moved motion-vector graph node construction out of
  `Renderer_RenderGraph.cpp` for both paths:
  visibility-buffer mesh motion as compute, and camera-only motion as a
  depth-reading graphics pass.
- Converted ignored graph-contract results into explicit fallback causes so a
  bad pass declaration cannot silently execute an empty graph.
- Extended `src/Graphics/TemporalRejectionMask.h/.cpp` with a graph contract
  entry point and moved temporal rejection mask node declaration out of the
  renderer.
- Kept renderer-owned state mirrors and diagnostics in the renderer callbacks
  while the pass modules own dependency declaration and stage validation.

Verified in Pass 8AH:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\MotionVectorPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\TemporalRejectionMask.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=3.250/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.83 center_luma=73.49 material_issues=0 transient_delta=0
  passed

Temporal validation:
  powershell -ExecutionPolicy Bypass -File tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.139 disocclusion=0.006856 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8AI:

- Added physical graph-contract modules:
  `src/Graphics/Passes/DepthPrepass.h/.cpp` and
  `src/Graphics/Passes/ShadowPass.h/.cpp`.
- Added both modules to `CMakeLists.txt`.
- Moved depth-prepass graph node declaration out of
  `Renderer_RenderGraph.cpp`; the pass module now owns the depth write
  contract and stage validation.
- Moved shadow graph node declaration out of `Renderer_RenderGraph.cpp`,
  including the shadow finalize shader-resource transition.
- Preserved renderer-owned draw callbacks and skip-transition scopes, so this
  pass changes ownership boundaries without changing draw behavior.

Verified in Pass 8AI:

```text
Configure:
  cmake -S CortexEngine -B CortexEngine\build
  passed

Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\DepthPrepass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Passes\ShadowPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=3.484/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.82 center_luma=73.47 material_issues=0 transient_delta=0
  passed

Temporal validation:
  powershell -ExecutionPolicy Bypass -File tools/run_temporal_validation_smoke.ps1 -NoBuild -IsolatedLogs
  gpu_ms=1.134 disocclusion=0.00686 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0
  passed
```

Implemented in Pass 8AJ:

- Extended `src/Graphics/Passes/PostProcessGraphPass.h/.cpp` with a
  pass-local `GraphContext` and `AddToGraph`.
- Moved the final post-process render-graph `AddPass` wrapper out of
  `Renderer_RenderGraph.cpp`.
- Kept the renderer callbacks for bloom override binding, transition skipping,
  and `RenderPostProcess`, while the pass module owns the HDR/input/backbuffer
  graph contract.

Verified in Pass 8AJ:

```text
Targeted compile and link:
  ninja -C build -v CMakeFiles\CortexEngine.dir\src\Graphics\Passes\PostProcessGraphPass.cpp.obj CMakeFiles\CortexEngine.dir\src\Graphics\Renderer_RenderGraph.cpp.obj CortexEngine
  passed

No-op build:
  ninja -C build -n CortexEngine
  no work to do

RT showcase smoke:
  powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1 -NoBuild -IsolatedLogs -TemporalRuns 1 -SkipSurfaceDebug
  frames=33 gpu_ms=1.978/16.7 dxgi_mb=403.95/512
  est_mb=190.52/256 rt_mb=114.63/160 write_mb=107.75/128
  luma=70.81 center_luma=73.44 material_issues=0 transient_delta=0
  passed
```

### Pass 9: Visual Demo Maturity

Goal:

Use the cleaned renderer to produce beautiful scenes.

Tasks:

- Add material gallery.
- Add mirror/glass interior.
- Add sunset beach scene.
- Add foggy courtyard scene.
- Add water/reflection test scene.
- Add visual validation thresholds per scene.

Done when:

- The engine can produce a stable attractive scene with water, sun, fog, bloom,
  IBL/procedural sky, RT reflections, and correct materials.

## Architectural End State

Desired module layout:

```text
src/Graphics/
    Renderer.h/.cpp                    thin owner/facade
    Renderer_Initialization.cpp
    Renderer_Shutdown.cpp
    Renderer_RenderOrchestration.cpp
    Renderer_Diagnostics.cpp

    Frame/
        FrameFeaturePlan.h/.cpp
        FrameExecutionContext.h
        FrameContract.h/.cpp
        FrameContractValidation.h/.cpp
        TemporalManager.h/.cpp
        BudgetPlanner.h/.cpp

    Scene/
        RendererSceneSnapshot.h/.cpp
        RendererSceneBuilder.h/.cpp
        RenderableClassification.h/.cpp

    Materials/
        MaterialModel.h/.cpp
        MaterialResolver.h/.cpp
        MaterialPresetRegistry.h/.cpp
        MaterialGPUBridge.h/.cpp

    Passes/
        DepthPrepass.h/.cpp
        ShadowPass.h/.cpp
        VisibilityBufferPass.h/.cpp
        DeferredLightingPass.h/.cpp
        RTShadowPass.h/.cpp
        RTReflectionPass.h/.cpp
        RTGIPass.h/.cpp
        SSAOPass.h/.cpp
        SSRPass.h/.cpp
        TAAPass.h/.cpp
        BloomPass.h/.cpp
        WaterPass.h/.cpp
        TransparencyPass.h/.cpp
        PostProcessPass.h/.cpp

    RayTracing/
        RayTracingScheduler.h/.cpp
        RayTracingDenoiser.h/.cpp
        DX12Raytracing.h/.cpp

    GPUDriven/
        GPUCulling.h/.cpp
        HZBBuilder.h/.cpp
        IndirectDrawPlanner.h/.cpp

    Resources/
        RenderResourceRegistry.h/.cpp
        DescriptorFrameAllocator.h/.cpp
        TransientResourceAllocator.h/.cpp
        AssetRegistry.h/.cpp
```

The existing flat files do not need to be moved all at once. The priority is
first to establish ownership boundaries, then physical folder structure.

## Definition Of Phase 2 Complete

Phase 2 is complete when:

- `Renderer.cpp` is a facade, not the renderer brain.
- Every pass has an explicit contract.
- Render graph owns normal frame resource transitions.
- Renderer scene snapshot is the single source of renderable truth.
- Material model is shared by raster, VB, and RT.
- RT is scheduled, budgeted, and denoised.
- Temporal histories are centrally managed and diagnosable.
- GPU culling and HZB have conservative correctness tests.
- Low-memory profiles preserve advanced features through budgets, not by
  removing the renderer.
- Phase 2 validation suite passes.
- At least one polished visual demo scene proves the renderer's maturity.

## Immediate Next Implementation Target

The remaining renderer work should move in this order:

```text
1. Apply the pass-local contract pattern to SSAO, SSR, TAA, and
   post-process so the render graph owns normal frame transitions and resource
   lifetimes without hidden renderer state mutation.
2. Continue extracting physical pass modules. Bloom now has the first shared
   `Passes/BloomPass.h/.cpp` helper module; the remaining work is to move graph
   node construction out of `Renderer_RenderGraph.cpp` once neighboring pass
   contracts are equally explicit.
3. Build the first mature visual demo scene once the pass contracts are stable:
   sunset beach or glass/mirror interior with water, fog, bloom, IBL/procedural
   sky, RT reflections/GI, and validation thresholds.
4. Keep running the full gate after each renderer-contract change:
   - script rebuild,
   - no-op build,
   - focused debug-layer trace when a graph path changes,
   - RT showcase smoke,
   - budget profile matrix,
   - temporal validation smoke,
   - render-graph transient validation smoke,
   - debug-layer heap/log smoke.
```

The default fused bloom path now uses graph-owned transients without global
descriptor/resource mutation, and reusable binding/draw/descriptor helpers live
in physical Passes modules. Depth prepass, shadow, SSAO, SSR, TAA, motion
vectors, temporal rejection mask, and post-process now have pass-local graph
contracts.

## Pass 8AK - Visibility Buffer Graph Module

Completed:

- Added `Passes/VisibilityBufferGraphPass.h/.cpp` and registered it in the
  build.
- Moved the staged visibility-buffer graph declaration surface out of
  `Renderer_RenderGraph.cpp`:
  - `VBClear`
  - `VBVisibility`
  - `VBMaterialResolve`
  - `VBDebugBlit`
  - `VBBRDFLUT`
  - `VBClusteredLights`
  - `VBDeferredLighting`
- Kept renderer-owned orchestration, frame diagnostics, fallback reason
  logging, and GPU work callbacks in the renderer.
- Added a pass-local contract that fails before graph construction when base,
  G-buffer, material resolve, or debug resources are incomplete.

Verified:

- Targeted compile/link for `VisibilityBufferGraphPass.cpp`,
  `Renderer_RenderGraph.cpp`, and `CortexEngine`.
- No-op Ninja build after extraction.
- RT showcase smoke:
  `frames=33 gpu_ms=3.331/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.136 disocclusion=0.006906 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- Visibility-buffer debug view 35 smoke:
  `frames=33 gpu_ms=2.136/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0 surface_debug=view=35 colorful=0.249
  nonblack=0.851`.

Remaining immediate renderer graph debt:

- Move the legacy single-node `VisibilityBufferPath` fallback declaration into
  `VisibilityBufferGraphPass`.
- Leave `RGTransientValidationA/B` in renderer diagnostics unless they become
  part of a dedicated render-graph validation module.

## Pass 8AL - Visibility Buffer Legacy Graph Boundary

Completed:

- Added `VisibilityBufferGraphPass::LegacyPathContext`.
- Moved the legacy single-node `VisibilityBufferPath` fallback graph
  declaration from `Renderer_RenderGraph.cpp` into
  `VisibilityBufferGraphPass`.
- Kept legacy state mirrors and the `RenderVisibilityBufferPath` execution
  callback in the renderer because those still belong to renderer
  orchestration until the fallback is fully retired.
- Re-scanned `Renderer_RenderGraph.cpp`; only `RGTransientValidationA/B`
  remain as direct renderer-local `AddPass` calls.

Verified:

- Targeted compile/link for `VisibilityBufferGraphPass.cpp`,
  `Renderer_RenderGraph.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.613/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.217 disocclusion=0.006862 high_motion=0.005228
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Move `RGTransientValidationA/B` into a small diagnostics pass module so
  `Renderer_RenderGraph.cpp` no longer owns any graph pass declarations.

## Pass 8AM - Render Graph Validation Module

Completed:

- Added `Passes/RenderGraphValidationPass.h/.cpp`.
- Moved `RGTransientValidationA/B` graph declarations out of
  `Renderer_RenderGraph.cpp`.
- Kept descriptor allocation, RTV/SRV creation, frame-contract warning
  recording, and render-graph stat accumulation in the renderer because those
  depend on renderer-owned device and diagnostics state.
- Re-scanned `Renderer_RenderGraph.cpp`; there are no remaining direct
  `AddPass` calls in the renderer file.

Verified:

- Full CMake-regenerated Ninja build after adding the new pass module.
- No-op Ninja build after the rebuild.
- RT showcase smoke with transient validation enabled:
  `frames=33 gpu_ms=2.153/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.

Next immediate boundary:

- With render-graph pass declarations now physically owned by pass modules,
  continue reducing renderer orchestration weight: isolate frame execution
  inputs/outputs and move facade-level dispatch out of monolithic renderer
  methods where the dependency surface is already clear.

## Pass 9A - Frame Feature Plan Contract

Completed:

- Added `FrameFeaturePlan.cpp`.
- Moved runtime debug/environment switch parsing into
  `LoadRuntimeFrameDebugSwitches()`.
- Added `FrameFeaturePlanInputs` and `BuildFrameFeaturePlan()`.
- Moved planned/active/run/use feature derivation out of
  `Renderer_RenderOrchestration.cpp`.
- `Render()` now supplies renderer resource availability and consumes the
  resulting `FrameFeaturePlan` instead of directly owning the plan logic.

Verified:

- CMake-regenerated full build after adding `FrameFeaturePlan.cpp`.
- Targeted compile/link after extracting `BuildFrameFeaturePlan()`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=2.223/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.214 disocclusion=0.006875 high_motion=0.005229
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Continue shrinking `Render()` by extracting the per-frame reset/setup
  contract into a helper that initializes frame counters, draw counters,
  pass records, transient render-graph diagnostics, and frame context before
  pass execution begins.

## Pass 9B - Frame Execution Context Setup

Completed:

- Added `Renderer_FramePlanning.cpp`.
- Added `Renderer::ResetFrameExecutionState()`.
- Added `Renderer::BuildFrameExecutionContext()`.
- Moved frame counter updates, per-frame draw/pass diagnostics reset, motion
  history reset propagation, total-time update, and frame feature input
  assembly out of `Render()`.
- `Render()` now starts from a compact sequence:
  reset frame state, validate device/window prerequisites, build
  `FrameExecutionContext`, then execute the frame.

Verified:

- CMake-regenerated full build after adding `Renderer_FramePlanning.cpp`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=2.189/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.45
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.215 disocclusion=0.006913 high_motion=0.005239
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Extract pre-frame services from `Render()`: VRAM diagnostics,
  environment residency streaming, and queued GPU jobs should run behind a
  named pre-frame preparation method before the pass graph begins.

## Pass 9C - Pre-Frame Services Extraction

Completed:

- Added `Renderer::RunPreFrameServices(const FrameExecutionContext&)`.
- Moved VRAM diagnostics, environment residency streaming, queued GPU job
  processing, and the `Render_BeforeBeginFrame` marker out of
  `Renderer::Render`.
- `Render()` now builds `FrameExecutionContext`, applies the active feature
  mirrors, then calls a named pre-frame service step before `BeginFrame()`.

Verified:

- Targeted compile/link for `Renderer_FramePlanning.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.940/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.213 disocclusion=0.006923 high_motion=0.00524
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Continue shrinking frame orchestration by extracting begin-frame/pass-entry
  setup. The next named step should own `BeginFrame()` entry, GPU profiler
  frame startup, descriptor allocator reset, debug marker setup, and any early
  frame abort contract before scene snapshot and pass execution begin.

## Pass 9D - Begin-Frame Execution Boundary

Completed:

- Added `Renderer::BeginFrameExecution(FrameExecutionContext&)`.
- Moved `BeginFrame()`, the begin-frame breadcrumb, device-removal abort
  contract, VB list clearing, scene snapshot creation, RT frame-plan update,
  GPU profiler frame/scope startup, frame-contract snapshot publication,
  material descriptor prewarm, frame constants, descriptor usage capture, and
  transient render-graph validation out of `Renderer::Render`.
- Kept the top-level `Frame` GPU profiler scope balanced with the existing
  `EndFrame()` close path.

Verified:

- Targeted compile/link for `Renderer_FramePlanning.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=3.067/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.14 disocclusion=0.006813 high_motion=0.005218
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Extract special early frame paths from `Render()`: minimal debug frame and
  voxel backend execution should become a named backend selection step that
  either consumes and ends the frame or returns control to the normal raster/RT
  pass sequence.

## Pass 9E - Special Frame Path Extraction

Completed:

- Added `Renderer::TryRenderSpecialFramePath(const FrameExecutionContext&)`.
- Moved forced minimal-frame clear/present execution out of `Renderer::Render`.
- Moved voxel backend frame execution out of `Renderer::Render`.
- Added an explicit `MinimalFrame_Done` pass marker so diagnostics identify
  the real terminal path when the clear-only debug backend is active.
- `Render()` now enters the normal raster/RT pass sequence only after the
  special backend selector declines to consume the frame.

Verified:

- Targeted compile/link for `Renderer_FramePlanning.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=3.372/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.135 disocclusion=0.006854 high_motion=0.005225
  object_motion=0.0731 visible=7 warnings=0`.
- Direct forced minimal-frame run:
  `CORTEX_FORCE_MINIMAL_FRAME=1 ... --smoke-frames=60
  --exit-after-visual-validation` exited with code 0 and reported
  `Last completed pass: MinimalFrame_Done`.
- The general RT showcase harness is intentionally not used as the minimal
  frame gate because it validates full-scene RT/VB/TAA contracts that the
  clear-only debug path deliberately skips.

Next immediate boundary:

- Extract the RT/depth preparation phase from `Render()`. The next named
  method should own depth-prepass graph execution, RT shadow/GI dispatch,
  frame-pass records, pass markers, and `m_lastRTPassMs` timing so the normal
  frame sequence reads as coarse renderer phases instead of nested pass bodies.

## Pass 9F - Ray-Tracing Frame Phase Extraction

Completed:

- Added `Renderer_FramePhases.cpp` as the first normal-frame execution phase
  module.
- Added `Renderer::ExecuteRayTracingFramePhase(const FrameExecutionContext&)`.
- Moved depth-prepass graph execution, RT shadow/GI dispatch, GPU profiler
  scopes, depth/RT frame-pass records, pass markers, `m_lastDepthPrepassMs`,
  and `m_lastRTPassMs` timing out of `Renderer::Render`.
- Re-generated CMake after adding the new translation unit.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.560/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.52
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.217 disocclusion=0.006854 high_motion=0.005225
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Continue extracting coarse normal-frame phases from `Render()`. The next
  low-risk phase is shadow execution: it should own shadow graph selection,
  breadcrumb/profiler scope, frame-pass record, `m_lastShadowPassMs`, and the
  `RenderShadowPass_Done` marker.

## Pass 9G - Shadow Frame Phase Extraction

Completed:

- Added `Renderer::ExecuteShadowFramePhase(const FrameExecutionContext&)`.
- Moved shadow graph selection, shadow breadcrumb, GPU profiler scope,
  shadow frame-pass record, `RenderShadowPass_Done`, and
  `m_lastShadowPassMs` timing out of `Renderer::Render`.
- Kept the implementation in `Renderer_FramePhases.cpp` beside the extracted
  RT/depth phase.

Verified:

- Targeted compile/link for `Renderer_FramePhases.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=3.310/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.52
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.141 disocclusion=0.006802 high_motion=0.005214
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Extract main scene setup from `Render()`: `PrepareMainPass`, skybox
  execution, skybox frame-pass record, and the main-pass profiler entry should
  become a named phase before geometry/VB selection begins.

## Pass 9H - Main Scene Setup Extraction

Completed:

- Added `Renderer::BeginMainSceneFramePhase(const FrameExecutionContext&)`.
- Moved the broad `MainPass` GPU profiler scope entry, `PrepareMainPass`,
  skybox breadcrumb, skybox GPU scope, skybox draw call, skybox frame-pass
  record, and setup pass markers out of `Renderer::Render`.
- Preserved the existing main-pass lifetime by opening the broad main scope in
  the new helper and leaving the existing post-TAA close path in `Render()`.

Verified:

- Targeted compile/link for `Renderer_FramePhases.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=6.918/16.7 dxgi_mb=404.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.53
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=5.231 disocclusion=0.006815 high_motion=0.005218
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Extract the geometry/backend selection phase from `Render()`: visibility
  buffer execution, GPU-driven fallback path selection, late geometry
  overlays, water, transparent rendering, and geometry pass records should
  become a named phase with an explicit result describing whether VB handled
  the opaque scene and whether HZB work remains pending.

## Pass 9I - Geometry Frame Phase Extraction

Completed:

- Added `Renderer::ExecuteGeometryFramePhase(const FrameExecutionContext&)`.
- Moved hyper-geometry selection, visibility-buffer execution, VB fallback
  recovery, GPU-driven indirect fallback, legacy forward fallback, overlays,
  water, transparent rendering, geometry pass records, GPU scopes, and pass
  markers out of `Renderer::Render`.
- Kept the backend decision inside the normal frame phase module so later
  post, temporal, and RT reflection work only needs the resulting renderer
  state (`m_vbRenderedThisFrame`) instead of owning geometry policy.

Verified:

- Targeted compile/link for `Renderer_FramePhases.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=2.048/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.45
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.211 disocclusion=0.006867 high_motion=0.005215
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Extract the main-scene effects phase from `Render()`: RT reflections,
  motion vectors, temporal rejection mask, RT denoise, HZB scheduling, and TAA
  should become a named phase that returns the normal/roughness resource name
  and whether the end-frame graph still has pending HZB work.

## Pass 9J - Main Scene Effects Phase Extraction

Completed:

- Added `Renderer::MainSceneEffectsResult`.
- Added `Renderer::ExecuteMainSceneEffectsFramePhase(const FrameExecutionContext&)`.
- Moved RT reflections, motion-vector graph execution, temporal rejection mask
  execution, RT denoise, HZB resource validation/scheduling, legacy HZB
  fallback execution, TAA execution, pass records, GPU scopes, and pass markers
  out of `Renderer::Render`.
- Made the phase return the normal/roughness resource selected by geometry
  backend state plus the pending HZB flag consumed by the end-frame graph.

Verified:

- Targeted compile/link for `Renderer_FramePhases.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=2.224/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.42
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.221 disocclusion=0.006865 high_motion=0.005228
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Extract the post-processing phase from `Render()`: SSR timing, particles,
  SSAO timing, bloom/end-frame graph selection, post-process fallback, and
  post-process timing should move behind a named phase that consumes the
  normal/roughness resource name and pending HZB flag.

## Pass 9K - Post-Processing Frame Phase Extraction

Completed:

- Added `Renderer::ExecutePostProcessingFramePhase(const FrameExecutionContext&,
  const MainSceneEffectsResult&)`.
- Moved SSR, particle rendering, SSAO, bloom graph/fallback selection,
  end-frame graph execution, post-process fallback, pass records, pass
  markers, GPU scopes, and SSR/SSAO/bloom/post timing out of
  `Renderer::Render`.
- Kept the post phase explicit about its inputs: the main-scene effects result
  supplies the selected normal/roughness resource and the pending HZB graph
  flag needed by `ExecuteEndFrameInRenderGraph`.

Verified:

- Targeted compile/link for `Renderer_FramePhases.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=7.816/16.7 dxgi_mb=404.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.53
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=6.892 disocclusion=0.006777 high_motion=0.005211
  object_motion=0.0731 visible=7 warnings=0`.

Next immediate boundary:

- Finish the orchestration split by extracting debug-line rendering and
  `EndFrame()` into a final named frame-completion phase. After that,
  `Renderer::Render` should read as a high-level sequence of frame phases.

## Pass 9L - Frame Completion Phase Extraction

Completed:

- Added `Renderer::CompleteFrameExecutionPhase(const FrameExecutionContext&)`.
- Moved debug-line rendering, debug-line frame-pass record, debug-line GPU
  scope, pass marker, and `EndFrame()` out of `Renderer::Render`.
- Removed the now-unused local GPU begin lambda from `Renderer::Render`; the
  orchestration body now directly closes only the broad main-pass profiler
  scope that was opened by `BeginMainSceneFramePhase`.

Verified:

- Targeted compile/link for `Renderer_FramePhases.cpp`,
  `Renderer_RenderOrchestration.cpp`, and `CortexEngine`.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.982/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.218 disocclusion=0.006865 high_motion=0.005229
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer::Render` is now a high-level frame-phase sequencer instead of the
  owner of individual pass implementations.
- The extracted phase module owns normal-frame execution boundaries for RT,
  shadows, main setup, geometry/backend selection, main-scene effects,
  post-processing, debug lines, and end-frame completion.
- Remaining maturity work is no longer about shrinking `Render()`; the next
  useful pass should split `Renderer_FramePhases.cpp` into narrower phase
  modules or continue lower-level pass ownership cleanup inside the renderer
  subsystems called by these phases.

## Pass 9M - Frame Phase Module Split

Completed:

- Deleted the monolithic `Renderer_FramePhases.cpp` after the render loop phase
  extraction was complete.
- Added `Renderer_FramePhaseGpuScope.h` for the tiny shared internal GPU scope
  helpers used by extracted frame phases.
- Split normal-frame phase implementation into narrower translation units:
  - `Renderer_FramePhases_Prepare.cpp`: ray-tracing preparation and shadow
    phase execution.
  - `Renderer_FramePhases_Main.cpp`: main-scene setup, geometry backend
    selection, and main-scene effects.
  - `Renderer_FramePhases_Post.cpp`: post-processing and frame completion.
- Updated the CMake source list to build the new frame-phase files.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.468/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.48
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2 disocclusion=0.006745 high_motion=0.005206
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- Frame orchestration is now physically split as well as logically split:
  `Renderer::Render` sequences named phases, and each phase group lives in a
  smaller file with a clearer dependency surface.
- Current frame-phase file sizes are:
  - prepare: 79 lines
  - main: 264 lines
  - post: 201 lines
- The next useful pass should target the remaining large ownership centers
  rather than continuing to shave already-small frame phase files. The highest
  leverage candidates are `Renderer_RenderGraph.cpp`,
  `Renderer_RenderTargets.cpp`, `Renderer_GPUDriven.cpp`, `Renderer.h`, and
  `VisibilityBuffer.cpp`.

## Pass 9N - HZB Renderer Boundary

Completed:

- Added `Renderer_HZB.cpp`.
- Moved HZB mip-count calculation, HZB resource allocation, legacy HZB build,
  and render-graph HZB pass scheduling out of `Renderer_RenderTargets.cpp`.
- Kept the public renderer method surface unchanged while making HZB a named
  renderer subsystem with its own source file.
- Removed the HZB pass include and depth sample-state helper from the generic
  render-target file.
- Updated the CMake source list to build the new HZB renderer module.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.547/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.53
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.064 disocclusion=0.006743 high_motion=0.005206
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderTargets.cpp` dropped from 1572 lines to 1213 lines.
- `Renderer_HZB.cpp` is 354 lines and owns the depth-pyramid resource and
  graph scheduling boundary.
- HZB was exercised in both visual smokes:
  `HZB resources created: 1088x612, mips=11` and
  `HZB RG: passes=23, barriers=46`.

Next immediate boundary:

- Continue splitting `Renderer_RenderTargets.cpp` by moving the ray-tracing
  auxiliary render targets (`CreateRTShadowMask`, `CreateRTGIResources`, and
  `CreateRTReflectionResources`) into a dedicated renderer RT resource module.

## Pass 9O - RT Resource Boundary

Completed:

- Added `Renderer_RTResources.cpp`.
- Moved RT auxiliary target creation out of `Renderer_RenderTargets.cpp`:
  - `CreateRTShadowMask`
  - `CreateRTGIResources`
  - `CreateRTReflectionResources`
- Kept descriptor table updates and history invalidation behavior inside the
  moved functions so the renderer's RT feature behavior remains unchanged.
- Updated the CMake source list to build the new RT resource module.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.918/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.45
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.012 disocclusion=0.006777 high_motion=0.00521
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderTargets.cpp` dropped from 1213 lines to 732 lines.
- `Renderer_RTResources.cpp` is 458 lines and owns the RT shadow, RT GI, and
  RT reflection auxiliary render-target allocation boundary.
- `Renderer_RenderTargets.cpp` now primarily owns generic depth, shadow-map,
  HDR/G-buffer, TAA/SSR/velocity, temporal rejection mask, bloom, and SSAO
  render-target resources.

Next immediate boundary:

- Move shadow-map resource allocation and safe-size recreation into a dedicated
  shadow resource file, leaving `Renderer_ShadowPass.cpp` focused on rendering
  and `Renderer_RenderTargets.cpp` focused on generic frame targets.

## Pass 9P - Shadow Resource Boundary

Completed:

- Added `Renderer_ShadowResources.cpp`.
- Moved shadow-map resource allocation and safe-size recreation out of
  `Renderer_RenderTargets.cpp`:
  - `CreateShadowMapResources`
  - `RecreateShadowMapResourcesForCurrentSize`
- Kept shadow rendering code in `Renderer_ShadowPass.cpp`; this pass only
  separated shadow atlas allocation and descriptor creation.
- Removed the now-unused `<cmath>` include from `Renderer_RenderTargets.cpp`.
- Updated the CMake source list to build the new shadow resource module.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.550/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.018 disocclusion=0.006853 high_motion=0.005224
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderTargets.cpp` dropped from 732 lines to 609 lines.
- `Renderer_ShadowResources.cpp` is 120 lines and owns shadow atlas allocation,
  DSV/SRV descriptor creation, viewport/scissor sizing, and safe-size
  recreation.
- The former generic target file has now shed HZB, RT auxiliary targets, and
  shadow atlas ownership. Its remaining responsibility is mostly generic
  per-frame render targets: depth, HDR/G-buffer, TAA history/intermediate, SSR,
  velocity, temporal rejection mask, bloom, and SSAO.

Next immediate boundary:

- Decide whether to continue shrinking `Renderer_RenderTargets.cpp` by
  extracting temporal/HDR targets, or pivot back to higher-value large owners:
  `Renderer_RenderGraph.cpp`, `Renderer_GPUDriven.cpp`, `Renderer.h`, and
  `VisibilityBuffer.cpp`.

## Pass 9Q - Render Graph Diagnostics And Depth/Shadow Boundaries

Completed:

- Added `Renderer_RenderGraphDiagnostics.cpp`.
- Moved render-graph execution statistics and transient validation out of
  `Renderer_RenderGraph.cpp`:
  - `AccumulateRenderGraphExecutionStats`
  - `RunRenderGraphTransientValidation`
- Added `Renderer_RenderGraphDepthShadow.cpp`.
- Moved simple depth and shadow graph adapters out of
  `Renderer_RenderGraph.cpp`:
  - `ExecuteDepthPrepassInRenderGraph`
  - `ExecuteShadowPassInRenderGraph`
- Updated the CMake source list to build both new render-graph modules.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.876/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.54
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.504 disocclusion=0.006808 high_motion=0.005217
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderGraph.cpp` dropped from 2053 lines to 1805 lines.
- `Renderer_RenderGraphDiagnostics.cpp` is 138 lines and owns graph execution
  telemetry plus transient-resource validation.
- `Renderer_RenderGraphDepthShadow.cpp` is 127 lines and owns the depth and
  shadow graph adapter boundary.
- `Renderer_RenderGraph.cpp` now mainly owns visibility-buffer graph execution,
  temporal/screen-space graph adapters, bloom graph execution, and end-frame
  post-process orchestration.

Next immediate boundary:

- Extract the motion vector, temporal rejection mask, TAA, SSR, and SSAO graph
  adapters into a temporal/screen-space graph module. That keeps
  `Renderer_RenderGraph.cpp` moving toward visibility-buffer and end-frame
  ownership only.

## Pass 9R - Temporal And Screen-Space Render Graph Boundary

Completed:

- Added `Renderer_RenderGraphTemporalScreen.cpp`.
- Moved temporal and screen-space graph adapters out of
  `Renderer_RenderGraph.cpp`:
  - `ExecuteMotionVectorsInRenderGraph`
  - `ExecuteTemporalRejectionMaskInRenderGraph`
  - `ExecuteTAAInRenderGraph`
  - `ExecuteSSRInRenderGraph`
  - `ExecuteSSAOInRenderGraph`
- Replaced the visibility-buffer-specific shader-resource-state helper with a
  local temporal/screen-space helper inside the new module.
- Cleaned the old graph file's include list after the extraction.
- Updated the CMake source list to build the new module.

Verified:

- Full CMake-regenerated Ninja build.
- Incremental Ninja build after include cleanup.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=2.115/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.98 disocclusion=0.006829 high_motion=0.005204
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderGraph.cpp` dropped from 1805 lines to 1333 lines.
- `Renderer_RenderGraphTemporalScreen.cpp` is 646 lines and owns graph-backed
  motion vectors, temporal mask generation, TAA, SSR, and SSAO.
- The remaining `Renderer_RenderGraph.cpp` responsibilities are visibility
  buffer graph execution, standalone bloom graph execution, and end-frame
  HZB/post-process orchestration.

Next immediate boundary:

- Extract the visibility-buffer graph adapter and its local helper structs into
  a dedicated visibility-buffer graph module. After that,
  `Renderer_RenderGraph.cpp` can be reduced to bloom and end-frame
  orchestration before those are split as separate final graph owners.

## Pass 9S - Visibility Buffer Render Graph Boundary

Completed:

- Added `Renderer_RenderGraphVisibilityBuffer.cpp`.
- Moved the visibility-buffer graph adapter and local helper types out of
  `Renderer_RenderGraph.cpp`:
  - `ExecuteVisibilityBufferInRenderGraph`
  - visibility-buffer debug-view constants
  - visibility-buffer graph resource import helpers
  - visibility-buffer debug-source selection helpers
- Replaced the old file's visibility-buffer shader-resource-state helper with a
  generic render-graph shader-resource-state helper for bloom/end-frame use.
- Cleaned no-longer-needed visibility-buffer includes from
  `Renderer_RenderGraph.cpp`.
- Updated the CMake source list to build the new visibility-buffer graph
  module.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.468/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.138 disocclusion=0.006689 high_motion=0.005187
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderGraph.cpp` dropped from 1333 lines to 724 lines.
- `Renderer_RenderGraphVisibilityBuffer.cpp` is 627 lines and owns both the
  staged visibility-buffer graph path and legacy visibility-buffer graph
  adapter.
- The remaining `Renderer_RenderGraph.cpp` responsibilities are standalone
  bloom graph execution and end-frame HZB/post-process orchestration.

Next immediate boundary:

- Extract standalone bloom graph execution into a bloom graph module, then leave
  the original file as the end-frame graph orchestration owner.

## Pass 9T - Standalone Bloom Render Graph Boundary

Completed:

- Added `Renderer_RenderGraphBloom.cpp`.
- Moved standalone bloom graph execution out of the generic render-graph file:
  - `ExecuteBloomInRenderGraph`
  - standalone bloom transient import/declaration logic
  - graph-owned bloom descriptor rebinding/restoration logic
  - standalone bloom pass callbacks for downsample, blur, composite, and copy
- Kept fused bloom inside end-frame orchestration because that path is declared
  together with post-process and back-buffer ownership.
- Updated the CMake source list to build the new bloom graph module.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=2.199/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.53
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.141 disocclusion=0.006828 high_motion=0.005215
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- The former `Renderer_RenderGraph.cpp` dropped from 724 lines to 428 lines.
- `Renderer_RenderGraphBloom.cpp` is 321 lines and owns standalone bloom graph
  execution.
- The remaining generic render-graph owner only contains end-frame HZB,
  fused-bloom, and post-process graph orchestration.

Next immediate boundary:

- Rename the remaining generic render-graph file to make its ownership
  explicit as end-frame graph orchestration.

## Pass 9U - End-Frame Render Graph Owner

Completed:

- Renamed the remaining `Renderer_RenderGraph.cpp` owner to
  `Renderer_RenderGraphEndFrame.cpp`.
- Removed the generic render-graph source from the CMake source list.
- Updated CMake to build `Renderer_RenderGraphEndFrame.cpp` instead.
- Kept the function body unchanged; this pass is an ownership/name correction
  after the previous adapter extractions.

Verified:

- Full CMake-regenerated Ninja build.
- No-op Ninja build.
- RT showcase smoke:
  `frames=33 gpu_ms=1.937/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.51
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.927 disocclusion=0.006734 high_motion=0.005201
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_RenderGraph.cpp` no longer exists as a generic mixed owner.
- `Renderer_RenderGraphEndFrame.cpp` is 428 lines and owns end-frame HZB,
  fused-bloom, and post-process graph orchestration.
- The render-graph adapter layer is now split by responsibility:
  - `Renderer_RenderGraphVisibilityBuffer.cpp`
  - `Renderer_RenderGraphTemporalScreen.cpp`
  - `Renderer_RenderGraphBloom.cpp`
  - `Renderer_RenderGraphDepthShadow.cpp`
  - `Renderer_RenderGraphDiagnostics.cpp`
  - `Renderer_RenderGraphEndFrame.cpp`

Next immediate boundary:

- Continue with the next large owner outside render-graph adapters. The highest
  leverage remaining candidates are `Renderer_GPUDriven.cpp`, `Renderer.h`,
  `VisibilityBuffer.cpp`, and the generic render-target resource file.

## Pass 9V - GPU-Driven Renderer Ownership Boundary

Completed:

- Added `Renderer_VisibilityBufferPath.cpp`.
- Moved the visibility-buffer scene path out of `Renderer_GPUDriven.cpp`:
  - `CollectInstancesForVisibilityBuffer`
  - `GetVisibilityBufferDebugView`
  - `ResolveVisibilityBufferCullMask`
  - `LogVisibilityBufferFirstFrame`
  - `RenderVisibilityBufferVisibilityStage`
  - `RenderVisibilityBufferMaterialResolveStage`
  - `PrepareVisibilityBufferDeferredLighting`
  - `ApplyVisibilityBufferDeferredLighting`
  - `RenderVisibilityBufferDeferredLightingStage`
  - `RenderVisibilityBufferPath`
- Added `Renderer_IndirectRendering.cpp`.
- Moved indirect-scene rendering out of `Renderer_GPUDriven.cpp`:
  - `RenderSceneIndirect`
  - indirect command upload and fallback behavior
  - optional HZB occlusion setup for GPU culling
  - culling freeze diagnostics
  - indirect draw statistics
- Trimmed `Renderer_GPUDriven.cpp` includes so it now owns only GPU-culling
  controls, instance collection, and dispatch.
- Updated the CMake source list to build both new renderer modules.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.923/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.259 disocclusion=0.006853 high_motion=0.005224
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `Renderer_GPUDriven.cpp` dropped from 1538 lines to 125 lines.
- `Renderer_VisibilityBufferPath.cpp` is 1092 lines and owns the complete
  visibility-buffer render path.
- `Renderer_IndirectRendering.cpp` is 345 lines and owns legacy indirect scene
  rendering.
- The GPU-driven boundary is now explicit enough for correctness work:
  culling control/dispatch, visibility-buffer rendering, and indirect fallback
  rendering are separate source owners.

Next immediate boundary:

- Continue with the next high-leverage mixed owner. The best candidates are
  `VisibilityBuffer.cpp` for pass-internal decomposition and `Renderer.h` for
  reducing the central renderer declaration into smaller owned contracts.

## Pass 9W - Visibility Buffer Pipeline Ownership Boundary

Completed:

- Added `VisibilityBuffer_Pipelines.cpp`.
- Moved visibility-buffer root-signature and PSO construction out of
  `VisibilityBuffer.cpp`:
  - `CreateRootSignatures`
  - `CreatePipelines`
  - visibility, alpha-test, material-resolve, motion-vector, clustered-light,
    BRDF LUT, debug-blit, and deferred-lighting pipeline setup
- Moved the dynamic descriptor range flag helper into the pipeline module.
- Trimmed no-longer-needed shader compiler and root-signature includes from
  `VisibilityBuffer.cpp`.
- Updated the CMake source list to build the new pipeline module.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=2.090/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.04 disocclusion=0.006741 high_motion=0.005205
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `VisibilityBuffer.cpp` dropped from 3852 lines to 2588 lines.
- `VisibilityBuffer_Pipelines.cpp` is 1277 lines and owns all VB pipeline
  construction.
- The remaining `VisibilityBuffer.cpp` responsibilities are initialization,
  resource allocation, per-frame uploads, visibility/material/deferred runtime
  execution, and debug blits.

Next immediate boundary:

- Split visibility-buffer resource ownership from per-frame execution. The
  next safe extraction is resource allocation and resize-owned helpers:
  `CreateVisibilityBuffer`, `CreateGBuffers`, `Shutdown`, and `Resize`.

## Pass 9X - Visibility Buffer Resource Ownership Boundary

Completed:

- Added `VisibilityBuffer_Resources.cpp`.
- Moved visibility-buffer lifetime and resize-owned resource creation out of
  `VisibilityBuffer.cpp`:
  - `Shutdown`
  - `Resize`
  - `CreateVisibilityBuffer`
  - `CreateGBuffers`
- Updated the CMake source list to build the new resource module.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.922/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.982 disocclusion=0.006732 high_motion=0.0052
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `VisibilityBuffer.cpp` dropped from 2588 lines to 2042 lines.
- `VisibilityBuffer_Resources.cpp` is 551 lines and owns VB resize/lifetime
  resource allocation.
- The visibility-buffer implementation is now split into pipeline creation,
  resource ownership, and the remaining runtime execution/upload paths.

Next immediate boundary:

- Split per-frame data uploads and mesh-table staging into a dedicated module:
  `UpdateInstances`, `UpdateMaterials`, `UpdateReflectionProbes`,
  `UpdateLocalLights`, and `UpdateMeshTable`.

## Pass 9Y - Visibility Buffer Upload Ownership Boundary

Completed:

- Added `VisibilityBuffer_Uploads.cpp`.
- Moved per-frame visibility-buffer upload and staging code out of
  `VisibilityBuffer.cpp`:
  - `UpdateInstances`
  - `UpdateMaterials`
  - `UpdateReflectionProbes`
  - `UpdateLocalLights`
  - `UpdateMeshTable`
- Updated the CMake source list to build the upload module.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.565/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.99 disocclusion=0.006749 high_motion=0.005206
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- `VisibilityBuffer.cpp` dropped from 2042 lines to 1649 lines.
- `VisibilityBuffer_Uploads.cpp` is 405 lines and owns upload-buffer growth,
  per-frame memcpy staging, local-light/probe staging, and mesh-table upload.
- The remaining `VisibilityBuffer.cpp` is now focused on runtime passes:
  visibility clear/raster, material resolve, motion vectors, debug blits,
  clustered lighting, BRDF LUT generation, and deferred lighting.

Next immediate boundary:

- Split runtime pass execution by stage. The next safe extractions are
  visibility/raster execution, material/motion resolve, debug blits, and
  deferred lighting.

## Pass 9Z - Visibility Buffer Runtime Stage Boundaries

Completed:

- Added `VisibilityBuffer_VisibilityPass.cpp`.
- Added `VisibilityBuffer_Resolve.cpp`.
- Added `VisibilityBuffer_DebugBlit.cpp`.
- Added `VisibilityBuffer_DeferredLighting.cpp`.
- Moved the remaining visibility-buffer runtime stages out of
  `VisibilityBuffer.cpp`:
  - visibility clear/raster execution
  - material resolve and motion-vector compute
  - G-buffer, visibility, depth, and material debug blits
  - clustered light-list construction
  - BRDF LUT generation
  - deferred lighting composite
- Updated the CMake source list to build the new stage modules.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.559/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.43
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.1 disocclusion=0.006758 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- `VisibilityBuffer.cpp` dropped from 1649 lines to 385 lines and now owns
  initialization only.
- The visibility-buffer implementation is now separated into explicit
  ownership modules:
  - `VisibilityBuffer_Pipelines.cpp` - root signatures and PSOs
  - `VisibilityBuffer_Resources.cpp` - lifetime, resize, G-buffer, and VB
    resources
  - `VisibilityBuffer_Uploads.cpp` - per-frame instance/material/probe/light
    uploads
  - `VisibilityBuffer_VisibilityPass.cpp` - clear and raster pass execution
  - `VisibilityBuffer_Resolve.cpp` - material resolve and motion vectors
  - `VisibilityBuffer_DebugBlit.cpp` - debug views and G-buffer SRV accessors
  - `VisibilityBuffer_DeferredLighting.cpp` - clustered lighting, BRDF LUT,
    and deferred lighting

Next immediate boundary:

- Move to the next large renderer subsystem now that the visibility-buffer
  implementation has a stable module boundary. The largest remaining graphics
  source is `GPUCulling.cpp`; split it by pipeline/resource/upload/dispatch
  ownership before returning to higher-risk `Renderer.h` header surgery.

## Pass 10A - GPU Culling Ownership Boundaries

Completed:

- Added `GPUCullingInternal.h` for the shader-matched cull constant layout.
- Split `GPUCulling.cpp` into focused modules:
  - `GPUCulling.cpp` - lifecycle only
  - `GPUCulling_Pipelines.cpp` - compute root signature, PSO, and indirect
    command signature
  - `GPUCulling_Resources.cpp` - buffers, UAV/SRV descriptors, HZB fallback,
    history buffers, debug buffers, and constant-buffer allocation
  - `GPUCulling_Uploads.cpp` - instance uploads, indirect command uploads,
    and all-command ExecuteIndirect preparation
  - `GPUCulling_Readback.cpp` - command count readback, debug counter readback,
    and optional visible-command logging
  - `GPUCulling_Dispatch.cpp` - HZB binding, frustum extraction, culling
    dispatch, UAV barriers, history ping-pong, and post-dispatch transitions
- Updated the CMake source list to build the new GPU culling modules.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.910/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.99 disocclusion=0.006749 high_motion=0.005207
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- `GPUCulling.cpp` dropped from 1521 lines to 69 lines.
- GPU culling now has explicit owner modules for pipeline setup, resource
  lifetime, upload/indirect preparation, readback telemetry, and dispatch.
- This reduces risk for future optimization work on HZB occlusion, visibility
  masks, indirect draw fallback, and debug readback because each concern is no
  longer interleaved in one file.

Next immediate boundary:

- Reassess the remaining large graphics implementation files. The next low-risk
  extraction candidates are implementation-only modules such as
  `Renderer_PipelineSetup.cpp`, `Renderer_AssetUpload.cpp`, and
  `Renderer_DebugPrimitives.cpp`. Defer `Renderer.h` splitting until more
  implementation churn is isolated, because that header is the broadest compile
  dependency.

## Pass 10B - Debug Primitive System Boundaries

Completed:

- Split the mixed `Renderer_DebugPrimitives.cpp` implementation into owned
  modules:
  - `Renderer_Particles.cpp` - particle collection, frustum filtering, upload
    buffers, quad setup, and particle draw submission
  - `Renderer_Vegetation.cpp` - vegetation instance buffers, billboard/grass
    staging, vegetation atlas binding, and placeholder vegetation render hooks
  - `Renderer_DebugLines.cpp` - debug-line staging and draw submission
- Left `Renderer_DebugPrimitives.cpp` as a tiny compatibility stub so the file
  can be removed from the build list later when the CMake source list is
  normalized.
- Updated the CMake source list to build the new modules.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.575/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.967 disocclusion=0.006838 high_motion=0.005222
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- `Renderer_DebugPrimitives.cpp` dropped from 933 lines to 7 lines.
- Particle rendering, vegetation staging, and debug-line drawing now have
  separate implementation owners.
- This clears another mixed-purpose renderer file without changing public
  `Renderer` declarations.

Next immediate boundary:

- Continue with implementation-only splits before touching `Renderer.h`.
  `Renderer_AssetUpload.cpp` is the next good target because it mixes mesh
  upload, file texture loading, generated texture creation, placeholder
  fallback descriptors, texture memory estimation, and bounds-proxy fallback.

## Pass 10C - Asset Upload Boundaries

Completed:

- Split the mixed `Renderer_AssetUpload.cpp` implementation into owned
  modules:
  - `Renderer_MeshUpload.cpp` - mesh GPU upload, bounds-proxy fallback, upload
    command-list use, persistent mesh SRVs, and queued mesh upload jobs
  - `Renderer_TextureLoad.cpp` - file texture loading, compressed DDS sibling
    lookup, mip admission, texture-memory estimation, texture cache lookup, and
    asset-registry accounting
  - `Renderer_TextureCreation.cpp` - generated RGBA textures, placeholder
    fallback textures, fallback material descriptors, and reserved bindless
    placeholder slots
- Left `Renderer_AssetUpload.cpp` as a tiny compatibility stub so the CMake
  source list can be normalized in a later cleanup pass.
- Updated the CMake source list to build the new modules.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.948/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.052 disocclusion=0.006856 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- `Renderer_AssetUpload.cpp` dropped from 1082 lines to 7 lines.
- Startup-critical asset paths remain covered by the smoke tests:
  placeholders are created, DDS textures load, glTF mesh upload runs, and the
  RT showcase plus temporal validation scenes render cleanly.

Next immediate boundary:

- The large mixed implementation files are now mostly stage-specific. The next
  pass should either normalize empty compatibility stubs from the build list or
  start splitting broad renderer files with clear internal seams, especially
  `Renderer_FrameSetup.cpp`, `Renderer_Settings.cpp`, and
  `Renderer_FrameLifecycle.cpp`.

## Pass 10D - Source List Stub Cleanup

Completed:

- Removed empty compatibility stubs from the CMake source list:
  - `Renderer_AssetUpload.cpp`
  - `Renderer_DebugPrimitives.cpp`
- Deleted those empty stub files now that their implementation code lives in
  owned modules.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- The source list now reflects the actual module ownership for mesh upload,
  texture loading, generated textures, particles, vegetation, and debug lines.
- No behavior changed in this cleanup pass; the prior smoke pair already
  covered the moved implementations before the empty stubs were removed.

Next immediate boundary:

- Continue with the next broad implementation file. Good candidates are
  `Renderer_FrameSetup.cpp` for frame-input/resource preparation,
  `Renderer_FrameLifecycle.cpp` for begin/end/synchronization handoff, and
  `Renderer_Settings.cpp` for runtime feature/configuration controls.

## Pass 10E - Frame Setup Boundaries

Completed:

- Split the mixed `Renderer_FrameSetup.cpp` implementation into owned modules:
  - `Renderer_MainPassSetup.cpp` - main pass render-target/depth attachment
    selection, clear/load policy, and viewport/scissor setup.
  - `Renderer_FrameConstants.cpp` - camera matrices, temporal jitter,
    previous-frame contracts, lighting constants, water/fog/post parameters,
    and feature flags uploaded to the frame constant buffer.
- Removed `Renderer_FrameSetup.cpp` from the CMake source list and deleted the
  old mixed file.

Verified:

- Full CMake-regenerated Ninja build with explicit MSVC/Windows SDK
  environment.
- RT showcase smoke:
  `frames=33 gpu_ms=2.151/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.347 disocclusion=0.006719 high_motion=0.005194
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Main pass attachment policy is now separated from frame constants and
  temporal camera state.
- The next high-value cleanup is `Renderer_Settings.cpp`, which currently owns
  unrelated runtime control surfaces in one file.

Next immediate boundary:

- Split `Renderer_Settings.cpp` into owned runtime-control modules while
  preserving the existing `Renderer` public API.

## Pass 10F - Runtime Settings Boundaries

Completed:

- Split the mixed `Renderer_Settings.cpp` implementation into owned modules:
  - `Renderer_FeatureSettings.cpp` - TAA, SSR, fog, god rays, area-light
    scale, and ray-tracing feature toggles.
  - `Renderer_DebugSettings.cpp` - debug view cycling, debug view selection,
    and HZB mip debug selection.
  - `Renderer_ShadowSettings.cpp` - shadow enablement, bias, PCF radius,
    cascade split, and cascade resolution scale controls.
  - `Renderer_SurfaceSettings.cpp` - water wave parameters, water height
    sampling, and procedural fractal surface parameters.
  - `Renderer_LightingSettings.cpp` - lighting rig presets, environment
    preset selection/cycling, IBL intensity/enabled state, and sun controls.
  - `Renderer_QualitySettings.cpp` - exposure, bloom intensity, color grade,
    and the memory-budget quality preset.
- Removed `Renderer_Settings.cpp` from the CMake source list and deleted the
  old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.457/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.54
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.249 disocclusion=0.00666 high_motion=0.005176
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Runtime renderer controls now have file-level ownership instead of one
  catch-all settings file.
- Scene startup still exercises the moved quality, lighting, fog, bloom, RT,
  IBL, shadow, and SSAO-adjacent controls through the smoke scenes.

Next immediate boundary:

- Continue reducing high-line renderer files. Good next targets are
  `Renderer_FrameLifecycle.cpp` for begin/end frame orchestration and
  `Renderer_FrameContracts.cpp` for contract publication/validation flow.

## Pass 10G - Frame Lifecycle Boundaries

Completed:

- Split the mixed `Renderer_FrameLifecycle.cpp` implementation into owned
  modules:
  - `Renderer_FrameBegin.cpp` - begin-frame fencing, descriptor frame setup,
    resize detection/reallocation, upload allocator reuse, and command-list
    reset/heap binding.
  - `Renderer_FrameEnd.cpp` - RT history copies, late resource-state cleanup,
    frame profiler closure, visual-validation back-buffer capture, present,
    device-removed reporting, and frame fence signaling.
  - `Renderer_TemporalReadback.cpp` - temporal rejection-mask stats readback
    and normalization.
- Removed `Renderer_FrameLifecycle.cpp` from the CMake source list and deleted
  the old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=2.265/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.348 disocclusion=0.00668 high_motion=0.005183
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Begin-frame and end-frame responsibilities are no longer interleaved with
  visual capture and temporal stats readback helpers.
- Frame lifecycle remains behavior-preserving while giving later optimization
  passes clear ownership points for resize policy, resource cleanup, present,
  and temporal telemetry.

Next immediate boundary:

- Continue with `Renderer_FrameContracts.cpp`. The likely split is:
  memory/descriptor estimates, temporal history validity, pass recording, frame
  contract snapshot publication, and validation/export.

## Pass 10H - Frame Contract Boundaries

Completed:

- Split the mixed `Renderer_FrameContracts.cpp` implementation into owned
  modules:
  - `Renderer_FrameContractMemory.cpp` - RT warmup checks, estimated VRAM
    breakdown, total estimated VRAM, and descriptor heap statistics.
  - `Renderer_FrameContractHistories.cpp` - TAA/RT history invalidation,
    history validity marking, temporal history contract publication, late
    culling state, ray-tracing scheduler state, and warning updates.
  - `Renderer_FrameContractPasses.cpp` - descriptor snapshot capture and
    per-pass read/write/resource/descriptor contract recording.
  - `Renderer_FrameContractSnapshot.cpp` - per-frame contract construction:
    frame size, feature plan, lighting state, scene snapshot, resource list,
    culling state, RT state, asset memory, draw counts, pass records, and
    RenderGraph summary.
  - `Renderer_FrameContractValidationBridge.cpp` - renderer-side validation
    context construction and material-validation warning bridge.
- Removed `Renderer_FrameContracts.cpp` from the CMake source list and deleted
  the old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.950/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.50
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.239 disocclusion=0.006722 high_motion=0.005193
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Frame contracts now have explicit implementation owners. This is important
  for the next renderer maturity phase because diagnostics, validation, memory
  reporting, and pass records can evolve independently.
- The verified smoke output still reports frame diagnostics, resource state
  summaries, material checks, and VRAM budget checks after the split.

Next immediate boundary:

- Continue reducing renderer-wide implementation files. Current high-value
  candidates are `Renderer_PipelineSetup.cpp`, `Renderer_VisibilityBufferPath.cpp`,
  `Renderer_ScreenSpace.cpp`, and `Renderer_RenderTargets.cpp`.

## Pass 10I - Render Target Resource Boundaries

Completed:

- Split the mixed `Renderer_RenderTargets.cpp` implementation into owned
  render-resource modules:
  - `Renderer_DepthTarget.cpp` - depth texture creation, writable/read-only
    DSV descriptors, depth SRV creation, and depth state initialization.
  - `Renderer_HDRTargets.cpp` - HDR target allocation plus dependent
    frame-sized color resources: G-buffer normal/roughness, TAA history and
    intermediate buffers, SSR color, velocity, bloom, and SSAO resources.
  - `Renderer_TemporalResources.cpp` - temporal rejection-mask texture,
    SRV/UAV descriptors, stats UAV buffer, and per-frame readback buffers.
- Removed `Renderer_RenderTargets.cpp` from the CMake source list and deleted
  the old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.926/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.48
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.361 disocclusion=0.00678 high_motion=0.005211
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Frame-sized target ownership is no longer grouped into one catch-all
  allocator file. Depth, HDR-dependent resources, and temporal mask resources
  can now evolve independently.
- Resize/reallocation bugs are easier to isolate because the resource lifetime
  boundary now matches the render feature that owns the texture.

Next immediate boundary:

- Split `Renderer_ScreenSpace.cpp` into owned screen-space pass modules:
  skybox, SSR, TAA resolve/history copies, motion vectors, and temporal
  rejection-mask execution/stats.

## Pass 10J - Screen-Space Pass Boundaries

Completed:

- Split the mixed `Renderer_ScreenSpace.cpp` implementation into owned
  screen-space execution modules:
  - `Renderer_SkyboxPass.cpp` - skybox/procedural-sky draw dispatch.
  - `Renderer_SSRPass.cpp` - SSR resource transitions, normal-source
    selection, descriptor validation, and SSR pass dispatch.
  - `Renderer_TAAExecution.cpp` - TAA history seeding, TAA resolve,
    intermediate HDR copy, history copy-back, and disabled-history handling.
  - `Renderer_MotionVectors.cpp` - visibility-buffer motion-vector dispatch
    and camera-only fallback path.
  - `Renderer_TemporalMaskPass.cpp` - temporal rejection-mask compute dispatch,
    stats dispatch, readback copy, and pass-contract recording.
- Removed `Renderer_ScreenSpace.cpp` from the CMake source list and deleted
  the old mixed file.
- Made the shared depth-sampling state dependency explicit through
  `RendererGeometryUtils.h` in the modules that transition depth for sampling.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.462/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.147 disocclusion=0.006731 high_motion=0.0052
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- The major screen-space features no longer share one broad source file. This
  gives future quality work clear ownership for TAA stability, SSR correctness,
  motion-vector validity, and temporal rejection telemetry.
- The validation scene still exercises motion vectors, temporal rejection, TAA,
  SSR-enabled post flow, and resource-state diagnostics after the split.

Next immediate boundary:

- Continue reducing renderer-wide implementation files with a bias toward
  behavior-preserving ownership splits. Good next targets are
  `Renderer_LateGeometry.cpp`, `Renderer_RayTracing.cpp`, and
  `Renderer_VisibilityBufferPath.cpp`; `Renderer_LateGeometry.cpp` is the
  lowest-risk next split because it is largely draw-stage ownership.

## Pass 10K - Late Geometry Boundaries

Completed:

- Split the mixed `Renderer_LateGeometry.cpp` implementation into owned late
  draw-stage modules:
  - `Renderer_OverlayGeometry.cpp` - overlay collection, deterministic overlay
    ordering, material constants, depth separation, and overlay draw counts.
  - `Renderer_WaterSurfaces.cpp` - water-surface filtering, read-only depth
    handoff, water material constants, and water draw counts.
  - `Renderer_TransparentGeometry.cpp` - transparent draw collection,
    frustum culling, far-to-near sorting, material classification, extended
    material constants, and transparent draw counts.
- Removed `Renderer_LateGeometry.cpp` from the CMake source list and deleted
  the old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.577/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.48
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.894 disocclusion=0.006766 high_motion=0.005209
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Late-frame composition has explicit source ownership for overlays, water,
  and transparent geometry. This reduces the risk of future depth-state or
  material changes leaking across unrelated late draw paths.
- The temporal validation scene still covers a water surface and alpha panel
  after the split, while the RT showcase continues to pass material and VRAM
  budget checks.

Next immediate boundary:

- Recompute the remaining large-file list and continue with the largest
  renderer file that has clear internal ownership boundaries. Likely candidates
  are now `Renderer_VisibilityBufferPath.cpp`, `Renderer_RayTracing.cpp`, and
  `Renderer_PipelineSetup.cpp`.

## Pass 10L - Visibility Buffer Path Boundaries

Completed:

- Split the mixed `Renderer_VisibilityBufferPath.cpp` implementation into
  owned visibility-buffer modules:
  - `Renderer_VisibilityBufferCollection.cpp` - stable mesh ordering,
    per-mesh instance bucket collection, bindless mesh SRV repair, material
    keying, VB material table upload, VB instance upload, culling ID lifetime,
    and collection diagnostics.
  - `Renderer_VisibilityBufferCulling.cpp` - VB debug-view mapping, GPU
    culling upload, HZB occlusion handoff, culling freeze/debug environment
    controls, and cull-mask resolution.
  - `Renderer_VisibilityBufferDiagnostics.cpp` - first-frame VB mesh/instance
    logging.
  - `Renderer_VisibilityBufferStages.cpp` - visibility pass execution,
    visibility/depth debug blits, material resolve, and G-buffer debug blits.
  - `Renderer_VisibilityBufferDeferredLighting.cpp` - local-light collection,
    deferred-lighting parameter construction, environment fallback selection,
    and deferred lighting dispatch.
  - `Renderer_VisibilityBufferOrchestration.cpp` - top-level VB path flow:
    collection, debug override, cull-mask resolution, stage dispatch, and
    deferred-lighting handoff.
- Removed `Renderer_VisibilityBufferPath.cpp` from the CMake source list and
  deleted the old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.550/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.137 disocclusion=0.006864 high_motion=0.005228
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- The visibility-buffer path now has separate ownership for collection,
  culling, diagnostics, pass execution, deferred lighting, and orchestration.
  This is a major step toward a mature renderer because the engine can now
  change material packing, culling policy, debug views, and deferred lighting
  without editing one monolithic path file.
- Smoke logs still show stable VB collection and material resolve after the
  split, including RT showcase `Collected=27` and temporal validation
  `Collected=6`.

Next immediate boundary:

- Continue with `Renderer_RayTracing.cpp`, splitting RT frame planning,
  shadow/TLAS dispatch, reflections, and denoise dispatch into owned modules.

## Pass 10M - Ray-Tracing Runtime Boundaries

Completed:

- Split the mixed `Renderer_RayTracing.cpp` implementation into owned
  ray-tracing runtime modules:
  - `Renderer_RTCache.cpp` - BLAS cache reset and mesh asset key invalidation
    for scene switches.
  - `Renderer_RTFramePlan.cpp` - RT scheduler input construction, render
    target size detection, renderer budget refresh, feature readiness checks,
    and acceleration-structure budget publication.
  - `Renderer_RTShadowsGI.cpp` - DXR command-list conversion, depth and
    shadow-mask transitions, TLAS build input construction, RT shadow dispatch,
    and RT GI dispatch.
  - `Renderer_RTReflections.cpp` - RT reflection readiness checks, depth and
    normal-source transitions, debug clear controls, environment descriptor
    refresh, reflection dispatch, and reflection UAV visibility barrier.
  - `Renderer_RTDenoise.cpp` - denoiser fallback tracking, shared input
    transitions, per-signal denoise dispatch, RT history validity updates, and
    RT denoise frame-pass recording.
- Removed `Renderer_RayTracing.cpp` from the CMake source list and deleted the
  old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.550/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.85 center_luma=73.55
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.493 disocclusion=0.00673 high_motion=0.0052
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Ray-tracing runtime ownership is now explicit. Scheduling, TLAS/shadow/GI,
  reflections, denoise, and cache invalidation can evolve independently.
- This split prepares the renderer for more serious RT work: independent
  scheduling heuristics, reflection quality fixes, denoiser improvements, and
  better memory-budget enforcement no longer require editing one RT catch-all.

Next immediate boundary:

- Recompute the large-file list and decide between splitting initialization
  pipeline setup or visibility-buffer pipeline creation. These are higher risk
  because they construct root signatures, PSOs, and shader-dependent state, so
  each pass needs a full build and smoke validation.

## Pass 10N - Visibility Buffer Pipeline Boundaries

Completed:

- Split the mixed visibility-buffer pipeline setup file into explicit pipeline
  ownership modules:
  - `VisibilityBuffer_RootSignatures.cpp` - visibility, material resolve,
    clustered-light culling, BRDF LUT, debug blit, and deferred-lighting root
    signature creation.
  - `VisibilityBuffer_PipelineStates.cpp` - visibility pass, material resolve,
    motion vectors, clustered-light culling, BRDF LUT, debug blit, and
    deferred-lighting PSO creation.
- Removed `VisibilityBuffer_Pipelines.cpp` from the CMake source list and
  deleted the old mixed file.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.456/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.46
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.54 disocclusion=0.006722 high_motion=0.005184
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Visibility-buffer initialization now separates root signature ABI ownership
  from PSO construction. This gives shader-interface changes and shader-state
  changes different edit surfaces, which is a cleaner contract for future VB
  material and debug-view work.
- The remaining major renderer debt is now concentrated in core DXR RHI
  implementation, renderer-wide pipeline setup, render graph internals, and a
  few mid-sized frame/pass modules.

Next immediate boundary:

- Inspect `Renderer_PipelineSetup.cpp`, `RenderGraph.cpp`, and the remaining
  render-graph pass files. Continue with the largest boundary that can be
  moved without changing public behavior, then run the same build and smoke
  pair before updating this document again.

## Pass 10O - Renderer Pipeline Setup Boundaries

Completed:

- Replaced the monolithic `Renderer_PipelineSetup.cpp` with a thin
  orchestrator for command-list creation, shader/pipeline initialization, and
  the legacy `CreatePipeline()` compatibility entry point.
- Added explicit renderer pipeline setup modules:
  - `Renderer_ShaderCompilation.cpp` - required and optional shader
    compilation for the renderer-wide PSO set.
  - `Renderer_RootSignatureSetup.cpp` - graphics root signature creation,
    compute root signature creation, compact 1-SRV/1-UAV compute ABI setup,
    temporal rejection mask initialization, and RT denoiser initialization.
  - `Renderer_GeometryPipelineSetup.cpp` - mesh, transparent, overlay, depth,
    water, particle, skybox, procedural sky, and shadow PSO creation.
  - `Renderer_ScreenComputePipelineSetup.cpp` - post-process, voxel
    raymarch, TAA, SSAO, SSAO compute, HZB compute, SSR, motion-vector, bloom,
    and debug-line PSO creation.
  - `Renderer_PipelineSetupTypes.h` - shared compiled-shader bundle used by
    the orchestrator and setup modules.
- Updated `Renderer.h` and CMake so these setup phases are first-class
  renderer contracts instead of one long initialization function.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=2.200/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.363 disocclusion=0.006736 high_motion=0.005203
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Renderer startup now has readable phase boundaries: compile shader bytecode,
  create root-signature ABIs, create geometry PSOs, and create screen/compute
  PSOs. This makes future renderer features safer because shader failures,
  ABI changes, and PSO policy changes no longer share one large edit surface.
- Remaining high-value boundaries are `RenderGraph.cpp`, the render-graph pass
  wrappers, core DXR RHI implementation, and visibility-buffer PSO internals.

Next immediate boundary:

- Split `RenderGraph.cpp` by ownership: frame lifecycle/resource import,
  transient allocation, barrier compilation, compile/validation, and execute
  should become separate modules while preserving the existing `RenderGraph`
  public API.

## Pass 10P - Render Graph Internal Boundaries

Completed:

- Split the render graph implementation into focused ownership modules while
  preserving the existing `RenderGraph` and `RGPassBuilder` public API:
  - `RenderGraph_Builder.cpp` - pass builder calls and access registration.
  - `RenderGraph_Lifecycle.cpp` - initialization, shutdown, frame reset,
    external resource import, transient handle creation, end-frame lifetime
    semantics, and direct resource lookup.
  - `RenderGraph_TransientResources.cpp` - transient lifetime analysis,
    placed-resource heap packing, alias reuse, heap growth, and automatic
    aliasing-barrier injection.
  - `RenderGraph_Barriers.cpp` - usage-to-state mapping, transition barrier
    generation, UAV hazard ordering, aliasing barrier emission, and final
    state publication.
  - `RenderGraph_CompileExecute.cpp` - pass culling, compile-time access
    validation, transient allocation handoff, barrier compilation, optional
    graph dumps, and pass execution.
  - `RenderGraph_StateQueries.cpp` - whole-resource and per-subresource state
    queries.
- Kept `RenderGraph.cpp` as a small index/source anchor documenting the split.
- Updated CMake to build the new render-graph modules.

Verified:

- Incremental Ninja build after CMake regeneration.
- RT showcase smoke:
  `frames=33 gpu_ms=2.287/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.51
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.389 disocclusion=0.006652 high_motion=0.00517
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Render graph ownership is now explicit enough for future work on async
  queues, aliasing policy, barrier validation, and graph dumps without editing
  one dense implementation file.
- The smoke logs still show the expected graph behavior after the split:
  transient heap growth to 16 MB and HZB graph execution with 23 passes and
  46 barriers.

Next immediate boundary:

- Recompute the remaining large-file list. The main candidates are now core
  DXR RHI implementation, visibility-buffer PSO internals, GPU particles,
  frame-constant assembly, and render-graph pass wrappers.

## Pass 10Q - Visibility Buffer PSO Internals

Completed:

- Split visibility-buffer PSO creation by shader family while preserving the
  existing `VisibilityBufferRenderer::CreatePipelines()` entry point:
  - `VisibilityBuffer_VisibilityPipelines.cpp` - visibility and alpha-tested
    visibility pass PSOs.
  - `VisibilityBuffer_MaterialResolvePipeline.cpp` - material resolve compute
    PSO.
  - `VisibilityBuffer_MotionVectorPipeline.cpp` - visibility-buffer motion
    vector compute PSO.
  - `VisibilityBuffer_ClusteredLightPipeline.cpp` - clustered light culling
    compute PSO.
  - `VisibilityBuffer_BRDFLUTPipeline.cpp` - BRDF LUT compute PSO.
  - `VisibilityBuffer_DebugBlitPipelines.cpp` - debug blit and debug compute
    PSOs.
  - `VisibilityBuffer_DeferredLightingPipeline.cpp` - deferred lighting
    compute PSO.
- Kept root-signature construction in `VisibilityBuffer_RootSignatures.cpp`
  and reduced `VisibilityBuffer_PipelineStates.cpp` to a policy orchestrator.
- Updated `VisibilityBuffer.h` and CMake so each PSO family is a first-class
  internal boundary.

Verified:

- Incremental Ninja build after CMake regeneration.
- RT showcase smoke:
  `frames=33 gpu_ms=10.710/16.7 dxgi_mb=404.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.85 center_luma=73.58
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.137 disocclusion=0.006906 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Visibility-buffer setup now has separate edit surfaces for ABI ownership and
  each PSO family. Future material resolve, motion-vector, clustered lighting,
  debug, and deferred-lighting changes no longer require editing one long PSO
  factory.
- The RT showcase run stayed inside budget but was slower than the previous
  validation runs. Treat this as a metric to watch across future passes rather
  than a correctness regression because the temporal smoke remained stable.

Next immediate boundary:

- Split `Renderer_RenderGraphTemporalScreen.cpp` into pass-specific render
  graph wrappers for motion vectors, temporal rejection mask, TAA, SSR, and
  SSAO. This keeps the render graph migration readable before touching larger
  and riskier DXR/RHI files.

## Pass 10R - Temporal Screen Render Graph Boundaries

Completed:

- Split `Renderer_RenderGraphTemporalScreen.cpp` into pass-specific render
  graph entry points:
  - `Renderer_RenderGraphMotionVectors.cpp` - motion-vector graph import,
    visibility-buffer motion path, camera-only fallback, and state publication.
  - `Renderer_RenderGraphTemporalMask.cpp` - temporal rejection mask graph
    dispatch and normal-source state tracking.
  - `Renderer_RenderGraphTAA.cpp` - TAA history seeding, resolve,
    intermediate copy, and history copy graph wrapper.
  - `Renderer_RenderGraphSSR.cpp` - SSR graph import, normal-source handling,
    scoped transition suppression, and fallback path.
  - `Renderer_RenderGraphSSAO.cpp` - raster/compute SSAO graph wrapper and
    scoped transition suppression.
- Kept `Renderer_RenderGraphTemporalScreen.cpp` as a small index/source anchor
  documenting the pass split.
- Updated CMake to compile the new pass-specific translation units.

Verified:

- CMake-regenerated Ninja build with the explicit MSVC/Windows SDK environment.
- RT showcase smoke:
  `frames=33 gpu_ms=1.930/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.45
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.119 disocclusion=0.00676 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Temporal screen-space graph work is no longer grouped by implementation
  accident. Each pass now has its own edit surface, which is the right shape
  for future correctness work on history validity, rejection masks, SSR
  composition, and async SSAO.
- The renderer is still carrying large files in DXR RHI, RHI texture/pipeline
  creation, GPU particles, frame constants, visibility-buffer collection, and
  visibility-buffer graph orchestration.

Next immediate boundary:

- Continue reducing renderer ownership hotspots. Prefer boundaries that
  preserve behavior and improve future correctness work: visibility-buffer
  graph helpers/staged execution, frame-constant assembly, and visibility-
  buffer instance collection before attempting the larger DXR RHI split.

## Pass 10S - Visibility Buffer Graph And Material-Key Helpers

Completed:

- Added `Renderer_RenderGraphVisibilityBufferHelpers.h` and moved
  visibility-buffer graph resource imports, debug-view constants, debug-source
  selection, and optional resource import helpers out of the orchestration
  source.
- Reduced `Renderer_RenderGraphVisibilityBuffer.cpp` to the actual graph
  execution policy: staged VB clear, visibility, material resolve, debug blit,
  BRDF LUT, clustered lights, deferred lighting, fallback handling, and state
  publication.
- Added `Renderer_VisibilityBufferMaterialKey.h` and moved visibility-buffer
  material deduplication key construction into a named contract:
  `VisibilityBufferMaterialKey`, `VisibilityBufferMaterialKeyHasher`, and
  `MakeVisibilityBufferMaterialKey`.
- Reduced `Renderer_VisibilityBufferCollection.cpp` so material-table
  deduplication is explicit and independent from instance bucketing.

Verified:

- Incremental Ninja builds after each helper extraction.
- RT showcase smoke:
  `frames=33 gpu_ms=1.939/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.963 disocclusion=0.00676 high_motion=0.005209
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Visibility-buffer graph orchestration and visibility-buffer collection are
  more readable, but each still has a large central function. The next mature
  step is not just moving code: it is defining renderer-private phase helpers
  for frame constants and staged visibility-buffer collection so the contracts
  can be tested and reasoned about independently.
- The remaining largest files are now dominated by DXR RHI, `Renderer.h`,
  RHI texture/pipeline creation, GPU particles, frame constants, decals, and
  the visibility-buffer collection/orchestration centers.

Next immediate boundary:

- Split frame-constant assembly into renderer-private phases: camera/jitter,
  light and shadow constants, post/feature flags, and temporal history
  publication. This is higher-value than a purely mechanical file split
  because frame constants drive visible correctness across raster, RT, TAA,
  SSR, SSAO, shadows, fog, and water.

## Pass 10T - Frame Constant Phase Boundaries

Completed:

- Added `Renderer_FrameConstantsTypes.h` with `FrameConstantCameraState`, a
  compact handoff record for the camera/jitter state shared by later constant
  phases.
- Reduced `Renderer_FrameConstants.cpp` to the camera/jitter setup and a clear
  orchestration sequence:
  - compute active/default camera matrices,
  - update camera movement and TAA jitter,
  - compute jittered and non-jittered view-projection state,
  - call renderer-private phase helpers.
- Added `Renderer_FrameLightingConstants.cpp` for exposure, ambient light,
  forward light table, directional cascades, local spotlight shadow slices,
  and shadow parameters.
- Added `Renderer_FramePostConstants.cpp` for debug overlay constants,
  post-process flags, IBL, color grading, fog, SSAO, bloom/SSR/RT feature
  flags, water parameters, clustered-light defaults, and projection params.
- Added `Renderer_FrameTemporalConstants.cpp` for previous view-projection
  publication, inverse view-projection, camera-cut RT history invalidation,
  previous-camera state update, and triple-buffered constant-buffer upload.
- Updated `Renderer.h` and CMake so these phases are explicit renderer
  contracts.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- Build initially exposed a hidden include dependency: the lighting phase
  needed `<glm/gtx/norm.hpp>` for `glm::length2`. Added the missing include
  and rebuilt successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.932/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.42
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.975 disocclusion=0.006847 high_motion=0.005223
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- Frame constants now have real phase ownership. This is a meaningful
  architectural step because the renderer can now change lighting, post
  feature flags, and temporal history publication independently without
  threading every edit through the camera/jitter setup function.
- The largest renderer hotspots after this pass are:
  - `DX12Raytracing.cpp` at 1794 lines.
  - `Renderer.h` at 1396 lines.
  - `DX12Texture.cpp` at 777 lines.
  - `DX12Pipeline.cpp` at 733 lines.
  - `GPUParticles.cpp` at 664 lines.
  - `DecalManager.cpp` at 576 lines.
  - `Renderer_VisibilityBufferCollection.cpp` at 494 lines.
  - `Renderer_RenderGraphVisibilityBuffer.cpp` at 462 lines.

Next immediate boundary:

- Continue with a real ownership split rather than another helper-only move.
  The best next renderer-core targets are either:
  - visibility-buffer collection phases: stable mesh draw table, culling ID
    lifecycle, material table, instance bucketing, uploads, and diagnostics;
  - or DXR RHI phases: context initialization, pipeline creation, BLAS/TLAS
    ownership, shader-table construction, dispatch, and resource lifetime.

## Pass 10U - DXR RHI Ownership Boundaries

Completed:

- Reduced `DX12Raytracing.cpp` to a small anchor translation unit and split the
  DXR context into focused implementation modules:
  - `DX12Raytracing_Lifecycle.cpp` - device query, descriptor-table allocation,
    resize/camera lifecycle, and shutdown.
  - `DX12Raytracing_RootSignature.cpp` - shared DXR root-signature ABI.
  - `DX12Raytracing_ShadowPipeline.cpp` - shadow DXIL load, state object, shader
    identifiers, and shader table.
  - `DX12Raytracing_ReflectionPipeline.cpp` - optional reflection DXIL load,
    state object, shader identifiers, and shader table.
  - `DX12Raytracing_GIPipeline.cpp` - optional diffuse GI DXIL load, state
    object, shader identifiers, and shader table.
  - `DX12Raytracing_BLAS.cpp` - per-mesh BLAS cache and scratch lifetime.
  - `DX12Raytracing_TLAS.cpp` - registry/snapshot TLAS build, instance buffer,
    material buffer, and top-level acceleration structure build.
  - `DX12Raytracing_Materials.cpp` - CPU-to-GPU RT material packing.
  - `DX12Raytracing_Dispatch.cpp` - shadow, reflection, and GI dispatch binding.
- Added explicit private DXR initialization contracts in `DX12Raytracing.h`:
  `CreateGlobalRootSignature`, `InitializeShadowPipeline`,
  `InitializeReflectionPipeline`, and `InitializeGIPipeline`.
- Preserved the existing optional-feature behavior: shadow pipeline failure
  leaves BLAS/TLAS-only DXR support, reflection failure leaves shadows enabled,
  and GI failure leaves shadows/reflections enabled.
- Updated CMake source registration for the new DXR modules.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=2.092/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.51
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.613 disocclusion=0.006759 high_motion=0.005209
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- The largest original DXR hotspot is gone. `DX12Raytracing.cpp` went from
  monolithic implementation to a 5-line ownership anchor, and the largest DXR
  implementation file is now `DX12Raytracing_TLAS.cpp` at 472 lines.
- DXR is still not "finished" architecturally: TLAS build still owns instance
  extraction, broad RT culling, BLAS build scheduling, upload-buffer growth,
  and top-level AS construction in one method. That is now isolated enough to
  refactor without touching shader-table or dispatch code.
- The largest renderer-adjacent hotspots after this pass are:
  - `Renderer.h` at 1396 lines.
  - `DX12Texture.cpp` at 777 lines.
  - `DX12Pipeline.cpp` at 733 lines.
  - `GPUParticles.cpp` at 664 lines.
  - `DecalManager.cpp` at 576 lines.
  - `Renderer_VisibilityBufferCollection.cpp` at 494 lines.
  - `DX12Raytracing_TLAS.cpp` at 472 lines.

Next immediate boundary:

- Continue with architectural debt that still blocks renderer maturity:
  - split `Renderer.h` into private interface headers by feature area so new
    renderer passes stop depending on a single 1396-line declaration surface;
  - or split `DX12Texture.cpp` into creation, file loading, upload, view
    creation, and lifetime modules before deeper asset-streaming work.
- Recommended next pass: `Renderer.h` interface containment. It is the highest
  leverage boundary because every rendering feature touches it, and shrinking
  that declaration surface will make subsequent texture, RT, and visual-demo
  work less brittle.

## Pass 10V - Renderer Header Support Boundary

Completed:

- Moved the generic triple-buffered `ConstantBuffer<T>` helper and
  `kFrameCount` definition out of `Renderer.h` into
  `Renderer_ConstantBuffer.h`.
- Updated `Renderer.h` to include the support header instead of carrying the
  full D3D12 upload-buffer template inline.
- Registered `Renderer_ConstantBuffer.h` in CMake's header list.

Verified:

- CMake-regenerated Ninja build with explicit MSVC/Windows SDK environment.
- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.617/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.26 disocclusion=0.006865 high_motion=0.005215
  object_motion=0.0731 visible=7 warnings=0`.
- `ninja -C build -n CortexEngine` reports no work to do.

Current renderer-refactor state:

- `Renderer.h` dropped from 1396 to 1289 lines without behavior changes. This
  is only the first header-containment step, but it removes a reusable GPU
  upload utility from the renderer class declaration and gives future constant
  buffer users a cleaner include target.
- The largest graphics hotspots after this pass are:
  - `Renderer.h` at 1289 lines.
  - `DX12Texture.cpp` at 777 lines.
  - `DX12Pipeline.cpp` at 733 lines.
  - `GPUParticles.cpp` at 664 lines.
  - `DecalManager.cpp` at 576 lines.
  - `VisibilityBuffer.h` at 525 lines.
  - `GPUCulling_Dispatch.cpp` at 498 lines.
  - `Renderer_VisibilityBufferCollection.cpp` at 494 lines.
  - `VisibilityBuffer_Resources.cpp` at 488 lines.
  - `Renderer_Bloom.cpp` at 485 lines.

Next immediate boundary:

- Continue header containment only where it is structurally clean. The next
  safe target is to move public diagnostics/telemetry structs out of
  `Renderer.h` into a renderer diagnostics types header, then reassess before
  attempting private method declaration grouping.
- The next implementation pass after that should attack `DX12Texture.cpp` by
  splitting texture creation/upload/view construction from file loading. That
  will directly support the long-term goals of asset streaming, residency
  control, and lower VRAM pressure.

## Pass 10W - Renderer Diagnostics Type Boundary

Completed:

- Added `Renderer_DiagnosticsTypes.h` for public renderer telemetry records:
  `RendererVRAMBreakdown` and `RendererDescriptorStats`.
- Updated `Renderer.h` so the renderer class aliases those diagnostics types
  instead of defining the structs inline.
- Removed the public `Renderer.h` dependency on `Debug/GPUProfiler.h` by
  forward declaring `Debug::GPUFrameProfile`.
- Moved the `GetLastGPUProfile()` implementation into
  `Renderer_FrameContractMemory.cpp`, where the profiler dependency is local
  to the frame-contract diagnostics implementation.
- Added the required profiler include to `Core/Engine.cpp`, which already
  dereferences the returned GPU profile.
- Registered the diagnostics types header in CMake.

Verified:

- Incremental Ninja build completed successfully after the include-boundary
  fix.
- RT showcase smoke:
  `frames=33 gpu_ms=1.454/16.7 dxgi_mb=404.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.53
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.142 disocclusion=0.006906 high_motion=0.005235
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- Diagnostics data is now a named public contract instead of another nested
  renderer declaration. This is a small boundary, but it removes one more
  reason for unrelated engine/UI code to include profiler internals through
  `Renderer.h`.
- `Renderer.h` is still too large and remains the central declaration surface
  for many unrelated renderer subsystems. Further header work should be driven
  by subsystem boundaries rather than arbitrary line-count reduction.

Next immediate boundary:

- Split `DX12Texture.cpp` by RHI responsibility. Texture upload and view
  construction are central to future streaming/residency work, and the current
  monolithic file hides upload lifetime, SRV creation, compressed mip upload,
  and cubemap upload in one implementation unit.

## Pass 10X - Texture RHI Ownership Boundaries

Completed:

- Split `DX12Texture.cpp` into focused RHI implementation units:
  - `DX12Texture.cpp` - basic texture creation, raw-data convenience creation,
    and placeholder texture generation.
  - `DX12Texture_MipUpload.cpp` - uncompressed and compressed mip-chain upload.
  - `DX12Texture_Upload.cpp` - hot-swap texture upload path.
  - `DX12Texture_Views.cpp` - legacy and bindless SRV construction.
  - `DX12Texture_Cube.cpp` - cubemap resource creation and face upload.
- Updated CMake so the new texture modules are built explicitly.
- Preserved existing behavior for standard texture creation, compressed DDS
  upload, environment-map upload, bindless registration, and cubemap upload.
- Tightened the upload contract while the file was split:
  - texture dimensions, mip count, and array size are validated before D3D12
    resource creation;
  - D3D12 16-bit mip/array fields are cast only after validation;
  - direct texture upload rejects null inputs and undersized data;
  - partial `UpdateData` calls now fail explicitly instead of pretending to
    support region updates while reading as a full texture upload.

Verified:

- CMake-regenerated Ninja graph with the new source files.
- Full `CortexEngine` build completed successfully.
- Follow-up incremental build after upload-contract tightening completed
  successfully with no new warnings from the touched texture files.
- RT showcase smoke:
  `frames=33 gpu_ms=1.997/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.48
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.25 disocclusion=0.006734 high_motion=0.005203
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- The original 916-line texture implementation is now a set of smaller
  ownership units. The largest texture file is `DX12Texture_MipUpload.cpp` at
  354 lines, with direct upload at 207 lines and SRV creation at 70 lines.
- This is a useful foundation for the next asset-memory work: upload lifetime,
  compressed mip handling, bindless view creation, and cubemap handling can now
  evolve independently.
- The largest remaining graphics hotspots are now:
  - `Renderer.h` at 1387 lines.
  - `GPUParticles.cpp` at 664 lines.
  - `Renderer_Bloom.cpp` at 579 lines.
  - `DecalManager.cpp` at 576 lines.
  - `Renderer_VisibilityBufferCollection.cpp` at 560 lines.
  - `DX12Raytracing_TLAS.cpp` at 549 lines.
  - `Renderer_FrameEnd.cpp` at 537 lines.
  - `Renderer_RTResources.cpp` at 535 lines.
  - `Renderer_Environment.cpp` at 528 lines.
  - `Renderer_Vegetation.cpp` at 527 lines.

Next immediate boundary:

- Move from mechanical containment into higher-value renderer maturity again.
  The best next pass is texture/asset residency planning in code:
  - define a texture upload request/receipt record that reports source format,
    mip count, estimated GPU bytes, bindless index, and residency class;
  - route texture creation diagnostics through that record so smokes can catch
    oversized or non-resident texture behavior;
  - prepare for a real streaming path by separating "create resource",
    "stage upload", "execute upload", and "publish shader view" as explicit
    phases rather than one synchronous helper.
- After that, return to `Renderer.h` with a feature-boundary split for public
  settings/telemetry versus private frame orchestration declarations.

## Pass 10Y - Texture Residency Receipts

Completed:

- Added `TextureUploadReceipt.h` with structured texture upload diagnostics:
  source encoding, residency class, source and resident dimensions, source and
  resident mip counts, first resident mip, estimated full/resident GPU bytes,
  active budget profile, budget size, bindless index, and trimming flags.
- Added renderer access to recent texture receipts through
  `Renderer::GetTextureUploadReceipts()`.
- Stored bounded recent receipts in the renderer so texture admissions can be
  inspected after scene load without scraping ad hoc log lines.
- Wired receipts into disk texture loading:
  - explicit DDS compressed path;
  - compressed sibling path;
  - RGBA fallback path;
  - DDS placeholder fallback path;
  - material and environment residency classes.
- Wired receipts into generated RGBA texture creation used by the Dreamer
  upload path.
- Added concise end-of-run diagnostics for the most recent receipts so smoke
  logs expose what texture data actually became resident on the GPU.

Verified:

- Full `CortexEngine` build completed successfully after adding the receipt
  type, renderer state, and CMake header registration.
- Incremental rebuild after diagnostics logging completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.877/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.52
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded `Texture receipts: count=11`; the latest
  material receipts show compressed DDS residency with 2048x2048 resident
  textures, 12 resident mips, ~5.33 MB each, and bindless indices 16-18.
- Temporal validation smoke:
  `gpu_ms=1.881 disocclusion=0.006749 high_motion=0.005206
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded `Texture receipts: count=1` for
  the resident environment texture: compressed DDS, 2048x1024, 12 mips,
  ~2.67 MB, bindless index 8.

Current renderer-refactor state:

- Texture loading now has a structured admission artifact. This does not yet
  make uploads asynchronous, but it is the correct intermediate contract for
  future streaming because it records what was requested, what was admitted,
  what budget profile made the decision, and what shader-visible handle was
  published.
- `Renderer_TextureLoad.cpp` grew to 587 lines because it now owns both mip
  admission and receipt publication. That is acceptable for this pass, but it
  marks the next split: admission policy, upload execution, and receipt
  publication should become separate helpers before adding async streaming.
- The current large hotspots to prioritize are:
  - `Renderer.h` at 1392 lines.
  - `GPUParticles.cpp` at 664 lines.
  - `Renderer_TextureLoad.cpp` at 587 lines.
  - `Renderer_Bloom.cpp` at 579 lines.
  - `DecalManager.cpp` at 576 lines.
  - `Renderer_VisibilityBufferCollection.cpp` at 560 lines.
  - `DX12Raytracing_TLAS.cpp` at 549 lines.

Next immediate boundary:

- Split texture loading into explicit helper phases without changing behavior:
  - texture source selection: explicit DDS, compressed sibling, RGBA fallback,
    placeholder fallback;
  - mip admission policy: budget profile, max dimension, floor dimension,
    resident-byte decision;
  - upload execution: compressed mip chain, RGBA mip chain, placeholder;
  - publication: staging SRV, bindless SRV, asset registry registration,
    receipt storage.
- After that, introduce an upload-ticket abstraction so the synchronous path
  and future async streaming path share the same resource/publish contract.

## Pass 10Z - Texture Admission Policy Module

Completed:

- Added `TextureAdmission.h` and `TextureAdmission.cpp` as the reusable policy
  module for texture byte estimation and mip admission.
- Moved `EstimateTextureBytes()` out of `Renderer_TextureLoad.cpp`.
- Moved `TextureMipAdmission` and `ChooseTextureMipAdmission()` out of
  `Renderer_TextureLoad.cpp`.
- Updated `Renderer_TextureLoad.cpp` to depend on the admission module instead
  of owning policy, source selection, upload execution, descriptor publishing,
  registry updates, and receipt publication in one file.
- Registered the new source/header in CMake.

Verified:

- CMake-regenerated Ninja graph and full `CortexEngine` build completed
  successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.470/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.51
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics still recorded `Texture receipts: count=11` with
  the same compressed DDS resident decisions as the previous pass.
- Temporal validation smoke:
  `gpu_ms=1.134 disocclusion=0.006819 high_motion=0.005219
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics still recorded the environment receipt as
  compressed DDS, 2048x1024, 12 resident mips, ~2.67 MB.

Current renderer-refactor state:

- Texture residency policy is now reusable by a future async streamer. This is
  the first real split toward an upload-ticket system because policy can be
  evaluated before resource creation.
- `Renderer_TextureLoad.cpp` dropped from 587 lines to 447 lines. It is still
  doing too much, but it no longer owns mip-budget policy.
- The next texture split should extract publication/receipt storage, then
  source selection, leaving the renderer method as a small orchestration path.

Next immediate boundary:

- Add a texture publication helper that owns:
  - staging SRV allocation;
  - legacy SRV creation;
  - bindless SRV registration;
  - asset registry registration;
  - receipt finalization and bounded storage.
- Once publication is separate, define a `TextureUploadTicket` that can carry
  the selected source, admitted mips, planned resource description, and final
  publication receipt through either the current synchronous path or a future
  queued upload path.

## Pass 11A - Texture Publication Boundary

Completed:

- Added `Renderer_TexturePublication.cpp` as the renderer-private publication
  boundary for texture resources.
- Centralized staging SRV allocation, legacy SRV creation, bindless SRV
  registration, optional asset-registry registration, receipt finalization,
  and bounded receipt storage.
- Replaced duplicated publication code in disk texture loading and generated
  RGBA texture creation with `Renderer::PublishTexture()`.
- Kept generated Dreamer textures out of the asset registry while still
  publishing their shader-visible SRV and upload receipt.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.551/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.52
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.552 disocclusion=0.006757 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0`.

## Pass 11B - Texture Upload Ticket Contract

Completed:

- Added `TextureUploadTicket.h`.
- Changed `Renderer::PublishTexture()` to consume a move-only ticket containing
  the completed `DX12Texture`, its `TextureUploadReceipt`, a publication
  context string, and an asset-registry flag.
- Updated `LoadTextureFromFile()` and `CreateTextureFromRGBA()` to publish
  tickets instead of passing loosely related state across the publication
  boundary.
- Preserved the existing synchronous upload behavior while giving the future
  streaming path a single publication artifact to target.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.551/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.52
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.552 disocclusion=0.006757 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0`.
- Ninja dry-run reported `no work to do`.

## Pass 11C - Texture Source Planning

Completed:

- Added `TextureSourcePlan.h` and `TextureSourcePlan.cpp`.
- Moved source selection out of `Renderer_TextureLoad.cpp`:
  - explicit compressed DDS;
  - compressed sibling DDS;
  - RGBA fallback with generated mip chain;
  - placeholder fallback for DDS load failure.
- Moved mip admission for the selected source into the plan stage so resource
  creation receives already-admitted mips and exact byte accounting.
- Kept source metadata together with the admitted data: requested path, source
  path, source dimensions, resident dimensions, mip count, format, compressed
  sibling flag, and copy-queue preference.
- Reduced `Renderer_TextureLoad.cpp` from 378 lines to a focused orchestration
  path before the upload-execution split.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.966/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.51
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.963 disocclusion=0.006804 high_motion=0.005216
  object_motion=0.0731 visible=7 warnings=0`.
- Texture receipts remained stable: RT showcase reported 11 receipts, and
  temporal validation reported the resident environment receipt as compressed
  DDS, 2048x1024, 12 mips, ~2.67 MB, bindless index 8.

## Pass 11D - Texture Upload Execution Boundary

Completed:

- Added `Renderer_TextureUploadExecution.cpp`.
- Added `Renderer::ExecuteTextureUpload(const TextureSourcePlan&)` as the
  explicit synchronous upload execution stage.
- Moved DX12 resource creation for compressed mip chains, RGBA mip chains, and
  placeholder fallback out of `LoadTextureFromFile()`.
- Left `LoadTextureFromFile()` as a 99-line orchestration path:
  cache lookup, source planning, upload execution, receipt population,
  ticket publication, and cache insertion.
- Registered all new texture-stage files in CMake.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=2.096/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.49
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.095 disocclusion=0.006803 high_motion=0.005215
  object_motion=0.0731 visible=7 warnings=0`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- The disk texture path now has four explicit stages:
  - `TextureSourcePlan`: source selection, compressed/RGBA fallback, mip
    admission, and byte accounting.
  - `ExecuteTextureUpload`: synchronous DX12 resource creation for the chosen
    source plan.
  - `TextureUploadTicket`: move-only artifact carrying the completed texture
    and receipt to publication.
  - `PublishTexture`: descriptor publication, bindless registration, optional
    asset-registry insertion, and bounded receipt storage.
- This does not yet make texture streaming asynchronous. It does remove the
  architectural blocker: the current path can now be replaced with a queued
  uploader without changing cache keys, source policy, receipt diagnostics, or
  shader-view publication.
- Current texture-stage sizes:
  - `Renderer_TextureLoad.cpp`: 99 lines.
  - `Renderer_TexturePublication.cpp`: 70 lines.
  - `Renderer_TextureUploadExecution.cpp`: 57 lines.
  - `TextureSourcePlan.cpp`: 238 lines.
  - `TextureSourcePlan.h`: 31 lines.
  - `TextureUploadTicket.h`: 15 lines.

Next immediate boundary:

- Introduce the queued upload model in small, testable layers:
  - define `TextureUploadJob` and `TextureUploadCompletion` around
    `TextureSourcePlan` and `TextureUploadTicket`;
  - keep the existing synchronous path as the initial executor;
  - add queue telemetry: pending jobs, completed jobs, failed jobs, uploaded
    resident bytes, and average upload time;
  - expose the telemetry through renderer diagnostics and smoke logs;
  - then move suitable non-critical texture loads onto the queue once the
    telemetry proves the synchronous executor is behaviorally identical.
- After queued upload telemetry exists, resume the `Renderer.h` split by
  extracting private texture API declarations into a texture-stage header.

## Pass 11E - Observable Texture Upload Queue Contract

Completed:

- Added `TextureUploadQueue.h`.
- Defined the first queue-facing data types:
  - `TextureUploadQueueStats`;
  - `TextureUploadJob`;
  - `TextureUploadCompletion`.
- Added renderer-owned texture upload queue telemetry:
  - submitted jobs;
  - completed jobs;
  - failed jobs;
  - pending jobs;
  - uploaded resident bytes;
  - total, average, and last upload time.
- Instrumented `ExecuteTextureUpload()` so the current synchronous upload path
  is now measured as a queue job without changing texture loading behavior.
- Added `Renderer::GetTextureUploadQueueStats()` for UI, diagnostics, and
  future smoke assertions.
- Extended renderer shutdown diagnostics with texture upload queue state.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.921/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.51
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=3.471 last_ms=2.635`.
- Temporal validation smoke:
  `gpu_ms=2.098 disocclusion=0.006821 high_motion=0.005216
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.022 last_ms=2.022`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- The texture upload path is now observable enough to support real streaming
  work. The engine can report how many texture uploads were requested,
  completed, failed, or left pending, plus how many resident bytes were
  uploaded and how much CPU-side time the synchronous executor spent.
- The queue is not yet asynchronous. That is deliberate: the new telemetry
  establishes the baseline contract before work is moved across threads or
  frames.
- The next asynchronous step can be implemented with direct comparison against
  this synchronous baseline.

Next immediate boundary:

- Convert the observable queue contract into an actual deferred queue:
  - maintain a pending `TextureUploadJob` deque;
  - keep critical loads synchronous until call sites are classified;
  - allow non-critical material texture requests to enqueue jobs and use
    placeholders until completion;
  - drain a bounded number of queued uploads per frame;
  - publish completed tickets through the existing `PublishTexture()` boundary;
  - extend smoke scripts to assert `failed=0` and bounded `pending` at
    shutdown.
- In parallel with that later pass, extract private texture-stage declarations
  out of `Renderer.h` to continue shrinking the renderer monolith.

## Pass 11F - Deferred Material Texture Queue

Completed:

- Converted the observable texture upload contract into a real renderer-owned
  deferred queue for material texture requests.
- Added a pending `TextureUploadJob` deque and pending/failed cache-key sets so
  repeated material requests deduplicate instead of reloading the same texture
  every frame.
- Kept environment, generated RGBA, and vegetation texture paths synchronous
  because those paths are currently startup-critical or owned by separate
  systems.
- Updated material texture resolution so renderables use cached textures when
  available, placeholders while uploads are pending, and stable fallbacks when
  an upload has failed.
- Drained a bounded number of material texture uploads from pre-frame services
  through the existing source-plan, upload-execution, and publication
  boundaries.
- Extended the RT showcase and temporal validation smoke scripts to fail when
  texture uploads report failed jobs or leave more than the configured pending
  budget at shutdown.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=2.009/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.43
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.415 last_ms=2.735`.
- Temporal validation smoke:
  `gpu_ms=1.142 disocclusion=0.006857 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.230 last_ms=2.230`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- The material texture path no longer performs unbounded synchronous work from
  renderable prewarm. It now has admission, deduplication, bounded per-frame
  execution, publication, cache insertion, and failure suppression.
- The queue is still CPU/D3D12-thread executed rather than a background worker.
  That is the safer intermediate architecture because upload publication still
  touches renderer-owned descriptor and bindless state.
- The remaining diagnostics gap is that queue validation currently depends on
  shutdown log text instead of the structured frame report.

Next immediate boundary:

- Promote `TextureUploadQueueStats` into `frame_report_last.json` and
  `frame_report_shutdown.json`.
- Move smoke assertions from shutdown-log regex parsing to structured JSON
  fields.
- Treat queue failures as frame health warnings so regressions are visible in
  the same report as GPU time, memory, descriptors, and frame-contract health.

## Pass 11G - Structured Texture Queue Diagnostics

Completed:

- Added a top-level `texture_upload_queue` block to the frame diagnostics
  report: submitted, completed, failed, pending, uploaded MB, total upload ms,
  average upload ms, and last upload ms.
- Made texture upload failures participate in frame health warnings.
- Updated smoke scripts to assert the JSON report contract instead of parsing
  human-readable shutdown logs.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.980/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=2.056 disocclusion=0.006762 high_motion=0.005209
  object_motion=0.0731 visible=7 warnings=0`.

## Pass 11H - Texture Queue State Ownership

Completed:

- Added `TextureUploadQueueState` as the dedicated owner for texture upload
  stats, pending jobs, pending cache keys, failed cache keys, next job ID, and
  per-frame drain budget.
- Replaced loose renderer fields with one `m_textureUploadQueue` member.
- Removed the unused `TextureUploadCompletion` type.
- Decoupled `TextureUploadQueue.h` from `TextureSourcePlan.h` and
  `TextureUploadTicket.h`; the queue contract now only depends on job keys,
  queue policy, and telemetry.
- Removed texture source-plan and upload-ticket includes from `Renderer.h` and
  moved those dependencies into the texture implementation files that actually
  need complete definitions.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.553/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.47
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.431 last_ms=2.536`.
- Temporal validation smoke:
  `gpu_ms=1.137 disocclusion=0.006881 high_motion=0.005231
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=3.352 last_ms=3.352`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- Texture upload now has stage boundaries, a real deferred material queue,
  structured diagnostics, smoke assertions, and a named queue state object.
- `Renderer.h` is still too large, but the texture path no longer injects
  source planning and publication implementation details into every renderer
  translation unit.

Next immediate boundary:

- Apply the same ownership cleanup to the renderer GPU job queue:
  - move queue stats and job records into a dedicated graphics header;
  - keep the renderer API stable for mesh upload and BLAS scheduling;
  - preserve per-frame bounded draining;
  - expose the resulting job queue state through existing diagnostics.

## Pass 11I - GPU Job Queue State Ownership

Completed:

- Added `GpuJobQueue.h` as the dedicated home for GPU job queue records and
  policy:
  - `GpuJobType`;
  - `GpuJob`;
  - `GpuJobQueueState`.
- Replaced loose renderer fields for mesh/BLAS queue data with one
  `m_gpuJobQueue` state object.
- Preserved the renderer public API for mesh upload scheduling and diagnostics:
  `HasPendingGpuJobs()`, `GetPendingMeshJobs()`, and `GetPendingBLASJobs()`.
- Routed frame-contract BLAS warming diagnostics, RT scheduling inputs, mesh
  upload enqueue, BLAS enqueue, bounded per-frame draining, and queue reset
  through the queue state object.
- Registered the new queue header in CMake.

Verified:

- Full `CortexEngine` build completed successfully after CMake regeneration.
- RT showcase smoke:
  `frames=33 gpu_ms=1.971/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.43
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.320 last_ms=2.440`.
- Temporal validation smoke:
  `gpu_ms=1.991 disocclusion=0.006845 high_motion=0.005223
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.188 last_ms=2.188`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- Both heavyweight frame-adjacent queues now have explicit state ownership:
  texture uploads use `TextureUploadQueueState`, and mesh/BLAS work uses
  `GpuJobQueueState`.
- The renderer API remained stable for callers while internal queue fields were
  reduced and centralized.
- The next refactor work should continue reducing `Renderer.h` dependency
  pressure and then move orchestration-only code out of the renderer class.

Next immediate boundary:

- Continue shrinking `Renderer.h` with dependency-oriented splits:
  - move inline render-scale policy out of the header;
  - move lightweight feature setter/getter implementation out of the header
    where it currently embeds behavior;
  - group public diagnostics accessors into narrow types where possible;
  - keep smoke validation on RT showcase and temporal validation after each
    header boundary.

## Pass 11J - Renderer Header Behavior Extraction

Completed:

- Moved render-scale policy out of `Renderer.h` and into
  `Renderer_QualitySettings.cpp`.
- Preserved render-scale behavior:
  - ignore scale changes after device removal;
  - clamp to the base render-scale range;
  - apply budget-planner render-scale caps;
  - apply 1440p/4K heavy-feature caps;
  - invalidate TAA, RT shadow, RT reflection, and RT GI histories when scale
    changes.
- Moved lightweight feature implementation out of `Renderer.h`:
  - `SetPCSS()`;
  - `SetFXAAEnabled()`;
  - `IsTAAEnabled()`;
  - `SetParticlesEnabled()`;
  - `GetParticlesEnabled()`.
- Moved GPU job queue diagnostics accessors out of `Renderer.h`:
  - `HasPendingGpuJobs()`;
  - `GetPendingMeshJobs()`;
  - `GetPendingBLASJobs()`.
- Left the public renderer API stable for UI, engine, diagnostics, and smoke
  scripts.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=2.089/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.466 last_ms=2.499`.
- Temporal validation smoke:
  `gpu_ms=2.007 disocclusion=0.006853 high_motion=0.005225
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.935 last_ms=1.935`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- `Renderer.h` still exposes many simple accessors, but it no longer embeds
  the render-scale budget policy or queue/feature behavior that changes more
  frequently than the public contract.
- This reduces accidental rebuild pressure and makes policy changes easier to
  review in focused implementation files.

Next immediate boundary:

- Continue extracting public inline accessors that expose renderer subsystem
  internals:
  - move placeholder texture accessors into texture implementation;
  - move timing and diagnostics accessors into diagnostics implementation;
  - move vegetation/wind accessors into vegetation implementation;
  - reassess whether small pure scalar feature getters should remain inline
    or move into a public renderer state snapshot.

## Pass 11K - Renderer Accessor Implementation Split

Completed:

- Moved placeholder texture accessors from `Renderer.h` to
  `Renderer_TextureCreation.cpp`.
- Moved asset registry, texture upload receipt, texture upload queue, mesh
  asset key, CPU pass timing, frame-contract, and visual-validation accessors
  into `Renderer_Diagnostics.cpp`.
- Moved vegetation and wind accessors into `Renderer_Vegetation.cpp`.
- Kept the renderer public API stable for engine, UI, diagnostics, and smoke
  tooling while reducing inline behavior in the public header.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.936/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.606 last_ms=2.536`.
- Temporal validation smoke:
  `gpu_ms=2.066 disocclusion=0.006757 high_motion=0.005205
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.168 last_ms=2.168`.
- Ninja dry-run reported `no work to do`.

## Pass 11L - Renderer Subsystem Accessor Extraction

Completed:

- Removed remaining public inline getter behavior from `Renderer.h` except the
  debug-overlay visibility mutator.
- Moved quality and presentation getters into the owning implementation files:
  - exposure and bloom into `Renderer_QualitySettings.cpp`;
  - shadow state into `Renderer_ShadowSettings.cpp`;
  - debug mode and HZB debug mip into `Renderer_DebugSettings.cpp`.
- Moved feature/state accessors into subsystem files:
  - fog, PCSS, FXAA, SSAO, IBL, SSR, god rays, area-light scale, RT feature
    toggles, ray-tracing support, and device-removed status into
    `Renderer_FeatureSettings.cpp`;
  - IBL residency limit into `Renderer_Environment.cpp`;
  - IBL intensity, sun intensity, and safe low-VRAM lighting rig toggle into
    `Renderer_LightingSettings.cpp`;
  - water parameter getters into `Renderer_SurfaceSettings.cpp`.
- Moved subsystem internals out of the header:
  - GPU culling state and freeze controls into `Renderer_GPUDriven.cpp`;
  - voxel backend state and dirty marking into `Renderer_Voxel.cpp`;
  - biome material validity and GPU-address access into
    `Renderer_Materials.cpp`;
  - command-list state query into `Renderer_Synchronization.cpp`.
- Used an explicit acquire load for biome material validity, matching its
  atomic ownership model.

Verified:

- Full `CortexEngine` build completed successfully after the header split.
- RT showcase smoke:
  `frames=33 gpu_ms=1.978/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.50
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.857 last_ms=2.770`.
- Temporal validation smoke:
  `gpu_ms=1.914 disocclusion=0.006771 high_motion=0.005210
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.055 last_ms=2.055`.
- Ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- `Renderer.h` is now much closer to a public contract: subsystem behavior has
  moved into subsystem implementation files, and queue state has explicit
  ownership.
- The class is still large because it declares many pass and subsystem methods,
  but the header no longer embeds most policy or access behavior.
- Remaining pressure is now architectural rather than cosmetic: pass state,
  feature state, and resource ownership still live in one renderer object.

Next immediate boundary:

- Replace scattered public feature reads with one or more explicit state
  snapshots for UI and diagnostics:
  - presentation/quality snapshot;
  - feature toggle snapshot;
  - RT capability snapshot;
  - frame-health snapshot if it can remove direct `Renderer` probing.
- Keep existing getters during transition, but route UI and diagnostics toward
  the snapshot API so future renderer decomposition has a smaller public
  surface.

## Pass 11M - Renderer State Snapshots

Completed:

- Added explicit renderer state snapshot structs:
  - `RendererQualityState`;
  - `RendererFeatureState`;
  - `RendererRayTracingState`;
  - `RendererWaterState`.
- Added renderer snapshot accessors:
  - `GetQualityState()`;
  - `GetFeatureState()`;
  - `GetRayTracingState()`;
  - `GetWaterState()`.
- Routed Core and UI readers away from scattered high-level renderer getters:
  - debug menu synchronization;
  - renderer startup debug-menu initialization;
  - frame report renderer feature block;
  - HUD/settings overlay state readback;
  - performance, quality, lighting, quick settings, and debug-menu windows;
  - LLM prompt context and simple water/exposure heuristics;
  - RT support checks in input/camera paths.
- Kept existing getters for compatibility with lower-level systems and
  commands, but made UI/diagnostic reads use stable snapshots.
- Verified with `rg` that `src/Core` and `src/UI` no longer directly read the
  migrated high-level feature getters.
- Follow-up: routed `LLM::CommandQueue` water-command readback through
  `GetWaterState()` and preserved current wave direction and steepness when
  applying partial water edits.
- Follow-up: made the legacy high-level getters compatibility wrappers over
  the snapshot API, so older call sites and new UI/diagnostics reads now share
  one renderer state read model.

Verified:

- Full `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.474/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.44
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.616 last_ms=3.301`.
- Temporal validation smoke:
  `gpu_ms=2.036 disocclusion=0.006734 high_motion=0.005203
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.534 last_ms=2.534`.
- Ninja dry-run reported `no work to do`.
- Follow-up LLM command queue build completed successfully, and a final ninja
  dry-run reported `no work to do`.
- Follow-up compatibility-wrapper build completed successfully, and a final
  ninja dry-run reported `no work to do`.

Current renderer-refactor state:

- Core/UI no longer need to know which individual renderer fields implement
  feature state.
- The renderer now has a compatibility API for older call sites and a clearer
  read model for newer architecture.
- This creates a practical path to move feature state out of `Renderer` later
  without updating every UI, frame-report, and LLM reader again.

Next immediate boundary:

- Continue reducing direct renderer coupling in non-UI systems:
  - audit `src/LLM` and command systems for direct feature-state reads;
  - decide whether command execution should keep calling mutators directly or
    use a renderer command facade;
  - separate read-only diagnostics from mutation commands so the future
    renderer modules can expose narrower interfaces.

## Pass 11N - Renderer Command Applier

Completed:

- Extracted the `modify_renderer` mutation logic out of `LLM::CommandQueue`
  into `LLM::RendererCommandApplier`.
- Added `RendererCommandApplyResult` so command execution receives an explicit
  success flag and status message instead of building status inline.
- Kept `CommandQueue::ExecuteModifyRenderer` responsible only for basic
  availability validation and status publication.
- Registered the new applier source/header in CMake.
- Preserved existing command behavior for:
  - exposure, shadows, shadow filter/bias/cascade controls;
  - debug mode;
  - environment and IBL controls;
  - color grade;
  - SSAO, fog, sun, water, and lighting rig controls.
- Routed partial water edits through `GetWaterState()` so omitted wave
  parameters keep the current renderer values.

Verified:

- Full `CortexEngine` build completed successfully after CMake regeneration.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=2.122/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.50
  material_issues=0 transient_delta=0`.
- Temporal validation smoke:
  `gpu_ms=1.340 disocclusion=0.006972 high_motion=0.005239
  object_motion=0.0731 visible=7 warnings=0`.

## Pass 11O - Renderer Control Applier

Completed:

- Added `Graphics::RendererDebugControlState` as a renderer-owned control
  contract for debug/persisted settings.
- Added `Graphics::RendererControlApplier` and moved debug-control mutation
  into graphics-owned functions:
  - `ApplyDebugControlState`;
  - `ApplyDebugControlReset`;
  - `ApplyHighQualityStartupControls`;
  - `ApplyHeroVisualBaselineControls`;
  - `ApplyAutoDemoFeatureLock`.
- Changed `UI::DebugMenu` so it only translates Win32 control state into a
  renderer control state, then calls the graphics applier.
- Removed direct renderer setter calls from the debug-menu apply and reset
  paths.
- Routed persisted debug-menu startup settings through
  `ApplyDebugControlState` instead of duplicating the low-level setter
  sequence in `Engine.cpp`.
- Routed high-quality startup defaults through
  `ApplyHighQualityStartupControls`.
- Routed the hero visual baseline through `ApplyHeroVisualBaselineControls`.
- Routed auto-demo RT/SSR/SSAO/environment enforcement through
  `ApplyAutoDemoFeatureLock`.
- Kept current behavior intact while giving future UI, LLM, and scene-preset
  code a concrete graphics-side control facade to converge on.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=2.136/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.42
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.282 last_ms=2.420`.
- Temporal validation smoke:
  `gpu_ms=1.219 disocclusion=0.006833 high_motion=0.005220
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.930 last_ms=1.930`.

Current renderer-refactor state:

- Renderer readback is now snapshot-based for Core/UI/LLM contexts that were
  previously probing scattered getters.
- LLM renderer commands now have a command applier instead of embedding
  renderer mutation policy inside `CommandQueue`.
- Debug UI and several Core startup/demo paths now call renderer-owned control
  appliers instead of repeating setter sequences.
- The remaining largest coupling concentration is scene preset construction:
  `Engine_Scenes.cpp` still mixes entity creation, material selection, camera
  placement, and renderer lighting/post-process tuning in the same functions.

Next immediate boundary:

- Split scene-preset renderer tuning away from scene-entity construction:
  - define renderer scene-preset control helpers for RT showcase, temporal
    validation, Cornell/hero studio, and experimental world scenes;
  - keep scene functions responsible for entities/cameras;
  - keep renderer-owned preset appliers responsible for lighting,
    post-process, RT, fog, water, and budget-sensitive quality settings.

## Pass 11P - Smoke-Covered Scene Preset Controls

Completed:

- Added renderer-owned scene preset control helpers:
  - `ApplyRTShowcaseSceneControls`;
  - `ApplyTemporalValidationSceneControls`.
- Moved RT showcase renderer tuning out of `BuildRTShowcaseScene`:
  - particles;
  - environment and IBL;
  - shadows;
  - sun direction/color/intensity;
  - courtyard water parameters;
  - default vs conservative render scale;
  - exposure, bloom, TAA/FXAA, SSR/SSAO, fog, and god-ray intensity.
- Moved temporal validation renderer tuning out of
  `BuildTemporalValidationScene`:
  - sun;
  - environment and IBL;
  - exposure and bloom;
  - fog/god-ray state;
  - SSAO parameters;
  - water parameters.
- Left the scene builders responsible for mesh upload, entity creation,
  materials, cameras, animation markers, and scene-specific object layout.
- Chose the smoke-covered scenes first so the refactor immediately exercised
  both new preset appliers.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.483/16.7 dxgi_mb=404.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.46
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=3.140 last_ms=5.365`.
- Temporal validation smoke:
  `gpu_ms=1.136 disocclusion=0.006902 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.468 last_ms=2.468`.

Current renderer-refactor state:

- The two main automated visual validation scenes now use renderer-side preset
  controls.
- `Engine_Scenes.cpp` is still large, but the tested RT showcase and temporal
  lab functions no longer own their renderer tuning.
- Remaining scene renderer debt is concentrated in less-covered scene builders
  and experimental world scenes.

Next immediate boundary:

- Continue extracting scene renderer controls where behavior is clear:
  - Cornell/hero studio lighting and water controls;
  - editor/world atmospheric controls;
  - duplicated infinite/experimental world renderer feature setup.
- Prefer adding focused smoke coverage before changing any scene whose visual
  behavior is not already covered by RT showcase or temporal validation.

## Pass 11Q - Remaining Scene and Editor Control Extraction

Completed:

- Added renderer-owned control helpers for the remaining obvious scene/editor
  renderer-tuning blocks:
  - `ApplyCornellSceneControls`;
  - `ApplyGodRaysSceneControls`;
  - `ApplyDragonWaterStudioSunControls`;
  - `ApplyOutdoorWorldSceneControls`;
  - `ApplyEditorModeBaseControls`;
  - `ApplyEditorTimeOfDayControls`.
- Removed the remaining clustered renderer setter sequences from
  `Engine_Scenes.cpp` for:
  - Cornell lighting/fog/god-ray/water setup;
  - god-rays gallery lighting/fog/god-ray/water setup;
  - dragon-over-water studio sun setup;
  - terrain world and editor terrain outdoor renderer setup.
- Removed editor mode direct IBL/fog/time-of-day renderer setup from
  `EngineEditorMode.cpp`.
- Verified with `rg` that `Engine_Scenes.cpp` and `EngineEditorMode.cpp` no
  longer contain the extracted high-level renderer setter clusters.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.629/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.84 center_luma=73.52
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.786 last_ms=2.478`.
- Temporal validation smoke:
  `gpu_ms=2.245 disocclusion=0.006835 high_motion=0.005220
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=4.061 last_ms=4.061`.

Current renderer-refactor state:

- Scene builders are materially cleaner: they now focus more on scene graph,
  mesh upload, materials, cameras, and layout, while renderer tuning has moved
  into `Graphics::RendererControlApplier`.
- The renderer has a concrete control facade used by:
  - DebugMenu;
  - persisted debug-menu startup state;
  - high-quality startup defaults;
  - hero visual baseline;
  - auto-demo enforcement;
  - RT showcase and temporal validation scene presets;
  - Cornell, god-rays, dragon-water, outdoor world, and editor mode setup.
- Remaining direct renderer mutation in Core/UI is mostly interactive control:
  keyboard shortcuts, quality/performance windows, lighting sliders, and
  governors.

Next immediate boundary:

- Turn the control applier into a more formal renderer command/control layer:
  - add typed helpers for interactive feature toggles and quality sliders;
  - route Performance/Quality/Lighting/QuickSettings windows through those
    helpers;
  - keep direct renderer calls only for low-level renderer internals and
    resource operations such as mesh upload.

## Pass 11R - Interactive UI Renderer Controls

Completed:

- Extended `Graphics::RendererControlApplier` with typed interactive controls:
  - render scale;
  - bloom intensity;
  - feature toggles for RT, RT reflections, RT GI, TAA, FXAA, SSR, SSAO,
    IBL, fog, particles, IBL residency limit, and PCSS;
  - sun intensity;
  - IBL diffuse/specular intensity;
  - god-ray intensity;
  - safe lighting rig toggle;
  - water steepness;
  - fog density;
  - area-light size scale;
  - safe quality preset;
  - environment residency load;
  - debug-view and environment cycling.
- Routed the following UI windows through the typed renderer control layer:
  - `PerformanceWindow`;
  - `QualitySettingsWindow`;
  - `LightingWindow`;
  - `QuickSettingsWindow`.
- Removed direct renderer setter/cycle calls from those four UI files for the
  extracted interactions.
- Kept UI readback through existing snapshots and kept UI-specific scene/light
  editing behavior local to the UI code.
- Removed a stale `<cmath>` include after render-scale delta checks moved into
  the control applier.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Direct-call audit reported no `renderer->Set*`, safe-preset,
  environment-load, or renderer cycle calls in the four targeted UI files.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=2.252/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.46
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.361 last_ms=2.556`.
- Temporal validation smoke:
  `gpu_ms=1.953 disocclusion=0.006746 high_motion=0.005204
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.010 last_ms=2.010`.

Current renderer-refactor state:

- The renderer control facade is now used by scene presets, debug-menu state,
  startup defaults, and the main interactive quality/performance/lighting
  windows.
- UI code is closer to being pure interaction/readback code instead of owning
  renderer mutation policy.
- Remaining direct renderer mutation outside graphics internals is mostly in
  Core runtime shortcuts/governors, LLM command appliers, and specialized scene
  resource operations.

Next immediate boundary:

- Route Core runtime controls through the same control facade:
  - render-scale changes from the VRAM/perf governors;
  - keyboard-driven FXAA/RT toggles;
  - startup RT enable/disable handling;
  - world-state sun updates where they are not already routed through scene or
    editor control helpers.

## Pass 11S - Core Runtime Renderer Controls

Completed:

- Routed Core runtime renderer policy through `Graphics::RendererControlApplier`:
  - RT-showcase startup render-scale preset;
  - conservative startup quality preset;
  - optional startup RT enable/disable handling;
  - procedural terrain time-of-day sun updates;
  - VRAM governor render-scale and safe-preset changes;
  - performance governor render-scale changes;
  - keyboard FXAA and RT feature toggles.
- Kept Core responsible for application flow and policy decisions while moving
  renderer mutation details behind the Graphics control facade.
- Preserved debug-menu synchronization after runtime controls that affect
  visible renderer state.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Targeted Core direct-call audit reported no remaining direct calls for:
  `SetRenderScale`, `SetRayTracingEnabled`, `SetFXAAEnabled`, `SetSunDirection`,
  `SetSunColor`, `SetSunIntensity`, or `ApplySafeQualityPreset`.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.599/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.83 center_luma=73.48
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.537 last_ms=3.288`.
- Temporal validation smoke:
  `gpu_ms=2.289 disocclusion=0.006859 high_motion=0.005227
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.752 last_ms=2.752`.

Current renderer-refactor state:

- The renderer control facade now owns most high-level renderer mutation from
  startup, Core governors, scene presets, debug state, UI windows, and LLM
  command application.
- Remaining direct Core renderer mutation is concentrated in keyboard-only
  debug/shadow/environment controls plus low-level renderer lifecycle and
  resource operations.

Next immediate boundary:

- Route the remaining keyboard-only debug and shadow controls through the same
  facade:
  - TAA and shadow feature toggles;
  - shadow PCF radius and bias deltas;
  - cascade split and cascade-resolution deltas;
  - debug-view and environment preset cycling.

## Pass 11T - Keyboard Debug Renderer Controls

Completed:

- Extended `Graphics::RendererControlApplier` with:
  - `RendererFeatureToggle::Shadows`;
  - `ToggleFeatureControl`;
  - shadow PCF radius delta control;
  - shadow bias delta control;
  - cascade split lambda delta control;
  - cascade resolution scale delta control.
- Routed keyboard renderer shortcuts in `Engine_Input.cpp` through the control
  facade:
  - FXAA and TAA toggles;
  - shadow enable toggle;
  - shadow PCF radius and bias adjustments;
  - cascade split and cascade-resolution adjustments;
  - debug-view cycling;
  - environment preset cycling.
- Preserved `SyncDebugMenuFromRenderer()` after controls that change debug-menu
  mirrored renderer state.

Verified:

- Targeted audits reported no remaining direct Core/UI/Scene/LLM calls for:
  `ToggleTAA`, `ToggleShadows`, `AdjustShadow*`, `AdjustCascade*`,
  `CycleDebugViewMode`, `CycleEnvironmentPreset`, `SetRayTracingEnabled`,
  `SetRenderScale`, or `ApplySafeQualityPreset`.
- Incremental `CortexEngine` build completed successfully.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.607/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.82 center_luma=73.45
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.434 last_ms=2.601`.
- Temporal validation smoke:
  `gpu_ms=1.66 disocclusion=0.006774 high_motion=0.005211
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=2.002 last_ms=2.002`.

Current renderer-refactor state:

- High-level renderer control is now consistently mediated through the Graphics
  control layer across startup, UI, scene presets, LLM commands, governors, and
  keyboard shortcuts.
- The remaining direct renderer calls outside `src/Graphics` are expected to be
  low-level lifecycle/resource operations, scene upload paths, state readback, or
  explicit command adapters.
- `RendererControlApplier` is now broad enough that the next cleanup should
  prevent it from becoming a new monolithic catch-all file.

Next immediate boundary:

- Audit the full non-Graphics call surface and classify remaining renderer
  access into:
  - accepted lifecycle/resource/readback calls;
  - command adapters that can remain explicit boundaries;
  - high-level mutation that still belongs behind a control facade.
- Split the control facade implementation into smaller domain files while
  keeping the public header stable:
  - debug/debug-menu controls;
  - scene-preset controls;
  - interactive/runtime controls.

## Pass 11U - Renderer Control Facade Split

Completed:

- Audited non-Graphics renderer access after the control-layer routing work.
- Classified the remaining direct calls outside `src/Graphics`:
  - accepted lifecycle/render calls: initialize, render, editor render phases,
    shutdown waits, and scene-change synchronization;
  - accepted resource calls: mesh upload, texture upload, placeholder textures,
    asset registry/memory readback, and UI draw calls;
  - accepted diagnostics/readback calls: quality, feature, RT, GPU profile,
    frame contract, descriptor, memory, upload queue, and validation state;
  - remaining high-level mutation: Core debug/backend controls and the explicit
    renderer command adapter.
- Split the broad control implementation into domain files while preserving the
  stable public include:
  - `RendererControlApplier_Debug.cpp`;
  - `RendererControlApplier_Runtime.cpp`;
  - `RendererControlApplier_ScenePresets.cpp`.
- Kept `RendererControlApplier.cpp` as a small anchor translation unit so the
  facade remains discoverable without becoming a monolithic implementation file.
- Updated `CMakeLists.txt` to build the new domain files.

Verified:

- CMake regenerated successfully and a full `CortexEngine` rebuild completed.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.957/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.80 center_luma=73.41
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.208 last_ms=2.424`.
- Temporal validation smoke:
  `gpu_ms=1.953 disocclusion=0.006765 high_motion=0.005210
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.880 last_ms=1.880`.

Current renderer-refactor state:

- The renderer control layer is now a real module boundary rather than a single
  accumulating file.
- The public API remains stable for existing Core/UI/Scene callers.
- Non-Graphics renderer mutation is reduced to a narrow set of remaining Core
  debug/backend setters and the explicit LLM renderer-command adapter.

Next immediate boundary:

- Route the remaining Core debug/backend controls through the facade:
  - GPU culling enable and freeze;
  - voxel backend selection;
  - explicit debug-view and HZB debug-mip controls;
  - debug overlay state.

## Pass 11V - Core Debug and Backend Controls

Completed:

- Added typed facade helpers for Core debug/backend mutation:
  - `ApplyGPUCullingEnabledControl`;
  - `ToggleGPUCullingFreezeControl`;
  - `ApplyVoxelBackendControl`;
  - `ApplyDebugViewModeControl`;
  - `ApplyDebugOverlayControl`;
  - `ApplyHZBDebugMipDeltaControl`.
- Routed Core startup/backend/debug paths through those helpers:
  - GPU culling startup enable;
  - experimental voxel backend selection;
  - `CORTEX_DEBUG_VIEW` override;
  - in-shader debug overlay state;
  - K-key GPU-culling freeze;
  - HZB debug-mip bracket controls.
- Re-ran the mutation audit. The only remaining `Set/Apply/Cycle/Toggle/Adjust`
  style renderer mutation outside Graphics is `ApplyLightingRig`, which still
  crosses renderer and ECS state and should either stay as an explicit scene
  command boundary or be moved into a dedicated lighting-system command pass.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Targeted mutation audit reported only:
  `Engine.cpp: m_renderer->ApplyLightingRig(rig, m_registry.get())`.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.958/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.43
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.221 last_ms=2.343`.
- Temporal validation smoke:
  `gpu_ms=2.139 disocclusion=0.006768 high_motion=0.005209
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.874 last_ms=1.874`.

Current renderer-refactor state:

- High-level Core/UI/Scene renderer mutation is now behind the Graphics control
  facade with one lighting-rig exception.
- Non-Graphics direct renderer dependencies are now mostly readback,
  lifecycle/render execution, upload/resource operations, UI drawing, and
  explicit command adapter logic.
- This is a stronger architectural position than the earlier scattered setter
  calls: renderer policy has a named boundary and can be tested/audited.

Next immediate boundary:

- Decide the fate of command adapters:
  - keep `LLM::RendererCommandApplier` as the single explicit boundary for
    natural-language renderer mutation;
  - or move its renderer field application through typed Graphics controls for
    consistency and validation.
- The more academically correct next step is to extend the control layer with
  absolute-value helpers for renderer command fields, then route
  `RendererCommandApplier` through those helpers while preserving its command
  parsing and summary responsibilities.

## Pass 11W - LLM Renderer Command Control Routing

Completed:

- Kept `LLM::RendererCommandApplier` as the explicit natural-language command
  boundary, but routed ordinary renderer field application through typed
  Graphics controls.
- Added absolute-value controls for command-driven renderer edits:
  - exposure;
  - shadow bias, shadow PCF radius, and cascade split lambda;
  - environment preset;
  - sun direction, color, and intensity;
  - color grading;
  - SSAO parameter tuples;
  - fog parameter tuples;
  - water level/amplitude/length/speed/secondary-amplitude state.
- Reused existing feature toggles for LLM-driven shadows, IBL, SSAO, and fog.
- Preserved `RendererCommandApplier` responsibilities:
  - decode `ModifyRendererCommand` fields;
  - build user/status summaries;
  - map lighting-rig names to renderer lighting rigs.
- Left lighting-rig application direct for now because it deliberately crosses
  renderer policy and ECS light entity mutation.

Verified:

- Targeted mutation audit now reports only two lighting-rig direct calls outside
  Graphics:
  - `LLM::RendererCommandApplier`;
  - `Engine::ApplyLightingRig` path.
- Incremental `CortexEngine` build completed successfully.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.952/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.42
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.235 last_ms=2.668`.
- Temporal validation smoke:
  `gpu_ms=2.107 disocclusion=0.006814 high_motion=0.005218
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.981 last_ms=1.981`.

Current renderer-refactor state:

- High-level renderer mutation from UI, Core runtime, scene presets, debug menu,
  startup, and LLM renderer commands is now mediated by the Graphics control
  facade.
- The only remaining direct high-level mutation outside Graphics is lighting-rig
  application, which should become a dedicated scene-lighting command boundary
  rather than being folded into generic renderer controls.
- The renderer is better positioned for the next maturity step: scene lighting
  commands and material/lighting correctness can be addressed without scattered
  renderer setters competing with UI and command policy.

Next immediate boundary:

- Create a dedicated lighting command boundary for renderer/ECS lighting rigs:
  - wrap `Renderer::ApplyLightingRig` behind a narrow helper or service;
  - route both Core shortcuts and LLM lighting-rig commands through it;
  - keep actual ECS light creation inside the renderer or move it to a future
    Scene lighting system when the render/scene ownership boundary is revisited.

## Pass 11X - Lighting-Rig Command Boundary

Completed:

- Added `Graphics::RendererLightingRigControl` as a narrow boundary for
  lighting-rig commands that cross renderer policy and ECS light state.
- Routed both remaining direct lighting-rig callers through the boundary:
  - Core debug-menu lighting-rig application;
  - LLM `modify_renderer` lighting-rig application.
- Kept `Renderer::ApplyLightingRig` as the underlying implementation for now.
  Moving the actual light creation out of Renderer is a larger scene-lighting
  ownership pass and should be done deliberately, not as incidental cleanup.
- Updated `CMakeLists.txt` with the new source/header.

Verified:

- High-level renderer mutation audit outside `src/Graphics` now reports no
  `Set/Apply/Cycle/Toggle/Adjust` renderer calls in Core, UI, Scene, LLM, or
  Editor.
- CMake regenerated successfully and a full `CortexEngine` rebuild completed.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=2.065/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.81 center_luma=73.43
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.211 last_ms=2.380`.
- Temporal validation smoke:
  `gpu_ms=2.014 disocclusion=0.006757 high_motion=0.005208
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.901 last_ms=1.901`.

Current renderer-refactor state:

- The high-level renderer mutation boundary is now clean:
  - Core/UI/Scene/LLM no longer scatter renderer setter/toggle/adjust calls;
  - renderer state changes flow through typed Graphics control helpers;
  - lighting rigs have a dedicated bridge because they touch renderer and ECS
    lighting state together.
- Remaining direct renderer access outside Graphics is now expected readback,
  upload/resource work, render execution, debug drawing, UI drawing, and
  lifecycle synchronization.
- This completes the control-surface cleanup portion of Phase 2 and gives the
  next rendering work a stable place to hang policy, validation, and testing.

Next immediate boundary:

- Move from control-surface cleanup to visual/render correctness:
  - inspect the material model and RT/raster material parity;
  - verify texture residency and default material fallbacks;
  - make the RT showcase visibly more stable and predictable rather than only
    architecturally cleaner.

## Pass 11Y - Shared Material Preset Resolution

Completed:

- Moved material-preset intent into the shared material model instead of
  leaving it as shader-path-specific convention.
- Extended `MaterialPresetInfo` with conservative physical defaults:
  - default metallic, roughness, transmission, and emissive strength;
  - opt-in flags so authored scalar/texture data is not overwritten.
- Applied preset defaults in `MaterialResolver::ResolveRenderable` only when
  the authored value is still default-like and no authored map exists.
- Normalized common presets across raster, visibility-buffer, transparent, and
  RT material paths:
  - glass now defaults to dielectric transmission and low roughness;
  - mirror/chrome/polished metal/gold now default to metallic conductor values;
  - plastic, brick, wood, cloth, and velvet get nonmetal roughness defaults;
  - emissive/neon/light presets now produce visible emission when no emissive
    factor or map was authored.
- Routed additional render paths through `MaterialResolver`:
  - transparent geometry;
  - overlay geometry;
  - alpha-tested depth prepass;
  - alpha-tested shadow pass.
- Updated surface classification so chrome, polished metal, and gold are
  treated as polished conductor classes by deferred/post/RT reflection policy.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Material-path audit now shows shared resolver usage in:
  - forward;
  - indirect rendering;
  - transparency;
  - overlays;
  - alpha depth;
  - alpha shadows;
  - visibility-buffer collection;
  - RT TLAS material upload.
- Ninja dry-run reported `no work to do`.
- RT showcase smoke:
  `frames=33 gpu_ms=1.951/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.79 center_luma=73.41
  material_issues=0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.145 last_ms=2.420`.
- Temporal validation smoke:
  `gpu_ms=2.134 disocclusion=0.006777 high_motion=0.005211
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.833 last_ms=1.833`.

Current renderer-refactor state:

- The control surface is clean and renderer mutation policy is centralized.
- Material intent now has a single CPU-side resolver used by the main material
  paths rather than duplicate preset parsing in individual passes.
- The remaining visible-correctness work should focus on proving the resolved
  material model reaches diagnostics and frame contracts, then using that data
  to tune shader behavior and scene composition.

Next immediate boundary:

- Add material-intent diagnostics to the frame contract:
  - count preset/default applications by class;
  - expose conductor, transmissive, and emissive resolved counts;
  - make RT/raster material parity measurable in smoke logs instead of
    relying only on visual captures.

## Pass 11Z - Resolved Material Frame Contract

Completed:

- Added resolved material diagnostics to the frame contract:
  - named preset count;
  - preset-default applications for metallic, roughness, transmission, and
    emission;
  - resolved metallic, conductor, transmissive, emissive, and clearcoat counts.
- Switched material luminance, roughness, metallic, and average material stats
  to report the resolved material model instead of raw renderable fields.
- Kept raw authoring validation checks for out-of-range roughness and metallic
  so bad scene input is still visible.
- Updated RT showcase smoke validation to require the showcase to contain:
  - at least one resolved conductor material;
  - at least one resolved transmissive material;
  - at least one resolved emissive material.
- Updated the RT smoke summary line to print resolved material counts and
  preset-default counters.

Verified:

- Full `CortexEngine` rebuild completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.968/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.79 center_luma=73.42
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.283 last_ms=2.711`.
- Temporal validation smoke:
  `gpu_ms=2.016 disocclusion=0.006764 high_motion=0.005209
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.885 last_ms=1.885`.

Current renderer-refactor state:

- Material intent is no longer a hidden visual assumption. The frame contract
  can now prove whether a scene actually contains resolved conductors,
  transmissive surfaces, emissive surfaces, and preset-derived defaults.
- The smoke tests now protect this material contract, which makes future shader
  tuning less likely to silently regress scene semantics.

Next immediate boundary:

- Tighten RT/raster material parity:
  - compare visibility-buffer material classes against RT material classes;
  - expose RT material resolved scalar ranges where useful;
  - reduce duplicated material-class decisions between TLAS and VB collection.

## Pass 11AA - Resolved Surface Classification

Completed:

- Added resolved-material overloads for surface classification:
  - `ClassifySurface(const MaterialModel&)`;
  - `ClassifyMaterialSurface(const MaterialModel&)`.
- Routed key material-class consumers through the resolved material model:
  - frame-contract material class counts;
  - visibility-buffer material class upload;
  - RT TLAS material records and RT material class counters.
- Kept the renderable overload available for older call sites, but the active
  VB/RT/frame-contract material paths no longer classify from raw material
  scalars.
- Classification now sees preset-derived scalar fixes:
  - conductor defaults can become brushed-metal/mirror class policy;
  - preset transmission can become glass policy;
  - preset or resolved emission can become emissive policy.

Verified:

- Incremental `CortexEngine` build completed successfully.
- Audit found no remaining live calls to classify VB, RT, or frame-contract
  material classes from raw renderables.
- RT showcase smoke:
  `frames=33 gpu_ms=1.957/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.79 center_luma=73.41
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.144 last_ms=2.462`.
- Temporal validation smoke:
  `gpu_ms=2.032 disocclusion=0.006771 high_motion=0.005211
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.944 last_ms=1.944`.

Current renderer-refactor state:

- Material resolution and material classification now form a coherent CPU-side
  contract shared by raster, visibility-buffer, RT, and diagnostics.
- This reduces one of the main causes of visual inconsistency: a material could
  previously look metallic in one path while still being treated as a generic
  surface in another.

Next immediate boundary:

- Continue visible shader correctness:
  - inspect how resolved material classes drive deferred lighting, SSR, RT
    reflection blending, and denoising;
  - remove any duplicated shader-side class heuristics that fight the CPU
    material contract;
  - tune material-specific reflection and roughness floors from measured
    results rather than one-off scene guesses.

## Pass 11AB - Forward Conductor Energy Correction

Completed:

- Removed the forward-shader metallic clamp that forced full conductors down to
  `0.95`.
- Forward materials now preserve authored/resolved conductor values with
  `metallic = saturate(metallic)`.
- Kept dielectric overrides for glass, plastic, brick, and emissive classes,
  which are material-class policy rather than an artificial readability hack.
- This aligns the forward BRDF with the deferred path, where metallic already
  participates in the Cook-Torrance energy split without a forced diffuse
  contribution.

Verified:

- RT showcase smoke recompiled the affected `Basic.hlsl` DXC shaders and
  passed:
  `frames=33 gpu_ms=1.969/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.78 center_luma=73.37
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.114 last_ms=2.450`.
- Temporal validation smoke:
  `gpu_ms=2.033 disocclusion=0.006856 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.968 last_ms=1.968`.

Current renderer-refactor state:

- The material pipeline is now materially more coherent:
  - shared preset resolution;
  - shared CPU material classification;
  - resolved material diagnostics;
  - smoke coverage for resolved conductor/transmissive/emissive presence;
  - no forced diffuse fallback for forward-path conductors.

Next immediate boundary:

- Continue shader correctness with reflection policy:
  - inspect SSR/RT blend weights for mirror, brushed-metal, glass, water, and
    default materials;
  - verify material-class reflection ceilings are not suppressing the showcase;
  - make any tuning data-driven through frame-contract/debug metrics.

## Pass 11AC - Material-Aware Reflection Policy Diagnostics

Completed:

- Fixed a reflection blend policy hole in post-process composition:
  - mirror-class surfaces now suppress unstable SSR without accidentally
    suppressing RT reflections through stale raw SSR confidence;
  - RT reflection weight now uses effective SSR confidence, material
    reflectance, roughness, and surface-class policy together.
- Added shared shader-side reflection ceilings for mirror, water, glass,
  brushed metal, plastic, masonry, wood, and fallback conductor/default
  materials.
- Routed post-process reflection composition through resolved material data:
  - metallic from the emissive/metallic G-buffer;
  - transmission from material extension data;
  - surface class and reflection-class mask from material extension data.
- Added CPU-side frame-contract diagnostics for reflection policy:
  - reflection-eligible material count;
  - high-ceiling reflection material count;
  - mirror, conductor, transmissive, and water reflection buckets;
  - max and average reflection ceiling estimates.
- Updated the RT showcase smoke gate so it fails if the scene loses its
  reflection-capable material contract.
- Updated the RT showcase summary line to print reflection diagnostics as
  `eligible/high_ceiling/max_ceiling/avg_ceiling`.

Verified:

- Incremental `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.950/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.02
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.187 last_ms=2.386`.
- Temporal validation smoke:
  `gpu_ms=2.058 disocclusion=0.006854 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.782 last_ms=1.782`.

Current renderer-refactor state:

- Reflection behavior is no longer tuned only by hidden shader constants. The
  frame contract now proves that the showcase contains materials that should
  receive strong reflections and exposes whether shader ceilings would allow
  those reflections.
- The material contract now spans authored presets, resolved CPU material
  classes, raster/RT upload paths, post-process reflection policy, and smoke
  validation.

Next immediate boundary:

- Continue visible correctness by tightening the deferred lighting and RT
  reflection path itself:
  - compare CPU material class counts against RT TLAS material class counts in
    the frame contract;
  - expose RT reflection dispatch/input health beyond scheduler dimensions;
  - verify that mirror/conductor/glass/water classes survive through
    visibility-buffer lighting, RT material records, denoising, and final
    composition.

## Pass 11AD - RT Material Surface Parity Contract

Completed:

- Added explicit RT/snapshot material surface parity diagnostics to the frame
  contract:
  - whether the RT material record count is comparable with the resolved
    snapshot material count;
  - whether the nine surface-class buckets match;
  - summed surface-class mismatch count.
- Moved the parity check into `FrameContractValidation` so it is part of the
  renderer contract rather than an ad hoc smoke-script comparison.
- Updated the frame-contract JSON with the new parity fields.
- Updated the RT showcase smoke to fail when:
  - RT/snapshot material parity is not comparable;
  - parity does not match;
  - the mismatch count is nonzero.
- Updated the RT showcase summary line with `rt_parity=matches/mismatches`.

Verified:

- Incremental `CortexEngine` build completed successfully.
- RT showcase smoke:
  `frames=33 gpu_ms=1.951/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.99 center_luma=74.00
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 transient_delta=0`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.079 last_ms=2.404`.
- Temporal validation smoke:
  `gpu_ms=2.054 disocclusion=0.006777 high_motion=0.005212
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.882 last_ms=1.882`.

Current renderer-refactor state:

- The renderer can now prove that resolved material classes match between the
  CPU scene snapshot and the RT TLAS material buffer for the RT showcase.
- This closes a key correctness gap: if a mirror, glass, water, or conductor
  becomes a different class in RT than in raster/post-process diagnostics, the
  validation path will report it immediately.

Next immediate boundary:

- Expose RT reflection dispatch/input health:
  - record whether reflection dispatch had a valid TLAS, material buffer,
    output target, depth, normal/roughness, and camera state;
  - make the frame contract distinguish scheduler intent from dispatch
    readiness;
  - gate the RT showcase on valid reflection inputs so a scene cannot silently
    fall back to weak or missing RT reflections.

## Pass 11AE - RT Reflection Dispatch Readiness Contract

Completed:

- Added explicit RT reflection dispatch readiness diagnostics to the frame
  contract:
  - reflection pipeline availability;
  - TLAS availability;
  - RT material-buffer availability;
  - reflection output target/UAV availability;
  - depth input availability;
  - normal/roughness input availability;
  - material-ext2 input availability;
  - environment descriptor-table availability;
  - frame-constant buffer availability;
  - RT dispatch descriptor-table availability;
  - actual reflection dispatch extent;
  - readiness reason.
- Added DXR context accessors for TLAS, RT material-buffer, and dispatch
  descriptor-table readiness.
- Populated readiness diagnostics inside `RenderRayTracedReflections`, where
  the renderer has the final selected normal/material inputs and dispatch
  dimensions.
- Copied the readiness fields during both initial frame-contract snapshot
  creation and the end-of-frame contract refresh. This fixed a diagnostic
  lifecycle issue where pass records showed `RTReflections` executed but the
  new readiness fields still contained their beginning-of-frame reset values.
- Extended frame-contract validation with warnings for:
  - scheduled reflection dispatch without readiness;
  - invalid reflection dispatch extent;
  - reflection writes without a ready dispatch contract.
- Updated the RT showcase smoke to require ready RT reflection inputs and to
  print `rt_refl_ready=ready/reason` in the summary.

Verified:

- Incremental `CortexEngine` build completed successfully.
- First RT showcase smoke intentionally exposed the missing end-of-frame field
  refresh:
  `rt_reflection_dispatch_not_ready`, `rt_reflection_dispatch_invalid_extent`,
  and `rt_reflection_written_without_ready_contract`.
- After fixing the end-of-frame contract refresh, RT showcase smoke passed:
  `frames=33 gpu_ms=2.074/16.7 dxgi_mb=403.95/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.01 center_luma=74.04
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0
  rt_refl_ready=True/ready transient_delta=0`.
- The final RT showcase report also confirms:
  `reflection_has_material_ext2=True`, `reflection_dispatch_width=544`,
  and `reflection_dispatch_height=306`.
- RT showcase diagnostics recorded:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB
  avg_ms=2.237 last_ms=2.474`.
- Temporal validation smoke:
  `gpu_ms=2.026 disocclusion=0.006966 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation diagnostics recorded:
  `submitted=1 completed=1 failed=0 pending=0 uploaded=2.67MB
  avg_ms=1.949 last_ms=1.949`.

Current renderer-refactor state:

- RT reflection dispatch is now explainable in the frame contract. The engine
  can distinguish scheduler intent, pass execution, and actual input readiness.
- The RT showcase is now protected against silent regressions where reflection
  dispatch appears enabled but lacks TLAS/material/depth/normal/output/env
  inputs.

Next immediate boundary:

- Move from readiness diagnostics to signal quality:
  - expose current RT reflection buffer luma/nonzero coverage in the frame
    contract;
  - validate that RT reflection output contains meaningful signal in the
    showcase, not only that dispatch inputs were valid;
  - use that signal data to tune denoising/composition without relying on
    screenshots alone.

## Pass 11AF - RT Reflection Signal Quality Contract

Completed:

- Added a GPU-side RT reflection signal reduction pass:
  - `RTReflectionSignalStats` compute pipeline with clear/reduce kernels;
  - raw UAV stats buffer plus triple-buffered readback buffers;
  - per-frame descriptor tables for the reflection SRV and stats UAV;
  - pass record `RTReflectionSignalStats`.
- The pass samples the current `rt_reflection` target after the DXR reflection
  dispatch and before RT denoising:
  - average luma;
  - max HDR luma;
  - nonzero coverage ratio;
  - bright-pixel ratio;
  - pixel count;
  - readback sample frame and latency.
- Exposed the signal metrics in the frame contract under `ray_tracing`:
  - `reflection_signal_stats_captured`;
  - `reflection_signal_valid`;
  - `reflection_signal_sample_frame`;
  - `reflection_signal_pixel_count`;
  - `reflection_signal_avg_luma`;
  - `reflection_signal_max_luma`;
  - `reflection_signal_nonzero_ratio`;
  - `reflection_signal_bright_ratio`;
  - `reflection_signal_readback_latency_frames`.
- Added `rt_reflection_signal_stats` to the frame resource contract and marked
  it as an unordered-access write resource.
- Extended frame-contract validation with warnings for:
  - reflection writes without signal stats;
  - signal validity without pixels;
  - signal ratios outside `[0, 1]`.
- Updated the RT showcase smoke to require:
  - the signal stats pass and resource;
  - captured/valid signal stats;
  - positive pixel count;
  - nonzero reflection coverage;
  - positive reflection max luma.
- Added `rt_signal=avg/nonzero/max` to the RT showcase smoke summary.

Verified:

- Full `CortexEngine` rebuild completed successfully after CMake regeneration.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.588/16.7 dxgi_mb=404.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.15 center_luma=74.52
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000 transient_delta=0 rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.232 disocclusion=0.006736 high_motion=0.005203
  object_motion=0.0731 visible=7 warnings=0`.

Current renderer-refactor state:

- RT reflections are now measured at three levels:
  - scheduler intent;
  - dispatch/input readiness;
  - actual output signal quality.
- The showcase can no longer silently pass with a ready reflection dispatch that
  produces an empty or unmeasured reflection target.

Next immediate boundary:

- Use the signal metrics to tune the RT reflection denoiser/compositor:
  - compare raw reflection signal against denoised history;
  - detect over-bright/clamped reflection outliers instead of accepting maxed
    HDR values blindly;
  - expose per-surface reflection contribution so mirror/glass/water behavior
    can be tuned from contract data rather than only from screenshots.

## Pass 11AG - Denoised Reflection History Signal Contract

Completed:

- Extended `RTReflectionSignalStats` so the raw `rt_reflection` target and the
  denoised `rt_reflection_history` target are measured by separate compute
  entry points:
  - raw clear/reduce pipelines bind `rt_reflection` at stable SRV slot `t0`
    and write `rt_reflection_signal_stats` at UAV slot `u0`;
  - history clear/reduce pipelines bind `rt_reflection_history` at stable SRV
    slot `t1` and write `rt_reflection_history_signal_stats` at UAV slot `u1`;
  - both paths reuse the same descriptor table shape, keeping persistent
    descriptor pressure under the 1024 smoke budget.
- Fixed a command-list descriptor overwrite bug in the stats path. Raw and
  history reductions can now be recorded in the same command list without the
  later history descriptor writes changing what the earlier raw dispatch reads.
- Fixed the same class of bug in `RTDenoiser`:
  - shadow, reflection, and GI denoise dispatches now use stable SRV/UAV slots
    in the shared descriptor table;
  - reflection history now receives the reflection denoiser output instead of
    being vulnerable to later GI descriptor writes.
- Added denoised history signal diagnostics to the frame contract:
  - captured/valid state;
  - sample frame and readback latency;
  - pixel count;
  - average luma;
  - max luma;
  - nonzero ratio;
  - bright ratio;
  - outlier ratio;
  - raw-vs-history average luma delta.
- Updated the readback updater so raw and history stats are read independently.
  A failed or empty raw readback no longer prevents history stats from being
  consumed, and cadence-skip frames keep the last valid metrics unless an
  actual pending readback is consumed and found invalid.
- Tightened RT showcase smoke coverage:
  - `rt_hist=avg/nonzero/max/delta` is printed in the summary;
  - history signal capture, validity, pixel count, nonzero ratio, and max luma
    are gated when reflection dispatch artifacts are required;
  - signal-stat pass/resource requirements respect `-AllowRTCadenceSkips`, so
    low-budget cadence skip frames validate scheduler intent without requiring
    dispatch-only artifacts.

Verified:

- Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.231/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.28 center_luma=74.96
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.406 disocclusion=0.007377 high_motion=0.005247
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The latest source and Release executable are back in sync.
- The denoised reflection history signal pass is no longer WIP; it is rebuilt,
  smoke-gated, cadence-aware, and covered by the budget matrix.
- RT reflections are now measured at four useful levels:
  - scheduler intent;
  - dispatch/input readiness;
  - raw output signal quality;
  - denoised history signal quality.
- Descriptor pressure is back under the smoke budget after the raw/history
  stats table reuse work. The current RT showcase reports
  `persistent_descriptors=988` and `transient_delta=0`.

Next immediate boundary:

- Use raw-vs-history metrics for actual denoiser/composition tuning:
  - clamp or account for overbright reflection outliers before composition;
  - tune temporal accumulation so history reduces noise without hiding missing
    raw reflection signal;
  - group RT reflection/denoise/stats resources into smaller pass-owned state
    structs to reduce `Renderer` member sprawl.

## Pass 11AH - RT Denoise and Signal State Ownership

Completed:

- Grouped RT denoise per-frame state into `Renderer::RTDenoisePassState`:
  - descriptor table arrays and validity;
  - shadow/reflection/GI denoised flags;
  - denoiser execution/pass count;
  - depth/normal, velocity, and disocclusion rejection flags;
  - per-signal accumulation alpha.
- Grouped RT reflection signal resources and metrics into
  `Renderer::RTReflectionSignalStatsState`:
  - raw/history stats buffers;
  - raw/history readback buffers and pending/sample-frame arrays;
  - raw/history UAV descriptors;
  - raw/history resource states;
  - shared stats descriptor tables;
  - raw/history capture flags;
  - raw/history signal metric snapshots.
- Added small state-owned reset methods:
  - `RTDenoisePassState::ResetFrame()`;
  - `RTReflectionSignalStatsState::ResetFrame()`;
  - `RTReflectionSignalStatsState::ResetResources()`.
- Updated the renderer call sites to use the grouped state instead of scattered
  `Renderer` members:
  - frame begin reset;
  - descriptor table allocation/validation;
  - RT denoise dispatch and frame-contract reporting;
  - RT reflection signal capture/readback;
  - RT resource allocation/reset;
  - frame-end denoise fallback checks.

Verified:

- Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.095/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.29 center_luma=74.99
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.201 disocclusion=0.007387 high_motion=0.005249
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The RT reflection signal contract remains stable after moving its resources
  and metrics behind a pass-owned state boundary.
- The RT denoiser contract remains stable after moving descriptor and
  per-frame execution state behind a pass-owned state boundary.
- `Renderer` still owns the state, but the raw member sprawl is reduced and
  the next extraction can target a cleaner `RendererRTState` or dedicated RT
  pass owner without changing behavior.

Next immediate boundary:

- Continue shrinking `Renderer` by grouping the remaining RT target resources:
  - RT shadow mask/history resources;
  - RT reflection color/history target resources;
  - RT GI color/history resources;
  - RT reflection dispatch readiness fields.
- After that, tune reflection composition using the raw/history metrics now
  protected by stable smoke gates.

## Pass 11AI - RT Target Resource Ownership

Completed:

- Grouped remaining RT target resources into explicit renderer-owned structs:
  - `Renderer::RTShadowTargetState`;
  - `Renderer::RTReflectionTargetState`;
  - `Renderer::RTGITargetState`.
- Moved RT shadow mask resources into `RTShadowTargetState`:
  - current mask resource/SRV/UAV/state;
  - history resource/SRV/UAV/state.
- Moved RT reflection target resources into `RTReflectionTargetState`:
  - current reflection color resource/SRV/UAV/state;
  - dispatch/post clear UAV handles;
  - history resource/SRV/UAV/state.
- Moved RT GI target resources into `RTGITargetState`:
  - current GI resource/SRV/UAV/state;
  - history resource/SRV/UAV/state.
- Updated all renderer call sites that consume those resources:
  - RT resource creation;
  - RT frame planning;
  - RT shadow/GI/reflection dispatch;
  - RT denoise dispatch;
  - frame-end fallback history copies;
  - post-process and render-graph imports;
  - visibility-buffer deferred lighting imports;
  - frame-contract resource, memory, history, and pass reporting;
  - diagnostics and debug settings.

Verified:

- Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.916/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.29 center_luma=74.97
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.976 disocclusion=0.007487 high_motion=0.005245
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The RT target resources are no longer scattered as individual `Renderer`
  members. Shadow, reflection, and GI targets now each have an explicit state
  owner.
- The RT denoise and signal-stat contracts survived the state migration with
  unchanged smoke-visible behavior.
- The remaining RT renderer sprawl is now mostly policy/readiness state rather
  than raw resource ownership.

Next immediate boundary:

- Group RT reflection dispatch readiness fields into a small state object:
  - readiness bool/reason;
  - required input booleans;
  - dispatch width/height.
- Then use the raw/history signal fields to tune composition and outlier
  handling without weakening the existing smoke gates.

## Pass 11AJ - RT Reflection Readiness State Ownership

Completed:

- Grouped RT reflection dispatch readiness into
  `Renderer::RTReflectionReadinessState`:
  - `ready`;
  - required input flags for pipeline, TLAS, material buffer, output, depth,
    normal/roughness, material ext2, environment table, frame constants, and
    dispatch descriptors;
  - dispatch width/height;
  - readiness reason.
- Replaced the scattered per-frame readiness fields with the state object in:
  - frame begin reset;
  - RT reflection readiness evaluation;
  - frame-contract history reporting;
  - frame-contract snapshot reporting.
- Added `RTReflectionReadinessState::ResetFrame()` so readiness resets are
  centralized and harder to partially update.

Verified:

- Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.358/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.26 center_luma=74.90
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.26 disocclusion=0.007285 high_motion=0.005251
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- RT reflection state is now split into clearer ownership groups:
  - target resources;
  - denoise state;
  - signal-stat state;
  - dispatch readiness state.
- The frame contract still reports the same RT readiness and signal fields, so
  the public diagnostic surface stayed stable while internal ownership
  improved.
- The immediate RT member-sprawl cleanup from Pass 11AG is complete.

Next immediate boundary:

- Move from RT ownership cleanup to quality work:
  - clamp/account for overbright raw reflection outliers;
  - tune raw-vs-history composition using the measured luma delta;
  - consider extracting the grouped RT structs into a dedicated `RendererRTState`
    header once another adjacent RT cleanup pass lands.

## Pass 11AK - RT Reflection Outlier Composition Guard

Completed:

- Added a luma-aware reflection limiter in `PostProcess.hlsl`:
  - sanitizes non-finite HDR reflection samples before composition;
  - leaves reflection luma unchanged below the outlier knee (`4.0`);
  - uses a soft knee above that point and asymptotically limits composition
    contribution toward `16.0` luma.
- Replaced the older max-channel clamps for SSR and RT reflections with the
  shared luma limiter.
- Applied the limiter at the places that affect final pixels:
  - SSR sample contribution;
  - RT center sample;
  - RT spatial-filter taps;
  - RT spatial-filter resolve;
  - RT reflection history sample;
  - RT/current-history temporal blend.
- Kept the raw/current and denoised-history RT buffers measurable as-is. The
  frame contract still reports raw/history signal quality, so outliers remain
  visible diagnostically instead of being hidden by the composition guard.
- Added RT showcase smoke gates for reflection outlier regressions:
  - `MaxRTReflectionSignalOutlierRatio = 0.02`;
  - `MaxRTReflectionHistorySignalOutlierRatio = 0.02`.

Verified:

- Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.124/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.28 center_luma=74.95
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- RT showcase frame-contract outlier fields from the same run:
  - raw outlier ratio: `0.000931`;
  - history outlier ratio: `0.000481`;
  - raw-vs-history average luma delta: `0.002409`.
- Temporal validation smoke passed:
  `gpu_ms=1.509 disocclusion=0.007406 high_motion=0.005249
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- RT reflections now have:
  - explicit target ownership;
  - explicit denoise/signal-stat ownership;
  - explicit dispatch-readiness ownership;
  - raw/history signal quality contracts;
  - smoke-gated outlier budgets;
  - final-pixel protection against non-finite or runaway HDR reflection samples.
- The current RT showcase keeps the same diagnostic signal shape while final
  composition is less vulnerable to rare overbright reflection hits.

Next immediate boundary:

- Use the now-stable raw/history metrics to tune the reflection temporal blend:
  - make history weight respond to measured luma disagreement;
  - keep glossy/mirror surfaces responsive enough to avoid trails;
  - preserve the RT showcase and temporal validation smoke gates.
- After that, extract the grouped RT state structs into a dedicated renderer RT
  state header to reduce `Renderer.h` weight without changing the public frame
  contract.

## Pass 11AL - RT Reflection Luma-Aware History Blend

Completed:

- Added a shared `ReflectionLuma()` helper in `PostProcess.hlsl` for RT/SSR
  reflection composition decisions.
- Reused that helper inside the soft reflection limiter so the outlier guard
  and temporal history policy use the same luma basis.
- Made RT reflection history blending luma-aware:
  - computes current-vs-history reflection luma delta;
  - suppresses history when relative luma disagreement is high;
  - also suppresses history for large absolute luma disagreement;
  - keeps the existing depth, normal, motion, validity, and max-RGB rejection
    gates intact.
- Kept the frame-contract fields stable. The existing raw/history signal stats
  still capture average luma, max luma, nonzero coverage, outlier ratio, and
  raw-vs-history average luma delta.

Verified:

- Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.003/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.26 center_luma=74.89
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.2 disocclusion=0.007438 high_motion=0.005243
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- RT reflection history accumulation now rejects stale history on surface,
  motion, component delta, and luma disagreement.
- Raw/history signal metrics are now useful not only for smoke diagnostics but
  also for guiding per-pixel composition policy.

Next immediate boundary:

- Extract grouped RT state structs into a dedicated renderer RT state header.
- Keep the extracted state types behavior-neutral and re-run the full smoke
  gates because the change touches shared renderer includes.

## Pass 11AM - Renderer RT State Header Extraction

Completed:

- Added `src/Graphics/RendererRTState.h`.
- Moved grouped RT state structs out of `Renderer.h`:
  - `RTDenoisePassState`;
  - `RTReflectionSignalStatsState`;
  - `RTShadowTargetState`;
  - `RTReflectionTargetState`;
  - `RTGITargetState`;
  - `RTReflectionReadinessState`.
- Left the renderer member names and all call sites unchanged:
  - `m_rtDenoiseState`;
  - `m_rtReflectionSignalState`;
  - `m_rtShadowTargets`;
  - `m_rtReflectionTargets`;
  - `m_rtGITargets`;
  - `m_rtReflectionReadiness`.
- Reduced `Renderer.h` weight without changing the public frame contract,
  resource contracts, pass records, or smoke-visible RT diagnostics.

Verified:

- Full Release rebuild completed successfully after the include move:
  `powershell -NoProfile -ExecutionPolicy Bypass -File rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.919/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.27 center_luma=74.92
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Temporal validation smoke passed:
  `gpu_ms=1.829 disocclusion=0.007701 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The RT subsystem now has a clearer internal boundary:
  - RT resource ownership state lives in `RendererRTState.h`;
  - renderer execution code still owns scheduling and pass orchestration;
  - frame-contract diagnostics remain stable.
- The main renderer header is still large, but the RT resource/readiness/signal
  ownership cluster is no longer embedded inline in the class declaration.

Next immediate boundary:

- Continue shrinking renderer header ownership clusters:
  - temporal validation/readback state;
  - HZB/SSAO/SSR descriptor table state;
  - bloom pass descriptor state.
- Prefer extracting state-only structs first, then move behavior only when
  call-site ownership is obvious and smoke gates remain stable.

## Pass 11AN - Temporal Mask State Header Extraction

Completed:

- Added `src/Graphics/RendererTemporalState.h`.
- Moved temporal rejection mask resource/readback/descriptor ownership into
  `TemporalMaskPassState`:
  - mask texture/SRV/UAV/resource state;
  - stats buffer/UAV/resource state;
  - per-frame stats readbacks, pending flags, and sample frames;
  - temporal mask dispatch descriptor tables;
  - temporal mask stats descriptor tables;
  - per-frame `builtThisFrame` state.
- Replaced scattered renderer members with `m_temporalMaskState` while leaving
  the existing behavior in:
  - `Renderer_TemporalResources.cpp`;
  - `Renderer_TemporalMaskPass.cpp`;
  - `Renderer_TemporalReadback.cpp`;
  - descriptor table allocation/validation;
  - frame-contract resource/pass/history reporting;
  - TAA and RT denoise consumers;
  - RenderGraph temporal-mask import/export.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 124 C++/asset steps and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.188 disocclusion=0.007326 high_motion=0.005247
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.681/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.28 center_luma=74.97
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Temporal mask state is no longer a loose cluster of `Renderer` members.
- Temporal validation, TAA, RT denoise, RenderGraph, and frame-contract paths
  still observe the same behavior and diagnostics.

Next immediate boundary:

- Extract HZB resource/capture state, since it is another concentrated
  renderer-owned state cluster with direct coverage from visibility culling,
  temporal validation, and RT showcase smoke.

## Pass 11AO - HZB State Header Extraction

Completed:

- Added `src/Graphics/RendererHZBState.h`.
- Moved HZB ownership into `HZBPassState`:
  - HZB texture and full-mip SRV;
  - staging mip SRV/UAV handles;
  - per-frame dispatch SRV/UAV descriptor tables;
  - HZB dimensions, mip count, resource state, debug mip, and validity flag;
  - captured camera matrices, camera position/forward, near/far planes,
    capture frame counter, and capture validity.
- Replaced scattered renderer HZB members with `m_hzbResources` in:
  - HZB resource creation and dispatch;
  - HZB RenderGraph import/export;
  - visibility-buffer GPU culling setup;
  - diagnostics/debug settings;
  - frame-contract resource and memory reporting;
  - post-process/HZB debug binding paths.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.689 disocclusion=0.007326 high_motion=0.005246
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.084/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.28 center_luma=74.94
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Renderer-owned state now has clearer pass boundaries for:
  - RT targets/denoise/signal/readiness;
  - temporal rejection mask resources/readback/descriptors;
  - HZB resources, dispatch descriptors, and capture state.
- `Renderer.h` is still large, but the most contract-heavy resource clusters
  have been grouped behind explicit state types.

Next immediate boundary:

- Continue with the remaining screen-space state clusters:
  - SSAO target and descriptor tables;
  - SSR target and descriptor tables;
  - bloom pyramid descriptors and state;
  - TAA history/intermediate/post-process descriptor tables.
- Keep each extraction behavior-neutral and verify with Release rebuild, RT
  showcase, temporal validation, and budget matrix before documenting it.

## Pass 11AP - SSAO State Header Extraction

Completed:

- Added `src/Graphics/RendererSSAOState.h`.
- Moved SSAO ownership into `SSAOPassState`:
  - SSAO texture, RTV, SRV, and UAV handles;
  - current SSAO resource state;
  - per-frame SSAO SRV/UAV descriptor tables;
  - descriptor-table validity tracking.
- Replaced scattered renderer SSAO members with `m_ssaoResources` in:
  - SSAO target creation and descriptor creation;
  - RenderGraph SSAO import/export;
  - SSAO compute and render paths;
  - post-process binding;
  - frame-contract resource and memory reporting;
  - descriptor invalidation and shutdown reset.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.564 disocclusion=0.007451 high_motion=0.005244
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.658/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.29 center_luma=74.99
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Renderer-owned state now has explicit pass-state boundaries for RT,
  temporal mask, HZB, and SSAO resources/descriptors.
- The next scattered screen-space cluster is SSR, followed by bloom and TAA
  history/intermediate/post-process descriptor state.

## Pass 11AQ - SSR State Header Extraction

Completed:

- Added `src/Graphics/RendererSSRState.h`.
- Moved screen-space reflection target ownership into `SSRPassState`:
  - SSR color target, RTV, and SRV;
  - current SSR resource state;
  - per-frame SSR SRV descriptor tables;
  - descriptor-table validity tracking.
- Replaced scattered renderer SSR members with `m_ssrResources` in:
  - HDR/SSR target creation;
  - descriptor table allocation, invalidation, and validation;
  - SSR pass execution;
  - RenderGraph SSR import/export;
  - post-process and end-frame transitions;
  - diagnostics, frame planning, and frame-contract resource/memory reporting.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.278 disocclusion=0.00691 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.054/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.01
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The screen-space reflection target and descriptor tables are no longer loose
  renderer members.
- Remaining high-value screen-space state clusters are bloom pyramid state and
  TAA/history/post-process descriptor state.

## Pass 11AR - Bloom State Header Extraction

Completed:

- Added `src/Graphics/RendererBloomState.h`.
- Moved bloom ownership into `BloomPassState`:
  - bloom pyramid A/B texture resources;
  - persistent and graph-owned RTV/SRV handles;
  - per-ping resource states;
  - active bloom level count;
  - combined bloom SRV;
  - per-frame bloom SRV descriptor tables;
  - post-process bloom override pointer used by RenderGraph transient bloom.
- Replaced scattered renderer bloom members with `m_bloomResources` in:
  - bloom pyramid creation and standalone bloom rendering;
  - fused RenderGraph bloom import/export and transient rebinding;
  - post-process descriptor binding;
  - frame planning and lighting constants;
  - frame-contract resource and memory reporting;
  - descriptor allocation, invalidation, and validation.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=2.063 disocclusion=0.006852 high_motion=0.005223
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.902/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.04
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- RT, temporal mask, HZB, SSAO, SSR, and bloom state are now grouped behind
  explicit state structs instead of loose renderer member clusters.
- The remaining post-process/temporal cleanup target is TAA history,
  TAA intermediate, motion-vector descriptor tables, and post-process
  descriptor tables.

## Pass 11AS - Temporal Screen State Header Extraction

Completed:

- Added `src/Graphics/RendererTemporalScreenState.h`.
- Moved temporal screen/post-process ownership into `TemporalScreenPassState`:
  - velocity/motion-vector render target, RTV, SRV, and resource state;
  - TAA history color target, SRV, and resource state;
  - TAA intermediate target, RTV, and resource state;
  - per-frame TAA resolve SRV descriptor tables;
  - per-frame motion-vector SRV descriptor tables;
  - per-frame post-process SRV descriptor tables.
- Replaced scattered renderer members with `m_temporalScreenState` in:
  - HDR/temporal target creation;
  - motion-vector render and RenderGraph paths;
  - TAA initialization, resolve, copy, and RenderGraph paths;
  - temporal rejection mask and RT denoise velocity consumers;
  - post-process descriptor binding and transitions;
  - frame planning, diagnostics, frame-contract resources, and history reporting;
  - descriptor allocation, invalidation, update, and validation.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.195 disocclusion=0.006861 high_motion=0.005228
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.562/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.01 center_luma=74.06
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The major screen-space resource clusters are now explicit state objects:
  RT, temporal mask, HZB, SSAO, SSR, bloom, and temporal screen/TAA.
- Remaining public-release work is less about raw member extraction and more
  about tightening pass APIs around those state objects, trimming remaining
  unrelated renderer members, and using the raw-vs-history RT metrics to tune
  denoise/composition quality.

## Pass 11AT - Shadow Map State Header Extraction

Completed:

- Added `src/Graphics/RendererShadowState.h`.
- Moved shadow-map ownership into `ShadowMapPassState`:
  - shadow texture-array resource;
  - per-slice DSV handles;
  - shadow SRV;
  - viewport and scissor;
  - current resource state;
  - editor-mode initialization flag.
- Replaced scattered renderer shadow-map members with `m_shadowResources` in:
  - shadow resource creation/recreation;
  - shadow pass rendering;
  - RenderGraph shadow import/export;
  - visibility-buffer shadow imports and deferred lighting binding;
  - environment descriptor table updates;
  - diagnostics, frame planning, and frame-contract resource/memory reporting.
- Kept `m_shadowMapSize` as a renderer quality/budget setting rather than
  folding it into the resource state object.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.28 disocclusion=0.006907 high_motion=0.005236
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.062/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.99 center_luma=73.99
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Shadow-map resources now follow the same explicit-state pattern as the
  screen-space and RT resource clusters.
- Remaining obvious resource clusters still directly owned by `Renderer`
  include depth, HDR color, and g-buffer normal/roughness targets.

## Pass 11AU - Depth Target State Header Extraction

Completed:

- Added `src/Graphics/RendererDepthState.h`.
- Moved primary depth-target ownership into `DepthTargetState`:
  - depth texture resource;
  - writable DSV;
  - read-only DSV;
  - depth SRV;
  - current resource state.
- Replaced scattered renderer depth members with `m_depthResources` in:
  - depth target creation, resize, clearing, and state reset;
  - depth prepass, main pass, overlays, transparency, particles, water, and
    motion vectors;
  - RenderGraph imports/exports and frame begin/end transitions;
  - HZB, SSAO, SSR, RT denoise, RT reflections, RT shadows/GI, TAA, temporal
    mask, visibility buffer, and frame planning;
  - diagnostics, frame-contract resource reporting, and memory tracking.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.276 disocclusion=0.006883 high_motion=0.005232
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.043/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.02
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Depth, shadow, temporal screen, bloom, SSR, SSAO, HZB, temporal mask, and RT
  resources now follow explicit state-object ownership.
- Remaining obvious direct render-target clusters include HDR color and
  g-buffer normal/roughness.

## Pass 11AV - Main Render Target State Header Extraction

Completed:

- Added `src/Graphics/RendererMainTargetState.h`.
- Moved main render-target ownership into `MainRenderTargetState`:
  - HDR color resource, RTV, SRV, and current state;
  - normal/roughness g-buffer resource, RTV, SRV, and current state.
- Replaced scattered renderer HDR and g-buffer members with `m_mainTargets`
  in:
  - HDR/g-buffer resource creation and resize handling;
  - main pass, overlays, transparency, particles, water, skybox, and
    visibility-buffer debug/deferred output paths;
  - TAA, post-process, bloom, SSR, temporal mask, RT denoise, RT reflections,
    and RT resource sizing;
  - RenderGraph imports/exports for end-frame, TAA, SSR, bloom, temporal mask,
    and visibility-buffer paths;
  - diagnostics, frame constants, frame-contract memory/resource reporting, and
    shutdown.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.496 disocclusion=0.006837 high_motion=0.005222
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.933/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.01 center_luma=74.04
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The largest render-target resource clusters are now grouped behind explicit
  pass/state structs rather than directly owned as loose `Renderer` members.
- Remaining cleanup should focus on smaller ownership clusters and API
  tightening: descriptor-table groups, pass-local parameter/state structs,
  fewer direct renderer cross-pass reads, and denoise/composition tuning using
  the verified raw-vs-history RT reflection metrics.

## Pass 11AW - Debug Line State Header Extraction

Completed:

- Added `src/Graphics/RendererDebugLineState.h`.
- Moved debug-line ownership into `DebugLineRenderState`:
  - queued line vertices;
  - reusable upload vertex buffer;
  - current vertex capacity;
  - disabled-after-failure flag.
- Replaced loose renderer debug-line members with `m_debugLineState` in:
  - debug-line enqueue, clear, upload, draw, and failure-disable paths;
  - frame-contract memory accounting;
  - scene/layout reset cleanup.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.189 disocclusion=0.006909 high_motion=0.005236
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.900/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.02
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Debug-line resources now follow the same explicit-state pattern as the
  renderer's main render-target and screen-space/RT resource clusters.
- The next useful cleanup targets are material/texture fallback state,
  environment/IBL descriptor state, particle buffers, and vegetation buffer
  state; each should remain separately verified because they touch asset
  residency and draw binding paths.

## Pass 11AX - Material Fallback Texture State Header Extraction

Completed:

- Added `src/Graphics/RendererMaterialTextureState.h`.
- Moved default material texture ownership into `MaterialFallbackTextureState`:
  - placeholder albedo, normal, metallic, and roughness textures;
  - persistent fallback material descriptor table.
- Replaced loose renderer fallback texture members with `m_materialFallbacks`
  in:
  - placeholder creation, bindless registration, and reserved descriptor copies;
  - material texture loading, fallback assignment, descriptor refresh, and
    bindless index population;
  - depth, forward, indirect, shadow, overlay, transparent, water, and
    visibility-buffer material binding;
  - environment fallback SRVs and visibility-buffer IBL fallback resources;
  - shutdown cleanup.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.19 disocclusion=0.00683 high_motion=0.005219
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.892/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.01
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Default material texture ownership is now explicit and grouped, reducing
  renderer member sprawl in material, draw-binding, and environment fallback
  paths.
- Remaining high-value ownership targets are environment/IBL state,
  particle buffers, vegetation buffers, upload-helper state, and eventually
  pipeline-state grouping.

## Pass 11AY - Environment and IBL State Header Extraction

Completed:

- Added `src/Graphics/RendererEnvironmentState.h`.
- Moved environment/IBL ownership into `EnvironmentLightingState`:
  - shadow/environment descriptor table used by forward/deferred/RT paths;
  - loaded environment maps;
  - pending deferred environment loads;
  - current environment index;
  - IBL residency-limit flag;
  - diffuse/specular IBL intensity;
  - IBL enabled flag.
- Replaced loose renderer environment members with `m_environmentState` in:
  - environment initialization, fallback creation, deferred loading, residency
    trimming, and Dreamer environment registration;
  - shadow/environment descriptor-table allocation and refresh;
  - lighting controls, environment preset selection/cycling, and current-name
    reporting;
  - frame planning, post constants, diagnostics, and frame-contract lighting;
  - forward, indirect, overlay, particles, transparent, water, skybox, SSR,
    TAA, post-process, visibility-buffer deferred lighting, RT resources, RT
    shadows/GI, and RT reflections.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.191 disocclusion=0.00683 high_motion=0.005218
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.561/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.01
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Environment map residency, IBL controls, and the shared shadow/environment
  descriptor table are now grouped behind explicit state.
- Remaining bounded cleanup targets include particle buffer state, vegetation
  buffer state, upload-helper state, voxel experiment state, and larger
  pipeline-state grouping.

## Pass 11AZ - Particle Runtime State Header Extraction

Completed:

- Added `src/Graphics/RendererParticleState.h`.
- Moved particle runtime ownership into `ParticleRenderState`:
  - scene-level particle enable flag;
  - instance-buffer map-failure latch;
  - particle instance upload buffer;
  - particle instance capacity;
  - reusable quad vertex buffer.
- Replaced loose renderer particle members with `m_particleState` in:
  - particle feature toggles and diagnostics;
  - frame planning feature inputs;
  - particle instance collection/upload;
  - quad vertex-buffer creation/upload;
  - map-failure disable path and draw binding.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.254 disocclusion=0.00686 high_motion=0.005225
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.923/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.99 center_luma=74.00
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Particle runtime resources are now grouped behind explicit state.
- Remaining bounded cleanup targets include vegetation buffer state,
  upload-helper state, voxel experiment state, and larger pipeline-state
  grouping.

## Pass 11BA - Vegetation Runtime State Header Extraction

Completed:

- Added `src/Graphics/RendererVegetationState.h`.
- Moved vegetation runtime ownership into `VegetationRenderState`:
  - mesh vegetation instance upload buffer, SRV, capacity, and count;
  - billboard instance upload buffer, SRV, capacity, and count;
  - grass-card instance upload buffer, SRV, capacity, and count;
  - scene-level vegetation enable flag.
- Kept vegetation pipelines, constant buffer, atlas texture, wind parameters,
  and vegetation stats in `Renderer` for now; those are adjacent but not part
  of the same low-risk resource-count ownership move.
- Replaced loose renderer vegetation members with `m_vegetationState` in:
  - vegetation feature toggles and diagnostics;
  - mesh, billboard, and grass instance-buffer creation;
  - instance collection/upload;
  - placeholder vegetation draw guards;
  - vegetation shadow draw guard.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.277 disocclusion=0.006882 high_motion=0.005231
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.047/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.99 center_luma=73.99
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Vegetation instance-buffer resources are now grouped behind explicit state.
- Remaining bounded cleanup targets include upload-helper state, voxel
  experiment state, vegetation pipeline/atlas/wind grouping, and larger
  pipeline-state grouping.

## Pass 11BB - Upload Command Pool State Header Extraction

Completed:

- Added `src/Graphics/RendererUploadState.h`.
- Moved copy-queue upload command-pool ownership into
  `UploadCommandPoolState`:
  - fixed-size upload command allocator pool;
  - fixed-size upload command-list pool;
  - round-robin allocator index;
  - per-slot copy-queue fence values;
  - pending upload fence bookkeeping.
- Kept the upload queue object itself in `Renderer`; this pass only grouped
  the reusable command-pool resources that support mesh uploads.
- Replaced loose renderer upload-pool members with `m_uploadCommands` in:
  - renderer initialization command allocator/list creation;
  - per-frame upload fence wait/reset;
  - mesh upload copy-list reuse, execution, and fence tracking.
- Left deferred texture-upload queue/receipt ownership unchanged for a later
  pass, since it is a separate diagnostic/admission queue rather than the
  low-level copy command-pool state.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.277 disocclusion=0.006877 high_motion=0.00523
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.041/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.99 center_luma=73.99
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- RT showcase texture-upload diagnostics stayed healthy:
  `submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Copy-queue upload command-pool resources are now grouped behind explicit
  state.
- Remaining bounded cleanup targets include deferred texture-upload
  queue/receipt state, voxel experiment state, vegetation pipeline/atlas/wind
  grouping, and larger pipeline-state grouping.

## Pass 11BC - Texture Upload Queue State Header Extraction

Completed:

- Added `src/Graphics/RendererTextureUploadState.h`.
- Moved deferred texture-upload runtime ownership into
  `TextureUploadRuntimeState`:
  - bounded upload receipt history;
  - pending upload job queue;
  - pending/failed upload key sets;
  - upload queue statistics;
  - next job id;
  - max jobs per frame.
- Centralized receipt retention in `TextureUploadRuntimeState::StoreReceipt`
  while keeping the existing 512-receipt cap.
- Replaced loose renderer texture-upload members with
  `m_textureUploadState` in:
  - public upload receipt/stat diagnostics;
  - end-of-run diagnostics logging;
  - frame planning job budget;
  - pending/failed texture upload checks;
  - queued texture upload creation and processing;
  - upload result statistics;
  - texture publication receipt storage.
- Kept the copy-queue command pool and command queue object separate; those
  remain low-level upload infrastructure, while this pass owns queue/admission
  diagnostics and deferred publication bookkeeping.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.285 disocclusion=0.006906 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation texture-upload diagnostics stayed valid:
  `receipts=1 submitted=1 completed=1 failed=0 pending=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.671/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.04
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- RT showcase texture-upload diagnostics stayed healthy:
  `receipts=11 submitted=11 completed=11 failed=0 pending=0 uploaded=40.00MB`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- Deferred texture-upload queue and receipt resources are now grouped behind
  explicit state.
- Remaining bounded cleanup targets include voxel experiment state,
  vegetation pipeline/atlas/wind grouping, renderer diagnostic timing/state
  grouping, and larger pipeline-state grouping.

## Pass 11BD - Voxel Runtime State Header Extraction

Completed:

- Added `src/Graphics/RendererVoxelState.h`.
- Moved experimental voxel runtime ownership into `VoxelRenderState`:
  - voxel backend enabled flag;
  - dense CPU voxel grid;
  - voxel grid dimension;
  - voxel grid dirty flag;
  - voxel grid upload buffer;
  - voxel grid SRV;
  - material-name to compact voxel material id map;
  - next material id.
- Added small voxel-state helpers for grid reset and material-palette reset.
- Kept the voxel pipeline object itself in `Renderer`; this pass only groups
  runtime voxel data and upload resources.
- Replaced loose renderer voxel members with `m_voxelState` in:
  - voxel backend controls;
  - frame planning feature inputs;
  - frame-contract memory accounting;
  - voxel grid dirty marking;
  - voxel grid build/voxelization;
  - voxel material id assignment;
  - voxel grid upload buffer/SRV creation;
  - voxel fullscreen draw SRV binding.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.279 disocclusion=0.00688 high_motion=0.005231
  object_motion=0.0731 visible=7 warnings=0`.
- Temporal validation startup still created the voxel renderer pipeline:
  `Voxel renderer pipeline created successfully (rtvFormat=R8G8B8A8_UNORM)`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.062/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.01
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Verification caveat:

- The standard smoke gates validate that voxel pipeline creation and normal
  renderer/RT paths still work, but they do not actively switch the renderer
  into the experimental voxel backend. There is currently no noninteractive
  voxel backend smoke script or CLI backend selector, so active voxel rendering
  remains compile-verified but not smoke-verified in automation.

Current renderer-refactor state:

- Experimental voxel runtime resources are now grouped behind explicit state.
- Remaining bounded cleanup targets include a noninteractive voxel smoke gate,
  vegetation pipeline/atlas/wind grouping, renderer diagnostic timing/state
  grouping, and larger pipeline-state grouping.

## Pass 11BE - Voxel Backend CLI Smoke Gate

Completed:

- Added noninteractive backend selection through:
  - `--backend <raster|voxel>`;
  - `--backend=<raster|voxel>`;
  - `--render-backend <raster|voxel>`;
  - `--render-backend=<raster|voxel>`.
- Made backend parsing case-insensitive for the canonical backend names and
  emit a warning for unknown backend strings.
- Kept backend flags out of the launcher path, so automation can select a
  backend without opening the launcher.
- Extended explicit visual-validation reporting so captures requested through
  `CORTEX_CAPTURE_VISUAL_VALIDATION=1` are analyzed for non-showcase scenes
  too. This lets backend-specific smoke tests validate cheaper scenes instead
  of requiring the heavy RT showcase.
- Tightened frame-contract feature reporting for special frame paths:
  - voxel frames now report `executed_features.voxel_backend_enabled=true`;
  - raster/RT/post features intentionally bypassed by the voxel path now report
    as not executed instead of producing misleading shadow/RT warnings.
- Added a `RenderVoxel` frame-contract pass record with `frame_constants` and
  `voxel_grid` reads and `back_buffer` write.
- Added `tools/run_voxel_backend_smoke.ps1`.
- The voxel smoke now verifies:
  - temporal validation scene loaded;
  - voxel backend selected and executed;
  - frame contract feature/planned/executed voxel flags;
  - RT did not execute in the voxel special path;
  - visible renderable classification;
  - `RenderVoxel` pass record;
  - visual capture and image statistics;
  - run-log evidence for backend selection, active voxel rendering, and voxel
    grid construction.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt the changed translation units and linked `CortexEngine.exe`.
- New voxel backend smoke passed:
  `gpu_ms=15.457 visible=7 avg_luma=116.9 nonblack=1`.
- Temporal validation smoke passed:
  `gpu_ms=1.281 disocclusion=0.006911 high_motion=0.005239
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.052/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=70.99 center_luma=74.00
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.

Current renderer-refactor state:

- The experimental voxel backend is now active-smoke-verified in automation,
  not just compile-verified through normal raster/RT smoke tests.
- The denoised reflection history signal path remains verified across the RT
  showcase and budget profile matrix after the descriptor-table reuse fix.
- Remaining bounded cleanup targets include vegetation pipeline/atlas/wind
  grouping, renderer diagnostic timing/state grouping, larger pipeline-state
  grouping, and continued material/lighting correctness tuning.

## Pass 11BF - Vegetation Runtime State Completion

Completed:

- Extended `src/Graphics/RendererVegetationState.h` so
  `VegetationRenderState` now owns the full vegetation runtime surface:
  - mesh vegetation pipeline;
  - mesh vegetation shadow pipeline;
  - billboard pipeline;
  - grass-card pipeline;
  - vegetation constant buffer;
  - mesh, billboard, and grass instance-buffer states;
  - vegetation atlas texture;
  - wind parameters;
  - vegetation statistics;
  - scene-level vegetation enable flag.
- Removed the remaining loose vegetation pipeline, atlas, wind, constant-buffer,
  and stats members from `Renderer`.
- Replaced vegetation access sites with `m_vegetationState` in:
  - wind parameter getters/setters;
  - vegetation stats getter;
  - vegetation constant-buffer update;
  - mesh instance stats update;
  - billboard stats update;
  - atlas loading;
  - per-frame wind time update;
  - mesh, billboard, grass, and shadow render guards.
- Kept vegetation draw behavior unchanged. The current vegetation pipelines are
  still placeholder/no-op paths when unset; this pass only completes ownership
  grouping so future vegetation rendering work has one state boundary.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.277 disocclusion=0.006791 high_motion=0.005214
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.050/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.00 center_luma=74.01
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.
- Voxel backend smoke passed:
  `gpu_ms=15.362 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- Vegetation runtime ownership is now fully grouped behind explicit state.
- The active release gate set now includes Release rebuild, temporal smoke, RT
  showcase smoke, budget profile matrix, and voxel backend smoke.
- Remaining bounded cleanup targets include renderer diagnostic timing/state
  grouping, larger pipeline-state grouping, and continued material/lighting
  correctness tuning.

## Pass 11BG - Renderer Frame Runtime And Timing State

Completed:

- Added explicit renderer runtime/timing structs in
  `src/Graphics/Renderer_DiagnosticsTypes.h`:
  - `RendererFrameRuntimeState`;
  - `RendererPassTimingState`.
- Moved frame runtime ownership out of loose `Renderer` members:
  - total renderer time;
  - triple-buffered graphics fence values.
- Moved CPU pass timing ownership out of loose `Renderer` members:
  - depth prepass;
  - shadow pass;
  - main pass;
  - RT pass;
  - SSR;
  - SSAO;
  - bloom;
  - post-process.
- Replaced all old loose timing/runtime member references in:
  - frame begin fence waits;
  - frame end fence signaling;
  - frame planning total-time update;
  - frame lighting constants;
  - vegetation constants;
  - surface animation time;
  - render orchestration timing;
  - prepare/post phase timing;
  - synchronization waits;
  - diagnostics getters and logs.
- Kept public timing getters and diagnostics output behavior unchanged.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.281 disocclusion=0.006913 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.044/16.7 dxgi_mb=403.96/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.01 center_luma=74.04
  dark=0.658/0.68 sat=0.006/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0784/1.0000/16.0000/0.0009
  rt_hist=0.0808/1.0000/7.9102/0.0024 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0808/1.0000/7.9102/0.0024`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0784/1.0000/16.0000/0.0009`,
    `rt_hist=0.0809/1.0000/7.9219/0.0025`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0834/1.0000/0.2617/0.0000`,
    `rt_hist=0.0834/1.0000/0.2500/0.0000`.
- Voxel backend smoke passed:
  `gpu_ms=16.771 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- Frame runtime and pass timing ownership are now grouped behind explicit
  diagnostic/runtime state.
- The active release gate set remains green after the latest renderer member
  grouping.
- Remaining bounded cleanup targets include larger pipeline-state grouping and
  continued material/lighting correctness tuning.

## Pass 11BH - Renderer Pipeline State Ownership And Runtime DXIL Gate

Completed:

- Added `src/Graphics/RendererPipelineState.h` as the renderer-owned root
  signature and pipeline-state boundary.
- Moved the remaining loose renderer root-signature and pipeline members into
  `RendererPipelineState`:
  - graphics and compute root signatures;
  - compact 1-SRV/1-UAV compute root signature;
  - geometry, transparent, overlay, depth, shadow, post, TAA, SSR, SSAO, HZB,
    bloom, sky, debug-line, water, particle, and voxel pipelines.
- Replaced renderer call sites with the grouped pipeline state in:
  - pipeline/root-signature setup;
  - frame feature planning;
  - frame-contract validation;
  - forward/depth/shadow/transparent/overlay/water/particle/voxel passes;
  - screen-space passes and render-graph adapters;
  - shutdown.
- Added a state-owned `RendererPipelineState::Reset()` so shutdown releases the
  complete pipeline/root-signature surface through one explicit boundary.
- Fixed a runtime DXIL validation issue found by running smoke gates from the
  actual `build/bin` asset layout: regenerated `RaytracedReflections.dxil`
  uses the current larger reflection payload, so the DXR reflection shader
  config now reserves 96 bytes instead of the stale 64-byte limit.
- Hardened the temporal, RT showcase, and voxel smoke scripts so each launch
  runs `CortexEngine.exe` from its executable directory. The gates now resolve
  runtime assets from `build/bin/assets` regardless of the caller's current
  shell directory.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt the renderer pipeline-state users and then the DXR reflection
  pipeline object change.
- Temporal validation smoke passed from the repo directory:
  `gpu_ms=1.26 disocclusion=0.006857 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed from the repo directory:
  `frames=33 gpu_ms=1.988/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  material_issues=0 resolved_mat=7/4/1 preset_defaults=0/0/4/0
  reflection=12/8/0.88/0.56 rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced`.
- Budget profile matrix passed from the repo directory:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed from the repo directory:
  `gpu_ms=15.362 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- Renderer root signatures and first-party pipeline objects are now grouped
  behind an explicit pipeline-state boundary instead of loose `Renderer`
  members.
- The active release gate set is reproducible from the repo directory and uses
  the runtime asset layout under `build/bin`.
- The current runtime-generated DXR reflection library is now covered by the
  RT showcase and budget profile gates.
- Remaining public-release work is now mostly material/lighting quality
  tuning, visual-demo polish, and any further pass API tightening that can be
  done without destabilizing the green gate set.

## Pass 11BI - Pipeline Readiness Snapshot

Completed:

- Added `RendererPipelineReadiness` to `src/Graphics/RendererPipelineState.h`.
- Added `RendererPipelineState::GetReadiness()` so callers can consume a stable
  pipeline readiness snapshot instead of repeatedly peeking individual COM
  pointers.
- Moved frame feature planning onto the readiness snapshot for:
  - shadows;
  - TAA;
  - SSR;
  - SSAO graphics and compute paths;
  - bloom downsample;
  - voxel backend;
  - motion vectors;
  - post-process.
- Moved frame-contract validation checks onto the same readiness snapshot for:
  - depth-only pipeline variants;
  - transparent, water-overlay, and overlay pipelines.
- Confirmed the planning and validation bridge files no longer directly test
  the targeted pipeline members for readiness.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.258 disocclusion=0.006916 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.014/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.005/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=18.44 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- Pipeline ownership now has both a storage boundary and a read-only readiness
  API for frame planning and frame-contract validation.
- The active release gate set remains green after tightening pipeline readiness
  access.
- Remaining public-release work is still concentrated in material/lighting
  quality, visual-demo polish, and additional pass API tightening where it
  reduces direct renderer state coupling without expanding risk.

## Pass 11BJ - RenderGraph Transition Override State

Completed:

- Added `RendererRenderGraphTransitionState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped the legacy-pass RenderGraph transition override flags into that
  state object:
  - depth prepass;
  - shadow pass;
  - SSR;
  - SSAO;
  - bloom;
  - post-process.
- Removed the six loose transition override booleans from `Renderer`.
- Replaced all transition-override call sites with
  `m_renderGraphTransitions` in:
  - depth prepass;
  - shadow pass;
  - SSR and SSAO legacy passes;
  - bloom, depth/shadow, SSR, SSAO, and end-frame RenderGraph adapters;
  - post-process.
- Kept behavior unchanged. The state still only tells legacy pass code whether
  the RenderGraph adapter has already handled resource transitions.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.271 disocclusion=0.006874 high_motion=0.005229
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.016/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.87
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.006/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.512 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- RenderGraph transition override state is no longer scattered as loose
  renderer members.
- Pipeline ownership/readiness and RenderGraph transition overrides now both
  have explicit state boundaries.
- The active release gate set remains green.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BK - RenderGraph Runtime State Boundary

Completed:

- Added `RendererRenderGraphRuntimeState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped RenderGraph frame diagnostics, optional transient-validation latch,
  and transition override state behind one renderer member:
  - `FrameContract::RenderGraphInfo info`;
  - `transientValidationRan`;
  - `RendererRenderGraphTransitionState transitions`.
- Replaced the old loose RenderGraph runtime members:
  - `m_frameRenderGraphInfo`;
  - `m_rgTransientValidationRan`;
  - `m_renderGraphTransitions`.
- Updated all RenderGraph diagnostics, fallback accounting, frame-contract
  reporting, and transition override call sites to use
  `m_renderGraphState`.
- Kept behavior unchanged; this pass only tightens ownership around existing
  RenderGraph runtime state.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke with `CORTEX_RG_TRANSIENT_VALIDATE=1` passed:
  `gpu_ms=1.263 disocclusion=0.006906 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0`.
- Verification caveat: the transient-validation environment run completed
  cleanly, but the saved final frame report did not expose a distinct
  `RGTransientValidation` pass record. Treat this as runtime smoke coverage of
  the path and latch, not as a separate final-report contract guarantee.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.827/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.039/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.353 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- RenderGraph runtime state now has a single owner for per-frame diagnostics,
  fallback counts, transient-validation latch state, and transition overrides.
- The active release gate set remains green after the additional RenderGraph
  state boundary.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BL - Debug Overlay State Boundary

Completed:

- Added `RendererDebugOverlayState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped the GPU debug overlay runtime fields:
  - overlay visibility;
  - selected settings row.
- Replaced the loose `Renderer` members with `m_debugOverlayState`.
- Updated the overlay setter path and post-process frame constants to read from
  the grouped state.
- Kept behavior unchanged. The pass only moves the data behind a named owner.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.26 disocclusion=0.0069 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.988/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.87
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.012/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.12 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- Debug overlay runtime state is no longer represented by loose renderer
  members.
- The active release gate set remains green after the overlay state boundary.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BM - Water Runtime State Boundary

Completed:

- Promoted the existing `RendererWaterState` into the single owner for runtime
  water simulation/settings data.
- Moved the loose water members behind `m_waterState`:
  - water level;
  - wave amplitude;
  - wave length;
  - wave speed;
  - primary wave direction;
  - secondary amplitude;
  - steepness.
- Updated water settings, water-height sampling, diagnostics, and post-process
  frame constants to use `m_waterState`.
- Preserved previous defaults and behavior:
  - `levelY = 0.0`;
  - `waveAmplitude = 0.2`;
  - `waveLength = 8.0`;
  - `waveSpeed = 1.0`;
  - `secondaryAmplitude = 0.1`;
  - `steepness = 0.6`;
  - `primaryDirection = (1.0, 0.0)`.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`.
- Temporal validation smoke passed:
  `gpu_ms=1.267 disocclusion=0.006872 high_motion=0.005228
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.001/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.037/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=18.488 visible=7 avg_luma=116.9 nonblack=1`.

Current renderer-refactor state:

- Water runtime state is no longer scattered across loose `Renderer` members.
- The active release gate set remains green after the water state boundary.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BN - Fractal Surface State Boundary

Completed:

- Added `RendererFractalSurfaceState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped the global shader-facing fractal surface parameters:
  - amplitude;
  - frequency;
  - octaves;
  - coordinate mode;
  - X/Z scale;
  - lacunarity;
  - gain;
  - warp strength;
  - noise type.
- Replaced the loose `m_fractal*` renderer members with
  `m_fractalSurfaceState`.
- Updated `SetFractalParams` and all material constant packing paths:
  - forward pass;
  - indirect rendering;
  - transparent geometry.
- Preserved previous clamps, defaults, shader packing, and logging behavior.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.258 disocclusion=0.006949 high_motion=0.005246
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.008/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.013/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.412 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old `m_fractal*` member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Fractal material modulation is now owned by a named state object instead of
  ten loose renderer members.
- The active release gate set remains green after another material-facing
  state boundary.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BO - Post-Process Grade State Boundary

Completed:

- Added `RendererPostProcessGradeState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped post-process grade controls:
  - warm grade;
  - cool grade;
  - god-ray intensity.
- Replaced the loose `m_colorGradeWarm`, `m_colorGradeCool`, and
  `m_godRayIntensity` renderer members with `m_postProcessGradeState`.
- Updated UI/control-facing setters and all reporting/packing paths:
  - `SetColorGrade`;
  - `SetGodRayIntensity`;
  - feature-state diagnostics;
  - frame-contract lighting snapshot;
  - post-process frame constants.
- Preserved previous clamps, defaults, shader constant packing, and logging
  behavior.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.263 disocclusion=0.006913 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.881/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.90
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.028/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.227 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old color-grade or god-ray member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Post-process grade runtime data is now owned by a named state object instead
  of loose renderer members.
- The active release gate set remains green after another shader-constant
  ownership cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BP - SSAO Pass State Boundary

Completed:

- Moved SSAO runtime controls into `SSAOPassState` in
  `src/Graphics/RendererSSAOState.h`.
- Grouped:
  - SSAO enabled state;
  - radius;
  - bias;
  - intensity.
- Replaced the loose `m_ssaoEnabled`, `m_ssaoRadius`, `m_ssaoBias`, and
  `m_ssaoIntensity` renderer members with fields on `m_ssaoResources`.
- Updated all SSAO control, planning, diagnostics, render-graph, frame-contract,
  and post-process constant paths.
- Preserved previous defaults, clamps, public setter/getter API, and shader
  constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.267 disocclusion=0.006875 high_motion=0.00523
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.990/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.87
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.001/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.024 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old SSAO loose member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- SSAO feature state and tuning parameters now live with the SSAO pass state
  instead of as loose renderer members.
- The active release gate set remains green after another pass-owned resource
  and control boundary.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BQ - Bloom Pass State Boundary

Completed:

- Moved Bloom runtime controls into `BloomPassState` in
  `src/Graphics/RendererBloomState.h`.
- Grouped:
  - intensity;
  - threshold;
  - soft-knee;
  - max contribution.
- Replaced the loose `m_bloomIntensity`, `m_bloomThreshold`,
  `m_bloomSoftKnee`, and `m_bloomMaxContribution` renderer members with fields
  on `m_bloomResources`.
- Updated Bloom control, scheduling, diagnostics, descriptor planning,
  frame-contract, frame lighting/post constants, render-graph, initialization,
  and quality-setting paths.
- Preserved previous defaults, clamps, public setter/getter API, descriptor
  behavior, and shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.739 disocclusion=0.006923 high_motion=0.00524
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.130/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.41 center_luma=74.84
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.034/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.515 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old Bloom loose member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Bloom feature state and tuning parameters now live with the Bloom pass state
  instead of as loose renderer members.
- The active release gate set remains green after another post-process
  ownership cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BR - Fog Runtime State Boundary

Completed:

- Added `RendererFogState` in `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped exponential height fog runtime controls:
  - enabled state;
  - density;
  - height;
  - falloff.
- Replaced the loose `m_fogEnabled`, `m_fogDensity`, `m_fogHeight`, and
  `m_fogFalloff` renderer members with `m_fogState`.
- Updated fog setters/getters, feature diagnostics, shutdown diagnostics,
  frame planning, frame-contract lighting snapshot, and post-process constant
  packing.
- Preserved previous defaults, clamps, public setter/getter API, logging, and
  shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.2 disocclusion=0.006538 high_motion=0.005197
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.119/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.021/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=17.532 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old fog loose member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Fog feature state and post-process constant inputs now live in a named state
  object instead of loose renderer members.
- The active release gate set remains green after another frame-feature
  ownership cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BS - Lighting Runtime State Boundary

Completed:

- Added `RendererLightingState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped runtime lighting controls:
  - directional light direction;
  - directional light color;
  - directional light intensity;
  - ambient light color;
  - ambient light intensity;
  - low-VRAM safe lighting rig toggle;
  - rectangular area light size scale.
- Replaced the loose `m_directionalLightDirection`, `m_directionalLightColor`,
  `m_directionalLightIntensity`, `m_ambientLightColor`,
  `m_ambientLightIntensity`, `m_useSafeLightingRigOnLowVRAM`, and
  `m_areaLightSizeScale` renderer members with `m_lightingState`.
- Updated lighting rig presets, sun controls, diagnostics, frame-contract
  lighting snapshot, forward lighting constant packing, and visibility-buffer
  deferred lighting constants.
- Fixed a correctness gap while moving ownership: rectangular area light size
  scale is now applied when packing area-light half-size for the shader instead
  of only being exposed through UI/diagnostics.
- Preserved previous defaults, clamps, public setter/getter API, and logging.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.191 disocclusion=0.005932 high_motion=0.005068
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.488/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.44 center_luma=74.93
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.263/2.5 changed=0.003/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=17.814 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose lighting member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Runtime lighting controls now live in a named state object instead of as
  loose renderer members.
- Area light size control now has shader-visible behavior through the forward
  light constant path.
- The active release gate set remains green after a material/lighting-facing
  ownership cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature and
  quality settings.

## Pass 11BT - Shadow Pass State Boundary

Completed:

- Extended `ShadowMapPassState` in `src/Graphics/RendererShadowState.h` with
  shadow runtime controls.
- Grouped:
  - shadow enabled state;
  - shadow map size;
  - depth bias;
  - PCF radius;
  - cascade split lambda;
  - per-cascade resolution scale.
- Replaced the loose `m_shadowsEnabled`, `m_shadowMapSize`, `m_shadowBias`,
  `m_shadowPCFRadius`, `m_cascadeSplitLambda`, and
  `m_cascadeResolutionScale` renderer members with fields on
  `m_shadowResources`.
- Updated shadow settings, budget initialization, shadow resource allocation,
  quality fallback, diagnostics, frame planning, frame-contract resource and
  lighting snapshots, forward lighting constants, and visibility-buffer
  deferred lighting constants.
- Preserved previous defaults, clamps, public setter/getter API, budget
  profile behavior, and shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.826 disocclusion=0.006525 high_motion=0.005195
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.013/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.91
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.095/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.183 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose shadow-control member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Shadow resource ownership and shadow quality controls now live together in
  the shadow pass state instead of being split across loose renderer members.
- Forward and visibility-buffer lighting now consume shadow controls from the
  same pass-owned state object.
- The active release gate set remains green after another lighting-facing pass
  ownership cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer feature,
  debug, and quality settings.

## Pass 11BU - SSR Pass State Boundary

Completed:

- Extended `SSRPassState` in `src/Graphics/RendererSSRState.h` with SSR
  runtime controls.
- Grouped:
  - SSR enabled state;
  - SSR active-this-frame state.
- Replaced the loose `m_ssrEnabled` and `m_ssrActiveThisFrame` renderer
  members with fields on `m_ssrResources`.
- Updated SSR feature toggles, screen-space effect cycling, diagnostics, frame
  planning, quality fallback, render orchestration, and post-process constant
  packing.
- Preserved previous defaults, public setter/getter API, frame feature
  planning behavior, and shader toggle packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.162 disocclusion=0.006836 high_motion=0.005221
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.469/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.89
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.119/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.382 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose SSR member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- SSR resources, descriptor tables, feature enablement, and per-frame activity
  now live together in the SSR pass state.
- The active release gate set remains green after another screen-space pass
  ownership cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer debug,
  anti-aliasing, ray-tracing, and quality settings.

## Pass 11BV - PCSS Shadow Filter State Boundary

Completed:

- Extended `ShadowMapPassState` in `src/Graphics/RendererShadowState.h` with
  the PCSS soft-shadow toggle.
- Replaced the loose `m_pcssEnabled` renderer member with
  `m_shadowResources.pcssEnabled`.
- Updated feature state reporting, the public PCSS setter, forward lighting
  shadow constants, and visibility-buffer deferred lighting shadow constants.
- Preserved the existing default, public control API, debug menu behavior, and
  shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.263 disocclusion=0.006917 high_motion=0.005239
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.000/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.029/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.521 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose PCSS member name remains;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Shadow map resources, shadow quality controls, and the PCSS filtering toggle
  now live under the same shadow pass state.
- Forward and visibility-buffer lighting now consume all shadow-filter toggles
  from `m_shadowResources`, which closes another small cross-cutting renderer
  member leak.
- The active release gate set remains green after the PCSS state boundary
  cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer debug,
  anti-aliasing, ray-tracing, and quality settings.

## Pass 11BW - Post-Process Feature State Boundary

Completed:

- Renamed the grade-only post-process state to `RendererPostProcessState`.
- Added the FXAA enable toggle to the post-process state.
- Replaced the loose `m_fxaaEnabled` renderer member with
  `m_postProcessState.fxaaEnabled`.
- Updated feature diagnostics, public FXAA setter, frame feature planning,
  post-process constant packing, frame-contract snapshotting, and color-grade
  controls to use `m_postProcessState`.
- Preserved previous defaults, public setter/getter API, scene setup behavior,
  and shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.266 disocclusion=0.006947 high_motion=0.005245
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.010/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.027/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.575 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose FXAA member or post-process grade-state names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Post-process feature toggles and color-grade/god-ray controls now live in a
  single post-process runtime state instead of splitting FXAA into a loose
  renderer member.
- Frame planning and post constants now read FXAA from the same state object
  that owns the rest of post-process presentation controls.
- The active release gate set remains green after the post-process state
  boundary cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer debug,
  temporal, ray-tracing, and GPU-culling settings.

## Pass 11BX - Debug View State Boundary

Completed:

- Added `RendererDebugViewState` beside the existing debug-line state.
- Replaced the loose `m_debugViewMode` renderer member with
  `m_debugViewState.mode`.
- Updated debug view cycling, explicit debug-view setting, quality-state
  reporting, HZB debug resource selection, post-process constants,
  post-process debug routing, render-graph HZB debug routing, RT reflection
  debug gating, and visibility-buffer debug routing.
- Preserved previous debug mode defaults, public getter/setter API, maximum
  debug mode clamp, debug labels, and shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.26 disocclusion=0.006873 high_motion=0.00523
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.993/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.018/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.784 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose debug-view member name remains;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Debug-line resources and debug-view mode now sit under explicit debug
  renderer state instead of leaving presentation/debug controls as loose
  renderer members.
- The RT showcase surface-class debug validation still reaches debug view 41,
  so the debug mode control path remains intact after the state move.
- The active release gate set remains green after another broad
  presentation/debug routing cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of temporal,
  ray-tracing, GPU-culling, and camera/frame runtime settings.

## Pass 11BY - RT Runtime State Boundary

Completed:

- Added `RTRuntimeState` in `src/Graphics/RendererRTState.h`.
- Grouped:
  - DXR support state;
  - global RT enabled state;
  - RT reflections enabled state;
  - RT GI enabled state.
- Replaced the loose `m_rayTracingSupported`, `m_rayTracingEnabled`,
  `m_rtReflectionsEnabled`, and `m_rtGIEnabled` renderer members with fields
  on `m_rtRuntimeState`.
- Updated RT feature toggles, diagnostics, frame planning, frame-contract
  memory/snapshot/history reporting, initialization, mesh upload, render-scale
  quality heuristics, RT frame planning, RT reflection readiness, post-process
  RT flags, frame-end denoise copies, and main-frame reflection execution.
- Preserved previous defaults, public setter/getter API, unsupported-DXR guard,
  RT history invalidation behavior, budget-profile scheduling, and shader
  constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.265 disocclusion=0.006903 high_motion=0.005233
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.008/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.012/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.143 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose RT runtime member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- RT runtime intent now lives in `m_rtRuntimeState`, while RT denoise,
  reflection signal stats, target resources, and readiness already live in
  RT-specific state structs.
- The budget matrix still preserves the important low-memory behavior:
  `4gb_low` skips RT reflection scheduling, while default and ultra-low
  profiles still report valid reflection readiness/signal as expected.
- The active release gate set remains green after moving a broad RT control
  cluster out of loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of temporal,
  GPU-culling, camera/frame runtime, and quality settings.

## Pass 11BZ - Temporal AA Runtime State Boundary

Completed:

- Added `TemporalAAState` in `src/Graphics/RendererTemporalScreenState.h`.
- Grouped:
  - TAA enabled state;
  - TAA blend factor;
  - camera-moving status used by jitter selection;
  - previous/current jitter pixels;
  - TAA sample index;
  - previous view-projection state and validity.
- Replaced loose renderer members:
  - `m_taaEnabled`;
  - `m_taaBlendFactor`;
  - `m_cameraIsMoving`;
  - `m_taaJitterPrevPixels`;
  - `m_taaJitterCurrPixels`;
  - `m_taaSampleIndex`;
  - `m_prevViewProjMatrix`;
  - `m_hasPrevViewProj`.
- Updated diagnostics, feature settings, frame constants, frame contract
  history reporting, frame planning, post constants, temporal constants,
  TAA RenderGraph pass setup, render orchestration reset logic, and TAA
  execution.
- Preserved previous TAA defaults, jitter behavior, no-jitter environment
  override, temporal-history invalidation, public setter/getter API, and
  shader constant packing.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.266 disocclusion=0.006923 high_motion=0.00524
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.999/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.41 center_luma=74.83
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.032/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.833`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.443`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.185`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.643 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose TAA runtime member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- TAA runtime control now lives in `m_temporalAAState`, while TAA/history
  textures and descriptor tables remain in `m_temporalScreenState`.
- Temporal validation, RT showcase, budget-profile, and voxel smoke gates all
  remain green after moving frame-wide temporal AA state out of loose renderer
  members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of GPU-culling,
  camera/frame runtime, local-shadow, and quality settings.

## Pass 11CA - GPU Culling Runtime State Boundary

Completed:

- Added `GpuCullingRuntimeState` in `src/Graphics/RendererGPUCullingState.h`.
- Grouped:
  - GPU culling enabled state;
  - indirect draw enabled state;
  - debug freeze state and freeze-captured camera data;
  - per-frame GPU instance cache;
  - per-frame indirect mesh-info cache;
  - GPU-culling HZB-occlusion usage flag.
- Replaced loose renderer members:
  - `m_gpuCullingEnabled`;
  - `m_indirectDrawEnabled`;
  - `m_gpuCullingFreeze`;
  - `m_gpuCullingFreezeCaptured`;
  - `m_gpuCullingFrozenViewProj`;
  - `m_gpuCullingFrozenCameraPos`;
  - `m_gpuInstances`;
  - `m_meshInfos`;
  - `m_gpuCullingHzbOcclusionUsedThisFrame`.
- Updated diagnostics, frame-contract history/snapshot reporting, frame
  planning, GPU-driven collection/dispatch, indirect rendering, renderer
  initialization, and visibility-buffer culling.
- Preserved previous startup defaults, public GPU-culling API behavior,
  debug freeze behavior, HZB occlusion scheduling, and visibility-buffer
  cull-mask behavior.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.192 disocclusion=0.006933 high_motion=0.005241
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.896/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.039/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.845`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.442`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.196`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.951 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose GPU-culling runtime member names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- GPU culling runtime control and per-frame culling caches now live in
  `m_gpuCullingState`.
- The GPU-culling entity-ID lifetime maps and previous-center history remain
  loose renderer members; they should be split separately because they couple
  to entity lifetime and visibility-buffer motion history.
- The active release gate set remains green after moving GPU culling runtime
  state out of loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of camera/frame runtime,
  local-shadow, quality settings, and renderer-owned entity history.

## Pass 11CB - Camera Frame State Boundary

Completed:

- Added `RendererCameraFrameState` in `src/Graphics/RendererCameraState.h`.
- Grouped:
  - current camera position;
  - current camera forward vector;
  - current near/far planes;
  - previous camera position;
  - previous camera forward vector;
  - previous-camera validity.
- Replaced loose renderer members:
  - `m_cameraPositionWS`;
  - `m_cameraForwardWS`;
  - `m_cameraNearPlane`;
  - `m_cameraFarPlane`;
  - `m_prevCameraPos`;
  - `m_prevCameraForward`;
  - `m_hasPrevCamera`.
- Updated forward-pass camera culling, frame constant camera caching, post
  constants, temporal history invalidation, HZB capture, GPU-culling HZB
  gating, RenderGraph end-frame HZB capture, and scene-change reset.
- Preserved previous camera defaults, TAA camera-motion detection, RT camera
  parameter forwarding, RT temporal history invalidation thresholds, and HZB
  capture semantics.
- Left the similarly named camera fields inside `DX12RaytracingContext`
  unchanged because those are raytracing-subsystem state, not renderer-owned
  frame cache.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.409 disocclusion=0.006891 high_motion=0.00523
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.877/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.89
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.069/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.840`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.445`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.103`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=20.923 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose renderer-owned camera member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Renderer-owned camera cache/history now lives in `m_cameraState` and is
  shared explicitly by temporal, HZB, culling, post constants, and RT camera
  forwarding paths.
- Camera state is now separated from TAA state, GPU culling state, and the DXR
  context's own camera cache.
- The active release gate set remains green after moving camera frame state out
  of loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of local-shadow,
  quality settings, frame runtime flags, and renderer-owned entity history.

## Pass 11CC - Local Shadow State Boundary

Completed:

- Added `RendererLocalShadowState` in
  `src/Graphics/RendererLocalShadowState.h`.
- Grouped:
  - per-frame local-shadow presence;
  - shadowed local-light count;
  - one-time local-shadow budget warning state;
  - local-light view-projection matrices;
  - local-shadow source entities.
- Replaced loose renderer members:
  - `m_hasLocalShadow`;
  - `m_localShadowCount`;
  - `m_localShadowBudgetWarningEmitted`;
  - `m_localLightViewProjMatrices`;
  - `m_localShadowEntities`.
- Updated frame constant reset/build logic, lighting constant packing, and
  shadow pass rendering to read and write local-shadow data through
  `m_localShadowState`.
- Preserved local-shadow defaults, per-frame reset semantics, budget warning
  behavior, local-light matrix packing, and entity-to-shadow matching in the
  shadow pass.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.171 disocclusion=0.006917 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.531/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.022/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.469`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.452`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.184`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=21.615 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose local-shadow member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Local shadow frame data now lives in `m_localShadowState`, separate from the
  broader directional/cascade shadow resources already owned by
  `m_shadowState`.
- Frame constants, lighting constants, and the local-shadow draw path now share
  the same explicit state boundary for local-light shadow metadata.
- The active release gate set remains green after moving local-shadow runtime
  data out of loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of quality settings,
  frame runtime flags, shadow cascade state, and renderer-owned entity history.

## Pass 11CD - Visibility Buffer Frame State Boundary

Completed:

- Added `RendererVisibilityBufferState` in
  `src/Graphics/RendererVisibilityBufferState.h`.
- Grouped:
  - per-frame visibility-buffer instance data;
  - per-frame visibility-buffer mesh draw records;
  - planned/rendered frame flags;
  - HZB occlusion usage for the VB path;
  - debug override state for VB debug/deferred routes.
- Replaced loose renderer members:
  - `m_vbInstances`;
  - `m_vbMeshDraws`;
  - `m_vbPlannedThisFrame`;
  - `m_vbRenderedThisFrame`;
  - `m_vbHzbOcclusionUsedThisFrame`;
  - `m_vbDebugOverrideThisFrame`.
- Updated VB collection, VB culling, VB render-graph stages, deferred
  lighting, motion vectors, diagnostics, descriptors, frame contracts, SSR,
  TAA, temporal mask, RT denoise, RT reflections, and end-frame publication to
  use `m_visibilityBufferState`.
- Added small state helpers for per-frame flag reset and draw-input clearing.
- Preserved VB collection ordering, draw-count reporting, material resolve,
  motion-vector input routing, HZB occlusion diagnostics, debug override
  behavior, and RT/post passes that switch inputs based on whether VB rendered.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.465 disocclusion=0.006831 high_motion=0.005219
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.528/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.024/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.972`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.370`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.185`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.983 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose visibility-buffer member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Visibility-buffer draw data and per-frame execution flags now live in
  `m_visibilityBufferState`, while the `VisibilityBufferRenderer` subsystem
  still owns the GPU resources and pipelines.
- Cross-cutting consumers now share a single state boundary for VB execution:
  frame contracts, motion vectors, SSR/TAA, RT denoise/reflections, resource
  publication, diagnostics, and debug paths.
- The active release gate set remains green after a broad visibility-buffer
  call-site cleanup.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of quality settings,
  frame runtime flags, shadow cascade state, and renderer-owned entity history.

## Pass 11CE - Quality Runtime State Boundary

Completed:

- Added `RendererQualityRuntimeState` in
  `src/Graphics/RendererQualityRuntimeState.h`.
- Grouped:
  - exposure;
  - internal render scale.
- Replaced loose renderer members:
  - `m_exposure`;
  - `m_renderScale`.
- Updated quality settings, diagnostics, frame contracts, frame lighting
  constants, depth/HDR sizing, initialization-time render-scale handling, and
  render-scale history invalidation to use `m_qualityRuntimeState`.
- Preserved exposure clamping, render-scale budget/profile clamping, history
  invalidation on render-scale changes, quality-state reporting, and the
  startup `0.85` internal render scale used by the smoke scenes.
- Left bloom, shadow quality, debug view, and feature toggles in their
  pass-specific state structs because those controls already have clearer
  owners than a broad quality bucket.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.172 disocclusion=0.00691 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.854/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.021/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.475`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.337`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.184`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.9 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose quality scalar member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Exposure and internal render scale now live in `m_qualityRuntimeState`,
  separating global quality scalars from lighting, post-process, shadow,
  temporal, and render-target resource ownership.
- The executable gates still prove the same render-scale sizing, budget-profile
  behavior, frame-contract reporting, and exposure-fed lighting constants.
- The active release gate set remains green after moving the last obvious loose
  quality scalars out of `Renderer`.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of frame runtime flags,
  shadow cascade state, and renderer-owned entity history.

## Pass 11CF - Shadow Cascade Frame State Boundary

Completed:

- Added `ShadowCascadeFrameState` in `src/Graphics/RendererShadowState.h`.
- Grouped:
  - camera-followed shadow frustum scalar defaults;
  - directional light view matrix;
  - per-cascade light projection matrices;
  - per-cascade light view-projection matrices;
  - per-cascade split distances.
- Replaced loose renderer members:
  - `m_shadowOrthoRange`;
  - `m_shadowNearPlane`;
  - `m_shadowFarPlane`;
  - `m_lightViewMatrix`;
  - `m_lightProjectionMatrices`;
  - `m_lightViewProjectionMatrices`;
  - `m_cascadeSplits`.
- Updated directional cascade construction in frame lighting constants to use
  `m_shadowCascadeState`.
- Preserved cascade split computation, texel snapping, shadow map resolution
  scaling, frame constant packing, and local spot-shadow matrix packing after
  the directional cascade range.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.257 disocclusion=0.006912 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.001/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.88
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.012/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.476`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.445`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.190`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.806 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose shadow cascade member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Directional shadow cascade frame data now lives in `m_shadowCascadeState`,
  while shadow-map resources and quality controls remain in `m_shadowResources`.
- Local light shadow metadata remains separately owned by `m_localShadowState`,
  so directional cascade state and local-shadow state now have distinct
  boundaries.
- The active release gate set remains green after moving shadow cascade frame
  data out of loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of frame runtime flags
  and renderer-owned entity history.

## Pass 11CG - Frame Lifecycle State Boundary

Completed:

- Added `RendererFrameLifecycleState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped:
  - device-removed status and one-shot logging state;
  - visual validation capture state;
  - last completed pass marker;
  - render frame counter;
  - missing-buffer and zero-draw warning throttles;
  - verbose diagnostics logging toggle;
  - per-frame RT reflection write and backbuffer-as-RT flags.
- Replaced loose renderer members:
  - `m_deviceRemoved`;
  - `m_deviceRemovedLogged`;
  - `m_visualValidationCaptured`;
  - `m_lastCompletedPass`;
  - `m_renderFrameCounter`;
  - `m_missingBufferWarningLogged`;
  - `m_zeroDrawWarningLogged`;
  - `m_verboseLoggingEnabled`;
  - `m_rtReflectionWrittenThisFrame`;
  - `m_backBufferUsedAsRTThisFrame`.
- Updated frame begin/end, diagnostics, render orchestration, render-graph
  pass tracking, RT reflection/composition paths, and device-removal handling
  to use `m_frameLifecycle`.
- Preserved the existing one-frame semantics for RT reflection writes,
  backbuffer-use tracking, warning throttles, verbose logging, pass completion
  breadcrumbs, render frame counting, and device-removal reporting.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.209 disocclusion=0.006909 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.875/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.41 center_luma=74.83
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.110/2.5 changed=0.001/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.884`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.310`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.185`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=20.184 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose frame-lifecycle member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Frame lifecycle flags now live in `m_frameLifecycle`, keeping transient
  frame diagnostics and device-loss bookkeeping out of the renderer's loose
  top-level field list.
- RT reflection write/backbuffer tracking remains tied to render graph and
  composition validation, but now has a clearer frame-lifecycle owner.
- The active release gate set remains green after moving lifecycle flags out
  of loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of renderer-owned entity
  history, command submission state, and frame-contract snapshot ownership.

## Pass 11CH - GPU Culling Entity History State Boundary

Completed:

- Expanded `GpuCullingRuntimeState` in
  `src/Graphics/RendererGPUCullingState.h`.
- Added `GpuCullingEntityHash` next to the GPU culling state owner.
- Grouped:
  - stable entity-to-culling-ID map;
  - recycled culling-ID free list;
  - per-slot generation counters;
  - next culling-ID slot allocator;
  - previous world-space centers for motion-inflated occlusion culling;
  - previous world matrices for visibility-buffer motion vectors;
  - pending previous-transform-history reset flag.
- Removed the loose renderer members:
  - `m_gpuCullingIdByEntity`;
  - `m_gpuCullingIdFreeList`;
  - `m_gpuCullingIdGeneration`;
  - `m_gpuCullingNextId`;
  - `m_gpuCullingPrevCenterByEntity`;
  - `m_prevWorldByEntity`;
  - `m_previousTransformHistoryResetPending`.
- Updated visibility-buffer collection, indirect rendering, frame planning,
  and scene-change invalidation to use `m_gpuCullingState`.
- Preserved stable packed culling IDs, generation bumping on slot recycle,
  previous-center updates, previous-world motion-vector seeding/pruning, and
  scene-change reset reporting.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.26 disocclusion=0.006887 high_motion=0.005233
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.000/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.41 center_luma=74.85
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.029/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.995`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.374`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.185`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.352 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose GPU culling/entity-history member names remain in renderer
    files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- GPU culling runtime data now owns both the live instance/mesh arrays and the
  entity lifetime history needed by occlusion culling and visibility-buffer
  motion vectors.
- Renderer no longer owns the culling-ID allocator or previous transform maps
  as loose top-level fields.
- The active release gate set remains green after moving entity history out of
  loose renderer members.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, and broader cleanup of command submission
  state, frame-contract snapshot ownership, and any remaining renderer-wide
  cross-cutting state.

## Pass 11CI - Frame Runtime Submission State Boundary

Completed:

- Expanded `RendererFrameRuntimeState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped:
  - graphics fence values;
  - compute fence values;
  - async-compute support flag;
  - swapchain frame index;
  - absolute monotonic frame index;
  - graphics command-list open state;
  - compute command-list open state.
- Removed loose renderer members:
  - `m_computeFenceValues`;
  - `m_asyncComputeSupported`;
  - `m_frameIndex`;
  - `m_absoluteFrameIndex`;
  - `m_commandListOpen`;
  - `m_computeListOpen`.
- Updated initialization, frame begin/end, synchronization, diagnostics,
  descriptor table selection, RT stats readback, RT frame indexing, SSAO
  compute selection, temporal readback, and visibility-buffer retry throttles
  to use `m_frameRuntime`.
- Preserved begin-frame fence waits, swapchain frame-index propagation,
  constant-buffer ring offsets, descriptor-table frame selection, command-list
  reset/close semantics, BLAS scratch release timing, async-compute fallback,
  and end-frame fence signaling.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 123 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.188 disocclusion=0.006913 high_motion=0.005238
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.999/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.87
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.001/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=2.000`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.459`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.186`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.612 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose frame-runtime member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Frame submission, fence, indexing, and async-compute runtime flags now live
  in `m_frameRuntime` instead of the renderer's loose top-level field list.
- The render graph, RT stats, temporal readback, descriptor tables, and
  visibility-buffer retry logic still share one source of truth for the active
  swapchain frame index.
- The active release gate set remains green after consolidating frame runtime
  submission state.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, frame-contract snapshot ownership, and any
  remaining renderer-wide cross-cutting state.

## Pass 11CJ - Visibility Buffer Enable State Boundary

Completed:

- Added `enabled` to `RendererVisibilityBufferState`.
- Removed the loose renderer member `m_visibilityBufferEnabled`.
- Updated initialization, frame feature planning, frame post constants,
  motion-vector routing, render-graph motion-vector registration,
  visibility-buffer orchestration, and diagnostics to use
  `m_visibilityBufferState.enabled`.
- Preserved the existing default-on visibility-buffer policy:
  `CORTEX_DISABLE_VISIBILITY_BUFFER=1` opts out, while the old explicit-enable
  environment variable is still logged if present.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.179 disocclusion=0.006948 high_motion=0.005245
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.038/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.87
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.008/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=2.002`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.991`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.190`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.676 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose visibility-buffer enable member names remain in renderer
    files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Visibility-buffer runtime state now owns its feature enable flag as well as
  its frame-planning/rendering flags and draw inputs.
- The active release gate set remains green after removing another loose
  renderer-wide toggle.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, frame-contract snapshot ownership, and
  remaining renderer-wide diagnostic/resource clusters.

## Pass 11CK - Frame Contract State Boundary

Completed:

- Added `RendererFrameContractState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped:
  - current frame contract object;
  - frame draw counts;
  - motion-vector diagnostics;
  - temporal-mask diagnostics;
  - per-pass frame records;
  - last-pass descriptor usage snapshot.
- Removed loose renderer members:
  - `m_frameContract`;
  - `m_frameDrawCounts`;
  - `m_frameMotionVectors`;
  - `m_frameTemporalMask`;
  - `m_framePassRecords`;
  - `m_lastPassDescriptorUsage`.
- Updated frame planning, pass record capture, contract snapshots, history
  export, validation bridge, temporal readback, motion-vector accounting,
  draw-count producers, and diagnostics to use `m_frameContractState`.
- Preserved pass accounting, descriptor deltas, RT signal/history reporting,
  budget-profile RT scheduling, temporal-mask reporting, motion-vector
  reporting, and public `GetFrameContract()` behavior.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 123 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.176 disocclusion=0.006911 high_motion=0.005237
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.990/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.42 center_luma=74.86
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.010/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.986`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.442`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.194`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=15.879 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose frame-contract member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Frame-contract runtime state now owns the live contract plus the per-frame
  accounting used to populate it.
- Descriptor deltas and pass records still report through the same frame
  contract surface, but the renderer no longer carries those values as loose
  top-level fields.
- The active release gate set remains green after consolidating frame-contract
  ownership.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, constant-buffer ownership, command-resource
  ownership, breadcrumb ownership, and remaining renderer-wide asset/texture
  registry clusters.

## Pass 11CL - Constant Buffer State Boundary

Completed:

- Added `RendererConstantBufferState` in
  `src/Graphics/RendererConstantBufferState.h`.
- Grouped:
  - frame constants ring buffer;
  - current frame-constants GPU address;
  - object constants ring buffer;
  - material constants ring buffer;
  - shadow constants ring buffer;
  - biome-material constants buffer;
  - biome-material validity flag.
- Removed loose renderer members:
  - `m_frameConstantBuffer`;
  - `m_currentFrameConstantsGPU`;
  - `m_objectConstantBuffer`;
  - `m_materialConstantBuffer`;
  - `m_shadowConstantBuffer`;
  - `m_biomeMaterialsBuffer`;
  - `m_biomeMaterialsValid`.
- Updated pass binding, object/material allocation, shadow constants,
  temporal/frame constant writes, RT dispatch descriptors, render-graph end
  frame context, biome-material accessors, and visibility-buffer material
  binding to use `m_constantBuffers`.
- Preserved frame-constant ring initialization, per-frame object/material/shadow
  resets, biome-material atomic validity checks, and all shader root CBV
  binding slots.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.775 disocclusion=0.006891 high_motion=0.005232
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.873/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.89
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.007/2.5 changed=0.000/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.475`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.448`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.186`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=17.203 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose constant-buffer member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Constant-buffer ownership now has a single renderer state boundary instead of
  several independent top-level renderer fields.
- The root CBV binding surface is unchanged, so this pass reduces renderer
  sprawl without changing shader contract behavior.
- The active release gate set remains green after consolidating constant-buffer
  ownership.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, command-resource ownership, breadcrumb
  ownership, and remaining renderer-wide asset/texture registry clusters.

## Pass 11CM - Breadcrumb Diagnostic State Boundary

Completed:

- Added `RendererBreadcrumbState` in
  `src/Graphics/RendererBreadcrumbState.h`.
- Moved the GPU breadcrumb marker enum out of the renderer class and into the
  breadcrumb state header.
- Grouped:
  - breadcrumb readback buffer;
  - mapped breadcrumb value pointer.
- Removed loose renderer members:
  - `m_breadcrumbBuffer`;
  - `m_breadcrumbMap`.
- Updated breadcrumb creation, mapping, reset, write, and device-removed
  diagnostics to use `m_breadcrumbs`.
- Added the missing `EndFrame` marker name to device-removed breadcrumb
  reporting.
- Preserved all existing `WriteBreadcrumb(...)` call sites and marker values.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.175 disocclusion=0.006853 high_motion=0.005226
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.880/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.43 center_luma=74.89
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.147/2.5 changed=0.002/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.489`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.444`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.187`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.145 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose breadcrumb buffer/map member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Device-removed GPU breadcrumb diagnostics now have a clear state boundary.
- The renderer still exposes the breadcrumb write helper, but no longer stores
  breadcrumb resources as loose top-level fields.
- The active release gate set remains green after consolidating breadcrumb
  ownership.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, command-resource ownership, and remaining
  renderer-wide asset/texture registry clusters.

## Pass 11CN - Command Resource State Boundary

Completed:

- Added `RendererCommandResourceState` in
  `src/Graphics/RendererCommandResourceState.h`.
- Grouped:
  - per-frame graphics command allocators;
  - graphics command list;
  - per-frame compute command allocators;
  - compute command list.
- Removed loose renderer members:
  - `m_commandAllocators`;
  - `m_commandList`;
  - `m_computeAllocators`;
  - `m_computeCommandList`.
- Updated graphics command-list recording, reset, close, execution, descriptor
  heap binding, GPU profiler scope binding, resource barriers, copies,
  breadcrumb writes, render-graph/pass execution, and compute command-list
  initialization to use `m_commandResources`.
- Preserved the existing frame-runtime ownership of command-list open/closed
  flags, fence values, async-compute support, and swapchain frame index.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.88 disocclusion=0.007422 high_motion=0.005245
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=3.896/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.68 center_luma=75.73
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.244/2.5 changed=0.004/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=3.181`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=2.342`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.967`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=18.052 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose command-resource member names remain in renderer files;
  - no accidental command-list-open state rewrites were found;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Graphics and compute command resources now have a clear renderer state
  boundary, while submission/runtime flags remain in `m_frameRuntime`.
- Command recording call sites still use the same DX12 command-list operations;
  only the owning field path changed.
- The active release gate set remains green after consolidating command
  resource ownership.
- Remaining public-release work is still concentrated in material/lighting
  quality, final showcase polish, asset/texture registry ownership, and any
  remaining cross-cutting renderer fields that are not yet pass-owned.

## Pass 11CO - Asset Runtime State Boundary

Completed:

- Added `RendererAssetRuntimeState` in
  `src/Graphics/RendererAssetRuntimeState.h`.
- Grouped:
  - asset/memory registry;
  - texture upload receipts and queue;
  - mesh-to-registry-key map;
  - deferred GPU job queue;
  - texture cache.
- Removed loose renderer members:
  - `m_assetRegistry`;
  - `m_textureUploadState`;
  - `m_meshAssetKeys`;
  - `m_gpuJobQueue`;
  - `m_textureCache`.
- Updated asset lifetime collection, memory-budget reporting, environment
  residency, texture loading/queueing/publication, texture receipts, mesh upload
  registration, BLAS/mesh job scheduling, RT frame planning, diagnostics, and
  shutdown cleanup to use `m_assetRuntime`.
- Preserved public diagnostic accessors for asset registry, asset memory
  breakdown, texture upload receipts, texture upload queue stats, and mesh
  asset keys.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.925 disocclusion=0.007414 high_motion=0.005247
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.754/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.71 center_luma=75.83
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.117/2.5 changed=0.003/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=2.413`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=2.333`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=2.184`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=22.347 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose asset/runtime member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Asset memory accounting, texture upload state, texture cache, mesh registry
  keys, and deferred GPU jobs now share one explicit renderer state boundary.
- RT frame planning, frame contracts, budget checks, diagnostics, and texture
  publication still observe the same values through the same public contracts.
- The active release gate set remains green after consolidating asset runtime
  ownership.
- Remaining public-release work is now concentrated in material/lighting
  quality, final showcase polish, and smaller residual renderer-wide fields
  that are not yet pass-owned.

## Pass 11CP - Frame Constants CPU State Boundary

Completed:

- Added `frameCPU` to `RendererConstantBufferState`.
- Removed the loose renderer member `m_frameDataCPU`.
- Updated frame temporal-constant upload, GPU culling inputs, HZB capture,
  overlay/particle/transparent frustum consumers, render-graph visibility
  buffer setup, visibility-buffer culling/deferred-lighting/stage paths, and
  water rendering to read frame constants through `m_constantBuffers.frameCPU`.
- Preserved frame constant upload-slot behavior, root CBV binding, culling
  camera data, HZB camera capture, visibility-buffer camera state, and water
  pass camera inputs.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=4.514 disocclusion=0.007432 high_motion=0.005244
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=6.996/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.69 center_luma=75.78
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.239/2.5 changed=0.004/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.540`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.870`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.972`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=22.655 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose `m_frameDataCPU` names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- CPU-side frame constants now live beside their GPU constant buffer and upload
  bookkeeping, instead of as an unrelated renderer-wide field.
- The frame contract, visibility buffer, culling, HZB, water, and root-binding
  paths still consume the same per-frame values through the existing contracts.
- The active release gate set remains green after consolidating frame constant
  CPU ownership.
- Remaining public-release work is now concentrated in the final residual
  renderer-owned planning/state objects, material/lighting quality, and final
  showcase polish.

## Pass 11CQ - Frame Diagnostics State Boundary

Completed:

- Added `RendererFrameDiagnosticsState` in
  `src/Graphics/Renderer_DiagnosticsTypes.h`.
- Grouped:
  - frame contract state;
  - render-graph runtime diagnostics and transition flags;
  - CPU pass timing diagnostics.
- Removed loose renderer members:
  - `m_frameContractState`;
  - `m_renderGraphState`;
  - `m_passTimings`.
- Updated frame-contract snapshot/pass/memory/history reporting, diagnostics
  accessors, pass draw-count accounting, render-graph transition checks,
  transient validation reporting, and CPU timing reads/writes to use
  `m_frameDiagnostics`.
- Preserved the public frame contract accessor, per-pass descriptor deltas,
  render-graph contract fields, transition skip behavior, and timing output.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 123 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.968 disocclusion=0.007334 high_motion=0.005246
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.867/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.71 center_luma=75.84
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.110/2.5 changed=0.002/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.750`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=2.361`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.993`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=17.848 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose frame diagnostics member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Frame contract production, render-graph diagnostics, transition flags, and
  pass timing diagnostics now share one explicit diagnostics boundary.
- Draw-count updates, pass records, transient descriptor deltas, RT signal
  contracts, and timing accessors still report the same contract data.
- The active release gate set remains green after consolidating frame
  diagnostics ownership.
- Remaining public-release work is now concentrated in scene/planning state
  boundaries, material/lighting quality, and final showcase polish.

## Pass 11CR - Frame Planning State Boundary

Completed:

- Added `RendererFramePlanningState` in
  `src/Graphics/RendererFramePlanningState.h`.
- Grouped:
  - per-frame scene snapshot;
  - RT scheduler frame plan;
  - renderer memory/budget plan.
- Removed loose renderer members:
  - `m_sceneSnapshot`;
  - `m_rtFramePlan`;
  - `m_rendererBudgetPlan`.
- Updated frame planning, RT scheduling, budget contract capture, memory
  reporting, environment residency, texture admission, texture receipts, RT
  denoising, RT dispatch, TLAS candidate collection, depth/shadow/forward/
  transparent/water/VB scene iteration, and diagnostics to use
  `m_framePlanning`.
- Trimmed redundant direct planning includes from `Renderer.h`; planning-owned
  types now enter through `RendererFramePlanningState`.
- Preserved scene snapshot frame validity, RT scheduler decisions, budget
  profile fields, texture/environment budgets, RT reflection dispatch readiness,
  and raw/history RT reflection signal metrics.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 120 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.653 disocclusion=0.007306 high_motion=0.005249
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.563/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.72 center_luma=75.87
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.114/2.5 changed=0.002/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.751`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.614`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.272`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=16.889 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose frame planning member names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Scene snapshotting, RT scheduling, and renderer budget planning now share one
  explicit frame-planning boundary.
- RT frame contracts, memory budgets, texture admission, TLAS candidate
  selection, denoising decisions, and scene iteration paths still observe the
  same planned values.
- The active release gate set remains green after consolidating frame planning
  ownership.
- Remaining public-release work is now concentrated in temporal-history
  ownership, root renderer service pointers, material/lighting quality, and
  final showcase polish.

## Pass 11CS - Temporal History State Boundary

Completed:

- Added `RendererTemporalHistoryState` in
  `src/Graphics/RendererTemporalState.h`.
- Moved the temporal history validity manager under the temporal state
  boundary as `m_temporalHistory.manager`.
- Removed the loose renderer member `m_temporalManager`.
- Updated frame begin, temporal history invalidation/mark-valid paths,
  frame-contract history reporting, post constants, TAA execution, render-graph
  TAA, and RT denoise reprojection checks to use `m_temporalHistory.manager`.
- Removed the direct `TemporalManager` include from `Renderer.h`; temporal
  history ownership now enters through `RendererTemporalState.h`.
- Preserved TAA history validity, RT shadow/reflection/GI history validity,
  reprojection gates, frame-contract history records, and denoiser history
  decisions.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 123 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=3.384 disocclusion=0.007471 high_motion=0.005244
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.883/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.71 center_luma=75.83
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.377/2.5 changed=0.005/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.734`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=4.179`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=2.201`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=19.637 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose `m_temporalManager` names remain in renderer files;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Temporal history validity now lives inside an explicit temporal ownership
  boundary, separate from temporal screen resources and the rejection-mask pass
  state.
- TAA, RT denoise, post constants, and frame-contract history records still use
  the same validity and reprojection decisions.
- The active release gate set remains green after consolidating temporal
  history ownership.
- Remaining public-release work is now concentrated in root renderer service
  pointers, material/lighting quality, final showcase polish, and release
  packaging/docs.

## Pass 11CT - Renderer Service State Boundary

Completed:

- Added `RendererServiceState` in `src/Graphics/RendererServiceState.h`.
- Grouped renderer root services:
  - DX12 device pointer;
  - graphics/upload/compute queues;
  - descriptor and bindless managers;
  - optional hyper-geometry engine;
  - DXR context;
  - RT denoiser;
  - RT reflection signal stats pass;
  - temporal rejection mask pass;
  - GPU culling pipeline;
  - render graph;
  - visibility-buffer renderer.
- Removed the corresponding loose renderer service members from `Renderer`.
- Updated initialization, shutdown, descriptors, resource publication, frame
  begin/end, queue submission/waits, RT resource setup, RT denoise, RT
  reflection signal stats, temporal rejection mask, GPU culling, render graph,
  visibility buffer, diagnostics, and budget/reporting paths to use
  `m_services`.
- Fixed compile feedback from the mechanical move:
  - restored state-struct names that were false-positive service replacements;
  - kept the hyper-geometry service behind the existing experiment guard while
    preserving the disabled-build forward declaration.
- Preserved service lifetimes, command-queue ownership, descriptor heap access,
  bindless publication, RT context readiness, render-graph execution,
  visibility-buffer ownership, temporal rejection-mask output, RT denoiser
  history use, and RT signal stats contracts.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 123 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=2.04 disocclusion=0.007412 high_motion=0.005247
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=2.359/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.70 center_luma=75.79
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.247/2.5 changed=0.004/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=1.739`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.750`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.261`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=27.228 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old loose root service member names remain in renderer files;
  - no false-positive service state names remain;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Root renderer services now live behind one explicit ownership boundary.
- Device, queues, descriptors, bindless resources, RT services, render graph,
  GPU culling, temporal rejection, and visibility-buffer service ownership are
  no longer scattered as loose `Renderer` members.
- The active release gate set remains green after consolidating service
  ownership.
- Remaining public-release work is now concentrated in final renderer-member
  audit, material/lighting quality, showcase polish, and release packaging/docs.

## Pass 11CU - Renderer Host Service Boundary

Completed:

- Extended `RendererServiceState` with the remaining renderer host/service
  fields:
  - presentation window pointer;
  - optional hyper-geometry scene-built flag.
- Removed the last direct primitive/pointer service fields from `Renderer`:
  - `m_window`;
  - `m_hyperSceneBuilt`.
- Updated window-dependent renderer paths to use `m_services.window`:
  - initialization and budget-plan sizing;
  - frame begin/end and presentation;
  - back-buffer import and RTV access;
  - frame constants and post constants;
  - main pass setup, post-process, TAA, bloom, SSAO, RT resources, shadow
    resources, environment sizing, diagnostics, render-graph end-frame, voxel
    backend, and visibility-buffer deferred lighting.
- Updated the optional hyper experiment path to use
  `m_services.hyperSceneBuilt`.
- Preserved presentation dimensions, swap-chain back-buffer access, aspect
  ratio reads, frame-index reads, present behavior, voxel backend rendering,
  and optional hyper-geometry scene build gating.

Verified:

- Full Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt 123 C++ translation units and linked `CortexEngine.exe`.
- Temporal validation smoke passed:
  `gpu_ms=1.267 disocclusion=0.007416 high_motion=0.005248
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase smoke passed:
  `frames=33 gpu_ms=1.834/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.69 center_luma=75.76
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.065/2.5 changed=0.002/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix passed:
  - `8gb_balanced`: `gpu_ms=2.212`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.3945/0.0089`;
  - `4gb_low`: `gpu_ms=1.839`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.530`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend smoke passed:
  `gpu_ms=23.369 visible=7 avg_luma=116.9 nonblack=1`.
- Hygiene passed:
  - no old `m_window` or `m_hyperSceneBuilt` names remain in renderer files;
  - no direct primitive/pointer `m_` members remain in `Renderer.h`;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- `Renderer.h` now contains named state/service aggregates rather than loose
  service pointers or primitive runtime flags.
- Presentation, DX12 services, descriptors, bindless resources, optional
  experiment service state, RT services, render graph, GPU culling, temporal
  rejection, and visibility-buffer ownership are all rooted in
  `RendererServiceState`.
- The active release gate set remains green after the final direct
  primitive/pointer member extraction.
- Remaining public-release work is now concentrated in material/lighting
  quality, final showcase polish, release packaging/docs, and a final
  readiness audit rather than broad renderer member extraction.

## Pass 11CV - Release Validation Gate

Completed:

- Added `tools/run_release_validation.ps1`.
- The release gate now provides one command that:
  - builds Release unless `-NoBuild` is passed;
  - runs temporal validation smoke with isolated logs;
  - runs the full RT showcase smoke with isolated logs;
  - runs the RT budget profile matrix;
  - runs the voxel backend smoke with isolated logs;
  - stops on the first failed step and reports the step log directory;
  - prints a compact per-step summary on success.
- Updated `tools/README.md` with the release validation command and the list of
  covered gates.
- Preserved the stricter individual smoke scripts as the source of truth for
  their own budgets and contracts; the release wrapper only orchestrates them.

Verified:

- Release validation wrapper passed with the current Release executable:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1 -NoBuild`.
- Temporal validation step passed:
  `gpu_ms=1.491 disocclusion=0.007411 high_motion=0.005246
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase step passed:
  `frames=33 gpu_ms=1.527/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.69 center_luma=75.77
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.162/2.5 changed=0.004/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix step passed:
  - `4gb_low`: `gpu_ms=1.811`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=2.314`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend step passed:
  `gpu_ms=16.762 visible=7 avg_luma=116.9 nonblack=1`.
- Aggregate release validation logs were written under:
  `CortexEngine\build\bin\logs\runs\release_validation_20260509_185128_829_71012_9cbbd43b`.

Current renderer-refactor state:

- The active release gate is now a reproducible one-command tool rather than a
  manually remembered sequence.
- The same strict temporal, RT showcase, budget-profile, and voxel gates remain
  independently runnable.
- Architecture extraction is now mostly complete at the `Renderer` member
  boundary; remaining public-release work is concentrated in final readiness
  audit, documentation polish, and any visual/material tuning found by that
  audit.

## Pass 11CW - Public Renderer README Refresh

Completed:

- Updated `CortexEngine/README.md` so the public project entry point describes
  the current renderer instead of the older forward-renderer/LLM-centered Phase
  2 framing.
- Added current renderer capabilities:
  - hybrid raster + RT rendering;
  - material and surface classification;
  - frame/resource contracts;
  - visibility buffer, GPU culling, temporal passes, SSAO, SSR, bloom, and IBL;
  - RT shadows/reflections/GI targets, denoising, and signal diagnostics.
- Added the one-command release validation flow to the README.
- Updated the renderer architecture and pipeline overview to mention frame
  planning, budget plans, render graph work, RT scheduling/readiness, RT
  raw/history signal metrics, and frame-contract export.
- Updated the abbreviated roadmap so Phase 2 now reflects renderer
  architecture/contracts, while Architect/LLM control and the Dream pipeline
  remain documented as adjacent engine capabilities.

Verified:

- Documentation hygiene passed:
  - no trailing whitespace in `CortexEngine/README.md`,
    `tools/README.md`, `tools/run_release_validation.ps1`, or `phase2.Md`;
  - no stale public README phrases such as `forward PBR`,
    `Neuro-Linguistic Control`, or `Dragon Over Water` remain in the checked
    README/tool docs;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- The public README now matches the measured renderer architecture and release
  gate instead of underselling the current DX12/RT contract work.
- The repo has both a high-level public entry point and a repeatable release
  validation command.
- Remaining public-release work is concentrated in final readiness audit and
  any material/lighting/showcase polish found by that audit.

## Pass 11CX - Dreamer Startup Release Polish

Completed:

- Removed the stale commented-out Dreamer initialization block from
  `src/Core/Engine.cpp`.
- Restored the normal optional Dreamer startup path:
  - when `config.enableDreamer` is true, `DreamerService` is created and
    initialized;
  - when initialization succeeds, `m_dreamerEnabled` is set and startup logs
    that async texture generation is ready;
  - when Dreamer is disabled by command line or configuration, startup logs a
    normal configuration message.
- Replaced the old public-facing log
  `Dreamer initialization SKIPPED (commented out for debugging)` with
  `Dreamer disabled by configuration`.
- Preserved the smoke-test `--no-dreamer` path and the Dreamer shutdown path.

Verified:

- Targeted Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt `Engine.cpp` and linked `CortexEngine.exe`.
- Release validation wrapper passed with the rebuilt executable:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1 -NoBuild`.
- Temporal validation step passed:
  `gpu_ms=1.517 disocclusion=0.007443 high_motion=0.005245
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase step passed:
  `frames=33 gpu_ms=2.266/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=71.66 center_luma=75.64
  dark=0.654/0.68 sat=0.009/0.12 near_white=0.016/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/8/0.88/0.56
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0226/0.1424/10.3164/0.0084
  rt_hist=0.0315/0.1433/7.3945/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.300/2.5 changed=0.005/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix step passed:
  - `4gb_low`: `gpu_ms=2.292`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0226/0.1424/10.3164/0.0084`,
    `rt_hist=0.0315/0.1433/7.4023/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=4.049`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0225/0.1424/9.6953/0.0087`,
    `rt_hist=0.0330/0.1443/9.1953/0.0105`.
- Voxel backend step passed:
  `gpu_ms=23.662 visible=7 avg_luma=116.9 nonblack=1`.
- Source/log audit passed:
  - no `commented out for debugging`, `Dreamer initialization SKIPPED`, or
    `TEMPORARILY DISABLED` strings remain in `CortexEngine/src` or the latest
    release-validation logs;
  - latest release-validation logs contain the expected
    `Dreamer disabled by configuration` message for smoke runs;
  - no trailing whitespace was found;
  - `git diff --check` reported only existing CRLF normalization warnings.

Current renderer-refactor state:

- Startup messaging no longer exposes stale debug-era Dreamer state.
- The optional Dreamer service path is represented honestly in code and logs.
- The one-command release gate remains green after the startup polish.
- Remaining public-release work is now a final readiness audit plus any
  material/lighting/showcase polish that audit uncovers.

## Pass 11CY - Public Repository Hygiene

Completed:

- Removed tracked scratch/history/log artifacts from the public repo surface:
  - `CortexEngine/unet_384_build_verbose.txt`;
  - `debug_logs/engine_log_2026-01-16_14-00-46.txt`;
  - `debug_logs/engine_log_2026-01-16_14-03-28.txt`;
  - `fix_postprocess.cpp`;
  - `flickering_tests/test_report.txt`;
  - `full_history.txt`;
  - `head_Engine_h.cpp`;
  - `ps-history-all.txt`;
  - `ps-history.txt`;
  - `temp_build.bat`;
  - `tmp_Engine_head.cpp`.
- Tightened the root `.gitignore` for local diagnostics and scratch files:
  - `debug_logs/`;
  - `flickering_tests/`;
  - shell/history dumps;
  - scratch C++ snapshots;
  - local `nul` artifacts;
  - local recordings and log files.
- Left unrelated untracked local artifacts ignored rather than deleting them
  from the user's workspace.

Verified:

- `git status --short --ignored --ignore-submodules` now reports the removed
  artifacts as deletions and the local `nul` files as ignored.
- No trailing whitespace was found in `.gitignore`, `Engine.cpp`, README/tool
  docs, release validation script, or `phase2.Md`.
- `git diff --check` reported only existing CRLF normalization warnings.
- No rebuild was required for this pass because it only deletes tracked
  scratch artifacts and updates ignore/documentation files.

Current renderer-refactor state:

- The repo is cleaner for publication: obvious tracked logs, shell histories,
  and scratch source snapshots have been removed from the intended commit.
- Local generated diagnostics are less likely to be reintroduced accidentally.
- Remaining public-release work is now concentrated in a final readiness note
  and any code/doc issues discovered by that final audit.

## Pass 11CZ - Release Readiness Note

Completed:

- Added `CortexEngine/RELEASE_READINESS.md`.
- The readiness note summarizes:
  - the one-command release validation gate;
  - the latest verified local gate;
  - the validated renderer scope;
  - known limitations for hardware-dependent timing, optional Dreamer support,
    voxel backend status, and remaining material/showcase polish.
- Linked `RELEASE_READINESS.md` from `CortexEngine/README.md`.

Verified:

- Documentation hygiene passed:
  - no trailing whitespace in `RELEASE_READINESS.md`, `README.md`,
    `.gitignore`, tool docs/scripts, `Engine.cpp`, or `phase2.Md`;
  - `git diff --check` reported only existing CRLF normalization warnings.
- Current `git status --short --ignore-submodules` shows the expected release
  hygiene deletions, the new release-readiness document, and the ongoing
  renderer refactor source/header changes.
- No rebuild was required for this pass because it only adds documentation and
  updates a README link.

Current renderer-refactor state:

- A reviewer can now start from the README, run one release validation command,
  and read a concise release-readiness status without parsing the full Phase 2
  working log.
- The latest code-bearing change remains covered by the rebuilt executable and
  the passing release validation wrapper from Pass 11CX.
- The remaining work, if continuing beyond this checkpoint, is visual
  material/lighting polish rather than architectural blocker removal.

## Pass 11DA - RT Showcase Lighting Polish

Completed:

- Tuned RT showcase presentation controls:
  - reduced sun intensity from `3.4` to `2.85`;
  - reduced bloom intensity from `0.16` to `0.12`;
  - reduced god-ray intensity from `0.65` to `0.42`;
  - lowered conservative-mode bloom/god-ray settings to match the softer
    showcase direction.
- Tuned the gallery hero material and lights:
  - made the metal dragon slightly darker and rougher;
  - softened the softbox, key, and rim lights;
  - reduced fire-particle emission so it contributes bloom/reflection signal
    without dominating the dragon highlight;
  - raised cool fill light enough to keep dark-detail validation inside
    budget.
- Updated `RELEASE_READINESS.md` to point at the newest passing release
  validation run after the visual changes.

Verified:

- Targeted Release rebuild completed successfully:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release`
  rebuilt the scene/control files and linked `CortexEngine.exe`.
- First RT showcase polish smoke intentionally exposed an over-dark frame:
  `dark=0.6928602430555556`, budget `<=0.68`.
- After the fill-light correction, full release validation passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1 -NoBuild`.
- Temporal validation step passed:
  `gpu_ms=2.044 disocclusion=0.00748 high_motion=0.005244
  object_motion=0.0731 visible=7 warnings=0`.
- RT showcase step passed:
  `frames=33 gpu_ms=1.868/16.7 dxgi_mb=408.46/512 est_mb=190.52/256
  rt_mb=114.63/160 write_mb=107.75/128 luma=69.20 center_luma=70.87
  dark=0.677/0.68 sat=0.004/0.12 near_white=0.014/0.14
  water_draws=0/0 max_albedo=1.000/1.0 material_issues=0
  resolved_mat=7/4/1 preset_defaults=0/0/4/0 reflection=12/7/0.88/0.55
  rt_parity=True/0 rt_refl_ready=True/ready
  rt_signal=0.0225/0.1424/10.3398/0.0084
  rt_hist=0.0314/0.1433/7.3008/0.0089 transient_delta=0
  rt_budget=8gb_balanced startup_realloc=0
  temporal_diff=mean=0.082/2.5 changed=0.002/0.08
  surface_debug=view=41 colorful=0.358 nonblack=1.000`.
- Budget profile matrix step passed:
  - `4gb_low`: `gpu_ms=1.605`,
    `rt_refl_ready=False/not_scheduled_this_frame`,
    `rt_signal=0.0225/0.1424/10.3398/0.0084`,
    `rt_hist=0.0314/0.1433/7.3086/0.0089`;
  - `2gb_ultra_low`: `gpu_ms=1.503`,
    `rt_refl_ready=True/ready`,
    `rt_signal=0.0224/0.1424/9.9219/0.0087`,
    `rt_hist=0.0330/0.1443/9.2109/0.0106`.
- Voxel backend step passed:
  `gpu_ms=21.87 visible=7 avg_luma=116.9 nonblack=1`.
- Final capture review:
  `CortexEngine\build\bin\logs\runs\rt_showcase_20260509_190252_988_72424_2ff15ef0\visual_validation_rt_showcase_main.bmp`.

Current renderer-refactor state:

- The showcase is less saturated and has lower near-white/bright ratios while
  staying inside dark-detail and RT signal contracts.
- The active release gate remains green after material/lighting polish.
- Remaining work is no longer an architectural blocker; further polish would be
  subjective scene composition and asset presentation.
