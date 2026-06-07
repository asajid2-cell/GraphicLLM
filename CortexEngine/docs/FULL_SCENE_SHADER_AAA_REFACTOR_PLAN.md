# Full Scene Shader AAA Refactor Plan

Status: planning ledger.

Default beauty stays unchanged until a separate promotion gate passes.

## 2026-06-06 Full Scene Shader Refactor Blueprint

This is the current refactor decision record before implementing the next
goal feature. The objective is to build an opt-in candidate renderer that can
grow toward high-end real-time scene quality without masking renderer debt or
destabilizing the default path.

The target is not "more effects." The target is a renderer where the final
pixel is explainable:

```text
Scene + Assets
  -> Visibility / Depth / Motion
  -> Material Payload
  -> Scene-Local Environment
  -> Direct Lighting + Shadows
  -> Indirect Lighting + Emissive
  -> Reflection Resolver
  -> Transparency / Water / Glass
  -> HDR Composite
  -> Cinematic Post
  -> Candidate Beauty
  -> Promotion Gate
```

Each stage must produce resources, debug views, frame-report ownership, and
packet evidence. If a feature cannot be inspected directly, it is not allowed
to affect candidate beauty.

### Refactor Tracks

Track A: renderer resource ownership.

- Convert V3 domains from adapters into real render-graph producers.
- Make each producer write named resources with explicit descriptors.
- Make each consumer edge visible in the frame report.
- Reject candidate promotion when required resources are missing, stale,
  blank, or silently replaced by legacy beauty.

Track B: physically useful shading inputs.

- Material payload must expose base color, normal, roughness, metallic,
  specular, AO, emissive, opacity, transmission, clearcoat, anisotropy,
  material class, and missing-channel masks.
- Lights must be separated into direct, shadow visibility, shadow loss,
  indirect, emissive, and atmosphere/media contributions.
- Reflections must be source-aware: local probes, SSR, RT/ray query,
  planar/hero probes, and scene-local environment fallback cannot be blended
  anonymously.
- Environment must separate visible background, lighting background,
  reflection background, irradiance, specular prefilter, and atmosphere.

Track C: cinematic composition and art controls.

- `FullSceneCompositeV3` owns HDR scene assembly from V3 resources.
- `CinematicPostV3` owns exposure, bloom, tone mapping, color grade, glare,
  sharpening, and LDR output.
- Exposure must have locked/manual modes for stability tests.
- Bloom must be sourced from overbright/emissive masks, not arbitrary LDR
  brightness.
- Post is not allowed to hide upstream flicker, missing shadows, bad
  reflections, or wrong material ranges.

Track D: verification and promotion.

- Every slice needs static, mouse-jitter, camera-sweep, close-surface, and
  reflective-object packets before it can be trusted.
- Cross-family evidence must include gallery, kitchen, office, gym, classroom,
  concert, red room, stadium, bathroom, bedroom, workshop, store, and street.
- Debug packets must include raw resource views, candidate/default
  comparisons, metrics, contact sheets, and failure reports.
- Default beauty remains unchanged until the candidate renderer passes the
  promotion gate and the user accepts the visual result.

### Final Resource Graph

The desired producer graph is:

```text
FullSceneMaterialResolveV3
  reads: visibility, mesh/material table, texture atlas/bindless table
  writes:
    material_base_color
    material_normal_world
    material_roughness
    material_metallic
    material_specular
    material_ao
    material_emissive
    material_opacity
    material_extra_channels
    material_class_id
    material_missing_channel_mask

SceneLocalEnvironmentV3
  reads: scene profile, environment assets, local probe rig, light policy
  writes:
    visible_background
    environment_irradiance
    environment_specular_prefilter
    reflection_background
    atmosphere_terms
    environment_ownership_mask

FullSceneLightingV3
  reads: material payload, depth/normal, scene-local environment, light list
  writes:
    direct_lighting
    direct_lighting_unshadowed
    shadow_visibility
    shadow_loss
    indirect_lighting
    emissive_indirect
    lighting_energy_budget

FullSceneReflectionV3
  reads: material payload, depth/normal/motion, local reflection, SSR, RT,
         scene-local environment, reflection history
  writes:
    reflection_radiance
    reflection_confidence
    reflection_source_id
    reflection_rejected_source_mask
    reflection_temporal_delta
    reflection_history_validity
    reflection_ssr_source_signal
    reflection_rt_source_signal
    reflection_source_suppression

TransparencyMediaV3
  reads: material payload, depth, lighting, reflection, atmosphere
  writes:
    transparent_radiance
    water_radiance
    glass_radiance
    transmission_mask
    volumetric_inscatter
    volumetric_transmittance

FullSceneCompositeV3
  reads: material, lighting, environment, reflection, transparency/media
  writes:
    candidate_hdr_scene_color
    overbright_mask
    underlit_mask
    composition_debug
    candidate_energy_budget

CinematicPostV3
  reads: candidate HDR, exposure policy, bloom source, color grade, history
  writes:
    candidate_exposure_meter
    candidate_bloom_extract
    candidate_bloom_resolved
    candidate_tonemapped_output
    candidate_color_grade_delta
    candidate_ldr_cinematic_output
```

### Implementation Order

Phase 0: freeze the contract.

- Update the V3 contract JSON, frame context, analyzer, debug-mode registry,
  packet schema, and promotion gate so the target resource names are known
  before more shader work lands.
- Add "candidate only" guardrails so default beauty cannot be touched by
  accident.

Phase 1: stabilize reflection as a real domain.

- Finish wiring RT/ray-query reflection into `FullSceneReflectionV3` as a
  resolver source.
- Keep SSR, RT, local probe, and environment visible as separate source IDs.
- Add source confidence, rejection reason, temporal delta, and history
  validity views.
- Split source suppression into a dedicated view so temporal/history rejection
  and material-policy rejection are independently measurable.
- Fix glossy/smooth jitter at the source level before increasing reflection
  influence in the composite.

Phase 2: make scene-local environment texture-backed.

- Replace logical environment ownership with actual local irradiance/specular
  resources.
- Support enclosed-room, stage, neutral-lab, and open-exterior profiles.
- Allow IBL lighting when useful, but prevent inappropriate panorama imagery
  from leaking into enclosed reflections or visible backgrounds.

Phase 3: promote material payload to first-class PBR inputs.

- Convert material evidence into concrete payload resources consumed by
  lighting, reflection, and composite.
- Add range normalization and missing-channel reporting for imported/generated
  assets.
- Add closeup packets for metals, plastics, glass, water, cloth, ceramic,
  wood, tile, emissive, and painted walls.

Phase 4: rebuild lighting around stable shadow and energy resources.

- Refactor shadow visibility/contact shadow ownership.
- Add light-family contracts: daylight room, warm interior, neon/stage,
  red-room, gym overheads, exterior sun/sky.
- Keep locked exposure during lighting tests so exposure cannot hide flicker.

Phase 5: add GI, emissive, transparency, water, glass, and media.

- Add emissive indirect for neon and concert scenes.
- Add room-local diffuse probe/ambient terms for enclosed scenes.
- Separate glass/water/transmission from opaque reflection so smooth surfaces
  do not fight the generic resolver.
- Add volumetric/atmosphere only after opaque lighting and reflection are
  stable.

Phase 6: replace composite adapter with real HDR composition.

- Remove dependence on legacy `hdr_color` except as a named fallback/reference
  debug input.
- Composite V3 material, direct, indirect, shadows, reflections, emissive,
  transparency, and media into `candidate_hdr_scene_color`.
- Emit overbright, underlit, and energy-budget diagnostics.

Phase 7: replace post adapter with real cinematic post.

- Implement locked/manual exposure, optional bounded auto exposure, bloom
  extract/resolve, filmic tone map, color grading, glare, sharpening, and LDR
  debug views.
- Add explicit bypass modes for raw HDR, post without bloom, post without
  grade, and final candidate.

Phase 8: promotion ladder.

- Run the full family and motion matrix.
- Produce contact sheets and metrics for default beauty, candidate HDR,
  candidate LDR, and raw debug resources.
- Promote only when artifacts are explained or fixed, packets pass, and the
  user accepts the visual result.

### Why This Direction Is Better

The previous failure mode was local optimization: fix one scene, blur one IBL,
change one camera, or adjust one artifact until a screenshot looked less bad.
That cannot lead to a reusable high-quality renderer.

This refactor makes renderer quality cumulative. Each phase adds an owned
capability that later phases must consume. Reflection problems become
source/confidence/history problems. Material problems become payload/range
problems. Lighting problems become shadow/energy problems. Post problems
become exposure/bloom/tone-map problems. The engine gets better even when a
particular screenshot still needs work because the failure becomes measurable
and localizable.

### Next Feature Boundary

The next implementation feature should remain narrow but architectural:

```text
FullSceneReflectionV3 RT/ray-query source input
  -> source IDs and confidence prove RT is available or absent
  -> candidate composite can consume reflection_radiance without guessing
  -> metallic/smooth jitter can be diagnosed per source
```

Do not move on to stronger post or prettier lighting until the reflection
domain can explain local, SSR, RT, and environment source choices under motion.

Implementation status, 2026-06-06:

- `FullSceneReflectionV3` now has RT/ray-query as a concrete source input.
- The resolver samples `rt_reflection` alongside local reflection radiance and
  SSR.
- The render graph binds `rt_reflection` as the third resolver SRV and records
  the pass read edge.
- Source override value `3` and labels `rt`, `ray_query`, `raytraced`, and
  `ray_traced` now map to the forced RT/ray-query contract.
- Analyzer readiness requires `FullSceneReflectionV3` to read
  `rt_reflection`.

This completes the ownership part of the RT input slice. Packet evidence still
has to prove whether the RT source is present and stable enough to admit in
auto mode. Until then, RT is wired for diagnosis, not promoted as a visual
quality solution.

Packet status:

- forced RT static, auto static, and auto mouse-jitter gallery packets passed.
- forced RT produced nonblank radiance/confidence and used
  `forced_ray_query_reflection`.
- auto mode still chose `local_probe`, which is the intended conservative
  behavior until RT/SSR source stability is better proven across smooth and
  metallic stress scenes.

Follow-up source diagnostic status, 2026-06-06:

- `reflection_rt_source_signal` is now part of the planned ReflectionV3
  contract.
- Debug mode `74`, `FullSceneReflectionV3RTSourceSignal`, is wired into the
  renderer, frame contract, packet view registry, and V3 analyzers.
- Forced RT static, auto static, and auto mouse-jitter gallery packets passed.
- The RT source signal is nonblank and measurable:
  `mean_luma=0.2907308`, `nonblack_ratio=0.3947667` in the forced RT static
  packet.
- Auto mode still chooses `local_probe`. Treat this as correct until RT/SSR
  source-quality stabilization is implemented and proven across smooth/metallic
  stress scenes.

Rejected source-stability shortcut, 2026-06-06:

- A screen-space derivative stability gate was tested inside
  `FullSceneReflectionResolverV3.hlsl`.
- It reduced `reflection_radiance` motion delta in one gallery mouse-jitter
  packet, but worsened `reflection_confidence` and `reflection_source_id`
  motion deltas.
- Decision: do not solve reflection stability with single-frame derivative
  heuristics. They make the source ownership contract less stable and are too
  easy to tune scene-by-scene.

Required next architecture: `ReflectionHistoryV3`

The next reflection-quality slice must be a real history/stability system, not
a final-composite boost and not another local heuristic.

Resource contract:

```text
FullSceneReflectionV3
  reads:
    local_reflection_radiance
    ssr_color
    rt_reflection
    reflection_history_v3_prev       optional on first frame
    velocity
    depth
    normal_roughness
  writes:
    reflection_radiance
    reflection_confidence
    reflection_source_id
    reflection_rejected_source_mask
    reflection_temporal_delta
    reflection_ssr_source_signal
    reflection_rt_source_signal
    reflection_source_suppression
    reflection_history_v3_curr
    reflection_history_v3_prev
    reflection_history_v3_validity

EndFrame
  swaps:
    reflection_history_v3_curr -> reflection_history_v3_prev
```

History payload:

```text
reflection_history_v3_curr RGBA16F
  rgb = resolved reflection radiance
  a   = resolved confidence

reflection_history_v3_validity RGBA8 or RG16F
  r = reprojected history valid
  g = source id stable enough to keep
  b = source changed/rejected this frame
  a = disocclusion or camera-cut invalid
```

Per-pixel policy pseudocode:

```text
currentSources = evaluate(localProbe, ssr, rt, environment)
prev = sample(historyPrev, uv + velocity + jitterDelta)

historySurfaceOk =
  depthClose(currentDepth, historyDepth) &&
  normalClose(currentNormal, historyNormal) &&
  !cameraCut &&
  taaHistoryValid

candidate = pickBestCurrentSource(currentSources)

sourceStable =
  historySurfaceOk &&
  prev.confidence > minHistoryConfidence &&
  sourceCompatible(prev.sourceId, candidate.sourceId, currentRoughness)

if sourceStable:
  candidate.score += hysteresisBonus(prev.sourceId, currentRoughness)
  candidate.radiance = clampNeighborhood(candidate.radiance, prev.radiance)

if candidate.source is SSR or RT:
  require candidate.score >= localProbe.score + sourceSwitchMargin
  require sourceStable or candidate.confidence >= firstFrameHighConfidence

resolved = candidate
historyCurr = pack(resolved.radiance, resolved.confidence, resolved.sourceId)
validity = pack(historySurfaceOk, sourceStable, sourceChanged, invalidReason)
```

Why this is better:

- Source changes become explicit measured events instead of hidden shimmer.
- SSR/RT can become strong when temporally stable, but cannot win just because
  one noisy frame is bright.
- Smooth/metallic surfaces get real hysteresis where shimmer is most visible.
- The same packet harness can rank source stability before we promote the
  result into the candidate beauty path.

Admission gates:

- `reflection_history_v3_curr`, `reflection_history_v3_prev`, and
  `reflection_history_v3_validity` exist as concrete frame resources.
- Frame report exposes `reflection_history_v3_ready`,
  `reflection_history_v3_prev_ready`, `reflection_history_v3_validity_ready`,
  and source-switch counts.
- Mouse-jitter packet must not increase `reflection_source_id` motion delta.
- Smooth/metallic stress packet must reduce source-ID active delta and final
  reflection radiance delta without blanking raw SSR/RT source signals.

Seed implementation status, 2026-06-06:

- `FullSceneReflectionHistoryV3` now exists as a separate pass after
  `FullSceneReflectionV3`.
- The pass writes:
  - `reflection_history_v3_curr`.
  - `reflection_history_v3_validity`.
- `FullSceneReflectionHistoryV3Copy` writes `reflection_history_v3_prev` from
  current history after the history pass.
- The seed pass owns previous-history storage. Reprojection validity is handled
  by the follow-up history-validity pass; source-ID hysteresis is still pending.
- The existing ReflectionV3 PSO target count was corrected from `5` to `7`.
- Debug modes `75`, `76`, and `77` expose the history resources.
- Static and mouse-jitter gallery packets passed with
  `reflection_v3_channel_count=9` before previous-history ownership.
- Remaining required architecture:
  - record source-switch counts and disocclusion invalidation.
  - use source-ID hysteresis to control SSR/RT/local-probe admission.

Previous-history ownership status, 2026-06-06:

- `reflection_history_v3_prev` now exists as a concrete persistent resource.
- Debug mode `77` exposes `reflection_history_v3_prev`.
- `FullSceneReflectionHistoryV3` reads previous history as an input.
- `FullSceneReflectionHistoryV3Copy` copies current history to previous
  history after the history pass.
- Reflection V3 readiness now requires `10` channels.
- Static and mouse-jitter gallery packets passed with
  `reflection_v3_channel_count=10`.
- Reprojection validity status, 2026-06-06:
  - `FullSceneReflectionHistoryV3` now reads `depth`, normal/roughness, and
    `velocity`.
  - previous history is sampled at `uv + velocity + taa_jitter_delta`.
  - `reflection_history_v3_validity` now reports active source, source class,
    reusable reprojected history, and rejection/debt strength.
  - readiness and analyzer gates require the geometry/motion inputs.
- Remaining required architecture:
  - add source-switch and disocclusion counters.
  - only then allow history to affect SSR/RT/local-probe source admission.

## 2026-06-06 Refactor Plan Before Goal Feature Completion

The target is not merely "better shaders." The target is a full candidate
beauty renderer that can approach Unreal-style final pixels because each visual
term is physically named, debug-visible, motion-tested, and composited through a
controlled HDR/post stack.

The current V3 path is in the right shape but is still incomplete:

- `FullSceneCompositeV3` exists and writes `candidate_hdr_scene_color`.
- `CinematicPostV3` exists and writes `candidate_ldr_cinematic_output`.
- The raw candidate HDR debug view exists as mode `67`.
- The composite currently uses V3 direct, indirect, and shadow visibility plus
  a bounded legacy HDR fallback.
- Reflection, scene-local environment, material payload richness, emissive GI,
  atmosphere/media, bloom, exposure, and color grading are not yet first-class
  producer inputs to the candidate beauty path.

The next goal feature must therefore be a staged full-scene shader feature, not
a local screenshot polish pass.

### Refactor North Star

Candidate beauty should render from this resource contract:

```text
MaterialResolveV3
  -> material_base_color
  -> material_normal_world
  -> material_roughness
  -> material_metallic
  -> material_ao
  -> material_emissive
  -> material_missing_channel_mask

LightingV3
  -> direct_lighting
  -> indirect_lighting
  -> shadow_visibility
  -> shadow_loss

SceneLocalEnvironmentV3
  -> environment_irradiance
  -> environment_specular_prefilter
  -> visible_background
  -> reflection_background
  -> atmosphere_terms

ReflectionResolverV3
  -> reflection_radiance
  -> reflection_confidence
  -> reflection_source_id
  -> reflection_rejected_source_mask
  -> reflection_temporal_delta

GIAndMediaV3
  -> emissive_indirect
  -> diffuse_probe_lighting
  -> ambient_occlusion
  -> atmosphere_inscatter
  -> atmosphere_transmittance

FullSceneCompositeV3
  -> candidate_hdr_scene_color
  -> overbright_mask
  -> underlit_mask
  -> lighting_energy_budget
  -> composition_debug

CinematicPostV3
  -> candidate_exposure_meter
  -> candidate_bloom_extract
  -> candidate_bloom_resolved
  -> candidate_tonemapped_output
  -> candidate_color_grade_delta
  -> candidate_ldr_cinematic_output
```

Default beauty may continue using the legacy path while this is built. The
candidate path must not silently call back into old helpers that reconstruct
lighting, reflection, or post terms differently from the named V3 resources.

### Implementation Slices

Slice 1: Reflection/environment enters the real composite.

- Feed the existing `local_reflection_radiance` graph resource into
  `FullSceneCompositeV3`.
- Add the SRV binding, HLSL input, frame-report read, analyzer requirement, and
  packet proof.
- Keep the blend conservative. The goal of this slice is ownership and
  inspectability, not a dramatic visual change.
- Admission: `FullSceneCompositeV3` reads `local_reflection_radiance`,
  `candidate_hdr_scene_color` remains nonblank, and mouse-jitter packets still
  pass.

Slice 2: Reflection resolver becomes a real resource producer.

- Split reflection source selection out of report-only evidence.
- Produce `reflection_radiance`, `reflection_confidence`,
  `reflection_source_id`, and `reflection_rejected_source_mask`.
- Support source overrides: local probe, SSR, RT, environment, and auto.
- Admission: glossy/metallic stress views show stable source IDs and bounded
  reflection deltas under mouse jitter.

Slice 3: Scene-local environment becomes real texture data.

- Separate camera-visible background from reflection and lighting backgrounds.
- Create local room/stage reflection backgrounds for enclosed scenes.
- Keep exterior IBL lighting usable without showing unrelated panorama content.
- Admission: enclosed kitchen/office/concert rows do not reflect or display
  unrelated exterior IBL imagery unless explicitly authorized.

Slice 4: Material payload becomes candidate-owned.

- Promote material channels from partial adapter evidence to stable V3
  resources used by reflection and composite.
- Add missing-channel masks and material fallback policy debug views.
- Normalize roughness/metallic/emissive ranges for generated/imported assets.
- Admission: material debug views are nonblank, stable under motion, and
  identify fallback usage instead of hiding it.

Slice 5: GI, emissive, and atmosphere/media.

- Add emissive contribution for neon, red-room, and concert scenes.
- Add diffuse probe/local irradiance contribution for enclosed scenes.
- Add atmosphere/media buffers only after direct material and reflection
  stability are proven.
- Admission: emissive scenes visibly affect nearby surfaces without exposure
  pumping, and non-emissive rooms do not become flat when visible IBL is hidden.

Slice 6: Composite diagnostics and energy controls.

- Move `FullSceneCompositeV3` from `direct + indirect + fallback` to a controlled
  HDR composition of material, direct, indirect, reflection, emissive, media,
  and confidence buffers.
- Emit `overbright_mask`, `underlit_mask`, `lighting_energy_budget`, and
  `composition_debug`.
- Admission: candidate HDR has bounded hot pixels, explainable overbright
  regions, and no silent legacy HDR rescue except in a named fallback view.

Slice 7: Real CinematicPostV3.

- Replace reuse of the current post shader with a candidate-owned filmic stack.
- Add manual/locked-auto exposure, bloom extract/resolve, tone map, color grade,
  optional sharpening, and bypass views.
- Admission: exposure does not pump under mouse motion; bloom only comes from
  bright/emissive sources; raw HDR and final LDR remain independently visible.

Slice 8: Cross-family promotion ladder.

- Run gallery, kitchen, office, gym, classroom, concert, red room, stadium,
  bathroom, bedroom, workshop, store, and street.
- Motion modes: static, mouse jitter, camera sweep, close-surface orbit, and
  reflective-object orbit.
- Artifacts: frame reports, debug metrics, contact sheets, candidate/default
  comparisons, failure reports, and promotion decision.
- Admission: candidate can be reviewed by the user, but default beauty still
  remains unchanged until explicit acceptance.

### Why This Is Better Than Previous Attempts

Previous work often moved artifacts around: blur the IBL, change a scene, add a
local fix, or polish one camera angle. This plan prevents that failure mode by
requiring each visual effect to be an inspectable resource with an owner and a
motion gate.

The engine gets stronger even when a screenshot is not yet beautiful because
every slice adds reusable renderer capability:

- reflection problems become source/confidence problems, not guesswork.
- IBL problems become environment ownership problems, not blur settings.
- material problems become missing-channel and range-normalization problems.
- lighting problems become direct/indirect/shadow resource problems.
- post problems become exposure/bloom/tone-map resource problems.

### Immediate Next Feature

The next implementation should be Slice 1:

`local_reflection_radiance -> FullSceneCompositeV3 -> candidate_hdr_scene_color`

This is the smallest meaningful step toward real full-scene shaders because it
connects an existing reflection producer to the candidate composite path without
changing default beauty. It also gives us a concrete place to detect the
remaining metallic/smooth jitter as reflection-resource instability rather than
as a vague final-image defect.

Implementation status, 2026-06-06:

- `local_reflection_radiance` is now wired into `FullSceneCompositeV3`.
- The shader samples `g_LocalReflectionRadiance : t4`.
- The frame context and analyzer require the reflection read before treating
  `FullSceneCompositeV3` as a real composite producer.
- Python/static plan checks pass.
- Native build passes. If Ninja graph evaluation stalls, run
  `ninja -C build -t recompact` before retrying.
- Static and mouse-jitter gallery candidate HDR packets pass:
  - `build/captures/v3_composite_reflection_input_static_smoke1_20260606`.
  - `build/captures/v3_composite_reflection_input_motion_smoke1_20260606`.
- Motion packet proof:
  `FullSceneCompositeV3` executed, read `direct_lighting`,
  `indirect_lighting`, `shadow_visibility`, `hdr_color`, and
  `local_reflection_radiance`, then wrote `candidate_hdr_scene_color`.

This completes Slice 1 as an opt-in candidate-path refactor. It does not
complete final reflection quality. The next implementation slice is a concrete
`ReflectionResolverV3` producer with source ID, confidence, rejected-source, and
temporal-delta resources.

### Non-Negotiable Gates

- Do not promote default beauty during these slices.
- Do not hide artifacts by disabling IBL, blurring reflection sources, or
  changing the scene under test.
- Do not mark a V3 domain ready unless a pass/resource/debug view supports it.
- Do not trust still screenshots without mouse-jitter and camera-sweep packets.
- Do not let candidate beauty affect normal `beauty` rows.
- Do not call this complete until the user accepts the candidate visuals.

This plan replaces the current pattern of isolated renderer improvements with
one full-scene shader architecture. The goal is not to hide artifacts with
scene changes or one-off settings. The goal is a robust, opt-in candidate
beauty path that can render enclosed rooms, stages, galleries, and large spaces
with stable lighting, shadows, reflections, materials, atmosphere, and post.

## Current Position

V3 already has the right scaffolding shape:

- named domains for material, lighting, environment, reflection, composite, and
  cinematic post.
- frame-report evidence for domain readiness.
- debug views and packet analyzers.
- a promotion decision tool that blocks default-beauty promotion.
- scene-local environment ownership so enclosed scenes do not depend on visible
  IBL leakage.
- source-aware reflection temporal ownership so stable local probes are not
  incorrectly blocked on RT/TAA history.

The current weakness is that several V3 domains are still adapters around older
paths. They prove ownership and wiring, but they are not yet a unified full
scene shading model. The current hard stability risk is a kitchen packet device
removal around the motion-vector path. That must be fixed before any beauty
promotion work is trusted.

## Design Principle

Every visual feature must have four things:

1. a named producer resource.
2. a debug view that exposes the feature directly.
3. a frame-report field that says whether the feature was produced correctly.
4. a packet gate that tests it under motion and across scene families.

If any of those are missing, the feature is not eligible for default beauty.

## Target Frame Architecture

```text
Scene Profile
  -> Geometry / Visibility
  -> Material Resolve V3
  -> Shadow Visibility V3
  -> Direct Lighting V3
  -> Indirect Lighting V3
  -> Reflection Resolver V3
  -> Participating Media / Atmosphere V3
  -> Full Scene Composite V3
  -> Cinematic Post V3
  -> Promotion Gate
```

The candidate beauty path must be opt-in:

```text
legacy beauty: current default output
candidate beauty: FullSceneCandidateBeautyV3
```

The engine should be able to show both, capture both, and compare both. The
candidate path can fail without breaking public default rendering.

Current bridge status:

- `FullSceneCandidateBeautyV3` now renders an opt-in LDR candidate output into
  `candidate_ldr_cinematic_output`.
- The debug menu / settings overlay can request the candidate path without
  command-line flags.
- `FullSceneCandidateBeautyV3Display` can now blit the candidate output to the
  swapchain for review while normal `beauty` rows remain unchanged.
- The candidate review row now has first real producer ownership:
  `FullSceneCompositeV3` writes `candidate_hdr_scene_color` from V3 direct,
  indirect, and shadow-visibility resources; `CinematicPostV3` consumes that
  target and writes `candidate_ldr_cinematic_output`.
- `candidate_hdr_scene_color` is now an explicit raw debug/packet view, so the
  pre-post composite target can be inspected without relying on the LDR
  candidate output.
- Normal/default rows still use adapter evidence until the candidate path is
  stress-tested and explicitly promoted.

Initial bridge:

- `candidate_beauty_v3` is an opt-in packet view.
- the view sets `CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3=1` only for that
  capture row.
- frame reports expose `candidate_beauty_requested`,
  `candidate_beauty_ready`, `candidate_beauty_producer`, and
  `candidate_beauty_output`.
- the first real producer is `FullSceneCandidateBeautyV3`, which may reuse the
  current post shader initially but must render through a named candidate pass
  into `candidate_ldr_cinematic_output`.
- the first real composite/post producer chain is
  `FullSceneCompositeV3 -> CinematicPostV3`; adapter rows remain valid only as
  fallback/default evidence.
- `default_beauty_affects` must remain `false`.

## Phase 0 - Stabilize Before Beauty

Objective:

Fix renderer instability that can invalidate all visual testing.

Work:

- diagnose and fix DX12 device removal in the kitchen packet.
- harden motion-vector shader inputs and visibility-buffer reads.
- add DRED/device-removal packet metadata to failure reports.
- add a focused motion-vector stress packet using kitchen plus one metallic
  stress scene.
- require no device removal before shader quality work continues.

Admission:

- kitchen `beauty` and V3 debug packets complete without device removal.
- frame reports survive failure cases and name the last GPU marker/pass.
- no promotion gate is allowed to ignore failed render rows.

## Phase 1 - Material System Upgrade

Objective:

Replace partial/material-adapter evidence with real PBR attribute ownership.

Outputs:

- `material_base_color`
- `material_normal_world`
- `material_roughness`
- `material_metallic`
- `material_ao`
- `material_emissive`
- `material_opacity`
- `material_class`
- `material_missing_channel_mask`

Work:

- define a stable material payload layout shared by forward, visibility-buffer,
  reflection, and post paths.
- normalize generated/imported assets into physically plausible roughness,
  metallic, emissive, opacity, and normal ranges.
- add fallback policies for missing channels instead of silent constants.
- add debug views for every high-value material channel.

Admission:

- material views are nonblank on gallery, kitchen, gym, concert, and stadium.
- metallic/roughness values are stable under camera motion.
- missing-channel masks identify fallback usage instead of hiding it.

## Phase 2 - Shadow And Direct Lighting Refactor

Objective:

Make shadows and direct lighting stable, readable, and separable.

Outputs:

- `direct_lighting`
- `direct_lighting_unshadowed`
- `shadow_visibility`
- `shadow_loss`
- `shadow_filter_radius`
- `light_importance`

Work:

- separate shadow visibility from direct light accumulation.
- standardize directional, point, spot, rect, emissive, and area-light inputs.
- add contact-shadow ownership without unstable screen-space popping.
- add per-light debug views and light contribution heatmaps.
- add shadow temporal stability metrics under mouse jitter and camera sweep.

Admission:

- no large dark/light popping on static surfaces during mouse motion.
- direct and shadow-loss views have expected nonzero signal.
- floor/wall shadow stability packets pass with IBL on and off.

## Phase 3 - Scene-Local Environment Refactor

Objective:

Use IBL as a lighting source when appropriate, never as accidental room content.

Outputs:

- `ambient_lighting`
- `visible_background`
- `reflection_background`
- `environment_irradiance`
- `environment_specular_prefilter`
- `atmosphere_transmittance`

Work:

- make environment mode a first-class scene-profile decision:
  `enclosed_room`, `open_exterior`, `stage`, `neutral_lab`, `street`, and
  `large_interior`.
- separate the background visible to camera from the background visible to
  reflections.
- build local reflection/ambient probes per scene shell.
- add local fallback colors and gradients for enclosed spaces.

Admission:

- enclosed scenes never show unrelated IBL imagery through the camera.
- glossy objects in enclosed scenes reflect local room lighting, not random
  exterior panoramas.
- open exterior scenes still retain rich sky/IBL lighting.

## Phase 4 - Reflection Resolver V3

Objective:

Replace reflection hacks with a source-aware resolver.

Outputs:

- `reflection_radiance`
- `reflection_confidence`
- `reflection_source_id`
- `reflection_rejected_source_mask`
- `reflection_temporal_delta`
- `reflection_roughness_lod`

Work:

- combine local probes, SSR, ray query, RT reflection, and environment fallback.
- choose sources per pixel based on material roughness, confidence, distance,
  surface orientation, and scene environment mode.
- clamp or blur reflection sources by roughness and confidence.
- expose rejected sources so bad SSR/RT/probe choices are visible.
- add temporal filtering only behind explicit history ownership.

Admission:

- metallic/glossy stress scenes do not shimmer under mouse jitter.
- rough surfaces do not show sharp unrelated IBL detail.
- source ID and confidence views explain every reflection pixel.

## Phase 5 - Indirect Lighting And GI

Objective:

Move beyond flat ambient without relying on accidental IBL bleed.

Outputs:

- `indirect_lighting`
- `ambient_occlusion`
- `diffuse_probe_lighting`
- `emissive_indirect`
- `gi_confidence`

Work:

- add scene-local diffuse probes or irradiance volumes for enclosed spaces.
- keep SSAO/GTAO as contact detail, not the whole indirect solution.
- add emissive contribution for neon/stage scenes.
- maintain a cheap fallback path for unsupported GPUs.

Admission:

- rooms are not flat when IBL background is hidden.
- neon concert/red-room scenes visibly light nearby surfaces.
- indirect lighting remains stable under motion.

## Phase 6 - Full Scene Composite

Objective:

Make final HDR color a controlled composition of known terms.

Inputs:

- material attributes.
- direct lighting.
- indirect lighting.
- shadow visibility.
- reflection radiance/confidence.
- atmosphere/media.
- emissive.

Outputs:

- `hdr_scene_color`
- `lighting_energy_budget`
- `overbright_mask`
- `underlit_mask`
- `composition_debug`

Work:

- implement a candidate composite shader that consumes V3 resources directly.
- add energy clamps and overbright diagnostics.
- expose term isolation views.
- compare V2 beauty and candidate beauty side by side.

Admission:

- candidate beauty is coherent on required families.
- overbright and underlit masks are bounded.
- V3 term isolation explains final output.

## Phase 7 - Cinematic Post

Objective:

Get the Unreal-like final look without hiding upstream renderer defects.

Outputs:

- `exposure_meter`
- `tone_mapped_output`
- `bloom_extract`
- `bloom_resolved`
- `color_grade_delta`
- `final_ldr`

Work:

- implement auto/manual exposure with scene-profile defaults.
- add filmic tonemap and color grading.
- add bloom with threshold diagnostics.
- add optional sharpening and vignette only after stability is proven.

Admission:

- exposure does not pump during small camera motion.
- bloom only appears from bright sources.
- post can be disabled to inspect raw HDR without changing upstream resources.

## Phase 8 - Asset And Material Quality Bridge

Objective:

Make imported/generated scenes feed the shader stack with usable data.

Work:

- add material normalization reports for imported/generated assets.
- author scene-family material presets:
  kitchen, office, gym, classroom, concert, red room, stadium, bathroom,
  bedroom, workshop, store, and street.
- add texture detail tiers for primitive geometry:
  clean blockout, mid-detail procedural, imported asset, hero asset.
- add a material-quality gate to scene packets.

Admission:

- blockout scenes can look materially richer without changing geometry.
- imported assets expose consistent PBR channels.
- bad material ranges are reported before rendering.

## Phase 9 - Cross-Family Evidence

Objective:

Prove the architecture generalizes.

Required families:

- gallery
- kitchen
- office
- gym
- classroom
- concert
- red room
- stadium
- bathroom
- bedroom
- workshop
- store
- street

Required motion modes:

- static
- mouse_jitter
- camera_sweep
- close_surface_orbit
- reflective_object_orbit

Required artifacts:

- frame reports.
- debug view metrics.
- contact sheets.
- before/after candidate beauty comparison.
- failure report if any row fails.
- promotion decision.

Admission:

- no device removal.
- no missing required debug resources.
- no major unresolved instability in materials, shadows, reflections, or
  exposure.
- candidate beauty remains opt-in until user acceptance.

## Implementation Order

1. Fix stability blocker in motion-vector/kitchen packet.
2. Add candidate beauty switch and side-by-side capture support.
3. Convert material attributes from adapter evidence to real shader resources.
4. Convert lighting split into real direct/shadow/indirect resources.
5. Convert scene-local environment into real irradiance/reflection resources.
6. Implement source-aware reflection resolver shader.
7. Implement candidate full-scene composite shader.
8. Implement cinematic post stack.
9. Build cross-family packet ladder and promotion report.

## Full Scene Shader Refactor Blueprint

Date: 2026-06-05.

This is the implementation blueprint for moving from the current V3 evidence
stack to a real full-scene shader stack. The key shift is that "AAA visuals"
must not mean one bigger beauty shader. It must mean a staged render pipeline
where each visual term is produced once, named, inspected, measured, and then
composited.

### Architecture Shape

The target frame should be split into three layers:

1. Foundation layer:
   geometry, visibility, depth, velocity, material attributes, masks, and
   scene profile constants.
2. Lighting layer:
   shadows, direct light, indirect light, environment light, reflections,
   emissive contribution, atmosphere, and confidence/validity buffers.
3. Presentation layer:
   HDR composite, exposure, bloom, tonemap, color grade, sharpening, UI-safe
   final LDR output, and side-by-side candidate capture.

Each layer owns resources. Later layers may read earlier resources, but they
must not silently reconstruct hidden state. For example, the composite pass must
read `direct_lighting`, `indirect_lighting`, `reflection_radiance`, and
`emissive_lighting`; it should not call back into old deferred-lighting helper
code that recomputes those terms differently.

### Candidate Beauty Contract

`FullSceneCandidateBeautyV3` is the safety rail for the refactor.

It must be:

- opt-in through a runtime flag or debug-menu switch.
- captured by packet tooling as a separate view.
- rendered into its own resource, not only aliased to the current back buffer.
- compared against default beauty without changing default beauty.
- blocked from default promotion until the user explicitly accepts it.

Target resources:

- `candidate_hdr_scene_color`
- `candidate_ldr_cinematic_output`
- `candidate_exposure_meter`
- `candidate_bloom_resolved`
- `candidate_color_grade_delta`
- `candidate_debug_term_id`

Admission for this step:

- `candidate_beauty_v3` packet row reports
  `candidate_beauty_producer=FullSceneCandidateBeautyV3`.
- `candidate_ldr_cinematic_output` exists as a valid runtime resource.
- a pass named `FullSceneCandidateBeautyV3` writes that resource.
- normal `beauty` rows keep `default_beauty_affects=false`.

### Pass And Resource Map

The intended pass graph is:

```text
DepthPrepass / VisibilityBuffer
  writes: depth, velocity, visibility_id

MaterialResolveV3
  reads: visibility_id, material tables, textures
  writes: material_base_color, material_normal_world,
          material_roughness, material_metallic, material_ao,
          material_emissive, material_missing_channel_mask

ShadowVisibilityV3
  reads: depth, material_normal_world, light tables, shadow maps
  writes: shadow_visibility, shadow_loss, shadow_filter_radius

DirectLightingV3
  reads: material_*, shadow_visibility, light tables
  writes: direct_lighting, direct_lighting_unshadowed, light_importance

SceneLocalEnvironmentV3
  reads: scene profile, local probes, sky/IBL assets
  writes: environment_irradiance, environment_specular_prefilter,
          visible_background, reflection_background, atmosphere_terms

IndirectLightingV3
  reads: material_*, environment_irradiance, probes, AO, emissive
  writes: indirect_lighting, diffuse_probe_lighting,
          emissive_indirect, gi_confidence

ReflectionResolverV3
  reads: material_*, depth, velocity, local probes, SSR, RT reflections,
         reflection_background
  writes: reflection_radiance, reflection_confidence,
          reflection_source_id, reflection_rejected_source_mask,
          reflection_temporal_delta, reflection_roughness_lod

AtmosphereMediaV3
  reads: depth, light tables, scene profile, fog/media volumes
  writes: atmosphere_inscatter, atmosphere_transmittance, volumetric_light

FullSceneCompositeV3
  reads: direct_lighting, indirect_lighting, reflection_radiance,
         material_emissive, atmosphere_*, confidence/mask buffers
  writes: candidate_hdr_scene_color, lighting_energy_budget,
          overbright_mask, underlit_mask, composition_debug

CinematicPostV3
  reads: candidate_hdr_scene_color, exposure history, bloom chain,
         color-grade settings
  writes: candidate_ldr_cinematic_output, candidate_exposure_meter,
          candidate_bloom_resolved, candidate_color_grade_delta
```

### Refactor Rules

Use these rules while replacing adapters with real producers:

- One producer owns one resource. If two passes write the same named resource,
  the frame report must mark the resource ambiguous.
- Every resource gets at least one debug view before it is used by candidate
  beauty.
- Every debug view gets packet metrics: nonblack ratio, luma range, hot-pixel
  ratio, and frame-to-frame delta under motion.
- The default beauty path may read legacy resources until promotion, but the
  candidate beauty path must read V3 resources directly.
- Post-processing cannot be used to hide upstream instability. Raw HDR,
  pre-tonemap, post-tonemap, and final LDR views must stay available.
- IBL has three independent roles: lighting source, reflection source, and
  visible background. Enclosed scenes can enable the first two without showing
  unrelated panorama pixels.
- Reflection source choice must be visible per pixel through
  `reflection_source_id` and `reflection_confidence`.
- Any temporal feature must name its history owner. If history is missing,
  the pass must degrade to a stable non-temporal path instead of flickering.

### Debug Menu Requirements

The debug menu should expose the refactor without forcing command-line flags:

- candidate beauty enable.
- default/candidate split-screen mode.
- post-process bypass.
- reflection source override: auto, local probe, SSR, RT, environment.
- IBL visible background strength.
- IBL reflection strength.
- local probe reflection strength.
- shadow mode: off, hard, filtered, contact only, full.
- exposure mode: manual, locked auto, live auto.
- debug-view selector for every V3 resource.

The menu must display whether the current frame is default beauty, candidate
beauty, or split-screen. This prevents accidental screenshots from being used
as promotion evidence for the wrong path.

### Packet Ladder

Use a ladder instead of one giant final test:

1. Single-pass smoke:
   one scene, static camera, one new resource, one debug view.
2. Motion smoke:
   one scene, mouse jitter plus camera sweep.
3. Reflective stress:
   gallery or metallic stress scene, close-surface orbit.
4. Enclosed-room stress:
   kitchen or office with visible IBL disabled but IBL lighting/reflection
   still available.
5. Stage-light stress:
   concert or red room with emissive and bloom pressure.
6. Large-space stress:
   stadium or gym with long camera distances and many repeated surfaces.
7. Cross-family packet:
   required families, required motion modes, candidate beauty enabled.
8. Promotion packet:
   before/after contact sheets, debug evidence, failure report, and explicit
   default-promotion decision.

No phase should skip directly to step 7. The current problem class is often
only visible in one narrow condition, so the ladder must preserve targeted
repro rows.

### Implementation Milestones

Milestone 1: real candidate output.

- add an offscreen LDR target for `candidate_ldr_cinematic_output`.
- add `FullSceneCandidateBeautyV3` as a named render graph pass.
- reuse current post shader initially, but write to the candidate resource.
- update frame reports and analyzers to require the real pass/resource.

Milestone 2: material payload normalization.

- create a stable material V3 resource layout.
- route imported/generated asset material data into it.
- expose missing channels and fallback ranges.
- add material debug-view packet gates.

Milestone 3: reflection resolver ownership.

- split local probe, SSR, RT, and environment reflection sources.
- add source ID, confidence, and rejected-source masks.
- add roughness-aware filtering and confidence clamping.
- validate on metallic stress with mouse jitter.

Current status, 2026-06-06:

- `FullSceneReflectionV3` is now a concrete render-graph producer, not only a
  report adapter.
- It writes `reflection_radiance`, `reflection_confidence`,
  `reflection_source_id`, `reflection_rejected_source_mask`, and
  `reflection_temporal_delta`.
- `FullSceneCompositeV3` consumes `reflection_radiance`.
- Gallery static and mouse-jitter packets pass:
  - `build/captures/v3_reflection_resolver_static_smoke2_20260606`.
  - `build/captures/v3_reflection_resolver_motion_smoke1_20260606`.
- The next reflection work is deeper source arbitration and filtering:
  local probe vs SSR vs RT/ray query vs scene-local environment, with
  roughness-aware source choice and temporal-history admission. Do not redo the
  basic resource/pass/report wiring unless packet evidence regresses.
- Source-policy admission has started:
  - auto policy prefers scene-local radiance, then scene-local environment.
  - `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE` can force `local`,
    `environment`, or `none` for packet/debug review.
  - frame reports expose forced source contracts instead of pretending the
    local-probe path was used.
  - packets:
    `build/captures/v3_reflection_source_policy_auto_static_smoke1_20260606`,
    `build/captures/v3_reflection_source_policy_environment_static_smoke1_20260606`,
    and
    `build/captures/v3_reflection_source_policy_auto_motion_smoke1_20260606`
    passed.
- SSR input admission has started:
  - `FullSceneReflectionV3` reads `ssr_color` in addition to
    `local_reflection_radiance`.
  - `FullSceneReflectionResolverV3` samples `g_SSRReflection : t1`.
  - `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE` can now force `ssr`,
    `screen_space`, or numeric `2`.
  - frame reports expose `forced_screen_space_reflection`.
  - auto policy admits SSR only when its confidence beats scene-local radiance
    by a large margin, so unstable screen-space data does not replace stable
    local/environment fallback by default.
  - auto static and mouse-jitter packets pass:
    `build/captures/v3_reflection_ssr_input_auto_static_smoke1_20260606` and
    `build/captures/v3_reflection_ssr_input_auto_motion_smoke1_20260606`.
  - forced SSR stress packet proved the render-graph wiring but failed signal
    gates because SSR produced blank radiance/confidence in that stress view.
- Remaining source-arbitration work is to make SSR source quality reliable,
  then add RT/ray-query inputs and choose among all reflection sources by
  roughness, confidence, distance, surface orientation, scene mode, and history
  availability.

Milestone 4: scene-local environment resources.

- create real irradiance/specular/background resources.
- give enclosed rooms local backgrounds and local probes.
- keep visible IBL separate from reflection/lighting IBL.
- validate old-office-IBL conditions without hiding the issue by blur.

Milestone 5: composite and post.

- build `FullSceneCompositeV3` over V3 lighting/reflection/environment terms.
- build `CinematicPostV3` with locked exposure diagnostics.
- add split-screen compare and contact-sheet output.

Milestone 6: cross-family promotion.

- run gallery, kitchen, office, gym, classroom, concert, red room, stadium,
  bathroom, bedroom, workshop, store, and street.
- require static, mouse jitter, camera sweep, close-surface orbit, and
  reflective-object orbit rows.
- keep default beauty unchanged until user review accepts candidate output.

### Stop Conditions

Stop a phase and diagnose if any of these happen:

- a packet row has device removal or a missing frame report.
- a resource is marked ready but has no producing pass.
- a candidate output exists only as a report field and not as a texture.
- a debug view is blank while its producer is marked ready.
- a motion packet shows large luminance jumps on static floor/wall pixels.
- enabling candidate beauty changes normal `beauty` output.
- IBL blur, background hiding, or disabled reflections are the only reason a
  visible artifact disappears.

## Goal Completion Boundary

Do not claim completion when a screenshot looks better.

Completion requires:

- the candidate beauty path exists and is opt-in.
- required V3 domains are real shader resources, not only report adapters.
- cross-family packets produce evidence across required scene families.
- stability packets pass without device removal.
- debug views explain the final image.
- user accepts the candidate visuals as good enough to promote.

## Next Refactor Direction - Full Scene Shader Renderer

Target:

- Build an opt-in `FullSceneCandidateBeautyV3` renderer that can plausibly
  reach high-end real-time scene quality without destabilizing current default
  beauty.
- Keep the renderer evidence-driven: every visual feature must produce named
  render-graph resources, frame-report ownership, debug views, and packet
  gates before it is allowed to affect the candidate composite.
- Do not treat IBL blur, hidden backgrounds, disabled reflections, or scene
  switching as fixes. The same scene must pass with the relevant source enabled
  and inspected.

Architectural shape:

1. Material payload V3.
   - Normalize asset/material data into a stable PBR payload:
     base color, normal, roughness, metallic, specular, emissive, opacity,
     AO, height/parallax, clearcoat, sheen, anisotropy, and material class.
   - Add material-class debug and range gates so tiny wrong parameters cannot
     silently produce plastic, chalky, or mirror-like surfaces.
   - Treat missing texture channels as explicit fallback debt in reports.

2. Lighting V3.
   - Split direct lighting, shadow visibility, indirect lighting, emissive
     lighting, volumetric/fog contribution, and exposure pre-tonemap energy.
   - Refactor shadows toward stable cascades/contact shadows with motion
     stability tests on floors and walls.
   - Add light-family contracts: room fill/key/rim, daylight window, stage
     lighting, neon/emissive, outdoor sun/sky.

3. Reflection V3.
   - Continue the resolver architecture already started:
     local radiance, SSR, RT/ray query, planar/hero probes, and
     scene-local environment are separate sources.
   - Each source must expose radiance, confidence, source ID, rejection mask,
     and temporal-history debt.
   - Next blocker is SSR source quality: it is wired but forced SSR is blank
     in the current stress packet.

4. Scene-local environment V3.
   - Separate visible background from lighting/reflection environment.
   - Enclosed scenes get local ambient/specular/probe contracts rather than
     visible outdoor/office IBL reflections leaking into every room.
   - Exterior scenes can still use visible sky/IBL, but through an explicit
     scene environment profile.

5. Composite V3.
   - Build a true `candidate_hdr_scene_color` from V3 material, lighting,
     reflection, environment, transparency, water/glass, decals, and effects.
   - Keep legacy `hdr_color` as an input/reference until the candidate path
     owns enough terms to stand alone.
   - Add split-screen and channel-isolation debug modes.

6. Cinematic post V3.
   - Add controlled exposure, bloom, tone mapping, color grade, glare, depth of
     field, motion/TAA history diagnostics, and LDR output ownership.
   - Lock exposure for stability tests so camera motion cannot hide flicker as
     adaptation.

Execution order:

1. SSR source-quality pass.
   - Build a source packet that captures raw `ssr_color`, SSR confidence,
     normal/roughness/depth inputs, rejection reasons, and resolver output.
   - Fix why forced SSR is blank before allowing SSR to win more often.
   - Current status, 2026-06-06:
     - `reflection_ssr_source_signal` is now a real ReflectionV3 output and
       debug view `73`.
     - forced SSR no longer goes blank when raw SSR has nonzero signal below
       the auto admission threshold.
     - auto SSR remains strict and still falls back to scene-local radiance in
       the gallery packet.
     - SSR producer hit coverage/confidence improved with a refined raymarch:
       96 steps, reduced near-origin skip, crossing refinement, edge fade, and
       source-confidence alpha.
     - forced SSR static coverage improved from about `8%` nonblack radiance to
       about `40%`.
     - forced SSR remains more motion-sensitive than the scene-local auto path,
       so next work is temporal/confidence stabilization and RT/ray-query
       fallback fusion, not loosening auto source selection.

2. Material payload pass.
   - Replace ad hoc material interpretation with a V3 material resource and
     material-class debug gates.
   - Validate gallery, kitchen, office, gym, and concert closeups.

3. Shadow and lighting stability pass.
   - Reproduce floor/wall camera-motion tests under locked exposure and fixed
     scene-local lighting.
   - Add shadow debug resources and reject candidate rows with moving static
     shadows on stationary geometry.

4. Scene-local environment pass.
   - Create room-local environment resources and reflection-safe backgrounds.
   - Prove old-office IBL can remain enabled without leaking inappropriate
     visible/reflection content into enclosed scenes.

5. Candidate composite pass.
   - Move from adapter ownership to actual candidate compositing over V3
     resources.
   - Compare candidate vs default beauty with contact sheets and per-channel
     diagnostics.

6. Cross-family art packet.
   - Run gallery, kitchen, office, gym, classroom, concert, red room, stadium,
     bathroom, bedroom, workshop, store, street.
   - Require static, mouse jitter, camera sweep, close-surface orbit, and
     reflective-object orbit rows.

Promotion rule:

- Candidate beauty can be default-promoted only after the V3 pipeline produces
  stable multiview evidence and the user accepts the visual result. Until then
  all work stays opt-in and explicitly reported as candidate renderer progress.

## 2026-06-06 Full AAA Scene Shader Refactor Execution Plan

This is the implementation map for moving from evidence slices to a real
candidate renderer. The goal is a full-scene shader stack that can produce
high-end, Unreal-style visual quality while staying debuggable enough to fix
root causes instead of hiding artifacts with blur, scene changes, or disabled
sources.

### North Star

Build an opt-in renderer path where final pixels come from owned V3 resources:

```text
Visibility / Motion / Depth
  -> Material Payload V3
  -> Scene-Local Environment V3
  -> Lighting and Shadow V3
  -> Reflection / Transparency / Media V3
  -> Candidate HDR Composite V3
  -> Cinematic Post V3
  -> Candidate LDR Beauty
```

Default beauty remains a comparison target. It is not replaced until the
candidate path passes cross-family packet gates and user review.

### Refactor Principle

Every visual term must pass the same ownership ladder:

```text
contract name
  -> render-graph resource
  -> producer shader/pass
  -> consumer shader/pass
  -> debug view
  -> frame-report proof
  -> packet metrics
  -> contact sheet
  -> promotion gate
```

A feature that only exists as a report field, adapter alias, post tweak, or
scene-specific workaround is not allowed into candidate beauty.

### Current Architecture Boundary

Already real enough to build on:

- `FullSceneReflectionV3` owns source-aware reflection outputs.
- `reflection_ssr_source_signal` exposes raw/admitted SSR diagnostics.
- `SSR.hlsl` now produces stronger source coverage and forced SSR no longer
  goes blank in the tested static packet.
- `FullSceneReflectionHistoryV3` owns current/previous radiance,
  previous-source-ID carryover, validity, and rejection diagnostics.
- V3 packet tooling can capture resources, frame reports, motion metrics, and
  promotion decisions.

Still adapter or incomplete:

- `FullSceneCompositeV3` still depends on current HDR output instead of owning
  final scene assembly from V3 terms.
- `CinematicPostV3` still wraps current post behavior instead of owning a full
  filmic output chain.
- material data is not yet a complete V3 PBR payload with range/missing-channel
  diagnostics.
- lighting is not yet split into stable direct, shadow, indirect, emissive, and
  atmosphere resources.
- scene-local environment is mostly a contract/profile layer, not a complete
  texture-backed irradiance/specular/background system.
- SSR is improved but still motion-sensitive; it should not dominate auto
  reflections until history-aware confidence is in place.

### Phase 1 - Reflection Stability and Source Fusion

Purpose:

- Make glossy and metallic surfaces stable enough that later beauty work has a
  solid reflection base.

Implementation:

- finish `FullSceneReflectionHistoryV3` reprojection validity using velocity,
  depth, normal/roughness, and previous-source-ID history.
- track history acceptance, source switches, disocclusion rejection,
  high-motion rejection, bounds rejection, depth mismatch, normal mismatch,
  and missing-history debt.
- feed history validity into reflection confidence, but keep resolver source
  selection conservative until packets prove stability.
- use previous source ID, history validity, and rejection diagnostics as bounded
  hysteresis inputs for auto SSR/RT admission; forced debug sources must bypass
  that hysteresis so source packets remain inspectable.
- use material roughness/metallic as source weighting inputs so rough surfaces
  reject sharp reflection sources and smooth/metallic surfaces admit stronger
  SSR/RT candidates when history allows.
- add RT/ray-query reflection as a first-class resolver source with its own
  source signal, confidence, rejection mask, and debug view.
- keep source IDs explicit: local probe, SSR, RT/ray-query, planar/hero probe,
  scene-local environment, forced debug sources, and unavailable.

Acceptance:

- static, mouse-jitter, and reflective-object orbit packets show no large
  unexpected luma jumps on smooth/metallic objects.
- forced SSR remains inspectable, but auto SSR only wins where history validity
  and confidence are strong.
- frame reports prove Reflection V3 reads depth, velocity, normal/roughness,
  current source signals, previous radiance history, and previous source ID,
  and writes a rejection diagnostic resource.

### Phase 2 - Material Payload V3

Purpose:

- Stop treating material quality as an accidental byproduct of imported texture
  data or legacy shader assumptions.

Implementation:

- create real material payload resources or a packed material target set:
  base color, normal world/view, roughness, metallic, specular, AO, emissive,
  opacity, transmission, clearcoat, sheen, anisotropy, IOR, thickness,
  material class, and missing-channel mask.
- normalize material ranges at the boundary, not inside each consumer.
- add material-class policies for wall paint, tile, wood, metal, glass, water,
  plastic, fabric, skin/organic, emissive, ceramic, concrete, and vegetation.
- emit material debt when fallback values are used.

Acceptance:

- close-surface packets for tile, metal, glass, water, wood, cloth, plastic,
  painted wall, emissive sign, and rough concrete.
- material debug views are nonblank and stable under camera motion.
- invalid ranges fail the candidate gate instead of silently producing
  plastic/chalk/mirror artifacts.

### Phase 3 - Scene-Local Environment V3

Purpose:

- Preserve useful IBL lighting while removing inappropriate panorama leakage
  from enclosed rooms, concert halls, gyms, and galleries.

Implementation:

- split environment into visible background, diffuse irradiance, specular
  prefilter, reflection background, local probe rig, atmosphere/media, and
  ownership mask.
- define environment profiles:
  enclosed room, neutral gallery/lab, kitchen/interior daylight, gym overhead,
  stage/concert, red room, exterior street, exterior stadium.
- add local reflection/probe resources for enclosed scenes.
- allow old-office IBL and other HDRIs to be lighting inputs only when the
  scene profile admits them.

Acceptance:

- enclosed scenes show no visible or reflected outdoor/office panorama leaks
  unless explicitly requested.
- old-office-IBL test conditions stay enabled and pass without blur-only fixes.
- reflection-background and visible-background debug views explain what the
  scene is using.

### Phase 4 - Lighting and Shadow V3

Purpose:

- Make scene lighting artistic and stable before post processing tries to make
  it impressive.

Implementation:

- produce direct lighting, unshadowed direct lighting, shadow visibility,
  shadow loss, indirect diffuse, emissive indirect, atmosphere/media lighting,
  and lighting energy budget.
- stabilize cascaded shadows, contact shadows, and screen-space/RT shadow
  terms under mouse jitter.
- add semantic light rigs:
  daylight window, warm interior practicals, gym overhead rows, neon strips,
  stage spot/rim/fill, red-room low key, exterior sun/sky.
- lock exposure for lighting/shadow validation packets.

Acceptance:

- static floor/wall pixels do not flicker in locked-exposure motion packets.
- shadow visibility explains every major darkening term.
- direct/indirect/emissive channels are independently inspectable and nonzero
  in the families where they are expected.

### Phase 5 - Transparency, Water, Glass, Decals, and Media

Purpose:

- Stop forcing all smooth, transparent, or layered materials through one opaque
  reflection path.

Implementation:

- add separate resources for glass radiance, water radiance, transparent
  accumulation, transmission mask, volumetric inscatter, volumetric
  transmittance, decal albedo/normal/material contribution, and particle
  lighting.
- route material classes to the correct domain before composite.
- add ordering/weight diagnostics so transparent terms cannot double-light or
  fight opaque reflections.

Acceptance:

- glass and water closeups remain stable under camera motion.
- decals and particles do not corrupt material payload ranges.
- transparent terms are visible in debug resources before entering composite.

### Phase 6 - Candidate HDR Composite V3

Purpose:

- Replace the adapter over legacy `hdr_color` with an actual V3 scene
  assembly shader.

Implementation:

- build `candidate_hdr_scene_color` from material payload, direct lighting,
  indirect lighting, shadow loss, reflection radiance/confidence, transparency,
  water/glass, emissive, decals, atmosphere, and environment terms.
- keep legacy `hdr_color` as a named comparison/reference input, not the
  candidate owner.
- output overbright mask, underlit mask, invalid-energy mask, and composition
  debug.
- add split-screen default/candidate debug modes.

Acceptance:

- candidate HDR is nonblank and explainable without reading legacy `hdr_color`
  as its main source.
- channel isolation proves each term contributes in expected scene families.
- energy diagnostics catch clipped, underlit, or double-lit pixels.

### Phase 7 - Cinematic Post V3

Purpose:

- Make the final image cinematic without using post to hide upstream
  instability.

Implementation:

- own exposure meter, locked/manual exposure, bounded auto exposure, bloom
  extract/resolve, glare, filmic tone map, color grade, sharpening, vignette,
  depth of field, and candidate LDR output.
- source bloom from overbright/emissive HDR terms, not arbitrary LDR
  brightness.
- add bypass modes: raw HDR, HDR without reflections, HDR without indirect,
  post without bloom, post without grade, final candidate.

Acceptance:

- locked exposure packets pass before auto exposure is evaluated.
- bloom/glare is stable under camera motion and tied to real HDR sources.
- candidate LDR output has contact sheets for default, raw HDR, and final post.

### Phase 8 - Cross-Family Art and Stability Matrix

Purpose:

- Prove the renderer works beyond one pretty screenshot.

Required families:

- gallery, kitchen, office, gym, classroom, concert, red room, stadium,
  bathroom, bedroom, workshop, store, street, exterior water/vegetation.

Required motion rows:

- static.
- mouse jitter.
- camera sweep.
- close-surface orbit.
- reflective-object orbit.
- high-contrast light sweep.

Required packet outputs:

- frame report shutdown JSON.
- debug resource metrics.
- motion/stability JSON and markdown.
- contact sheet.
- promotion decision.
- failure report with resource owners and first failing gate.

Promotion:

- only candidate beauty can be promoted.
- default beauty remains unchanged until the full matrix passes and user review
  accepts the result.

### Near-Term Implementation Order

The next work should proceed in this order:

1. finish ReflectionHistoryV3 reprojection validity and rejection diagnostics.
2. add RT/ray-query reflection source signal and source fusion diagnostics.
3. convert Composite V3 from adapter to real `candidate_hdr_scene_color`.
4. promote material payload from aggregate contract to concrete PBR resources.
5. make SceneLocalEnvironmentV3 texture-backed for enclosed rooms.
6. split Lighting V3 into direct/shadow/indirect/emissive resources.
7. build CinematicPostV3 on top of candidate HDR.
8. run the full cross-family matrix and iterate only on failing gates.

This order keeps the known user-visible issues in scope: smooth/metal jitter,
shadow flicker, IBL leakage, flat materials, weak lighting, and adapter-based
beauty. It also prevents the old failure pattern where one scene becomes less
bad while the renderer remains unexplainable.

### Current ReflectionV3 Material Policy Slice - 2026-06-06

Status:

- semantic material input is now wired into `FullSceneReflectionV3` through
  `vb_gbuffer_material_ext2`.
- reflection material payload reads now use pixel-exact `Load()` in
  `FullSceneReflectionResolverV3.hlsl` and `LocalReflectionRadiance.hlsl`
  instead of linear filtering categorical IDs/classes.
- material stress packets now capture and summarize `surface_class`,
  `material_family`, and frame-report smooth-class coverage.

Evidence:

- water packet:
  `build/captures/reflection_v3_material_policy_water_after_pixel_loads_20260606`.
- metal/glass packet:
  `build/captures/reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606`.
- metal/glass reports no material-stress warnings.
- water remains high roughness despite smooth-class coverage, so the next
  refactor should target water/glass BRDF roughness policy and source
  admission, not descriptor plumbing.

Update:

- The water warning was reclassified as a harness ownership issue. Water
  targets now use `frame_contract.water.roughness` when the water pass owns the
  target surface.
- `glass_water_courtyard:water_closeup` now reports opaque center roughness
  `0.75008`, target roughness `0.03000`, and no material-stress warnings.
- Continue renderer work on BRDF/source quality, but do not use opaque
  G-buffer roughness as the water-pass admission gate.
