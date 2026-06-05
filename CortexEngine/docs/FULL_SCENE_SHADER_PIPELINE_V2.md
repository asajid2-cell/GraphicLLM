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
