# Full Scene Shader AAA Refactor Plan

Status: planning ledger.

Default beauty stays unchanged until a separate promotion gate passes.

## 2026-06-07 Whole-Renderer Refactor Blueprint

This is the implementation map for moving CortexEngine from current debug-heavy
scene-local experiments toward an opt-in AAA full-scene shader path. The goal
is not to make one scene look better through a preset. The goal is to make the
renderer own every visible term in a way that can be inspected, measured,
debugged, and promoted.

### North Star

Build `FullSceneShaderV3` as a candidate renderer with Unreal-style visual
ingredients:

- stable geometry/visibility buffers
- typed PBR material payloads
- scene-local ambient/specular/background resources
- source-attributed direct and indirect lighting
- stable contact, cascade, and screen/RT shadow behavior
- reflection provider fusion for SSR, local probes, planar/hero probes, RT, and
  environment fallback
- glass, water, decals, particles, and atmosphere as explicit media domains
- HDR composition with contribution accounting
- cinematic post only after upstream HDR is owned

Public/default rendering stays conservative until this candidate path passes
promotion evidence. Any feature that cannot explain its contribution in a debug
view remains prototype-only.

### Refactor Layers

The refactor should land in layers. Each layer has a product purpose and an
engineering contract.

| Layer | Purpose | Required Contract |
|---|---|---|
| `SceneProfileV3` | Decide what kind of visual world this scene is allowed to use. | family, enclosure, IBL visibility permission, lighting intent, reflection intent, media intent, post intent |
| `RenderGraphV3` | Make the candidate frame deterministic and inspectable. | named passes, typed resources, pass dependency report, per-pass debug aliases |
| `VisibilityV3` | Stop camera/mouse instability from poisoning every effect. | depth, normal, velocity, object id, material id, motion confidence, disocclusion |
| `MaterialPayloadV3` | Turn arbitrary scene assets into physically meaningful channels. | base color, normal, roughness, metallic, AO, emissive, opacity, transmission, invalid/missing masks |
| `SceneLocalEnvironmentV3` | Replace inappropriate visible/sharp external IBL in enclosed scenes. | local background, irradiance, specular prefilter, reflection background, room-shell occlusion, provenance |
| `LightingShadowV3` | Make shadow and lighting artifacts attributable. | light source id, direct/unshadowed direct, indirect, shadow visibility, shadow loss, filter radius, energy budget |
| `ReflectionV3` | Stop smooth/metallic jitter through provider confidence, not blur. | SSR, probe, planar/hero, RT, environment fallback, source id, confidence, rejected source, history validity |
| `TransparencyMediaV3` | Add expensive beauty terms without hiding opaque bugs. | ordered glass/water/decals/particles/fog, transmittance, composition masks, media debt |
| `CompositeV3` | Own candidate HDR instead of rescuing legacy color. | contribution map, clamp debt, legacy-rescue debt, overbright/underlit masks |
| `CinematicPostV3` | Add final AAA polish. | exposure, tonemap, bloom/glare, grade, sharpen, vignette, optional DOF, bypass views |
| `PromotionV3` | Decide when the candidate can become default. | packet matrix, analyzers, thresholds, failure reports, user review |

### Target Runtime Shape

The candidate frame should be built as typed producers, not scattered feature
booleans:

```text
FrameStart
  BuildSceneProfileV3()
  BuildVisibilityV3()
  BuildMaterialPayloadV3()
  BuildSceneLocalEnvironmentV3()
  BuildLightingShadowV3()
  BuildReflectionV3()
  BuildTransparencyMediaV3()
  BuildCompositeV3()
  BuildCinematicPostV3()
  BuildPromotionV3Reports()
FrameEnd
```

Every producer must emit:

```text
policy inputs
resources written
shader contribution
debug views
frame contract fields
JSON report fields
analyzer checks
packet evidence
promotion state
```

If a producer only changes pixels but does not emit that chain, it is not a
candidate-renderer feature yet.

### Data Ownership Rules

The current renderer has too much implicit ownership. The refactor should make
these rules explicit:

1. The scene profile owns what environment content may be visible or reflected.
   Enclosed scenes can use IBL for lighting only if the visible/reflection
   background is scene-local or explicitly allowed.
2. Material payloads own all PBR channel synthesis. Lighting and reflection
   shaders may consume missing-channel debt, but they may not silently invent
   unrelated material behavior.
3. Lighting owns shadows and shadow loss. A dark/light flicker must be reported
   as sun, local light, contact shadow, RT shadow, screen-space shadow,
   environment/exposure, or unknown debt.
4. Reflections own provider selection. Smooth/metallic jitter is a source
   resolver failure until proven otherwise.
5. Composite owns final HDR contribution balance. Legacy rescue is allowed only
   as measured debt.
6. Post owns presentation only. It cannot be used as evidence that lighting,
   reflection, material, or environment problems were fixed.

### Implementation Phases

Phase A: Contract Unification

- Normalize V3 names across C++, HLSL, JSON, packet scripts, analyzers, and
  docs.
- Make `FrameContract` the authoritative cross-domain report structure.
- Add a `RenderGraphV3` pass/resource report so captures can prove which domain
  wrote each debug view.
- Stop adding new visual switches until their report/analyzer route exists.

Exit gate:

- Static validator fails if a V3 domain has shader code without contract fields,
  report fields, and analyzer coverage.

Phase B: Visibility And Material Base

- Build stable visibility buffers once per frame.
- Add object/material/surface id debug views.
- Refactor material extraction into a typed payload producer.
- Mark missing normals, roughness, metallic, AO, emissive, opacity, and
  transmission explicitly.

Exit gate:

- Cross-family packet proves material payload presence and missing-channel debt
  for enclosed rooms, gallery, gym, concert, stadium, and exterior/water.

Phase C: Real Scene-Local Environment

- Replace proxy-only local environment work with resource-backed local
  background, irradiance, and specular prefilter.
- Keep external IBL as lighting input only when profile policy allows it.
- Add local reflection background separate from visible background.
- Report resource shape, filter method, variance, source count, room occlusion,
  light-rig influence, and fallback reason.

Exit gate:

- Enclosed scenes no longer show or sharply reflect unauthorized external IBL
  content, and analyzers can prove which environment resource produced each
  term.

Phase D: Lighting And Shadow Stability

- Split lighting into direct, unshadowed direct, indirect, shadow visibility,
  shadow loss, and energy budget resources.
- Add source ids for sun, area, point, emissive, environment, and fallback.
- Add close-surface and mouse-jiggle shadow stress packets.
- Tie remaining flicker to a named source before changing filters or quality.

Exit gate:

- Old office floor/wall style repros and scene-local rooms have no unexplained
  dark/light flicker. Any remaining issue has a source-specific failure report.

Phase E: Reflection Provider Fusion

- Keep forced SSR/local/RT/environment modes as diagnostics.
- Promote only auto resolver output.
- Add SSR continuity/hole metrics, local probe confidence, planar/hero probe
  support for known smooth surfaces, RT/ray-query confidence where available,
  history validity, material suppression, and source hysteresis.
- Separate reflection background from lighting environment.

Exit gate:

- Smooth and metallic object motion packets show low source churn in auto mode,
  and forced-provider instability is diagnosed rather than hidden.

Phase F: Transparency And Media

- Add glass and water after opaque lighting/reflection are inspectable.
- Add decals, particles, fog, and atmosphere as explicit resources with ordering
  diagnostics.
- Do not allow media/post to cover opaque instability.

Exit gate:

- Glass/water/decals/fog have bypass views and composition masks, and disabling
  them leaves opaque HDR coherent.

Phase G: Composite And Cinematic Post

- Make `CompositeV3` assemble the candidate HDR from V3 resources.
- Add contribution map, clamp debt, overbright/underlit masks, and legacy rescue
  debt.
- Add `CinematicPostV3` only after candidate HDR is stable.
- Tune exposure, tonemap, bloom, glare, grade, sharpening, vignette, and
  optional DOF with bypass/debug outputs.

Exit gate:

- Candidate HDR can stand without heavy post, and post improves presentation
  without hiding upstream debt.

Phase H: Promotion

- Run a full packet matrix:
  - gallery/showcase
  - kitchen
  - office
  - gym
  - classroom
  - concert
  - red room
  - stadium
  - exterior water/vegetation
  - rain/glass stress scene
- For each family, capture static, mouse-jiggle, camera sweep, close-surface,
  reflective-orbit, and high-contrast-light motion.
- Produce contact sheets, JSON reports, markdown summaries, failure packets, and
  a final promotion decision.

Exit gate:

- Candidate path is promoted only when analyzer evidence passes and human review
  accepts the beauty result.

### First Implementation Slice After This Plan

The next coding slice should not attempt all phases. It should create the
foundation that makes later visual work faster:

1. Finish the current `SceneLocalEnvironmentV3` runtime reporting slice so
   filtered proxy shape/variance are visible in frame reports and analyzers.
   This slice is now implemented at the contract/source level: generated proxy
   records carry resource shape, filtered output count, and minimum filter
   variance through runtime binding, frame contract, V3 context, JSON, analyzer,
   and static validator paths. Focused and cross-profile packets validated that
   runtime reports expose `filtered_directional_bc1_v1`, three filtered proxy
   outputs, non-flat minimum variance, and zero analyzer failures.
2. Add `RenderGraphV3` pass/resource inventory to the frame contract.
   This is now implemented as a V3 runtime inventory derived from existing
   `FrameContract::PassRecord` entries. Reports expose V3 pass counts, executed
   pass counts, read/write resource counts, missing producer counts, pass names,
   read resources, written resources, and missing producer resources. The V3
   `render_graph` domain now reports `RenderGraphV3Inventory` instead of a
   planned empty placeholder. The first follow-up producer slice records
   `LocalReflectionRadiance` as the runtime writer for
   `local_reflection_radiance`, removing the render-graph missing-producer debt
   for ReflectionV3.
3. Add a static validator rule that every V3 shader/debug view has an owning
   domain, contract field, and analyzer mention.
4. Add an explicit "candidate path debt" section to frame JSON:
   material debt, environment debt, lighting debt, reflection debt, composite
   debt, and post debt.
   This is now implemented as `candidate_path_debt` in the V3 runtime report.
   It summarizes missing required channels per candidate domain, render-graph
   missing producer resources, total missing required channels, not-ready domain
   count, and whether legacy rescue is still a live resource.
5. Run one focused packet and one cross-family report-only packet.

This gives us the harness needed to make later shader work less hand-wavy.

### Completion Boundary

The goal feature is not complete when a screenshot looks good. It is complete
only when:

- `FullSceneShaderV3` is opt-in and stable
- V3 owns candidate HDR without normal reliance on legacy rescue
- debug views and reports explain material, environment, lighting, reflection,
  transparency/media, composite, and post contributions
- cross-family and motion packets pass
- the user accepts the beauty packet as artistically good enough

## 2026-06-07 Full Scene Shader Goal Feature Execution Architecture

This is the current refactor plan to use before implementing the goal feature.
The target is an opt-in `FullSceneShaderV3` candidate renderer that can become
the default only after evidence and user review. The target is not a stronger
post-process pass, a blurrier environment map, or a scene-specific workaround.

### Final Shape

The renderer should be split into stable domain producers that write typed
resources and diagnostics:

```text
SceneProfileV3
  -> VisibilityV3
  -> MaterialPayloadV3
  -> SceneLocalEnvironmentV3
  -> LightingShadowV3
  -> ReflectionV3
  -> TransparencyMediaV3
  -> CompositeV3
  -> CinematicPostV3
  -> PromotionV3
```

Every domain must have the same engineering contract:

```text
policy input
  -> producer/resource
  -> shader contribution
  -> debug view
  -> frame report fields
  -> analyzer
  -> packet evidence
```

If a domain lacks one of these links, it stays prototype debt even when the
beauty screenshot looks good.

### Data Model Refactor

The current visual stack should move away from scattered booleans and implicit
fallbacks. The candidate path needs explicit renderer data objects:

- `SceneProfileV3Policy`: family, enclosure, environment permissions, light
  rig, reflection priority, transparency/media needs, exposure/post look, and
  promotion family id.
- `VisibilityV3Buffers`: depth, velocity, normals, material id, object id,
  surface id, disocclusion, motion confidence, and close-surface flags.
- `MaterialPayloadV3Surface`: base color, normal, roughness, metallic,
  specular, AO, emissive, opacity, transmission, clearcoat, sheen,
  anisotropy, IOR, thickness, class id, missing mask, and invalid mask.
- `SceneLocalEnvironmentV3Resources`: visible background, diffuse irradiance,
  specular prefilter, reflection background, atmosphere parameters, ownership
  mask, payload provenance, and fallback reason.
- `LightingShadowV3Resources`: direct, unshadowed direct, indirect, shadow
  visibility, shadow loss, source attribution, filter data, energy budget, and
  light-motion diagnostics.
- `ReflectionV3Resources`: SSR, RT/ray-query, local probe, hero/planar probe,
  environment fallback, source id, confidence, rejected-source mask, history
  validity, temporal delta, and source hysteresis.
- `TransparencyMediaV3Resources`: glass, water, decals, particles,
  volumetric inscatter, transmittance, ordering diagnostics, and composition
  masks.
- `CompositeV3Resources`: candidate HDR, contribution map, clamp debt,
  legacy-rescue debt, overbright/underlit masks, and final pre-post audit.
- `CinematicPostV3State`: exposure meter/state, bloom, glare, tonemap, color
  grade, sharpen, vignette, optional DOF, and bypass outputs.

### Render Graph Refactor

The render graph should stop treating the candidate path as a set of loose
debug passes. It should have a predictable frame structure:

1. `BuildSceneProfileV3`.
   Resolve scene family policy and immutable per-frame visual intent.

2. `BuildVisibilityV3`.
   Produce stable geometry buffers once, then feed every temporal or
   screen-space effect from those buffers.

3. `BuildMaterialPayloadV3`.
   Normalize material payloads into typed PBR channels. Synthesize missing
   channels only through explicit fallback policy and report that debt.

4. `BuildSceneLocalEnvironmentV3`.
   Bind or generate local visible background, irradiance, specular prefilter,
   and reflection background. Enclosed scenes must not depend on visible or
   sharp external IBL content unless the profile allows it.

5. `BuildLightingShadowV3`.
   Produce source-attributed lighting and shadow resources. Shadow instability
   must report whether it came from sun, local lights, contact/RT,
   screen-space paths, filter radius, environment, or exposure.

6. `BuildReflectionV3`.
   Produce all candidate providers separately, then resolve them with
   confidence, history validity, material policy, and hysteresis. Smooth or
   metallic jitter is fixed here.

7. `BuildTransparencyMediaV3`.
   Add water, glass, decals, fog, particles, and volumetrics after opaque
   lighting/reflection ownership is inspectable.

8. `BuildCompositeV3`.
   Assemble candidate HDR from V3 terms. Legacy HDR rescue remains measured and
   should trend toward zero before default promotion.

9. `BuildCinematicPostV3`.
   Apply exposure, bloom, glare, tonemap, grade, sharpen, vignette, and optional
   DOF. Every strong post term has a bypass/debug view.

10. `BuildPromotionV3Reports`.
    Emit frame report fields and packet evidence for every enabled domain.

### Shader Refactor

Each V3 shader should avoid hidden cross-domain assumptions. The target
organization is:

- shared contracts in small HLSL headers for profile, material, visibility,
  environment, lighting, reflection, transparency, composite, and post
- one owning shader/pass per domain output, with helper functions allowed only
  when they do not hide fallback decisions
- debug modes that map directly to resource ownership, source id, confidence,
  history, rejection, debt, and contribution
- no visual fix is accepted unless the relevant debug mode and frame report
  move in a coherent way

The immediate shader risk areas are:

- environment resources still use derived proxy assets rather than real
  filtered local radiance
- reflection source fusion still needs richer local probe or RT fallback proof
- transparency/media should wait until opaque reflection and lighting are
  stable enough to separate artifact sources
- post should stay restrained until candidate HDR is real

### Validation Refactor

The packet system becomes the promotion harness, not a screenshot generator.
Each domain gets three evidence levels:

1. Focus packet.
   A narrow repro scene or stress view that makes the domain defect obvious.

2. Cross-family packet.
   Gallery, kitchen, office, gym, classroom, concert, red room, stadium,
   bathroom, bedroom, workshop, store, street, and exterior water/vegetation.

3. Motion matrix.
   Static, mouse jitter, camera sweep, close-surface orbit, reflective-object
   orbit, high-contrast light sweep, and optional camera cut.

Default promotion is blocked when:

- candidate HDR normally relies on legacy rescue
- enclosed scenes show or sharply reflect unauthorized external IBL content
- floor, wall, shadow, smooth, or metallic flicker lacks source attribution
- material payload channels are missing without downstream debt reporting
- local environment resources are scalar approximations where resource-backed
  radiance is required
- post is hiding upstream instability
- cross-family reports are missing because a capture crashed before diagnostics
- human review rejects the beauty packet

### Execution Phases

Phase 0, lock the baseline.
Keep current public beauty stable and keep all aggressive work behind candidate
flags, debug views, and packet gates.

Phase 1, unify contracts.
Make C++ frame context, JSON contracts, HLSL bindings, debug aliases, analyzers,
packet scripts, and promotion decisions speak the same V3 domain vocabulary.

Phase 2, finish material ownership.
Turn every material input into typed payload, explicit synthesis, or explicit
missing debt. Lighting, reflections, transparency, composite, and post must
consume and report this debt.

Phase 3, make scene-local environment real.
Move from payload aliases and generated flat proxies to decoded material-color
sampling, room-shell influence, light-rig influence, filtered diffuse
irradiance, filtered specular prefilter, and visible local background.

Phase 4, finish shadow attribution and stability.
Keep the old-office/floor/wall repro as a hard stress case, but diagnose it
through `LightingShadowV3` source channels rather than hiding it through IBL or
scene changes.

Phase 5, finish reflection source fusion.
Keep forced providers as diagnostics, but promote only the auto resolver.
Strengthen SSR continuity, local probe fallback, RT/ray-query fallback, history
validity, material suppression, and source hysteresis until smooth/metallic
jitter has a named fix.

Phase 6, add transparency and media.
Introduce glass, water, decals, fog, particles, and volumetric terms only after
opaque resources can prove whether artifacts come from lighting, reflection,
environment, or ordering.

Phase 7, own candidate HDR.
Make `CompositeV3` the normal beauty source. Legacy rescue is allowed for
diagnostics and emergency fallback, but promotion requires it to be near zero.

Phase 8, add cinematic post.
Add filmic exposure, bloom, glare, tone mapping, grade, sharpen, vignette, and
optional DOF after candidate HDR is stable and inspectable.

Phase 9, run promotion.
Run the full cross-family and motion matrix, produce contact sheets and failure
reports, and use human review as the final gate.

### Near-Term Work Order

The next implementation should start with the highest-leverage upstream terms:

1. Upgrade `SceneLocalEnvironmentV3` proxy generation from filename inventory
   to decoded material-color sampling plus room-shell/light-rig influence.
2. Add frame-report and analyzer fields that prove local irradiance, local
   specular, and visible background are generated from the same scene-local
   contract.
3. Add cross-family report/capture separation so a model-scene crash does not
   erase diagnostics.
4. Expand `ReflectionV3` provider evidence from gallery/office into a bounded
   family matrix and add pixel-level SSR continuity if needed.
5. Continue `LightingShadowV3` light-sweep and close-surface stress until
   remaining flicker has a named source.
6. Only after those pass, implement `TransparencyMediaV3` and
   `CinematicPostV3`.

### Non-Goals For This Refactor

- Do not make IBL blur, disabled reflections, lowered sharpness, or scene swaps
  count as fixes.
- Do not promote a single scene or a single screenshot.
- Do not build post-processing before candidate HDR ownership is real.
- Do not wire new visual terms without debug views, frame reports, analyzers,
  and packet evidence.
- Do not mix scene-authoring quality work into this renderer stabilization and
  AAA shader goal.

## 2026-06-07 Goal Feature Refactor Plan - Full Scene Shader Renderer

This is the short authoritative plan for the requested goal feature. The
feature is not a single visual preset. It is a refactored candidate renderer
that can eventually produce high-end full-scene visuals because each visible
term is owned, inspectable, and validated before it reaches the final frame.

### Product Goal

Build an opt-in `FullSceneShaderV3` candidate beauty path that can render
interior rooms, stage scenes, stadiums, galleries, and exterior water or
vegetation scenes with the kind of visual depth expected from modern realtime
engines:

- local environment lighting that matches the scene instead of leaking random
  IBL content into enclosed rooms
- physically meaningful material response from typed PBR payloads
- stable shadows and reflections during mouse movement, camera orbit, and
  high-contrast light motion
- believable glass, water, decals, particles, fog, and atmosphere
- final cinematic post that enhances a valid HDR frame instead of hiding
  upstream instability

The existing public beauty path remains the stable baseline. The candidate path
can be aggressive, but it does not become the default until the promotion matrix
and human review pass.

### Why This Refactor Is Required

The current renderer has useful pieces, but they are still too easy to tune in
isolation. That creates the failure mode we have been fighting: a scene can look
better because an IBL was blurred, a reflection was softened, a background was
swapped, or a post pass hid a defect. That is not robust.

The V3 refactor makes the renderer harder to fool. Every visible contribution
must have this chain:

```text
SceneProfileV3 policy
  -> typed producer/resource
  -> shader pass contribution
  -> debug view
  -> frame-report fields
  -> analyzer gate
  -> packet/promotion evidence
```

If any link is missing, the contribution is still prototype debt, even if a
screenshot looks good.

### Renderer Stack To Build

The final candidate renderer should be structured as these domains:

1. `SceneProfileV3`
   Owns scene family, enclosure mode, light rig, reflection policy, environment
   policy, material expectations, exposure policy, post look, motion tolerance,
   and promotion family metadata.

2. `VisibilityV3`
   Owns depth, velocity, stable normals, object id, material id, surface id,
   disocclusion, motion confidence, and close-surface flags. This becomes the
   stability input for temporal reflection, shadows, decals, transparency, and
   post.

3. `MaterialPayloadV3`
   Owns base color, normal, roughness, metallic, specular, AO, emissive,
   opacity, transmission, clearcoat, sheen, anisotropy, IOR, thickness, material
   class, missing-channel mask, and invalid-range mask. Missing or synthesized
   material channels must be reported and consumed intentionally downstream.

4. `SceneLocalEnvironmentV3`
   Owns local visible background, diffuse irradiance, specular prefilter,
   reflection background, atmosphere parameters, and ownership masks. Enclosed
   rooms and stages should use local room/stage radiance. Gallery and exterior
   scenes may use HDRI only when the scene profile explicitly allows it.

5. `LightingShadowV3`
   Owns direct lighting, unshadowed direct, indirect lighting, shadow
   visibility, shadow loss, energy budget, source attribution, filter radius,
   and light-motion diagnostics. Floor or wall flicker must map to a named
   source before we call it fixed.

6. `ReflectionV3`
   Owns SSR, RT/ray-query, planar or hero probe, local probe, environment
   fallback, source id, confidence, rejected-source mask, history validity,
   temporal delta, and hysteresis. Smooth or metallic jitter is fixed here, not
   in tonemapping or by making the IBL blurry.

7. `TransparencyMediaV3`
   Owns glass, water, transmission, decals, particles, volumetric inscatter,
   volumetric transmittance, and ordering diagnostics. It must be separable from
   opaque reflection and lighting.

8. `CompositeV3`
   Owns candidate HDR scene color, contribution map, energy clamp policy,
   overbright/underlit diagnostics, and measured legacy-rescue usage.

9. `CinematicPostV3`
   Owns exposure meter/state, bloom extraction and resolve, glare, tone map,
   color grade, sharpening, vignette, optional DOF, and bypass debug outputs.
   Post work starts only after upstream V3 resources are sufficiently real.

10. `PromotionV3`
    Owns focused packets, cross-family packets, contact sheets, frame reports,
    analyzer summaries, failure reports, default-promotion decisions, and human
    review state.

### Refactor Order

The implementation order is strict because each layer feeds the next:

1. Reconcile contracts and promotion gates.
   Make JSON contracts, C++ frame context, HLSL bindings, debug views, packet
   aliases, analyzers, and promotion scripts use the same V3 domain names.

2. Finish material ownership.
   Material channels must be owned, synthesized with a reason, or marked
   missing. Lighting, reflection, transparency, composite, and post must report
   whether they consumed missing material debt.

3. Make `SceneProfileV3` the renderer policy brain.
   Remove scattered family conditionals where possible and replace them with a
   single consumed policy object.

4. Make `SceneLocalEnvironmentV3` resource-backed.
   Move beyond scalar palette influence. Bind and eventually generate local
   irradiance, local specular/prefilter, visible background, and atmosphere
   resources per profile.

5. Expand environment coverage across scene families.
   Gallery, enclosed room, stage/red room, stadium, and exterior water profiles
   need payload-backed proof, not just a gallery stress case.

6. Finish `LightingShadowV3` attribution and stability.
   Use mouse jitter, close floor/wall views, and high-contrast light sweep to
   prove flicker is absent or attributed to directional, local, contact, RT,
   screen-space, filter, exposure, or environment sources.

7. Harden `ReflectionV3`.
   Add provider confidence, source-id stability, history rejection reasons, and
   hysteresis where diagnostics prove source churn on smooth or metallic
   surfaces.

8. Add `TransparencyMediaV3`.
   Only add water/glass/fog/particles once opaque environment, lighting, and
   reflection ownership is good enough to separate artifact sources.

9. Convert `CompositeV3` into the candidate HDR owner.
   Candidate beauty should normally consume V3 resources, not legacy HDR.
   Legacy rescue remains visible and should trend toward zero.

10. Build `CinematicPostV3`.
    Add filmic exposure, bloom, glare, tonemap, grade, sharpen, vignette, and
    optional DOF with bypass views for every strong post term.

11. Run the promotion matrix.
    Required families include gallery, kitchen, office, gym, classroom,
    concert, red room, stadium, bathroom, bedroom, workshop, store, street, and
    exterior water or vegetation. Required motion includes static, mouse
    jitter, camera sweep, close-surface orbit, reflective-object orbit, and
    high-contrast light sweep.

### Evidence Gates

Each domain must ship with:

- debug view coverage
- frame-report fields for readiness, source, fallback, and debt
- focused analyzer
- focused packet
- cross-family matrix row before promotion
- human-readable markdown summary
- failure mode that tells us what to fix next

The promotion gate blocks default beauty if:

- candidate HDR depends on uninspected legacy rescue as a normal path
- enclosed spaces show or sharply reflect unauthorized external IBL content
- shadow/floor/wall flicker lacks source attribution
- metallic or smooth jitter lacks ReflectionV3 source/history attribution
- required material channels are missing without downstream debt reporting
- environment resources are scalar approximations where payload-backed
  resources are expected
- post is hiding upstream instability
- cross-family packets are missing or fail

### Immediate Work After This Planning Slice

The next implementation phase should not start with final post-processing. It
should harden the core producer stack in this order:

1. `ReflectionV3` provider resolver hardening, because the user still sees
   smooth/metallic jitter and reflection instability.
2. `SceneLocalEnvironmentV3` resource expansion beyond payload aliases into
   generated or selected local irradiance/specular/background proxies.
3. Cross-family capture/report separation, so known model-scene crashes do not
   prevent frame-report diagnostics from being collected.
4. `CompositeV3` legacy-rescue accounting, so the candidate beauty path cannot
   quietly fall back to old HDR.
5. Only then, `CinematicPostV3` polish.

### Completion Boundary

Do not call the goal feature complete when one scene looks good. Completion
requires candidate beauty to pass packet evidence and human review across the
required family and motion matrix. Until then, the correct status is active
renderer refactor, not public-release-ready AAA visuals.

### ReflectionV3 Source Resolver Hysteresis Checkpoint

Implemented after the planning slice:

- Added bounded source hysteresis to `FullSceneReflectionResolverV3`.
  Auto mode can hold the previous reflection source only when previous history
  is reusable, the same source is still available, and the new winner is not
  decisively better.
- Forced overrides remain untouched as diagnostics. `local`, `ssr`, `rt`,
  `environment`, and `none` still show what happens when a provider is forced.
- Added `tools/analyze_reflection_v3_source_resolver.py`.
  It measures `reflection_source_id` as categorical provider evidence instead
  of generic luma:
  - red: source class
  - green: confidence
  - blue: override policy
  - outputs: source delta, switch ratio, active switch ratio, dominant source,
    and warnings
- Wired the analyzer into
  `tools/run_reflection_v3_motion_focus_packet.ps1`.
- Changed the focused runner default to `-SourceOverride auto` because the
  production resolver gate should test the actual auto policy. Forced SSR
  remains available as explicit stress evidence.
- Updated the V3 static validator so the resolver shader, focused runner, and
  source analyzer stay in the checked runtime surface.

Evidence:

- `build\captures\v3_reflection_source_hysteresis_focus_20260607`
  passed in auto mode with `0` resolver warnings:
  max source switch `0.000442`, max active switch `0.000442`.
- `build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607`
  produced expected resolver warnings:
  max source switch `0.101157`, max active switch `0.261804`.
  This proves the analyzer catches provider churn and leaves forced SSR as a
  remaining targeted diagnostic.

Follow-up diagnosis:

- The source resolver analyzer now summarizes adjacent ReflectionV3 diagnostic
  views, not just `reflection_source_id`.
- Forced SSR churn is currently attributed to:
  `ssr_signal_changes_under_motion`,
  `ssr_rejection_mask_high`,
  `ssr_rejection_changes_under_motion`,
  `forced_or_history_debt_present`,
  `temporal_delta_tracks_source_churn`,
  `material_suppression_contributes`, and
  `history_validity_changes_under_motion`.
- This means the warning is not generic shimmer. It is provider continuity debt:
  SSR signal and rejection state change under motion, and the history/material
  diagnostics move with it.
- Production implication: keep auto source resolution as the public candidate
  gate. Do not promote forced SSR for this view until SSR continuity or a
  stronger RT/local-probe fallback is proven.

Family packet extension:

- `run_reflection_v3_motion_focus_packet.ps1` now accepts `-FamilyFilter`.
- Omitted `-FamilyFilter` preserves the original stress-only packet behavior.
- Present `-FamilyFilter` runs gallery/model family packets through the same
  reflection motion analyzer, source-resolver analyzer, and review sheet.
- First bounded family evidence:
  `build\captures\v3_reflection_source_family_probe_gallery_office_20260607`.
  It passed for `gallery,office` with `26` reflection motion view sequences and
  `0` source-resolver warnings.
- Gallery max source switch was `0.000152`; office max source switch was
  `0.000000`.
- This moves ReflectionV3 from one stress-scene proof toward cross-family
  evidence, but does not satisfy the final promotion matrix.

CompositeV3 promotion gate:

- `build_full_scene_shader_v3_promotion_decision.py` now requires
  `v3_composite_diagnostics.json`.
- Promotion evidence exposes CompositeV3 legacy-rescue, clamp, direct
  contribution, and reflection contribution metrics.
- Promotion blocks when explicit or overbright legacy rescue exceeds `0.05`,
  when clamp debt exceeds `0.10`, or when requested candidate beauty lacks
  meaningful direct/reflection contribution.
- Existing clean evidence
  `build\captures\v3_environment_payload_resource_binding_gallery_20260607`
  passed the updated decision probe:
  mean explicit legacy rescue `0.000000`, mean legacy rescue `0.000000`,
  mean clamp mask `0.000110`, mean clamp ratio `0.000031`,
  mean direct contribution `0.642235`, mean reflection contribution
  `0.011335`.
- Fresh post-change packet
  `build\captures\v3_composite_promotion_gate_fresh_smoke_20260607` passed
  end to end with V2 evidence, V3 placeholders, scene profile, environment
  payload, material payload, CompositeV3 diagnostics, and promotion decision.
  Its CompositeV3 metrics were: mean explicit legacy rescue `0.000000`, mean
  legacy rescue `0.000000`, mean clamp mask `0.000110`, mean clamp ratio
  `0.000031`, mean direct contribution `0.643191`, and mean reflection
  contribution `0.011720`.
- The fresh packet remained a review packet, not default promotion, because it
  intentionally covered only the `stress_rt_showcase_reflection_closeup` family
  and static motion.
- This does not make CompositeV3 complete, but it prevents candidate-promotion
  packets from ignoring legacy HDR rescue debt.

SceneLocalEnvironmentV3 proxy-resource binding:

- The environment producer has moved past the previous two-texture payload
  binding. It now binds a five-slot table: payload albedo, payload normal,
  local irradiance proxy, local specular proxy, and visible-background proxy.
- The shader samples those proxy resources separately, so ambient, reflection,
  and visible-background ownership can diverge instead of all being derived
  from the same payload albedo signal.
- Frame reports and V3 aliases expose proxy-resource table required/bindable,
  bound count, binding source, and fallback reason.
- `scene_local_environment_ready` now requires `21` channels; payload-ready
  scenes must prove both payload texture binding and proxy texture binding.
- Fresh evidence
  `build\captures\v3_scene_local_proxy_binding_fresh_smoke_20260607` passed
  end to end with `54` payload-ready reports, `54` payload-resource-bindable
  reports, and `54` proxy-resource-bindable reports. First row bound `2`
  payload resources and `3` proxy resources with source
  `cached_scene_local_proxy_triple`.
- Cross-profile evidence
  `build\captures\v3_scene_local_proxy_binding_cross_profile_20260607` passed
  with `4` payload-ready reports, `4` proxy-resource-bindable reports, and
  profile coverage for `gallery_neutral=1.0`, `enclosed_room=2.0`,
  `stage=3.0`, and `open_exterior=4.0`.
- Explicit proxy asset generation now exists through
  `tools\generate_scene_local_environment_proxies.py`. It generates small BC1
  DDS files under `assets\textures\scene_local_proxy\<set_id>\` for local
  irradiance, local specular, and visible-background proxy roles.
- Proxy generation now uses `profile_payload_inventory_v1`: scene-profile base
  colors are adjusted by scene-local payload inventory, including texture
  count, albedo/normal count, and filename role signals for floor, wall, cube,
  cylinder, and metal/brushed surfaces.
- The generator writes
  `assets\textures\scene_local_proxy\proxy_manifest.json` with base RGB,
  derived RGB, payload inventory, role weights, and derivation method.
- The renderer now prefers those explicit proxy assets and reports
  `cached_explicit_scene_local_proxy_triple`; payload-derived fallback reports
  `cached_payload_derived_scene_local_proxy_triple`.
- The V3 placeholder and environment-payload analyzers now fail payload-ready
  packets unless the explicit proxy source is used. The environment-payload
  analyzer also requires a manifest entry with derivation method
  `profile_payload_inventory_v1`.
- Fresh evidence
  `build\captures\v3_scene_local_explicit_proxy_fresh_smoke_20260607` passed
  with `54/54` explicit proxy bindings.
- Cross-profile evidence
  `build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607` passed
  with `4/4` explicit proxy bindings across gallery, office, concert, and
  stadium profile modes.
- Fresh derived-proxy evidence
  `build\captures\v3_scene_local_derived_proxy_fresh_smoke_20260607` passed
  with `54/54` explicit proxy bindings and `54/54` derived proxy manifest
  matches.
- Cross-profile derived-proxy evidence
  `build\captures\v3_scene_local_derived_proxy_cross_profile_20260607` passed
  with `4/4` explicit proxy bindings and `4/4` derived proxy manifest matches.
- Material-sampled proxy generation now exists through
  `profile_payload_material_sample_v1`.
- The proxy generator decodes albedo/diffuse DDS payloads through Pillow,
  including current BC7 DX10 payload textures, and records material sample
  evidence in `assets\textures\scene_local_proxy\proxy_manifest.json`.
- The environment-payload analyzer now fails payload-ready packets unless the
  explicit proxy manifest proves decoded material-color samples are present.
- Latest generation report:
  `build\captures\scene_local_environment_proxy_generation_20260607\material_sample_proxy_generation_report.json`.
- Current material-sampled evidence: `basketball_gym_day` has `5` sampled
  color payloads with `0` failed; the other tracked proxy sets have `6` sampled
  color payloads with `0` failed.
- Fresh material-sampled proxy packet
  `build\captures\v3_scene_local_material_sample_proxy_fresh_smoke_20260607`
  passed with `54/54` explicit proxy bindings and `54/54` material-sampled
  proxy reports. The only derivation was
  `profile_payload_material_sample_v1`.
- Cross-profile material-sampled proxy packet
  `build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607`
  passed environment payload/profile analysis across gallery, office, concert,
  and stadium with `4/4` material-sampled proxy reports.
- Room/light proxy generation now exists through
  `profile_payload_material_room_light_v1`.
- The proxy generator records room-shell and light-rig influence in the
  manifest, including enclosure, reflectance, local background occlusion, light
  mode, key/fill/accent RGB, and accent strength.
- The environment-payload analyzer now fails payload-ready packets unless
  decoded material samples and scene-contract influence are both present.
- Latest generation report:
  `build\captures\scene_local_environment_proxy_generation_20260607\room_light_proxy_generation_report.json`.
- Fresh room/light proxy packet
  `build\captures\v3_scene_local_room_light_proxy_fresh_smoke_20260607`
  passed with `54/54` derived proxy reports, `54/54` material-sampled proxy
  reports, and `54/54` scene-contract proxy reports.
- Cross-profile room/light proxy packet
  `build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607`
  passed environment payload/profile analysis across gallery, office, concert,
  and stadium with `4/4` scene-contract proxy reports and profile coverage for
  gallery, enclosed room, stage, and open exterior.
- Runtime proxy-contract reporting now exists. Frame reports expose the proxy
  derivation method, room shell, room occlusion, light rig, and light accent
  strength in both the nested environment contract and the V3 report.
- The environment-payload analyzer now compares those runtime fields against
  `assets\textures\scene_local_proxy\proxy_manifest.json`.
- Fresh runtime proxy-contract packet
  `build\captures\v3_scene_local_runtime_proxy_contract_fresh_smoke_20260607`
  passed with `54/54` runtime scene-contract proxy reports.
- Cross-profile runtime proxy-contract packet
  `build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607`
  passed across gallery, office, concert, and stadium with runtime rooms
  `gallery_partial`, `evening_enclosed_room`, `dark_stage_volume`, and
  `open_exterior_bowl`.
- The room/light proxy contract is now generated into
  `src\Graphics\Generated\SceneLocalProxyContracts.generated.h` by
  `tools\generate_scene_local_environment_proxies.py`.
- Runtime uses `Generated::FindSceneLocalProxyContract()` instead of a
  handwritten duplicate mapping.
- Generated-header packet
  `build\captures\v3_scene_local_generated_proxy_contract_fresh_smoke_20260607`
  passed with `54/54` runtime scene-contract proxy reports.
- Cross-profile generated-header packet
  `build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607`
  passed across gallery, office, concert, and stadium with `4/4` runtime
  scene-contract proxy reports.
- Scene-local proxies are no longer flat color swatches. The generator now
  emits `filtered_directional_bc1_v1` BC1 maps with low-frequency directional
  color variation for irradiance, specular, and visible background roles.
- The manifest records per-output filter shape, block dimensions,
  average/min/max RGB, and variance. The environment-payload analyzer rejects
  payload-ready packets if the proxy shape is not filtered or if the minimum
  variance is at or below `0.01`.
- Fresh filtered proxy packet
  `build\captures\v3_scene_local_filtered_proxy_fresh_smoke_20260607` passed
  with `54/54` filtered proxy reports.
- Cross-profile filtered proxy packet
  `build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607` passed
  across gallery, office, concert, and stadium with `4/4` filtered proxy
  reports. The weakest proxy variance was `0.014379`.
- This is still not final environment generation. The next environment step is
  to convert these filtered 2D BC1 proxies into stronger probe-like resources
  with higher resolution, mip/prefilter levels, or explicit diffuse/specular
  sampling contracts.

## 2026-06-07 Master Refactor Before Goal Feature Completion

This is the current authoritative plan for moving CortexEngine from
"debuggable renderer with some cinematic pieces" to a full-scene shader stack
capable of AAA-style output. The goal feature is not one shader, one nicer IBL,
or one polished screenshot. The goal feature is an opt-in candidate renderer
where every visible contribution has a named producer, typed resource,
debug view, frame-report field, analyzer, and promotion packet.

## 2026-06-07 Full Scene Shader Refactor Blueprint

This section is the high-level architecture pass before continuing
implementation. The target is not to mimic Unreal with a pile of toggles. The
target is to give CortexEngine the same kind of full-frame ownership that makes
high-end realtime renderers stable: a scene policy controls the render intent,
typed material/environment/lighting/reflection resources carry that intent, and
composite/post only beautify terms that are already owned and diagnosable.

### North Star

Build a candidate V3 beauty path that can render an enclosed kitchen, a gallery,
an office, a gym, a concert/stage scene, a red room, a stadium, and an exterior
water/vegetation scene with:

- coherent local environment lighting instead of arbitrary visible/reflected
  HDRI bleed
- rich material response from typed PBR payloads, not anonymous defaults
- stable shadows/reflections under mouse jitter and close camera movement
- believable transparency/media for glass, water, fog, particles, and decals
- cinematic HDR assembly and post that improves the frame without hiding
  upstream bugs

Default public beauty remains the fallback until this candidate path passes the
promotion matrix and human review.

### Refactor Principle

Every visible pixel in candidate beauty must be explainable by a chain like:

```text
scene profile
  -> typed resource producer
  -> shader contribution
  -> debug view
  -> frame-report field
  -> analyzer gate
  -> promotion packet
```

If that chain is broken, the term is not production-ready. If a shader uses a
fallback, null resource, legacy buffer, or profile approximation, it must report
that debt. We do not tune post, blur an IBL, lower reflection sharpness, or swap
scenes to make a defect disappear.

### Architecture Layers

1. Scene policy layer.
   `SceneProfileV3` becomes the single policy source for family, enclosure,
   lighting rig, reflection priority, material expectations, exposure/post
   intent, motion tolerance, and local environment ownership.

2. Visibility and geometry layer.
   `VisibilityV3` owns stable depth, velocity, normals, object/material/surface
   ids, disocclusion, and motion confidence. This is the base for temporal
   stability, reflection history, contact shadows, decals, and transparent
   ordering.

3. Material payload layer.
   `MaterialPayloadV3` owns base color, normal, roughness, metallic, specular,
   AO, emissive, opacity/transmission, clearcoat, sheen, anisotropy, IOR,
   thickness, material class, missing-channel mask, and invalid-range mask.
   Downstream domains must report whether they consumed missing material debt.

4. Scene-local environment layer.
   `SceneLocalEnvironmentV3` owns visible background, diffuse irradiance,
   specular prefilter, reflection background, atmosphere parameters, and an
   ownership mask. Enclosed spaces get local room/stage radiance. Exterior and
   gallery profiles may admit HDRI only when the profile authorizes it.

5. Lighting and shadow layer.
   `LightingShadowV3` splits direct lighting, unshadowed direct, indirect,
   shadow visibility, shadow loss, lighting-energy budget, and shadow source
   attribution. Remaining floor/wall flicker must identify a concrete source:
   directional shadow, local shadow, contact/RT/screen-space shadow, PCSS
   radius, exposure, or environment.

6. Reflection layer.
   `ReflectionV3` produces separate SSR, RT/ray-query, planar/hero probe, local
   probe, and environment signals. A resolver chooses the source using material
   policy, source confidence, disocclusion, history validity, rejection mask,
   and hysteresis. Smooth/metallic object jitter is fixed here, not hidden in
   composite or post.

7. Transparency and media layer.
   `TransparencyMediaV3` owns glass, water, transparent accumulation, decals,
   particles, volumetric inscatter, volumetric transmittance, and ordering
   diagnostics. It must be separable from opaque reflections and lighting.

8. Composite layer.
   `CompositeV3` assembles candidate HDR from V3 material, environment,
   lighting, reflection, transparency, emissive, atmosphere, and decals. Legacy
   rescue is allowed only as a measured emergency lane trending toward zero.

9. Cinematic post layer.
   `CinematicPostV3` owns exposure meter/state, bloom extract/resolve, glare,
   tonemap, color grade, sharpen, vignette, optional DOF, and bypass views.
   Post tuning starts only after upstream V3 resources are real enough to
   inspect.

10. Promotion layer.
    Matrix packets cover families and motion modes. The promotion decision
    aggregates analyzers, contact sheets, frame reports, stability metrics,
    legacy-rescue debt, and human review.

### Dependency Order

Work proceeds in this order because each step gives the next step typed inputs:

1. Reconcile V3 contracts and promotion gates.
2. Finish material missing-channel ownership and downstream debt reporting.
3. Promote `SceneProfileV3` from evidence adapter to consumed renderer policy.
4. Make `SceneLocalEnvironmentV3` resource-backed, starting with real
   descriptor binding for local payload textures/proxies.
5. Expand cross-profile payload coverage for gallery, enclosed room, stage,
   exterior, and stadium-like spaces.
6. Add high-contrast `LightingShadowV3` stress rows and split source
   attribution until shadow/floor/wall flicker has named causes.
7. Harden `ReflectionV3` provider fusion, history, and source hysteresis for
   smooth/metallic motion.
8. Add `TransparencyMediaV3` only after opaque material/environment/reflection
   ownership is stable.
9. Convert `CompositeV3` into the real candidate HDR owner and reduce legacy
   rescue.
10. Build `CinematicPostV3` on the candidate HDR path.
11. Run cross-family promotion, inspect packet evidence, and only then consider
    default-beauty promotion.

### Near-Term Implementation Slices

Slice 1, resource-backed environment.

- Bind real scene-local payload/proxy SRVs into `SceneLocalEnvironmentV3`.
- Keep null descriptors valid and reported when resources are missing.
- Emit resource binding fields: table bound, bound resource count, binding
  source, payload/proxy source id, and fallback reason.
- Validate with the old-office/gallery stress case with IBL enabled and sharp
  enough to reveal bad reflections.

Slice 2, multi-profile environment payloads.

- Add or alias payload sets for enclosed room, stage/red room, exterior water,
  and stadium.
- Prove profile changes alter visible background, diffuse irradiance, specular
  reflection background, and atmosphere without shader-code edits.

Slice 3, shadow flicker attribution.

- Add high-contrast light-sweep and close-floor/wall motion rows.
- Split shadow attribution into directional, local, contact/RT, screen-space,
  filter-radius, and exposure/env interaction channels if the current
  attribution is too coarse.

Slice 4, reflection provider resolver.

- Expand source signals and debug outputs so metallic/smooth jitter is tied to
  source-id churn, history rejection, low confidence, SSR holes, RT fallback,
  planar/local probe mismatch, or environment fallback.
- Add hysteresis where the diagnostics prove source churn.

Slice 5, V3-only HDR assembly.

- Make candidate beauty consume V3 resources by default.
- Keep legacy rescue visible in contribution maps and promotion reports.
- Block promotion when rescue dominates, when required V3 resources are blank,
  or when fallback debt is unreported.

Slice 6, cinematic post.

- Implement post as an inspected final layer: locked exposure, bounded auto
  exposure, bloom/glare from real HDR masks, filmic tonemap, color grade,
  sharpening, vignette, optional DOF, and bypass views.
- Validate final LDR against raw HDR, bloom-off, grade-off, and exposure-locked
  views.

### Validation Harnesses

Focused harnesses stay small and root-cause oriented:

- environment payload binding focus
- cross-profile environment focus
- lighting/shadow high-contrast motion focus
- reflection smooth/metallic motion focus
- transparency/media closeup focus
- composite contribution focus
- post bypass/final LDR focus

Promotion harnesses are broader and slower:

- family matrix: gallery, kitchen, office, gym, classroom, concert, red room,
  stadium, bathroom, bedroom, workshop, store, street, exterior water/vegetation
- motion matrix: static, mouse jitter, camera sweep, close-surface orbit,
  reflective-object orbit, high-contrast light sweep
- review packet: contact sheets, metrics, frame reports, failure reports,
  default-promotion decision, and human review notes

### What Blocks Completion

The goal feature remains incomplete while any of these are true:

- candidate beauty still depends on uninspected legacy HDR as a normal path
- enclosed scenes show or sharply reflect unauthorized external IBL content
- smooth/metallic objects jitter without a named ReflectionV3 cause
- floor/wall shadow or color flicker lacks LightingShadowV3 attribution
- material channels are missing without visible downstream debt reporting
- scene-local environment uses only scalar/profile tint instead of bound local
  resources where resources are expected
- post-processing is used to hide upstream instability
- cross-family packets are missing or fail
- the user has not accepted the resulting visuals as good enough

### Product Shape

Keep two render lines until promotion:

```text
stable public beauty
  current default path, used as the fallback and regression baseline

candidate beauty V3
  opt-in full-scene shader path
  consumes only V3-owned resources unless an emergency legacy rescue lane is
  explicitly measured
```

Default beauty must not be changed while the refactor is incomplete. Candidate
beauty can be aggressive, cinematic, and experimental because it is isolated
behind packets, debug views, and promotion gates.

### Final Render Architecture

The target frame graph is:

```text
SceneProfileV3
  -> family/enclosure/environment/lighting/reflection/material/post policies

VisibilityV3
  -> depth, velocity, object_id, material_id, surface_id,
     stable normal, disocclusion, motion confidence

MaterialPayloadV3
  -> base_color, normal, roughness, metallic, specular, ao, emissive,
     opacity, transmission, clearcoat, sheen, anisotropy, ior, thickness,
     material_class, missing_channel_mask, invalid_range_mask

SceneLocalEnvironmentV3
  -> visible_background, diffuse_irradiance, specular_prefilter,
     reflection_background, atmosphere, ownership_mask

LightingShadowV3
  -> direct_lighting, unshadowed_direct, indirect_lighting,
     shadow_visibility, shadow_loss, shadow_source_attribution,
     lighting_energy_budget

ReflectionV3
  -> local_probe_signal, planar_probe_signal, ssr_signal, rt_signal,
     environment_signal, source_id, source_confidence, rejected_source_mask,
     temporal_delta, history_validity, history_rejection

TransparencyMediaV3
  -> glass, water, transmission, decals, particles, volumetric_inscatter,
     volumetric_transmittance, ordering_diagnostics

CompositeV3
  -> candidate_hdr_scene_color, contribution_map, energy_clamp_policy,
     overbright_diagnostics, underlit_diagnostics, legacy_rescue_usage

CinematicPostV3
  -> exposure_meter, exposure_state, bloom_extract, bloom_resolve,
     glare, tone_map, color_grade_delta, sharpen, vignette,
     candidate_ldr_cinematic_output
```

Each arrow is a contract. If a pass consumes a legacy resource, a default
texture, or a fallback value, the frame report must say so and the promotion
packet must count it.

### Dependency Order

1. **Contract unification.**
   Make the JSON contract, render graph, C++ frame context, HLSL resource
   bindings, debug-view registry, packet aliases, analyzers, and promotion
   decision use the same V3 names. Add negative tests where deleting one
   required resource fails by name.

2. **SceneProfileV3 as the policy brain.**
   One declared profile must own scene family, enclosure, environment mode,
   light rig, shadow policy, reflection priority, material expectations,
   exposure policy, post look, and motion tolerance. Scattered scene-family
   conditionals are allowed only as adapters while being retired.

3. **MaterialPayloadV3 as a typed surface contract.**
   Material quality cannot depend on anonymous defaults. Every channel must be
   owned, synthesized with a visible reason, or marked missing. Downstream
   lighting, reflections, transparency, composite, and post must report whether
   they consumed missing-channel debt.

4. **SceneLocalEnvironmentV3 as the room/stage/exterior owner.**
   Enclosed scenes need local backgrounds and local radiance, not visible or
   sharply reflected unrelated IBLs. The old-office IBL remains a stress case
   with IBL on and enough sharpness to expose wrong reflection behavior.

5. **LightingShadowV3 as a source-attributed lighting model.**
   Directional, local, rect, emissive, ambient, screen-space, RT/contact, and
   filter-radius terms must be separable. Floor/wall darkening under motion
   must map to a named term or be proven absent under locked exposure.

6. **ReflectionV3 as a source resolver, not one reflection texture.**
   SSR, RT/ray query, planar/hero probes, local probes, and environment fallback
   must produce separate signals. The resolver chooses using confidence,
   material policy, disocclusion, history validity, and hysteresis.

7. **TransparencyMediaV3.**
   Water, glass, particles, decals, fog, and volumetric terms need a real
   ordering model and diagnostics before post effects are tuned.

8. **CompositeV3 as V3-only HDR assembly.**
   Candidate HDR must be assembled from V3 material, lighting, environment,
   reflection, transparency, emissive, atmosphere, and decal terms. Legacy HDR
   rescue should trend toward zero and remain visible in every packet.

9. **CinematicPostV3.**
   Only after upstream ownership is real should we tune exposure, bloom, glare,
   tone mapping, color grade, sharpening, vignette, and optional DOF. Post must
   expose bypass views so it cannot hide upstream renderer defects.

10. **Promotion matrix and human review.**
    Required families: gallery, kitchen, office, gym, classroom, concert,
    red room, stadium, bathroom, bedroom, workshop, store, street, exterior
    water/vegetation. Required motion: static, mouse jitter, camera sweep,
    close-surface orbit, reflective-object orbit, high-contrast light sweep.
    Default beauty can be considered only after packet gates and user review
    accept the candidate visuals across the matrix.

### Implementation Slices

Slice A, current checkpoint:

- Keep the existing reflection-history stability work.
- Keep material debug-view coverage and missing-channel-mask evidence.
- Keep scene-local environment provenance and texture-payload reporting.
- Commit this checkpoint before deeper shader surgery.

Slice B:

- Upgrade `SceneProfileV3` from adapted frame evidence to a renderer policy
  object consumed by environment, lighting, reflection, material, and post.
- Add profiles for neutral lab, gallery, kitchen, gym, concert, red room,
  stadium, and exterior water.
- Packet proof: changing family changes the profile output and downstream
  policies without editing shader code.
- Current state: the first policy-owner contract slice is implemented.
  `SceneProfileV3` now emits `scene_profile_policy_contract` in frame reports
  and the old `scene_visual_contract` is recorded as a backing adapter input.
  Downstream domains still need to consume the policy fields explicitly.

Slice C:

- Expand `SceneLocalEnvironmentV3` from payload reporting to actual resource
  production: local visible background, local diffuse irradiance, local
  specular prefilter, atmosphere parameters, and ownership mask.
- Packet proof: enclosed scenes do not show or sharply reflect unrelated IBL
  imagery unless explicitly authorized by profile.
- Current state: the first policy-consumption gate is implemented.
  `SceneLocalEnvironmentV3` readiness now requires parity with
  `scene_profile_policy_contract` for environment policy, enclosure mode, and
  reflection policy. Actual local resource selection still needs to consume
  those fields.

Slice D:

- Split high-contrast shadow/lighting sources and add the focused light-sweep
  packet.
- Packet proof: remaining shadow flicker or wall/floor pulsing is attributed
  to a named source or rejected by a locked-exposure control.

Slice E:

- Finish ReflectionV3 provider fusion and history: local probe, planar/hero,
  SSR, RT/ray query, environment fallback, source confidence, source ID,
  rejection mask, hysteresis, and debug history.
- Packet proof: smooth and metallic objects remain stable under mouse jitter,
  camera orbit, and reflective-object orbit.

Slice F:

- Add TransparencyMediaV3 for water, glass, decals, particles, and volumetrics.
- Packet proof: transparency artifacts are separable from opaque reflection,
  environment, and composite artifacts.

Slice G:

- Make CompositeV3 the real candidate HDR owner and drive legacy rescue toward
  zero.
- Packet proof: contribution maps show where beauty comes from, and no single
  fallback dominates the image.

Slice H:

- Build CinematicPostV3 on candidate HDR with bypass/debug views.
- Packet proof: final LDR quality improves while raw HDR, bloom-off,
  grade-off, and exposure-locked views remain stable.

Slice I:

- Run the promotion matrix, summarize failures, fix root domains, and repeat.
- Default promotion remains blocked until matrix evidence and user review pass.

### Acceptance Standard

Do not call the goal feature complete until all are true:

- Candidate beauty produces materially richer, more stable, more cinematic
  images than current beauty in at least the required indoor, stage, stadium,
  and exterior families.
- Old-office IBL stress with IBL enabled no longer produces hidden reflection
  instability.
- Smooth/metallic closeups pass focused reflection-motion packets.
- High-contrast floor/wall motion passes focused lighting-shadow packets.
- Enclosed scenes have local environment ownership and no unauthorized visible
  or reflected background leakage.
- Material packets show owned/synthesized/missing channels and downstream debt
  consumption.
- Composite and post packets prove final beauty is coming from V3 resources,
  not an uninspected legacy HDR bridge.
- Promotion evidence includes frame reports, debug metrics, contact sheets,
  failure reports, matrix decision, and human review.

### 2026-06-07 SceneProfileV3 Policy Contract Checkpoint

Implemented:

- V3 scene-profile domain producer changed from
  `SceneCinematicProfileV1Adapter` to `SceneProfileV3`.
- Domain output changed from `scene_visual_contract` to
  `scene_profile_policy_contract`.
- `scene_visual_contract` remains the backing contract and adapter input.
- Frame reports now expose a policy contract with owner, contract id, family,
  enclosure, environment, lighting, reflection, exposure, material, temporal,
  post, and motion-stability policies.
- The scene-profile analyzer and static plan validator now require that
  policy contract.

Evidence:

- Static plan validation passed.
- Native build passed.
- Focus packet root:
  `build\captures\scene_profile_v3_policy_contract_focus_20260607`.
- Manual analyzer passed with `21` reports, `3` families, `3` profiles,
  `3` policy contracts, `0` failures, and `0` warnings.
- The packet wrapper returned nonzero because the known model-authored kitchen
  view path hit DX12 `DXGI_ERROR_DEVICE_HUNG` on some views; shutdown reports
  were still valid and the profile analyzer passed.

Next:

- Make `SceneLocalEnvironmentV3` consume
  `scene_profile_policy_contract.environment_policy`,
  `enclosure_mode`, and `reflection_policy` to choose local visible
  background, diffuse irradiance, specular prefilter, atmosphere, and
  ownership mask behavior.

### 2026-06-07 SceneLocalEnvironmentV3 Policy Consumption Checkpoint

Implemented:

- Environment readiness now requires three SceneProfileV3 policy-consumption
  channels, raising the ready-channel contract from `10` to `13`.
- Frame reports expose environment/profile parity through:
  `scene_local_environment_consumes_scene_profile_policy`,
  `scene_local_environment_profile_contract_id`,
  `scene_local_environment_profile_enclosure_mode`,
  `scene_local_environment_profile_policy`, and
  `scene_local_environment_profile_reflection_policy`.
- The V3 placeholder and environment-payload analyzers fail on policy contract
  mismatch.

Evidence:

- Full V3 stress packet:
  `build\captures\v3_environment_profile_policy_consumption_stress_20260607`.
- Packet passed V2 evidence, V3 placeholder checks, scene-profile analysis,
  environment-payload analysis, material-payload analysis, CompositeV3
  diagnostics, and review-packet promotion decision.
- Environment payload analysis reported `54/54` profile-policy-consumed
  reports and `0` failures. Texture payload readiness remained `0`, expected
  for `rt_showcase_gallery` until a texture set is added.

Next:

- Move from evidence parity to real resource selection in
  `SceneLocalEnvironmentV3`: local visible background, diffuse irradiance,
  specular prefilter, atmosphere parameters, and ownership mask.

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

### SceneLocalEnvironmentV3 Provenance Contract - 2026-06-07

Implemented:

- `SceneLocalEnvironmentV3` now exposes environment provenance in the V3 frame
  report:
  `scene_local_environment_policy`,
  `scene_local_visible_background_source`,
  `scene_local_reflection_background_source`,
  `scene_local_ambient_source`,
  `scene_local_atmosphere_source`, and
  `scene_local_environment_source_count`.
- The environment V3 domain now requires `10` channels:
  mode, policy, ownership bits, and per-layer source ownership for ambient,
  visible background, reflection background, and atmosphere.
- The V3 JSON contract lists these as required environment policy channels.
- The placeholder analyzer and plan validator both gate the new provenance
  fields.

Validated evidence:

- `build/captures/v3_scene_local_environment_provenance_full_stress_20260607`
  passed V2 evidence, V3 placeholder artifacts, scene profile, material
  payload, CompositeV3 diagnostics, and review-packet promotion decision.
- Environment readiness remained complete:
  `scene_local_environment_ready_report_count=41/41`.
- Sample beauty report had `scene_local_environment_channel_count=10`,
  source count `4`, and concrete source ownership:
  `authorized_visible_hdri`, `local_reflection_probe_radiance`,
  `scene_profile_lighting_balance`, and `environment_matched_fog`.

Next refactor direction:

- Use this provenance to move from procedural/tint environment buffers toward
  texture-backed scene-local environment payloads: authored room visible
  background, local irradiance/specular proxies, and atmosphere parameters per
  scene profile.

### V3 Promotion Matrix Harness - 2026-06-07

Implemented:

- `tools/build_full_scene_shader_v3_matrix_decision.py` aggregates passed V3
  review packets and reports observed/missing families and motion modes.
- `tools/run_full_scene_shader_pipeline_v3_matrix.ps1` either aggregates
  existing packet roots or, with explicit `-RunPackets`, renders a bounded
  family/motion matrix.
- The harness keeps `default_beauty_promotable=false`; it is a promotion
  evidence gate, not an automatic default switch.

Smoke proof:

- Existing passing packet
  `build/captures/v3_scene_local_environment_provenance_full_stress_20260607`
  was aggregated by both the Python analyzer and PowerShell wrapper.
- The matrix correctly reported incomplete coverage:
  one passed packet, observed motion `static`, observed family
  `stress_rt_showcase_reflection_closeup`, missing required families
  `gallery,kitchen`, and missing required motion `mouse_jitter`.

### SceneLocalEnvironmentV3 Texture Payload Contract - 2026-06-07

Implemented:

- The frame contract now reports scene-local texture payload state:
  texture set id/path/presence, total texture count, albedo/normal counts,
  payload readiness, and irradiance/specular/visible-background proxy
  readiness.
- Runtime derives the texture set id from the active scene family and scans
  `assets/textures/scene_local/<family>`, including the repo-relative path
  when launched from `build/bin`.
- V3 frame reports now expose `scene_local_texture_payload_ready`,
  `scene_local_texture_payload_count`, and `scene_local_texture_set_id`.
- New analyzer:
  `tools/analyze_full_scene_shader_v3_environment_payload.py`.
- Standard V3 packets now emit `v3_environment_payload.json/md`, and promotion
  review requires that artifact.

Validated evidence:

- Gym focus:
  `build/captures/v3_environment_payload_gym_focus_20260607`.
  The renderer device-hung before successful captures, but shutdown reports
  were written and the environment-payload analyzer passed with:
  `6/6` payload-ready reports, texture set `basketball_gym_day`, `10` DDS
  textures, `5` albedo, `5` normal, and all three proxies ready.
- Full stress:
  `build/captures/v3_environment_payload_full_stress_20260607`.
  Standard V3 packet passed end to end and emitted the required
  environment-payload artifact. `rt_showcase_gallery` currently has no
  texture set, so payload-ready count is `0` with no failures.

Next refactor direction:

- Add/import texture sets for the promotion families and then make
  `SceneLocalEnvironmentV3.hlsl` consume payload readiness for richer local
  irradiance/specular/background color selection.
- Treat repeated model-authored scene `DXGI_ERROR_DEVICE_HUNG` failures as a
  separate renderer stability blocker for broad cross-family capture packets.

### SceneLocalEnvironmentV3 Shader Profile Resource Selection - 2026-06-07

Implemented:

- `SceneLocalEnvironmentV3` now has a runtime shader-profile selector produced
  from the active scene profile/visual contract by
  `Renderer::BuildSceneLocalEnvironmentV3ProfileParams()`.
- The profile selector is carried to shaders in
  `FrameConstants::cinematicDofParams.zw`:
  profile mode in `.z`, local-background ownership strength in `.w`.
- The environment shader uses the profile to choose local gallery, enclosed
  room, stage, and exterior palettes for visible background, ambient lighting,
  reflection background, and atmosphere. This is the first actual shader-side
  resource-selection step after the earlier provenance/policy-consumption
  checkpoints.
- Frame reports and V3 context now expose the selected shader profile, numeric
  profile mode, and local-background strength. Environment readiness requires
  these lanes, raising the channel contract to `15/15`.
- The contract, analyzers, and validator now require these lanes. The validator
  explicitly includes `Renderer_FramePostConstants.cpp`, so producer-side C++
  wiring is part of the checked runtime surface.

Validated evidence:

- Static Python compile and V3 plan validation passed.
- Focused diff hygiene passed for the renderer, shader, contract, analyzer,
  and validator files.
- Native `CortexEngine` target rebuilt successfully.
- Packet
  `build/captures/v3_scene_local_environment_shader_profile_stress_20260607`
  passed V2 evidence, V3 placeholder checks, scene-profile analysis,
  environment-payload analysis, material-payload analysis, CompositeV3
  diagnostics, and promotion decision.
- Environment payload reported `54` reports, `54` profile-policy-consumed
  reports, `0` failures, and the new shader profile row:
  `gallery_neutral`, mode `1.0`, local-background strength `0.35`.

Next refactor direction:

- Add texture-backed payload sets for gallery and promotion-family scenes, then
  replace palette-only profile selection with payload-backed local irradiance,
  local specular, visible-background, and atmosphere resources.
- Add a bounded cross-profile packet proving gallery, enclosed room, stage/red
  room, and exterior modes select different environment behavior.
- Keep CinematicPostV3 tuning blocked until SceneLocalEnvironmentV3,
  LightingShadowV3, ReflectionV3, and CompositeV3 ownership evidence is
  materially stronger.

### SceneLocalEnvironmentV3 Cross-Profile Analyzer - 2026-06-07

Implemented:

- Added `tools/analyze_full_scene_shader_v3_environment_profiles.py` as a
  bounded proof harness for profile-driven environment selection.
- The analyzer reads one or more manifests, extracts V3 frame-report shader
  profile lanes, and can require:
  - minimum ready environment reports
  - minimum distinct shader profiles
  - minimum distinct shader modes
  - named profile/mode pairs such as `gallery_neutral=1`,
    `enclosed_room=2`, and `stage=3`
- Missing reports remain failures by default. Diagnostic packets from known
  crash paths can opt into `--allow-missing-reports`.
- The V3 plan validator now includes this analyzer in the checked runtime
  surface.

Validated evidence:

- Static compile and V3 plan validation passed.
- A diagnostic model-family packet was run for
  `kitchen,gym,concert,red_room,stadium` using only the
  `scene_local_environment` view. It returned nonzero because the known
  model-scene crash path still affected kitchen/gym/red-room captures, but
  shutdown reports were written for the usable rows.
- Cross-profile analysis combined the passing gallery stress packet with the
  diagnostic model-family manifest:
  `build/captures/v3_environment_profiles_model_family_probe_20260607/v3_environment_profiles_cross_probe.json`.
- Result:
  - reports `58`
  - environment-ready reports `57`
  - shader profiles:
    `enclosed_room`, `gallery_neutral`, `open_exterior`, `stage`
  - shader modes: `1.0`, `2.0`, `3.0`, `4.0`
  - failures `0`

Next refactor direction:

- Promote this from diagnostic evidence into regular matrix evidence after
  model-scene capture stability is separated from frame-report availability.
- Replace the current profile-palette approximation with texture/resource
  payloads for each profile family.

### SceneLocalEnvironmentV3 Payload-Backed Shader Influence - 2026-06-07

Implemented:

- Added a payload-availability producer,
  `Renderer::BuildSceneLocalEnvironmentV3PayloadParams()`, that scans
  scene-local payload textures and derives readiness, texture richness, proxy
  score, and shader influence.
- Packed payload readiness, texture richness, and shader influence into
  `FrameConstants::fogExtraParams.yzw`, leaving `.x` as fog start distance.
- Updated `SceneLocalEnvironmentV3.hlsl` so payload-ready scenes bias visible
  background, ambient lighting, reflection background, and confidence toward
  payload-owned local radiance rather than profile palette constants alone.
- Added an explicit `rt_showcase_gallery` payload alias to the tracked
  `assets/textures/rtshowcase` DDS set. This avoids duplicating large binary
  textures while giving the gallery a real payload-backed path.
- Added frame-report and V3 aliases for payload texture richness, proxy score,
  and shader influence. The contract, analyzer, and validator now require and
  check these fields.

Validated evidence:

- Static Python compile, V3 plan validation, focused diff hygiene, and native
  `CortexEngine` build all passed.
- Packet:
  `build/captures/v3_environment_payload_shader_influence_gallery_20260607`.
  It passed V2 evidence, V3 placeholder checks, scene-profile analysis,
  environment-payload analysis, material-payload analysis, CompositeV3
  diagnostics, and promotion decision.
- Environment payload result:
  - `54` reports
  - `54` payload-ready reports
  - `54` shader-influence reports
  - `0` failures
  - `rt_showcase_gallery` resolves to `12` DDS textures, `5` albedo, `6`
    normal, richness `1.0`, proxy score about `0.67`, shader influence about
    `0.87`

Known limitation:

- This is still a payload-influence path, not final texture sampling. Actual
  AAA environment ownership still needs bound local irradiance/specular/visible
  background resources and multi-family payload coverage.
- Model-authored gym direct smoke still exited `2173` after writing a BMP but
  before writing a frame report; keep that as renderer stability debt, not a
  SceneLocalEnvironmentV3 proof.

Next refactor direction:

- Add real resource binding for local irradiance/specular/background proxies.
- Add payload sets or aliases for enclosed room, stage, and exterior profiles.
- Resume LightingShadowV3/ReflectionV3 work after the environment resource path
  has at least one non-gallery payload-backed proof.

### SceneLocalEnvironmentV3 Payload Resource Binding - 2026-06-07

Implemented:

- `SceneLocalEnvironmentV3` now binds a real two-slot payload SRV table:
  `t0` scene-local payload albedo and `t1` scene-local payload normal/detail.
- The pass always writes valid descriptors. Missing payload resources become
  null SRVs with explicit frame-report fallback fields.
- `Renderer::BuildSceneLocalEnvironmentV3PayloadBindingInfo()` selects a
  representative scene-local albedo/normal pair, prefers resident cached GPU
  textures, and queues missing uploads outside render-graph execution.
- `SceneLocalEnvironmentV3.hlsl` samples the payload resources and gates their
  influence by actual sample signal so scalar payload readiness is no longer
  the only shader-side input.
- Frame reports, V3 aliases, the JSON contract, placeholder analyzer,
  environment-payload analyzer, and static V3 validator all include resource
  binding fields.

Validated evidence:

- Native `CortexEngine` build passed.
- `ctest --test-dir build --output-on-failure -C Release` exited successfully,
  though the current build has no registered tests.
- Focused packet:
  `build/captures/v3_environment_payload_resource_binding_gallery_20260607`.
- Packet passed V2 evidence, V3 placeholder checks, scene-profile analysis,
  environment-payload analysis, material-payload analysis, CompositeV3
  diagnostics, and promotion decision.
- Environment payload proof:
  `54/54` reports payload-ready, `54/54` resource-bindable,
  `54/54` bound-resource reports, `2` bound resources per row, binding source
  `cached_scene_local_payload_pair`, fallback reason `none`.

Remaining environment debt:

- This proves one gallery/stress payload pair. It is not yet full
  scene-local irradiance/specular/background proxy generation.
- Cross-profile payload resource evidence is still required for enclosed room,
  stage/red room, exterior water, and stadium-like spaces.

### SceneLocalEnvironmentV3 Cross-Profile Payload Aliases - 2026-06-07

Implemented:

- Added explicit scene-local payload source aliases for non-gallery families
  without duplicating DDS assets:
  `home_kitchen_lantern`, `home_office_evening`, `school_classroom_day`,
  `basketball_gym_day`, `neon_streamer_concert`, `red_light_room`, and
  `stadium_night_match`.
- The alias path is shared by payload scanning and payload binding discovery,
  so frame-report texture counts and shader SRV binding use the same source.
- The static V3 validator now checks for the alias helper.

Validated evidence:

- Native build passed.
- Cross-profile payload analysis passed on
  `build/captures/v3_environment_payload_resource_binding_cross_profile_20260607`:
  `5/5` reports payload-ready and resource-bindable.
- Office probe passed on
  `build/captures/v3_environment_payload_resource_binding_office_probe_20260607`:
  `home_office_evening`, shader profile `enclosed_room`, `2` bound resources,
  binding source `cached_scene_local_payload_pair`.
- Combined profile analysis passed across gallery, office, concert, and
  stadium evidence:
  `57` environment-ready reports with shader profiles
  `gallery_neutral`, `enclosed_room`, `stage`, and `open_exterior`.

Known limitation:

- Kitchen, gym, and red-room model packets still return nonzero because those
  scenes hit the known renderer/model-scene capture instability before the
  environment pass executes. They still emit payload diagnostics, but they are
  not used as clean environment-ready proof yet.
- Aliases are an ownership bridge. They are not a substitute for proper
  per-family baked irradiance/specular/background proxy generation.

### LightingShadowV3 Source Attribution Split - 2026-06-07

Implemented:

- Split `FullSceneLightingV3` shadow-source attribution into explicit source
  channels:
  - red: directional/sun shadow-loss ratio.
  - green: local fixture shadow-loss ratio.
  - blue: shadow-map path enabled.
  - alpha: PCSS/filter path enabled.
- Added `tools/analyze_full_scene_shader_v3_shadow_attribution.py`.
  The analyzer checks that `v3_shadow_source_attribution` is not just present,
  but has source signal consistent with `v3_shadow_loss`,
  `v3_shadow_visibility`, and `v3_lighting_energy_budget`.
- Updated `tools/run_lighting_v3_shadow_motion_focus_packet.ps1` so the
  focused packet runs the attribution analyzer and emits
  `v3_shadow_attribution.json/md`.
- Narrowed `--focus shadow` in
  `tools/analyze_full_scene_shader_v3_lighting_motion.py` to shadow-owned
  views. `v3_indirect_lighting` is no longer part of this focused gate.
- Updated the static V3 validator to include the new analyzer, runner,
  deferred lighting shader, and source-split tokens.

Validated evidence:

- Static Python compile and PowerShell parse checks passed.
- Native `CortexEngine` build passed with the known trailing `vswhere.exe`
  warning after the successful Ninja target.
- Fresh packet:
  `build/captures/v3_lighting_shadow_source_split_focus_pass2_20260607`.
- Packet wrapper passed end to end.
- Motion analyzer passed:
  `11` view sequences, `0` warnings, `0` failures.
- Shadow-attribution analyzer passed:
  `1` family, `0` warnings, `0` failures.
- Attribution row for `stress_rt_showcase_reflection_closeup`:
  sun loss `0.339516`, local loss `0.007993`, source active `0.464322`,
  shadow-loss active `0.839763`, visibility occlusion `1.000000`,
  shadow-map enabled `1.000000`, energy active `1.000000`.

Known limitation:

- This is still mouse-jitter stress, not the planned high-contrast light-sweep
  row. The next LightingShadowV3 pass should add scripted light-rig variation
  or cascade/slice attribution if source-specific instability remains.

### LightingShadowV3 Light-Sweep Stress Row - 2026-06-07

Implemented:

- Added an opt-in runtime light sweep for capture automation:
  `CORTEX_LIGHT_SWEEP`, `CORTEX_LIGHT_SWEEP_FRAMES`,
  `CORTEX_LIGHT_SWEEP_CYCLES`,
  `CORTEX_LIGHT_SWEEP_YAW_AMPLITUDE_DEGREES`,
  `CORTEX_LIGHT_SWEEP_ELEVATION_AMPLITUDE`, and
  `CORTEX_LIGHT_SWEEP_INTENSITY_AMPLITUDE`.
- The sweep runs in `Engine::Update()` and changes real renderer lighting
  state via `SetSunDirection()` and `SetSunIntensity()`, so shadow maps,
  LightingV3 buffers, frame reports, and analyzers all see the same condition.
- Added `light_sweep` to the scene-local packet runner and the focused
  LightingShadowV3 runner.
- Added `light_sweep` support to the V3 lighting-motion matrix runner.
- Extended the scene-local contract tests and V3 static validator to keep the
  light-sweep interface covered.

Validated evidence:

- Native build passed.
- Scene-local packet contract tests passed.
- Focused packet:
  `build/captures/v3_lighting_shadow_light_sweep_focus_20260607`.
- Packet wrapper passed end to end.
- Motion analyzer passed:
  `11` view sequences, `0` warnings, `0` failures.
- Shadow-attribution analyzer passed:
  `1` family, `0` warnings, `0` failures.
- Key rows:
  - `v3_shadow_visibility.delta=0.01310343`, `1.000x` legacy,
    `31.476x` beauty.
  - `v3_shadow_loss.delta=0.00762285`, `0.957x` legacy,
    `18.311x` beauty.
  - `v3_shadow_source_attribution.delta=0.00207217`, `4.978x` beauty.
- Attribution row for `stress_rt_showcase_reflection_closeup`:
  sun loss `0.621078`, local loss `0.007351`, source active `0.777158`,
  shadow-loss active `0.998522`, visibility occlusion `1.000000`,
  shadow-map enabled `1.000000`, energy active `1.000000`.

Known limitation:

- This is a focused gallery/stress-scene row. The next promotion-grade step is
  to run `light_sweep` in a bounded cross-family matrix after model-scene
  capture/report instability is separated from diagnostics.
