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

Initial bridge:

- `candidate_beauty_v3` is an opt-in packet view.
- the view sets `CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3=1` only for that
  capture row.
- frame reports expose `candidate_beauty_requested`,
  `candidate_beauty_ready`, `candidate_beauty_producer`, and
  `candidate_beauty_output`.
- the first producer is `FullSceneCandidateBeautyV3Adapter`, which is allowed
  to reuse current ready composite/post evidence while the real candidate
  composite shader is built.
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

## Goal Completion Boundary

Do not claim completion when a screenshot looks better.

Completion requires:

- the candidate beauty path exists and is opt-in.
- required V3 domains are real shader resources, not only report adapters.
- cross-family packets produce evidence across required scene families.
- stability packets pass without device removal.
- debug views explain the final image.
- user accepts the candidate visuals as good enough to promote.
