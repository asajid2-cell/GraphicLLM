# Full Scene Shader Refactor Master Plan

This is the execution plan for moving CortexEngine toward full-scene,
Unreal-like visuals without repeating the old loop of isolated shader tweaks,
IBL hiding, or single-scene polish.

The goal is not "make this screenshot shinier." The goal is a renderer that can
build the final image from scene-owned facts:

```text
Scene + Asset Registry + Camera
  -> SceneVisualContract
  -> FullSceneMaterialTable
  -> FullSceneFrameData / GBuffer
  -> FullSceneLightRig
  -> FullSceneProbeSet
  -> Shadows + Reflections + Indirect
  -> Material-aware Temporal
  -> HDR Post
  -> Beauty + Debug Atlases + Frame Report
```

If a pass cannot explain what it used and who owned it, it is not ready to drive
V2 beauty output.

## Current State

Implemented baseline:

- Scene-Local Cinematic Renderer V1 is the playable fallback.
- `FullSceneShaderFrameContext` exists as a V2 runtime facade.
- V2 frame reports exist under `full_scene_shader_pipeline_v2`.
- V2 packet harness exists and can capture gallery debug evidence.
- Runtime material evidence is promoted for the gallery packet.
- GBuffer required resources and producers are reported.
- Visibility payload, instance identity table, material lookup table, and
  stable instance id evidence are packet-proved for the gallery packet.
- Per-pixel material id and object id debug views are packet-proved for the
  gallery packet.
- The shader-facing material table is packet-proved with complete policy rows
  and GBuffer policy-channel backing.
- Material-family, reflection-policy, temporal-policy, and post-sensitivity
  policy columns are per-pixel debug-visible from the material table.

Known blockers:

- Lighting is still partly profile/bootstrap driven instead of a semantic
  scene light rig.
- Reflection ownership still needs local room, hero, planar, SSR, RT, neutral,
  and authorized external environment sources in one resolver.
- Shadow/contact stability needs a formal policy and packet gates.
- Temporal resolve needs material/object-aware history confidence rather than a
  mostly global policy.
- HDR post is not yet a named, measured, packet-visible presentation pipeline.
- The render graph still does not own enough pass/resource/debug evidence to
  prove every final pixel.

## Refactor Principle

Every V2 domain must follow the same promotion ladder:

1. `instrumented`
   - Reports owner, fallback owner, readiness, and failure reason.
   - V1 still drives beauty.
2. `shadow_output`
   - Runs beside V1 and emits debug/compare views.
   - Beauty still uses V1 unless explicitly overridden.
3. `candidate`
   - Can drive V2 beauty for selected scenes behind a runtime flag.
4. `packet_passed`
   - Passes packet gates for at least one target family.
5. `cross_family_passed`
   - Passes gallery, kitchen, office, gym, concert, and one wet/glass-heavy
     scene.
6. `default_ready`
   - Preserves V1 gates, has stronger V2 evidence, and is accepted visually.

No domain can skip instrumentation. No beauty switch is valid without debug
views and frame-report evidence.

## Target Runtime Types

These are the major structures the refactor should converge on. Names can
change, but the ownership boundaries should stay intact.

```cpp
struct SceneVisualContract {
    SceneFamily family;
    SceneEnclosureMode enclosure;
    EnvironmentVisibilityPolicy environmentVisibility;
    LightingMood lightingMood;
    ReflectionFamily reflectionFamily;
    TemporalFamily temporalFamily;
    PostProfileId postProfile;
    bool externalIblVisibleAllowed;
};

struct FullSceneMaterialModel {
    MaterialFamily family;
    MaterialFeatureFlags features;
    TextureEvidence textures;
    ReflectionPolicy reflection;
    TemporalPolicy temporal;
    PostSensitivity post;
    SceneMaterialClassId sceneClass;
    AssetRegistryId sourceAsset;
};

struct FullSceneFramePixelFacts {
    uint materialId;
    uint objectId;
    uint policyId;
    float3 normalWs;
    float roughness;
    float metallic;
    float ao;
    float3 emissive;
    float2 velocity;
};

struct SceneLightRig {
    vector<SemanticLight> lights;
    AmbientPolicy ambient;
    ExposureIntent exposure;
};

struct SceneProbeSet {
    vector<RoomProbe> roomProbes;
    vector<HeroProbe> heroProbes;
    vector<PlanarProbe> planarProbes;
    ReflectionFallback fallback;
};

struct FullSceneRenderGraphContract {
    vector<PassContract> passes;
    vector<ResourceContract> resources;
    vector<DebugViewContract> debugViews;
};
```

The beauty shaders consume these contracts. They do not infer scene meaning
from incidental bound textures, global flags, or fallback resources.

## Phase 0: Freeze The Baseline

Purpose:

- Keep V1 playable and reproducible while V2 is built beside it.
- Prevent old flicker/IBL/reflection issues from being hidden by changing
  defaults.

Implementation:

- Preserve the current V1 launch path.
- Keep V2 domain flags default-off or facade-only until packet-gated.
- Record current V1 packets as comparison targets.
- Keep the P menu diagnostics for IBL sharpness/reflection visibility so user
  reports can be recreated without editing command lines.

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
```

Exit criteria:

- V1 still launches.
- V2 packet still emits evidence.
- Any known failing V2 domain fails honestly with a specific reason.

## Phase 1: Frame Identity And GBuffer Ownership

Purpose:

- Make the frame carry stable material/object identity so all downstream passes
  operate on the same truth.

Required changes:

- Expose the raw visibility payload as a frame-report resource.
- Report visibility payload producer ownership.
- Report visibility instance count, material table count, and invalid stable id
  count.
- Add or prove per-pixel material id and object id debug views.
- Keep current GBuffer targets append-only until validation passes.

Likely files:

- `src/Graphics/FrameContract.h`
- `src/Graphics/FrameContractJson.cpp`
- `src/Graphics/Renderer_FrameContractSnapshot.cpp`
- `src/Graphics/Renderer_VisibilityBufferCollection.cpp`
- `src/Graphics/Renderer_VisibilityBufferStages.cpp`
- `src/Graphics/FullSceneShaderFrameContext.h`
- `assets/shaders/VisibilityPass.hlsl`
- `assets/shaders/MaterialResolve.hlsl`
- `assets/shaders/DebugBlitVisibility.hlsl`

Pseudocode:

```cpp
FrameIdentityEvidence BuildIdentityEvidence(const FrameContract& contract) {
    FrameIdentityEvidence e;
    e.visibilityPayloadReady = HasResource(contract, "visibility_buffer");
    e.visibilityPayloadProducerReady =
        PassWrites(contract, "VBVisibility", "visibility_buffer") ||
        PassWrites(contract, "VisibilityBuffer", "visibility_buffer");
    e.instanceTableReady = contract.draws.visibilityBufferInstances > 0;
    e.materialLookupReady = contract.draws.visibilityBufferMaterials > 0;
    e.stableObjectIdsReady =
        e.instanceTableReady &&
        contract.draws.visibilityBufferInvalidStableIds == 0;
    e.debugViewsReady =
        HasDebugView(contract, "material_id") &&
        HasDebugView(contract, "object_id");
    e.ready = e.visibilityPayloadReady &&
              e.visibilityPayloadProducerReady &&
              e.materialLookupReady &&
              e.stableObjectIdsReady &&
              e.debugViewsReady;
    return e;
}
```

Validation:

- Frame report shows `visibility_buffer` as a resource.
- Packet captures material id and object id debug views.
- Camera motion does not change object ids for static objects.
- `gbuffer.domain_ready` remains false until the above is true.

Exit criteria:

- Every V2 lighting/reflection/temporal pass can read or derive stable
  material/object identity from the same frame data.

## Phase 2: Full Runtime Material Table

Purpose:

- Replace broad material hints with a shader-ready material table.

Required changes:

- Promote `FullSceneMaterialModel` from evidence-only into the material upload
  path.
- Encode shader family, scene material class, reflectance behavior, temporal
  policy, post sensitivity, texture evidence, and feature flags.
- Add debug views for material family, texture readiness, reflection policy,
  temporal policy, and post sensitivity.
- Connect Asset Registry V2 evidence for imported PBR assets.

Likely files:

- `src/Graphics/MaterialModel.h`
- `src/Graphics/MaterialModel.cpp`
- `src/Graphics/MaterialState.h`
- `src/Graphics/Renderer_Materials.cpp`
- `src/Graphics/RendererMaterialTextureState.h`
- `assets/shaders/MaterialResolve.hlsl`
- `assets/shaders/PBR_Lighting.hlsli`

Pseudocode:

```cpp
FullSceneMaterialModel ResolveFullSceneMaterial(
    const Renderable& renderable,
    const AssetRegistryEntry* asset,
    const SceneVisualContract& scene) {
    FullSceneMaterialModel model = ResolveCurrentMaterial(renderable);
    model.family = ResolveMaterialFamily(renderable, asset, scene);
    model.features = ResolveFeatureFlags(renderable, asset);
    model.textures = ResolveTextureEvidence(renderable, asset);
    model.reflection = PickReflectionPolicy(model, scene);
    model.temporal = PickTemporalPolicy(model, scene);
    model.post = PickPostSensitivity(model, scene);
    Validate(model);
    return model;
}
```

Validation:

- No unknown material families in target scenes.
- Hero materials report texture/PBR evidence or explicit blocker.
- Material debug views line up with scene roles.
- Smooth metal/glass/water/neon get distinct policies.

Exit criteria:

- Material domain passes gallery plus at least kitchen and gym packets.

## Phase 3: Scene-Local Lighting Rig

Purpose:

- Turn lighting from ad hoc brightness into semantic scene light rigs.

Required changes:

- Add `SceneLightRig` generation from scene family/profile.
- Add semantic light types: key, fill, practical, accent, high-bay, display,
  stage beam, sun, skylight, emergency fallback.
- Report light owner, role, intensity, color temperature, shadow policy, and
  affected zone.
- Add direct-light debug, light-id debug, and exposure debug.

Likely files:

- `src/Graphics/RendererSceneProfile.h`
- `src/Graphics/RendererSceneProfile.cpp`
- `src/Graphics/RendererLightingRigControl.cpp`
- `src/Graphics/Renderer_FrameLightingConstants.cpp`
- `src/Graphics/Renderer_ForwardPass.cpp`
- `assets/shaders/DeferredLighting.hlsl`
- `assets/shaders/PBR_Lighting.hlsli`

Pseudocode:

```cpp
SceneLightRig BuildSceneLightRig(const SceneVisualContract& scene,
                                 const SceneGraphSummary& graph) {
    SceneLightRig rig;
    rig.Add(KeyLight(scene, graph));
    rig.Add(FillLight(scene, graph));
    rig.Add(PracticalFixtures(scene, graph));
    rig.Add(AccentLights(scene, graph));
    rig.ValidateOwnership();
    return rig;
}
```

Validation:

- Kitchen reads as practical/home lighting.
- Gym reads as high-bay indoor lighting.
- Concert reads as stage/display lighting.
- Gallery reads as controlled exhibit lighting.
- Exposure and clipping metrics stay bounded without emergency per-scene hacks.

Exit criteria:

- Lighting domain can run as shadow output with a debug atlas and no V1 gate
  regression.

## Phase 4: Local Reflection And Probe Ownership

Purpose:

- Stop reflective materials from using unrelated environment content unless the
  scene explicitly allows it.

Required changes:

- Add `SceneProbeSet` with room probes, hero probes, planar probes, SSR, RT,
  neutral fallback, and authorized external environment.
- Route all reflection paths through a single `ReflectionSourceResolver`.
- Report reflection source per pixel.
- Add probe coverage and unauthorized-environment metrics.

Likely files:

- `src/Graphics/Renderer_Environment.cpp`
- `src/Graphics/RendererEnvironmentState.h`
- `src/Graphics/Renderer_RTReflections.cpp`
- `src/Graphics/Renderer_SSRPass.cpp`
- `src/Graphics/Renderer_FramePostConstants.cpp`
- `assets/shaders/RaytracedReflections.hlsl`
- `assets/shaders/SSR.hlsl`
- `assets/shaders/DeferredLighting.hlsl`

Pseudocode:

```cpp
ReflectionSource ResolveReflectionSource(PixelSurface p,
                                          FullSceneMaterialModel m,
                                          SceneProbeSet probes,
                                          SceneVisualContract scene) {
    if (!m.reflection.enabled) return ReflectionSource::DiffuseOnly();
    if (probes.HasPlanar(p.surfaceId)) return probes.Planar(p.surfaceId);
    if (probes.HasHeroProbe(p.worldPosition)) return probes.Hero(p.worldPosition);
    if (probes.HasRoomProbe(p.worldPosition)) return probes.Room(p.worldPosition);
    if (scene.externalIblVisibleAllowed) return probes.ExternalEnvironment();
    return probes.NeutralFallback("no_local_probe");
}
```

Validation:

- Reflection-owner packet distinguishes room, hero, planar, SSR, RT, neutral,
  and external.
- Enclosed scenes have zero unauthorized external environment reflection.
- Smooth/metallic surfaces do not shimmer under mouse jiggle packets.

Exit criteria:

- Reflections can drive V2 candidate output for gallery and one enclosed scene.

## Phase 5: Shadow And Contact Stability

Purpose:

- Make shadows stable and physically plausible across scene families.

Required changes:

- Centralize directional cascade, local shadow, and contact shadow policy.
- Scale bias and filter radius by scene units, light type, receiver slope, and
  material receiver class.
- Add contact terms for furniture, props, balls, counters, plinths, and small
  object supports.
- Add shadow debug: cascade id, local light id, shadow factor, bias class,
  contact term.

Likely files:

- `src/Graphics/Renderer_ShadowPass.cpp`
- `src/Graphics/Renderer_RenderGraphDepthShadow.cpp`
- `src/Graphics/RendererShadowState.h`
- `src/Graphics/RendererLocalShadowState.h`
- `src/Graphics/Renderer_FrameLightingConstants.cpp`
- `assets/shaders/DeferredLighting.hlsl`
- `assets/shaders/RaytracedShadows.hlsl`

Pseudocode:

```cpp
ShadowPolicy ResolveShadowPolicy(const SemanticLight& light,
                                  const MaterialReceiver& receiver,
                                  const SceneVisualContract& scene) {
    ShadowPolicy p = BasePolicy(light.type, scene.family);
    p.bias = ScaleBias(p.bias, receiver.normalSlope, scene.unitScale);
    p.filterRadius = StableFilterRadius(light.size, receiver.distance);
    p.contactEnabled = NeedsContactShadow(receiver.materialClass, light.type);
    return p;
}
```

Validation:

- Mouse jiggle packet shows bounded shadow delta.
- Contact shadows remain visible and stable on small supports.
- Cascade/local shadow transitions are visible in debug and do not pop.

Exit criteria:

- Shadow domain can pass camera-motion stability gates on gallery, kitchen,
  gym, and concert.

## Phase 6: Material-Aware Temporal Pipeline

Purpose:

- Stop smooth, metallic, glass, emissive, and high-frequency surfaces from
  popping or smearing under camera motion.

Required changes:

- Temporal resolve consumes material id, object id, velocity, depth, normal,
  roughness, and emissive policy.
- Add material-specific history confidence and clamp widths.
- Use the same jitter-aware history coordinate contract in rejection and TAA
  resolve.
- Add temporal debug: velocity, rejection, clamp width, history weight,
  disocclusion, material policy.

Likely files:

- `src/Graphics/TemporalManager.*`
- `src/Graphics/TemporalRejectionMask.*`
- `src/Graphics/Renderer_TAAExecution.cpp`
- `src/Graphics/Renderer_RenderGraphTemporalMask.cpp`
- `src/Graphics/Renderer_RenderGraphTAA.cpp`
- `assets/shaders/TemporalRejectionMask.hlsl`
- `assets/shaders/PostProcess.hlsl`
- `assets/shaders/VBMotionVectors.hlsl`

Pseudocode:

```hlsl
TemporalPolicy p = LoadTemporalPolicy(materialId);
float2 historyUv = CurrentUvToHistoryUv(uv, velocity, jitterDelta);
HistorySample h = SampleHistory(historyUv);
float confidence = EvaluateHistoryConfidence(
    depth, normal, objectId, materialId, p);
float3 clamped = ClampHistory(h.color, NeighborhoodCurrentColor(), p);
return lerp(currentColor, clamped, confidence * p.maxBlend);
```

Validation:

- Mouse-jiggle packets include smooth metal, glass, water, emissive, patterned
  tile, and matte wall targets.
- Residual flicker metrics stay under threshold.
- No hidden fix by blurring or disabling reflection/IBL.

Exit criteria:

- Temporal domain can drive V2 candidate output for the reported flicker-heavy
  scenes.

## Phase 7: HDR Cinematic Post V2

Purpose:

- Build a measured presentation stack that makes good lighting/materials look
  rich without hiding broken inputs.

Required changes:

- Split post into named stages: exposure, bloom threshold, bloom composite,
  highlight rolloff, tone map, color grade, clarity/sharpen, output encode.
- Add optional LUT support.
- Add per-stage debug/luma reports.
- Keep stochastic/noise-based post disabled unless stability-gated.

Likely files:

- `src/Graphics/Renderer_PostProcess.cpp`
- `src/Graphics/Renderer_FramePostConstants.cpp`
- `src/Graphics/RendererPostProcessState.h`
- `assets/shaders/PostProcess.hlsl`

Pseudocode:

```hlsl
float3 ApplyPostV2(float3 hdr, PostProfile p) {
    hdr = ApplyExposure(hdr, p.exposure);
    hdr = CompositeBloom(hdr, p.bloom);
    hdr = ApplyHighlightRolloff(hdr, p.rolloff);
    float3 ldr = FilmicToneMap(hdr, p.tonemap);
    ldr = ApplyColorGrade(ldr, p.grade);
    ldr = ApplyStableClarity(ldr, p.clarity);
    return EncodeDisplay(ldr, p.output);
}
```

Validation:

- Bright ratio, dark crush, midtone contrast, saturation, bloom area, and edge
  clarity are packet-gated.
- Post cannot turn a failed lighting/material domain into a passing V2 packet.

Exit criteria:

- Post domain can run as candidate after lighting/reflection/temporal domains
  are not failing hard.

## Phase 8: Render Graph Ownership

Purpose:

- Make pass order, resources, transitions, and debug ownership explicit enough
  that packet tools can prove the frame.

Required changes:

- Declare every V2 pass with inputs, outputs, state, debug labels, and timing.
- Attach frame-report evidence to pass/resource contracts.
- Fail V2 packets on missing producers, stale resources, ambiguous debug
  sources, or unowned fallback resources.
- Keep V1 graph paths until V2 packets pass.

Likely files:

- `src/Graphics/RenderGraph.*`
- `src/Graphics/Renderer_RenderGraph*.cpp`
- `src/Graphics/Renderer_FrameContractPasses.cpp`
- `src/Graphics/Renderer_FrameContractSnapshot.cpp`
- `src/Graphics/FrameContractResources.cpp`
- `src/Graphics/FrameContractValidation.cpp`

Pseudocode:

```cpp
FullSceneRenderGraph BuildFullSceneRenderGraph(const FullSceneFrameContext& ctx) {
    graph.Add("ShadowPrepass", reads(lights, geometry), writes(shadowAtlas));
    graph.Add("Visibility", reads(geometry, materials), writes(visibility, depth));
    graph.Add("MaterialResolve", reads(visibility, materials), writes(gbuffer));
    graph.Add("Lighting", reads(gbuffer, lights, shadows), writes(hdrLight));
    graph.Add("Reflections", reads(gbuffer, probes, history), writes(reflection));
    graph.Add("Temporal", reads(hdrLight, reflection, velocity), writes(temporalHdr));
    graph.Add("Post", reads(temporalHdr, bloom), writes(backbuffer));
    graph.ValidateOwnership();
    return graph;
}
```

Validation:

- Frame reports list every expected V2 pass and resource producer.
- Debug view contract maps each view to the pass and resources that produced it.
- Packet checker fails on missing or ambiguous ownership.

Exit criteria:

- Render graph domain can be promoted after at least one V2 candidate path
  renders beauty from declared graph resources.

## Phase 9: Cross-Family Promotion

Purpose:

- Prove this is a general full-scene shader architecture, not a gallery-only
  improvement.

Required scene families:

- gallery
- kitchen
- office
- gym
- concert
- wet/glass-heavy scene

Packet views:

- beauty
- material family
- material texture readiness
- material reflection policy
- material temporal policy
- albedo
- normal/roughness
- emissive/metallic
- material id
- object id
- velocity
- direct light
- light id
- reflection owner
- shadow factor
- contact shadow
- temporal rejection
- temporal blend
- post luma/stage atlas

Acceptance:

- V1 gates remain passing.
- V2 frame-report domains are ready or have explicit accepted blockers.
- No scene passes because IBL, reflection, shadows, or temporal history were
  disabled to hide defects.
- User review accepts the visual direction as materially closer to AAA final
  art across the family set.

## Implementation Order

The next slices should be:

1. Identity ownership:
   - expose visibility payload/resource producer evidence.
   - report stable instance/material counts.
   - add material id/object id debug evidence.
2. Runtime material table:
   - move material model data into shader upload structures.
   - add material policy debug views.
3. Scene light rig:
   - add semantic rigs and frame-report ownership.
   - run shadow-output debug before beauty promotion.
4. Reflection source resolver:
   - add local/authorized reflection source ownership.
   - keep external HDRI allowed only by scene contract.
5. Shadow/contact stability:
   - centralize policies and camera-motion gates.
6. Material-aware temporal:
   - consume material/object ids and add flicker packet gates.
7. HDR post V2:
   - split and measure post stages.
8. Render graph ownership:
   - enforce producer/resource/debug view contracts.
9. Cross-family packet:
   - only then evaluate default V2 beauty.

This order is deliberate. Full-scene shaders need identity and material truth
before lighting and reflections can be trustworthy. Temporal stability needs
object/material ids before it can stop smooth surfaces from popping. Post is
late because it should present good inputs, not mask bad ones.

## Do Not Do

- Do not start by tuning bloom, contrast, or IBL blur.
- Do not turn off reflections, IBL, shadows, or temporal history as a fix.
- Do not promote a domain without debug views.
- Do not accept a single scene as proof.
- Do not let shader passes infer scene intent independently.
- Do not mix scene-authoring automation work into this renderer refactor.

## First Concrete Feature After Planning

The first implementation feature was `FSSP-V2-003A Identity Ownership`, followed
by `FSSP-V2-003B Per-Pixel Identity Debug`.

Deliverables:

- frame-report resource entry for `visibility_buffer`.
- draw-count evidence for visibility instances, visibility materials, and
  invalid stable ids.
- V2 GBuffer evidence fields for visibility payload readiness, producer
  readiness, instance identity table readiness, material lookup readiness, and
  stable instance id readiness.
- material id and object id debug views from the visibility payload and
  instance table.
- packet summary showing the GBuffer domain ready only after material id,
  object id, and debug producer-source ownership are all present.

That is the correct bridge from planning into real shader refactor work.

Status as of 2026-06-05:

- `FSSP-V2-003A` packet:
  `build/captures/full_scene_shader_pipeline_v2_identity_ownership_packet_20260605`.
- `FSSP-V2-003B` packet:
  `build/captures/full_scene_shader_pipeline_v2_per_pixel_identity_packet_20260605`.
- `FSSP-V2-003B` captured `9` views, `90` evidence rows, and `0` failures.
- Gallery beauty GBuffer evidence reports:
  - `material_id_channel_ready=true`.
  - `object_id_channel_ready=true`.
  - `debug_view_source_report_available=true`.
  - `missing_required_channel_count=0`.
  - `missing_ownership_channel_count=0`.
  - `gbuffer.domain_ready=true`.

Next implementation feature:

- Start Phase 2, full runtime material-table promotion.
- The material table should become shader-facing data, not only report
  evidence. It must carry material family, PBR/texture readiness, reflection
  policy, temporal policy, and post sensitivity so lighting, reflections,
  temporal, and post consume the same material truth.
