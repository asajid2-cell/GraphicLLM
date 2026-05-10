# Cortex Renderer Robustness And Advancement Plan

This document is the implementation plan for turning Cortex from a rough hybrid
renderer into a robust, ambitious real-time rendering engine. The goal is not to
hide advanced features. Ray tracing, god-rays, transparency, water, temporal
filtering, GPU culling, and high-quality materials should remain first-class
features. The plan is to make those features correct, measurable, budgeted, and
architecturally clean enough to support complex scenes such as a sunset beach,
reflective interiors, cinematic atriums, and later a dedicated path tracing
mode.

The current engine has real technical merit, but it is carrying too many systems
without strong contracts between them:

- raster/deferred or visibility-buffer rendering
- optional DXR shadows, reflections, and GI-like effects
- SSAO, SSR, TAA, FXAA, bloom, fog, and god-rays
- water and transparent materials
- bindless materials and preset-driven shading
- GPU culling and HZB occlusion
- scene presets that mix showcase geometry, water, glass, emissive panels, and
  large thin planes
- startup-time resource creation, mesh upload, BLAS/TLAS construction, and
  render-target allocation

The result is not one isolated bug. The video triage showed a cluster of issues:
startup pressure, temporal instability, false occlusion, screen-space god-ray
streaks, thin-plane scene geometry problems, and likely depth-pass mistakes.
This plan treats those as symptoms of missing renderer contracts.

## Core Direction

Target rendering model:

```text
stable ECS scene
    -> renderer scene snapshot / renderable classification
    -> validated resource residency and material descriptors
    -> visibility preparation with conservative CPU/GPU culling
    -> depth and normal/G-buffer contracts
    -> opaque lighting with stable PBR and IBL
    -> ray tracing effects with scheduled AS updates
    -> transparency, water, fog, and participating media
    -> temporal resolve with explicit history validity
    -> post-processing with bounded energy and luminance
    -> diagnostics, budgets, and reproducible captures
```

The long-term target is a hybrid renderer that can run in real time while also
supporting high-end physically grounded effects. The renderer should be honest
about which features are physically complete and which are approximations, but
the default direction is to improve them rather than remove them.

## Non-Negotiable Design Constraints

- Advanced features stay in the architecture. RT, god-rays, water, transparency,
  and temporal filtering must be fixed, not abandoned.
- Expensive features must be budgeted. A feature is acceptable when it has a
  stable cost model, quality settings, and measurable output.
- Render passes must have explicit input/output contracts. A pass must not
  sample stale resources, wrong-size resources, or undefined history.
- Depth ownership must be precise. Opaque, alpha-tested, transparent, water,
  decals, particles, and sky cannot all be treated as the same depth class.
- Thin planes and double-sided geometry must be handled intentionally. Scene
  authoring should not rely on accidental backface behavior.
- Temporal systems must reset or reproject correctly on camera, scene, material,
  resolution, and feature changes.
- Resource creation must be staged. Startup should not synchronously build every
  heavy resource at once.
- VRAM and DRAM estimates must match actual allocations. The engine should not
  make quality decisions from optimistic accounting.
- Energy use is a real target. Avoid unnecessary memory bandwidth, redundant
  full-screen passes, redundant barriers, and repeated acceleration-structure
  rebuilds.
- Diagnostic toggles are allowed, but not as product fixes. They exist to isolate
  bugs and prove improvements.

## Current Findings From Video And Code Inspection

Video analyzed:

```text
C:\Users\Ahmed\Videos\Screen Recordings\Screen Recording 2026-05-03 032412.mp4
```

The recording is 33.57 seconds at 30 FPS, 1292 x 764, with 1007 video frames.
Contact sheets were generated under:

```text
C:\Users\Ahmed\AppData\Local\Temp\cortex_video_triage_20260503_032412
```

Observed classes of problems:

- Startup and first-frame pressure are plausible even though the recording
  starts after rendering is already visible. RT is enabled by default, RT
  reflections and RT GI toggles are also enabled by default in the high-quality
  path, and RT Showcase contains comments acknowledging BLAS/TLAS memory
  pressure.
- Around frames 418-539, a bright yellow triangular/streaking artifact appears.
  This reads like a god-ray or post-process artifact, not stable physical light.
- Around frames 620-865, large black panels and walls dominate the view. Some of
  this is camera movement into thin scene geometry, but the renderer should still
  handle depth, culling, transparency, and clipping robustly.
- Some white surfaces, panels, and water-like areas change visibility or
  occlusion in ways that look inconsistent.
- Materials look visually underdeveloped: some surfaces are too dark, some are
  too white, roughness/metalness behavior is not obviously stable, and glass/
  water/mirror behavior is not yet convincing.

Most suspicious code-level findings:

- `EngineConfig::enableRayTracing` defaults to true.
- `Engine::Initialize` enables RT at startup if supported.
- RT Showcase config enables heavy settings, including TAA, SSR, SSAO, fog,
  god-rays, RT reflections, and RT GI-related toggles.
- `Renderer::RenderRayTracing` builds TLAS over the current ECS scene during the
  frame when RT is enabled.
- RT Showcase comments explicitly mention memory pressure during first-frame RT
  warm-up and dragon BLAS builds.
- `Renderer::RenderDepthPrepass` draws every renderable through one backface
  culling depth-only pipeline.
- The depth prepass does not skip transparent/glass/water renderables and does
  not select a double-sided depth path for double-sided geometry.
- Water has both a main depth-writing pipeline and a later overlay pipeline,
  which can create confusing depth ownership unless the contract is strict.
- GPU culling is globally enabled and feeds visibility masks into the visibility
  pass.
- HZB occlusion is layered on top of thin planes and temporal history, making it
  a possible false-culling contributor.
- The god-ray shader computes screen-space crepuscular rays from depth and HDR
  luminance, but the configured intensity control path is not cleanly reflected
  in the inspected shader logic.
- VRAM estimation appears optimistic relative to actual full-resolution HDR and
  depth allocations.

## Renderer Contracts To Establish

Before large feature work, the renderer needs explicit contracts. These become
the basis for code changes, tests, debug overlays, and acceptance gates.

### Frame Contract

Each frame must publish:

- render resolution and presentation resolution
- camera matrices with and without jitter
- previous-frame camera matrices
- feature flags actually active this frame
- history validity for each temporal buffer
- pass timings
- transient and persistent GPU memory usage
- descriptor heap usage
- number of renderables by class
- number of RT instances and BLAS/TLAS updates
- number of culling candidates, visible objects, and rejected objects

Acceptance gates:

- The debug HUD can show the current pass graph, resource sizes, history states,
  and memory budgets.
- The engine can capture a single frame report to disk.
- The report can be compared between two runs.

### Depth Contract

Renderable depth classes:

- opaque depth-writing
- alpha-tested depth-writing
- double-sided opaque depth-writing
- double-sided alpha-tested depth-writing
- transparent depth-tested but non-depth-writing
- water depth policy, explicitly selected per water mode
- overlay/debug depth policy
- particles
- sky/procedural atmosphere

Rules:

- Transparent glass must not write opaque depth unless it is in a deliberate
  depth prepass for refraction or volumetric sorting.
- Water must have one clear depth ownership policy per mode.
- Double-sided planes must either use a double-sided depth PSO or be converted to
  solid geometry.
- The depth buffer sampled by SSAO, SSR, RT shadows, RT GI, god-rays, water, and
  transparency must be the intended depth buffer for that stage.

Acceptance gates:

- A debug view can show depth class colors.
- A one-plane double-sided test renders consistently from both sides.
- A glass-over-water test does not incorrectly block water, fog, or god-rays.
- A wall panel does not disappear because the prepass culled the only visible
  side.

### Material Contract

Every renderable must resolve to a material model, not only ad hoc preset text.

Material families:

- diffuse dielectric
- rough dielectric
- metallic conductor
- mirror-like conductor
- clearcoat dielectric
- glass/transmission
- water
- emissive surface
- vegetation/foliage
- skin/subsurface later
- terrain layered material later

Rules:

- Presets can remain as authoring shortcuts, but runtime shading should consume
  explicit structured material parameters.
- Material debug views must show albedo, normal, metallic, roughness, emissive,
  ambient occlusion, material type, and transparency class.
- Texture fallback must be visible in diagnostics and must not silently create
  misleading materials.
- Normal map orientation and tangent frame quality must be testable.

Acceptance gates:

- A material ball scene demonstrates each material family.
- A "known values" material test confirms that metallic, roughness, emissive,
  and glass parameters reach both raster and RT paths.
- The same material appears consistent in opaque, reflection, and RT hit paths
  within the limits of the renderer.

### Ray Tracing Contract

RT remains a core feature, but it must be scheduled and scoped correctly.

RT pass classes:

- RT sun shadows
- RT reflections
- RT diffuse visibility/GI approximation
- future RT probes or DDGI
- future path tracing mode

Rules:

- BLAS builds are not allowed to create startup stalls without being scheduled.
- TLAS rebuilds must be incremental or budgeted.
- Static meshes should build BLAS once and reuse them.
- Dynamic objects should update or rebuild only when necessary.
- RT resources should be half/quarter/full resolution by explicit policy.
- RT history must reset when feature state, resolution, camera cut, scene, or
  material inputs change.
- RT GI must be named and implemented honestly. If it is ambient-visibility GI,
  it should be treated as such until upgraded.

Acceptance gates:

- Startup reaches interactive state while RT warm-up progresses in a measured
  schedule.
- RT effects converge without flashing when enabled from launch.
- The renderer can report BLAS count, TLAS instance count, scratch allocation,
  and per-pass RT cost.

### Atmosphere And God-Ray Contract

God-rays should become a real, controllable atmospheric feature rather than an
unbounded screen-space artifact.

Rules:

- God-ray intensity control must be used by the shader.
- Rays must be energy bounded.
- Rays must be stable under camera motion.
- Rays should use physical or physically motivated parameters:
  - sun direction
  - sun radiance
  - medium density
  - anisotropy
  - extinction
  - scattering color
  - depth-aware occlusion
- The implementation may start as improved screen-space volumetric scattering,
  then evolve toward froxel/volumetric integration.

Acceptance gates:

- The frame range that produced the yellow triangular artifact no longer
  produces screen-space streak explosions.
- God-ray sliders produce predictable results.
- A sunset/beach scene can show warm volumetric sun shafts without blowing out
  the image.

## Migration 1: Build A Reproducible Renderer Triage Harness

### Purpose

Before changing major systems, create repeatable capture and comparison tools so
we can prove each fix. The current video analysis is useful, but the engine
needs its own deterministic diagnostics.

### Architecture

Add a renderer test harness that can run a scene for a fixed number of frames,
move the camera along a scripted path, and write:

- frame captures
- selected debug-view captures
- frame timing CSV
- memory usage CSV
- feature-state JSON
- resource inventory JSON
- optional GPU marker output if available

This should not replace normal interactive use. It is a reproducibility layer
for engine development.

### Implementation Steps

1. Add a `RenderCaptureConfig` structure.
2. Add CLI/config support for scripted camera paths.
3. Add a `--capture-frames` or equivalent development option.
4. Add deterministic scene load seed and deterministic animation time stepping.
5. Add frame hash and image difference tooling.
6. Add a debug mode sequence capture:
   - beauty
   - depth
   - normals
   - material type
   - roughness/metallic
   - motion vectors
   - HZB mip
   - culling mask
   - RT shadow mask
   - RT reflection buffer
   - RT GI buffer
   - god-ray contribution

### Acceptance Gates

- Running the same scene/camera script twice produces comparable frame hashes
  within an expected tolerance.
- A capture can isolate whether popping comes from color, depth, culling,
  material, RT, or post-process.
- The current RT Showcase path has a baseline capture checked into ignored test
  output, not source control.

### Tests

- Unit test config parsing.
- Smoke test RT Showcase scripted camera for 300 frames.
- Image-diff test for no full-frame black flashes.
- Image-diff test for no large unbounded luminance spikes.

## Migration 2: Correct Resource And Memory Accounting

### Purpose

The engine cannot optimize VRAM, DRAM, GPU usage, or energy if it does not know
what it actually allocated. The current estimate is a useful start but is too
coarse and appears optimistic.

### Architecture

Add a renderer memory ledger with two layers:

- actual committed D3D12 resources owned by the renderer
- estimated external or transient allocations, such as BLAS scratch, descriptor
  tables, upload rings, and asset buffers

Each resource should record:

- name
- width/height/depth
- format
- bytes per pixel or byte size
- mip count
- array count
- heap type
- persistent/transient classification
- owning subsystem
- current state if tracked

### Implementation Steps

1. Fix bytes-per-pixel accounting for formats:
   - `R16G16B16A16_FLOAT` is 8 bytes per pixel.
   - `R11G11B10_FLOAT` is 4 bytes per pixel.
   - `D32_FLOAT` is 4 bytes per pixel.
   - compressed textures require block-based accounting.
2. Report actual HDR/depth resource dimensions, not intended render scale.
3. Track render target resources:
   - HDR
   - TAA history/intermediate
   - depth
   - normal/roughness
   - emissive/metallic
   - SSR
   - SSAO
   - RT shadow mask
   - RT reflection color/history
   - RT GI color/history
   - bloom chain
   - HZB mips
   - shadow maps
   - water/transparent intermediates if any
4. Track mesh and material asset buffers.
5. Track BLAS/TLAS result buffers and scratch buffers.
6. Track upload heap/ring usage.
7. Add per-frame memory pressure HUD.
8. Add warnings when budgets are exceeded.

### Efficiency Goals

- Avoid duplicate full-resolution buffers.
- Prefer half-resolution for noisy low-frequency effects when quality allows.
- Reuse transient resources through a render-graph allocator.
- Keep history buffers only for features that are active or warming.
- Use aliasing/transient heaps only after correctness is proven.

### Acceptance Gates

- The memory HUD matches actual D3D12 resource descriptors.
- The RT Showcase report lists all major GPU resources and total GPU bytes.
- Budget warnings identify which subsystem owns the memory.
- No resource estimate uses stale render-scale assumptions.

### Tests

- Unit tests for DXGI format byte accounting.
- Runtime smoke that dumps memory JSON.
- Diff memory JSON before and after scene switch to catch leaks.

## Migration 3: Fix Depth Ownership And Renderable Classification

### Purpose

The depth prepass is currently the strongest low-level bug candidate. It draws
all renderables through one backface-culling depth-only pipeline. That is not a
valid contract for transparent objects, water, or double-sided planes.

### Architecture

Introduce a `RenderableClassification` stage before drawing. The renderer should
classify each renderable exactly once per frame or scene update:

```text
RenderableComponent + material + mesh + components
    -> RenderClass
    -> depth policy
    -> cull mode
    -> pass membership
    -> material shader path
```

Render classes:

- opaque single-sided
- opaque double-sided
- alpha-tested single-sided
- alpha-tested double-sided
- transparent glass
- water surface
- emissive opaque
- overlay/debug
- particle emitter
- sky/atmosphere

### Implementation Steps

1. Add a renderer-side classification helper.
2. Replace ad hoc `IsTransparentRenderable` usage with the classification.
3. Add depth prepass buckets:
   - opaque single-sided
   - opaque double-sided
   - alpha-tested single-sided
   - alpha-tested double-sided
4. Exclude transparent glass from opaque depth prepass unless a future
   refraction-depth mode explicitly requests it.
5. Exclude water from opaque depth prepass until water depth policy is defined.
6. Create or reuse double-sided depth-only PSO.
7. Add alpha-tested depth-only PSO.
8. Make the visibility-buffer path and classic path use the same classification.
9. Add debug overlay showing render class per object.

### Water Depth Policy

Water needs a deliberate mode:

- Mode A: water writes depth in main pass, suitable for opaque-looking water.
- Mode B: water depth-tests but does not write depth, suitable for transparent
  reflective water.
- Mode C: water writes a separate water-depth or water-mask buffer for SSR,
  refraction, and fog.

The ambitious correct path is Mode C. It gives post-process and transparency a
stable way to know where water exists without corrupting the opaque depth buffer.

### Acceptance Gates

- Transparent glass no longer blocks opaque depth-based effects incorrectly.
- Water no longer participates in the wrong depth pass.
- Double-sided planes render consistently from both sides.
- The frame has one authoritative opaque depth buffer.

### Tests

- Depth prepass unit/classification tests.
- Scene test with a double-sided plane viewed from front and back.
- Scene test with glass roof above water.
- Scene test with thin wall panels and camera moving around them.

## Migration 4: Stabilize GPU Culling And HZB Occlusion

### Purpose

GPU culling and HZB occlusion are valuable. They should not be removed. They
need stricter correctness rules so they reduce GPU work without causing
visibility popping.

### Architecture

Culling stages:

```text
CPU scene bounds validation
    -> GPU frustum culling
    -> optional HZB occlusion
    -> conservative hysteresis
    -> visibility mask
    -> visibility buffer / indirect draws
```

Key principle: culling can only reject objects when it is conservative. If the
system is uncertain, it must draw.

### Implementation Steps

1. Add a culling diagnostics overlay:
   - total tested
   - frustum rejected
   - HZB rejected
   - force-visible count
   - rejected object IDs or names in debug mode
2. Validate mesh bounds for generated planes, quads, cylinders, torus, and
   imported meshes.
3. Inflate thin-plane bounds in world space based on actual transform.
4. Disable HZB rejection for:
   - transparent objects
   - water
   - large near-camera surfaces
   - objects crossing the near plane
   - objects with unreliable bounds
5. Use previous-frame HZB only when previous camera state is valid.
6. Reset occlusion history on scene change, camera cut, large teleport, and
   render-target resize.
7. Add a "force visible but still measure" mode for debugging.
8. Add culling heatmap debug view.

### Acceptance Gates

- GPU culling reduces draw cost in complex scenes without visible popping.
- No object disappears for one frame because HZB history was stale.
- Thin planes in RT Showcase are never HZB-culled while partially visible.

### Tests

- Camera-path test through RT Showcase with HZB enabled.
- A/B capture: force visible versus culling enabled must differ only in expected
  hidden geometry, not visible surfaces.
- Unit tests for world-space transformed bounds.

## Migration 5: Schedule Ray Tracing Instead Of Blocking Startup

### Purpose

Ray tracing should remain enabled as a core engine feature. The fix is not to
turn it off. The fix is to make RT resource construction asynchronous,
incremental, cache-aware, and budgeted.

### Architecture

RT lifecycle:

```text
mesh asset loaded
    -> static/dynamic RT eligibility classification
    -> BLAS build request
    -> build budget scheduler
    -> BLAS cache publication
    -> TLAS instance table update
    -> RT pass dispatch
    -> history accumulation
```

### Implementation Steps

1. Separate RT feature "desired" from RT feature "ready".
   - desired: user/config wants RT.
   - ready: required pipelines/resources/history are valid.
   - warming: RT is building AS/resources within budget.
2. Build BLAS over multiple frames with a budget:
   - max builds per frame
   - max scratch bytes per frame
   - max upload bytes per frame
   - priority based on visible objects
3. Cache BLAS per mesh asset key.
4. Track BLAS memory and scratch memory in the memory ledger.
5. Build TLAS from currently available BLAS instances.
6. Render missing RT instances with safe fallback until their BLAS is ready.
7. Reset RT histories when TLAS membership changes materially.
8. Add RT warm-up HUD:
   - BLAS queued
   - BLAS ready
   - TLAS instances
   - scratch bytes
   - RT pass cost
9. Add shader table and descriptor validity checks.

### Academic Direction

The correct long-term model is a renderer scene with explicit acceleration
structure ownership, not direct ad hoc ECS traversal every frame. The ECS should
author the scene; the renderer should maintain a GPU-ready representation.

### Acceptance Gates

- RT can be requested at startup without a visible hang.
- RT warms up progressively and reports progress.
- Static BLAS are reused across frames and scene switches where possible.
- TLAS rebuild cost is visible and bounded.
- RT reflections/shadows/GI do not sample undefined data during warm-up.

### Tests

- Startup smoke with RT requested.
- Stress scene with many static meshes.
- Scene switch test confirms BLAS cache lifetime is safe.
- Device-removed regression test around RT Showcase.

## Migration 6: Make RT Effects Physically Cleaner And More Honest

### Purpose

Cortex has real DXR, but the current effects are auxiliary passes. They should
be improved into a coherent hybrid RT system.

### RT Shadows

Plan:

- Keep as a single visibility-ray pass for the sun.
- Add stable bias derived from surface normal, depth slope, and scene scale.
- Exclude transparent or alpha materials only according to material flags.
- Add debug view for shadow mask and ray misses.
- Add temporal filtering only with strict history validity.

Acceptance gates:

- No shadow flashing on static geometry.
- Shadow mask matches raster shadow direction and scene scale.

### RT Reflections

Plan:

- Define reflection inputs:
  - depth
  - normal
  - roughness
  - material type
  - reflection mask
  - previous reflection history
- Trace only where material roughness and reflection eligibility justify cost.
- Use roughness-aware resolution:
  - mirror/glossy at higher quality
  - rough reflections at lower quality or probe fallback
- Add spatial/temporal denoising that is aware of normal, depth, roughness, and
  camera motion.
- Use environment/probe fallback for misses.
- Avoid claiming full recursive path tracing in this pass.

Acceptance gates:

- Mirror panels are stable under camera motion.
- RT reflection buffer has no previous-frame G-buffer sampling artifacts.
- Rough materials do not waste high-cost reflection rays.

### RT GI / Ambient Visibility

Plan:

- Rename internal concept to match implementation until upgraded:
  - "RT ambient visibility" or "RT diffuse visibility"
  - later "RTGI" once it samples lighting and bounces correctly
- Add material-aware diffuse ray directions.
- Add blue-noise/random sequence stability.
- Add temporal accumulation with reprojection.
- Add denoising.
- Later migrate to a more academically complete real-time GI option:
  - DDGI probes,
  - ReSTIR GI,
  - SDF/voxel cone assist,
  - or a separate path tracing mode for validation.

Acceptance gates:

- The pass darkens contact/occluded areas without full-frame flicker.
- It has a debug view and documented limitations.
- It does not destabilize base lighting when history is invalid.

## Migration 7: Rebuild God-Rays Into A Proper Volumetric System

### Purpose

The current yellow flare artifact points to an unbounded or under-controlled
screen-space god-ray path. The feature should become physically motivated,
artist-controllable, and stable.

### Phase A: Fix Current Screen-Space God-Rays

Implementation steps:

1. Wire `m_godRayIntensity` through the shader and actually use it.
2. Add parameters:
   - scattering intensity
   - extinction
   - anisotropy
   - max contribution
   - depth threshold
   - temporal blend factor
3. Clamp luminance before accumulation.
4. Use depth linearization consistently.
5. Avoid multiplying by arbitrary HDR luminance without normalization.
6. Reject invalid sun projections and near-camera singularities.
7. Add a separate god-ray contribution debug view.
8. Add a per-frame max luminance readback or approximate histogram.

Acceptance gates:

- No triangular streak explosion in the video-equivalent camera path.
- Slider changes visibly and predictably affect intensity.
- Maximum god-ray contribution is bounded.

### Phase B: Move To Froxel/Volumetric Lighting

Implementation steps:

1. Add a low-resolution froxel grid aligned to camera frustum.
2. Inject participating media density.
3. Inject sun lighting with phase function.
4. Integrate along view rays.
5. Composite volumetric lighting before bloom/tonemap.
6. Support local volumetric lights later.
7. Add temporal reprojection for froxel history.
8. Add blue-noise jitter to reduce banding.

Academic target:

- Henyey-Greenstein phase function.
- Beer-Lambert transmittance.
- Physically meaningful density/extinction.
- Energy-bounded scattering.

Acceptance gates:

- Sunset/beach scene can show warm atmospheric shafts.
- Indoor atrium can show volumetric beams through windows.
- God-rays are stable under camera movement and do not become screen-space
  triangles.

## Migration 8: Clean Up Materials And Build A Real Showcase Material System

### Purpose

Beautiful scenes require a strong material system. The current preset string
approach is useful for prototyping but too implicit for a mature renderer.

### Architecture

Add a structured material model:

```text
MaterialAsset
    -> base color / texture
    -> normal / tangent policy
    -> metallic / roughness
    -> emissive
    -> alpha mode
    -> transmission / IOR
    -> clearcoat
    -> sheen
    -> anisotropy
    -> material domain
    -> render class
```

### Implementation Steps

1. Create explicit material asset structures.
2. Convert scene presets from string-only material logic to material assets.
3. Preserve preset names as authoring shortcuts only.
4. Add material validation:
   - missing texture
   - invalid normal map
   - extreme roughness/metallic mismatch
   - alpha mode mismatch
   - transmission without transparent pass
5. Add material debug views.
6. Add material ball test scene.
7. Add physically plausible default values.
8. Add environment probe compatibility.
9. Add material serialization for authored scenes later.

### Sunset Beach Material Requirements

To support a high-quality beach scene, the renderer needs:

- sand material with layered roughness and normal detail
- wet sand material with Fresnel reflectance
- ocean/water material with normal waves, reflection, refraction, and foam
- sky/atmosphere material or procedural sky model
- sun/sky lighting with correct color temperature controls
- vegetation/rock materials if the scene includes cliffs or plants
- volumetric haze
- reflection probes or RT reflections for wet surfaces

### Acceptance Gates

- Material ball scene looks stable across camera movement.
- RT Showcase materials are authored as structured materials.
- Glass, water, chrome, brushed metal, plastic, brick, concrete, wood, and
  emissive surfaces have distinct, predictable behavior.

## Migration 9: Water, Glass, And Transparency Correctness

### Purpose

Water and glass are currently mixed into a renderer that does not have a strong
transparency contract. These are core showcase features and should become
correct.

### Architecture

Transparency should be split into:

- alpha-tested cutouts
- blended transparent surfaces
- physically transmissive glass
- water
- particles

Water should have its own pass with:

- water mask
- water normal
- water depth or thickness
- reflection input
- refraction input
- foam/shoreline input later

### Implementation Steps

1. Exclude glass/water from opaque depth prepass unless explicitly configured.
2. Add transparent sorting by camera depth and material priority.
3. Add weighted blended OIT or a more robust transparency method for multiple
   transparent layers.
4. Add water mask buffer.
5. Add water normal and roughness outputs.
6. Add SSR/RT reflection integration for water.
7. Add refraction using scene color and thickness approximation.
8. Add foam and shoreline support later.
9. Add debug views for water mask, transparency depth, and refraction.

### Acceptance Gates

- Glass roof over water renders consistently.
- Water no longer corrupts opaque depth effects.
- Transparent surfaces do not randomly occlude god-rays or RT passes.
- Water can support a sunset beach scene convincingly.

## Migration 10: Temporal Stability And History Management

### Purpose

TAA, RT accumulation, SSR, SSAO, god-rays, and future volumetrics all depend on
history. History must be explicitly valid or invalid. Stale history is a common
source of flashing, ghosting, and inconsistent materials.

### Architecture

Add a `TemporalHistoryManager` or equivalent renderer subsystem.

Tracked histories:

- TAA color
- motion vectors
- RT shadow mask
- RT reflections
- RT GI / diffuse visibility
- SSR
- SSAO if temporally filtered
- volumetric fog/god-rays
- exposure/luminance history

Invalidation events:

- scene rebuilt
- camera cut or teleport
- render scale or target resize
- material changes
- RT TLAS membership changed beyond threshold
- feature toggled
- shader hot reload
- environment map changed
- debug mode requiring unfiltered output

### Implementation Steps

1. Centralize history validity flags.
2. Replace scattered booleans with named history resources.
3. Add per-history frame index and reset reason.
4. Add debug HUD showing history age and validity.
5. Ensure every temporal shader receives correct jitter and previous matrices.
6. Add history clamping based on depth, normal, material, roughness, and motion.
7. Add camera-cut detection.

### Acceptance Gates

- Scene switch does not leave afterimages.
- Enabling RT from startup or during runtime does not flash undefined buffers.
- Camera motion does not cause repeated material popping.

## Migration 11: Render Graph And Transient Resource Optimization

### Purpose

Energy, VRAM, and GPU usage improve when passes are explicit and resources can
be reused safely. Cortex already has render-graph pieces, but the renderer still
has large manual sections.

### Architecture

Long-term pass graph:

```text
FrameSetup
DepthPrepass
VisibilityBuffer or GBuffer
MaterialResolve
ShadowMaps
HZB
SSAO
RTShadows
RTGI
OpaqueLighting
SSR
RTReflections
Water
Transparency
Volumetrics
Bloom
TAA
Tonemap
UI
Present
```

Each pass declares:

- reads
- writes
- resource state transitions
- resolution
- clear/load behavior
- async compute eligibility later
- timing marker

### Implementation Steps

1. Inventory which passes are already in the render graph.
2. Move remaining manual full-screen passes into graph one by one.
3. Add transient resource aliasing after correctness tests pass.
4. Add pass culling for resources that are provably unused in a frame.
5. Add resource lifetime visualization.
6. Add automatic barrier validation.
7. Add pass timings to capture output.

### Efficiency Goals

- Fewer full-resolution persistent buffers.
- Fewer redundant clears.
- Fewer redundant barriers.
- Better cache locality.
- Better async compute candidates:
  - HZB
  - SSAO
  - bloom
  - RT denoising
  - volumetric integration

### Acceptance Gates

- Pass graph output matches current renderer visually.
- Memory ledger shows reduced persistent render-target footprint.
- GPU time does not regress after each migration.

## Migration 12: Scene Authoring Cleanup For RT Showcase

### Purpose

Some visual problems are renderer bugs, but RT Showcase itself is too fragile.
It is a large environment made from many isolated planes and showcase pieces.
The scene needs to become a proper validation and portfolio scene.

### Architecture

Split RT Showcase into smaller test scenes:

- material lab
- reflective gallery
- water courtyard
- volumetric atrium
- full combined showcase

Each subscene should be useful for debugging one feature family.

### Implementation Steps

1. Replace large thin walls where possible with solid box geometry.
2. Keep intentional planes only when their culling/depth policy is explicit.
3. Add camera collision or camera bounds for showcase walkthroughs.
4. Add named landmarks and scripted camera paths.
5. Add proper light rigs per subscene.
6. Add authored material assets.
7. Add reflection probes where appropriate.
8. Add scene validation:
   - missing mesh buffers
   - missing textures
   - invalid bounds
   - transparent objects in opaque pass
   - zero-area or extreme-scale transforms

### Acceptance Gates

- Camera path through the full showcase has no unintended wall clipping.
- Each subscene can be captured independently.
- Scene validation reports no high-severity issues.

## Migration 13: High-End Lighting Roadmap

### Purpose

To render beautiful scenes, Cortex needs lighting architecture beyond isolated
effects.

### Near-Term Lighting

- Direct sun with physically meaningful intensity controls.
- IBL with environment maps and exposure control.
- Local lights with clear shadow policy.
- Reflection probes for stable glossy fallback.
- RT shadows for high-quality visibility.
- RT reflections for mirror/glossy surfaces.
- SSAO and RT ambient visibility for contact grounding.

### Medium-Term Lighting

- DDGI or probe-based diffuse global illumination.
- Volumetric atmosphere/fog.
- Sky model with sun elevation/color temperature.
- Auto exposure and luminance histogram.
- Light units or at least consistent scene-referred intensity scale.

### Long-Term Lighting

- ReSTIR DI/GI exploration.
- Reservoir-based many-light sampling.
- Hardware ray tracing denoisers.
- Path tracing mode for validation and beauty captures.

### Sunset Beach Target

Required systems:

- procedural or authored sky model
- low-angle sun with warm color temperature
- atmospheric haze/fog
- ocean/water material
- wet sand reflections
- foam and shoreline treatment
- exposure adaptation
- reflection fallback for water/wet surfaces
- volumetric shafts or aerial perspective

Acceptance gate:

- A sunset beach scene can be rendered with stable frame time, believable
  materials, visible atmosphere, and no post-process artifacts.

## Migration 14: Optional Path Tracing Mode

### Purpose

Cortex should remain a real-time renderer, but a separate path tracing mode is
academically valuable. It can validate materials and lighting, produce beauty
shots, and clarify the difference between hybrid RT effects and full light
transport.

### Architecture

Add a separate path tracing render mode, not a mutation of the RT reflection
pass.

Path tracer requirements:

- primary ray generation
- TLAS traversal
- material hit evaluation
- BSDF sampling
- direct light sampling
- multiple bounces
- throughput accumulation
- progressive sample accumulation
- denoising later
- camera/scene change reset

### Implementation Steps

1. Build Cornell path tracing test scene.
2. Add diffuse and mirror materials.
3. Add emissive area light sampling.
4. Add progressive accumulation buffer.
5. Add 4-8 bounce loop.
6. Add debug modes:
   - albedo
   - normal
   - depth
   - sample count
   - variance
7. Add glass/transmission after diffuse/mirror is correct.
8. Add one-way mirror experiment only after material and path rules are solid.

### Acceptance Gates

- Path tracing mode renders a Cornell box progressively.
- It does not replace or destabilize the real-time renderer.
- It can be used as a reference for material and lighting behavior.

## Migration 15: Performance And Energy Optimization Program

### Purpose

The engine should use less VRAM, DRAM, GPU time, and energy while becoming more
advanced. This requires disciplined measurement and targeted optimization.

### CPU / DRAM

Plan:

- Avoid rebuilding scene draw data when ECS state has not changed.
- Cache renderable classification.
- Use dirty flags for transforms, materials, meshes, and lights.
- Avoid per-frame string processing for material presets.
- Avoid repeated vector allocation in hot paths.
- Batch upload jobs.
- Keep CPU mirrors only where needed.

Acceptance gates:

- CPU frame time decreases in RT Showcase.
- Per-frame allocations are measured and reduced.

### GPU / VRAM

Plan:

- Use actual resource ledger.
- Reuse transient render targets.
- Use half/quarter resolution for low-frequency effects with denoising.
- Keep RT history buffers only when needed.
- Budget BLAS scratch.
- Prefer compressed textures for large authored assets.
- Add texture streaming later.
- Add reflection probes to reduce unnecessary RT rays on rough surfaces.

Acceptance gates:

- RT Showcase memory usage is known and stable.
- No startup memory spike causes device removal.
- Feature cost scales predictably with resolution.

### Bandwidth / Energy

Plan:

- Reduce full-screen pass count where passes can merge without harming clarity.
- Avoid clearing resources that are fully overwritten.
- Use smaller formats where quality allows.
- Use async compute for suitable passes after correctness.
- Add dynamic quality budgets that preserve features by reducing resolution,
  sample count, or update rate instead of turning features off.

Important distinction:

- Dynamic quality scaling is not feature disabling. RT and god-rays remain
  enabled, but their resolution, ray count, denoising cadence, or update rate can
  adapt to budget.

Acceptance gates:

- Under heavy load, the renderer degrades quality smoothly rather than stalling.
- GPU time and memory pressure are visible and controlled.

## Migration 16: Verification Matrix

### Required Scenes

- RT Showcase full path
- Reflective gallery
- Water courtyard
- Volumetric atrium
- Cornell box
- Material lab
- Sunset beach target scene
- Stress scene with many small meshes
- Transparent layering scene
- Thin-plane/double-sided scene

### Required Debug Captures

- beauty
- depth
- normals
- albedo
- metallic
- roughness
- material type
- motion vectors
- TAA history confidence
- HZB mip
- culling mask
- shadow map
- RT shadow mask
- RT reflections
- RT GI / ambient visibility
- water mask
- transparency contribution
- god-ray contribution
- bloom contribution
- luminance histogram or approximation

### Required Automated Checks

- no device removal
- no full-frame black flash
- no unbounded luminance spike
- no NaN/Inf in GPU debug buffers where measurable
- no missing descriptor for active pass
- no stale history after scene reset
- no visible object rejected by culling in force-visible comparison
- no memory ledger mismatch with actual resource descriptors

## Implementation Order Summary

1. Capture harness and diagnostics.
2. Actual memory/resource ledger.
3. Renderable classification and depth ownership fix.
4. GPU culling/HZB correctness gates.
5. RT warm-up scheduler and AS memory tracking.
6. RT shadow/reflection/GI stability improvements.
7. God-ray shader control and bounded screen-space fix.
8. Material asset model and material lab scene.
9. Water/glass/transparency depth and composition contract.
10. Temporal history manager.
11. Render graph migration and transient resource reuse.
12. RT Showcase scene cleanup and validation.
13. High-end lighting roadmap implementation.
14. Sunset beach target scene.
15. Optional path tracing mode.

## Immediate Next Phase Proposal

The first implementation phase should not touch aesthetics yet. It should build
the measurement and correctness foundation:

```text
Phase 1A
    - renderer capture harness
    - memory ledger
    - pass/resource HUD
    - debug-view capture sequence

Phase 1B
    - renderable classification
    - depth prepass buckets
    - transparent/water exclusion from opaque depth
    - double-sided depth path

Phase 1C
    - culling diagnostics
    - HZB/culling conservative gates
    - RT warm-up metrics
```

After Phase 1, the engine should be much easier to reason about. Then the
ambitious visual work becomes safer:

```text
Phase 2A
    - controlled god-rays
    - material lab
    - RT reflection denoising/stability

Phase 2B
    - structured material assets
    - water/glass correctness
    - volumetric/froxel prototype

Phase 2C
    - sunset beach target scene
    - lighting/exposure polish
    - render graph resource reuse
```

## Open Design Questions

These do not block the plan, but they should be resolved as implementation
starts:

1. Should the first target GPU budget be based on an 8 GB card, a 12 GB card, or
   the machine currently running Cortex?
2. Should the renderer keep both visibility-buffer and classic/deferred paths,
   or should one become the primary path with the other as a temporary fallback?
3. Should RT Showcase remain the default startup scene, or should Material Lab
   become the default development scene while Showcase becomes a validation
   target?
4. Should the sunset beach scene be authored manually first, procedurally
   generated first, or built as a hybrid authored/procedural scene?
5. Should the path tracing mode be planned for after the real-time renderer is
   stable, or should it be developed earlier as a material/lighting reference?

## Definition Of Done For This Plan

This plan is complete when Cortex can:

- start with advanced features requested without startup stalls or device
  removal,
- report accurate resource, memory, and pass timing data,
- render RT Showcase without popping, false occlusion, or god-ray explosions,
- show stable materials across raster, RT, water, and transparency paths,
- scale quality under budget pressure without turning the renderer into a
  different feature set,
- render a high-quality sunset beach scene with believable water, atmosphere,
  materials, reflections, and lighting,
- provide enough diagnostics that future bugs are isolated by subsystem instead
  of guessed from visual symptoms.

## Implementation Notes: Depth Contract Pass

Status: implemented and smoke-tested.

Changes made:

- Added a shared renderable depth classification contract for opaque,
  alpha-tested, double-sided, transparent, water, overlay, and invalid
  renderables.
- Routed depth prepass, main opaque rendering, visibility-buffer collection,
  GPU culling collection, indirect rendering, shadow rendering, water,
  transparent, and overlay passes through the same classification policy.
- Removed water from the normal opaque path. Water is now owned by its dedicated
  depth-tested no-write pass instead of being conditionally rendered by both
  opaque and water paths.
- Added frame-contract warnings for missing depth, transparent, water, overlay,
  and read-only DSV resources when matching renderables exist.
- Added `frame_contract.depth_policy` to the frame report so diagnostics show
  depth-writing versus depth-tested no-write totals.
- Fixed frame diagnostics output to write beside the executable under
  `build/bin/logs` instead of nesting under the process working directory.

Verification:

- Build passes with `cmake --build build`.
- RT showcase smoke produced a fresh frame report with no frame-contract
  warnings and no health warnings.
- Latest smoke classification: 26 depth-writing renderables and 5
  depth-tested no-write renderables: 20 opaque, 6 double-sided opaque, 4
  transparent, 1 water, 0 overlays.

Next implementation pass:

- Promote visibility/culling to the same contract level: HZB validity, culling
  history age, conservative first-frame gates, and explicit warnings when
  occlusion data is stale or unavailable.
- Add per-pass draw counters to the frame report so the depth contract can be
  compared against actual submitted draw counts.
- Begin extracting pass-local state mutation into small pass records once the
  report can prove which passes own each resource transition.

## Implementation Notes: Culling And HZB Contract Pass

Status: implemented and smoke-tested.

Changes made:

- Extended `FrameContract::CullingInfo` with visibility-buffer planned/rendered
  state, HZB resource validity, HZB capture validity, HZB dimensions, mip count,
  capture frame, capture age, and whether HZB occlusion was used by the
  visibility-buffer or indirect GPU-culling path.
- Reset per-frame HZB-occlusion usage flags at frame entry and set them at the
  exact dispatch sites that choose to consume the previous-frame HZB.
- Refreshed dynamic culling/HZB contract data at end-of-frame, because the
  original frame snapshot is taken before pass execution and cannot know yet
  whether VB rendered or HZB was rebuilt.
- Added warnings for planned-but-not-rendered VB, HZB marked valid without a
  capture, HZB occlusion used without a valid resource/capture, and stale HZB
  captures.

Verification:

- Build passes with `cmake --build build`.
- RT showcase smoke produced a fresh frame report with no frame-contract
  warnings and no health warnings.
- Latest smoke culling state: VB planned/rendered=true, HZB valid=true,
  capture=true, age=0, size=1088x612, mips=11, VB HZB occlusion used=true,
  indirect GPU-culling HZB occlusion used=false.

Next implementation pass:

- Add actual submitted draw counters for depth prepass, shadow pass, visibility
  buffer, opaque, transparent, water, overlays, and particles.
- Compare draw counters against renderable depth classes and culling totals in
  the frame contract.
- Start converting pass-local resource assumptions into explicit pass records:
  inputs, outputs, expected states, draw count, and fallback path.

## Implementation Notes: Draw Counts And Pass Contract Pass

Status: implemented and smoke-tested.

Changes made:

- Added `FrameContract::DrawCounts` for depth prepass, shadows, opaque,
  visibility-buffer instances/meshes/batches, indirect execution, overlays,
  water, transparent geometry, particles, and debug lines.
- Incremented draw counters at the actual submission sites instead of estimating
  from scene contents.
- Added conservative validation that compares submitted draws against renderable
  depth classes and warns when a draw family has no matching pass record.
- Added `FrameContract::PassRecord` and now records the high-level frame
  sequence with pass name, planned/executed state, fallback state, draw count,
  declared reads, and declared writes.
- Covered the current RT showcase frame path: depth prepass, RT shadows/GI,
  shadows, skybox, visibility buffer, overlays, water, transparent, RT
  reflections, motion vectors, TAA, SSR, particles, SSAO, bloom, HZB,
  post-process, RenderGraph end-frame execution, and debug lines.
- Serialized `frame_contract.draw_counts` and `frame_contract.passes` into the
  diagnostic frame report.

Verification:

- Build passes when run from a Visual Studio developer environment:
  `VsDevCmd.bat -arch=x64 -host_arch=x64` followed by
  `cmake --build build --config Release --target CortexEngine`.
- A plain shell build can find `cl.exe` but not the MSVC/Windows include paths,
  so it fails before compiling renderer code; this is an environment setup issue.
- RT showcase smoke produced a fresh frame report with no frame-contract
  warnings and no health warnings.
- Latest smoke draw counts: 26 depth-prepass draws, 104 shadow draws, 26
  visibility-buffer instances across 13 meshes and 14 batches, 1 water draw, 1
  transparent draw, 0 particle draws, and 1 debug-line draw with 6 vertices.
- Latest smoke pass contract records 19 high-level passes, including HZB and
  post-process through the RenderGraph end-frame path.

Next implementation pass:

- Add expected resource-state classes to pass records so reads/writes describe
  both the logical resource and required D3D12 state.
- Promote resource transition ownership out of ad hoc helper code into a pass
  boundary contract where possible.
- Add budget records beside pass records: estimated bandwidth, render target
  footprint, transient descriptor usage, and whether a pass is full-resolution,
  half-resolution, or history-dependent.

## Implementation Notes: Pass State And Budget Contract Pass

Status: implemented and smoke-tested.

Changes made:

- Added structured resource access records beside each pass's legacy read/write
  name lists.
- Each pass read/write now carries an expected state class such as
  `constant_buffer`, `shader_resource`, `depth_write`,
  `depth_read_or_shader_resource`, `unordered_access`, or
  `render_target_to_present`.
- Added per-pass budget metadata:
  - estimated write footprint in MB,
  - resolution class,
  - full-screen classification,
  - history dependency,
  - ray-tracing dependency,
  - RenderGraph ownership.
- The frame-contract validator now checks for missing state-contract metadata,
  missing resolution classes, and invalid budget values.
- RenderGraph-owned HZB and post-process records are now explicitly marked as
  RenderGraph-owned, separate from the coarse `RenderGraphEndFrame` execution
  record.

Verification:

- Build passes from the Visual Studio developer environment.
- RT showcase smoke produced a fresh frame report with no frame-contract
  warnings and no health warnings.
- Latest smoke recorded 19 high-level passes. Example budget records:
  - `DepthPrepass`: render resolution, 2.54 MB write footprint.
  - `RTShadowsGI`: reduced resolution, 1.91 MB write footprint.
  - `ShadowPass`: shadow-map resolution, 16.00 MB write footprint.
  - `VisibilityBuffer`: render resolution, 10.16 MB write footprint.
  - RenderGraph-owned `HZB`: depth pyramid, 2.25 MB write footprint.
  - RenderGraph-owned `PostProcess`: presentation resolution, 3.52 MB write
    footprint.

Next implementation pass:

- Start moving pass-specific transition assumptions toward a typed pass boundary
  API so the renderer can compare declared state classes against actual tracked
  D3D12 states.
- Add descriptor usage deltas per pass, starting with transient CBV/SRV/UAV
  allocation pressure.
- Add a compact pass-budget summary to diagnostics/HUD once the report format
  is stable.

## Implementation Notes: Per-Pass Descriptor Pressure Pass

Status: implemented and smoke-tested.

Changes made:

- Added per-pass descriptor usage snapshots to `FrameContract::PassRecord`.
- Each pass now reports:
  - RTV descriptors used,
  - DSV descriptors used,
  - shader-visible CBV/SRV/UAV descriptors used,
  - shader-visible delta since the previous recorded pass,
  - transient CBV/SRV/UAV descriptors used,
  - transient delta since the previous recorded pass,
  - staging CBV/SRV/UAV descriptors used,
  - staging delta since the previous recorded pass.
- Added a renderer helper for capturing descriptor usage so the pass contract
  and frame-level descriptor report use the same source of truth.
- Reset the per-pass descriptor baseline after frame setup, material descriptor
  prewarming, and frame constants. This prevents setup work from being charged
  to the first render pass.
- Serialized descriptor pressure under each `frame_contract.passes[]` entry.

Verification:

- Build passes from the Visual Studio developer environment.
- RT showcase smoke produced a fresh frame report with no frame-contract
  warnings and no health warnings.
- Latest descriptor-pressure hot spots by transient descriptor delta:
  - `HZB`: +154 transient descriptors.
  - `VisibilityBuffer`: +18 transient descriptors.
  - `SSAO`: +14 transient descriptors.
  - `Bloom`: +12 transient descriptors.
  - `SSR`: +10 transient descriptors.

Next implementation pass:

- Use the new descriptor-pressure data to reduce per-frame transient descriptor
  churn, starting with HZB mip descriptors and repeated full-screen pass tables.
- Add pass-level descriptor budget warnings once stable per-scene baselines are
  known.
- Start introducing reusable descriptor tables for passes whose bindings are
  stable across frames.

## Implementation Notes: HZB Descriptor Churn Optimization Pass

Status: implemented and smoke-tested.

Changes made:

- Added a compact HZB-only compute root signature with one SRV table and one
  UAV table.
- Routed HZB init and downsample compute pipelines through the compact root
  signature when it is available.
- Preserved the previous shared compute root signature path as a fallback if
  compact HZB root creation fails.
- Updated legacy and RenderGraph HZB dispatch paths to use the compact table
  sizes and compact root-parameter indices.
- Added a top-level `frame_contract.pass_budget_summary` block with:
  - total estimated pass write footprint,
  - full-screen pass count,
  - ray-tracing pass count,
  - RenderGraph pass count,
  - total transient descriptor delta,
  - top write-footprint passes,
  - top transient-descriptor passes.

Verification:

- Build passes from the Visual Studio developer environment.
- RT showcase smoke produced a fresh frame report with no frame-contract
  warnings and no health warnings.
- HZB transient descriptor delta dropped from +154 to +22.
- Latest pass budget summary:
  - total estimated write footprint: 92.51 MB,
  - full-screen passes: 8,
  - ray-tracing pass records: 4,
  - RenderGraph-owned pass records: 3,
  - total transient descriptor delta: 78.

Next implementation pass:

- Reduce repeated full-screen pass descriptor table churn in SSAO, bloom, SSR,
  motion vectors, and post-process.
- Add per-pass warning thresholds after collecting stable baselines across RT
  Showcase, Cornell, God Rays, and the terrain-disabled default scene.
- Begin extracting reusable descriptor-table builders so table shape and null
  descriptor population are not repeated by each pass.

## Implementation Notes: Visible Correctness and Descriptor Cleanup Pass

Status: implemented and smoke-tested.

Changes made:

- Generalized the compact compute root signature into a shared
  one-SRV/one-UAV compute root used by HZB and the SSAO async compute path.
- Converted SSR and camera-only motion-vector SRV tables to persistent
  per-frame descriptor tables with transient fallback only if persistent table
  allocation fails.
- Added material diagnostics to `frame_contract.materials`:
  - sampled visible mesh material count,
  - albedo luminance min/max/average,
  - roughness min/max/average,
  - metallic min/max/average,
  - very dark / very bright albedo counts,
  - roughness and metallic out-of-range counts,
  - alpha blend and alpha mask counts.
- Added lighting diagnostics to `frame_contract.lighting`:
  - exposure,
  - sun intensity,
  - IBL diffuse/specular intensity,
  - bloom intensity,
  - SSAO radius/bias/intensity,
  - fog density/height/falloff,
  - god-ray intensity,
  - shadow bias and PCF radius,
  - scene light counts and intensity totals.
- Corrected the RT Showcase visual baseline:
  - raised IBL and sun contribution,
  - raised exposure to 1.35 in default mode,
  - reduced bloom from the previous aggressive value to 0.28,
  - reduced SSAO intensity and radius to avoid crushed midtones,
  - reduced fog and god-ray strength while keeping both effects enabled,
  - lifted implausibly dark atrium/courtyard/gallery albedos,
  - lifted water albedo/alpha so the pool reads as reflective water instead of
    a nearly black patch.
- Prevented stale persisted debug-menu exposure and bloom values from
  overriding the RT Showcase scene-authored baseline at startup.
- Fixed GPU profiler timestamp readback synchronization:
  - the profiler now creates a readback fence,
  - signals it after the renderer submits the command list,
  - only reads timestamp query results after the queue has completed the
    resolve.
- Kept governor state in the diagnostics report, but removed governor activity
  from `health_warnings` because a render-scale governor adjustment is
  telemetry, not a correctness failure.

Verification:

- Build passes from the Visual Studio developer environment.
- RT Showcase smoke produced a fresh frame report with:
  - frame-contract warnings: 0,
  - health warnings: 0,
  - GPU frame time: 0.905216 ms,
  - total transient descriptor delta: 55.
- Descriptor churn changed from the earlier baseline:
  - total transient descriptor delta: 78 -> 55,
  - SSR transient descriptor delta: +10 -> +0,
  - SSAO transient descriptor delta: +14 -> +1,
  - motion-vector transient descriptor delta: +10 -> +2.
- Current hot spots after this pass:
  - `HZB`: +22 transient descriptors,
  - `VisibilityBuffer`: +18 transient descriptors,
  - `Bloom`: +12 transient descriptors,
  - `MotionVectors`: +2 transient descriptors,
  - `SSAO`: +1 transient descriptor.
- Material diagnostics for RT Showcase:
  - sampled materials: 31,
  - average albedo luminance: 0.873,
  - minimum albedo luminance: 0.157,
  - very dark albedo count: 0,
  - roughness out-of-range count: 0,
  - metallic out-of-range count: 0.
- Lighting diagnostics for RT Showcase:
  - exposure: 1.35,
  - bloom intensity: 0.28,
  - sun intensity: 5.2,
  - IBL diffuse/specular: 1.05 / 1.35,
  - fog density/falloff: 0.018 / 0.55,
  - god-ray intensity: 1.25,
  - SSAO radius/bias/intensity: 0.22 / 0.04 / 0.28.

Next implementation pass:

- Move the remaining descriptor hot spots into reusable descriptor-table
  builders:
  - Bloom persistent tables for downsample/upsample/combine,
  - VisibilityBuffer table ownership and null-descriptor policy,
  - HZB persistent mip descriptor tables where mip count is stable.
- Add screenshot/frame-capture validation for RT Showcase so visual regressions
  are tested with image evidence, not only JSON diagnostics.
- Start renderer-module extraction around diagnostics, descriptors, and
  post-processing now that the pass contracts expose stable boundaries.

## Implementation Notes: Renderer Consolidation Pass

Status: implemented and smoke-tested.

Changes made:

- Added persistent Bloom descriptor tables:
  - one per swap-chain frame,
  - 12 slots matching the current Bloom graph,
  - used for HDR downsample, mip downsample, horizontal/vertical blur, and
    composite draws,
  - transient fallback remains if the persistent table allocation fails.
- Added persistent compact-root HZB dispatch descriptor tables:
  - one SRV and one UAV descriptor per HZB dispatch,
  - copied from the depth SRV and HZB mip staging descriptors when HZB
    resources are created,
  - used by both the legacy HZB path and the RenderGraph HZB path when the
    compact root signature is available,
  - the old transient table path remains as the noncompact fallback.
- Added deterministic visual validation capture:
  - `CORTEX_CAPTURE_VISUAL_VALIDATION=1` now writes
    `build/bin/logs/visual_validation_rt_showcase.bmp`,
  - capture is done through GPU readback from the swap-chain back buffer before
    present,
  - this replaces the GDI screenshot path for validation because GDI/BitBlt can
    capture black frames from flip-model swap chains,
  - `frame_report_last.json` now records the capture path and whether the file
    was captured.
- Kept the manual `CaptureScreenshot()` path available for user-triggered
  screenshots, but the validation path no longer depends on it.

Verification:

- Build passes from the Visual Studio developer environment.
- RT Showcase smoke produced a fresh frame report with:
  - frame-contract warnings: 0,
  - health warnings: 0,
  - GPU frame time: 1.431552 ms,
  - total transient descriptor delta: 21.
- Descriptor churn changed from the previous visible-correctness pass:
  - total transient descriptor delta: 55 -> 21,
  - Bloom transient descriptor delta: +12 -> +0,
  - HZB transient descriptor delta: +22 -> +0 on the compact-root path.
- Current transient descriptor hot spots:
  - `VisibilityBuffer`: +18,
  - `MotionVectors`: +2,
  - `SSAO`: +1.
- Visual validation BMP:
  - path: `CortexEngine/build/bin/logs/visual_validation_rt_showcase.bmp`,
  - size: 3,686,454 bytes,
  - dimensions: 1280x720,
  - average luma: 110.26,
  - nonblack ratio: 1.0000,
  - colorful ratio: 0.6738.

Next implementation pass:

- Move VisibilityBuffer descriptor ownership into a dedicated pass-local table
  builder. This is now the dominant remaining transient descriptor source.
- Collapse the small MotionVectors and SSAO transient leftovers if they remain
  after VisibilityBuffer ownership is cleaned up.
- Extract the descriptor table helpers from `Renderer.cpp` into a dedicated
  renderer support module once the VisibilityBuffer path follows the same
  ownership pattern.
- Add image-stat thresholds to the smoke script/report so blank, black, or
  single-color validation captures fail automatically.

## Implementation Notes: VisibilityBuffer Descriptor Ownership Pass

Status: implemented and smoke-tested.

Changes made:

- Added stable per-frame descriptor-table ownership inside
  `VisibilityBufferRenderer`:
  - resolve visibility/depth SRV tables,
  - resolve G-buffer UAV tables,
  - motion-vector velocity UAV tables,
  - deferred lighting G-buffer SRV tables,
  - deferred lighting environment/shadow/BRDF SRV tables.
- Rewired material resolve, VB motion vectors, and deferred lighting to write
  descriptors into those persistent tables instead of allocating transient
  tables while the pass executes.
- Bound the persistent visibility SRV directly for VB motion vectors because a
  one-descriptor SRV handle is already a valid descriptor table.
- Left the depth debug blit transient path in place because it is debug-view
  only and does not appear in the RT Showcase default flow.

Verification:

- Build passes from the Visual Studio developer environment.
- RT Showcase smoke produced a fresh frame report with:
  - frame-contract warnings: 0,
  - health warnings: 0,
  - GPU frame time: 2.267136 ms,
  - estimated renderer memory: 172.11 MB,
  - DXGI current usage: 318.73 MB,
  - total transient descriptor delta: 2.
- Descriptor churn changed from the previous renderer consolidation pass:
  - total transient descriptor delta: 21 -> 2,
  - VisibilityBuffer transient descriptor delta: +18 -> +1,
  - MotionVectors transient descriptor delta: +2 -> +0.
- Current remaining transient descriptor hot spots:
  - `VisibilityBuffer`: +1,
  - `SSAO`: +1.
- Visual validation BMP:
  - path: `CortexEngine/build/bin/logs/visual_validation_rt_showcase.bmp`,
  - size: 3,686,454 bytes,
  - dimensions: 1280x720,
  - average luma: 110.21,
  - nonblack ratio: 1.0000,
  - colorful ratio: 0.4970.

Next implementation pass:

- Trace the final `VisibilityBuffer:+1` descriptor delta. It is no longer from
  `VisibilityBufferRenderer` transient table allocation; the likely source is
  a renderer-side late descriptor allocation or first-use helper inside the VB
  pass.
- Move SSAO to persistent descriptor-table ownership so the default RT Showcase
  flow reaches zero recurring transient descriptors.
- Convert material texture descriptor prewarming away from per-frame transient
  tables. This is outside pass-budget hot spots because it runs before recorded
  passes, but it still consumes hundreds of transient descriptors per frame and
  should become persistent/bindless-owned for the mature renderer path.
- Add automated image-stat thresholds to the frame report once the remaining
  descriptor hot spots are closed.

## Implementation Notes: Descriptor Churn Closure Pass

Status: implemented and smoke-tested.

Changes made:

- Removed SSAO transient descriptor allocation:
  - added persistent per-frame SSAO SRV and UAV descriptor tables,
  - graphics SSAO now binds depth through the current frame's persistent SRV
    table,
  - compute SSAO now binds both depth and SSAO UAV through persistent per-frame
    tables for compact and legacy root signatures.
- Removed hidden material prewarm transient churn:
  - `MaterialGPUState` now allocates persistent shader-visible descriptor slots
    once per material,
  - material descriptors are rewritten only when texture source pointers change,
  - texture descriptor indices remain stable for VB material resolve and
    forward paths.
- Traced and removed the final `VisibilityBuffer:+1` descriptor delta:
  - the remaining allocation came from `GPUCullingPipeline::DispatchCulling`,
    where HZB occlusion copied its HZB SRV through a transient slot during the
    VB pass,
  - GPU culling now owns one per-frame persistent HZB dispatch SRV descriptor
    and copies the staging SRV into that frame-local table.

Verification:

- Build passes from the Visual Studio developer environment.
- RT Showcase smoke produced a fresh frame report with:
  - frame-contract warnings: 0,
  - health warnings: 0,
  - GPU frame time: 2.152448 ms,
  - estimated renderer memory: 181.42 MB,
  - DXGI current usage: 328.46 MB,
  - total transient descriptor delta: 0.
- Descriptor hot spots are now empty in the default RT Showcase flow.
- Descriptor ownership after the pass:
  - persistent descriptors: 733,
  - shader-visible cursor: 98304,
  - transient segment: 98304..180224,
  - recorded pass transient usage: 0.
- Visual validation BMP:
  - path: `CortexEngine/build/bin/logs/visual_validation_rt_showcase.bmp`,
  - size: 3,686,454 bytes,
  - dimensions: 1280x720,
  - average luma: 110.21,
  - nonblack ratio: 1.0000,
  - colorful ratio: 0.4970.

Next implementation pass:

- Add first-class smoke automation / auto-exit support so validation runs do
  not need to kill the app after a capture is produced.
- Add image-stat thresholds to `frame_report_last.json` so blank, black, or
  single-color captures fail automatically.
- Continue the architecture cleanup by extracting descriptor table builders and
  pass-local ownership helpers out of `Renderer.cpp`.
- Start the next performance pass on ray tracing descriptor ownership and pass
  timing, since DXR still has transient descriptor paths in `DX12Raytracing.cpp`
  even though they are not currently showing up in the recorded RT Showcase
  pass deltas.

## Implementation Notes: Smoke Automation and DXR Descriptor Pass

Status: implemented and smoke-tested.

Changes made:

- Added first-class smoke automation:
  - `EngineConfig::maxFrames` exits the main loop after a bounded number of
    rendered frames,
  - `EngineConfig::exitAfterVisualValidationCapture` exits once the renderer
    writes the RT showcase visual validation capture,
  - CLI flags and environment variables now support automated validation:
    `--smoke-frames`, `--max-frames`,
    `--exit-after-visual-validation`, `CORTEX_SMOKE_FRAMES`, and
    `CORTEX_EXIT_AFTER_VISUAL_VALIDATION`.
- Removed the startup modal stall from default flow:
  - the camera-help SDL message box is now opt-in via
    `CORTEX_SHOW_CAMERA_HELP`,
  - smoke/automation runs suppress it automatically,
  - RT showcase startup dropped from a modal-blocked 97 seconds to about
    one second in the measured smoke path.
- Added visual validation image stats to the frame report:
  - BMP header/pixel parsing runs in-process,
  - report records width, height, average luma, nonblack ratio, and colorful
    ratio,
  - health warnings now flag invalid, mostly black, or too-dark captures.
- Moved DXR dispatch descriptor binding away from transient allocations:
  - sun shadows, reflections, and GI now use persistent per-frame/per-pass
    descriptor tables,
  - TLAS/depth/output/normal descriptors are copied into the current frame's
    pass table before `DispatchRays`,
  - descriptor tables are split by pass to avoid aliasing when multiple DXR
    dispatches occur in the same frame.
- Fixed SSAO async compute PSO creation:
  - the compact compute root signature now includes the required static
    sampler at `s0`,
  - `SSAO_Compute.hlsl` now creates a valid compute PSO instead of falling
    back after `E_INVALIDARG`.

Verification:

- Build passes from the Visual Studio developer environment.
- RT Showcase smoke command exits cleanly without external process kill:
  `CORTEX_CAPTURE_VISUAL_VALIDATION=1 CORTEX_DISABLE_DEBUG_LAYER=1
  build/bin/CortexEngine.exe --scene rt_showcase --mode=default --no-llm
  --no-dreamer --no-launcher --smoke-frames=240
  --exit-after-visual-validation`.
- Latest RT Showcase smoke report:
  - frame-contract warnings: 0,
  - health warnings: 0,
  - GPU frame time: 1.093632 ms,
  - estimated renderer memory: 181.42 MB,
  - DXGI current usage: 328.46 MB,
  - recorded pass transient descriptor delta: 0,
  - persistent descriptors: 759,
  - scene/camera initialization: 31 ms,
  - full engine initialization without LLM: 1033 ms.
- Visual validation image stats:
  - dimensions: 1280x720,
  - average luma: 110.19,
  - nonblack ratio: 1.0000,
  - colorful ratio: 0.7708.
- Remaining non-fatal log noise:
  - debug layer disabled warning is intentional for the smoke command,
  - one missing glTF sample asset warning comes from external
    `glTF-Sample-Models` data and is not engine-owned.

Next implementation pass:

- Make the frame report distinguish the latest active frame contract from the
  shutdown snapshot so culling readback fields do not report stale zeroes after
  teardown.
- Move one-off validation/run scripts into a documented tools folder or remove
  stale scratch scripts from the repo root.
- Continue splitting renderer subsystems out of `Renderer.cpp`, starting with
  descriptor table construction, render-target lifetime, and RT dispatch
  orchestration.

## Implementation Notes: Persistent Descriptor Contract and Smoke Report Pass

Status: implemented and smoke-tested.

Changes made:

- Split active-frame diagnostics from shutdown diagnostics:
  - `frame_report_last.json` now represents the latest active frame,
  - `frame_report_shutdown.json` records teardown state separately,
  - shutdown no longer overwrites useful culling/readback data with stale
    teardown values.
- Removed remaining descriptor-ring escape hatches in the default RT showcase
  path:
  - VisibilityBuffer depth debug blit now owns persistent per-frame SRV
    descriptors,
  - optional RT reflection debug clears now use persistent per-frame UAV
    descriptors,
  - SSR, TAA, motion vectors, post-process, and HZB now require persistent
    descriptor tables and report a contract error instead of allocating hidden
    transient descriptor tables.
- Made GPU culling telemetry useful in normal builds:
  - command-count readback now populates valid summary stats without requiring
    `CORTEX_DEBUG_CULLING`,
  - detailed frustum/occlusion counters remain opt-in.
- Tightened smoke automation:
  - renderer visual-capture state is exposed to the engine,
  - `--exit-after-visual-validation` now waits a few settle frames so GPU
    timestamp and culling readbacks are valid,
  - the active report is written before exit and no longer needs the
    max-frame fallback.
- Cleaned compiler warning noise:
  - Win32 control IDs now cast through `INT_PTR`,
  - UI string helpers use Win32 UTF-8 conversion instead of narrowing
    `wchar_t`,
  - remaining unused/narrowing warnings in renderer/LLM paths were removed.

Verification:

- Release build passes from the Visual Studio developer environment with no
  compiler warnings.
- `Select-String` over the checked graphics paths finds no
  `AllocateTransientCBV_SRV_UAV` calls.
- RT Showcase smoke command:
  `CORTEX_CAPTURE_VISUAL_VALIDATION=1 CORTEX_DISABLE_DEBUG_LAYER=1
  build/bin/CortexEngine.exe --scene rt_showcase --mode=default --no-llm
  --no-dreamer --no-launcher --smoke-frames=240
  --exit-after-visual-validation`.
- Latest active-frame report:
  - frame-contract warnings: 0,
  - health warnings: 0,
  - GPU frame time: 0.894976 ms,
  - total frames before smoke exit: 4,
  - estimated renderer memory: 181.42 MB,
  - DXGI current usage: 328.46 MB,
  - recorded pass transient descriptor delta: 0,
  - GPU culling summary: tested 26, visible 3.
- Visual validation image stats:
  - dimensions: 1280x720,
  - average luma: 110.19,
  - nonblack ratio: 1.0000,
  - colorful ratio: 0.7708.

Next implementation pass:

- Extract descriptor table setup and render-target lifetime code out of
  `Renderer.cpp` into focused renderer support modules.
- Add a scripted smoke/diagnostics runner under `CortexEngine/tools` so this
  validation path is repeatable without ad hoc root scripts.
- Start visual correctness work on the RT showcase itself: material calibration,
  exposure/tonemapping review, and focused debug views for RT reflection/GI
  contribution.

## Implementation Notes: RT Showcase Smoke Budget and Descriptor Module Pass

Status: implemented and smoke-tested.

Changes made:

- Added a repeatable RT showcase validation tool:
  - `CortexEngine/tools/run_rt_showcase_smoke.ps1`,
  - builds Release by default,
  - runs the RT showcase with visual validation enabled,
  - validates `frame_report_last.json` instead of relying on manual visual
    inspection.
- Added a CMake target for the same validation path:
  - `cortex_smoke_rt_showcase`,
  - depends on the Release executable,
  - runs the tool with `-NoBuild` after the target is current.
- Added explicit smoke budgets that match the current low-memory direction:
  - GPU frame time <= 16.7 ms,
  - DXGI current usage <= 512 MB,
  - estimated renderer memory <= 256 MB,
  - render-target memory <= 160 MB,
  - estimated pass write bandwidth <= 128 MB/frame,
  - persistent descriptors <= 1024,
  - staging descriptors <= 128.
- Moved TAA resolve and post-process descriptor table ownership out of
  `Renderer.cpp` into `Renderer_Descriptors.cpp`.
- Updated stale initialization wording so descriptor-table failures no longer
  imply a transient-descriptor fallback path.

Why this matters:

- The project now has an executable definition of "RT showcase is healthy"
  covering correctness, visual capture, culling telemetry, startup frame count,
  GPU timing, memory, bandwidth, and descriptor budgets.
- Future material, RT, god-ray, or scene-quality work can be judged against a
  fixed budget instead of relying on subjective smoothness or task-manager
  observations.
- The renderer split has started at a contract boundary rather than by random
  file size: descriptor table setup is now isolated and can be expanded into a
  fuller descriptor/resource binding module.

Next implementation pass:

- Continue renderer decomposition with render-target lifetime and resize-owned
  resources:
  - depth,
  - HDR color,
  - normal/roughness G-buffer,
  - TAA history/intermediate,
  - SSR color,
  - velocity,
  - HZB,
  - RT shadow/reflection/GI buffers.
- While extracting, preserve descriptor-slot reuse across resizes and make any
  descriptor reallocation visible in the frame contract.
- After render-target extraction, start visual correctness work from the measured
  RT showcase:
  - material energy ranges,
  - exposure and tonemapping,
  - RT reflection/GI contribution isolation,
  - god-ray intensity and depth dependence.

Verification:

- Release build passes from the Visual Studio developer environment.
- Direct tool run passes:
  `powershell -ExecutionPolicy Bypass -File tools/run_rt_showcase_smoke.ps1
  -NoBuild`.
- CMake target run passes:
  `cmake --build build --config Release --target cortex_smoke_rt_showcase`.
- Latest budgeted smoke metrics:
  - total frames: 4,
  - GPU frame time: 0.717 ms / 16.7 ms budget,
  - DXGI current usage: 328.46 MB / 512 MB budget,
  - estimated renderer memory: 181.42 MB / 256 MB budget,
  - render-target memory: 114.63 MB / 160 MB budget,
  - estimated pass write bandwidth: 92.51 MB/frame / 128 MB/frame budget,
  - average luma: 110.19,
  - transient descriptor delta: 0.

## Implementation Notes: RT Showcase Startup Render-Scale Pass

Status: implemented and smoke-tested.

Changes made:

- Pre-seeded the RT showcase render scale before `Renderer::Initialize()`:
  - default RT showcase starts renderer-owned resources at 0.85 scale,
  - conservative RT showcase starts renderer-owned resources at 0.67 scale.
- Preserved full-scale startup for non-RT-showcase scenes.
- Fixed conservative RT showcase scene setup so it no longer overwrites the
  safe 0.67 render scale with 1.0 after renderer initialization.

Why this matters:

- Before this pass, the RT showcase initialized depth/HDR/VB/post resources at
  one size and then immediately resized them on the first frame after scene
  setup changed `renderScale`.
- That forced an early `WaitForGPU()` and major render-target reallocation in
  `BeginFrame`, exactly where startup hitching and transient VRAM spikes are
  most visible.
- The startup log now shows resources created directly at 1088x612 for the
  default 1280x720 RT showcase smoke run, with no first-frame
  `BeginFrame: reallocating render targets` or `BeginFrame: recreating ...`
  messages.

Verification:

- Release build passes from the Visual Studio developer environment.
- Budgeted RT showcase smoke passes:
  - total frames: 4,
  - GPU frame time: 0.706 ms / 16.7 ms budget,
  - DXGI current usage: 328.46 MB / 512 MB budget,
  - estimated renderer memory: 181.42 MB / 256 MB budget,
  - render-target memory: 114.63 MB / 160 MB budget,
  - estimated pass write bandwidth: 92.51 MB/frame / 128 MB/frame budget,
  - transient descriptor delta: 0.
- Log inspection confirms:
  - `Renderer startup render scale preset for RT showcase: 0.85`,
  - `VisibilityBuffer initialized (1088x612, max 65536 instances)`,
  - `HDR target created: 1088x612 (scale 0.85)`,
  - no first-frame render-target reallocation log.

## Implementation Notes: Render-Target and Frame-Lifecycle Split Pass

Status: implemented and smoke-tested.

Changes made:

- Split render-target ownership out of `Renderer.cpp` into
  `Renderer_RenderTargets.cpp`:
  - depth buffer,
  - HZB texture and HZB build path,
  - shadow map atlas,
  - RT shadow mask,
  - RT GI buffers,
  - RT reflection buffers,
  - HDR color, normal/roughness G-buffer, TAA, SSR, velocity, bloom/SSAO
    dependent target creation.
- Split frame lifecycle ownership out of `Renderer.cpp` into
  `Renderer_FrameLifecycle.cpp`:
  - `BeginFrame`,
  - `EndFrame`,
  - visual validation BMP capture helpers used by end-of-frame smoke capture.
- Extended the RT showcase smoke tool so startup render-target reallocation is a
  contract failure by default:
  - detects `BeginFrame: reallocating render targets`,
  - detects depth/HDR/SSAO first-frame recreation logs,
  - reports `startup_realloc=0` on success.
- Repaired renderer helper ownership after the split:
  - restored local resource-size estimation used by VRAM diagnostics,
  - restored bounds-proxy mesh fallback used by oversized mesh upload handling.

Why this matters:

- The monolithic renderer is now smaller at two major architectural seams:
  descriptor binding and render-target/frame lifecycle ownership.
- The startup-scale fix is now enforced by automation, so this specific class of
  startup hitch/regression cannot silently return.
- Resource creation and frame presentation are now easier to reason about before
  the next work on material/lighting correctness.

Verification:

- Release build passes from the Visual Studio developer environment.
- Budgeted CMake smoke target passes:
  `cmake --build build --config Release --target cortex_smoke_rt_showcase`.
- Latest smoke metrics:
  - total frames: 4,
  - GPU frame time: 0.708 ms / 16.7 ms budget,
  - DXGI current usage: 328.46 MB / 512 MB budget,
  - estimated renderer memory: 181.42 MB / 256 MB budget,
  - render-target memory: 114.63 MB / 160 MB budget,
  - estimated pass write bandwidth: 92.51 MB/frame / 128 MB/frame budget,
  - average luma: 110.19,
  - transient descriptor delta: 0,
  - startup render-target reallocations: 0.
