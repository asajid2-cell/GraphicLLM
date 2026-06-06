# Full Scene Shader AAA Refactor Plan

Status: planning ledger.

Default beauty stays unchanged until a separate promotion gate passes.

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
