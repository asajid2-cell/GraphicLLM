# FullSceneShaderPipeline V3 Refactor Plan and Ledger

Status: planned, not promoted.

Default beauty remains unchanged until an explicit V3 promotion decision passes.

V3 exists to turn the current V2 renderer slices into a coherent full-scene
shader stack. V2 proved useful source signals: material evidence, local
reflection radiance, direct-light debug gates, named lighting ownership, and
semantic light payload lanes. V3 must make those signals first-class render
graph resources and validation artifacts before any default beauty promotion.

This document is both the plan and the stopping ledger. Keep it current before
and after each implementation slice.

## Target

Make CortexEngine capable of Unreal-style scene rendering through a stable,
debuggable, scene-local pipeline:

- material attributes with complete PBR channel ownership.
- cinematic direct and indirect lighting.
- soft, stable shadow visibility.
- source-aware reflections with confidence and temporal bounds.
- scene-local environment lighting instead of visible IBL leakage.
- filmic post processing.
- packet evidence proving that each domain is real and stable.

## Non-Goals

- Do not hide problems by blurring IBLs, disabling reflections, changing test
  scenes, or moving artifacts out of frame.
- Do not promote a visual feature to default beauty without a named producer
  resource, debug view, frame-report field, analyzer, and packet evidence.
- Do not claim AAA quality from a single screenshot.
- Do not mix scene-authoring quality with renderer quality. V3 is a renderer
  refactor; scene construction can feed it later.

## Contract Source

Machine-checkable contract:

`assets/final_art/full_scene_shader_pipeline_v3_contract.json`

Required scene families:

- `gallery`
- `kitchen`
- `office`
- `gym`
- `concert`
- `red_room`
- `stadium`

## Architecture

V3 is organized as explicit producer and consumer edges:

```text
Geometry / Visibility
  -> FullSceneMaterialResolveV3
       -> material_attributes
  -> FullSceneLightingV3
       -> direct_lighting
       -> direct_lighting_unshadowed
       -> indirect_lighting
       -> shadow_visibility
       -> shadow_loss
  -> FullSceneReflectionV3
       -> reflection_radiance
       -> reflection_confidence
       -> reflection_source_id
       -> reflection_temporal_delta
  -> SceneLocalEnvironmentV3
       -> scene_local_environment
       -> ambient_lighting
       -> visible_background
       -> reflection_background
       -> atmosphere
  -> FullSceneCompositeV3
       -> hdr_scene_color
  -> CinematicPostV3
       -> ldr_cinematic_output
```

Every edge must be visible in:

- runtime frame report.
- debug view selector.
- packet metrics.
- contact sheet.
- promotion decision.

## Frame Pseudocode

```cpp
void RenderFrameV3(FrameInput input) {
    SceneProfile profile = CompileSceneProfile(input.scene);
    V3Contract contract = BeginV3FrameContract(profile);

    VisibilityBuffers visibility = RenderVisibility(input);

    MaterialAttributes material = FullSceneMaterialResolveV3(
        visibility,
        input.materials,
        profile.materialPolicy);
    contract.RecordResource("material_attributes", material);

    SceneLocalEnvironment env = SceneLocalEnvironmentV3(
        profile.environmentMode,
        input.environment,
        input.lights);
    contract.RecordResource("scene_local_environment", env);

    LightingOutputs lighting = FullSceneLightingV3(
        visibility,
        material,
        env,
        input.semanticLights,
        input.shadowResources);
    contract.RecordResource("direct_lighting", lighting.direct);
    contract.RecordResource("shadow_visibility", lighting.shadowVisibility);
    contract.RecordResource("indirect_lighting", lighting.indirect);

    ReflectionOutputs reflections = FullSceneReflectionV3(
        visibility,
        material,
        env,
        lighting,
        input.reflectionSources,
        input.history);
    contract.RecordResource("reflection_radiance", reflections.radiance);
    contract.RecordResource("reflection_confidence", reflections.confidence);

    Texture hdr = FullSceneCompositeV3(
        material,
        lighting,
        reflections,
        env.atmosphere);
    contract.RecordResource("hdr_scene_color", hdr);

    Texture ldr = CinematicPostV3(
        hdr,
        input.camera,
        profile.postPolicy,
        input.history);
    contract.RecordResource("ldr_cinematic_output", ldr);

    EmitDebugViews(contract, material, lighting, reflections, env, hdr, ldr);
    EmitFrameReport(contract);
}
```

## Domain Plans

### Render Graph Ownership

Deliverables:

- `FullSceneShaderPipelineV3` frame context.
- render graph resource descriptors for every V3 output.
- owner/producer/consumer fields in the frame report.
- debug view IDs for every required output.

Admission gates:

- no required resource is missing.
- no required resource has an unknown owner.
- default beauty is unchanged while V3 is review-only.

### Material Resolve V3

Deliverables:

- packed material attributes buffer or texture set.
- complete PBR channel policy:
  `base_color`, `roughness`, `metallic`, `normal`, `ambient_occlusion`,
  `emissive`, `opacity`, `transmission`, `clearcoat`, `sheen`, `anisotropy`,
  `ior`, `thickness`, `material_class`.
- missing-channel mask.
- material class debug view.

Admission gates:

- material channels are present or have explicit authored fallback policy.
- material class IDs are stable across camera motion.
- roughness/metallic/normal debug views are nonblank on material stress scenes.

### Lighting V3

Deliverables:

- separate lighting outputs rather than only `hdr_color` ownership.
- support for directional, point, spot, rect area, emissive mesh, and
  scene-local ambient lights.
- shadow visibility output.
- shadow loss output.
- overbright and underlit diagnostic views.

Admission gates:

- direct signal is nonzero on all required families.
- shadow loss is nonzero where shadows are expected.
- shadow visibility is stable under mouse-jiggle/camera-sweep packets.
- no lighting domain depends on visible IBL background for enclosed rooms.

### Reflection V3

Deliverables:

- source-aware resolver combining local reflection radiance, SSR, ray query,
  local probe, and scene-local environment fallback.
- per-pixel source ID.
- per-pixel confidence.
- temporal delta view.
- rejected-source mask.

Admission gates:

- confidence is nonzero on glossy/metallic stress scenes.
- temporal delta is bounded under camera motion.
- rough surfaces do not inherit sharp IBL detail.
- enclosed rooms do not reflect unrelated exterior IBLs.

### Scene Local Environment V3

Deliverables:

- environment modes:
  `enclosed_room`, `open_exterior`, `stage`, `neutral_lab`.
- separate visible background and reflection background.
- ambient lighting output.
- atmosphere output.

Admission gates:

- enclosed rooms hide external IBL backgrounds.
- reflection backgrounds match scene mode.
- ambient lighting remains nonzero without leaking unrelated office/forest
  imagery into room reflections.

### Cinematic Post V3

Deliverables:

- filmic tonemap.
- exposure meter.
- bloom extract.
- color grade delta.
- optional depth of field and sharpen stages.

Admission gates:

- no persistent exposure clipping.
- bloom has nonzero signal only from bright sources.
- color grade delta is visible but bounded.
- raw HDR and final LDR comparison packets are saved.

## Packet Harness

V3 packets must emit:

- `frame_report_shutdown.json`
- `debug_view_metrics.json`
- `v3_signal.json`
- `v3_stability.json`
- `contact_sheet.png`
- `promotion_decision.md`

Required view set:

```text
beauty,
material_base_color,
material_roughness,
material_metallic,
material_normal,
material_class,
direct_light,
direct_light_unshadowed,
shadow_visibility,
shadow_loss,
indirect_light,
reflection_radiance,
reflection_confidence,
reflection_source_id,
reflection_temporal_delta,
environment_mode,
ambient_lighting,
reflection_background,
hdr_scene_color,
exposure_meter,
bloom_extract,
ldr_cinematic_output
```

Required motion modes:

- static.
- camera_sweep.
- mouse_jiggle.

## Implementation Phases

## Full AAA Visual Refactor Roadmap

This roadmap is the concrete path from the current review-only V3 slices to a
full-scene shader stack that can credibly target Unreal-style visuals.

The core rule: each visual domain is refactored into an owned resource before it
is allowed to affect default beauty. A domain is not real because a screenshot
looks better. It is real when it has a producer, resource name, debug view,
frame-report fields, packet metrics, motion stability evidence, cross-family
evidence, and a fallback decision.

### Current Proven Base

Already proven:

- V3 contract, validator, packet skeleton, and frame-report visibility exist.
- Material Resolve V3 exposes material attributes and debug views.
- Lighting V3 can write concrete split MRT resources:
  `direct_lighting`, `direct_lighting_unshadowed`, `shadow_visibility`,
  `shadow_loss`, and `indirect_lighting`.
- Concrete split-buffer packet evidence exists for the static gallery path.
- Default beauty is still V2/V1 fallback, not V3-promoted.

Not proven yet:

- lighting split parity under camera motion.
- lighting split parity across kitchens, offices, gyms, concerts, red rooms,
  and stadiums.
- source-aware reflections.
- scene-local reflection backgrounds.
- physically coherent roughness/metal/normal response under movement.
- cinematic HDR composite and post pipeline.
- default-beauty promotion.

### Refactor Spine

The engine should converge on this runtime spine:

```text
FrameSetupV3
  -> Visibility / GBuffer
  -> MaterialResolveV3
  -> SceneLocalEnvironmentV3
  -> LightingV3
  -> ReflectionV3
  -> CompositeV3
  -> CinematicPostV3
  -> Packet / PromotionGate
```

Each stage owns exactly one contract boundary:

- `FrameSetupV3` owns scene profile, quality tier, camera jitter state, and
  debug mode routing.
- `MaterialResolveV3` owns all PBR material channels and authored fallback
  policy.
- `SceneLocalEnvironmentV3` owns ambient radiance, visible background,
  reflection background, atmosphere, and IBL permission.
- `LightingV3` owns direct light, unshadowed light, shadow visibility, shadow
  loss, and diffuse indirect.
- `ReflectionV3` owns reflection radiance, source ID, confidence, roughness
  filtering, and temporal delta.
- `CompositeV3` owns HDR energy combination and clamps only by explicit policy.
- `CinematicPostV3` owns exposure, tone map, bloom, color grade, sharpen, and
  optional depth of field.
- `Packet / PromotionGate` owns whether any stage can affect default beauty.

### Execution Order

1. Lock motion-stable Lighting V3.
   - Run mouse-jiggle and camera-sweep packets against the five concrete split
     buffers.
   - Compare V3 split buffers to legacy deferred debug terms.
   - Fix only root parity/stability causes, not per-scene brightness tweaks.
   - Completion means lighting split has bounded temporal delta and no missing
     split resources across gallery, kitchen, gym, and concert.

2. Build SceneLocalEnvironmentV3 before ReflectionV3 promotion.
   - Separate visible background from lighting environment and reflection
     fallback.
   - Add explicit modes:
     `enclosed_room`, `open_exterior`, `stage`, `neutral_lab`.
   - Enclosed rooms must not show or reflect unrelated external IBL imagery.
   - Stage/concert scenes should support dark local background with authored
     emissive and spot/area lighting.

3. Build ReflectionV3 as a source-aware resolver.
   - Inputs: material roughness/metallic/normal, scene-local environment,
     local probes, optional SSR/ray query, and history.
   - Outputs: `reflection_radiance`, `reflection_source_id`,
     `reflection_confidence`, `reflection_temporal_delta`, and
     `rejected_reflection_source`.
   - The resolver must explicitly choose between local probe, SSR, ray query,
     and scene-local fallback per pixel.
   - Glossy/metal surfaces must stop jittering through source switches that are
     invisible to the debug packet.

4. Refactor shadows into stable visibility, not hidden lighting math.
   - Keep `shadow_visibility` and `shadow_loss` inspectable.
   - Add per-light shadow ownership and contact-shadow provenance.
   - Stabilize with receiver-plane bias, cascade/projection diagnostics, and
     temporal rejection metrics where applicable.
   - Completion means shadow flicker is caught by packet deltas even when the
     user reports it only during high-FPS mouse movement.

5. Replace ad hoc HDR composition with CompositeV3.
   - Combine base material, direct, indirect, emissive, reflection, atmosphere,
     and transmission through one named pass.
   - Emit `hdr_scene_color`, `energy_clamp_mask`, and `overbright_mask`.
   - The default beauty path should remain unchanged until CompositeV3 can
     reproduce or improve the current fallback on multiple scene families.

6. Add CinematicPostV3 after HDR stability.
   - Filmic tonemap, exposure meter, bloom extract, color grade delta, and
     sharpen should be separate debug views.
   - Post cannot hide unstable lighting/reflection/shadow inputs.
   - Completion means raw HDR and final LDR packets show bounded exposure,
     useful bloom, and no persistent clipping.

7. Cross-family promotion ladder.
   - Promote domains in order:
     material debug readiness -> lighting review -> environment review ->
     reflection review -> composite review -> post review -> default beauty.
   - Required families:
     gallery, kitchen, office, gym, concert, red_room, stadium.
   - Every promotion packet must include static, camera sweep, and mouse jiggle.

### Why This Is Better Than Screenshot Tweaking

- It converts visual quality into inspectable resources, so we can isolate
  whether an artifact is material, shadow, reflection, environment, composite,
  or post.
- It preserves a playable fallback while building risky renderer domains in
  review-only mode.
- It forces motion tests before promotion, which directly targets the flicker
  and shiny-surface jitter failures that static screenshots missed.
- It separates scene-local environment from visible IBLs, so room scenes can
  use ambient/reflection energy without leaking unrelated backgrounds.
- It creates packet evidence that survives compaction and avoids re-litigating
  whether a bug was actually reproduced.

### Immediate Next Slice

The next implementation slice is not ReflectionV3 yet. First finish Lighting V3
motion stability and cross-family parity because ReflectionV3 consumes lighting
and shadow terms.

Required outputs for the next slice:

- motion packet command that captures the five concrete V3 lighting buffers.
- analyzer fields for per-buffer temporal delta under camera sweep and
  mouse-jiggle.
- cross-family packet rows for at least gallery, kitchen, gym, and concert.
- updated ledger evidence showing whether Lighting V3 is promotable or exactly
  which parity/stability blocker remains.

### P0 - Contract and Harness

- Add V3 contract JSON.
- Add V3 plan validator.
- Add frame-report placeholders defaulting to not ready.
- Add packet command skeleton for required V3 views.
- Keep default beauty unchanged.

### P1 - Material Resolve

- Create `FullSceneMaterialResolveV3`.
- Export material attribute debug views.
- Add material channel completeness analyzer.
- Run gallery/material-lab/kitchen packets.

### P2 - Lighting Split

- Split current deferred lighting output into named V3 resources.
- Preserve existing V2 semantic light payload ownership fields.
- Add shadow visibility and shadow loss resources.
- Run gallery/gym/concert/red-room packets.

### P3 - Reflection Resolver

- Replace review-only reflection candidate with `FullSceneReflectionV3`.
- Add confidence/source/temporal debug outputs.
- Run glossy, metallic, glass, and enclosed-room packets.

### P4 - Scene Local Environment

- Add scene mode profile.
- Separate visible background from reflection background.
- Add enclosed-room IBL-leak gates.
- Run kitchen/office/classroom/stadium packets.

### P5 - Cinematic Post

- Add filmic post chain after stable HDR composition.
- Add exposure, bloom, and color-grade diagnostics.
- Run all required scene families.

### P6 - Promotion Decision

- Compare V2 beauty and V3 beauty packets.
- Produce contact sheets and promotion decisions.
- Promote only domains that pass their packet gates.

## Ledger

### L001 - V3 Contract Exists

Status: complete.

Evidence:

- `assets/final_art/full_scene_shader_pipeline_v3_contract.json`.
- this document.

### L002 - V3 Plan Validator Exists

Status: complete.

Evidence:

- `tools/validate_full_scene_shader_pipeline_v3_plan.py`.
- validator passes in local shell.
- validator checks the plan, contract JSON, and runtime placeholder source.

### L003 - V3 Frame Contract Placeholders

Status: complete.

Evidence:

- runtime frame report has `full_scene_shader_pipeline_v3`.
- all domains default to not ready until implemented.
- `default_beauty_affects=false`.
- `status=planned_not_promoted`.
- smoke packet:
  `build/captures/v3_runtime_placeholder_smoke2_20260605`.
- extracted frame report:
  `full_scene_shader_pipeline_v3.schema=cortex.full_scene_shader_pipeline_v3.runtime_report.v1`.
- extracted frame report:
  `required_outputs=9`, `domains=7`, `packet_gate_ready=false`.

### L003A - V3 Packet Skeleton Artifacts

Status: complete.

Evidence:

- `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1`.
- packet skeleton emits `v3_signal.json`.
- packet skeleton emits `v3_stability.json`.
- packet skeleton enforces `status=planned_not_promoted`.
- packet skeleton enforces `default_beauty_affects=false`.
- wrapper smoke packet:
  `build/captures/v3_packet_skeleton_smoke1_20260605`.
- wrapper smoke packet produced:
  `v3_signal.json` and `v3_stability.json`.

### L004 - Material Resolve V3

Status: complete.

Evidence:

- runtime V3 report exposes `material_attributes_ready=true`.
- runtime V3 report exposes `material_attributes_resource_count=6`.
- runtime V3 report exposes `material_attributes_channel_count=17`.
- material domain is produced by `FullSceneMaterialResolveV3`.
- material domain outputs `material_attributes`.
- material domain is backed by:
  `vb_gbuffer_albedo`,
  `vb_gbuffer_normal_roughness`,
  `vb_gbuffer_emissive_metallic`,
  `vb_gbuffer_material_ext0`,
  `vb_gbuffer_material_ext1`, and
  `vb_gbuffer_material_ext2`.
- material debug views include:
  `VB_GBuffer_Albedo`,
  `VB_GBuffer_NormalRoughness`,
  `VB_GBuffer_EmissiveMetallic`,
  `VB_GBuffer_MaterialExt0`,
  `VB_GBuffer_MaterialExt1`,
  `VB_GBuffer_SurfaceClass`,
  `VB_MaterialFamilyPolicy`,
  `VB_ReflectionPolicy`,
  `VB_TemporalPolicy`, and
  `VB_PostSensitivity`.
- smoke packet:
  `build/captures/v3_material_attributes_smoke1_20260605`.
- packet result:
  `material_ready_report_count=6`, failures `0`, warnings `0`.

### L005 - Lighting V3 Split

Status: in progress.

Current producer evidence:

- current producer is `FullSceneLightingV3`.
- current producer mode is opt-in via
  `CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT=1`.
- current producer uses `PSMainV3LightingSplit` in `DeferredLighting.hlsl`.
- current producer writes all five split resources with one fullscreen MRT
  draw instead of five deferred debug-term redraws.
- current producer is still review-only and is not the default beauty path.
- current adapter owner is `VBDeferredLighting`.
- current adapter output is `hdr_color`.
- runtime V3 report exposes `lighting_adapter_ready=true`.
- runtime V3 report exposes `lighting_split_resources_allocated=true`.
- runtime V3 report exposes `lighting_split_resources_ready=true`.
- runtime V3 report exposes `lighting_adapter_signal_count=4`.
- runtime V3 report exposes `lighting_split_resource_count=5`.
- `FullSceneLightingV3` writes:
  `direct_lighting`,
  `direct_lighting_unshadowed`,
  `shadow_visibility`,
  `shadow_loss`, and
  `indirect_lighting`.
- lighting domain backing resources are:
  `hdr_color`,
  `direct_lighting`,
  `direct_lighting_unshadowed`,
  `shadow_visibility`,
  `shadow_loss`, and
  `indirect_lighting`.
- lighting domain is `ready=true`.
- lighting domain producer is `FullSceneLightingV3`.
- lighting domain output resource is `lighting_split`.
- lighting domain promotion state is `producer`.
- adapter debug views include:
  `VB_DeferredDirectLight`,
  `VB_DeferredDirectLightUnshadowed`,
  `VB_DeferredDirectLightShadowLoss`,
  `VB_DeferredShadowFactor`, and
  `VB_DeferredAmbientIBL`.
- concrete V3 split debug modes are wired for direct MRT inspection:
  `VB_V3DirectLighting`,
  `VB_V3DirectLightingUnshadowed`,
  `VB_V3ShadowVisibility`,
  `VB_V3ShadowLoss`, and
  `VB_V3IndirectLighting`.
- packet view names for the concrete split buffers are:
  `v3_direct_lighting`,
  `v3_direct_lighting_unshadowed`,
  `v3_shadow_visibility`,
  `v3_shadow_loss`, and
  `v3_indirect_lighting`.
- smoke packet:
  `build/captures/v3_lighting_split_mrt_smoke3_strict_20260605`.
- packet result:
  `lighting_adapter_ready_report_count=6`,
  `lighting_split_allocated_report_count=6`,
  `lighting_split_ready_report_count=6`,
  `lighting_signal_metrics_ready=true`,
  failures `0`, warnings `0`.
- concrete split-buffer packet:
  `build/captures/v3_lighting_split_concrete_debug_smoke1_20260605`.
- concrete split-buffer packet result:
  `report_count=16`,
  `lighting_split_ready_report_count=16`,
  `full_scene_lighting_v3_executed_report_count=16`,
  `lighting_signal_metrics_ready=true`,
  failures `0`, warnings `0`.
- pass evidence:
  `FullSceneLightingV3.executed=true`,
  `FullSceneLightingV3.draw_count=1`,
  `FullSceneLightingV3.writes=direct_lighting,direct_lighting_unshadowed,shadow_visibility,shadow_loss,indirect_lighting`.
- signal evidence:
  `direct_light.mean_luma=0.426794`,
  `direct_light_unshadowed.mean_luma=0.457842`,
  `direct_light_shadow_loss.mean_luma=0.223022`,
  `shadow_factor.mean_luma=0.350937`, and
  `ambient_ibl.mean_luma=0.196339`.
- concrete split signal evidence:
  `v3_direct_lighting.mean_luma=0.431061`,
  `v3_direct_lighting_unshadowed.mean_luma=0.470903`,
  `v3_shadow_visibility.mean_luma=0.350934`,
  `v3_shadow_loss.mean_luma=0.175254`, and
  `v3_indirect_lighting.mean_luma=0.193502`.
- V3 lighting motion analyzer:
  `tools/analyze_full_scene_shader_v3_lighting_motion.py`.
- V3 lighting motion matrix runner:
  `tools/run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1`.
- the matrix runner supports `-SummarizeExisting` to recover completed packet
  captures when the outer matrix wrapper is interrupted before aggregation.
- the V3 packet runner now emits `v3_lighting_motion.json` and
  `v3_lighting_motion.md` whenever `CaptureSequenceCount >= 2`.
- no-stress packet wiring exists for clean family-only matrix rows:
  `-NoStressScene` on V3/V2 packet runners.
- packet family wiring now includes the full required V3 family set:
  `gallery`, `kitchen`, `office`, `gym`, `concert`, `red_room`, and
  `stadium`.
- gallery-only mouse-jiggle matrix smoke:
  `build/captures/v3_lighting_motion_matrix_gallery_smoke3_20260605`.
- gallery-only mouse-jiggle matrix result:
  `rows=5`, failures `0`.
- gallery-only mouse-jiggle stability result:
  `report_count=11`,
  `lighting_split_ready_report_count=11`,
  `full_scene_lighting_v3_executed_report_count=11`,
  `lighting_signal_metrics_ready=true`,
  failures `0`, warnings `0`.
- gallery-only mouse-jiggle motion evidence:
  `v3_direct_lighting.delta=0.02786466`,
  `v3_direct_lighting_unshadowed.delta=0.02834359`,
  `v3_shadow_visibility.delta=0.01748130`,
  `v3_shadow_loss.delta=0.01939935`,
  `v3_indirect_lighting.delta=0.00789734`.
- gallery-only V3/legacy motion ratios:
  direct `1.045`,
  unshadowed `1.077`,
  shadow visibility `1.305`,
  shadow loss `0.810`,
  indirect `0.257`.
- first cross-family V3 lighting motion matrix probe:
  `build/captures/v3_lighting_motion_matrix_cross_family_probe2_20260605`.
- cross-family probe scope:
  families `gallery,kitchen,gym,concert`;
  modes `mouse_jitter,camera_sweep`;
  sequence count `2`;
  views `beauty`, five legacy lighting terms, and five concrete V3 lighting
  buffers.
- cross-family aggregate result:
  `rows=40`, failures `0`, warnings `1`.
- per-mode V3 stability:
  - `mouse_jitter`: `report_count=44`,
    `lighting_split_ready_report_count=44`,
    `full_scene_lighting_v3_executed_report_count=44`,
    `lighting_signal_metrics_ready=true`,
    failures `0`, warnings `0`.
  - `camera_sweep`: `report_count=44`,
    `lighting_split_ready_report_count=44`,
    `full_scene_lighting_v3_executed_report_count=44`,
    `lighting_signal_metrics_ready=true`,
    failures `0`, warnings `0`.
- current cross-family motion blocker:
  `mouse_jitter: concert/v3_indirect_lighting` has motion delta `0.00395094`,
  legacy `ambient_ibl` delta `0.00115572`, V3/legacy ratio `3.419`.
- indirect parity fix:
  `PSMainV3LightingSplit` now uses the same scene-local ambient/probe
  contract as the legacy `ambient_ibl` path:
  probe weight selection, interior no-environment gate, box-projected probe
  direction, diffuse/specular local probe scaling, reflection-footprint mip
  floor, specular ceiling, split AO, local fill, and sheen.
- indirect parity fix also removes emissive from `indirect_lighting`; emissive
  belongs in the future V3 composite domain rather than the ambient/indirect
  debug resource.
- targeted concert mouse-jiggle parity probe:
  `build/captures/v3_lighting_concert_indirect_parity_probe2_20260605`.
- targeted concert result:
  `v3_indirect_lighting.delta=0.00115572`,
  legacy `ambient_ibl.delta=0.00115572`,
  V3/legacy ratio `1.000`,
  failures `0`, warnings `0`.
- post-fix cross-family matrix:
  `build/captures/v3_lighting_motion_matrix_cross_family_after_indirect_fix1_20260605`.
- post-fix cross-family result:
  families `gallery,kitchen,gym,concert`,
  modes `mouse_jitter,camera_sweep`,
  `rows=40`, failures `0`, warnings `0`.
- post-fix per-mode V3 stability:
  - `mouse_jitter`: `report_count=44`,
    `lighting_split_ready_report_count=44`,
    `full_scene_lighting_v3_executed_report_count=44`,
    `lighting_signal_metrics_ready=true`,
    failures `0`, warnings `0`.
  - `camera_sweep`: `report_count=44`,
    `lighting_split_ready_report_count=44`,
    `full_scene_lighting_v3_executed_report_count=44`,
    `lighting_signal_metrics_ready=true`,
    failures `0`, warnings `0`.
- new-family packet wiring smoke:
  `build/captures/v3_lighting_motion_new_families_smoke1_20260605`.
- new-family smoke scope:
  families `red_room,stadium`;
  mode `mouse_jitter`;
  sequence count `2`;
  views `beauty`, five legacy lighting terms, and five concrete V3 lighting
  buffers.
- new-family smoke matrix result:
  `rows=10`, failures `0`, warnings `0`.
- new-family debug-view metrics:
  captured views `22`, measured views `22`, failures `0`.
- new-family V3/legacy mouse-jiggle ratios:
  - `red_room`: direct `1.062`, unshadowed `1.062`, shadow visibility `0.715`,
    shadow loss `0.193`, indirect `0.568`.
  - `stadium`: direct `1.031`, unshadowed `1.030`, shadow visibility `1.091`,
    shadow loss `0.540`, indirect `1.000`.

Required next evidence for completion/promotion:

- close parity gaps between `PSMainV3LightingSplit` and the current default
  deferred beauty lighting path, especially local probe and environment terms.
- repeat the V3 lighting motion matrix with promotion-grade frame counts and
  include the full required family set now that red-room/stadium are
  packet-wired.
- compare V3 split outputs against the legacy deferred terms under those
  motion and cross-family packets, not just the static/gallery smoke.
- keep default beauty unchanged until the consumer/composite path and packet
  gates prove promotion quality.

### L006 - Reflection V3 Resolver

Status: pending.

### L007 - Scene Local Environment V3

Status: in progress.

Current evidence-domain implementation:

- runtime V3 frame report now exposes:
  - `scene_local_environment_ready`.
  - `scene_local_environment_mode`.
  - `scene_local_environment_channel_count`.
- `SceneLocalEnvironmentV3` is now a real V3 domain entry rather than a
  planned-only placeholder.
- current producer is `SceneLocalEnvironmentV3`.
- current output resource is the logical ownership contract
  `scene_local_environment`.
- current debug view is `environment_mode`.
- current mode compiler derives:
  - `enclosed_room` for enclosed local rooms.
  - `stage` for enclosed concert/stage rigs.
  - `neutral_lab` for gallery/lab style profiles.
  - `open_exterior` for non-enclosed exterior scenes.
- current readiness channels are:
  `environment_mode`, `ambient_lighting`, `visible_background`,
  `reflection_background`, and `atmosphere`.
- current backing contracts are:
  `scene_visual_contract`, `environment_state`,
  `scene_lighting_balance_policy`, `local_reflection_probe_rig`, and
  `scene_post_exposure_policy`.
- the placeholder packet analyzer now allows and validates the `environment`
  domain when these ownership channels are ready.
- first runtime smoke:
  `build/captures/v3_scene_local_environment_contract_smoke3_20260605`.
- first runtime smoke result:
  `report_count=16`,
  `scene_local_environment_ready_report_count=16`,
  `lighting_split_ready_report_count=16`,
  `full_scene_lighting_v3_executed_report_count=16`,
  failures `0`, warnings `0`.
- gallery smoke environment evidence:
  `scene_local_environment_mode=neutral_lab`,
  `scene_local_environment_channel_count=5`,
  ready channels `ambient_lighting_owned`, `visible_background_owned`,
  `reflection_background_owned`, and `atmosphere_owned`.

Important limitation:

- This slice does not add a new environment shader or change default beauty.
  It creates the runtime contract that ReflectionV3 and CompositeV3 can consume
  without relying on visible IBL/background leakage.

### L008 - Cinematic Post V3

Status: pending.

### L009 - Cross-Family Packet Evidence

Status: pending.

Required evidence:

- gallery, kitchen, office, gym, concert, red_room, and stadium packets.
- no missing required V3 debug views.
- no missing required V3 frame-report fields.

### L010 - Default Beauty Promotion

Status: pending.

Required evidence:

- promotion decision document.
- before/after contact sheets.
- passing stability and signal gates.
- user acceptance that the generated visual quality is good enough to promote.

## Current Stopping Position

- V3 is planned, contract-grounded, and frame-report visible as not promoted.
- V2 remains the active renderer path.
- Default beauty remains unchanged.
- Latest V3 placeholder packet:
  `build/captures/v3_material_attributes_smoke1_20260605`.
- First real V3 domain is now instrumented:
  `FullSceneMaterialResolveV3 -> material_attributes`.
- Current lighting domain is producer-ready under the opt-in V3 split flag:
  `FullSceneLightingV3 -> lighting_split`.
- Latest V3 lighting packet:
  `build/captures/v3_lighting_split_producer_smoke1_20260605`.
- Next safe implementation slice is replacing the debug-term redraw producer
  with a direct split-output lighting shader/pass and then adding lighting
  signal gates.
