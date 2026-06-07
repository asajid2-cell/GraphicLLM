# Full Scene Shader AAA Refactor Plan

Status: planning ledger.

Default beauty stays unchanged until a separate promotion gate passes.

## 2026-06-07 Authoritative Execution Queue

This section supersedes older "next work" notes below when they conflict. The
goal is a full-scene shader architecture, not another local beauty patch. The
current renderer already has useful V3 slices for composite contribution,
reflection history, material debug coverage, and LightingV3 energy/shadow
attribution. The remaining work should now move through the stack in dependency
order so each later visual feature consumes owned resources instead of legacy
adapters.

### Current State

Implemented and kept candidate-only:

- `FullSceneCompositeV3` contribution and legacy rescue diagnostics exist.
- `FullSceneLightingV3` writes direct, unshadowed direct, shadow visibility,
  shadow loss, indirect, lighting-energy budget, and shadow-source
  attribution.
- Reflection-focused motion packets exist and the latest forced-SSR history
  warning was resolved with confidence-weighted history diagnostics.
- Standard V3 packets cover material base color, normal, roughness, metallic,
  material class, and `material_missing_channel_mask`.
- `material_missing_channel_mask` is now a required material channel, appears
  in the material domain frame evidence as `VB_MaterialMissingChannelMask`, and
  is covered by a focused material packet with zero contract debug-view debt.
- Focused shadow-motion packets exist. The missing focused row is a
  high-contrast light-sweep/stress row that keeps IBL and scene conditions
  visible instead of hiding instability.

Still not complete:

- `SceneProfileV3` is not yet the single policy owner for environment,
  lighting, reflection, exposure, material expectations, and post.
- `SceneLocalEnvironmentV3` is not yet the texture-backed owner of visible
  background, diffuse irradiance, specular prefilter, reflection background,
  atmosphere, and ownership mask across indoor/stage/exterior profiles.
- `CompositeV3` still has measured legacy rescue debt; it is not yet a
  V3-only HDR owner.
- `CinematicPostV3` is not ready for strong beauty tuning because upstream
  material, environment, lighting, reflection, transparency, and composite
  ownership are not fully promoted.
- Cross-family promotion has not happened. One passing stress packet is not a
  promotion proof.

### Refactor Queue

Work in this order unless a failing packet proves a different root dependency:

1. **Contract reconciliation and promotion gate hardening.**
   - Make the JSON contract, C++ frame context, debug registry, packet aliases,
     and analyzers agree on the canonical V3 resource list.
   - Add failure reasons for missing, blank, stale, legacy-owned, and
     uninspected resources.
   - Promotion evidence: an intentionally missing V3 resource fails the packet
     by name; default beauty remains untouched.

2. **MaterialPayloadV3 missing-channel ownership.**
   - Done for the current aggregate material path: `material_missing_channel_mask`
     is required by contract, captured by packet alias, included in frame
     evidence, and quantified by the material analyzer.
   - Continue by making downstream consumers report whether they consumed
     missing-channel debt.
   - Track missing base-color, normal, roughness, metallic, AO, emissive,
     opacity/transmission, clearcoat, sheen, anisotropy, IOR, and thickness.
   - Promotion evidence: material packets can show which channels are owned,
     which are fallback, and which consumers read fallback debt.

3. **SceneProfileV3 policy owner.**
   - Replace scattered scene-family conditionals with a declared profile that
     owns enclosure mode, local environment mode, light rig, reflection source
     priority, exposure policy, material expectations, post look, and motion
     tolerances.
   - Required initial profiles: neutral lab, gallery, kitchen, office, gym,
     classroom, concert, red room, stadium, and exterior water.
   - Promotion evidence: changing scene family changes profile output in the
     frame report without changing renderer code.

4. **SceneLocalEnvironmentV3 ownership.**
   - Build the texture/resource split for visible background, diffuse
     irradiance, specular prefilter, reflection background, atmosphere, and
     environment ownership.
   - Keep old-office IBL as a stress case with IBL on and sharp enough to
     reveal wrong reflections. Do not blur or disable it to pass.
   - Promotion evidence: enclosed kitchen/concert/gym packets no longer show
     or sharply reflect unrelated IBL scenery unless the profile explicitly
     permits it.

5. **LightingShadowV3 high-contrast stress and source split.**
   - Add the focused high-contrast light-sweep row.
   - Split attribution further if needed: directional shadow map, local light
     shadow map, contact shadow, RT/screen-space shadow, PCSS/filter radius,
     and exposure contribution.
   - Promotion evidence: floor/wall darkening and motion flicker can be
     attributed to a named term or proven absent under locked exposure.

6. **ReflectionV3 provider expansion and resolver hardening.**
   - Keep SSR, RT/ray query, local probe, planar/hero probe, and environment
     fallback as separate source signals.
   - Resolve with source ID, confidence, rejection mask, history validity,
     disocclusion, and hysteresis.
   - Promotion evidence: smooth/metallic objects keep stable source IDs and
     confidence under mouse jitter, close orbit, and reflective-object orbit.

7. **TransparencyMediaV3.**
   - Add owned water, glass, transparent accumulation, decals, particles,
     volumetric inscatter, volumetric transmittance, and ordering diagnostics.
   - Promotion evidence: water/glass closeups are stable and separable from
     opaque reflection/composite behavior.

8. **CompositeV3 V3-only HDR assembly.**
   - Reduce legacy rescue from normal operation to a measured emergency lane.
   - Expand contribution diagnostics into material, direct, shadow loss,
     indirect, reflection, environment, transparency, emissive, atmosphere,
     decals, and rescue terms.
   - Promotion evidence: target packets produce nonblank candidate HDR with
     near-zero legacy rescue and explainable channel contribution.

9. **CinematicPostV3.**
   - Add locked/manual exposure, bounded auto exposure, bloom extract/resolve,
     glare, filmic tonemap, color grade, sharpening, vignette, optional DOF,
     and bypass views.
   - Promotion evidence: raw HDR, post without bloom, post without grade, and
     final candidate LDR all pass stability packets. Bloom/glare must come
     from real HDR/emissive masks.

10. **Cross-family promotion matrix.**
    - Families: gallery, kitchen, office, gym, classroom, concert, red room,
      stadium, bathroom, bedroom, workshop, store, street, and exterior
      water/vegetation.
    - Motion rows: static, mouse jitter, camera sweep, close-surface orbit,
      reflective-object orbit, and high-contrast light sweep.
    - Promotion evidence: metrics, frame reports, contact sheets, failure
      reports, and user review accept the candidate visuals. Only then can
      default beauty be considered for promotion.

### Next Concrete Slice

The next implementation slice should not be cinematic post. It should be:

```text
SceneProfileV3 policy owner
  -> declared profile object for scene family, enclosure, environment,
     lighting, reflection, exposure, material expectations, post, and motion
  -> frame-report profile evidence
  -> profile-driven differences for at least neutral lab, gallery, kitchen,
     concert, gym, red room, stadium, and exterior water
  -> focused profile packet evidence
```

This is the right next step because material-channel ownership is now covered
for the current aggregate path. The renderer now needs one policy owner so
environment, lighting, reflections, exposure, material expectations, and post
stop drifting through scattered local conditionals.

## 2026-06-07 Full Scene Shader Refactor Execution Blueprint

The refactor should be planned as a renderer product line: the current public
renderer remains the stable line, while `FullSceneCandidateBeautyV3` becomes
the AAA-quality line under review. The goal feature is not complete when a
single scene looks better. It is complete when the candidate line can produce a
beautiful image from named, stable, inspectable scene-shader resources and prove
that each visible contribution is owned.

### Why This Is The Right Direction

The existing failure pattern was predictable: blur, stronger IBL filtering,
scene changes, or local material tweaks could reduce a visible artifact without
proving that the renderer understood the image. Unreal-style results need a
coherent stack:

```text
scene intent
  -> stable visibility
  -> rich material payload
  -> scene-local environment
  -> owned lights and shadows
  -> source-aware reflections
  -> transparent/media layers
  -> HDR composite
  -> cinematic post
  -> promotion evidence
```

If any layer is hidden behind legacy `hdr_color`, old global IBL behavior, or
anonymous fallback material defaults, the image can look acceptable in one
camera angle and still break under mouse motion or another scene family. The
refactor therefore prioritizes ownership and diagnostics before visual strength.

### Final Candidate Render Graph

The target graph is:

```text
SceneProfileV3
  produces policy constants:
    scene family, enclosure mode, key/fill/rim rig, exposure policy,
    reflection provider priority, local environment policy, post look,
    material palette, motion stability tolerances

VisibilityV3
  writes:
    depth, velocity, object_id, material_id, surface_id,
    normal_for_history, disocclusion_mask

MaterialPayloadV3
  reads:
    visibility IDs, mesh material table, texture/material descriptors
  writes:
    base_color, normal_world, roughness, metallic, specular, ao,
    emissive, opacity, transmission, clearcoat, sheen, anisotropy,
    ior, thickness, material_class, missing_channel_mask,
    invalid_range_mask

SceneLocalEnvironmentV3
  reads:
    SceneProfileV3, environment state, optional IBL/probe assets
  writes:
    visible_background, ambient_lighting, reflection_background,
    specular_prefilter, atmosphere, environment_ownership_mask

LightingShadowV3
  reads:
    SceneProfileV3, VisibilityV3, MaterialPayloadV3,
    SceneLocalEnvironmentV3, light records, shadow maps/RT data
  writes:
    direct_lighting, direct_lighting_unshadowed, shadow_visibility,
    shadow_loss, indirect_lighting, emissive_indirect,
    lighting_energy_budget, shadow_source_attribution

ReflectionV3
  reads:
    VisibilityV3, MaterialPayloadV3, SceneLocalEnvironmentV3,
    local probes, SSR, RT reflection, previous reflection history
  writes:
    reflection_radiance, reflection_confidence, reflection_source_id,
    reflection_rejected_source_mask, reflection_temporal_delta,
    reflection_ssr_source_signal, reflection_rt_source_signal,
    reflection_probe_source_signal, reflection_env_source_signal,
    reflection_history_validity, reflection_source_suppression

TransparencyMediaV3
  reads:
    VisibilityV3, MaterialPayloadV3, LightingShadowV3, ReflectionV3,
    water/glass/decal/particle/volume records
  writes:
    glass_radiance, water_radiance, transparent_accumulation,
    decal_accumulation, volumetric_inscatter, volumetric_transmittance,
    transparency_order_diagnostics

CompositeV3
  reads:
    MaterialPayloadV3, LightingShadowV3, ReflectionV3,
    SceneLocalEnvironmentV3, TransparencyMediaV3, legacy hdr_color reference
  writes:
    candidate_hdr_scene_color, contribution_map, legacy_rescue_usage,
    overbright_diagnostics, underlit_diagnostics, invalid_energy_mask

CinematicPostV3
  reads:
    candidate_hdr_scene_color, contribution/energy diagnostics,
    SceneProfileV3 post policy
  writes:
    exposure_meter, bloom_extract, bloom_resolve, tone_mapped_ldr,
    color_grade_delta, candidate_ldr_cinematic_output
```

The rule is simple: if a resource is not in this graph, not in the frame
contract, and not visible in a debug packet, it cannot be used to claim AAA
candidate quality.

### Refactor Milestones

Milestone 0: freeze the contract.

- Make the JSON contract, C++ frame context, render-graph resource names,
  debug view registry, packet scripts, and analyzers agree on the same V3
  resource vocabulary.
- Add a promotion gate that rejects missing, blank, stale, legacy-owned, or
  uninspected candidate resources.
- Make legacy `hdr_color` legal only as `legacy_reference` or
  `legacy_rescue_usage`.

Exit proof:

- A broken/missing V3 resource fails the packet for the correct named reason.
- Default beauty remains untouched.

Milestone 1: build the policy layer.

- Promote `SceneProfileV3` from a convenience helper into the single policy
  source for environment, lights, reflections, exposure, post, and material
  expectations.
- Profiles must exist for gallery, kitchen, office, gym, classroom, concert,
  red room, stadium, exterior water, and neutral lab.

Exit proof:

- Two scene families produce different declared policies without changing
  renderer code.
- The frame report records which profile controlled the frame.

Milestone 2: harden visibility and material payload.

- Convert the material path into a typed `MaterialPayloadV3` boundary.
- Normalize PBR ranges at the boundary.
- Track missing channels and invalid ranges as visible debt.
- Ensure categorical IDs use point/exact sampling, not filtered reads.

Exit proof:

- Material debug packets explain roughness, metallic, normal, emissive,
  transparency, and missing-channel behavior on close-up material scenes.

Milestone 3: make scene-local environment real.

- Split visible background, lighting environment, reflection background,
  atmosphere, and ownership mask.
- Keep sharp old-office IBL as a stress case, not a disabled case.
- Let enclosed rooms borrow IBL lighting without reflecting/showing unrelated
  panorama imagery unless the profile explicitly allows it.

Exit proof:

- Old-office IBL, kitchen, concert, and exterior water packets show different
  environment ownership and pass motion stability checks.

Milestone 4: rebuild lighting and shadows as owned resources.

- Split direct, unshadowed direct, shadow visibility, shadow loss, indirect,
  emissive indirect, and energy budget.
- Add debug views that show whether floor/wall darkening came from a named
  shadow, ambient term, exposure, or invalid fallback.
- Validate with locked exposure before any cinematic exposure or bloom.

Exit proof:

- Mouse-jitter packets can attribute dark flicker to a named term or prove it
  is absent.
- Shadow/resource source IDs remain stable during camera rotation.

Milestone 5: refactor reflections into a source-aware resolver.

- Keep SSR, RT/ray query, local probe, planar/hero probe, and environment
  fallback as separate source signals.
- Resolve with source IDs, confidence, rejection masks, history validity,
  disocclusion checks, and hysteresis.
- Do not strengthen reflection weight until source choice is stable.

Exit proof:

- Smooth/metallic object packets show stable source ID, confidence, and
  temporal delta under mouse jitter and close reflective-object orbit.

Milestone 6: give glass, water, decals, and media their own lane.

- Stop hiding transparent/media behavior inside opaque composite paths.
- Add owned glass, water, transparent accumulation, decal accumulation, and
  volumetric resources.
- Add ordering and energy diagnostics so transparency cannot double-light or
  fight reflections.

Exit proof:

- Glass/water closeups remain stable under motion and have separable
  reflection/refraction/transmission evidence.

Milestone 7: build honest HDR composition.

- Assemble `candidate_hdr_scene_color` only from V3-owned inputs.
- Emit a contribution map with material/direct/indirect/reflection/
  environment/transparency/emissive/rescue channels.
- Emit `legacy_rescue_usage` as promotion debt.

Exit proof:

- Candidate HDR remains nonblank with legacy rescue disabled or near zero on
  target packets.
- Contribution debug views explain the image.

Milestone 8: add cinematic post last.

- Implement locked/manual exposure first, then bounded auto exposure.
- Add bloom extract/resolve, glare, filmic tone map, color grade, sharpening,
  optional DOF, and bypass views.
- Bloom and glare must come from real HDR/emissive masks.

Exit proof:

- Turning off bloom/grade/DOF does not hide upstream instability.
- Candidate LDR and raw HDR both pass review packets.

Milestone 9: cross-family promotion.

- Run focused packets by subsystem first.
- Then run the full matrix:
  gallery, kitchen, office, gym, classroom, concert, red room, stadium,
  bathroom, bedroom, workshop, store, street, exterior water/vegetation.
- Required motion rows:
  static, mouse jitter, camera sweep, close-surface orbit,
  reflective-object orbit, high-contrast light sweep.

Exit proof:

- Metrics pass.
- Debug views are present and nonblank where expected.
- Contact sheets pass human review.
- The user accepts candidate visuals as good enough for promotion.

### Implementation Slices

The implementation should land as small committed slices:

1. Contract and debug registry freeze.
2. Composite contribution and legacy rescue diagnostics.
3. `SceneProfileV3` frame-report/policy completion.
4. `MaterialPayloadV3` typed payload and invalid-channel gates.
5. `SceneLocalEnvironmentV3` ownership mask/specular prefilter completion.
6. `LightingShadowV3` split resources and locked-exposure stability packets.
7. `ReflectionV3` source resolver/history/hysteresis pass.
8. `TransparencyMediaV3` glass/water/decal/media resources.
9. `CompositeV3` V3-only HDR assembly.
10. `CinematicPostV3` final candidate LDR path.
11. Cross-family promotion runner and review packet.

Each slice must update:

- shader/resource code.
- frame report ownership.
- debug view names and IDs.
- packet aliases.
- analyzer gates.
- `assets/final_art/full_scene_shader_pipeline_v3_contract.json`.
- this handoff/plan when architecture changes.

### Do Not Do

- Do not use IBL blur as correctness proof.
- Do not disable a feature and call the root issue fixed.
- Do not make default beauty depend on candidate resources before promotion.
- Do not let legacy `hdr_color` silently fill missing candidate terms.
- Do not strengthen post before raw HDR, shadows, materials, environment, and
  reflections are stable.
- Do not rely on one camera angle or one scene family.

### First Concrete Goal Feature Boundary

The next goal feature should be:

```text
FullSceneCandidateBeautyV3 scaffolding and diagnostics
  includes:
    contract freeze,
    CompositeV3 contribution outputs,
    legacy rescue usage output,
    debug views,
    packet/analyzer gates,
    candidate-only review capture
  excludes:
    default-beauty promotion,
    heavy cinematic post,
    broad visual tuning
```

That feature is the correct first step because it makes every later visual
upgrade measurable. Once candidate HDR can say exactly how much came from
material, light, shadow, reflection, environment, transparency, emissive, and
legacy rescue, we can push beauty hard without guessing.

## 2026-06-07 Full Scene Shader Master Refactor Plan

The next goal feature should be treated as a renderer architecture shift, not
as one more visual effect. The target is a candidate-only full scene shader
path that can eventually look like a high-end realtime renderer because every
visible term is owned, inspectable, stable under motion, and promoted through
evidence.

The core decision:

```text
default renderer:
  remains stable release path

candidate renderer:
  owns a new full-scene shader stack
  proves each layer with resources, reports, debug views, and packets
  becomes default only after cross-family user acceptance
```

### Visual Quality Target

The engine should move toward these image properties:

- rich PBR material response instead of flat proxy shading.
- stable glossy and metallic surfaces under mouse motion.
- local indoor/stage/exterior environment lighting without visible or
  reflected IBL leakage.
- direct, indirect, emissive, and shadow terms that can be art-directed
  without fighting hidden fallback paths.
- cinematic HDR composition with controlled exposure, bloom, tone mapping,
  color grade, and bypass views.
- final pixels that can be explained from named render resources.

This is intentionally not "make post stronger." Post is the last stage. If
materials, shadows, environment, and reflections are wrong, post is only
allowed to reveal that debt, not hide it.

### Required Refactor Shape

The candidate path should be built as a typed resource graph:

```text
SceneProfileV3
  -> scene family, enclosure, lighting rig, atmosphere profile,
     reflection policy, exposure policy

VisibilityV3
  -> depth, motion, object ID, material ID, surface ID

MaterialPayloadV3
  -> base color, world normal, roughness, metallic, specular, AO,
     emissive, opacity, transmission, clearcoat, sheen, anisotropy,
     material class, surface class, missing-channel mask

SceneLocalEnvironmentV3
  -> visible background, diffuse irradiance, specular prefilter,
     reflection background, atmosphere, ownership mask

LightingShadowV3
  -> direct light, unshadowed direct light, shadow visibility,
     shadow loss, indirect diffuse, emissive indirect, energy budget

ReflectionV3
  -> radiance, confidence, source ID, rejected-source mask,
     temporal delta, provider source signals, history validity,
     source suppression

TransparencyMediaV3
  -> water, glass, transparent accumulation, decals, volumetric
     inscatter, volumetric transmittance

CompositeV3
  -> candidate HDR, overbright mask, underlit mask, invalid energy,
     contribution/debug views, legacy rescue usage

CinematicPostV3
  -> exposure meter, bloom extract, bloom resolve, tone mapped output,
     color grade delta, final candidate LDR
```

Every arrow above must exist as a render-graph read/write edge or a declared
frame contract field. If a layer cannot be inspected in a debug view, it is not
ready to affect candidate beauty.

### Refactor Principles

1. Candidate-only until proven.
   Default beauty must not be mutated while the candidate stack is incomplete.
   This keeps release stability separate from experimental quality work.

2. No anonymous fallback.
   Legacy `hdr_color`, old IBLs, and fallback material defaults can exist only
   as named rescue/reference lanes. Their usage must be measured as promotion
   debt.

3. Source-aware visual effects.
   Reflections, shadows, environment, indirect light, water, and glass need to
   expose where their signal came from and why alternatives were rejected.

4. Stability before strength.
   Do not increase reflection weight, bloom, contrast, shadow softness, or
   color grading until the raw resource is stable under mouse jitter and camera
   sweep.

5. Scene-local by default.
   Indoor rooms, stages, gyms, and galleries need local environments. Outdoor
   panoramas may light the scene, but they must not leak as visible/reflected
   imagery unless the scene profile allows it.

6. Focused packets for iteration, full packets for promotion.
   Small packets reproduce one bug class cheaply. Full cross-family packets are
   only for promotion decisions.

### Implementation Phases

Phase 0: contract freeze.

- Freeze canonical resource names in the JSON contract, C++ frame context,
  frame report, debug view registry, packet scripts, and analyzers.
- Add candidate promotion gates for missing, stale, blank, legacy-owned, and
  uninspected resources.
- Add explicit `legacy_rescue_usage` reporting for candidate composite.

Exit gate:

- packet analyzers can fail cleanly when any required V3 layer is missing.
- default beauty is untouched.

Phase 1: SceneProfileV3.

- Add a compact scene profile object that describes enclosure, lighting intent,
  reflection policy, environment policy, post policy, and family tags.
- Use it to choose local environment, light rig, exposure constraints, and
  reflection provider priorities.

Exit gate:

- gallery, kitchen, gym, concert, red room, stadium, and exterior profiles
  produce different declared lighting/environment/reflection policies without
  changing renderer code.

Phase 2: MaterialPayloadV3 hardening.

- Move all PBR material reads behind a concrete payload boundary.
- Normalize ranges and expose missing texture/provider channels.
- Require point/pixel-exact reads for categorical IDs and classes.
- Add material closeup packets for metal, glass, water, tile, wood, plastic,
  cloth, emissive, painted wall, and skin-like/subsurface placeholders.

Exit gate:

- material payload debug views explain the final material response.
- missing-channel debt is visible and does not silently become a pretty default.

Phase 3: SceneLocalEnvironmentV3.

- Produce visible background, diffuse irradiance, specular prefilter,
  reflection background, atmosphere, and ownership mask as real resources.
- Support enclosed-room, stage, neutral lab, city/night, daylight interior, and
  outdoor profiles.
- Keep IBL blur as a tunable artistic input, not a correctness fix.

Exit gate:

- old-office IBL stress case remains enabled.
- enclosed scenes no longer sharply reflect unrelated panorama imagery.
- environment debug views explain visible, lighting, and reflection inputs.

Phase 4: LightingShadowV3.

- Rebuild direct, unshadowed, shadow visibility, shadow loss, indirect,
  emissive indirect, and energy-budget resources.
- Split shadow terms by source where practical: shadow map, contact, RT, and
  screen-space.
- Validate with locked exposure first.

Exit gate:

- floor/wall darkening can be attributed to a named shadow or lighting term.
- shadow and lighting packets pass mouse jitter and camera sweep before mood
  tuning begins.

Phase 5: ReflectionV3 source fusion.

- Keep local probe, SSR, RT/ray query, planar/hero probe, and environment
  fallback separate until the resolver admits them.
- Use confidence, history validity, source-ID hysteresis, disocclusion checks,
  and rejection counters.
- Smooth/metallic surfaces are the main acceptance case.

Exit gate:

- source IDs and confidence do not pop under mouse jitter.
- raw provider signals remain inspectable even when rejected.

Phase 6: TransparencyMediaV3.

- Route water, glass, decals, transparent objects, particles, and volumetrics
  through owned resources instead of opaque fallback paths.
- Add ordering and energy diagnostics to avoid double lighting.

Exit gate:

- glass and water closeups are stable under motion.
- transparent/media resources are visible before composite.

Phase 7: CompositeV3.

- Assemble candidate HDR from V3 material, lighting, environment, reflection,
  transparency, emissive, atmosphere, and decals.
- Keep legacy `hdr_color` only as a measured reference/rescue input.
- Emit overbright, underlit, invalid-energy, contribution, and rescue-usage
  debug views.

Exit gate:

- candidate HDR remains nonblank and explainable with legacy rescue disabled
  or near zero for the target packet.

Phase 8: CinematicPostV3.

- Implement locked/manual exposure, bounded auto exposure, bloom
  extract/resolve, glare, tone map, color grade, sharpening, vignette, optional
  DOF, and final candidate LDR.
- Provide bypass modes for raw HDR, no bloom, no grade, and final output.

Exit gate:

- post can be disabled layer by layer without hiding upstream instability.
- bloom comes from real HDR/emissive masks.

Phase 9: promotion matrix.

- Run focused packets for reflection, shadows, material, environment,
  transparency, composite, and post.
- Then run full matrix:
  gallery, kitchen, office, gym, classroom, concert, red room, stadium,
  bathroom, bedroom, workshop, store, street, exterior water/vegetation.
- Required motion rows:
  static, mouse jitter, camera sweep, close-surface orbit, reflective-object
  orbit, and high-contrast light sweep.

Exit gate:

- metrics pass.
- contact sheets pass human review.
- default and candidate comparison is documented.
- user accepts promotion.

### Near-Term Work Order

The next implementation sequence should be:

1. Finish the current `SceneLocalEnvironmentV3` producer/resource slice and
   commit it as infrastructure, not as visual promotion.
2. Add `SceneProfileV3` as the policy input that drives environment, lighting,
   reflection, and post choices.
3. Add environment-focused packets for old-office IBL, enclosed kitchen,
   concert stage, and exterior water/vegetation.
4. Convert `FullSceneCompositeV3` legacy HDR rescue into a measured
   contribution/debug lane.
5. Add candidate HDR contribution views so we can see material, direct,
   indirect, reflection, environment, transparency, emissive, and rescue terms.
6. Only then start stronger cinematic post.

This order is deliberate: it gives the renderer a brain and a resource spine
before we chase final beauty. The goal feature should complete when the
candidate path can honestly show how it made the image, where it still used
legacy rescue, and why it is stable enough to review.

## 2026-06-06 Goal Feature Refactor Decision

The next goal feature is not a single shader, post effect, or IBL setting. It
is an opt-in AAA candidate renderer path with explicit ownership from scene
facts to final pixels.

The engine is currently close enough to prove V3 resources, but not close
enough to promote beauty. The important shift is:

```text
old path:
  legacy beauty + adapters + debug captures + local visual fixes

new path:
  scene contract + owned V3 resources + source-aware composition +
  cinematic post + promotion packet
```

This matters because Unreal-style visuals come from a coherent stack:
materials, light transport, shadows, reflections, local environment, HDR
composition, and post all agreeing on the same physical inputs. A bloom pass
or a sharper reflection cannot fix the image if material ranges, shadow
ownership, reflection source selection, or environment ownership are unstable.

### Feature Boundary

Build `FullSceneCandidateBeautyV3` as a candidate-only renderer.

It must:

- consume V3 material, lighting, reflection, scene-local environment,
  transparency/media, and post resources through named render-graph edges.
- keep legacy `hdr_color` only as a comparison or explicitly reported rescue
  input.
- expose raw HDR, per-domain channels, rejection masks, energy diagnostics,
  post diagnostics, and final LDR output.
- pass packet evidence before any default-beauty switch is allowed.

It must not:

- make default beauty prettier by hiding IBL/reflection/shadow instability.
- blur or disable an effect as the proof of correctness.
- promote a feature that lacks a resource, frame-report owner, debug view,
  analyzer metric, and contact-sheet evidence.

### Target Frame Contract

The promoted candidate path should eventually look like this:

```text
VisibilityV3
  -> depth
  -> velocity
  -> object_id
  -> material_id

MaterialPayloadV3
  -> base_color
  -> normal_world
  -> roughness_metallic_specular_ao
  -> emissive_opacity
  -> transmission_clearcoat_anisotropy
  -> material_class
  -> material_missing_channel_mask

SceneLocalEnvironmentV3
  -> visible_background
  -> diffuse_irradiance
  -> specular_prefilter
  -> reflection_background
  -> local_probe_rig
  -> atmosphere_terms
  -> environment_ownership_mask

LightingShadowV3
  -> direct_lighting
  -> direct_lighting_unshadowed
  -> shadow_visibility
  -> shadow_loss
  -> indirect_diffuse
  -> emissive_indirect
  -> lighting_energy_budget

ReflectionV3
  -> reflection_radiance
  -> reflection_confidence
  -> reflection_source_id
  -> reflection_rejected_source_mask
  -> reflection_temporal_delta
  -> reflection_history_validity
  -> reflection_source_signal_ssr
  -> reflection_source_signal_rt

TransparencyMediaV3
  -> glass_radiance
  -> water_radiance
  -> transparent_accumulation
  -> volumetric_inscatter
  -> volumetric_transmittance

CompositeV3
  -> candidate_hdr_scene_color
  -> overbright_mask
  -> underlit_mask
  -> invalid_energy_mask
  -> composition_debug

CinematicPostV3
  -> exposure_meter
  -> bloom_extract
  -> bloom_resolved
  -> tone_mapped_ldr
  -> color_grade_delta
  -> candidate_ldr_cinematic_output
```

### Implementation Order For The Goal Feature

1. Contract freeze.
   - Update the JSON contract, frame context, render-graph names, debug view
     registry, packet schema, and promotion decision around the target
     `FullSceneCandidateBeautyV3` shape.
   - Add hard gates that fail candidate promotion when a required V3 resource
     is missing, stale, blank, legacy-owned, or silently rescued.

2. Reflection and smooth-surface stability.
   - Finish `ReflectionHistoryV3` source-ID hysteresis, disocclusion rejection,
     motion/depth/normal validity, and rejection counters.
   - Keep SSR/RT/local/environment source signals inspectable separately.
   - Do not increase reflection influence in candidate beauty until smooth and
     metallic stress packets prove stable source selection under motion.

3. Material payload promotion.
   - Replace ad hoc material reads with a concrete PBR payload consumed by
     lighting, reflection, composite, transparency, and post.
   - Normalize material ranges at the payload boundary.
   - Report missing channels and invalid ranges as candidate gate debt.

4. Scene-local environment ownership.
   - Split visible background from lighting and reflection environments.
   - Build texture-backed enclosed-room ambient/specular resources so indoor
     scenes can use useful IBL lighting without panorama leakage.
   - Keep old-office IBL and sharp-reflection stress cases as explicit tests.

5. Lighting and shadow rebuild.
   - Make direct, unshadowed direct, shadow visibility, shadow loss, indirect,
     emissive indirect, and lighting budget concrete resources.
   - Validate with locked exposure so auto exposure cannot hide flicker.
   - Add semantic light rigs for daylight rooms, warm interiors, gyms,
     concerts, red rooms, exteriors, and water/vegetation scenes.

6. Transparency, water, glass, decals, and media.
   - Stop forcing transparent or layered materials through the opaque
     reflection path.
   - Give water, glass, transparent accumulation, decals, and volumetrics
     separate owned resources before composite.

7. Real HDR composite.
   - Make `FullSceneCompositeV3` assemble candidate HDR from V3 resources, not
     from legacy beauty.
   - Keep legacy `hdr_color` as a named reference/rescue lane with measured
     usage.
   - Emit overbright, underlit, invalid-energy, and composition diagnostics.

8. Cinematic post.
   - Build locked/manual exposure first, then bounded auto exposure.
   - Source bloom/glare from real HDR/emissive masks.
   - Add filmic tone mapping, color grade, sharpening, optional DOF, and
     bypass views.

9. Cross-family proof.
   - Run gallery, kitchen, office, gym, classroom, concert, red room, stadium,
     bathroom, bedroom, workshop, store, street, and exterior water/vegetation.
   - Required motion rows: static, mouse jitter, camera sweep, close-surface
     orbit, reflective-object orbit, and high-contrast light sweep.
   - Produce default/candidate contact sheets, raw debug contact sheets,
     metrics JSON/MD, frame-report summaries, and promotion decision.

### First Slice After Planning

The first implementation slice should be contract and candidate-path
scaffolding, not prettier post:

```text
FullSceneCandidateBeautyV3 contract
  -> explicit target resource names
  -> candidate-only render graph ownership
  -> promotion gate rejects missing/legacy-owned resources
  -> packet output contains candidate HDR, candidate LDR, and domain evidence
```

That gives every later visual improvement a stable place to land. Once the
candidate path can honestly say which V3 terms are real and which are still
debt, we can add stronger shading without repeating the old cycle of guessing
from screenshots.

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

### CompositeV3 Reflection Confidence Slice - 2026-06-06

Status:

- `FullSceneCompositeV3` now consumes `reflection_confidence` with
  `reflection_radiance`.
- candidate HDR reflection weighting now follows the ReflectionV3 resolver's
  actual source confidence instead of a luma-derived estimate.
- runtime readiness and analyzers require this edge before treating CompositeV3
  as the real candidate HDR producer.

Evidence:

- `build/captures/v3_composite_reflection_confidence_static_fullviews_20260606`.
- packet status `review_packet_passed`, with default beauty still not
  promotable.
- frame report shows `composite_v3_producer=FullSceneCompositeV3`,
  `composite_v3_ready=true`, and `reflection_confidence_ready=true`.

Next:

- Add explicit candidate HDR energy/overbright diagnostics.
- Reduce or report legacy `hdr_color` rescue usage so CompositeV3 becomes less
  adapter-like and more self-owned.

## 2026-06-07 Full Scene Shader Refactor Before Goal Feature Completion

This is the plan boundary before completing the goal feature. The target is not
one more reflection patch or a nicer IBL preset. The target is a full scene
shader architecture that can produce high-end real-time visuals because every
major term in the final image is owned, inspectable, stable under motion, and
admitted through a promotion gate.

### Outcome

Build an opt-in `FullSceneCandidateBeautyV3` path with this invariant:

```text
candidate pixel =
  material payload
  + scene-local environment
  + direct lighting
  + shadow visibility
  + indirect/emissive lighting
  + source-aware reflections
  + transparent/media terms
  + HDR energy policy
  + cinematic post
```

No part of that equation is allowed to be an anonymous legacy rescue unless it
is explicitly named, measured, shown in debug views, and treated as debt.

### Why This Refactor Is Needed

The current renderer can produce useful isolated signals, but the final image
is still partly assembled through adapters and legacy fallback paths. That makes
it too easy to hide the real problem:

- IBL blur can hide reflection instability without fixing source selection.
- Post effects can hide bad material ranges or flat lighting.
- Legacy HDR rescue can make candidate beauty look acceptable while V3 domains
  are incomplete.
- A single scene packet can pass while kitchen, gym, concert, glass, water, or
  metallic stress cases still fail under motion.

The refactor must therefore turn renderer quality into a set of contracts:
resource ownership, visual diagnostics, motion stability, cross-family proof,
and promotion decisions.

### Refactor Layers

Layer 1: frame contract and render graph ownership.

- Define the canonical V3 resource names in the JSON contract and C++ frame
  context.
- Every V3 producer writes a named render target or buffer.
- Every V3 consumer records its read edges.
- Runtime frame reports must say which producer owned each resource, whether it
  executed, and whether any legacy rescue was used.
- Candidate promotion fails if any required resource is missing, stale, blank,
  or silently substituted.

Layer 2: material payload.

- Consolidate PBR channels into a concrete payload consumed by lighting,
  reflection, composite, transparency, and post.
- Required channels: base color, world normal, roughness, metallic, specular,
  AO, emissive, opacity, transmission, clearcoat, sheen, anisotropy, IOR,
  thickness, material class, surface class, missing-channel mask, and fallback
  policy.
- Texture/provider missing cases must be visible as payload debt instead of
  hidden material defaults.
- Categorical material IDs/classes must use point or pixel-exact reads.

Layer 3: scene-local environment.

- Split visible background, diffuse irradiance, specular prefilter, reflection
  background, local probe rig, atmosphere, and environment ownership mask.
- Enclosed scenes must not visibly show or sharply reflect unrelated external
  IBLs.
- Reflection/background blur is an artistic parameter, not a bug fix.
- Old-office IBL remains a stress case for reflection stability.

Layer 4: lighting and shadows.

- Lighting V3 owns direct light, unshadowed direct light, shadow visibility,
  shadow loss, indirect diffuse, emissive indirect, and energy-budget views.
- Shadow maps, contact shadows, RT shadows, and any screen-space shadow terms
  need separate debug views and stability gates.
- Locked exposure packets are required before auto exposure can be trusted.
- The refactor should support semantic rigs for interiors, gyms, concert halls,
  red rooms, stadiums, streets, and exterior water/vegetation.

Layer 5: reflection source fusion.

- ReflectionV3 chooses between local probes, SSR, RT/ray query, planar/hero
  probes, and scene-local environment fallback with source IDs and confidence.
- The resolver must expose rejected-source masks, temporal delta, source
  suppression, source signal per provider, history validity, and history
  rejection.
- Smooth and metallic surfaces are the primary stress tests; they must not
  jitter or pop under mouse movement.
- Remaining current blocker: `reflection_history_v3_validity` and
  `reflection_history_v3_rejection` still move too much in forced-SSR mouse
  jitter packets.

Layer 6: transparency, water, glass, decals, and media.

- Transparent and layered materials cannot be squeezed through the opaque
  reflection path.
- Glass, water, decals, transparent accumulation, volumetric inscatter, and
  volumetric transmittance need owned resources and debug modes.
- Water/glass material gates must read the owning pass payload, not only the
  opaque G-buffer center sample.

Layer 7: HDR composite.

- `FullSceneCompositeV3` assembles candidate HDR from owned V3 terms.
- Legacy `hdr_color` can remain only as a named comparison/rescue lane with
  measured usage.
- Composite diagnostics must include overbright, underlit, invalid energy,
  reflection contribution, indirect contribution, legacy rescue, and final HDR
  before post.

Layer 8: cinematic post.

- Start with locked/manual exposure, then bounded auto exposure.
- Bloom/glare must be sourced from real HDR/emissive masks.
- Tone mapping, color grading, sharpening, optional DOF, and final LDR output
  must have bypass/debug views.
- Post is not allowed to hide upstream instability; packet gates compare raw
  HDR, post bypass, and final LDR.

Layer 9: proof harness.

- Full packets are too large for rapid iteration on the current disk budget.
  Add focused packet runners for reflection, shadows, material payload, and
  post so individual instability can be reproduced without 300 MB captures.
- Full packets still remain the promotion gate.
- Required motion modes: static, mouse jitter, camera sweep, close-surface
  orbit, reflective-object orbit, and high-contrast light sweep.
- Required families: gallery, kitchen, office, gym, classroom, concert,
  red room, stadium, bathroom, bedroom, workshop, store, street, and exterior
  water/vegetation.

### Implementation Sequence

1. Contract freeze and candidate shell.
   - Update contract/resource schema around the final V3 resource list.
   - Ensure candidate HDR/LDR, domain diagnostics, and promotion decisions can
     fail honestly when resources are missing.

2. Focused diagnostic harness.
   - Add small reflection/shadow/material/post packet runners.
   - Keep full packets for promotion only.
   - This avoids disk failures and makes root-cause debugging faster.

3. Reflection history stability.
   - Fix the remaining forced-SSR mouse-jitter warnings in
     `reflection_history_v3_validity` and `reflection_history_v3_rejection`.
   - Add history source-ID hysteresis and explicit disocclusion/source-switch
     counters before increasing reflection influence.

4. Material payload hardening.
   - Replace aggregate/ad hoc material reads with a named payload boundary.
   - Make missing texture channels, fallback presets, and material class
     disagreements fail the material gate.

5. Scene-local environment split.
   - Create texture-backed indoor/local diffuse and specular environment
     resources.
   - Keep visible background independent from reflection and lighting inputs.

6. Lighting/shadow rebuild.
   - Make shadow and indirect terms first-class resources.
   - Add stable contact/soft shadow gates before tuning final mood.

7. Reflection provider expansion.
   - Add RT/ray-query and optional planar/hero probe source signals behind the
     same resolver contract.
   - The resolver admits providers by confidence and stability, not by global
     toggles.

8. Transparency/media integration.
   - Add water, glass, decals, and volumetric resources before composite.
   - Validate with closeups and motion rows.

9. Real CompositeV3 and CinematicPostV3.
   - Remove or sharply reduce legacy HDR rescue.
   - Build post from real candidate HDR and measured emissive/highlight masks.

10. Cross-family promotion matrix.
    - Run focused packets for root causes, then full V3 packets for release
      promotion.
    - Promotion requires metrics, contact sheets, frame reports, and a human
      review packet. Do not declare the goal solved from one screenshot.

### First Concrete Slice

The first implementation slice after this plan should be harness-first:

```text
focused reflection motion packet
  -> forced SSR / old-office IBL / smooth-metal closeup
  -> only reflection and beauty debug views
  -> history validity/rejection report
  -> small contact sheet
  -> no default scene or IBL workaround
```

This directly attacks the remaining smooth/metal jitter issue and also creates
the pattern for focused shadow, material, and post packets. Once the focused
harness can reproduce the issue cheaply, the shader fix can be validated
without filling the disk or conflating it with unrelated scene quality.

### Focused Reflection Motion Harness - 2026-06-07

Implemented the first harness slice:

- `tools/run_reflection_v3_motion_focus_packet.ps1`.
- default stress target: `rt_showcase:reflection_closeup`.
- default motion: `mouse_jitter`.
- default source override: forced `ssr`.
- default view set:
  `beauty`, `reflection_radiance`, `reflection_confidence`,
  `reflection_source_id`, `reflection_rejected_source_mask`,
  `reflection_temporal_delta`, `reflection_ssr_source_signal`,
  `reflection_rt_source_signal`, `reflection_source_suppression`,
  `reflection_history_v3_curr`, `reflection_history_v3_prev`,
  `reflection_history_v3_validity`, and
  `reflection_history_v3_rejection`.
- output artifacts:
  `manifest.json`, `v3_reflection_motion_focus.json`,
  `v3_reflection_motion_focus.md`, and
  `v3_reflection_motion_focus_sheet.png`.

Analyzer update:

- `tools/analyze_full_scene_shader_v3_lighting_motion.py` now supports
  `--focus reflection`.
- Focused reflection mode gates only the reflection diagnostics plus beauty
  baseline. It no longer pollutes narrow packets with missing V3 lighting or
  unrelated composite-view warnings.

Usage:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_reflection_motion_focus_manual
```

This is a harness and root-cause tool. It is not a promotion packet. Full V3
packets remain required before any candidate beauty promotion.

Follow-up result:

- The focused packet reproduced the two remaining reflection-history warnings
  in `build/captures/v3_reflection_motion_focus_forced_ssr_mouse_jitter_20260607`.
- `FullSceneReflectionHistoryV3.hlsl` was changed to use confidence-weighted
  continuous history diagnostics.
- The focused after packet
  `build/captures/v3_reflection_history_confidence_weighted_focus_20260607`
  passed with `0` warnings and `0` failures.
- The full stress packet
  `build/captures/v3_reflection_history_confidence_weighted_full_20260607`
  passed V2 evidence, V3 packet checks, V3 lighting motion, material payload,
  CompositeV3 diagnostics, and review-packet promotion decision.
- Default beauty is still not promotable; the passing packet is stress-only,
  not the required cross-family and cross-motion promotion matrix.

### Material Payload Contract Coverage - 2026-06-07

Follow-up material hardening:

- Standard V3 packets now capture `material_base_color` and
  `material_normal` alongside roughness, metallic, surface class, and policy
  views.
- The V3 material payload analyzer now reports contract-required material
  debug-view coverage against
  `assets/final_art/full_scene_shader_pipeline_v3_contract.json`.
- The first contract coverage packet
  `build/captures/v3_material_payload_contract_views_stress_20260607`
  passed V3 placeholder checks, V3 lighting motion, V3 material payload,
  CompositeV3 diagnostics, and review-packet promotion decision.
- Contract coverage is now explicit:
  `material_base_color`, `material_roughness`, `material_metallic`,
  `material_normal`, and `material_class` are covered; the remaining material
  payload debt is `material_missing_channel_mask`.

Next material payload slice:

- Create a real missing-channel-mask resource/debug view or a stricter
  equivalent frame-contract gate so missing texture/channel ownership is
  visible before lighting, reflections, composite, or post consume materials.

### SceneProfileV3 Policy Evidence - 2026-06-07

Implemented refactor slice:

- `SceneProfileV3` is now a first-class V3 evidence domain adapted from the
  existing `SceneCinematicProfile` / `scene_visual_contract`.
- The frame contract serializes `scene_profile_ready`,
  `scene_profile_policy_count`, `scene_profile_producer`, and
  `scene_profile_output`.
- The V3 contract requires the `scene_profile` domain, `v3_scene_profile.json`,
  `scene_profile_policy_ready`, and
  `scene_profile_family_differences_present`.
- New analyzer: `tools/analyze_full_scene_shader_v3_scene_profile.py`.
- New narrow packet harness: `tools/run_scene_profile_v3_focus_packet.ps1`.
- The standard V3 packet runner and promotion-decision builder now include
  scene-profile evidence.

Validated evidence:

- Static plan validation passed with `10` V3 domains and `29` required outputs.
- Native target rebuilt successfully.
- Cross-family scene-profile proof:
  `build/captures/scene_profile_v3_focus_2fam_beauty_20260607`.
  Manual analyzer result:
  `families=2`, `profiles=2`, `light rigs=2`,
  `material palettes=2`, `failures=0`, `warnings=0`.
- Integrated stress packet:
  `build/captures/v3_scene_profile_full_stress_20260607`.
  Scene-profile analyzer passed with `54` reports and no failures/warnings.

Important boundary:

- `SceneProfileV3` is not enough by itself to promote default beauty. The
  current promotion decision remains blocked because CompositeV3 and
  CinematicPostV3 readiness are only present on the six candidate
  composite/post views, while the full-pipeline report set has `41` reports.
- Next refactor step is to make composite/post readiness coverage coherent
  across full-pipeline debug views, then continue into texture-backed
  `SceneLocalEnvironmentV3`.

Follow-up correction:

- CompositeV3 and CinematicPostV3 are candidate-beauty domains, not mandatory
  upstream diagnostic domains.
- The contract now records `readiness_scope: candidate_beauty_requested` for
  both domains.
- Promotion now requires:
  - base domains for every full-pipeline row.
  - composite/post domains only where `candidate_beauty_requested=true`.
- Re-running promotion on
  `build/captures/v3_scene_profile_full_stress_20260607` passed as a review
  packet:
  `scene_profile=41/41`, material `54/54`,
  lighting/environment/reflection `41/41`,
  composite/cinematic-post `6/6` candidate rows, candidate beauty `6/6`.
- Default beauty remains non-promotable until cross-family and motion evidence
  exists.

Integrated rerun:

- Deleted old generated `20260604` capture directories under `build/captures`
  after path verification, increasing free space to about `33 GB`.
- The fresh integrated packet
  `build/captures/v3_candidate_scope_full_stress_20260607` passed end to end:
  V2 evidence, V3 placeholder artifacts, scene profile, material payload,
  CompositeV3 diagnostics, and review-packet promotion decision.
- More rendered sweeps are now possible, but capture size still needs active
  monitoring.
