# Full Scene Shader Pipeline V2

This is the living plan and completion ledger for moving CortexEngine from the
stable Scene-Local Cinematic Renderer V1 into a full-scene AAA shader pipeline.

The purpose is not to hide the remaining asset/blockout ceiling with blur,
post-process, or per-scene tweaks. The purpose is to build a renderer contract
where materials, lighting, reflections, shadows, temporal accumulation, and
post-processing are explicit, scene-owned, debuggable, and enforceable through
packets.

## Current Baseline

Renderer V1 is the baseline to preserve:

- `SceneCinematicProfile` controls scene-local environment, lighting,
  reflection, temporal, material, water, and post policy.
- Enclosed scenes no longer rely on visible random HDRI backgrounds.
- Reflection-owner, material-class, stability, and visual-quality packet
  analyzers exist.
- The final V1 seq8 packet is review-ready, but the beauty sheet still exposes
  stylized/blockout assets and limited material richness.

V2 starts from that baseline. A V2 change is valid only if it preserves the V1
hard gates or replaces them with stronger evidence.

## Non-Goals

- Do not restart scene authoring automation in this renderer plan.
- Do not hand-polish a single scene as proof of a general shader system.
- Do not turn off IBL, shadows, reflections, or temporal history to hide a bug.
- Do not claim AAA quality because a narrow screenshot looks better.
- Do not accept whole-scene neural renders as final engine output. External
  models may propose materials, textures, meshes, or lighting metadata, but the
  engine must own the runtime scene graph and validation.

## Target Outcome

V2 is acceptable when the engine can render at least gallery, kitchen, office,
gym, and concert with:

- full PBR material models and texture evidence for hero surfaces.
- scene-local light rigs with semantic ownership.
- local reflection probes or planar probes for enclosed scenes.
- stable shadows/contact shadows under camera motion.
- temporal accumulation that is material-aware and stable under mouse jitter and
  camera sweeps.
- cinematic HDR post without clipping or crush regressions.
- render graph pass contracts that make resource ownership and pass order
  explicit.
- frame reports and packet analyzers that can prove the above without relying
  on manual screenshot interpretation.

## Architecture

V2 is organized as contracts, not scattered features.

1. Material Contract
   - Every visible renderable resolves to a `FullSceneMaterialModel`.
   - The model records shader family, texture slots, procedural detail,
     reflection behavior, temporal behavior, and admission evidence.
   - Asset Registry V2 must eventually declare which material model each
     imported asset uses.

2. Geometry And GBuffer Contract
   - The visibility/deferred path emits enough data for final lighting:
     albedo, world normal, roughness, metallic, AO, emissive, material id,
     object id, velocity, clearcoat, transmission, opacity, and optional detail
     masks.
   - The GBuffer must carry named scene material policy data, not just broad
     surface class. `MaterialExt2.w` is the current bridge for encoded scene
     material class so lighting, reflections, temporal, and post can agree on
     whether a pixel is wall paint, ceramic tile, wood, metal, glass, fabric,
     wet surface, neon, screen, water, mirror, and related classes.
   - Debug views must expose each channel.

3. Scene-Local Lighting Contract
   - Scene profiles declare semantic rigs, not raw ad hoc lights.
   - Supported rig elements: sun/directional, rect area, spot, point, emissive
     cards, practical fixtures, high-bay lights, stage beams, and display
     lights.
   - Each light has owner id, purpose, intensity units or normalized artistic
     exposure, shadow policy, color temperature, and affected zones.
   - Forward shading, procedural sky, and water shaders must use the
     scene-local ambient/background/reflection contract. A scene-owned sky or
     water surface cannot behave like a detached generic HDRI backdrop.

4. Reflection And Indirect Contract
   - Enclosed scenes use local room probes, hero probes, planar probes, or
     neutral fallback with explicit ownership.
   - Visible external HDRI is allowed only for authored outdoor/gallery cases.
   - Ray-traced reflection misses must follow the scene-local environment
     contract. In an authored enclosed scene with zero background exposure, a
     miss may not synthesize a visible sky/office/HDRI lobe; it must return an
     owned neutral fallback or local ambient term.
   - Reflection-owner debug remains mandatory.

5. Shadow Contract
   - Directional cascades, local shadow atlas, contact shadows, and optional
     RT shadows must report readiness independently.
   - Bias, softness, filter radius, cascade selection, and contact terms must
     be visible in debug views.

6. Temporal Contract
   - Motion vectors are first-class for camera and objects.
   - TAA/history rejection is material-aware.
   - Temporal rejection and TAA resolve must use the same jitter-aware history
     coordinate contract. A mask that tests `uv + velocity` while resolve uses
     `uv + velocity + jitter_delta` is invalid because it will over-reject
     stable surfaces during mouse rotation and make reflections/materials pop.
   - Smooth metals, glass, water, emissive, and high-frequency normal maps
     receive stricter history clamps.

7. Post Contract
   - HDR tone mapping, exposure, bloom, color grade, highlight rolloff, dark
     lift, and sharpening/clarity are profile-owned.
   - Post layers cannot use frame-varying noise unless the stability analyzer
     has an explicit gate for it.

8. Render Graph Contract
   - Pass order, resources, states, and debug labels are declared in one graph.
   - Passes include:
     `ShadowPrepass`, `DepthPrepass`, `VisibilityResolve`, `GBufferResolve`,
     `Lighting`, `LocalReflections`, `Transparent`, `Temporal`, `Post`,
     `DebugComposite`, and `FrameReport`.

## Refactor Execution Blueprint

This refactor should move like a renderer migration, not like visual tuning.
The current V1 path remains the playable fallback until V2 proves equivalent
or stronger gates. Every phase must ship with a visible debug mode, frame-report
fields, and a packet gate before its beauty output becomes trusted.

The whole-renderer implementation plan now lives in
`docs/FULL_SCENE_SHADER_REFACTOR_MASTER_PLAN.md`. Use that document for the
architecture boundaries, implementation order, pseudocode, and first concrete
feature slice. Keep this file as the phase ledger and evidence record.

### Master Refactor Plan

The V2 refactor has one central rule: the final beauty pixel must be assembled
from scene-owned facts, not pass-local guesses. Today, several systems can still
make independent decisions about material meaning, environment lighting,
reflection fallback, temporal rejection, and post treatment. That is the ceiling
that keeps the renderer below Unreal-like full-scene quality. The refactor must
turn those independent choices into a single frame contract.

The target runtime architecture is:

```text
ScenePreset / SceneGraph / AssetRegistry
  -> SceneVisualContract
  -> FullSceneMaterialTable
  -> FullSceneLightRig
  -> FullSceneProbeSet
  -> FullSceneRenderGraph
  -> FullSceneFrameData / GBuffer
  -> Lighting + Reflections + Shadows
  -> Material-aware Temporal
  -> HDR Presentation
  -> Evidence Packet + Beauty
```

Each layer has a narrow job:

- `SceneVisualContract` defines the place: enclosed/outdoor, mood, allowed
  background visibility, lighting family, reflection family, temporal family,
  post family, and the scene family being rendered.
- `FullSceneMaterialTable` defines what things are: shader family, authored
  parameters, texture slots, procedural detail, material class, reflectance
  behavior, transparency/transmission behavior, and temporal behavior.
- `FullSceneLightRig` defines why light exists: key, fill, practical, accent,
  display, stage, sun, skylight, high-bay, or emergency/fallback.
- `FullSceneProbeSet` defines where indirect and reflection energy comes from:
  room probes, hero probes, planar probes, SSR, RT, neutral fallback, or visible
  external environment. Unauthorized environment fallback is a bug.
- `FullSceneRenderGraph` defines pass order and resource ownership. A debug
  view must know which pass produced it and which resources it consumed.
- `FullSceneFrameData` carries compact per-pixel truth: object id, material id,
  material family, normal, velocity, roughness, metallic, AO, emissive,
  clearcoat/transmission, opacity, shadow policy, reflection policy, and
  temporal policy.
- The final lighting/temporal/post passes consume those facts. They do not
  infer scene intent from whether an IBL, texture, or fallback buffer happens
  to be bound.

Promotion works in four explicit stages:

1. `Instrumented`
   - The domain reports owner, fallback owner, missing evidence, and readiness.
   - Beauty still uses the V1 path.
2. `Shadow Output`
   - The V2 domain runs beside V1 and exports debug/compare views.
   - Beauty still uses V1 unless a debug flag explicitly selects V2.
3. `Candidate`
   - V2 can render beauty for selected scenes, but packet gates still compare
     against V1 stability and visual contracts.
4. `Default Ready`
   - V2 becomes the default only after cross-family packets pass and the user
     accepts that the visual direction is materially better.

The order of work is intentionally data-first:

1. Make material truth real.
   - Runtime materials must stop being broad style hints. Every visible object
     needs a `FullSceneMaterialModel` with family, texture/procedural evidence,
     reflectance/transmission, temporal policy, and post sensitivity.
   - This is the prerequisite for good metal, glass, tile, wood, fabric, neon,
     water, mirror, and wall paint.
2. Make the frame carry that truth.
   - Extend/debug the GBuffer and visibility data so lighting, reflections,
     shadows, temporal, and post all agree on the same material/object ids.
3. Make lighting scene-local.
   - Replace ad hoc brightness with semantic rigs and stable units/contracts:
     kitchen practicals, gym high-bays, concert beams, office soft panels,
     gallery spots, outdoor sun/sky.
4. Make reflections local and owned.
   - Add room/hero/planar probe ownership and keep enclosed scenes from
     reflecting unrelated HDRI content.
5. Make shadows/contact stable.
   - Normalize bias/filtering/contact policy through the same scene contract.
     This includes small object contacts and smooth camera motion stability.
6. Make temporal material-aware.
   - Glass, water, polished metal, emissive signs, patterned tile, and matte
     walls need different history confidence and clamp behavior.
7. Make post a named HDR pipeline.
   - Exposure, rolloff, bloom, color grade, clarity, and sharpening must be
     explicit stages with packet-visible metrics.
8. Move pass ownership into a render graph.
   - Once the domains exist, the graph becomes the enforcement mechanism:
     missing producers, stale resources, wrong pass order, or unowned debug
     views fail validation.
9. Run cross-family promotion.
   - Gallery alone is insufficient. The gate must include gallery, kitchen,
     office, gym, concert, plus at least one wet/glass-heavy scene.

The acceptance bar for this plan is not "a nicer screenshot." The acceptance
bar is that the renderer can explain the sources of material, light,
reflection, shadow, temporal history, and post for the final image, and that
the evidence stays stable under camera motion.

### Whole-Renderer Refactor Strategy

The target is not a larger collection of nicer shaders. The target is a
scene-owned rendering architecture where every final pixel can be traced back
to a material model, lighting rig, reflection source, shadow policy, temporal
policy, and post profile. Unreal-like quality comes from those systems agreeing
on the same scene facts, not from a single bloom, IBL, or roughness tweak.

The refactor is split into six ownership layers:

1. Scene visual contract layer
   - Owns the high-level intent for the frame: scene family, enclosure type,
     environment owner, local/visible background policy, lighting mood,
     reflection policy, temporal policy, and post grade.
   - This is the only layer allowed to decide whether a scene is gallery,
     kitchen, office, gym, concert, outdoor, wet, neon, daylight, or enclosed.
   - Existing entry points: `SceneCinematicProfile`,
     `RendererSceneProfile`, `FrameContract::SceneVisualInfo`, and the
     scene-local renderer V1 packet tools.

2. Material and asset evidence layer
   - Owns what every renderable is made of.
   - It must combine Asset Registry V2 evidence, authored material settings,
     imported texture slots, procedural detail, and semantic scene role into a
     single `FullSceneMaterialModel`.
   - Later shading passes are not allowed to guess that a pixel is tile,
     glass, brushed metal, wall paint, fabric, water, or neon from roughness
     alone.

3. Frame data and GBuffer layer
   - Owns the compact per-pixel facts that shaders need: material family,
     object id, material id, velocity, normal, roughness, metallic, AO,
     emissive, clearcoat, transmission, detail masks, and policy ids.
   - This layer is the bridge from scene semantics to shader math. It must stay
     append-only until every packet gate proves the new channels are valid.

4. Scene illumination layer
   - Owns all direct, indirect, reflection, probe, and sky/background energy.
   - It should expose a semantic light rig, local probes, planar/hero probes,
     shadow policy, and fallback ownership.
   - Enclosed scenes should read as lit by their own room, not by a leftover
     HDRI. Outdoor/gallery scenes can still intentionally expose external IBL.

5. Stability and composition layer
   - Owns temporal history, motion-vector validation, material-aware history
     rejection, shadow stability, reflection stability, exposure stability, and
     camera packet stability.
   - This layer prevents the renderer from producing one good still while
     breaking during mouse-look or camera sweeps.

6. Presentation and evidence layer
   - Owns tone map, color grade, bloom, clarity, debug atlases, frame reports,
     packet comparison, and promotion gates.
   - No V2 domain is accepted because the beauty image looks better once. It is
     accepted only when evidence says the frame is owned, stable, and
     comparable across scene families.

### Refactor Boundaries

These boundaries are intended to stop V2 from becoming another scattered set of
scene-specific patches:

- `RendererSceneProfile` and `SceneCinematicProfile` decide scene intent.
- `MaterialModel` and Asset Registry V2 decide material truth.
- Visibility/GBuffer code only encodes facts; it should not decide artistic
  lighting.
- Lighting shaders consume material and scene policy; they should not infer
  enclosure/background ownership from whether a texture happens to be bound.
- Reflection shaders choose from declared reflection sources; they should not
  synthesize external environment misses in enclosed scenes.
- Temporal code consumes material/object/policy ids; it should not use one
  global clamp for glass, water, brushed metal, emissive, and wall paint.
- Post code consumes an HDR profile; it should not hide lighting or material
  problems with uncontrolled crushing, sharpening, or bloom.
- Packet tools own promotion; screenshots are review material, not proof.

### Data Flow Target

The intended frame path is:

```text
scene preset / scene graph / asset registry
  -> SceneVisualContract
  -> FullSceneShaderFrameContext
  -> FullSceneMaterialTable
  -> FullSceneLightRig + FullSceneProbeSet + FullScenePostProfile
  -> Visibility / GBuffer facts
  -> Lighting + Reflections + Shadows
  -> Material-aware Temporal
  -> HDR Post
  -> Debug Atlases + Frame Report + Beauty Output
```

The important change is that the renderer stops letting each pass invent its
own local interpretation of the scene. The scene contract and material table
become the shared source of truth.

### Refactor Implementation Tracks

Track A: contracts and facades

- Add `FullSceneShaderFrameContext` as a per-frame facade.
- Populate it from existing V1 profile data first.
- Emit frame-report ownership for every field, including fallback owner.
- Do not change beauty output until ownership reports are complete.

Track B: material truth

- Convert asset/material evidence into runtime material models.
- Add missing texture-slot and procedural-detail evidence to frame reports.
- Preserve existing scene material class ids while adding richer families and
  feature bits.
- Add debug views for material family, policy id, texture readiness, and
  temporal class.

Track C: GBuffer and debug surfaces

- Inventory every render target and shader channel.
- Add object/material/velocity/policy ids in a measured way.
- Make debug views and packet captures available before lighting depends on
  the new channel.

Track D: lighting, probes, and reflections

- Promote semantic light rigs out of ad hoc scene setup.
- Add local room/hero/planar probe ownership for enclosed scenes.
- Route RT/SSR/probe/environment reflection choices through a single reflection
  source resolver.
- Keep sharp external IBL valid for gallery/outdoor scenes, but make it
  explicit and measurable.

Track E: shadows and temporal stability

- Normalize shadow bias/filter/contact policy by scene scale, receiver class,
  and light type.
- Add material/object-aware temporal rejection and clamp widths.
- Add mouse-jiggle, camera-sweep, and smooth-metal/glass/water packet gates.

Track F: post, render graph, and promotion

- Split post into named HDR stages: exposure, bloom, rolloff, tone map, grade,
  clarity, output encode.
- Move pass/resource ownership into an explicit render graph contract.
- Add a cross-family packet command that captures beauty, material, GBuffer,
  lighting, reflection, shadow, temporal, post, and frame-report evidence.

### Promotion Ladder

V2 should move through these promotion states:

| State | Meaning | Beauty Output |
|---|---|---|
| `planned` | contract exists, no runtime evidence | V1 |
| `instrumented` | runtime emits ownership/evidence, still using V1 output | V1 |
| `shadow_output` | V2 domain computes/debugs beside V1 | V1 |
| `candidate` | V2 domain can drive beauty under a flag | optional V2 |
| `packet_passed` | packet gates pass on at least one target family | opt-in V2 |
| `cross_family_passed` | gallery/kitchen/office/gym/concert pass | preset V2 |
| `default_ready` | V1 gates preserved and V2 gates stronger | default V2 |

Any domain can be rolled back independently. A failed domain must still emit
its failed evidence so the packet explains why it did not promote.

### Target File Ownership Map

The likely code ownership map for the refactor is:

| Area | Current Files | V2 Direction |
|---|---|---|
| scene visual contract | `RendererSceneProfile.*`, `RendererControlApplier_*`, `Engine_Scenes.cpp` | centralize profile application and scene-owned visual intent |
| material model | `MaterialModel.*`, `MaterialState.h`, `Renderer_Materials.cpp`, asset registry tools | richer runtime material families and texture evidence |
| GBuffer/VB | `VisibilityBuffer_*`, `MaterialResolve.hlsl`, `DeferredLighting.hlsl` | explicit material/object/policy/velocity facts |
| environment/reflection | `Renderer_Environment.cpp`, `RendererEnvironmentState.h`, `RaytracedReflections.hlsl`, `PostProcess.hlsl` | one reflection-source resolver and local probe ownership |
| lighting/shadow | `Renderer_FrameLightingConstants.cpp`, `Renderer_ShadowPass.cpp`, `Renderer_RenderGraphDepthShadow.cpp`, lighting HLSL | semantic rigs, stable shadow policy, debug owners |
| temporal | `TemporalRejectionMask.*`, temporal shader code, `Renderer_FrameEnd.cpp` | material-aware rejection, clamp, and packet capture |
| post | `PostProcess.hlsl`, post constants, UI controls | named HDR stages and profile-owned color pipeline |
| frame reports | `FrameContract*`, packet analyzers, V2 validators | promotion gates and cross-family evidence |

### First Feature Slice After Planning

The first implementation slice should not be a beauty tweak. It should be the
runtime facade and packet skeleton:

1. Add `FullSceneShaderFrameContext` and populate it from current V1 scene
   profile state.
2. Add per-domain enable/promotion state:
   `material`, `gbuffer`, `lighting`, `reflection`, `shadow`, `temporal`,
   `post`, and `render_graph`.
3. Emit frame-report JSON with owner, fallback owner, readiness, and failure
   reason for each domain.
4. Add a packet command that captures one scene with beauty plus debug atlases.
5. Keep beauty output on V1 until this instrumentation proves the renderer can
   explain its own pixels.

### Migration Shape

1. Add V2 contracts beside V1.
   - Introduce `FullSceneShaderPipelineV2` data shapes in frame reports and
     tools before changing shader output.
   - Keep current V1 scene profiles, material class analyzers, stability
     checks, and visual-quality packets running.
   - Dual-write V1 and V2 evidence where possible so a regression has a direct
     comparison point.

2. Build a runtime facade.
   - Route material resolution, lighting selection, reflection ownership,
     temporal policy, and post profile through named facade calls.
   - Facades may call existing V1 code at first.
   - The point is to make ownership explicit before replacing internals.

3. Replace one domain at a time.
   - Material model first, because every later pass depends on material
     semantics.
   - GBuffer second, because lighting, reflections, temporal, and debug views
     need stable channels.
   - Lighting/reflections/shadows third, because they are the visible scene
     realism stack.
   - Temporal/post/render graph last, because they should stabilize and
     structure real signals, not compensate for missing upstream data.

4. Promote by packets, not screenshots.
   - A domain becomes default only when debug packets show complete ownership,
     mouse-jiggle stability, and cross-family coverage.
   - The beauty result can improve during development, but it cannot be the
     sole proof.

### Runtime Facade Targets

The first C++ migration slice should add small ownership facades without
rewiring every call site:

```cpp
struct FullSceneShaderFrameContext {
    SceneCinematicProfile sceneProfile;
    FullSceneMaterialTable materialTable;
    FullSceneLightRig lightRig;
    FullSceneProbeSet probeSet;
    FullSceneTemporalPolicy temporalPolicy;
    FullScenePostProfile postProfile;
    FullSceneFrameReport report;
};

FullSceneMaterialTable BuildFullSceneMaterialTable(SceneSnapshot snapshot);
FullSceneLightRig ResolveFullSceneLightRig(SceneProfile profile);
FullSceneProbeSet ResolveFullSceneProbeSet(SceneProfile profile);
FullSceneTemporalPolicy ResolveFullSceneTemporalPolicy(SceneProfile profile);
FullScenePostProfile ResolveFullScenePostProfile(SceneProfile profile);
```

Initial implementations can wrap current V1 behavior. The required change is
that the frame report can say which facade owned each decision and whether it
fell back to V1 defaults.

### Shader Data Migration

V2 shader data should be introduced as append-only channels until the contract
is proven:

| Stage | First V2 Data | Promotion Gate |
|---|---|---|
| Material resolve | material family, feature bits, texture coverage, temporal class | all visible pixels resolve to known family or declared fallback |
| GBuffer | object id, material id, velocity, clearcoat/transmission/detail masks | debug views prove nonzero coverage on target scenes |
| Lighting | semantic light id, rig id, shadow policy id | every non-ambient contribution has owner metadata |
| Reflection | reflection source id, probe id, planar/room/hero/fallback tag | shiny/glass/water pixels never sample inappropriate visible HDRI in enclosed scenes |
| Shadow | cascade/local/contact owner, bias class, filter class | mouse-jiggle packets stay below flicker threshold |
| Temporal | rejection reason, clamp width, history weight, material policy | smooth and metallic surfaces do not shimmer or pop under camera jitter |
| Post | exposure, bloom, rolloff, grade, clarity stage outputs | luma/saturation/edge metrics stay in bounds across families |

### Rollback And Default Policy

- V2 has a runtime flag and per-domain enable bits until cross-family gates
  pass.
- A failed V2 domain must fall back to V1 for beauty output while still
  reporting the failed V2 evidence.
- A domain may not become default if its debug path is missing, if frame-report
  ownership is incomplete, or if it only works by disabling IBL/reflections,
  shadows, or temporal history.
- The default preset should advance only after `gallery`, `kitchen`, `office`,
  `gym`, and `concert` all pass a comparable packet.

### Evidence Packet Shape

Every V2 candidate packet should include:

- beauty stills from the same camera bookmarks as V1.
- material family/debug atlas.
- GBuffer channel atlas.
- light ownership and shadow ownership debug.
- reflection ownership debug.
- temporal rejection/clamp debug.
- post-stage luma and color report.
- frame-report JSON.
- comparison table against V1 seq8.

This is the guardrail against repeating the old loop where a setting change
made one capture look better while another scene regressed.

## Phase Ledger

### FSSP-V2-001 Contract And Plan

Status: IN PROGRESS

Deliverables:

- `docs/FULL_SCENE_SHADER_PIPELINE_V2.md`
- `assets/final_art/full_scene_shader_pipeline_v2_contract.json`
- `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
- `assets/final_art/full_scene_shader_material_evidence_v2.schema.json`
- `assets/final_art/full_scene_shader_material_evidence_v2.json`
- `assets/final_art/full_scene_shader_material_upgrade_plan_v2.schema.json`
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.json`
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.md`
- `assets/final_art/full_scene_shader_material_provider_requests_v2.schema.json`
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.json`
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.md`
- `assets/final_art/full_scene_shader_material_fulfillment_v2.schema.json`
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.json`
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.md`
- `tools/validate_full_scene_shader_pipeline_v2_plan.py`
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`
- `tools/build_full_scene_shader_material_evidence_v2.py`
- `tools/plan_full_scene_shader_material_upgrades_v2.py`
- `tools/export_full_scene_shader_material_provider_requests_v2.py`
- `tools/build_full_scene_shader_material_fulfillment_v2.py`
- `tools/validate_full_scene_shader_material_fulfillment_v2.py`
- handoff update in `docs/AAA_ASSET_QUALITY_HANDOFF.md`

Completion evidence:

- plan validator passes.
- contract includes every required shader domain.
- external frame-report contract maps every required shader domain to a future
  `full_scene_shader_pipeline_v2` frame-report section.
- runtime frame-report JSON emits a `full_scene_shader_pipeline_v2` section
  with all required readiness fields, labeled as `runtime_placeholder_v1_fallback`
  until V2 domains are promoted.
- runtime material-policy source is statically checked across CPU material
  resolution, VB material constants, HLSL material resolve, and the scene
  material GBuffer channel.
- temporal rejection source is statically checked for jitter-aware history
  reprojection matching the TAA resolve path.
- material evidence report derives shader-family and PBR/hero-surface blockers
  from Asset Registry V2 and the scene binding overlay.
- material upgrade plan converts blocked material evidence into P0/P1 work
  orders.
- material provider request export converts the work orders into fulfillment
  packs with PBR, shader-feature, LOD, collision, preview, and registry update
  requirements.
- material fulfillment manifest tracks every provider request through pending,
  submitted, fulfilled, admitted, or rejected state.
- handoff identifies this as the next renderer/AAA direction.
- focused commit is pushed.

Notes:

- The first frame-report contract is intentionally external because the current
  renderer C++ worktree contains unrelated uncommitted frame-contract changes.
- The follow-up runtime slice should consume this contract in
  `FrameContract.h`, `FrameContractJson.cpp`, and
  `FrameContractValidation.cpp` only after the existing dirty renderer changes
  are reconciled.

Material evidence baseline:

- status `BLOCKED`.
- assets `33`.
- V2 material-ready assets `1`.
- PBR texture-ready assets `1`.
- missing hero texture evidence `10`.
- primitive hero material blockers `24`.
- unknown material-family assets `0`.

Material upgrade work-order baseline:

- status `READY`.
- work orders `56`.
- P0 orders `34`.
- P1 orders `22`.
- primitive hero material orders `24`.
- hero asset material orders `10`.
- registry asset material orders `22`.

Material provider request baseline:

- requests `56`.
- P0 requests `34`.
- P1 requests `22`.
- request files including manifests `58`.

Material fulfillment baseline:

- status `PENDING`.
- requests `56`.
- pending `56`.
- admitted `0`.
- rejected `0`.

Runtime V2 frame-report placeholder baseline:

- status `runtime_placeholder_v1_fallback`.
- beauty output remains `v1_fallback`.
- section coverage includes material, GBuffer, lighting, reflections, shadows,
  temporal, post, render graph, asset evidence, and packet gate.
- readiness values are derived from existing V1 frame-contract ownership data.
- checker now verifies that `FrameContractJson.cpp` emits every required V2
  readiness field.

Runtime facade baseline:

- `src/Graphics/FullSceneShaderFrameContext.h` owns the first runtime facade
  for V2.
- `BuildFullSceneShaderFrameContext(const FrameContract&)` derives shared
  V2 evidence from the current V1 frame contract without changing beauty
  output.
- The facade exposes per-domain evidence for material, GBuffer, lighting,
  reflections, shadows, temporal, post, render graph, asset evidence, and
  packet gate.
- Every domain now carries:
  - `promotion_state`
  - `domain_ready`
  - `facade_owner`
  - `fallback_owner`
  - `failure_reason`
- `FrameContractJson.cpp` consumes the facade instead of calculating all V2
  readiness inline.
- `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
  declares those common evidence fields.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py` now fails if the
  runtime report stops using `FullSceneShaderFrameContext` or if supplied frame
  reports omit the per-domain evidence object.

Runtime packet harness baseline:

- `tools/run_full_scene_shader_pipeline_v2_packet.ps1` wraps the existing
  scene-local cinematic packet runner and narrows capture to V2-relevant debug
  views:
  `beauty`, `surface_policy`, `reflection_owner`, `shadow_factor`,
  `direct_light`, `ambient_ibl`, and `taa_blend`.
- The packet runner validates each emitted `frame_report_last.json` or
  `frame_report_shutdown.json` with
  `tools/check_full_scene_shader_pipeline_v2_frame_report.py --strict-frame-report`.
- The packet runner writes:
  - `v2_frame_report_evidence_summary.json`
  - `v2_frame_report_evidence_summary.md`
  - `v2_frame_report_checker_stdout.txt`
- `tools/FinalArtPipeline.ps1` exposes the command as
  `-Action FullSceneShaderV2Packet`.
- Fresh runtime evidence:
  - `.\build.ps1 -Config Release` passed and produced
    `build/bin/CortexEngine.exe`.
  - `tools/run_full_scene_shader_pipeline_v2_packet.ps1` passed on the gallery
    packet with `7` captured views, `70` evidence rows, and `0` failures.
  - Runtime summary:
    `build/captures/full_scene_shader_pipeline_v2_facade_packet_20260605/v2_frame_report_evidence_summary.json`.

Material runtime-policy bridge baseline:

- every registry asset now carries a V2 runtime policy bridge in material
  evidence: scene material class, reflection preference, temporal policy, post
  sensitivity, required texture slots, and missing texture slots.
- `runtime_policy_bridge_asset_count`: `33`.
- provider request packs now include `runtime_policy`, `runtime_policy_candidates`,
  `required_pbr_maps`, and `missing_texture_slots` inside `material_contract`.
- material evidence remains `BLOCKED`: the bridge improves contract precision,
  but does not claim assets are PBR/AAA-ready.

### FSSP-V2-002 Material Model Upgrade

Status: PLANNED

Implementation target:

- Extend `MaterialModel` into a fuller runtime material contract without
  breaking current `SceneMaterialClassId` and packet analyzers.
- Add material families:
  `dielectric`, `metal`, `brushed_metal`, `glass`, `water`, `emissive`,
  `wood`, `fabric`, `ceramic`, `tile`, `painted_wall`, `rubber`,
  `plastic`, `skin_like`, and `subsurface_wax`.
- Add feature bits:
  `base_color_texture`, `normal_texture`, `orm_texture`, `emissive_texture`,
  `clearcoat`, `anisotropy`, `transmission`, `opacity`, `parallax`,
  `detail_normal`, and `procedural_micro_detail`.

Pseudocode:

```cpp
MaterialModel ResolveRenderableMaterial(Renderable r, AssetRegistryEntry a) {
    MaterialModel model = ResolveExistingMaterial(r);
    MaterialEvidence evidence = ReadRegistryMaterialEvidence(a);
    model.family = PickFamily(model, evidence, r.semanticRole);
    model.features = MergeFeatureFlags(model, evidence);
    model.temporalPolicy = PickTemporalPolicy(model.family, model.features);
    model.reflectionPolicy = PickReflectionPolicy(model.family, model.roughness);
    ValidateMaterialModel(model);
    return model;
}
```

Validation:

- material debug views show no unknown material family for target scenes.
- frame reports expose per-family counts and missing texture evidence.
- current V1 material-class packet remains passing.

### FSSP-V2-003 GBuffer And Debug Channel Expansion

Status: PLANNED

Implementation target:

- Add explicit channel inventory for the visibility/deferred path.
- Add object/material id debug views.
- Add velocity validity debug view.
- Keep old debug views as aliases until packet tools migrate.

Pseudocode:

```cpp
GBufferLayout BuildFullSceneGBufferLayout(RendererCaps caps) {
    AddRequired("albedo");
    AddRequired("normal_roughness");
    AddRequired("emissive_metallic");
    AddRequired("material_ext0");
    AddRequired("material_ext1");
    AddRequired("material_ext2");
    AddRequired("velocity_object_id");
    AddOptional("clearcoat_transmission", caps.supportsExtendedMaterial);
    AddOptional("detail_masks", caps.supportsExtendedMaterial);
    return layout;
}
```

Validation:

- frame contract reports every active GBuffer target and format.
- debug packet can capture each channel.
- missing or null material/object/velocity channel fails in V2 mode.

### FSSP-V2-004 Scene-Local Light Rig System

Status: PLANNED

Implementation target:

- Promote light rigs to first-class profile assets.
- Replace raw bootstrap lighting with semantic rigs.
- Add area/rect light support or a documented approximation path.

Pseudocode:

```cpp
SceneLightRig BuildLightRig(SceneFamily family, SceneZones zones) {
    SceneLightRig rig;
    rig.Add(KeyLightForFamily(family, zones));
    rig.Add(FillLightForReadableShadows(family, zones));
    rig.Add(PracticalFixturesFromSceneGraph(family, zones));
    rig.Add(AccentLightsForFocalObjects(family, zones));
    ValidateLightOwnership(rig);
    return rig;
}
```

Validation:

- frame reports list semantic light counts by type and owner.
- debug views expose direct light, light id, shadow caster, and fixture owner.
- target scenes pass exposure/clipping gates without per-scene emergency clamps.

### FSSP-V2-005 Local Reflection Probe System

Status: PLANNED

Implementation target:

- Replace neutral-only enclosed reflection fallback with scene-local probes.
- Support room probes, hero probes, and planar probes.
- Keep external HDRI visible only when the scene profile explicitly allows it.

Pseudocode:

```cpp
ReflectionSource PickReflectionSource(Pixel p, MaterialModel m, SceneProbeSet probes) {
    if (!m.reflectionEligible) return NeutralDiffuseFallback();
    if (m.planarEligible && probes.HasPlanarFor(p.surfaceId)) return PlanarProbe(p);
    if (probes.HasHeroProbeNear(p.worldPos)) return HeroProbe(p.worldPos);
    if (probes.HasRoomProbeContaining(p.worldPos)) return RoomProbe(p.worldPos);
    return NeutralFallbackWithOwner("no_valid_local_probe");
}
```

Validation:

- reflection-owner analyzer distinguishes room, hero, planar, SSR, RT, neutral,
  and external HDRI ownership.
- enclosed scenes have zero unauthorized external HDRI ratio.
- smooth/metallic motion packets stay stable.

### FSSP-V2-006 Shadow And Contact Stability

Status: PLANNED

Implementation target:

- Normalize shadow bias/filtering through `SceneCinematicProfile` successor.
- Add local contact shadow layer for small props and furniture contacts.
- Expose shadow stability counters in frame reports.

Pseudocode:

```cpp
ShadowPolicy ResolveShadowPolicy(Light l, MaterialReceiver receiver) {
    ShadowPolicy p = ProfileShadowPolicy(l.semanticType);
    p.bias = ScaleBiasBySceneUnits(p.bias, receiver.normalSlope);
    p.filterRadius = StableFilterRadius(l.size, receiver.distance);
    p.contact = NeedsContactTerm(receiver, l);
    return p;
}
```

Validation:

- shadow factor, shadow id, contact term, cascade, and bias debug views exist.
- mouse-jitter and camera-sweep packets have no hard-gated shadow flicker.
- contacts on chairs, counters, balls, props, and plinths are visible and stable.

### FSSP-V2-007 Material-Aware Temporal Pipeline

Status: PLANNED

Implementation target:

- Add material-aware rejection and history clamp policies.
- Use motion vectors and object/material ids in temporal resolve.
- Create specific policies for glass, water, mirror, conductor, emissive, and
  high-frequency normal surfaces.

Pseudocode:

```hlsl
TemporalPolicy policy = LoadTemporalPolicy(materialId);
float2 historyUv = uv + motionVector;
HistorySample h = SampleHistory(historyUv);
float confidence = ComputeHistoryConfidence(depth, normal, objectId, materialId, policy);
float3 clamped = ClampHistoryToNeighborhood(h.color, currentNeighborhood, policy);
return lerp(currentColor, clamped, confidence * policy.maxBlend);
```

Validation:

- temporal debug packet exposes velocity, rejection, clamp width, and blend.
- smooth/metal/glass/water pixels cannot exceed configured residual flicker
  thresholds under mouse jitter.

### FSSP-V2-008 HDR Cinematic Post V2

Status: PLANNED

Implementation target:

- Keep the current post look polish, but refactor it into named HDR stages.
- Add optional LUT support and measured exposure mode.
- Preserve deterministic/no-noise behavior unless gated.

Pseudocode:

```hlsl
float3 PostV2(float3 hdr, PostProfile p) {
    hdr = ApplyExposure(hdr, p);
    hdr = ApplyBloomComposite(hdr, p);
    hdr = ApplyHighlightRolloff(hdr, p);
    float3 ldr = ApplyFilmicToneMap(hdr, p);
    ldr = ApplyColorGrade(ldr, p);
    ldr = ApplyClarityIfStable(ldr, p);
    return EncodeOutput(ldr);
}
```

Validation:

- bright ratio, dark crush, midtone coverage, saturation, and edge density
  remain packet-gated.
- shader compile warnings are not allowed to increase.

### FSSP-V2-009 Render Graph Ownership Refactor

Status: PLANNED

Implementation target:

- Make the full frame pass graph explicit.
- Give every pass declared inputs, outputs, states, debug name, and timing.
- Frame reports should be able to prove which pass produced every V2 debug view.

Pseudocode:

```cpp
RenderGraph graph;
graph.AddPass("ShadowPrepass", reads(sceneLights), writes(shadowAtlas));
graph.AddPass("DepthPrepass", reads(sceneGeometry), writes(depth, hzb));
graph.AddPass("VisibilityResolve", reads(sceneGeometry), writes(visibility, materialIds));
graph.AddPass("GBufferResolve", reads(visibility, materials), writes(gbuffer));
graph.AddPass("Lighting", reads(gbuffer, shadowAtlas, probes), writes(hdrLighting));
graph.AddPass("LocalReflections", reads(gbuffer, probes, history), writes(reflection));
graph.AddPass("Temporal", reads(hdrLighting, reflection, velocity, history), writes(temporalHdr));
graph.AddPass("Post", reads(temporalHdr, bloom), writes(backbuffer));
graph.CompileAndValidate();
```

Validation:

- frame contract reports expected pass list and resource ownership.
- missing transition, skipped producer, or null debug source fails V2 packets.

### FSSP-V2-010 Cross-Family V2 Gate

Status: PLANNED

Implementation target:

- Run a V2 packet over gallery, kitchen, office, gym, and concert.
- Include beauty, material, GBuffer, reflection, shadow, temporal, post, and
  frame-report evidence.
- Compare against the final V1 seq8 baseline.

Validation:

- V1 gates remain passing.
- V2-specific gates pass.
- visual-quality analyzer still passes.
- human review accepts the visual direction as materially closer to AAA final
  art.

## Validation Matrix

| Domain | Required Evidence | Current Status |
|---|---|---|
| Material | material families, texture evidence, debug views | runtime material evidence ready for gallery packet; asset-registry/hero texture evidence still pending |
| GBuffer | channel inventory, object/material ids, velocity | visibility payload, producer, instance table, material lookup, and stable instance ids reported; stable per-pixel material-id/object-id/debug-source ownership still blocking |
| Lighting | semantic light rigs, owner/debug reports | planned |
| Reflection | room/hero/planar/local ownership | planned |
| Shadow | cascade/local/contact debug and stability | planned |
| Temporal | material-aware rejection/clamping | planned |
| Post | HDR stage contract and visual analyzer gates | planned |
| Render graph | pass/resource ownership in frame reports | planned |
| Asset registry | material/texture readiness linked to assets | planned |
| Packets | V1 baseline preserved plus V2 debug matrix | planned |

## Next Implementation Order

1. Land this plan, contract, and validator.
2. Land the external V2 frame-report contract and checker.
3. Land the V2 material evidence report from Asset Registry V2 and scene
   bindings.
4. Land the V2 material upgrade work-order planner.
5. Land the V2 material provider request exporter.
6. Land the V2 material fulfillment/admission manifest and validator.
7. Landed runtime frame-report placeholders for V2 domains without changing
   rendering.
8. Landed runtime material model readiness evidence for the gallery V2 packet.
9. Landed GBuffer resource/producer ownership evidence; material/object-id
   ownership remains the next Track C blocker.
10. Landed identity substrate evidence for the visibility payload, instance
    table, material lookup table, and stable instance ids.
11. Add per-pixel material-id/object-id debug ownership.
12. Add local reflection probe ownership before chasing more shiny materials.
13. Add semantic light rigs and shadow/contact stability.
14. Refactor post into named HDR stages.
15. Move pass ownership into the render graph.
16. Run cross-family V2 packets and compare to V1 seq8.

This order keeps the engine diagnosable. It avoids the previous trap where a
scene looked better only because one setting hid the problem.

## Full Scene Shader Pipeline V2 Identity Ownership Slice - 2026-06-05

Purpose:

- Implement the first concrete feature from
  `docs/FULL_SCENE_SHADER_REFACTOR_MASTER_PLAN.md`.
- Prove the visibility-buffer identity substrate before trying to make V2
  lighting, reflections, shadows, or temporal consume object/material ids.
- Keep V2 beauty on the V1 fallback while the renderer gains better frame
  ownership evidence.

Implemented:

- Added `visibility_buffer` to frame-contract resource snapshots.
- Added draw-count evidence:
  - `visibility_buffer_materials`.
  - `visibility_buffer_invalid_stable_ids`.
- `Renderer_VisibilityBufferCollection.cpp` now reports:
  - visibility material table size.
  - invalid stable culling-id count.
- `FullSceneGBufferEvidence` now reports:
  - visibility payload channel readiness.
  - visibility payload producer readiness.
  - instance identity table readiness.
  - instance material lookup readiness.
  - stable instance id availability.
  - visibility-buffer instance count.
  - visibility-buffer material count.
  - invalid stable instance id count.
- The V2 frame-report contract requires those fields.
- The V2 checker statically requires the new identity ownership surface.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- src\Graphics\FrameContract.h src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\Renderer_VisibilityBufferCollection.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json tools\check_full_scene_shader_pipeline_v2_frame_report.py
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_identity_ownership_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- diff whitespace check: passed.
- executable target build: passed.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Build caveat:

- `.\build.ps1 -Config Release` hung in the asset-sync step
  (`tools/sync_assets.cmake`) before producing a fresh executable timestamp.
- The stuck `cmake`/`ninja` helper processes were stopped.
- A direct `CortexEngine` target build with the Visual Studio environment then
  passed and linked `build/bin/CortexEngine.exe`.

Packet evidence:

- manifest:
  `build/captures/full_scene_shader_pipeline_v2_identity_ownership_packet_20260605/manifest.json`
- summary:
  `build/captures/full_scene_shader_pipeline_v2_identity_ownership_packet_20260605/v2_frame_report_evidence_summary.json`
- captured views: `7`.
- evidence rows: `70`.
- failures: `0`.

Gallery beauty GBuffer identity evidence:

- `visibility_payload_channel_ready=true`.
- `visibility_payload_producer_ready=true`.
- `instance_identity_table_ready=true`.
- `instance_material_lookup_ready=true`.
- `stable_instance_id_available=true`.
- `visibility_buffer_instance_count=55`.
- `visibility_buffer_material_count=36`.
- `invalid_stable_instance_id_count=0`.
- `missing_required_channel_count=0`.
- `missing_ownership_channel_count=3`.
- `material_id_channel_ready=false`.
- `object_id_channel_ready=false`.
- `gbuffer.domain_ready=false`.
- failure reason:
  `Stable per-pixel material-id channel is not promoted`.

Current interpretation:

- The visibility-buffer identity substrate is now reported and packet-proved.
- The GBuffer domain still correctly fails because per-pixel material id,
  object id, and debug-view source ownership are not promoted.
- This is the intended stopping point for `FSSP-V2-003A`; it narrows the next
  blocker from broad GBuffer ownership to explicit per-pixel id/debug outputs.

Next recommended implementation:

- Continue Track C with `FSSP-V2-003B Per-Pixel Identity Debug`.
- Add or expose material-id/object-id debug views from the visibility payload
  and instance/material tables.
- Only promote `material_id_channel_ready`, `object_id_channel_ready`, or
  `debug_view_source_report_available` after the packet captures those views.

## Full Scene Shader Pipeline V2 Per-Pixel Identity Debug Slice - 2026-06-05

Purpose:

- Complete `FSSP-V2-003B` by exposing per-pixel material and object identity
  views from the visibility-buffer payload and instance table.
- Promote GBuffer ownership only after the packet captures both identity views.
- Keep V2 beauty on the V1 fallback while the identity substrate becomes
  available to later lighting, reflection, shadow, temporal, and post work.

Implemented:

- Added `DebugBlitVisibilityMode` with:
  - `PayloadInstance`.
  - `MaterialId`.
  - `StableObjectId`.
- `DebugBlitVisibility.hlsl` now decodes the visibility payload and can color:
  - payload instance id.
  - per-pixel material id via the visibility instance table.
  - per-pixel stable object id via the stable culling id.
- Expanded the shared visibility debug-blit root signature with:
  - debug mode constants.
  - visibility instance-table root SRV.
- Wired debug modes through both immediate and render-graph visibility paths.
- Added public debug-view modes:
  - `48 = VB_MaterialId`.
  - `49 = VB_StableObjectId`.
- Updated V2 packet defaults to capture:
  - `material_id`.
  - `object_id`.
- `FullSceneGBufferEvidence` now derives:
  - `material_id_channel_ready` from visibility payload plus instance-material
    lookup readiness.
  - `object_id_channel_ready` from visibility payload plus stable instance-id
    readiness.
  - `debug_view_source_report_available` from visibility payload producer
    ownership plus both identity channels.
- The V2 checker now validates the identity-debug runtime surface and packet
  view set.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- assets\shaders\DebugBlitVisibility.hlsl src\Graphics\VisibilityBuffer.h src\Graphics\VisibilityBuffer_DebugBlit.cpp src\Graphics\VisibilityBuffer_DebugBlitPipelines.cpp src\Graphics\Renderer_VisibilityBufferStages.cpp src\Graphics\Renderer_VisibilityBufferCulling.cpp src\Graphics\Renderer_VisibilityBufferOrchestration.cpp src\Graphics\Renderer_RenderGraphVisibilityBufferHelpers.h src\Graphics\Passes\VisibilityBufferGraphPass.h src\Graphics\Passes\VisibilityBufferGraphPass.cpp src\Graphics\Renderer_RenderGraphVisibilityBuffer.cpp src\Graphics\Renderer_DebugSettings.cpp tools\run_full_scene_shader_pipeline_v2_packet.ps1 tools\check_full_scene_shader_pipeline_v2_frame_report.py src\Graphics\FullSceneShaderFrameContext.h tools\FinalArtPipeline.ps1
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_per_pixel_identity_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- diff whitespace check: passed.
- focused changed-object build: passed.
- full `CortexEngine` target build: passed and linked
  `build/bin/CortexEngine.exe`.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_per_pixel_identity_packet_20260605`.
- captured views: `9`.
- evidence rows: `90`.
- failures: `0`.
- captured views:
  - `beauty`.
  - `surface_policy`.
  - `material_id`.
  - `object_id`.
  - `reflection_owner`.
  - `shadow_factor`.
  - `direct_light`.
  - `ambient_ibl`.
  - `taa_blend`.

Gallery beauty GBuffer identity evidence:

- `visibility_payload_channel_ready=true`.
- `visibility_payload_producer_ready=true`.
- `instance_identity_table_ready=true`.
- `instance_material_lookup_ready=true`.
- `stable_instance_id_available=true`.
- `material_id_channel_ready=true`.
- `object_id_channel_ready=true`.
- `debug_view_source_report_available=true`.
- `visibility_buffer_instance_count=55`.
- `visibility_buffer_material_count=36`.
- `invalid_stable_instance_id_count=0`.
- `missing_required_channel_count=0`.
- `missing_ownership_channel_count=0`.
- `gbuffer.domain_ready=true`.
- failure reason:
  `FullSceneFrameData GBuffer ownership is ready`.

Build caveat:

- The first build attempt timed out in `tools/sync_assets.cmake`.
- Stale CortexEngine `cmake`/`ninja` workers were stopped.
- Touching only the local build stamp under `build/` allowed the generated
  build to proceed; the subsequent full target build completed and linked.

Current interpretation:

- `FSSP-V2-003B` is complete for the gallery packet.
- The V2 GBuffer domain now has a stable per-pixel identity substrate.
- This does not promote V2 beauty. It only gives later domains a reliable
  material/object truth layer.

Next recommended implementation:

- Start the full runtime material-table promotion work.
- Material records should move from evidence-only into shader-facing upload
  data for material family, texture readiness, reflection policy, temporal
  policy, and post sensitivity.
- After material-table debug views are packet-proved, move to scene-local light
  rig ownership and local reflection/probe ownership.

## Full Scene Shader Pipeline V2 Runtime Material Table Slice - 2026-06-05

Purpose:

- Start Phase 2 by turning the existing visibility-buffer material upload into
  an explicit `FullSceneMaterialTable` contract.
- Stop treating material readiness as only sampled-material counts.
- Prove that the shader-facing material table has policy rows and backs the
  GBuffer material policy channel before later lighting, reflection, temporal,
  or post passes consume it.

Implemented:

- Extended `FullSceneMaterialModelEvidence` with:
  - `shaderMaterialTableReady`.
  - `shaderMaterialPolicyRowsReady`.
  - `gbufferPolicyChannelBackedByMaterialTable`.
  - `shaderMaterialTableRowCount`.
  - `shaderMaterialPolicyColumnCount`.
- `BuildFullSceneMaterialModelEvidence` now receives:
  - visibility-buffer material-table row count.
  - GBuffer material-policy channel readiness.
- `fullSceneMaterialModelReady` now requires:
  - runtime policy coverage.
  - populated shader material table.
  - complete 4-column policy rows.
  - GBuffer policy channel backed by that table.
  - texture/descriptor evidence.
  - no unknown material family.
  - no validation errors.
- The V2 frame-report contract and checker require the new material-table
  fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- src\Graphics\MaterialModel.h src\Graphics\MaterialModel.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json tools\check_full_scene_shader_pipeline_v2_frame_report.py
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_material_table_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- diff whitespace check: passed.
- focused changed-object build: passed.
- full `CortexEngine` target build: passed and linked.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_material_table_packet_20260605`.
- captured views: `9`.
- evidence rows: `90`.
- failures: `0`.

Gallery beauty material evidence:

- `sampled_material_count=60`.
- `shader_material_table_row_count=36`.
- `shader_material_policy_column_count=4`.
- `runtime_policy_bridge_ready=true`.
- `shader_material_policy_rows_ready=true`.
- `shader_material_table_ready=true`.
- `gbuffer_policy_channel_backed_by_material_table=true`.
- `family_counts_available=true`.
- `reflection_policies_available=true`.
- `temporal_policies_available=true`.
- `post_policies_available=true`.
- `texture_evidence_available=true`.
- `shader_feature_flags_available=true`.
- `unknown_material_family_count=0`.
- `validation_error_count=0`.
- `material.domain_ready=true`.

Current interpretation:

- The material domain now has a packet-visible shader-table contract.
- Later V2 lighting/reflection/temporal/post work can depend on the table rows
  and policy channel instead of re-inferring material meaning.
- V2 beauty remains `v1_fallback`.

Next recommended implementation:

- Add material policy debug views for material family, texture readiness,
  reflection policy, temporal policy, and post sensitivity.
- Then start scene-local light-rig ownership and local reflection/probe
  ownership.

## Full Scene Shader Pipeline V2 Material Policy Debug Views Slice - 2026-06-05

Purpose:

- Make the shader-facing material table inspectable per pixel.
- Prove material-family, reflection-policy, temporal-policy, and
  post-sensitivity policy columns are read directly from
  `VBMaterialConstants.policyParams`.
- Give later lighting, reflection, temporal, and HDR post refactors a concrete
  debug substrate instead of relying on broad surface-class overlays.

Implemented:

- Added visibility-buffer debug modes:
  - `50`: `VB_MaterialFamilyPolicy`.
  - `51`: `VB_ReflectionPolicy`.
  - `52`: `VB_TemporalPolicy`.
  - `53`: `VB_PostSensitivity`.
- Extended `DebugBlitVisibility.hlsl` with:
  - `StructuredBuffer<VBMaterialConstants> g_Materials : register(t2)`.
  - material-family policy from `policyParams.x`.
  - reflection policy from `policyParams.y`.
  - temporal policy from `policyParams.z`.
  - post sensitivity from `policyParams.w`.
- Extended the visibility debug-blit root signature with material-table SRV
  slot `t2`.
- Added a runtime guard that fails material-policy debug blits if the material
  table is missing.
- Added packet names:
  - `material_family`.
  - `reflection_policy`.
  - `temporal_policy`.
  - `post_sensitivity`.
- Updated the V2 packet runner and final-art pipeline wrapper to capture the
  expanded material-policy packet by default.
- Updated the V2 checker to require the debug modes, material table binding,
  root SRV binding, and packet view names.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- assets\shaders\DebugBlitVisibility.hlsl src\Graphics\VisibilityBuffer.h src\Graphics\VisibilityBuffer_DebugBlit.cpp src\Graphics\VisibilityBuffer_DebugBlitPipelines.cpp src\Graphics\Renderer_DebugSettings.cpp src\Graphics\Renderer_VisibilityBufferCulling.cpp src\Graphics\Renderer_VisibilityBufferOrchestration.cpp src\Graphics\Renderer_VisibilityBufferStages.cpp src\Graphics\Renderer_RenderGraphVisibilityBufferHelpers.h tools\run_full_scene_shader_pipeline_v2_packet.ps1 tools\FinalArtPipeline.ps1 tools\check_full_scene_shader_pipeline_v2_frame_report.py assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json tools\run_scene_local_cinematic_renderer_v1_packets.ps1
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_material_policy_debug_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- diff whitespace check: passed.
- Release `CortexEngine` target build: passed and linked after restoring the
  Visual Studio developer environment in-process.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_material_policy_debug_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.
- policy debug views:
  - `material_family` debug view `50`.
  - `reflection_policy` debug view `51`.
  - `temporal_policy` debug view `52`.
  - `post_sensitivity` debug view `53`.

Current interpretation:

- `FSSP-V2-002C` is complete for the gallery packet.
- The material domain now has both shader-table readiness and per-pixel policy
  debug visibility.
- V2 beauty remains `v1_fallback`; this slice only strengthens the facts that
  future full-scene lighting/reflection/temporal/post passes can consume.

Next recommended implementation:

- Start scene-local semantic light-rig ownership.
- Then build local reflection/probe ownership so enclosed scenes no longer
  depend on unauthorized environment fallback.

## Full Scene Shader Pipeline V2 Semantic Light Rig Evidence Slice - 2026-06-05

Purpose:

- Start Phase 3 by replacing the thin `rig_id != custom` lighting check with a
  real scene-local semantic light-rig evidence contract.
- Make V2 lighting prove that scene-owned lights, policy ids, balance policy,
  local fixtures, shadowed-light ownership, and exposure bounds are present
  before later full-scene lighting shaders depend on them.

Implemented:

- Added `FullSceneLightingRigEvidence` to `FullSceneShaderFrameContext`.
- V2 lighting readiness now checks:
  - semantic rig id/source.
  - scene-local environment shader readiness.
  - light-owner/semantic fixture evidence.
  - semantic light roles.
  - rig/shadow/exposure policy consistency against the scene visual contract.
  - scene-local lighting balance policy.
  - usable local fixture ownership.
  - shadow-casting light ownership when shadows are enabled.
  - named exposure policy and bounded exposure/light-intensity values.
- V2 frame reports now emit:
  - `semantic_light_roles_available`.
  - `rig_policy_ids_consistent`.
  - `lighting_balance_policy_ready`.
  - `local_fixture_contract_ready`.
  - `shadowed_light_contract_ready`.
  - `exposure_policy_ready`.
  - semantic fixture, soft fixture, emissive fixture, stage fixture, practical
    fixture, shadow-casting light, total intensity, max intensity, and missing
    lighting-contract counts.
- The V2 checker now requires the runtime lighting evidence builder and the
  new frame-report fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8 --verbose
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_semantic_light_rig_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed and linked.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_semantic_light_rig_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.

Gallery beauty lighting evidence:

- `semantic_light_rig_ready=true`.
- `scene_local_environment_shader_ready=true`.
- `light_owner_report_available=true`.
- `semantic_light_roles_available=true`.
- `rig_policy_ids_consistent=true`.
- `lighting_balance_policy_ready=true`.
- `local_fixture_contract_ready=true`.
- `shadowed_light_contract_ready=true`.
- `exposure_policy_ready=true`.
- `exposure_clipping_gate_passed=true`.
- `semantic_fixture_light_count=4`.
- `soft_fixture_light_count=2`.
- `stage_fixture_light_count=2`.
- `rect_area_light_count=2`.
- `shadow_casting_light_count=1`.
- `total_light_intensity=18.039999`.
- `max_light_intensity=5.455999`.
- `missing_lighting_contract_count=0`.
- `lighting.domain_ready=true`.
- failure reason:
  `Scene-local semantic light-rig ownership is ready`.

Current interpretation:

- `FSSP-V2-004A` is complete for the gallery packet.
- Lighting still renders through the V1 beauty fallback, but V2 now has a
  packet-proved semantic lighting contract that full-scene lighting shaders can
  consume.
- Next architecture slice should build local reflection/probe ownership and RT
  miss fallback evidence with the same contract-first pattern.

## Full Scene Shader Pipeline V2 Reflection Ownership Evidence Slice - 2026-06-05

Purpose:

- Start Phase 4 by replacing the loose reflection facade with a scene-local
  reflection/probe ownership contract.
- Make V2 prove whether reflection owners are known, local probes are valid,
  external IBL visibility is authorized, RT miss fallback is safe, and enclosed
  scenes have a local or neutral miss path before any V2 reflection beauty pass
  is promoted.

Implemented:

- Added `FullSceneReflectionOwnershipEvidence` to
  `FullSceneShaderFrameContext`.
- V2 reflection readiness now checks:
  - reflection-owner debug/report availability.
  - known scene reflection owner.
  - complete material reflection policies.
  - authorized external IBL visibility.
  - declared local probe rig table/radiance/intensity readiness.
  - RT reflection miss environment policy.
  - enclosed-scene miss fallback safety.
  - at least one authorized reflection source contract.
- V2 frame reports now emit:
  - skipped probe count.
  - local probe rig/table/radiance/intensity readiness.
  - local probe diffuse/specular intensity.
  - enclosed miss fallback safety.
  - reflection source contract readiness.
  - external IBL visibility authorization.
  - reflection owner known.
  - missing reflection contract count.
- The V2 checker now requires the reflection ownership evidence builder and the
  new frame-report fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8 --verbose
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_ownership_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed and linked.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_ownership_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.

Gallery beauty reflection evidence:

- `reflection_owner_known=true`.
- `reflection_owner_report_available=true`.
- `reflection_policies_available=true`.
- `external_ibl_visibility_authorized=true`.
- `rt_miss_environment_policy_ready=true`.
- `enclosed_miss_fallback_safe=true`.
- `reflection_source_contract_ready=true`.
- `room_probe_count=2`.
- `local_probe_rig_declared=true`.
- `local_probe_table_ready=true`.
- `local_probe_radiance_ready=true`.
- `local_probe_intensity_ready=true`.
- `local_probe_contract_ready=true`.
- `local_probe_diffuse_intensity=0.171000`.
- `local_probe_specular_intensity=0.323000`.
- `skipped_probe_count=0`.
- `unauthorized_external_hdri_ratio=0`.
- `unknown_reflection_owner_ratio=0`.
- `missing_reflection_contract_count=0`.
- `reflections.domain_ready=true`.
- failure reason:
  `Scene-local reflection/probe ownership is ready`.

Current interpretation:

- `FSSP-V2-005A` is complete for the gallery packet.
- Reflection still renders through the V1 beauty fallback, but V2 now has a
  packet-proved local reflection/probe ownership contract.
- Next architecture slice should formalize shadow/contact stability evidence,
  then material-aware temporal promotion evidence.

## Full Scene Shader Pipeline V2 Shadow Contact Evidence Slice - 2026-06-05

Purpose:

- Start Phase 5 by replacing the thin shadow facade with explicit
  shadow/contact stability evidence.
- Make V2 prove shadow policy, shadow-map resource ownership, producer
  ownership, caster ownership, cascade/bias/filter policy bounds, RT shadow
  signal readiness, and contact/near-field shadow readiness before any V2
  shadow-heavy beauty path is promoted.

Implemented:

- Added `FullSceneShadowContactEvidence` to `FullSceneShaderFrameContext`.
- V2 shadow readiness now checks:
  - scene-local shadow policy report.
  - valid `shadow_map` resource.
  - `ShadowPass` producer ownership for `shadow_map`.
  - cascade debug/resource availability.
  - cascade split, bias, and filter policy bounds.
  - shadow-casting light ownership.
  - local shadow atlas readiness.
  - RT shadow mask readiness.
  - RT shadow history readiness.
  - contact/near-field shadow readiness.
  - missing shadow contract count.
- V2 frame reports now emit the above fields directly.
- The V2 checker now requires the shadow/contact evidence builder and the new
  frame-report fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8 --verbose
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_shadow_contact_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed and linked.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_shadow_contact_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.

Gallery beauty shadow evidence:

- `shadow_policy_report_available=true`.
- `shadow_map_ready=true`.
- `shadow_map_producer_ready=true`.
- `cascade_debug_available=true`.
- `cascade_policy_ready=true`.
- `shadow_bias_policy_ready=true`.
- `shadow_filter_policy_ready=true`.
- `shadow_caster_ownership_ready=true`.
- `local_shadow_atlas_ready=true`.
- `rt_shadow_mask_ready=true`.
- `rt_shadow_history_ready=true`.
- `contact_shadow_ready=true`.
- `shadow_stability_gate_passed=true`.
- `shadow_casting_light_count=1`.
- `shadow_bias=0.003000`.
- `shadow_pcf_radius=3.0`.
- `cascade_split_lambda=0.55`.
- `missing_shadow_contract_count=0`.
- `shadows.domain_ready=true`.
- failure reason:
  `Scene-local shadow/contact stability is ready`.

Current interpretation:

- `FSSP-V2-006A` is complete for the gallery packet.
- Shadows still render through the V1 beauty fallback, but V2 now has a
  packet-proved shadow/contact stability contract.
- Next architecture slice should formalize material-aware temporal promotion
  evidence.

## Full Scene Shader Pipeline V2 Material-Aware Temporal Evidence Slice - 2026-06-05

Purpose:

- Start `FSSP-V2-007A` by replacing the thin temporal facade with explicit
  material-aware temporal evidence.
- Make V2 prove motion-vector ownership, visibility-buffer motion, previous
  transform history, temporal rejection-mask statistics, jitter-aware
  reprojection, material temporal policies, TAA history readiness, and
  camera-sweep stability before any temporal candidate drives beauty.

Implemented:

- Added `FullSceneTemporalEvidence` to `FullSceneShaderFrameContext`.
- Added `BuildFullSceneTemporalEvidence`.
- V2 temporal readiness now checks:
  - TAA enabled state.
  - velocity/motion-vector resource ownership.
  - visibility-buffer motion instead of camera-only fallback.
  - previous transform history coverage.
  - temporal rejection-mask resource/stat/latency readiness.
  - jitter-aware reprojection.
  - material-aware rejection via material policy channel.
  - TAA history resource validity, age, accumulation alpha, velocity
    reprojection, and disocclusion rejection.
  - smooth-surface and camera-sweep gates.
- V2 frame reports now emit:
  - visibility-buffer motion and previous transform history readiness.
  - temporal-mask readiness, ratios, and readback latency.
  - TAA-history readiness, age, and accumulation alpha.
  - missing temporal-contract count.
- The V2 checker now requires the temporal evidence builder and the new
  frame-report fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_temporal_evidence_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_temporal_evidence_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.

Gallery beauty temporal evidence:

- `missing_temporal_contract_count=0`.
- `motion_vectors_ready=true`.
- `visibility_buffer_motion_ready=true`.
- `previous_transform_history_ready=true`.
- `temporal_mask_ready=true`.
- `temporal_mask_stats_ready=true`.
- `temporal_mask_latency_ready=true`.
- `temporal_mask_readback_latency_frames=3`.
- `temporal_mask_accepted_ratio=1.0`.
- `temporal_mask_high_motion_ratio=0.0`.
- `temporal_mask_out_of_bounds_ratio=0.0`.
- `taa_history_ready=true`.
- `taa_history_velocity_reprojection_ready=true`.
- `taa_history_disocclusion_rejection_ready=true`.
- `taa_history_age_frames=0`.
- `taa_history_accumulation_alpha=0.05999999865889549`.
- `smooth_surface_motion_gate_passed=true`.
- `camera_sweep_gate_passed=true`.
- `temporal.domain_ready=true`.

Current interpretation:

- `FSSP-V2-007A` is complete for the gallery packet.
- Temporal ownership is now explicit at the frame-report contract level and
  packet-proved for the gallery target.
- V2 beauty still remains `v1_fallback`.
- The next architecture slice should start semantic light buffers and V2
  direct-light shadow output.

## Full Scene Shader Pipeline V2 Semantic Light Buffer Evidence Slice - 2026-06-05

Purpose:

- Start `FSSP-V2-004B` by proving that the semantic light rig is backed by
  shader-facing light payloads and direct-light pass ownership.
- Move lighting evidence beyond scene-profile counts toward the actual V2
  direct-light inputs and outputs.

Implemented:

- Extended `FullSceneLightingRigEvidence` with:
  - shader light-array readiness.
  - semantic light-payload readiness.
  - area-light payload readiness.
  - clustered light-list readiness.
  - direct-light pass readiness.
  - direct-light shadow-output readiness.
  - point, spot, and two-sided area-light counts.
- Added `FullSceneShaderPassReadsResource` to pair with existing pass-write
  ownership checks.
- V2 lighting readiness now requires:
  - populated shader-facing light array.
  - semantic fixture payloads.
  - valid area-light payloads when rect lights exist.
  - `VBClusteredLights` ownership of `cluster_ranges` and
    `cluster_light_indices`.
  - `VBDeferredLighting` reading GBuffer inputs and writing `hdr_color`.
  - `VBDeferredLighting` reading `shadow_map` when shadows are enabled.
- The V2 frame-report contract and checker require the new lighting fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_light_buffer_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_light_buffer_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.

Gallery beauty lighting evidence:

- `missing_lighting_contract_count=0`.
- `shader_light_array_ready=true`.
- `semantic_light_payload_ready=true`.
- `area_light_payload_ready=true`.
- `clustered_light_list_ready=true`.
- `direct_light_pass_ready=true`.
- `direct_light_shadow_output_ready=true`.
- `point_light_count=1`.
- `spot_light_count=3`.
- `rect_area_light_count=2`.
- `semantic_fixture_light_count=4`.
- `shadow_casting_light_count=1`.
- `lighting.domain_ready=true`.

Current interpretation:

- `FSSP-V2-004B` is packet-proved for the gallery target at the evidence
  layer.
- Lighting still renders through the V1 beauty fallback, but V2 now proves the
  actual shader-facing light array, clustered light-list ownership, direct-light
  HDR output, and shadow-map input connection.
- The next shader-side lighting slice should add a V2 direct-light shadow
  output/debug comparison path rather than adding more profile-only evidence.

## Full Scene Shader Pipeline V2 Direct-Light Shadow Comparison Slice - 2026-06-05

Purpose:

- Continue `FSSP-V2-004B` by adding a shader-side direct-light comparison
  surface.
- Make V2 direct lighting inspectable as:
  - shadowed direct light.
  - unshadowed direct light.
  - direct-light shadow loss.
- Keep V1 beauty as fallback while exposing the shadow-output comparison in
  packet captures.

Implemented:

- `DeferredLighting.hlsl`
  - now accumulates `directLightUnshadowed` beside the existing shadowed
    `directLight`.
  - local lights also accumulate `localDirectUnshadowed` before applying
    local shadow factors.
  - debug mode `54` returns unshadowed direct light.
  - debug mode `55` returns the direct-light energy removed by shadows.
- `Renderer_DebugSettings.cpp`
  - expands the debug view range to `55`.
  - labels:
    - `VB_DeferredDirectLightUnshadowed`.
    - `VB_DeferredDirectLightShadowLoss`.
- Packet runners now expose:
  - `direct_light_unshadowed`.
  - `direct_light_shadow_loss`.
- The V2 frame-report contract and checker require the new comparison views.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
$env:CORTEX_SKIP_ASSET_SYNC='1'; cmake --build build --config Release --target CortexEngine --parallel 8
cmake -E copy_if_different assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_direct_light_shadow_compare_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed with `CORTEX_SKIP_ASSET_SYNC=1`.
- Updated `DeferredLighting.hlsl` was copied into `build/bin/assets/shaders`.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_direct_light_shadow_compare_packet_20260605`.
- captured views: `15`.
- evidence rows: `150`.
- failures: `0`.
- direct-light comparison views:
  - `direct_light`, debug view `44`.
  - `direct_light_unshadowed`, debug view `54`.
  - `direct_light_shadow_loss`, debug view `55`.

Current interpretation:

- V2 direct-light/shadow contribution can now be inspected as a shader-side
  comparison surface.
- This is still a debug/packet surface, not a V2 beauty promotion.
- The next lighting implementation should turn this comparison into a
  shadow-output candidate path with per-view delta metrics, or move to the
  reflection source resolver shadow-output slice if lighting comparison is
  sufficient for now.

## Full Scene Shader Pipeline V2 Reflection Resolver Debug Slice - 2026-06-05

Purpose:

- Continue `FSSP-V2-005B` by making post-composite reflection source decisions
  numeric and packet-visible.
- Expose whether smooth/metallic reflection instability is coming from SSR
  weight, RT weight, IBL/prelit potential, material reflectance, gloss, or the
  scene/material stability scale.
- Keep V1 beauty as fallback while adding resolver debug surfaces.

Implemented:

- `PostProcess.hlsl`
  - added `iblReflectionPotential` beside the existing SSR/RT resolver.
  - debug mode `56` outputs reflection source weights:
    - R = SSR post-composite weight.
    - G = RT post-composite weight.
    - B = IBL/prelit reflection potential.
  - debug mode `57` outputs reflection stability policy:
    - R = material reflectance.
    - G = gloss.
    - B = scene/material reflection stability scale.
- `Renderer_DebugSettings.cpp`
  - expands the debug view range to `57`.
  - labels:
    - `PostReflectionSourceWeights`.
    - `PostReflectionStabilityPolicy`.
- Packet runners now expose:
  - `reflection_source_weights`.
  - `reflection_stability_policy`.
- The V2 frame-report contract and checker require the new resolver views.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set CORTEX_SKIP_ASSET_SYNC=1 && cmake --build build --config Release --target CortexEngine --parallel 8 && cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl && cmake -E copy_if_different assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed with `CORTEX_SKIP_ASSET_SYNC=1`
  after loading Visual Studio 18 `VsDevCmd.bat`.
- Updated `PostProcess.hlsl` and `DeferredLighting.hlsl` were copied into
  `build/bin/assets/shaders`.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605`.
- captured views: `17`.
- evidence rows: `170`.
- failures: `0`.
- reflection resolver views:
  - `reflection_owner`, debug view `46`.
  - `reflection_source_weights`, debug view `56`.
  - `reflection_stability_policy`, debug view `57`.

Current interpretation:

- V2 reflections now have a packet-visible source-weight and stability-policy
  comparison surface at the post resolver.
- This does not promote V2 beauty. It gives the next pass enough evidence to
  make candidate resolver changes without guessing whether SSR, RT, IBL, or
  material stability policy owns the artifact.
- Next implementation should add per-view numeric delta metrics or start an
  opt-in reflection resolver candidate path behind a debug/profile flag.

## Full Scene Shader Pipeline V2 Debug View Metrics Slice - 2026-06-05

Purpose:

- Convert packet debug-view BMPs into numeric evidence so shader changes can be
  compared across runs without relying only on contact sheets.
- Measure reflection, lighting, material, temporal, and beauty debug surfaces
  with stable per-view image statistics.

Implemented:

- Added `tools/analyze_full_scene_shader_debug_view_metrics.py`.
  - reads packet `manifest.json`.
  - parses captured BMPs with the Python standard library.
  - emits `debug_view_metrics.json` and `debug_view_metrics.md`.
  - records width, height, pixel count, mean RGB, max RGB, mean/max luma,
    nonblack ratio, and hot-pixel ratio per captured view.
- Updated `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
  - every V2 packet now runs the debug-view metrics analyzer after frame-report
    evidence is generated.
- Updated `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.
  - requires the metrics analyzer and packet metric outputs.

Validation:

```powershell
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605\manifest.json --output-json build\captures\full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605\debug_view_metrics.json --output-md build\captures\full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605\debug_view_metrics.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_debug_view_metrics_packet_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_debug_view_metrics.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Results:

- standalone metrics analyzer: passed on the previous reflection resolver
  packet.
- V2 packet with integrated metrics: passed.
- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_debug_view_metrics_packet_20260605`.
- captured views: `17`.
- evidence rows: `170`.
- frame-report failures: `0`.
- measured debug views: `17`.
- metric failures: `0`.
- key reflection metrics:
  - `reflection_source_weights` mean RGB:
    `0.000001, 0.069131, 0.060542`.
  - `reflection_source_weights` nonblack ratio: `0.322766`.
  - `reflection_stability_policy` mean RGB:
    `0.159872, 0.110794, 0.343249`.
  - `reflection_stability_policy` nonblack ratio: `1.0`.

Current interpretation:

- V2 packets now produce numeric debug-view evidence beside screenshots and
  frame reports.
- The next candidate-shader pass can compare these metrics before/after and
  reject changes that silently zero out reflection, lighting, temporal, or
  material debug surfaces.

## Full Scene Shader Pipeline V2 Reflection Resolver Candidate Slice - 2026-06-05

Purpose:

- Start an opt-in `FSSP-V2-005B` reflection resolver candidate path without
  changing default beauty.
- Make the candidate and candidate-vs-current delta packet-visible so smooth
  and metallic reflection changes can be validated before promotion.

Implemented:

- `PostProcess.hlsl`
  - preserves the current resolver as default beauty.
  - adds `reflectionBaseColor`, `currentReflectionCompositeColor`, and
    `candidateReflectionCompositeColor`.
  - adds a conservative V2 candidate resolver with stricter SSR admission via
    `stableSSRConfidence` and smoother RT handoff on polished, mirror, and
    water-class surfaces.
  - debug view `58` renders the V2 reflection resolver candidate beauty.
  - debug view `59` renders candidate-vs-current reflection delta.
- `Renderer_DebugSettings.cpp`
  - expands the debug view range to `59`.
  - labels:
    - `PostReflectionResolverV2Candidate`.
    - `PostReflectionResolverV2CandidateDelta`.
- Packet runners now expose:
  - `reflection_resolver_candidate`.
  - `reflection_resolver_candidate_delta`.
- The V2 frame-report contract and checker require the new candidate views.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_debug_view_metrics.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set CORTEX_SKIP_ASSET_SYNC=1 && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
cmake -E copy_if_different assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_candidate_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Build note:

- The first `cmake --build` wrapper command timed out with stale `cmake` and
  `ninja` child processes.
- Those orphaned build processes were stopped.
- Direct `ninja -C build CortexEngine -v` under the Visual Studio 18 developer
  environment completed and linked `build/bin/CortexEngine.exe`.

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- Release `CortexEngine` target build: passed through direct `ninja`.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_candidate_packet_20260605`.
- captured views: `19`.
- evidence rows: `190`.
- frame-report failures: `0`.
- measured debug views: `19`.
- metric failures: `0`.
- candidate views:
  - `reflection_resolver_candidate`, debug view `58`.
  - `reflection_resolver_candidate_delta`, debug view `59`.
- key candidate metrics:
  - `reflection_resolver_candidate` mean RGB:
    `0.620142, 0.591729, 0.550348`.
  - `reflection_resolver_candidate` nonblack ratio: `1.0`.
  - `reflection_resolver_candidate_delta` mean RGB:
    `0.000000055, 0.000000043, 0.000000034`.
  - `reflection_resolver_candidate_delta` nonblack ratio:
    `0.00000217`.

Current interpretation:

- The V2 reflection resolver candidate is wired, packet-visible, and
  conservative on the static gallery frame.
- The near-zero delta means this slice does not yet prove visible improvement;
  it proves safe opt-in infrastructure and measurable comparison.
- Next proof should use mouse-jiggle/camera-sweep and cross-family packets so
  the candidate can show whether it reduces smooth/metallic instability under
  motion before any default beauty promotion.

### Reflection Resolver Candidate Mouse-Jitter Packet - 2026-06-05

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 3 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_candidate_mouse_jitter_packet_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_candidate_mouse_jitter_packet_20260605`.
- stability motion mode: `mouse_jitter`.
- capture sequence count: `3`.
- captured views: `19`.
- evidence rows: `190`.
- frame-report failures: `0`.
- measured debug views: `19`.
- metric failures: `0`.
- key motion metrics:
  - `reflection_source_weights` mean RGB:
    `0.000001, 0.069131, 0.060542`.
  - `reflection_stability_policy` mean RGB:
    `0.159872, 0.110794, 0.343249`.
  - `reflection_resolver_candidate` mean RGB:
    `0.619970, 0.591707, 0.550361`.
  - `reflection_resolver_candidate_delta` mean RGB:
    `0.000000055, 0.000000043, 0.000000034`.
  - `reflection_resolver_candidate_delta` nonblack ratio:
    `0.00000217`.

Interpretation:

- Mouse-jitter packet generation works with the candidate and delta views.
- The candidate remains effectively identical to current output on this
  gallery frame under the captured jitter sequence.
- This is still not a promotion proof. The next useful packet should use
  cross-family and/or a reflection-stress scene where SSR/RT ownership changes
  enough for the candidate delta to become meaningful.

## Full Scene Shader Pipeline V2 Reflection Candidate Signal Audit - 2026-06-05

Purpose:

- Make the reflection candidate proof honest by distinguishing packet success
  from meaningful reflection-source/candidate signal.
- Surface when a family has no SSR/RT/IBL post-reflection source weight, or
  when the candidate path is wired but produces near-zero delta.

Implemented:

- Added `tools/analyze_full_scene_shader_reflection_candidate_signal.py`.
  - consumes `debug_view_metrics.json`.
  - audits `reflection_source_weights`,
    `reflection_resolver_candidate`, and
    `reflection_resolver_candidate_delta`.
  - emits `reflection_candidate_signal.json` and
    `reflection_candidate_signal.md`.
  - reports per-family source luma, source nonblack ratio, delta luma, delta
    nonblack ratio, source-signal family count, candidate-delta family count,
    warnings, and failures.
- Updated `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
  - every V2 packet now emits the reflection candidate signal report after
    debug-view metrics.
- Updated `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.
  - requires the signal analyzer and packet output names.

Validation:

```powershell
python tools\analyze_full_scene_shader_reflection_candidate_signal.py --metrics build\captures\full_scene_shader_pipeline_v2_reflection_candidate_cross_family_mouse_jitter_packet_20260605\debug_view_metrics.json --output-json build\captures\full_scene_shader_pipeline_v2_reflection_candidate_cross_family_mouse_jitter_packet_20260605\reflection_candidate_signal.json --output-md build\captures\full_scene_shader_pipeline_v2_reflection_candidate_cross_family_mouse_jitter_packet_20260605\reflection_candidate_signal.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_candidate_signal_integrated_packet_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_reflection_candidate_signal.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Results:

- standalone signal analyzer: passed on the previous cross-family packet.
- integrated V2 packet with signal report: passed.
- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.

Integrated packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_candidate_signal_integrated_packet_20260605`.
- requested families: `5`.
- captured views: `30`.
- evidence rows: `300`.
- frame-report failures: `0`.
- measured debug views: `30`.
- metric failures: `0`.
- source-signal families: `1`.
- candidate-delta families: `0`.
- signal warnings: `5`.
- signal statuses:
  - `gallery`: `wired_no_delta`, source luma `0.05381363`,
    delta luma `0.00000004`.
  - `kitchen`: `no_reflection_source_signal`.
  - `office`: `no_reflection_source_signal`.
  - `gym`: `no_reflection_source_signal`.
  - `concert`: `no_reflection_source_signal`.

Current interpretation:

- The cross-family packet succeeds technically, but it proves the current V2
  candidate is not being meaningfully exercised outside gallery.
- The next real renderer architecture target is not another candidate tweak;
  it is making model-authored families feed scene-local reflection/probe/SSR/RT
  source signal into the post resolver, then rerunning this signal audit.

## Full Scene Shader Pipeline V2 Scene-Local Source Plumbing Slice - 2026-06-05

Purpose:

- Start `FSSP-V2-004C` by fixing the source-signal gap found in the reflection
  candidate audit.
- Keep default beauty unchanged.
- Make scene-local probe radiance visible to the post-stage source contract so
  enclosed model-authored families do not look like they have zero authorized
  reflection source merely because external IBL is disabled.

Diagnosis:

- Deferred lighting already receives local probe radiance through
  `g_LocalProbeParams`.
- Post-process `reflection_source_weights` only saw SSR, RT, and
  `g_EnvParams.y`.
- Enclosed model-authored families intentionally set external IBL/specular to
  zero, so their post-stage source signal was zero even when their scene
  profile declared local reflection probes.

Implemented:

- `ShaderTypes.h`
  - appended `FrameConstants::localProbeParams`.
  - x = local probe diffuse scale.
  - y = local probe specular scale.
  - z = local probe radiance enabled.
  - w = reserved.
- `Renderer_FramePostConstants.cpp`
  - populates `localProbeParams` from `RendererEnvironmentState`.
- `PostProcess.hlsl`
  - reads `g_LocalProbeParams`.
  - computes `sceneLocalReflectionPotential` from local probe specular scale,
    material reflectance, and gloss.
  - updates reflection-owner debug view `46` so local scene probe ownership can
    light up before generic fallback.
  - updates reflection-source debug view `56` so blue now reports authorized
    scene-local or IBL/prelit reflection potential.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set CORTEX_SKIP_ASSET_SYNC=1 && ninja -C build CortexEngine -v'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_scene_local_source_plumbing_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Release `CortexEngine` target build: passed through direct `ninja`.
- V2 cross-family packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_scene_local_source_plumbing_packet_20260605`.
- requested families: `5`.
- captured views: `30`.
- evidence rows: `300`.
- frame-report failures: `0`.
- measured debug views: `30`.
- metric failures: `0`.
- source-signal families: `5`.
- candidate-delta families: `0`.
- signal warnings: `5`.
- signal statuses:
  - `gallery`: `wired_no_delta`, source luma `0.05381363`,
    delta luma `0.00000004`.
  - `kitchen`: `wired_no_delta`, source luma `0.00072311`,
    source nonblack `0.17523763`.
  - `office`: `wired_no_delta`, source luma `0.00023052`,
    source nonblack `0.05801107`.
  - `gym`: `wired_no_delta`, source luma `0.00029567`,
    source nonblack `0.10023872`.
  - `concert`: `wired_no_delta`, source luma `0.00089806`,
    source nonblack `0.16962348`.

Current interpretation:

- The source plumbing slice achieved its immediate gate:
  `source_signal_family_count` increased from `1/5` to `5/5`.
- This is not a beauty promotion and not proof of visible reflection
  improvement.
- Candidate delta remains zero in model-authored families because the
  candidate resolver still only changes SSR/RT blend behavior; the local probe
  term is now visible to evidence but not yet composed by the V2 candidate
  resolver.
- The next useful slice is `FSSP-V2-005C`: route scene-local probe/fallback
  sources into a real candidate reflection resolver path and keep it opt-in
  until motion packets show stable improvement.

### Reflection Source Authority Packet - 2026-06-05

Purpose:

- Make the source signal more honest by separating external environment,
  scene-local probe, and SSR/RT authority.
- Keep this as a debug/evidence view only.

Implemented:

- `PostProcess.hlsl`
  - debug view `60`:
    - R = authorized external IBL/prelit source potential.
    - G = scene-local probe source potential.
    - B = screen/ray source potential.
- `Renderer_DebugSettings.cpp`
  - `kMaxDebugViewMode = 60`.
  - label `PostReflectionSourceAuthority`.
- Packet runners:
  - added `reflection_source_authority`.
- Frame-report contract/checker:
  - require `reflection_source_authority` alongside the existing reflection
    source/candidate views.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\analyze_full_scene_shader_reflection_candidate_signal.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_source_authority_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Build note:

- Use quoted `set "CORTEX_SKIP_ASSET_SYNC=1"` in `cmd.exe`.
- The unquoted `set CORTEX_SKIP_ASSET_SYNC=1 && ...` form can pass a trailing
  space and miss the exact CMake fast path, causing `sync_assets.cmake` to scan
  the full asset tree and appear stuck.

Results:

- static checker: passed.
- plan validator: passed.
- Python compile: passed.
- Release build: passed.
- runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_source_authority_packet_20260605`.
- captured views: `35`.
- evidence rows: `350`.
- failures: `0`.
- measured debug views: `35`.
- metric failures: `0`.
- source-signal families: `5`.
- candidate-delta families: `0`.

Authority view metrics:

| Family | Mean RGB | Mean Luma | Nonblack |
|---|---:|---:|---:|
| gallery | `0.0605,0.0156,0.0691` | `0.0290` | `0.3228` |
| kitchen | `0.0000,0.0100,0.0000` | `0.0072` | `0.1752` |
| office | `0.0000,0.0032,0.0000` | `0.0023` | `0.0580` |
| gym | `0.0000,0.0041,0.0000` | `0.0029` | `0.1002` |
| concert | `0.0000,0.0124,0.0000` | `0.0089` | `0.1696` |

Current interpretation:

- The authority split behaves correctly:
  - model-authored enclosed families show green scene-local probe authority and
    no red external-IBL authority.
  - gallery carries external/RT authority as expected.
- This closes the evidence side of `FSSP-V2-004C` for local source visibility.
- The next implementation should not add more views; it should add a candidate
  reflection resolve path that actually consumes the local probe authority term
  in a physically bounded way.

### Local Probe Candidate Resolver Packet - 2026-06-05

Purpose:

- Move from source evidence to an opt-in candidate resolver behavior.
- Make the V2 reflection candidate consume scene-local probe authority in
  model-authored enclosed scenes.
- Keep default beauty on the existing resolver.

Implemented:

- `PostProcess.hlsl`
  - adds a bounded candidate-only local probe sheen term.
  - gates it by:
    - scene-local reflection potential.
    - reflection stability scale.
    - material `SurfaceReflectionCeiling`.
    - existing SSR/RT candidate weight, so local probe does not fight stronger
      screen/ray owners.
  - derives candidate local probe color from already-lit scene color plus
    ambient scene-local floor because post still does not bind probe cubemaps.
  - `reflection_resolver_candidate` and
    `reflection_resolver_candidate_delta` now move on local-probe-only
    model-authored families.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_local_probe_candidate_weighted_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static checker: passed.
- plan validator: passed.
- runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_local_probe_candidate_weighted_packet_20260605`.
- captured views: `35`.
- evidence rows: `350`.
- failures: `0`.
- source-signal families: `5`.
- candidate-delta families: `5`.
- warnings: `0`.

Candidate signal:

| Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| gallery | `meaningful_delta` | `0.05381363` | `0.32276584` | `0.00343681` | `0.08734592` |
| kitchen | `meaningful_delta` | `0.00072311` | `0.17523763` | `0.00149478` | `0.08704644` |
| office | `meaningful_delta` | `0.00023052` | `0.05801107` | `0.00032100` | `0.00128472` |
| gym | `meaningful_delta` | `0.00029567` | `0.10023872` | `0.00052648` | `0.01372613` |
| concert | `meaningful_delta` | `0.00089806` | `0.16962348` | `0.00219636` | `0.11669922` |

Authority remains local for enclosed model-authored families:

| Family | Authority Mean RGB | Authority Nonblack |
|---|---:|---:|
| kitchen | `0.0000,0.0100,0.0000` | `0.1752` |
| office | `0.0000,0.0032,0.0000` | `0.0580` |
| gym | `0.0000,0.0041,0.0000` | `0.1002` |
| concert | `0.0000,0.0124,0.0000` | `0.1696` |

Current interpretation:

- This completes the first candidate-behavior slice after source plumbing:
  the V2 candidate now consumes local probe authority across all tested
  families.
- It still does not promote V2 beauty to default.
- The next slice should make the local probe candidate less approximate by
  passing actual probe/source color or resolved local reflection radiance into
  post, then run motion/stability gates on glossy surfaces before any beauty
  promotion.

## 2026-06-05 Post-Owned Local Probe Source Slice

Purpose:

- Remove the candidate resolver's dependence on final scene color as a fake
  local probe source.
- Keep default beauty unchanged.
- Let post-process consume an owned reflection source contract:
  - authorized external environment radiance when the scene contract permits
    IBL/specular environment.
  - scene-local procedural room-probe radiance for enclosed scenes where
    external HDRI bleed is not authorized.

Implemented:

- `PostProcess.hlsl`
  - declares existing `space1` environment SRVs:
    - `g_EnvDiffuse`, `t1`.
    - `g_EnvSpecular`, `t2`.
  - adds post-local environment direction/mip helpers.
  - adds `ComputePostSceneLocalProbeSpecular`, matching the deferred lighting
    enclosed-scene local probe fallback palette.
  - adds `SamplePostSceneLocalReflectionSource`.
  - rewires the V2 candidate-only local probe sheen term to consume that owned
    source instead of deriving color from `reflectionBaseColor` and ambient.
- Default beauty remains unchanged.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\analyze_full_scene_shader_reflection_candidate_signal.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static checker: passed.
- plan validator: passed.
- Python compile: passed.
- Release build: passed.
- runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_packet_20260605`.
- captured views: `35`.
- measured debug views: `35`.
- metric failures: `0`.
- source-signal families: `5/5`.
- candidate-delta families: `5/5`.
- warnings: `0`.

Candidate signal:

| Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| gallery | `meaningful_delta` | `0.05381363` | `0.32276584` | `0.01109893` | `0.08724501` |
| kitchen | `meaningful_delta` | `0.00072311` | `0.17523763` | `0.00207694` | `0.09141168` |
| office | `meaningful_delta` | `0.00023052` | `0.05801107` | `0.00062022` | `0.01469184` |
| gym | `meaningful_delta` | `0.00029567` | `0.10023872` | `0.00067247` | `0.01252604` |
| concert | `meaningful_delta` | `0.00089806` | `0.16962348` | `0.00249549` | `0.12392687` |

Current interpretation:

- The V2 candidate now consumes an owned post-visible local reflection source.
- Enclosed scenes still avoid external IBL/HDRI bleed because the post source
  falls back to scene-local room radiance unless the environment is explicitly
  authorized.
- This is still candidate/debug output only. Do not promote to default beauty
  until motion/stability packets compare glossy surfaces under camera sweeps
  and the user accepts the visual direction.

### Camera-Sweep Motion Proof - 2026-06-05

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 120 -CaptureFrame 60 -CaptureSequenceCount 3 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_camera_sweep_packet_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_camera_sweep_packet_20260605`.
- source-signal families: `5/5`.
- candidate-delta families: `5/5`.
- warnings: `0`.
- failures: `0`.

Camera-sweep candidate signal:

| Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| gallery | `meaningful_delta` | `0.05388108` | `0.32298611` | `0.01119955` | `0.08748481` |
| kitchen | `meaningful_delta` | `0.00071762` | `0.17505534` | `0.00205583` | `0.08955838` |
| office | `meaningful_delta` | `0.00022964` | `0.05770833` | `0.00061088` | `0.01438151` |
| gym | `meaningful_delta` | `0.00028623` | `0.09729167` | `0.00063635` | `0.01116862` |
| concert | `meaningful_delta` | `0.00088398` | `0.16769206` | `0.00243640` | `0.12174154` |

Interpretation:

- The owned local probe candidate remains measurable under camera-sweep packet
  motion.
- This is stronger than the prior mouse-jitter packet, but still not default
  promotion. The next gate should compare actual beauty/candidate stability on
  explicit glossy/metal/glass close-up stress surfaces.

### Sequence Stability Analyzer Integration - 2026-06-05

Implemented:

- `tools/analyze_full_scene_shader_sequence_stability.py`
  - reads packet `manifest.json`.
  - consumes each result's `capture_sequence`.
  - measures consecutive frame-to-frame mean absolute luma/RGB deltas.
  - compares `reflection_resolver_candidate` motion delta against `beauty`
    per family.
  - emits:
    - `sequence_stability.json`.
    - `sequence_stability.md`.
- `tools/run_full_scene_shader_pipeline_v2_packet.ps1`
  - now runs sequence stability after debug metrics and reflection signal.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`
  - now requires the sequence stability analyzer and packet outputs.

Integrated smoke:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen" -ViewFilter "beauty,reflection_source_weights,reflection_source_authority,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_sequence_stability_integrated_smoke_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_sequence_stability_integrated_smoke_20260605`.
- source-signal families: `2/2`.
- candidate-delta families: `2/2`.
- sequence stability warnings: `0`.
- sequence stability failures: `0`.

Sequence stability smoke:

| Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| gallery | `0.00311596` | `0.00310690` | `0.997` |
| kitchen | `0.00385527` | `0.00383711` | `0.995` |

Current interpretation:

- V2 packets now carry first-class sequence stability evidence.
- The current owned local-probe candidate did not add motion instability in
  the integrated smoke or the full cross-family camera-sweep packet.
- This still does not promote default beauty; it only strengthens the harness
  needed for safe promotion later.

### Glossy Surface Stress Packet Harness - 2026-06-05

Implemented:

- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`
  - adds `-StressSceneFilter` with comma-separated `scene:camera_bookmark`
    targets.
  - validates stress scene ids and bookmarks against
    `assets/config/showcase_scenes.json`.
  - emits each stress view as its own packet family, for example
    `stress_material_lab_metal_closeup`.
  - adds `-StressSceneOnly` so expensive glossy stress packets can run without
    the normal family set.
- `tools/run_full_scene_shader_pipeline_v2_packet.ps1`
  - forwards `-StressSceneFilter` and `-StressSceneOnly` into the packet runner.

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:metal_closeup,glass_water_courtyard:water_closeup" -ViewFilter "beauty,roughness,metallic,reflection_source_weights,reflection_source_authority,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_glossy_stress_only_smoke2_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_sequence_stability.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\analyze_full_scene_shader_reflection_candidate_signal.py
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_glossy_stress_only_smoke2_20260605`.
- captured views: `21`.
- stress families: `3`.
- V2 frame evidence failures: `0`.
- debug metric failures: `0`.
- sequence stability warnings/failures: `0/0`.
- reflection candidate warnings/failures: `2/0`.

Sequence stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty | Delta View Luma Delta |
|---|---:|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `0.00158056` | `0.00158056` | `1.000` | `0.00000000` |
| `stress_material_lab_metal_closeup` | `0.00246855` | `0.00246855` | `1.000` | `0.00000001` |
| `stress_rt_showcase_reflection_closeup` | `0.00803787` | `0.00798941` | `0.994` | `0.00363569` |

Reflection candidate signal:

| Stress Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `wired_no_delta` | `0.00024963` | `0.03081489` | `0.00000000` | `0.00000000` |
| `stress_material_lab_metal_closeup` | `wired_no_delta` | `0.00527105` | `0.17212348` | `0.00000000` | `0.00000000` |
| `stress_rt_showcase_reflection_closeup` | `meaningful_delta` | `0.12203094` | `0.41034071` | `0.02113077` | `0.17418837` |

Current interpretation:

- The stress harness now gives V2 a repeatable way to test hard glossy,
  metallic, glass, and water camera bookmarks instead of only broad scene
  families.
- The current candidate remains motion-stable on these closeups.
- The current candidate is too weak on `material_lab:metal_closeup` and
  `glass_water_courtyard:water_closeup`: the source debug views are wired, but
  the candidate beauty delta is effectively zero.
- Do not promote V2 reflections to default beauty. The next root refactor is
  to make local reflection radiance materially active on these stress surfaces,
  either by binding actual local probe radiance into post or by adding a
  resolved local reflection radiance buffer before post.

### Authorized Reflection Source Candidate Activation - 2026-06-05

Root cause:

- The stress packet reported source signal through `reflection_source_weights`
  for authorized external/prelit reflection sources.
- The V2 candidate sheen gate only used `sceneLocalReflectionPotential`.
- Therefore scenes whose valid source was authorized IBL/prelit radiance could
  show source signal but receive zero candidate blend, even though
  `SamplePostSceneLocalReflectionSource` was already able to sample the
  authorized source.

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - changes the V2 candidate source-sheen gate from
    `sceneLocalReflectionPotential` to `authorizedPrelitReflectionPotential`.
  - keeps the owned-source sampler unchanged: it only samples external IBL when
    `g_EnvParams` explicitly authorizes it, otherwise it falls back to
    scene-local radiance.
  - default beauty remains unchanged because this path is still debug/candidate
    only.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:metal_closeup,glass_water_courtyard:water_closeup" -ViewFilter "beauty,roughness,metallic,reflection_source_weights,reflection_source_authority,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_authorized_source_stress_smoke_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_authorized_source_stress_smoke_20260605`.
- source-signal families: `3/3`.
- candidate-delta families: `3/3`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.

Candidate signal after patch:

| Stress Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `meaningful_delta` | `0.00024963` | `0.03081489` | `0.00047534` | `0.00544705` |
| `stress_material_lab_metal_closeup` | `meaningful_delta` | `0.00527105` | `0.17212348` | `0.01294731` | `0.09842122` |
| `stress_rt_showcase_reflection_closeup` | `meaningful_delta` | `0.12203094` | `0.41034071` | `0.02150589` | `0.17567708` |

Sequence stability after patch:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `0.00158056` | `0.00158097` | `1.000` |
| `stress_material_lab_metal_closeup` | `0.00246855` | `0.00244604` | `0.991` |
| `stress_rt_showcase_reflection_closeup` | `0.00803785` | `0.00798630` | `0.994` |

Current interpretation:

- The stress packet no longer has wired-but-inactive reflection candidate
  sources.
- The candidate still does not increase motion instability versus beauty in
  the tested glossy stress closeups.
- This is stronger evidence for the candidate path, but not default-ready:
  the next gate is a broader glossy packet including `material_lab:glass_emissive`,
  `glass_water_courtyard:glass_canopy`, and
  `dragon_over_water:floor_reflection_closeup`, then visual review.

### Broader Glossy Stress Gate - 2026-06-05

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive,glass_water_courtyard:glass_canopy,dragon_over_water:floor_reflection_closeup" -ViewFilter "beauty,roughness,metallic,surface_class,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605`.
- captured views: `36`.
- stress families: `4`.
- source-signal families: `4/4`.
- candidate-delta families: `4/4`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.

Candidate signal:

| Stress Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `meaningful_delta` | `0.04915582` | `0.29987956` | `0.00955596` | `0.07552409` |
| `stress_glass_water_courtyard_glass_canopy` | `meaningful_delta` | `0.00635910` | `0.37000543` | `0.00991124` | `0.16202257` |
| `stress_material_lab_glass_emissive` | `meaningful_delta` | `0.00729248` | `0.18290799` | `0.01555773` | `0.13664931` |
| `stress_rt_showcase_reflection_closeup` | `meaningful_delta` | `0.12236592` | `0.41124783` | `0.02144538` | `0.17576714` |

Sequence stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty | Delta View Luma Delta |
|---|---:|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00292183` | `0.00289800` | `0.992` | `0.00107355` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00121205` | `0.00118411` | `0.977` | `0.00031130` |
| `stress_material_lab_glass_emissive` | `0.00152219` | `0.00149388` | `0.981` | `0.00025119` |
| `stress_rt_showcase_reflection_closeup` | `0.00487044` | `0.00480172` | `0.986` | `0.00260679` |

Current interpretation:

- The authorized-source candidate now survives a broader glossy/glass/water
  packet without wired-no-delta regressions.
- Candidate motion remains below beauty motion in all four stress bookmarks.
- The next promotion step should not be another shader gate alone. It should be
  a review packet/contact sheet comparing default beauty against the V2
  candidate on these stress views, then a decision on whether to expose a
  runtime candidate toggle for interactive review.

### Stress Review Sheet Export - 2026-06-05

Implemented:

- `tools/build_full_scene_shader_v2_review_sheet.py`
  - reads a V2 packet `manifest.json`.
  - groups captures by packet family/stress bookmark.
  - exports a side-by-side review sheet for selected views.
  - default columns:
    `beauty`, `reflection_resolver_candidate`,
    `reflection_resolver_candidate_delta`, `reflection_source_authority`, and
    `reflection_source_weights`.
  - emits JSON/Markdown summaries with missing-cell accounting.

Validation:

```powershell
python tools\build_full_scene_shader_v2_review_sheet.py --manifest build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\manifest.json --output build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\v2_stress_review_sheet.jpg --summary-json build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\v2_stress_review_sheet.json --summary-md build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\v2_stress_review_sheet.md
python -m py_compile tools\build_full_scene_shader_v2_review_sheet.py
```

Generated review artifacts:

- image:
  `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605/v2_stress_review_sheet.jpg`.
- JSON:
  `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605/v2_stress_review_sheet.json`.
- Markdown:
  `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605/v2_stress_review_sheet.md`.

Review summary:

- rows: `4`.
- views:
  `beauty`, `reflection_resolver_candidate`,
  `reflection_resolver_candidate_delta`, `reflection_source_authority`,
  `reflection_source_weights`.
- missing cells: `0`.

Current interpretation:

- The V2 candidate now has a human-readable review packet, not only metrics.
- The visual delta is subtle but consistently localized to the intended
  reflective/glossy regions.
- Next work can either expose an interactive runtime candidate toggle or deepen
  the candidate into a resolved local reflection radiance buffer before any
  default-beauty promotion.

### Interactive Candidate Beauty Toggle - 2026-06-05

Implemented:

- `src/Graphics/RendererPostProcessState.h`
  - adds default-off `v2ReflectionCandidateEnabled`.
- `src/Graphics/Renderer_DiagnosticsTypes.h` and
  `src/Graphics/Renderer_Diagnostics.cpp`
  - expose the flag in `RendererFeatureState`.
- `src/Graphics/Renderer_FeatureSettings.cpp` and
  `src/Graphics/Renderer.h`
  - add `SetV2ReflectionCandidateEnabled` and
    `IsV2ReflectionCandidateEnabled`.
- `src/Graphics/RendererControlApplier.h` and
  `src/Graphics/RendererControlApplier_Runtime.cpp`
  - add `RendererFeatureToggle::V2ReflectionCandidate`.
- `src/UI/PerformanceWindow.cpp`
  - adds a P-menu checkbox:
    `V2 reflection candidate (review)`.
- `src/Graphics/Renderer_FramePostConstants.cpp`
  - packs the review toggle into post bit `24`.
  - supports `CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY=1` for packet validation.
- `assets/shaders/PostProcess.hlsl`
  - uses the V2 candidate as beauty only when bit `24` is set or debug view
    `58` is active.
  - default beauty remains unchanged.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
$env:CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive" -ViewFilter "beauty,reflection_resolver_candidate,reflection_resolver_candidate_delta,reflection_source_weights,reflection_source_authority" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_candidate_beauty_toggle_smoke_20260605; Remove-Item Env:\CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY -ErrorAction SilentlyContinue
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_candidate_beauty_toggle_smoke_20260605`.
- source-signal families: `2/2`.
- candidate-delta families: `2/2`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.
- logs confirm:
  `CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY set; V2 reflection candidate drives beauty (review)`.

Candidate-beauty signal:

| Stress Family | Delta Luma | Delta Nonblack |
|---|---:|---:|
| `stress_material_lab_glass_emissive` | `0.01586410` | `0.13685547` |
| `stress_rt_showcase_reflection_closeup` | `0.02374713` | `0.17570312` |

Candidate-beauty sequence stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_material_lab_glass_emissive` | `0.00257806` | `0.00257806` | `1.000` |
| `stress_rt_showcase_reflection_closeup` | `0.00802251` | `0.00802251` | `1.000` |

Current interpretation:

- The V2 reflection candidate can now be reviewed interactively from the P menu
  without changing defaults.
- The env hook proves the same beauty-toggle path in packet automation.
- The candidate is still not default-ready; user/visual review should happen
  through the checkbox before promotion.

### Structured Scene-Local Reflection Candidate - 2026-06-05

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - adds `ComputePostSceneLocalReflectionStructure`.
  - adds `ResolveV2SceneLocalReflectionRadiance`.
  - replaces the older one-source V2 local sheen call with the resolved
    scene-local radiance path.
  - uses stable reflection-direction/world-position terms for broad
    architectural breakup, horizon/floor bounce, and key-light strips.
  - keeps the path behind debug view `58` or the default-off
    `V2 reflection candidate (review)` toggle.
  - default beauty remains unchanged when the review toggle is off.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive,glass_water_courtyard:glass_canopy,dragon_over_water:floor_reflection_closeup" -ViewFilter "beauty,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_struct_refl_20260605
python tools\build_full_scene_shader_v2_review_sheet.py --manifest build\captures\v2_struct_refl_20260605\manifest.json --output build\captures\v2_struct_refl_20260605\v2_structured_reflection_review_sheet.jpg --summary-json build\captures\v2_struct_refl_20260605\v2_structured_reflection_review_sheet.json --summary-md build\captures\v2_struct_refl_20260605\v2_structured_reflection_review_sheet.md
$env:CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive" -ViewFilter "beauty,reflection_resolver_candidate,reflection_resolver_candidate_delta,reflection_source_weights,reflection_source_authority" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_struct_refl_toggle_20260605; Remove-Item Env:\CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY -ErrorAction SilentlyContinue
```

Results:

- packet:
  `build/captures/v2_struct_refl_20260605`.
- captured views: `24`.
- source-signal families: `4/4`.
- candidate-delta families: `4/4`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.
- review sheet:
  `build/captures/v2_struct_refl_20260605/v2_structured_reflection_review_sheet.jpg`.
- env/P-menu beauty-toggle path packet:
  `build/captures/v2_struct_refl_toggle_20260605`.
- beauty-toggle source-signal families: `2/2`.
- beauty-toggle candidate-delta families: `2/2`.
- beauty-toggle reflection candidate warnings/failures: `0/0`.
- beauty-toggle sequence stability warnings/failures: `0/0`.

Candidate delta compared with the previous broader glossy gate:

| Stress Family | Previous Delta Luma | Structured Delta Luma | Previous Delta Nonblack | Structured Delta Nonblack |
|---|---:|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00955596` | `0.01253093` | `0.07552409` | `0.07989692` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00991124` | `0.01315398` | `0.16202257` | `0.16984158` |
| `stress_material_lab_glass_emissive` | `0.01555773` | `0.01941374` | `0.13664931` | `0.13757161` |
| `stress_rt_showcase_reflection_closeup` | `0.02144538` | `0.02852098` | `0.17576714` | `0.18127604` |

Structured candidate stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00291844` | `0.00289316` | `0.991` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00121205` | `0.00117808` | `0.972` |
| `stress_material_lab_glass_emissive` | `0.00152219` | `0.00148692` | `0.977` |
| `stress_rt_showcase_reflection_closeup` | `0.00487044` | `0.00478890` | `0.983` |

Operational note:

- An initial packet using the longer output folder
  `full_scene_shader_pipeline_v2_structured_reflection_candidate_20260605`
  failed because one dragon delta BMP capture path was too long to open.
  The same packet passed under the shorter `v2_struct_refl_20260605` root.

Current interpretation:

- The V2 review candidate now has a stronger scene-local glossy response on
  all four stress bookmarks.
- The stronger candidate still does not increase motion instability versus
  beauty in the stress packet.
- This is still a candidate/review path, not default beauty.
- Next work should either run interactive user review from the P-menu checkbox
  or begin the real render-graph reflection-radiance buffer that can replace
  the post-only approximation.

### Local Reflection Radiance Buffer Kernel - 2026-06-05

Implemented:

- `assets/shaders/LocalReflectionRadiance.hlsl`
  - new compute shader kernel for the first render-graph-ready local
    reflection radiance buffer.
  - consumes depth, normal/roughness, emissive/metallic, material ext channels,
    scene color, and authorized environment specular input.
  - writes `g_OutputRadiance` as `rgb = resolved local reflection radiance`,
    `a = confidence/admission weight`.
  - uses the same material/surface classification vocabulary as
    `PostProcess.hlsl` through `SurfaceClassification.hlsli`.
  - reconstructs stable world position and reflection direction from
    `FrameConstants`.
  - computes local architectural reflection structure, floor/horizon bounce,
    key-light strips, material-class boosts, local probe confidence, and
    optional authorized IBL contribution.
  - rejects sky/background pixels and zero-confidence pixels up front.
  - soft-limits extreme radiance with the existing RT reflection firefly clamp
    contract.

Binding contract for the next C++ pass:

| Resource | Register | Purpose |
|---|---:|---|
| `g_Depth` | `t0` | surface ownership / world reconstruction |
| `g_NormalRoughness` | `t1` | reflection direction and gloss |
| `g_EmissiveMetallic` | `t2` | metallic channel |
| `g_MaterialExt1` | `t3` | transmission channel |
| `g_MaterialExt2` | `t4` | surface class and scene material class |
| `g_SceneColor` | `t5` | reserved for later scene-color-aware radiance |
| `g_EnvSpecular` | `t6` | authorized IBL/specular source |
| `g_OutputRadiance` | `u0` | local reflection radiance buffer |
| `g_Sampler` | `s0` | shared linear sampler |
| `FrameConstants` | `b1` | camera, lighting, material policy, local probe state |

Validation:

```powershell
build\vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe -T cs_6_3 -E CSMain -O3 -Qstrip_debug -I assets\shaders -Fo build\bin\assets\shaders\LocalReflectionRadiance.dxil assets\shaders\LocalReflectionRadiance.hlsl
```

Result:

- DXC compile passed.
- Generated:
  `build/bin/assets/shaders/LocalReflectionRadiance.dxil`.

Current interpretation:

- We now have a compiled local reflection radiance producer shader, not only a
  post-process approximation.
- It is deliberately not bound into default rendering yet, because the current
  source tree has broad unrelated C++ changes and the first safe checkpoint is
  the stable shader/resource contract.
- Next work should add the C++ resource state:
  local reflection radiance texture + SRV/UAV descriptors + render-graph
  dispatch pass + post-process SRV binding behind the existing V2 review toggle.

### Reserved Post Local Reflection Radiance Slot - 2026-06-05

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - adds `g_LocalReflectionRadiance` at `t13`.
  - adds debug view `61`:
    `local_reflection_radiance`.
- `src/Graphics/RHI/DX12Pipeline.cpp`
  - widens the graphics/post SRV root-signature range from `t0-t12` to
    `t0-t13`.
- `src/Graphics/RendererTemporalScreenState.h`
  - widens persistent post-process SRV tables from `13` to `14` slots.
- `src/Graphics/Passes/PostProcessPass.*`
  - writes slot `13` as a null `R16G16B16A16_FLOAT` SRV until a producer exists.
- `src/Graphics/Passes/PostProcessGraphPass.*` and
  `src/Graphics/Renderer_RenderGraphEndFrame.cpp`
  - add a render-graph read/handle surface for the future local radiance
    producer while binding null for now.
- `src/Graphics/Renderer_Descriptors.cpp`
  - binds the runtime non-graph post descriptor surface with a null local
    radiance SRV.
- `src/Graphics/Renderer_DebugSettings.cpp`
  - raises the debug-view ceiling to `61` and adds the
    `LocalReflectionRadiance` label.
- `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
  and V2 packet scripts/checkers
  - include `local_reflection_radiance` as an official reflection debug/evidence
    view.

Diagnosis fixed during this slice:

- First packet attempt failed PSO creation because `PostProcess.hlsl` declared
  `t13` while the graphics root signature only exposed `13` descriptors.
- After the root-signature fix, the `local_reflection_radiance` capture matched
  `reflection_source_authority`. Root cause was a stale runtime debug clamp:
  requested debug view `61` was clamped to `60`.
- The corrected packet proves debug view `61` now reaches the shader and samples
  the reserved null SRV independently from view `60`.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
build\vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe -T cs_6_3 -E CSMain -O3 -Qstrip_debug -I assets\shaders -Fo build\bin\assets\shaders\LocalReflectionRadiance.dxil assets\shaders\LocalReflectionRadiance.hlsl
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup" -ViewFilter "beauty,local_reflection_radiance,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 50 -CaptureFrame 25 -CaptureSequenceCount 1 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_local_radiance_slot_smoke4_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- packet:
  `build/captures/v2_local_radiance_slot_smoke4_20260605`.
- captured views: `6`.
- V2 evidence rows: `60`.
- V2 packet failures: `0`.
- debug metrics:
  - `reflection_source_authority`: luma `0.0661`, nonblack `0.4109`.
  - `local_reflection_radiance`: luma `0.0000`, nonblack `0.0000`.
- reflection candidate signal remains valid:
  source luma `0.12212419`, delta luma `0.02840133`.
- `ctest` found no registered tests in this build, so it is not meaningful
  coverage for this slice.

Current stopping position:

- The post-process binding surface for a render-graph-owned local reflection
  radiance buffer is reserved, validated, and packet-visible.
- This does not yet produce local radiance. The next implementation pass should
  allocate the `R16G16B16A16_FLOAT` local radiance texture, dispatch
  `LocalReflectionRadiance.hlsl`, bind the produced SRV into slot `13`, and
  make debug view `61` show nonzero producer-owned signal.

### Render-Graph Local Reflection Radiance Producer - 2026-06-05

Implemented:

- `src/Graphics/Passes/LocalReflectionRadiancePass.*`
  - adds a compute render-graph pass named `LocalReflectionRadiance`.
  - creates a transient `R16G16B16A16_FLOAT` UAV/SRV radiance target.
  - reads depth, normal/roughness, emissive/metallic, material ext channels,
    scene color, and optional active environment specular.
  - dispatches `LocalReflectionRadiance.hlsl`.
- `src/Graphics/RendererLocalReflectionState.h`
  - owns per-frame local radiance SRV/UAV descriptor tables.
- `src/Graphics/RendererPipelineState.h` and
  `src/Graphics/Renderer_ScreenComputePipelineSetup.cpp`
  - add and compile the local radiance compute pipeline.
- `src/Graphics/Renderer_Descriptors.cpp`
  - allocates persistent per-frame local radiance compute descriptor tables.
- `src/Graphics/Renderer_RenderGraphEndFrame.cpp`
  - inserts the `LocalReflectionRadiance` pass before post when the GBuffer
    inputs are available.
  - passes the graph-produced radiance handle into post.
- `src/Graphics/Passes/PostProcessGraphPass.*`
  - resolves the graph-produced radiance resource at post execution time and
    binds it to slot `13`.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
build\vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe -T cs_6_3 -E CSMain -O3 -Qstrip_debug -I assets\shaders -Fo build\bin\assets\shaders\LocalReflectionRadiance.dxil assets\shaders\LocalReflectionRadiance.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup" -ViewFilter "beauty,local_reflection_radiance,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 50 -CaptureFrame 25 -CaptureSequenceCount 1 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_local_radiance_producer_smoke1_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py
ctest --test-dir build --output-on-failure -C Release
```

Results:

- packet:
  `build/captures/v2_local_radiance_producer_smoke1_20260605`.
- captured views: `6`.
- V2 packet evidence: passed.
- debug view `61` changed from the previous null-slot proof to producer signal:
  - previous `local_reflection_radiance`: luma `0.0000`, nonblack `0.0000`.
  - producer `local_reflection_radiance`: luma `0.0950`, nonblack `1.0000`.
- `reflection_source_authority` remains distinct:
  luma `0.0661`, nonblack `0.4109`.
- candidate signal remains valid:
  source luma `0.12212419`, delta luma `0.02840133`.
- logs confirm:
  `Local reflection radiance compute pipeline created successfully`.
- `ctest` still found no registered tests in this build.

Current stopping position:

- The renderer now has a real render-graph-owned local reflection radiance
  producer and packet-visible debug output.
- Default beauty is still unchanged.
- Next work should feed `g_LocalReflectionRadiance` into the V2 reflection
  candidate path behind the existing review toggle, then rerun broader glossy
  stress packets and compare candidate delta/stability against the prior
  structured post-only candidate.

### Local Reflection Radiance Candidate Consumption - 2026-06-05

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - the V2 reflection candidate now samples `g_LocalReflectionRadiance`.
  - produced radiance is admitted by the buffer alpha confidence channel.
  - alpha `0` preserves the previous structured post resolver fallback.
  - produced RGB is firefly-limited before it can affect the candidate.
- Default beauty remains unchanged unless debug view `58` or the existing
  `V2 reflection candidate (review)` toggle is active.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
build\vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe -T ps_6_3 -E PSMain -O3 -Qstrip_debug -I assets\shaders -Fo build\bin\assets\shaders\PostProcess.dxil assets\shaders\PostProcess.hlsl
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
build\vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe -T cs_6_3 -E CSMain -O3 -Qstrip_debug -I assets\shaders -Fo build\bin\assets\shaders\LocalReflectionRadiance.dxil assets\shaders\LocalReflectionRadiance.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup" -ViewFilter "beauty,local_reflection_radiance,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 50 -CaptureFrame 25 -CaptureSequenceCount 1 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_local_radiance_candidate_consume_smoke1_20260605
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive,glass_water_courtyard:glass_canopy,dragon_over_water:floor_reflection_closeup" -ViewFilter "beauty,local_reflection_radiance,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_local_radiance_candidate_broader_glossy_20260605
```

Focused packet result:

- packet:
  `build/captures/v2_local_radiance_candidate_consume_smoke1_20260605`.
- captured views: `6`.
- V2 packet evidence: passed.
- `local_reflection_radiance`: luma `0.0950`, nonblack `0.99999`.
- `reflection_resolver_candidate_delta`: luma `0.0281`, nonblack `0.1805`.

Broader glossy stress result:

- packet:
  `build/captures/v2_local_radiance_candidate_broader_glossy_20260605`.
- captured views: `28`.
- source-signal families: `4/4`.
- candidate-delta families: `4/4`.
- warnings/failures: `0/0`.

Candidate signal:

| Stress Family | Local Radiance Luma | Local Radiance Nonblack | Candidate Delta Luma | Candidate Delta Nonblack |
|---|---:|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.06236662` | `0.86520833` | `0.01245237` | `0.07959635` |
| `stress_glass_water_courtyard_glass_canopy` | `0.08687313` | `0.98508681` | `0.01305811` | `0.16990560` |
| `stress_material_lab_glass_emissive` | `0.09037495` | `0.99988064` | `0.01930018` | `0.13744358` |
| `stress_rt_showcase_reflection_closeup` | `0.09496863` | `0.99999132` | `0.02824227` | `0.18028971` |

Sequence stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00291844` | `0.00289495` | `0.992` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00121205` | `0.00118124` | `0.975` |
| `stress_material_lab_glass_emissive` | `0.00152219` | `0.00149033` | `0.979` |
| `stress_rt_showcase_reflection_closeup` | `0.00487044` | `0.00478940` | `0.983` |

Current stopping position:

- The V2 reflection candidate now consumes a real render-graph-produced local
  radiance buffer instead of using only the post-only structured fallback.
- The candidate remains opt-in/review-only.
- Next work should either expose stronger visual review packets/contact sheets
  for this produced-radiance candidate or begin the next owned source domain:
  semantic light-buffer/direct-light V2 shadow output.
