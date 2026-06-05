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

Required next evidence for completion/promotion:

- close parity gaps between `PSMainV3LightingSplit` and the current default
  deferred beauty lighting path, especially local probe and environment terms.
- run the concrete split-resource packet after native build availability is
  restored and compare V3 split outputs against the legacy deferred terms.
- keep default beauty unchanged until the consumer/composite path and packet
  gates prove promotion quality.

### L006 - Reflection V3 Resolver

Status: pending.

### L007 - Scene Local Environment V3

Status: pending.

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
