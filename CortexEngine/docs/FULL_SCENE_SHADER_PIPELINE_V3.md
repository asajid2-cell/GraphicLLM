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
       -> lighting_energy_budget
       -> shadow_source_attribution
  -> FullSceneReflectionV3
       -> reflection_radiance
       -> reflection_confidence
       -> reflection_source_id
       -> reflection_rejected_source_mask
       -> reflection_temporal_delta
       -> reflection_ssr_source_signal
       -> reflection_rt_source_signal
       -> reflection_source_suppression
  -> FullSceneReflectionHistoryV3
       -> reflection_history_v3_curr
       -> reflection_history_v3_prev
       -> reflection_history_v3_prev_source_id
       -> reflection_history_v3_validity
       -> reflection_history_v3_rejection
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
    contract.RecordResource("reflection_rt_source_signal", reflections.rtSourceSignal);
    contract.RecordResource("reflection_source_suppression", reflections.sourceSuppression);
    contract.RecordResource("reflection_history_v3_curr", reflections.historyCurr);
    contract.RecordResource("reflection_history_v3_prev", reflections.historyPrev);
    contract.RecordResource("reflection_history_v3_prev_source_id", reflections.historyPrevSourceId);
    contract.RecordResource("reflection_history_v3_validity", reflections.historyValidity);
    contract.RecordResource("reflection_history_v3_rejection", reflections.historyRejection);

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
- raw SSR and RT source-signal views.
- source-suppression view that separates history suppression, material
  suppression, roughness, and metallic.
- history carryover for previous source ID so source switching is measurable
  across frames.
- history rejection view with source-switch, disocclusion, high-motion, and
  missing-history/debt lanes.

Admission gates:

- confidence is nonzero on glossy/metallic stress scenes.
- temporal delta is bounded under camera motion.
- rough surfaces do not inherit sharp IBL detail.
- enclosed rooms do not reflect unrelated exterior IBLs.
- source switches and disocclusion are visible in
  `reflection_history_v3_rejection` before source fusion is allowed to consume
  history validity.
- auto SSR/RT admission reads previous source ID, history validity, and history
  rejection so smooth/metallic pixels do not flip sources on marginal
  confidence while forced debug source modes remain inspectable.
- auto SSR/RT admission reads material normal/roughness and emissive/metallic
  so rough surfaces damp sharp reflection sources while smooth/metallic
  surfaces keep stronger source eligibility.
- auto SSR/RT admission writes `reflection_source_suppression` so packets can
  distinguish history-driven rejection from material-policy rejection.

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
- `promotion_decision.json`
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
lighting_energy_budget,
shadow_source_attribution,
reflection_radiance,
reflection_confidence,
reflection_source_id,
reflection_rejected_source_mask,
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
  `shadow_loss`, `indirect_lighting`, `lighting_energy_budget`, and
  `shadow_source_attribution`.
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
  filtering, temporal delta, source-history carryover, source-suppression
  diagnostics, and rejection reasons.
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
     `reflection_confidence`, `reflection_rejected_source_mask`, and
     `reflection_temporal_delta`.
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
- Add confidence/source/temporal/history-rejection debug outputs.
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
  `FullSceneLightingV3.writes=direct_lighting,direct_lighting_unshadowed,shadow_visibility,shadow_loss,indirect_lighting,lighting_energy_budget,shadow_source_attribution`.
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

Status: in progress.

Current evidence-domain implementation:

- runtime V3 frame report now exposes:
  - `reflection_v3_ready`.
  - `reflection_radiance_ready`.
  - `reflection_confidence_ready`.
  - `reflection_source_id_ready`.
  - `reflection_temporal_delta_ready`.
  - `reflection_ssr_source_signal_ready`.
  - `reflection_v3_source_contract`.
  - `reflection_v3_channel_count`.
  - `reflection_v3_source_count`.
- `FullSceneReflectionV3` is now a real V3 domain entry rather than a
  planned-only placeholder.
- current producer is `FullSceneReflectionV3`.
- current output resource is the logical ownership contract
  `reflection_radiance`.
- current debug view is `reflection_confidence`.
- current source contract chooses the first ready source among:
  `local_probe`, `ray_query_reflection`, `screen_space_reflection`, and
  `scene_local_environment`.
- current readiness channels are:
  `reflection_radiance`, `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, `reflection_temporal_delta`, and
  `reflection_ssr_source_signal`.
- current temporal-delta ownership is source-aware:
  - scene-local probe/environment reflection sources can own a deterministic
    `reflection_temporal_delta_scene_local_bound` channel without RT history.
  - RT/SSR or other history-sensitive reflection sources must own a
    `reflection_temporal_delta_history_bound` channel through reflection
    history or TAA history evidence.
- current backing contracts are:
  `scene_local_environment`, `scene_visual_reflection_owner`,
  `material_reflection_policy`, `local_reflection_radiance`, and
  `rt_reflection_signal_history`.
- the placeholder packet analyzer now allows and validates the `reflection`
  domain when these five channels are owned and `SceneLocalEnvironmentV3` is
  already ready.
- first runtime smoke:
  `build/captures/v3_reflection_contract_smoke1_20260605`.
- first runtime smoke result:
  `report_count=16`,
  `reflection_v3_ready_report_count=16`,
  `scene_local_environment_ready_report_count=16`,
  `lighting_split_ready_report_count=16`,
  failures `0`, warnings `0`.
- gallery smoke reflection evidence:
  `reflection_v3_source_contract=local_probe`,
  `reflection_v3_channel_count=4`,
  `reflection_v3_source_count=4`,
  all four channel flags ready.

Important limitation:

- The source-aware temporal-delta rule is deliberately a contract fix, not a
  scene workaround: enclosed scenes with stable local reflection probes should
  not be blocked on RT reflection history, while dynamic reflection paths still
  require history/TAA evidence.

2026-06-06 RT/ray-query source input update:

- `FullSceneReflectionV3` now has a concrete RT/ray-query resolver input edge.
- The resolver shader samples:
  - `local_reflection_radiance` at `t0`.
  - `ssr_color` at `t1`.
  - `rt_reflection` at `t2`.
- The render graph records `rt_reflection` as a read by
  `FullSceneReflectionV3`.
- Source overrides now include:
  - `0`/auto.
  - `1`/local.
  - `2`/SSR.
  - `3`/RT/ray-query.
  - `4`/environment.
  - `255`/none.
- `reflection_source_id.r` encodes RT as `0.75`.
- `forced_ray_query_reflection` is accepted by the analyzer as a valid
  `reflection_v3_source_contract`.

Required evidence before calling this source qualitatively useful:

- forced RT packet proves either nonblank RT radiance or explicit unavailable
  rejection through confidence/source/rejected/temporal channels.
- auto packet proves RT does not destabilize the normal local/SSR/environment
  policy.
- mouse-jitter packet proves the selected reflection source is stable enough
  for smooth and metallic surfaces.

First packet evidence:

- forced RT static packet:
  `build/captures/v3_reflection_rt_input_forced_static_smoke1_20260606`.
  - status `review_packet_passed`.
- source contract `forced_ray_query_reflection`.
- `reflection_radiance.mean_luma=0.0547562`,
    `nonblack_ratio=0.3815104`.
  - `reflection_confidence.mean_luma=0.3718455`,
    `nonblack_ratio=0.3947656`.
- auto static packet:
  `build/captures/v3_reflection_rt_input_auto_static_smoke1_20260606`.
  - status `review_packet_passed`.
  - source contract remained `local_probe`.
- auto mouse-jitter packet:
  `build/captures/v3_reflection_rt_input_auto_motion_smoke1_20260606`.
  - status `review_packet_passed`.
  - source contract remained `local_probe`.
  - `reflection_radiance.delta=0.0048307`, active `0.0309180`.
  - `reflection_source_id.delta=0.0039639`, active `0.0126432`.
  - `reflection_temporal_delta.delta=0.0`, active `0.0`.

Interpretation:

- RT is now wired and force-selectable with nonblank signal in the gallery
  packet.
- Auto policy does not yet admit RT in this row because local probe remains the
  stable choice.
- The next ReflectionV3 step is broader source-quality/stability work across
  smooth/metallic stress views, not default-beauty promotion.

2026-06-06 RT source-signal update:

- Added `reflection_rt_source_signal` as a concrete ReflectionV3 output.
- Added debug mode `74`, `FullSceneReflectionV3RTSourceSignal`.
- Added V3 frame-report flag `reflection_rt_source_signal_ready`.
- Reflection readiness now requires seven channels: radiance, confidence,
  source ID, rejected-source mask, temporal delta, SSR source signal, and RT
  source signal.
- Packet view filters now include `reflection_rt_source_signal` so forced RT
  and auto motion packets can inspect raw RT source quality directly.

2026-06-06 ReflectionHistoryV3 seed update:

- Added `FullSceneReflectionHistoryV3` as a separate fullscreen pass instead
  of expanding `FullSceneReflectionV3` beyond the 8-MRT hardware limit.
- Corrected the existing `FullSceneReflectionV3` PSO target count to `7` so
  the PSO matches the seven resolver MRT outputs.
- Added concrete history resources:
  - `reflection_history_v3_curr`.
  - `reflection_history_v3_prev`.
  - `reflection_history_v3_validity`.
- Added debug modes:
  - mode `75`: `FullSceneReflectionHistoryV3Curr`.
  - mode `76`: `FullSceneReflectionHistoryV3Validity`.
  - mode `77`: `FullSceneReflectionHistoryV3Prev`.
- Added V3 frame-report flags:
  - `reflection_history_v3_ready`.
  - `reflection_history_v3_prev_ready`.
  - `reflection_history_v3_validity_ready`.
- Reflection readiness now requires ten channels: the seven resolver outputs
  plus current history, previous history, and history validity.
- `FullSceneReflectionHistoryV3Copy` copies current history into previous
  history after the history pass so the next frame has a stable read target.
- Follow-up reprojection validity now makes `FullSceneReflectionHistoryV3`
  read `depth`, a normal/roughness resource, and `velocity`.
- The pass samples previous history at `uv + velocity + taa_jitter_delta` and
  writes `reflection_history_v3_validity` as active source, source class,
  reusable reprojected history, and rejection/debt strength.
- The validity resource is still advisory. It does not yet loosen ReflectionV3
  auto source selection until motion packets prove the confidence contract.

Concrete resolver producer update, 2026-06-06:

- added `assets/shaders/FullSceneReflectionResolverV3.hlsl`.
- added concrete ReflectionV3 render targets:
  `reflection_radiance`, `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, and `reflection_temporal_delta`.
- `FullSceneReflectionV3` now runs as a render-graph pass after
  `LocalReflectionRadiance`.
- the pass reads `local_reflection_radiance` and writes all five concrete
  ReflectionV3 resources.
- `FullSceneCompositeV3` now reads `reflection_radiance` from the resolver
  when it is scheduled, falling back to `local_reflection_radiance` only when
  the resolver is unavailable.
- added debug views:
  - mode `68`: `reflection_radiance`.
  - mode `69`: `reflection_confidence`.
  - mode `70`: `reflection_source_id`.
  - mode `71`: `reflection_rejected_source_mask`.
  - mode `72`: `reflection_temporal_delta`.
  - mode `73`: `reflection_ssr_source_signal`.
- the runtime required-output list now matches the machine contract and
  includes all five ReflectionV3 outputs.
- static packet:
  `build/captures/v3_reflection_resolver_static_smoke2_20260606`.
  - reports: `23`.
  - promotion status: `review_packet_passed`.
- mouse-jitter packet:
  `build/captures/v3_reflection_resolver_motion_smoke1_20260606`.
  - reports: `23`.
  - V3 motion analyzer measured `17` view sequences.
  - promotion status: `review_packet_passed`.
- direct frame-report proof from the mouse-jitter packet:
  - required outputs: `14`.
  - `reflection_v3_ready=true`.
  - `reflection_v3_channel_count=5`.
  - `reflection_v3_source_contract=local_probe`.
  - `FullSceneReflectionV3.executed=true`.
  - `FullSceneReflectionV3.reads=local_reflection_radiance`.
  - `FullSceneReflectionV3.writes=reflection_radiance`,
    `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, `reflection_temporal_delta`, and
  `reflection_ssr_source_signal`.
  - `FullSceneCompositeV3.reads=direct_lighting`, `indirect_lighting`,
    `shadow_visibility`, `hdr_color`, and `reflection_radiance`.
  - `FullSceneCompositeV3.writes=candidate_hdr_scene_color`.
- reflection mouse-jitter rows:
  - `reflection_radiance`: mean abs luma delta `0.0024393373`,
    active delta ratio `0.0140386285`.
  - `reflection_confidence`: mean abs luma delta `0.0003614430`,
    active delta ratio `0.0051226128`.
  - `reflection_source_id`: mean abs luma delta `0.0004442693`,
    active delta ratio `0.0057389323`.
  - `reflection_rejected_source_mask`: mean abs luma delta `0.0001864565`,
    active delta ratio `0.0017708333`.
  - `reflection_temporal_delta`: mean abs luma delta `0.0001864565`,
    active delta ratio `0.0017708333`.

Remaining limitation:

- This is a concrete resolver resource/pass slice, but it still derives from
  the current local reflection radiance source. SSR/RT/environment source
  competition, roughness-aware source selection, and real temporal history
  admission remain future ReflectionV3 work.
- Default beauty remains unchanged and not promoted.

Source-suppression diagnostic update, 2026-06-06:

- Added `reflection_source_suppression` as a concrete ReflectionV3 output.
- Added debug mode `79`, `FullSceneReflectionV3SourceSuppression`.
- The resource stores history/source-switch suppression in `R`,
  material/roughness suppression in `G`, roughness in `B`, and metallic in
  `A`.
- Reflection readiness now requires `13` channels including the new
  suppression resource.
- Frame reports now expose `reflection_source_suppression_ready`.
- Static packet:
  `build/captures/v3_reflection_source_suppression_static_smoke1_20260606`.
- Mouse-jitter packet:
  `build/captures/v3_reflection_source_suppression_motion_smoke1_20260606`.
- Motion packet proof:
  - `reflection_v3_ready=true`.
  - `reflection_v3_channel_count=13`.
  - `reflection_source_suppression_ready=true`.
  - `FullSceneReflectionV3.writes` includes
    `reflection_source_suppression`.
- Default beauty remains unchanged and not promoted.

Source-policy admission update, 2026-06-06:

- `FullSceneReflectionResolverV3` now admits either scene-local reflection
  radiance or scene-local environment fallback.
- `FrameConstants.localProbeParams.w` carries a ReflectionV3 source override:
  `0` auto, `1` local, `4` environment, `255` none.
- `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE` supports:
  `auto`, `local`, `environment`, and `none`.
- frame reports can now name forced review contracts:
  `forced_scene_local_radiance`, `forced_scene_local_environment`,
  `forced_none`, and `forced_unknown`.
- the V3 analyzer accepts forced local/environment contracts and still rejects
  invalid source ownership.
- source ID debug output now exposes selected source class, confidence, and
  override signal.
- rejected-source mask now exposes missing/rejected local radiance,
  missing/rejected environment fallback, and not-yet-admitted dynamic SSR/RT
  source debt.
- static auto packet:
  `build/captures/v3_reflection_source_policy_auto_static_smoke1_20260606`.
  - reports: `23`.
  - `reflection_v3_source_contract=local_probe`.
  - promotion status: `review_packet_passed`.
- static forced-environment packet:
  `build/captures/v3_reflection_source_policy_environment_static_smoke1_20260606`.
  - reports: `23`.
  - `reflection_v3_source_contract=forced_scene_local_environment`.
  - promotion status: `review_packet_passed`.
- auto mouse-jitter packet:
  `build/captures/v3_reflection_source_policy_auto_motion_smoke1_20260606`.
  - reports: `23`.
  - V3 lighting/reflection motion measured `17` view sequences.
  - promotion status: `review_packet_passed`.
- auto mouse-jitter rows:
  - `reflection_radiance`: mean abs luma delta `0.0024278814`,
    active delta ratio `0.0138726128`.
  - `reflection_confidence`: mean abs luma delta `0.0007756502`,
    active delta ratio `0.0068261719`.
  - `reflection_source_id`: mean abs luma delta `0.0006937146`,
    active delta ratio `0.0058745660`.
  - `reflection_rejected_source_mask`: mean abs luma delta `0.0001864565`,
    active delta ratio `0.0017708333`.
  - `reflection_temporal_delta`: mean abs luma delta `0.0`,
    active delta ratio `0.0`.

SSR input admission update, 2026-06-06:

- `FullSceneReflectionV3` now reads `ssr_color` alongside
  `local_reflection_radiance`.
- `FullSceneReflectionResolverV3` samples `g_SSRReflection : t1`.
- `FrameConstants.localProbeParams.w` supports a screen-space override:
  `0` auto, `1` local, `2` SSR, `4` environment, `255` none.
- `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE` supports:
  `auto`, `local`, `ssr`, `screen_space`, `environment`, and `none`.
- frame reports can now name `forced_screen_space_reflection`.
- the V3 analyzer fails if `FullSceneReflectionV3` stops reading `ssr_color`.
- auto policy admits SSR only when it has nonzero radiance and confidence high
  enough to beat scene-local radiance; otherwise local radiance and
  environment remain the stable fallback path.
- auto static packet:
  `build/captures/v3_reflection_ssr_input_auto_static_smoke1_20260606`.
  - reports: `23`.
  - promotion status: `review_packet_passed`.
- auto mouse-jitter packet:
  `build/captures/v3_reflection_ssr_input_auto_motion_smoke1_20260606`.
  - reports: `23`.
  - V3 lighting/reflection motion measured `17` view sequences.
  - promotion status: `review_packet_passed`.
- direct report proof:
  - `SSR.executed=true`.
  - `SSR.writes=ssr_color`.
  - `FullSceneReflectionV3.reads=local_reflection_radiance` and `ssr_color`.
  - `FullSceneCompositeV3.reads=reflection_radiance`.

Forced-SSR limitation:

- forced SSR stress packet:
  `build/captures/v3_reflection_source_policy_ssr_stress_static_smoke1_20260606`.
- the packet proved SSR pass execution and resolver consumption, but failed
  signal gates because forced SSR produced blank `reflection_radiance` and
  `reflection_confidence` in that stress view.
- Treat this as remaining SSR source-quality debt. Do not promote SSR as a
  visually reliable reflection source until its source packet produces stable
  nonblank radiance/confidence.

SSR producer refinement update, 2026-06-06:

- `SSR.hlsl` now uses a refined view-space raymarch:
  - `96` steps.
  - smaller near-origin skip.
  - reduced minimum hit/separation gates.
  - crossing refinement with 5 binary-search steps.
  - source-confidence alpha using reflection weight, distance fade, screen-edge
    fade, and SSR strength.
- forced SSR static packet now passes:
  `build/captures/v3_ssr_producer_refined_forced_static_smoke1_20260606`.
- auto static packet now passes with stronger source diagnostic signal:
  `build/captures/v3_ssr_producer_refined_auto_static_smoke1_20260606`.
- auto mouse-jitter packet passes:
  `build/captures/v3_ssr_producer_refined_auto_motion_smoke1_20260606`.
- producer improvement:
  - forced SSR `reflection_radiance.nonblack_ratio` improved from `0.0820681`
    to `0.3997233`.
  - forced SSR `reflection_confidence.nonblack_ratio` improved from
    `0.0337229` to `0.3876128`.
  - auto `reflection_ssr_source_signal.nonblack_ratio` is now `0.4163715`.
- auto source contract remains `local_probe`; this is intentional until SSR
  motion/temporal confidence is stable enough to win source selection.

Remaining limitation:

- SSR is now a real resolver input with stronger producer signal, but it is
  still more motion-sensitive than scene-local radiance.
- RT/ray-query reflection is still not a resolver input.
  Remaining source-fusion work should add SSR temporal confidence/history
  stabilization, then add RT/ray-query ownership with the same source-ID,
  confidence, rejection-mask, and temporal-history evidence.

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

### L008 - Composite V3 and Cinematic Post V3

Status: in progress.

Current evidence-domain implementation:

- runtime V3 frame report now exposes:
  - `composite_v3_ready`.
  - `hdr_scene_color_ready`.
  - `composite_inputs_ready`.
  - `composite_energy_policy_ready`.
  - `composite_overbright_diagnostics_ready`.
  - `composite_v3_producer`.
  - `composite_v3_channel_count`.
  - `cinematic_post_v3_ready`.
  - `ldr_cinematic_output_ready`.
  - `exposure_meter_ready`.
  - `bloom_extract_ready`.
  - `color_grade_ready`.
  - `tone_map_ready`.
  - `cinematic_post_v3_producer`.
  - `cinematic_post_v3_channel_count`.
- `FullSceneCompositeV3Adapter` is now a real V3 domain entry.
- current composite output resource is the logical ownership contract
  `hdr_scene_color`, backed by the current `hdr_color` resource.
- current composite debug view is `hdr_scene_color`.
- current composite readiness channels are:
  `hdr_scene_color`, `composite_inputs`, `energy_clamp_policy`, and
  `overbright_diagnostics`.
- current composite backing contracts are:
  `hdr_color`, `material_attributes`, `lighting_split`,
  `reflection_radiance`, and `scene_local_environment`.
- `CinematicPostV3Adapter` is now a real V3 domain entry.
- current post output resource is the logical ownership contract
  `ldr_cinematic_output`, backed by the current `PostProcess -> back_buffer`
  path.
- current post debug view is `exposure_meter`.
- current post readiness channels are:
  `ldr_cinematic_output`, `exposure_meter`, `bloom_extract`,
  `color_grade_delta`, and `tone_map`.
- the placeholder packet analyzer now allows and validates the `composite`
  and `cinematic_post` domains only after their upstream V3 domains are ready.
- first runtime smoke:
  `build/captures/v3_composite_post_contract_smoke1_20260605`.
- first runtime smoke result:
  `report_count=16`,
  `composite_v3_ready_report_count=16`,
  `cinematic_post_v3_ready_report_count=16`,
  `reflection_v3_ready_report_count=16`,
  failures `0`, warnings `0`.
- gallery smoke composite evidence:
  `composite_v3_producer=FullSceneCompositeV3Adapter`,
  `composite_v3_channel_count=4`,
  all four composite channel flags ready.
- gallery smoke post evidence:
  `cinematic_post_v3_producer=CinematicPostV3Adapter`,
  `cinematic_post_v3_channel_count=5`,
  all five post channel flags ready.

Important limitation:

- This slice does not promote default beauty and does not replace the current
  shader composite/post path. It creates named V3 ownership around the existing
  HDR and LDR outputs so a later candidate beauty path can be gated rather than
  guessed from screenshots.

### L009 - Cross-Family Packet Evidence

Status: in progress.

Current promotion-gate implementation:

- added `tools/build_full_scene_shader_v3_promotion_decision.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now emits:
  `promotion_decision.json` and `promotion_decision.md`.
- promotion decision schema:
  `cortex.full_scene_shader_pipeline_v3.promotion_decision.v1`.
- current statuses:
  - `blocked` when required artifacts, frame reports, domains, or analyzer
    gates fail.
  - `review_packet_passed` when the packet is internally coherent but does
    not yet cover all required families/motion modes.
  - `candidate_ready_not_promoted` when full required family and motion
    evidence is present without failures or warnings.
- the gate intentionally keeps `default_beauty_promotable=false`; promotion
  still requires a separate explicit default-beauty decision.
- required ready domains for the gate:
  `material`, `lighting`, `environment`, `reflection`, `composite`, and
  `cinematic_post`.
- full coverage target remains:
  families `gallery,kitchen,office,gym,concert,red_room,stadium` and motion
  modes `static,mouse_jitter,camera_sweep`.
- added `tools/summarize_full_scene_shader_v3_frame_reports.py` so partial
  packet runs that hit a renderer/GPU fault can still produce explicit V3
  frame-report evidence instead of losing the useful contract signal.
- source-aware reflection temporal evidence:
  `build/captures/v3_promotion_decision_gate_smoke2_20260605/v3_frame_report_summary.md`
  and
  `build/captures/v3_promotion_decision_gate_smoke3_20260605/v3_frame_report_summary.md`.
  Both attempts show every emitted report has `reflection` and `composite`
  ready, and every emitted report uses
  `reflection_temporal_delta_scene_local_bound`.
- current remaining blocker in those attempts is a DX12 device removal in one
  kitchen row, which prevents one `cinematic_post` report from completing. This
  is separate from the old ReflectionV3 temporal-delta contract blocker.

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
### ReflectionV3 Semantic Material Input - 2026-06-06

Implemented:

- `FullSceneReflectionV3` now reads `vb_gbuffer_material_ext2` in addition to
  normal/roughness and emissive/metallic.
- `FullSceneReflectionResolverV3.hlsl` decodes surface class and named scene
  material class and applies class-specific source floors for water, glass,
  mirror, conductor, and wet surfaces.
- Reflection resolver material payload reads now use pixel-exact `Load()`
  instead of linear sampling. This applies to `FullSceneReflectionV3` and
  `LocalReflectionRadiance`, preventing categorical material classes and
  per-pixel roughness/metallic from blending across object edges during camera
  motion.
- The V3 JSON contract, runtime readiness, analyzer, and plan validator now
  require `FullSceneReflectionV3` to read `vb_gbuffer_material_ext2`.
- `tools/run_reflection_v3_material_stress_packet.ps1` captures
  `surface_class` and `material_family` by default.
- `tools/analyze_reflection_v3_material_stress.py` reports material-policy
  class coverage from frame reports, including smooth-class coverage for
  glass/water/metal targets.

Validation:

- `python -m py_compile tools/analyze_reflection_v3_material_stress.py
  tools/analyze_full_scene_shader_v3_placeholders.py
  tools/validate_full_scene_shader_pipeline_v3_plan.py`
- `python tools/validate_full_scene_shader_pipeline_v3_plan.py`
- native `CortexEngine` build passed.
- water stress packet:
  `build/captures/reflection_v3_material_policy_water_after_pixel_loads_20260606`.
- metal/glass stress packet:
  `build/captures/reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606`.

Result:

- metal/glass stress has no material-stress warnings after pixel-exact material
  reads.
- water still reports `smooth_target_has_high_roughness_signal` with smooth
  class coverage `31/66`; this is now a roughness/source-policy problem, not a
  missing semantic input problem.

Update:

- The water warning was a diagnostic ownership bug. Water closeups are owned by
  the water pass, so the material stress analyzer now uses
  `frame_contract.water.roughness` when `water.surface_count > 0`.
- `glass_water_courtyard:water_closeup` now reports opaque roughness center
  `0.75008`, target roughness `0.03000`, and `warnings=0`.

### CompositeV3 Reflection Confidence Input - 2026-06-06

Implemented:

- `FullSceneCompositeV3` consumes `reflection_confidence` alongside
  `reflection_radiance`.
- candidate HDR reflection weight is now driven by the ReflectionV3 resolver's
  confidence output, not a luma estimate inside the composite shader.
- the V3 contract, frame readiness, and analyzer require
  `reflection_confidence` before CompositeV3 is treated as the real producer.

Validation:

- `build/captures/v3_composite_reflection_confidence_static_fullviews_20260606`.
- static gallery V3 packet passed with `review_packet_passed`.
- `composite_v3_producer=FullSceneCompositeV3`.
- `candidate_hdr_scene_color_owned_by_full_scene_composite_v3`.

### CompositeV3 Energy Diagnostics Output - 2026-06-06

Implemented:

- `FullSceneCompositeV3` now writes three concrete MRT outputs:
  - `candidate_hdr_scene_color`.
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.
- `energy_clamp_policy` stores pre-clamp luma, clamp mask, clamp ratio, and
  legacy HDR rescue usage.
- `overbright_diagnostics` stores overbright mask, underlit mask, legacy rescue
  usage, and reflection confidence.
- CompositeV3 render-target allocation, render-graph import/write tracking,
  frame-contract resources, memory accounting, and V3 analyzer gates now
  require the two diagnostic resources when `FullSceneCompositeV3` is the real
  producer.
- Debug modes:
  - `80`: `FullSceneCompositeV3EnergyClampPolicy`.
  - `81`: `FullSceneCompositeV3OverbrightDiagnostics`.
- V3 packet view names:
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.

Validation:

- `python -m py_compile tools/analyze_full_scene_shader_v3_placeholders.py
  tools/validate_full_scene_shader_pipeline_v3_plan.py
  tools/check_full_scene_shader_pipeline_v2_frame_report.py`.
- `python tools/validate_full_scene_shader_pipeline_v3_plan.py`.
- native `CortexEngine` build passed.
- packet:
  `build/captures/v3_composite_energy_diagnostics_static_fullviews_20260606`.

Evidence:

- packet status: `review_packet_passed`.
- `report_count=32`.
- `composite_v3_producer=FullSceneCompositeV3`.
- `FullSceneCompositeV3` reads `direct_lighting`, `indirect_lighting`,
  `shadow_visibility`, `hdr_color`, `reflection_radiance`, and
  `reflection_confidence`.
- `FullSceneCompositeV3` writes `candidate_hdr_scene_color`,
  `energy_clamp_policy`, and `overbright_diagnostics`.
- frame resources for all three outputs are valid at `1088x612`.
- debug metrics:
  - `candidate_hdr_scene_color.mean_luma=0.4412127`,
    `nonblack_ratio=0.9693370`.
  - `energy_clamp_policy.mean_luma=0.0063769`,
    `nonblack_ratio=0.8010699`.
  - `overbright_diagnostics.mean_luma=0.0654600`,
    `nonblack_ratio=0.2378244`.

Remaining limitation:

- This proves concrete CompositeV3 diagnostic ownership for a static gallery
  packet only.
- Default beauty remains unchanged and not promotable.
- Cross-family and motion evidence are still required before this can support
  candidate/default promotion.

### CompositeV3 Diagnostic Gate - 2026-06-06

Implemented:

- Added `tools/analyze_full_scene_shader_v3_composite_diagnostics.py`.
- The analyzer measures lane-aware CompositeV3 diagnostic captures:
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.
- It emits `v3_composite_diagnostics.json` and
  `v3_composite_diagnostics.md`.
- It is now run by `tools/run_full_scene_shader_pipeline_v3_packet.ps1` before
  the promotion decision.

Validation:

- direct analyzer pass on
  `build/captures/v3_composite_energy_diagnostics_static_fullviews_20260606`.
- integrated packet:
  `build/captures/v3_composite_diagnostics_gate_static_gallery_20260606`.

Evidence:

- direct analyzer output:
  - failures `0`.
  - warnings `0`.
  - `mean_clamp_mask=0.000045`.
  - `mean_clamp_ratio=0.000011`.
  - `mean_legacy_rescue=0.048630`.
  - `mean_underlit=0.083867`.
  - `mean_overbright=0.009254`.
- integrated packet output:
  - `PASS: CompositeV3 diagnostics are measurable`.
  - `PASS: V3 promotion decision status=review_packet_passed`.

Remaining limitation:

- The gate establishes a baseline; it does not yet reduce legacy HDR rescue.
- Next CompositeV3 shader work should reduce `mean_legacy_rescue` and then run
  motion packets.

### CompositeV3 Material Albedo / Scene-Local Floor - 2026-06-06

Implemented:

- `FullSceneCompositeV3` now reads `vb_gbuffer_albedo` through a concrete
  `MaterialAlbedo` render-graph import.
- CompositeV3 readiness, runtime frame context, analyzers, and the V3 contract
  require the material-albedo read edge.
- Before using legacy `hdr_color` rescue, the shader applies a bounded
  candidate-owned fill from material albedo and a small neutral scene-local
  floor.

Evidence:

- baseline diagnostic packet:
  `build/captures/v3_composite_diagnostics_gate_static_gallery_20260606`.
  - `mean_legacy_rescue=0.048630`.
  - failures `0`, warnings `0`.
- static scene-floor packet:
  `build/captures/v3_composite_scene_floor_static_gallery_20260606`.
  - `mean_legacy_rescue=0.000000`.
  - `mean_underlit=0.080436`.
  - failures `0`, warnings `0`.
- mouse-jitter scene-floor packet:
  `build/captures/v3_composite_scene_floor_mouse_jitter_gallery_20260606`.
  - `mean_legacy_rescue=0.000000`.
  - `mean_underlit=0.082492`.
  - failures `0`, warnings `0`.
- frame report proof:
  `FullSceneCompositeV3.reads` includes `vb_gbuffer_albedo`, and V3 composite
  channels include `material_albedo_input_read`.

Remaining limitation:

- This reduces legacy HDR rescue in gallery static/mouse-jitter evidence only.
- It is still candidate-only and not a default-beauty promotion.
- The neutral floor is a temporary bounded scene-local fallback, not the final
  texture-backed `SceneLocalEnvironmentV3`.

### Candidate Beauty Strict Gate Scaffold - 2026-06-06

Implemented:

- `candidate_beauty` is now validated as a required V3 domain.
- The V3 contract rejects `hdr_color` and `ldr_cinematic_output` as ready
  candidate-beauty inputs.
- `candidate_beauty_ready` now requires the real path:
  `CinematicPostV3(candidate_hdr_scene_color -> candidate_ldr_cinematic_output)`.
- The placeholder analyzer and promotion gate fail candidate-ready rows that
  come from the legacy `FullSceneCandidateBeautyV3` bridge.

Evidence:

- packet:
  `build/captures/v3_candidate_beauty_strict_gate_static_gallery_20260606`.
- packet status: `review_packet_passed`.
- `v3_signal.json`:
  - rows `32`, failures `0`, warnings `0`.
  - candidate requested reports `4`.
  - candidate ready reports `4`.
  - candidate producers: `CinematicPostV3`, `none`.
- `v3_stability.json`:
  - `default_beauty_affects_any=false`.
  - composite ready reports `32`.
  - cinematic post ready reports `32`.
- `promotion_decision.json`:
  - `default_beauty_promotable=false`.
  - failures `0`.
  - warnings are limited to subset coverage: missing non-gallery families,
    missing motion modes, and sequence count below promotion evidence.

Remaining limitation:

- This is a strict candidate-path gate, not a final visual-quality change.
- Cross-family and motion evidence remain required before candidate/default
  promotion.

### V3 Material Payload Diagnostic Gate - 2026-06-06

Implemented:

- Added `tools/analyze_full_scene_shader_v3_material_payload.py`.
- V3 packets now capture material payload views:
  `roughness`, `metallic`, `surface_class`, `surface_policy`,
  `material_family`, `reflection_policy`, `temporal_policy`,
  `post_sensitivity`, `material_id`, and `object_id`.
- V3 packets now emit `v3_material_payload.json` and
  `v3_material_payload.md`.
- Promotion decisions now require `v3_material_payload.json`.
- The V3 placeholder analyzer now distinguishes full-pipeline reports from
  material-payload diagnostic reports, so material debug views are not forced
  to prove lighting/reflection/post readiness.

Evidence:

- packet:
  `build/captures/v3_material_payload_gate_static_gallery_retry_20260606`.
- `v3_signal.json`:
  - reports `42`.
  - full-pipeline reports `32`.
  - material-payload reports `10`.
  - ok reports `42`.
  - failures `0`, warnings `0`.
- `v3_material_payload.json`:
  - ready `true`.
  - failures `0`.
  - warnings `2`.
  - required material debug views `9`.
  - optional material debug views `1`.
  - sampled materials total across reports `2520`.
  - named materials total across reports `2520`.
  - representative per-report stats: sampled `60`, named `60`, average
    roughness `0.5013`, average metallic `0.2167`, average albedo luminance
    `0.4559`.
  - material debug views are nonblank, including roughness, surface class,
    material family, material ID, and object ID at `1.00000` nonblack ratio.
- `promotion_decision.json`:
  - status `review_packet_passed`.
  - `default_beauty_promotable=false`.
  - failures `0`.
  - warnings are material fallback debt plus expected subset coverage warnings.

Remaining limitation:

- This gate measures material payload evidence; it does not yet replace the
  current VB material resolve with a richer standalone PBR resource set.
- Current material debt is explicit:
  `preset_default_roughness fallback count 8` and
  `preset_default_transmission fallback count 5`.

### Material Default Semantics Split - 2026-06-06

Implemented:

- Added frame-contract fields that distinguish authored material-class defaults
  from unresolved fallback:
  - `preset_class_authored_default_roughness`.
  - `preset_class_authored_default_transmission`.
  - `unresolved_default_roughness_fallback`.
  - `unresolved_default_transmission_fallback`.
- `RendererSceneSnapshot` now counts named material preset roughness and
  transmission overrides as class-authored defaults.
- `v3_material_payload` now fails on unresolved default roughness/transmission
  fallback, but no longer warns on authored material preset values.
- The V3 plan validator requires the new frame-contract fields.

Evidence:

- packet:
  `build/captures/v3_material_class_authored_defaults_static_gallery_20260606`.
- `v3_material_payload.json`:
  - ready `true`.
  - failures `0`.
  - warnings `0`.
  - sampled materials `2520`.
  - named materials `2520`.
  - advanced feature materials `1344`.
  - reflection eligible `756`.
  - class-authored roughness defaults `336`.
  - class-authored transmission defaults `210`.
  - unresolved roughness fallback `0`.
  - unresolved transmission fallback `0`.
- `promotion_decision.json`:
  - status `review_packet_passed`.
  - `default_beauty_promotable=false`.
  - failures `0`.
  - warnings `3`, all expected subset warnings.

Remaining limitation:

- This is semantics and evidence cleanup, not new material art.
- The unresolved fallback counters are explicit and gateable, but true
  provider-missing material cases still need cross-family exercise.
- Rich texture-backed PBR resources remain the next material quality step.

### ReflectionV3 Source Stability Gate - 2026-06-06

Implemented:

- `FullSceneReflectionResolverV3` now uses pixel-exact `Load()` for
  pixel-aligned reflection source buffers instead of linear-filtered samples.
- Reflection source rejection, suppression, and inactive diagnostics now use
  continuous confidence/debt signals where possible.
- `v3_lighting_motion` now warns on reflection diagnostic masks that move more
  than `1.75x` beauty with delta above `0.02`.
- `CMakeLists.txt` explicitly appends the V3 runtime shaders to
  `CORTEX_ASSET_FILES` so future CMake regeneration can track shader-only
  runtime asset sync.

Evidence:

- before packet:
  `build/captures/v3_forced_ssr_reflection_pixel_loads_mouse_jitter_20260606`.
- after packet:
  `build/captures/v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606`.
- after status:
  - V3 packet passed.
  - material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision `review_packet_passed`.
- forced-SSR mouse-jitter motion:
  - `reflection_ssr_source_signal` remains ok: delta `0.021202`, `0.797x`
    beauty.
  - `reflection_source_suppression` improved from `0.060532` (`2.276x`
    beauty, warning) to `0.014887` (`0.560x` beauty, ok).
  - `reflection_temporal_delta` improved from `0.076251` to `0.068744`, but
    remains a warning.

Remaining limitation:

- `reflection_rejected_source_mask`, `reflection_temporal_delta`,
  `reflection_history_v3_validity`, and `reflection_history_v3_rejection` still
  warn under forced-SSR mouse jitter.
- The next reflection refactor should stabilize `FullSceneReflectionHistoryV3`
  rather than loosening the warning gate.
- This packet used a manual copy into `build/bin/assets/shaders` because the
  current generated Ninja asset graph was stale and full `CortexAssets`
  regeneration timed out in this session.

### ReflectionV3 History Stability Slice - 2026-06-07

Implemented:

- `FullSceneReflectionHistoryV3` now loads current-frame reflection radiance,
  source ID, and temporal-delta resources pixel-exactly.
- Reprojected depth and normal tests now use filtered samples with a wider
  acceptance band, reducing false disocclusion under small mouse motion.
- History activity/reuse is confidence-driven instead of radiance-luma-driven.
- Source-switch rejection is continuous rather than hard-thresholded.

Evidence:

- baseline packet:
  `build/captures/v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606`.
- after packet:
  `build/captures/v3_reflection_history_confidence_validity_mouse_jitter_20260607`.
- after status:
  - V3 packet passed.
  - material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision `review_packet_passed`.
- forced-SSR mouse-jitter motion:
  - `reflection_history_v3_validity`: `0.053437 -> 0.052630`.
  - `reflection_history_v3_rejection`: `0.070312 -> 0.061797`.
  - `reflection_history_v3_rejection` active delta:
    `0.343471 -> 0.278238`.

Remaining limitation:

- History/rejection motion improved but still warns.
- The next reflection slice should target resolver-side
  `reflection_rejected_source_mask` and `reflection_temporal_delta`, whose
  forced-SSR availability channel still moves at `0.082185` RGB delta.

### ReflectionV3 Forced-Availability Diagnostic Stability - 2026-06-07

Implemented:

- `FullSceneReflectionResolverV3` now reports forced-source unavailability as
  continuous availability debt instead of a binary raw-active threshold.
- Forced SSR keeps continuous SSR availability debt in
  `reflection_rejected_source_mask.g`.

Evidence:

- baseline packet:
  `build/captures/v3_reflection_history_confidence_validity_mouse_jitter_20260607`.
- after packet:
  `build/captures/v3_reflection_resolver_continuous_forced_availability_mouse_jitter_20260607`.
- after status:
  - V3 packet passed.
  - material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision `review_packet_passed`.
- forced-SSR mouse-jitter motion:
  - `reflection_rejected_source_mask`: `0.060360 -> 0.014533`, warning
    cleared.
  - `reflection_temporal_delta`: `0.068744 -> 0.022837`, warning cleared.
  - shared G-channel delta:
    `0.082185 -> 0.018025`.

Remaining limitation:

- The only reflection diagnostic warnings left in this packet are
  `reflection_history_v3_validity` and `reflection_history_v3_rejection`.

### ReflectionV3 Confidence-Weighted History Diagnostics - 2026-06-07

Implemented:

- Added `tools/run_reflection_v3_motion_focus_packet.ps1` as a compact
  ReflectionV3 motion harness.
- Added `--focus reflection` to
  `tools/analyze_full_scene_shader_v3_lighting_motion.py` so focused packets
  gate only reflection diagnostics plus beauty baseline instead of reporting
  unrelated missing lighting/composite views.
- `FullSceneReflectionHistoryV3.hlsl` now emits confidence-weighted continuous
  history diagnostics:
  - current active uses reflection confidence directly instead of a thresholded
    activity mask.
  - history reusable uses previous history confidence times reprojection
    acceptance instead of a near-binary previous-history availability mask.
  - source-switch, disocclusion, high-motion, and out-of-bounds rejection are
    gated by current/previous reflection history support.

Why:

- The remaining forced-SSR mouse-jitter warnings were not in the resolver
  source signals anymore. They were in history validity/rejection diagnostics
  that treated low-support geometry/depth changes as reflection-history debt.
- That made the diagnostic masks move more than beauty even though
  reflection confidence, source ID, rejected-source mask, temporal delta, SSR
  source signal, RT source signal, and source suppression were already stable.

Validation:

```powershell
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_reflection_history_confidence_weighted_focus_20260607
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_history_confidence_weighted_full_20260607
```

Evidence:

- focused baseline:
  `build/captures/v3_reflection_motion_focus_forced_ssr_mouse_jitter_20260607`.
- focused after:
  `build/captures/v3_reflection_history_confidence_weighted_focus_20260607`.
- full after:
  `build/captures/v3_reflection_history_confidence_weighted_full_20260607`.
- focused before:
  - `reflection_history_v3_validity`: `0.05262983`,
    `1.979x` beauty, warning.
  - `reflection_history_v3_rejection`: `0.06179731`,
    `2.324x` beauty, warning.
- focused after:
  - `reflection_history_v3_validity`: `0.03415736`,
    `1.284x` beauty, ok.
  - `reflection_history_v3_rejection`: `0.00473161`,
    `0.178x` beauty, ok.
- full stress packet:
  - V2 packet evidence passed.
  - V3 placeholder packet passed.
  - V3 lighting motion measured `24` view sequences with `0` warnings and
    `0` failures.
  - V3 material payload diagnostics passed.
  - CompositeV3 diagnostics passed.
  - promotion decision: `review_packet_passed`.

Current limitation:

- This is still a stress-only packet. Promotion remains intentionally blocked
  for missing families and motion modes in the promotion decision.
- The next renderer slice should move from this now-stable reflection history
  base into material payload hardening or the scene-local environment split,
  not default-beauty promotion.

### Material Payload Contract Debug View Coverage - 2026-06-07

Implemented:

- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1` now exposes
  material packet aliases:
  - `material_base_color` -> debug view `35` / VB G-buffer albedo.
  - `material_normal` -> debug view `36` / VB G-buffer normal-roughness.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now includes those two
  aliases in the default V3 packet view set.
- `tools/analyze_full_scene_shader_v3_material_payload.py` now requires
  `material_base_color` and `material_normal` debug-view metrics.
- The material analyzer now reads
  `assets/final_art/full_scene_shader_pipeline_v3_contract.json` and reports
  coverage for contract-required material debug views.
- `tools/analyze_full_scene_shader_v3_placeholders.py` now treats debug modes
  `35` and `36` as material-payload diagnostic scope, so material-only debug
  views do not falsely require `FullSceneLightingV3` execution.
- `assets/final_art/full_scene_shader_pipeline_v3_contract.json` now lists
  `material_base_color` and `material_normal` in material
  `packet_debug_views`.

Why:

- The V3 material contract already required base color, normal, roughness,
  metallic, material class, and missing-channel-mask debug evidence.
- The previous packet only gated roughness/metallic/class-policy views, so it
  could pass while contract-required material debug coverage was incomplete.

Validation:

```powershell
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_material_payload_contract_views_stress_20260607
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_material_payload_contract_views_stress_20260607 --signal-output build\captures\v3_material_payload_contract_views_stress_20260607\v3_signal.json --stability-output build\captures\v3_material_payload_contract_views_stress_20260607\v3_stability.json --require-lighting-split-ready --require-lighting-split-draw-count 1 --require-lighting-signal-metrics
python tools\analyze_full_scene_shader_v3_material_payload.py --manifest build\captures\v3_material_payload_contract_views_stress_20260607\manifest.json --output-json build\captures\v3_material_payload_contract_views_stress_20260607\v3_material_payload.json --output-md build\captures\v3_material_payload_contract_views_stress_20260607\v3_material_payload.md
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_material_payload_contract_views_stress_20260607 --output-json build\captures\v3_material_payload_contract_views_stress_20260607\promotion_decision.json --output-md build\captures\v3_material_payload_contract_views_stress_20260607\promotion_decision.md --allow-subset-review
```

Evidence:

- packet:
  `build/captures/v3_material_payload_contract_views_stress_20260607`.
- V3 placeholder analyzer passed after material debug scope correction.
- V3 lighting motion passed with `24` view sequences.
- V3 material payload passed:
  - `sampled materials`: `2640`.
  - `named materials`: `2640`.
  - `advanced feature materials`: `1408`.
  - `unresolved roughness fallback`: `0`.
  - `unresolved transmission fallback`: `0`.
  - `contract required debug views`: `6`.
  - `contract debug view debt`: `1`.
  - `material_base_color`, `material_roughness`, `material_metallic`,
    `material_normal`, and `material_class` are covered.
  - `material_missing_channel_mask` remains missing packet/debug-view debt.
- CompositeV3 diagnostics passed.
- promotion decision: `review_packet_passed`; default beauty remains not
  promotable because this is still a stress-only subset packet.

Current limitation:

- The missing-channel-mask resource/view is now explicit debt. The next
  material payload slice should create a real missing-channel mask or a
  stronger frame-contract equivalent instead of leaving it as a warning.

### LightingShadowV3 Source Attribution Split - 2026-06-07

Implemented:

- `FullSceneLightingV3` now encodes shadow-source attribution as:
  - red: directional/sun shadow-loss ratio.
  - green: local fixture shadow-loss ratio.
  - blue: shadow-map path enabled.
  - alpha: PCSS/filter path enabled.
- Added `tools/analyze_full_scene_shader_v3_shadow_attribution.py`.
  It validates source attribution against `v3_shadow_loss`,
  `v3_shadow_visibility`, and `v3_lighting_energy_budget` captures.
- `tools/run_lighting_v3_shadow_motion_focus_packet.ps1` now runs the
  shadow-attribution analyzer and produces `v3_shadow_attribution.json/md`.
- `tools/analyze_full_scene_shader_v3_lighting_motion.py --focus shadow` now
  measures only shadow-owned views; `v3_indirect_lighting` is covered by
  broader lighting packets.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_shadow_attribution.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_lighting_v3_shadow_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_lighting_shadow_source_split_focus_pass2_20260607 -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 2 -MotionFrames 64 -MotionLookAmplitude 0.025 -MotionLookCycles 5.0
```

Evidence:

- Packet:
  `build/captures/v3_lighting_shadow_source_split_focus_pass2_20260607`.
- Motion analyzer: `11` view sequences, `0` warnings, `0` failures.
- Shadow-attribution analyzer: `1` family, `0` warnings, `0` failures.
- Attribution row for `stress_rt_showcase_reflection_closeup`:
  sun loss `0.339516`, local loss `0.007993`, source active `0.464322`,
  shadow-loss active `0.839763`, visibility occlusion `1.000000`,
  shadow-map enabled `1.000000`, energy active `1.000000`.

Current limitation:

- This is a focused mouse-jitter proof. The next LightingShadowV3 step is a
  scripted high-contrast light-sweep row and, if needed, cascade/slice/RT-mask
  attribution.

### LightingShadowV3 Light-Sweep Stress Row - 2026-06-07

Implemented:

- Added `CORTEX_LIGHT_SWEEP` runtime automation plus sweep controls for frame
  count, cycles, yaw amplitude, elevation amplitude, and intensity amplitude.
- The sweep updates real renderer sun direction/intensity in
  `Engine::Update()`, then the normal shadow map and LightingV3 paths consume
  that state.
- Added `light_sweep` to:
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`
  - `tools/run_lighting_v3_shadow_motion_focus_packet.ps1`
  - `tools/run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1`
- Contract tests and the V3 static validator cover the new mode.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_shadow_attribution.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_lighting_v3_shadow_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_lighting_shadow_light_sweep_focus_20260607 -StabilityMotionMode light_sweep -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -MotionFrames 72 -MotionLookAmplitude 0.035 -MotionForwardAmplitude 0.45 -MotionLiftAmplitude 0.28 -MotionLookCycles 2.0
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1
```

Evidence:

- Packet:
  `build/captures/v3_lighting_shadow_light_sweep_focus_20260607`.
- Motion analyzer: `11` view sequences, `0` warnings, `0` failures.
- Shadow-attribution analyzer: `1` family, `0` warnings, `0` failures.
- `v3_shadow_visibility.delta=0.01310343`, `1.000x` legacy,
  `31.476x` beauty.
- `v3_shadow_loss.delta=0.00762285`, `0.957x` legacy,
  `18.311x` beauty.
- `v3_shadow_source_attribution.delta=0.00207217`, `4.978x` beauty.
- Attribution row for `stress_rt_showcase_reflection_closeup`:
  sun loss `0.621078`, local loss `0.007351`, source active `0.777158`,
  shadow-loss active `0.998522`, visibility occlusion `1.000000`,
  shadow-map enabled `1.000000`, energy active `1.000000`.

Current limitation:

- This is still focused stress evidence. Promotion-grade LightingShadowV3
  evidence needs the same `light_sweep` row in a bounded cross-family matrix.

### LightingShadowV3 Promotion Matrix - 2026-06-09

Implemented:

- Added `tools/build_lighting_v3_shadow_promotion_matrix.py`.
- The matrix builder consumes existing packet roots and runs:
  - `analyze_full_scene_shader_v3_shadow_attribution.py` for ownership/source
    attribution on every packet;
  - `analyze_full_scene_shader_v3_lighting_motion.py --focus shadow` for
    non-static motion packets.
- The matrix reports:
  - required/observed/missing shadow families;
  - required/observed/missing shadow motion modes;
  - attribution-ready packet count;
  - motion-ready packet count;
  - per-packet attribution/motion failures and warnings.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` now requires the
  promotion matrix builder and its schema/readiness markers.

Validation:

```powershell
python -m py_compile `
  CortexEngine\tools\build_lighting_v3_shadow_promotion_matrix.py `
  CortexEngine\tools\validate_full_scene_shader_pipeline_v3_plan.py

python CortexEngine\tools\validate_full_scene_shader_pipeline_v3_plan.py

python CortexEngine\tools\build_lighting_v3_shadow_promotion_matrix.py `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_gallery_smoke1_20260609 `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_kitchen_static_smoke1_20260609 `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_reflection_static_smoke1_20260609 `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_office_gym_static_smoke1_20260609 `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_heavy_light_sweep_seq1_20260609 `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_gallery_camera_sweep_seq1_20260609 `
  --packet-root CortexEngine\build\captures\v3_default_beauty_promotion_gallery_mouse_jitter_seq1_20260609 `
  --required-families stress_rt_showcase_reflection_closeup,gallery,kitchen,office,gym,concert,red_room,stadium `
  --required-motion-modes static,mouse_jitter,camera_sweep,light_sweep `
  --output-json CortexEngine\build\captures\v3_lighting_shadow_promotion_matrix1_20260609\lighting_shadow_promotion_matrix.json `
  --output-md CortexEngine\build\captures\v3_lighting_shadow_promotion_matrix1_20260609\lighting_shadow_promotion_matrix.md
```

Evidence:

- Matrix:
  `build\captures\v3_lighting_shadow_promotion_matrix1_20260609\lighting_shadow_promotion_matrix.md/json`.
- Results:
  - `packet_count=7`;
  - `ready_attribution_packet_count=7`;
  - `ready_motion_packet_count=3`;
  - `shadow_promotion_ready=true`;
  - observed families:
    `concert,gallery,gym,kitchen,office,red_room,stadium,stress_rt_showcase_reflection_closeup`;
  - missing families: none;
  - observed motion modes: `camera_sweep,light_sweep,mouse_jitter,static`;
  - missing motion modes: none;
  - failures: `0`;
  - warnings: `0`.

Interpretation:

- The prior LightingShadowV3 limitation is resolved for promotion-grade
  evidence: shadow attribution and focused shadow motion now have a bounded
  cross-family matrix over the required family/motion set.
