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
   - Debug views must expose each channel.

3. Scene-Local Lighting Contract
   - Scene profiles declare semantic rigs, not raw ad hoc lights.
   - Supported rig elements: sun/directional, rect area, spot, point, emissive
     cards, practical fixtures, high-bay lights, stage beams, and display
     lights.
   - Each light has owner id, purpose, intensity units or normalized artistic
     exposure, shadow policy, color temperature, and affected zones.

4. Reflection And Indirect Contract
   - Enclosed scenes use local room probes, hero probes, planar probes, or
     neutral fallback with explicit ownership.
   - Visible external HDRI is allowed only for authored outdoor/gallery cases.
   - Reflection-owner debug remains mandatory.

5. Shadow Contract
   - Directional cascades, local shadow atlas, contact shadows, and optional
     RT shadows must report readiness independently.
   - Bias, softness, filter radius, cascade selection, and contact terms must
     be visible in debug views.

6. Temporal Contract
   - Motion vectors are first-class for camera and objects.
   - TAA/history rejection is material-aware.
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

## Phase Ledger

### FSSP-V2-001 Contract And Plan

Status: IN PROGRESS

Deliverables:

- `docs/FULL_SCENE_SHADER_PIPELINE_V2.md`
- `assets/final_art/full_scene_shader_pipeline_v2_contract.json`
- `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
- `assets/final_art/full_scene_shader_material_evidence_v2.schema.json`
- `assets/final_art/full_scene_shader_material_evidence_v2.json`
- `tools/validate_full_scene_shader_pipeline_v2_plan.py`
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`
- `tools/build_full_scene_shader_material_evidence_v2.py`
- handoff update in `docs/AAA_ASSET_QUALITY_HANDOFF.md`

Completion evidence:

- plan validator passes.
- contract includes every required shader domain.
- external frame-report contract maps every required shader domain to a future
  `full_scene_shader_pipeline_v2` frame-report section.
- material evidence report derives shader-family and PBR/hero-surface blockers
  from Asset Registry V2 and the scene binding overlay.
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
| Material | material families, texture evidence, debug views | planned |
| GBuffer | channel inventory, object/material ids, velocity | planned |
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
4. Add runtime frame-report placeholders for V2 domains without changing
   rendering.
5. Upgrade material model and registry material evidence.
6. Expand GBuffer/debug channel inventory.
7. Add local reflection probe ownership before chasing more shiny materials.
8. Add semantic light rigs and shadow/contact stability.
9. Refactor post into named HDR stages.
10. Move pass ownership into the render graph.
11. Run cross-family V2 packets and compare to V1 seq8.

This order keeps the engine diagnosable. It avoids the previous trap where a
scene looked better only because one setting hid the problem.
