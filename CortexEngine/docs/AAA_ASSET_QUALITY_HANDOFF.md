# AAA Asset Quality Handoff

This is the living handoff for the AAA asset-quality goal.
Read this after compaction before continuing.

## 2026-06-09 Forward Light CBuffer TDR Root-Cause Slice

Context:

- Active repro scene:
  `build/diagnostics/kitchen_seed_reduction/shell5.json`
- Launch path:
  `CortexEngine.exe --scene model_authored_scene --mode=default --no-llm --no-dreamer --no-launcher --smoke-frames=1`
- Repro env disabled skybox, aux geometry, VB, GPU culling, SSR, SSAO,
  bloom, TAA, shadows, HZB, motion vectors, post, RT, RT reflections, RT GI,
  fog, and particles.
- The executable reads shaders from `build/bin/assets/shaders`, so shader-only
  probes must copy `assets/shaders/Basic.hlsl` there and use
  `CORTEX_DISABLE_SHADER_CACHE=1`.

Diagnosis:

- Beauty with opaque geometry enabled previously TDR'd even after IBL, RT,
  shadows, and post were disabled.
- Earlier probes showed:
  - debug view before the direct-light loop passed
  - debug view after the loop hung
  - constant pixel shader passed
  - reduced non-PBR albedo path passed
- New probes narrowed the failure further:
  - static pre-loop `g_Lights[0]` read passed
  - loop with no light-array read passed
  - loop-variable indexing of `g_Lights[i]` reproduced the GPU fence timeout
    / `DXGI_ERROR_DEVICE_HUNG` behavior
- Root cause for this repro: the forward Basic shader used a dynamically
  indexed constant-buffer light array in the direct light loop. On this DX12
  driver/GPU path, the bounded loop induction variable access could still TDR.

Implemented:

- `assets/shaders/Basic.hlsl`
  - changed the main direct-light loop from dynamic
    `for (i < lightCount) g_Lights[i]` to an unrolled `LIGHT_MAX` lane loop
    with a runtime `if (i >= lightCount) break` gate
  - changed debug view 17's forward-light loop the same way
  - kept full shader features enabled; temporary probe modes and compile-time
    feature-disable switches were removed
- `src/Graphics/Renderer_FrameLightingConstants.cpp`
  - `shadowParams.z/w` now mean the shadow map is owned by this frame:
    controls enabled, runtime shadow disable is false, shadow map/SRV exist,
    and the shadow pipeline exists
- `src/Graphics/Renderer_FramePostConstants.cpp`
  - `postParams.w` now means the RT shadow mask is owned by this frame:
    RT shadow dispatch is planned and mask/SRV exist
  - this prevents the forward shader from sampling RT shadow masks based only
    on raw RT pipeline readiness

Validation:

```powershell
Copy-Item assets\shaders\Basic.hlsl build\bin\assets\shaders\Basic.hlsl -Force
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
ctest --test-dir build --output-on-failure -C Release
```

Results:

- Native build completed with `CORTEX_SKIP_ASSET_SYNC=1`.
- The known trailing `vswhere.exe` warning still appears after the successful
  build command.
- Full asset sync hung in `tools/sync_assets.cmake`; stopped those build
  processes and used explicit shader sync for runtime validation.
- `ctest` returned success but this build tree reported `No tests were found`.
- Clean stripped beauty repro passed:
  `build/captures/kitchen_shell5_static_light_lanes_minimal_final_beauty_20260609`
  - exit `0`
  - no fence timeout
  - no device removal
  - reached `Renderer shutdown: GPU idle`
- Forward-light debug view 17 passed:
  `build/captures/kitchen_shell5_static_light_lanes_minimal_final_17_20260609`
  - exit `0`
  - no fence timeout
  - no device removal
  - reached `Renderer shutdown: GPU idle`
- A scene-local/IBL-visible variant also passed:
  `build/captures/kitchen_shell5_beauty_ibl_on_static_light_lanes_probe_20260609`
  - note: this kitchen profile reported IBL intensity `0`, so it is not a full
    old-office-HDRI stress case

Current interpretation:

- This slice fixes the proven TDR root in the shell5 forward opaque path.
- It does not yet prove every user-visible flicker case is gone. Next passes
  should stress the old office HDRI/default-scene floor with IBL active, higher
  frame count, and mouse-look motion.
- Keep using synchronized shader copies or a non-skipped asset sync before
  judging shader changes.

Next work:

1. Run the original/default office-HDRI floor repro with IBL active and
   mouse-look motion.
2. If flicker remains without TDR, separate shadow-map shimmer, SSR/specular
   instability, and IBL/reflection source instability with debug views.
3. Continue Full-Scene AAA work after the stability gate:
   ReflectionV3 provider evidence, LightingShadowV3 ownership diagnostics,
   material-quality gates, and cross-scene packets.

## 2026-06-09 Runtime SceneLocalResourceContractV1 Checkpoint

Latest pushed work before this section:

- Commit `748fd81` added the packet-side
  `SceneLocalResourceContractV1` gate.

Implemented after that:

- Added runtime `SceneLocalResourceContractV1` interpretation to
  `FullSceneShaderFrameContext.h`.
- Runtime frame reports now expose:
  - `scene_local_resource_contract_ready`
  - `scene_local_resource_contract_id`
  - `scene_local_resource_contract_family`
  - `scene_local_resource_contract_status`
  - `scene_local_resource_contract_unsafe_reason`
  - `scene_local_resource_contract_visible_external_hdri_allowed`
  - `scene_local_resource_contract_external_hdri_safe`
  - `scene_local_resource_contract_environment_policy_allowed`
  - `scene_local_resource_contract_reflection_policy_allowed`
  - `scene_local_resource_contract_reflection_source_allowed`
  - `scene_local_resource_contract_proxy_resources_ready`
  - `scene_local_resource_contract_payload_resources_ready`
  - `scene_local_resource_contract_role_count`
  - `scene_local_resource_contract_ready_role_count`
- Extended `FrameContractJson.cpp` to serialize those runtime fields.
- Extended `tools/analyze_scene_local_resource_contract_v1.py` so packet
  review now requires the renderer's own runtime contract verdict, not only
  Python-side re-derivation.
- Updated `assets/final_art/full_scene_shader_pipeline_v3_contract.json` with
  the required runtime fields.
- Updated `assets/final_art/scene_local_resource_contract_v1.json` to include
  renderer vocabulary such as `enclosed_scene_local_only`,
  `screen_space_reflection`, and `ray_query_reflection`.
- Extended `tools/validate_full_scene_shader_pipeline_v3_plan.py` to require
  the runtime contract fields and helper functions.

Validation:

```powershell
python -m py_compile tools\analyze_scene_local_resource_contract_v1.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter "beauty,candidate_beauty_v3,candidate_hdr_scene_color,scene_local_environment,ambient_lighting,visible_background,reflection_background,atmosphere,reflection_radiance,reflection_confidence,reflection_source_id,reflection_source_suppression,ambient_ibl,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage,material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution" -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_runtime_scene_local_resource_contract_smoke_20260609
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_runtime_scene_local_resource_contract_smoke_20260609 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_runtime_scene_local_resource_contract_smoke_20260609\v3_matrix_single_packet_decision.json --output-md build\captures\v3_runtime_scene_local_resource_contract_smoke_20260609\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- Static V3 validator passed.
- Native Release build linked `bin\CortexEngine.exe`.
- Known trailing `vswhere.exe` warning still appears after successful builds.
- Focused runtime packet passed:
  - packet root
    `build\captures\v3_runtime_scene_local_resource_contract_smoke_20260609`
  - `41` reports
  - scene-local resource contract passed `41/41`
  - runtime sample: `ready=true`, `family=gallery`, `status=ready`,
    `unsafe=none`, `roles=6/6`, `source=local_probe`
  - promotion decision status `review_packet_passed`
  - single-packet matrix passed for
    `stress_rt_showcase_reflection_closeup/static`

Enclosed-scene probe:

- A kitchen packet attempt timed out before manifest assembly, but produced
  direct frame reports under
  `build\captures\v3_runtime_scene_local_resource_contract_kitchen_smoke_20260609`.
- Sample kitchen report:
  - `sceneFamily=home_kitchen_lantern`
  - `profile=kitchen_morning_warm_scene_local_v1`
  - `enclosed=true`
  - `externalVisible=false`
  - `visibleExternalHDRIAllowed=false`
  - `invalidExternalHDRI=false`
  - `contractFamily=kitchen`
  - `contractReady=false`
  - `status=unsafe`
  - `unsafe=proxy_resources_below_contract`
  - `roles=4/6`
- Interpretation: the runtime is now correctly refusing to mark kitchen as
  final-art resource-ready. This is the next real rendering-quality blocker:
  kitchen needs scene-local proxy/payload resources that satisfy the contract.

Current next work:

1. Make at least one enclosed non-gallery family pass
   `SceneLocalResourceContractV1`, preferably kitchen.
2. Add or bind kitchen scene-local proxy/payload resources so runtime reports:
   - `scene_local_resource_contract_proxy_resources_ready=true`
   - `scene_local_resource_contract_payload_resources_ready=true`
   - `scene_local_resource_contract_ready_role_count=6`
   - `scene_local_resource_contract_ready=true`
3. Then run a cross-packet matrix with:
   - `stress_rt_showcase_reflection_closeup/static`
   - `kitchen/static`
4. Do not lower contract thresholds to make kitchen pass. The failure is
   useful because it identifies missing scene-local resource ownership.

## 2026-06-09 SceneLocalResourceContractV1 Checkpoint

Latest pushed work before this section:

- Commit `ff1e95f` rebaselined the AAA work away from pure V3 plumbing and
  toward full-scene shader resource, reflection, lighting, material, and
  cross-scene evidence.

Implemented after that:

- Added `assets/final_art/scene_local_resource_contract_v1.json`.
- Added `tools/analyze_scene_local_resource_contract_v1.py`.
- Extended `assets/final_art/full_scene_shader_pipeline_v3_contract.json` with
  a `scene_local_resource_contract` section.
- Extended `tools/run_full_scene_shader_pipeline_v3_packet.ps1` so V3 packets
  emit:
  - `scene_local_resource_contract_v1.json`
  - `scene_local_resource_contract_v1.md`
- Extended `tools/build_full_scene_shader_v3_promotion_decision.py` so packet
  review requires the scene-local resource contract.
- Extended `tools/validate_full_scene_shader_pipeline_v3_plan.py` so static V3
  validation requires the contract file, analyzer, packet hook, promotion hook,
  resource roles, and family contracts.

What the contract proves:

- Required roles:
  - diffuse irradiance
  - specular radiance
  - visible background
  - reflection background
  - atmosphere
  - exposure
- Required family contracts:
  - gallery
  - kitchen
  - office
  - gym
  - concert
  - red_room
  - stadium
- Per-family rules now declare:
  - whether visible external HDRI is allowed
  - allowed environment policies
  - allowed reflection policies
  - allowed reflection source contracts
  - minimum scene-local proxy and payload resource counts
  - expected material families for later material-quality gates

Validation:

```powershell
python -m py_compile tools\analyze_scene_local_resource_contract_v1.py tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_resource_contract_smoke_20260609
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_scene_local_resource_contract_smoke_20260609 --signal-output build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_signal.json --stability-output build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_stability.json --require-lighting-split-ready --require-lighting-split-draw-count 1 --require-lighting-signal-metrics
python tools\analyze_full_scene_shader_v3_scene_profile.py --manifest build\captures\v3_scene_local_resource_contract_smoke_20260609\manifest.json --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_scene_profile.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_scene_profile.md --min-family-count 1
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_resource_contract_smoke_20260609\manifest.json --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_environment_payload.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_environment_payload.md --min-payload-ready 0
python tools\analyze_scene_local_resource_contract_v1.py --manifest build\captures\v3_scene_local_resource_contract_smoke_20260609\manifest.json --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\scene_local_resource_contract_v1.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\scene_local_resource_contract_v1.md --min-family-count 1
python tools\analyze_full_scene_shader_v3_material_payload.py --manifest build\captures\v3_scene_local_resource_contract_smoke_20260609\manifest.json --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_material_payload.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_material_payload.md
python tools\analyze_full_scene_shader_v3_composite_diagnostics.py --manifest build\captures\v3_scene_local_resource_contract_smoke_20260609\manifest.json --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_composite_diagnostics.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_composite_diagnostics.md
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_scene_local_resource_contract_smoke_20260609 --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\promotion_decision.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\promotion_decision.md --allow-subset-review
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_scene_local_resource_contract_smoke_20260609 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_matrix_single_packet_decision.json --output-md build\captures\v3_scene_local_resource_contract_smoke_20260609\v3_matrix_single_packet_decision.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter "beauty,candidate_beauty_v3,candidate_hdr_scene_color,scene_local_environment,ambient_lighting,visible_background,reflection_background,atmosphere,reflection_radiance,reflection_confidence,reflection_source_id,reflection_source_suppression,ambient_ibl,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage,material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution" -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_resource_contract_integrated_smoke_20260609
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_scene_local_resource_contract_integrated_smoke_20260609 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_scene_local_resource_contract_integrated_smoke_20260609\v3_matrix_single_packet_decision.json --output-md build\captures\v3_scene_local_resource_contract_integrated_smoke_20260609\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- Static V3 plan validator passed.
- Packet capture produced `54` reports. The full packet command timed out
  after capture while evidence files were still being written, so the analyzer
  chain was rerun manually against the completed manifest.
- V3 placeholder packet artifacts passed with `54` reports.
- V3 scene profile policy ownership passed.
- V3 environment payload diagnostics passed.
- Scene-local resource contract passed:
  - `54/54` ready reports
  - contract family: `gallery`
  - all six resource roles proved on all `54` reports
- V3 material payload diagnostics passed.
- CompositeV3 diagnostics passed.
- Promotion decision passed with status `review_packet_passed`.
- Single-packet matrix passed for
  `stress_rt_showcase_reflection_closeup/static`.
- Integrated V3 packet runner path also passed after adding the analyzer:
  - packet root
    `build\captures\v3_scene_local_resource_contract_integrated_smoke_20260609`
  - `41` reports
  - scene-local resource contract passed
  - promotion decision status `review_packet_passed`
  - single-packet matrix passed for
    `stress_rt_showcase_reflection_closeup/static`

Important caveat:

- This is the contract/evidence slice. It does not yet change renderer resource
  selection or improve beauty. The next slice must consume this contract in
  renderer-side resource selection and debug views.

Current next work:

1. Wire `SceneLocalResourceContractV1` into renderer resource selection:
   - map scene profile/family to contract family
   - expose chosen diffuse/specular/background/reflection/exposure owners in
     frame reports
   - fail or mark unsafe when an enclosed scene uses unauthorized visible IBL
2. Add/strengthen debug views:
   - reflection provider id
   - reflection confidence
   - reflection rejection/suppression
   - scene-local visible background owner
   - scene-local reflection background owner
3. Prove the contract on one enclosed non-gallery scene, preferably kitchen, so
   the "no arbitrary visible IBL in enclosed rooms" rule is exercised.

## 2026-06-09 AAA Engine Scenes Rebaseline

User concern:

- Recent work can feel too small if it remains context plumbing only.
- The target is not "a cleaner V3 callsite"; the target is full-scene shader
  quality that can support breathtaking, Unreal-style scenes with robust
  reflections, lighting, materials, enclosed environments, and evidence across
  multiple scene families.

Current honest state:

- V3 renderer architecture is partially cleaned up and safer to extend.
- `FullSceneShaderV3GraphBuilder` owns display, scene-local environment, and
  candidate HDR composite context construction.
- The existing V3 packet system can prove placeholder outputs, material payload
  diagnostics, environment payload diagnostics, composite diagnostics, and a
  narrow promotion matrix.
- This is not yet AAA-quality rendering. It is a foundation for larger shader
  work.
- Continuing to only move context structs around would now be too local unless
  it directly unblocks shader/resource quality.

Strategic pivot:

1. Resource contracts before more cleanup.
   - Define scene-local diffuse irradiance, specular radiance, visible
     background, reflection background, fog/atmosphere, and exposure contracts
     per scene family.
   - Enclosed scenes must not reflect arbitrary outdoor/office IBLs unless the
     scene contract explicitly owns that reflection source.
   - Exterior scenes need separate sky, sun, atmosphere, water, and local probe
     ownership instead of one global environment hack.

2. ReflectionV3 provider fusion.
   - Make reflection source selection explicit: planar, screen-space, ray/RT,
     local probe, scene-local background, fallback.
   - Add debug views that show provider id, confidence, rejection reason, and
     contribution weight.
   - Stop hiding reflection defects with blur. Blur can be a roughness result,
     not a correctness mask.

3. LightingShadowV3 ownership.
   - Make direct light, fill light, emissive light, local bounce, and shadow
     source attribution measurable.
   - Add debug views for shadow visibility, shadow loss, direct unshadowed,
     indirect, and energy budget by source.
   - Validate moving-camera stability, not just static screenshots.

4. Material payload quality.
   - Strengthen material family, surface class, normal/roughness/metallic
     completeness, missing-channel masks, and energy clamp diagnostics.
   - Reject or mark placeholder materials in final-art packets.
   - Add scene-family expectations: kitchen tile, metal appliances, glass,
     wood, fabric, painted walls; gallery polished floor and display glass;
     concert black stage materials, haze, emissives, and controlled speculars.

5. Cross-scene proof, not one stress view.
   - Required matrix should include at least:
     - `rt_showcase:reflection_closeup`
     - one enclosed scene, preferably kitchen or gallery
     - one emissive/heavy-lighting scene, preferably concert or red room
   - Motion modes should include static, camera sweep, and mouse jitter.
   - Evidence must include beauty plus debug views for reflection source,
     reflection confidence, shadow source/loss, material family, missing
     material channels, and energy clamp behavior.

Commit cadence:

- Push after every coherent vertical slice:
  - contract/schema update
  - renderer implementation
  - debug/evidence tooling
  - focused packets and matrix gate
  - handoff update
- Avoid giant unpushed exploratory changes.
- Avoid committing unrelated dirty scene-authoring artifacts unless the slice
  explicitly owns them.

Next concrete implementation slice:

- Build `SceneLocalResourceContractV1`.
- Wire it into the V3 packet/evidence path first, then into renderer resource
  selection.
- Prove it on reflection-closeup and one enclosed scene.
- Expected first commit: contract + validator + handoff.
- Expected second commit: renderer resource selection + debug views.
- Expected third commit: cross-scene packet/matrix evidence.

## 2026-06-07 V3 Composite Context Builder Checkpoint

Latest pushed work before this section:

- Commit `0d6a73a` moved scene-local environment context construction into
  `FullSceneShaderV3GraphBuilder`.

Implemented after that:

- Added `FullSceneShaderV3GraphBuilder::CompositeCommon`.
- Added `FullSceneShaderV3GraphBuilder::CompositeSubmission`.
- Added `FullSceneShaderV3GraphBuilder::SubmitComposite(CompositeCommon,
  CompositeSubmission)`.
- `FullSceneShaderV3GraphBuilder` now constructs
  `FullSceneCompositeV3Context` internally for the candidate HDR scene-color
  composite pass.
- `Renderer_RenderGraphEndFrame.cpp` no longer constructs
  `FullSceneCompositeV3Context` directly. End-frame still decides whether the
  scheduled reflection resolver or local reflection fallback supplies the
  reflection inputs, but the low-level pass context now belongs to the builder.
- Extended `tools\validate_full_scene_shader_pipeline_v3_plan.py` so the
  static V3 validator enforces:
  - `CompositeCommon` and `CompositeSubmission` exist in the builder
  - end-frame does not construct `FullSceneCompositeV3Context`
  - end-frame uses `CompositeCommon compositeCommon`
  - end-frame uses `CompositeSubmission compositeSubmission`
  - end-frame submits through
    `SubmitComposite(compositeCommon, compositeSubmission)`

Validation:

```powershell
python -m py_compile tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter "beauty,candidate_beauty_v3,candidate_hdr_scene_color,scene_local_environment,reflection_radiance,reflection_confidence,reflection_source_id,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage,material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id" -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_context_builder_smoke_20260607
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_composite_context_builder_smoke_20260607 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_composite_context_builder_smoke_20260607\v3_matrix_single_packet_decision.json --output-md build\captures\v3_composite_context_builder_smoke_20260607\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- Static V3 plan validator passed.
- Native build under `VsDevCmd` rebuilt
  `FullSceneShaderV3GraphBuilder.cpp.obj`,
  `Renderer_RenderGraphEndFrame.cpp.obj`, and linked `bin\CortexEngine.exe`.
- Known trailing `vswhere.exe` warning still appears after successful builds.
- Focused packet
  `build\captures\v3_composite_context_builder_smoke_20260607` passed end to
  end:
  - `36` reports
  - V2 frame-report evidence passed
  - V3 placeholder artifacts passed
  - V3 scene profile passed
  - V3 environment payload passed
  - V3 material payload passed
  - CompositeV3 diagnostics passed
  - promotion decision status `review_packet_passed`
- Single-packet matrix passed for
  `stress_rt_showcase_reflection_closeup/static`.

Current next work:

1. Stop spending more time on pure plumbing unless it directly unblocks
   rendering quality. The builder now owns display, scene-local environment,
   and candidate HDR composite context construction.
2. Start the first real full-scene shader quality slice:
   - create scene-local diffuse/specular/background resource contracts per
     scene family
   - make enclosed scenes reflection-safe without hiding IBL bugs behind blur
   - add provider-source debug views for ReflectionV3 fusion
   - improve LightingShadowV3 ownership and shadow-source attribution
3. Required evidence for the next slice:
   - one focused reflection-closeup packet
   - at least one enclosed-scene packet, preferably kitchen or gallery
   - debug views proving reflection source, shadow source, material family, and
     energy clamp behavior
   - matrix gate that includes the focused stress scene and the enclosed scene

## 2026-06-07 V3 Environment Context Builder Checkpoint

Latest pushed work before this section:

- Commit `50787c3` moved candidate/debug display context construction into
  `FullSceneShaderV3GraphBuilder`.

Implemented after that:

- Added `FullSceneShaderV3GraphBuilder::SceneLocalEnvironmentCommon`.
- Added `FullSceneShaderV3GraphBuilder::SceneLocalEnvironmentSubmission`.
- Added `FullSceneShaderV3GraphBuilder::SubmitSceneLocalEnvironment(
  SceneLocalEnvironmentCommon, SceneLocalEnvironmentSubmission)`.
- `FullSceneShaderV3GraphBuilder` now constructs
  `SceneLocalEnvironmentV3Context` internally for the environment pass.
- `Renderer_RenderGraphEndFrame.cpp` no longer constructs
  `SceneLocalEnvironmentV3Context` directly. End-frame still owns the
  payload-binding query, but only submits common device/frame state and
  per-pass environment resources into the builder.
- Extended `tools\validate_full_scene_shader_pipeline_v3_plan.py` so the
  static V3 validator enforces:
  - `SceneLocalEnvironmentCommon` and `SceneLocalEnvironmentSubmission` exist
    in the builder
  - end-frame does not construct `SceneLocalEnvironmentV3Context`
  - end-frame uses `SceneLocalEnvironmentCommon environmentCommon`
  - end-frame uses `SceneLocalEnvironmentSubmission environmentSubmission`
  - end-frame submits through
    `SubmitSceneLocalEnvironment(environmentCommon, environmentSubmission)`

Validation:

```powershell
python -m py_compile tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter "beauty,candidate_beauty_v3,candidate_hdr_scene_color,scene_local_environment,reflection_radiance,reflection_confidence,reflection_source_id,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage,material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id" -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_environment_context_builder_smoke_20260607
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_environment_context_builder_smoke_20260607 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_environment_context_builder_smoke_20260607\v3_matrix_single_packet_decision.json --output-md build\captures\v3_environment_context_builder_smoke_20260607\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- Static V3 plan validator passed.
- Native build under `VsDevCmd` rebuilt
  `FullSceneShaderV3GraphBuilder.cpp.obj`,
  `Renderer_RenderGraphEndFrame.cpp.obj`, and linked `bin\CortexEngine.exe`.
- Known trailing `vswhere.exe` warning still appears after successful builds.
- Focused packet
  `build\captures\v3_environment_context_builder_smoke_20260607` passed end to
  end:
  - `36` reports
  - V2 frame-report evidence passed
  - V3 placeholder artifacts passed
  - V3 scene profile passed
  - V3 environment payload passed
  - V3 material payload passed
  - CompositeV3 diagnostics passed
  - promotion decision status `review_packet_passed`
  - requested candidate beauty ready `6/6`, requested blockers `{}`
- Single-packet matrix passed for
  `stress_rt_showcase_reflection_closeup/static`.

Current next work:

1. Finish the same ownership move for `FullSceneCompositeV3Context`, so
   candidate HDR assembly becomes builder-owned instead of end-frame-owned.
2. Then pivot from plumbing to resource quality:
   - per-family scene-local diffuse, specular, and visible-background resources
   - physically safer lighting contracts for enclosed and exterior scenes
   - ReflectionV3 provider fusion for planar, screen-space, probe, and fallback
     sources
   - LightingShadowV3 ownership, shadow-source attribution, and debug views
3. Keep the release trajectory focused on full-scene shader quality, not
   automatic scene authoring. Every slice should ship with static validation,
   native build, focused packet evidence, promotion decision, and matrix gate.

## 2026-06-07 V3 Display Context Builder Checkpoint

Latest pushed work before this section:

- Commit `5ee7ca1` introduced `FullSceneShaderV3GraphBuilder` and routed V3
  pass submissions through it.

Implemented after that:

- Added `FullSceneShaderV3GraphBuilder::DisplayCommon`.
- Added `FullSceneShaderV3GraphBuilder::DisplaySubmission`.
- Added `FullSceneShaderV3GraphBuilder::SubmitDisplay(DisplayCommon,
  DisplaySubmission)`.
- `FullSceneShaderV3GraphBuilder` now constructs
  `CandidateBeautyDisplayContext` internally for candidate/debug display
  passes.
- `Renderer_RenderGraphEndFrame.cpp` now builds shared display state once and
  only submits per-view display deltas:
  - pass name
  - source resource handle
  - source SRV
  - ran flag
- Removed repeated direct `CandidateBeautyDisplayContext` construction from
  end-frame for:
  - candidate beauty display
  - CompositeV3 debug display
  - SceneLocalEnvironmentV3 debug display
  - ReflectionV3/debug-history display
- Extended `tools\validate_full_scene_shader_pipeline_v3_plan.py` so the
  static V3 validator enforces:
  - `DisplayCommon` and `DisplaySubmission` exist in the builder
  - end-frame does not construct `CandidateBeautyDisplayContext`
  - end-frame uses `DisplayCommon displayCommon`
  - end-frame submits displays through `SubmitDisplay(displayCommon, ...)`

Validation:

```powershell
python -m py_compile tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter "beauty,candidate_beauty_v3,candidate_hdr_scene_color,scene_local_environment,reflection_radiance,reflection_confidence,reflection_source_id,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage,material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id" -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_display_context_builder_smoke_20260607
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_display_context_builder_smoke_20260607 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_display_context_builder_smoke_20260607\v3_matrix_single_packet_decision.json --output-md build\captures\v3_display_context_builder_smoke_20260607\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- Static V3 plan validator passed.
- Native build under `VsDevCmd` rebuilt
  `FullSceneShaderV3GraphBuilder.cpp.obj`,
  `Renderer_RenderGraphEndFrame.cpp.obj`, and linked `bin\CortexEngine.exe`.
- Known trailing `vswhere.exe` warning still appears after successful builds.
- Focused packet
  `build\captures\v3_display_context_builder_smoke_20260607` passed end to
  end:
  - `36` reports
  - V2 frame-report evidence passed
  - V3 placeholder artifacts passed
  - V3 scene profile passed
  - V3 environment payload passed
  - V3 material payload passed
  - CompositeV3 diagnostics passed
  - promotion decision status `review_packet_passed`
- Single-packet matrix passed for
  `stress_rt_showcase_reflection_closeup/static`.

Current next work:

1. Continue moving repeated context construction into the builder. Best next
   candidates:
   - `SceneLocalEnvironmentV3Context` construction, because it has a clear
     payload-binding boundary.
   - `FullSceneCompositeV3Context` construction, because it is the candidate
     HDR assembly boundary.
2. After one more builder-owned context family, resume resource-quality work:
   per-family scene-local diffuse/specular/background resources,
   LightingShadowV3 ownership, and ReflectionV3 provider fusion.
3. Keep every structural move behind static validation, native build, focused
   packet, promotion decision, and single-packet matrix evidence.

## 2026-06-07 V3 Graph Builder Facade Checkpoint

Latest pushed work before this section:

- Commit `714dff6` extracted non-trivial V3 pass helpers from
  `Renderer_RenderGraphEndFrame.cpp` into `FullSceneShaderV3Passes`.

Implemented after that:

- Added `src\Graphics\FullSceneShaderV3GraphBuilder.h`.
- Added `src\Graphics\FullSceneShaderV3GraphBuilder.cpp`.
- Registered the builder source/header in `CMakeLists.txt`.
- `Renderer_RenderGraphEndFrame.cpp` now creates one
  `FullSceneShaderV3GraphBuilder fullSceneShaderV3` after render-graph
  `BeginFrame()`.
- End-frame no longer directly calls low-level V3 helper functions:
  `AddSceneLocalEnvironmentV3Pass`, `AddFullSceneReflectionResolverV3Pass`,
  `AddFullSceneReflectionHistoryV3Pass`,
  `AddFullSceneReflectionHistoryV3CopyPass`, `AddFullSceneCompositeV3Pass`,
  or `AddCandidateBeautyDisplayPass`.
- End-frame still builds the same context structs in this slice. The builder is
  a submission façade only; later slices can move context construction into the
  builder without changing pass behavior.
- Extended `tools\validate_full_scene_shader_pipeline_v3_plan.py` so it now
  checks:
  - builder header/source existence
  - builder tokens and submit methods
  - CMake compilation of `FullSceneShaderV3GraphBuilder.cpp`
  - end-frame includes/constructs the builder
  - end-frame does not call low-level V3 pass helpers directly

Validation:

```powershell
python -m py_compile tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter "beauty,candidate_beauty_v3,candidate_hdr_scene_color,scene_local_environment,reflection_radiance,reflection_confidence,reflection_source_id,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution,direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage,material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id" -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_graph_builder_smoke4_20260607
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_graph_builder_smoke4_20260607 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_graph_builder_smoke4_20260607\v3_matrix_single_packet_decision.json --output-md build\captures\v3_graph_builder_smoke4_20260607\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- Static V3 plan validator passed.
- Native build under `VsDevCmd` configured, built
  `FullSceneShaderV3GraphBuilder.cpp.obj`, rebuilt
  `Renderer_RenderGraphEndFrame.cpp.obj`, and linked `bin\CortexEngine.exe`.
- Known trailing `vswhere.exe` warning still appears after the successful
  build.
- Focused packet
  `build\captures\v3_graph_builder_smoke4_20260607` passed end to end:
  - `36` reports
  - V2 frame-report evidence passed
  - V3 placeholder artifacts passed
  - V3 scene profile passed
  - V3 environment payload passed
  - V3 material payload passed
  - CompositeV3 diagnostics passed
  - promotion decision status `review_packet_passed`
- Single-packet matrix passed for
  `stress_rt_showcase_reflection_closeup/static`.

Current next work:

1. Move one narrow context-construction family into
   `FullSceneShaderV3GraphBuilder` without changing behavior. Best first
   candidate: display/debug display submission because it is repeated for
   candidate beauty, composite debug, environment debug, and reflection debug.
2. Keep each structural move validated with static validator, native build, and
   focused V3 packet evidence.
3. After builder owns repeated context construction, resume resource-quality
   work: scene-local diffuse/specular/background generation, true
   LightingShadowV3 ownership, and ReflectionV3 provider fusion.

## 2026-06-07 V3 Pass Helper Extraction Checkpoint

Latest pushed work before this section:

- Commit `82272ae` planned the full-scene shader renderer refactor and made
  structural extraction the next implementation slice.

Implemented after that:

- Added `src\Graphics\Passes\FullSceneShaderV3Passes.h`.
- Added `src\Graphics\Passes\FullSceneShaderV3Passes.cpp`.
- Moved the non-trivial V3 pass contexts and pass-add helpers out of
  `src\Graphics\Renderer_RenderGraphEndFrame.cpp`:
  - `SceneLocalEnvironmentV3Context`
  - `FullSceneReflectionResolverV3Context`
  - `FullSceneReflectionHistoryV3Context`
  - `FullSceneReflectionHistoryV3CopyContext`
  - `FullSceneCompositeV3Context`
  - `CandidateBeautyDisplayContext`
  - `AddSceneLocalEnvironmentV3Pass`
  - `AddFullSceneReflectionResolverV3Pass`
  - `AddFullSceneReflectionHistoryV3Pass`
  - `AddFullSceneReflectionHistoryV3CopyPass`
  - `AddFullSceneCompositeV3Pass`
  - `AddCandidateBeautyDisplayPass`
- `Renderer_RenderGraphEndFrame.cpp` now keeps the end-frame orchestration and
  imports `FullSceneShaderV3Passes` instead of owning those implementation
  details.
- Registered the new source/header in `CMakeLists.txt`.
- Extended `tools\validate_full_scene_shader_pipeline_v3_plan.py` so the
  static V3 validator enforces this boundary:
  - the new helper files must exist
  - `Renderer_RenderGraphEndFrame.cpp` must include the helper module
  - the extracted context structs must not live in end-frame
  - the helper header/source must carry the V3 pass contexts/functions
  - CMake must compile `FullSceneShaderV3Passes.cpp`

Validation:

```powershell
python -m py_compile tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && cmake --build build --config Release --target CortexEngine --parallel 8"
git diff --check -- CMakeLists.txt src\Graphics\Renderer_RenderGraphEndFrame.cpp src\Graphics\Passes\FullSceneShaderV3Passes.h src\Graphics\Passes\FullSceneShaderV3Passes.cpp tools\validate_full_scene_shader_pipeline_v3_plan.py docs\AAA_ASSET_QUALITY_HANDOFF.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_pass_helper_extraction_smoke_20260607
python tools\analyze_full_scene_shader_v3_scene_profile.py --manifest build\captures\v3_pass_helper_extraction_smoke_20260607\manifest.json --output-json build\captures\v3_pass_helper_extraction_smoke_20260607\v3_scene_profile.json --output-md build\captures\v3_pass_helper_extraction_smoke_20260607\v3_scene_profile.md --min-family-count 1
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_pass_helper_extraction_smoke_20260607\manifest.json --output-json build\captures\v3_pass_helper_extraction_smoke_20260607\v3_environment_payload.json --output-md build\captures\v3_pass_helper_extraction_smoke_20260607\v3_environment_payload.md
python tools\analyze_full_scene_shader_v3_material_payload.py --manifest build\captures\v3_pass_helper_extraction_smoke_20260607\manifest.json --output-json build\captures\v3_pass_helper_extraction_smoke_20260607\v3_material_payload.json --output-md build\captures\v3_pass_helper_extraction_smoke_20260607\v3_material_payload.md
python tools\analyze_full_scene_shader_v3_composite_diagnostics.py --manifest build\captures\v3_pass_helper_extraction_smoke_20260607\manifest.json --output-json build\captures\v3_pass_helper_extraction_smoke_20260607\v3_composite_diagnostics.json --output-md build\captures\v3_pass_helper_extraction_smoke_20260607\v3_composite_diagnostics.md
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_pass_helper_extraction_smoke_20260607 --output-json build\captures\v3_pass_helper_extraction_smoke_20260607\promotion_decision.json --output-md build\captures\v3_pass_helper_extraction_smoke_20260607\promotion_decision.md --allow-subset-review
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_pass_helper_extraction_smoke_20260607 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_pass_helper_extraction_smoke_20260607\v3_matrix_single_packet_decision.json --output-md build\captures\v3_pass_helper_extraction_smoke_20260607\v3_matrix_single_packet_decision.md
```

Results:

- Python compile passed.
- V3 plan validator passed.
- Native build under `VsDevCmd` reported `ninja: no work to do`; the generated
  build graph contains `FullSceneShaderV3Passes.cpp.obj`.
- `git diff --check` passed.
- Running `cmake --build` outside `VsDevCmd` failed with missing standard
  header `memory`; treat that as an environment invocation issue, not a code
  failure.
- The packet wrapper exceeded a 5-minute tool timeout after capture/analyzer
  artifacts were written. The leftover `CortexEngine` process was stopped.
- Captured packet root:
  `build\captures\v3_pass_helper_extraction_smoke_20260607`.
- Evidence present:
  - `54` frame reports
  - `debug_view_metrics.json/md`
  - `v2_frame_report_evidence_summary.json/md`
  - `v3_signal.json`
  - `v3_stability.json`
  - `v3_scene_profile.json/md`
  - `v3_environment_payload.json/md`
  - `v3_material_payload.json/md`
  - `v3_composite_diagnostics.json/md`
  - `promotion_decision.json/md`
  - `v3_matrix_single_packet_decision.json/md`
- Focused analyzer reruns passed:
  scene profile with `--min-family-count 1`, environment payload, material
  payload, and CompositeV3 diagnostics.
- Promotion decision passed with
  `status=review_packet_passed`.
- Single-packet matrix passed for
  `stress_rt_showcase_reflection_closeup/static`.

Current next work:

1. Continue the structural refactor by introducing a small
   `FullSceneShaderV3GraphBuilder` façade around the V3 pass calls.
2. Keep behavior-preserving validation: focused candidate-beauty packet,
   promotion decision, and single-packet matrix after each structural move.
3. Only after that, resume resource-quality work: scene-local environment
   diffuse/specular/background resources, true LightingShadowV3 ownership, and
   stronger ReflectionV3 provider fusion.

## 2026-06-07 Full Refactor Plan Before Goal Feature

User direction:

- Move to full-scene shaders capable of high-end Unreal-style visuals.
- Plan the entire renderer refactor before implementing the goal feature.
- Do not count stronger post, blurrier IBL, disabled features, or scene swaps
  as correctness.

Controlling plan update:

- Updated `docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` with
  `2026-06-07 Full Refactor Plan Before Goal Feature`.
- Treat this new section as the current implementation guide.
- Current diagnosis:
  - V3 pieces exist, but too much candidate graph assembly still lives in
    `src\Graphics\Renderer_RenderGraphEndFrame.cpp`.
  - `FullSceneLightingV3` is instrumented and split, but still partly
    adapter-owned by the visibility-buffer deferred path.
  - `SceneLocalEnvironmentV3` has payload aliases and real SRV binding, but
    still needs per-family baked local irradiance/specular/background
    resources.
  - `ReflectionV3` has source ids, confidence, history, and rejection views,
    but still needs stronger SSR/local/hero/planar/RT/environment provider
    fusion.
  - `CompositeV3` and `CinematicPostV3` are narrow-path candidate-ready, but
    cross-family HDR ownership and low legacy rescue remain required.
- Refactor principle:
  `policy -> typed producer -> named resources -> shader contribution -> debug
  view -> frame report -> analyzer -> packet matrix -> promotion decision`.
- Target module boundaries:
  `SceneProfileV3`, `VisibilityV3`, `MaterialPayloadV3`,
  `SceneLocalEnvironmentV3`, `LightingShadowV3`, `ReflectionV3`,
  `TransparencyMediaV3`, `CompositeV3`, `CinematicPostV3`, and `PromotionV3`.
- Immediate next coding slice should be structural:
  1. Extract `SceneLocalEnvironmentV3`, `FullSceneReflectionV3`,
     `FullSceneReflectionHistoryV3`, `FullSceneCompositeV3`, and candidate
     display helper code out of `Renderer_RenderGraphEndFrame.cpp`.
  2. Preserve current packet behavior exactly.
  3. Add a validator rule that non-trivial V3 pass helpers live outside
     end-frame assembly.
  4. Update this handoff with the ownership boundary and evidence.

Do not start by tuning beauty. The next implementation pass should make the
renderer tractable enough that environment, lighting, reflection, composite,
and post can be improved with source attribution and packet evidence.

## 2026-06-07 Promotion/Matrix Candidate Predicate Summary Checkpoint

Latest pushed work before this section:

- Commit `e929479` added candidate-beauty predicate debt to runtime frame
  reports and `candidate_path_debt`.

Implemented after that:

- `tools\analyze_full_scene_shader_v3_placeholders.py` now carries the
  individual candidate predicate booleans into `v3_signal.json` rows:
  composite ready, cinematic post ready, candidate LDR output ready, candidate
  HDR read ready, legacy bridge rejected, and default beauty unchanged.
- `tools\build_full_scene_shader_v3_promotion_decision.py` now emits
  `candidate_beauty_predicates`:
  - report count
  - requested report count
  - ready report count
  - predicate count
  - min/max ready predicate count
  - per-predicate ready report counts
  - blocker counts
  - requested-report blocker counts
- `tools\build_full_scene_shader_v3_matrix_decision.py` now carries candidate
  ready/requested counts and blocker summaries into each matrix packet row and
  aggregates blocker counts across packets.
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` now includes the
  matrix runner/analyzer in the checked runtime surface and requires the
  candidate predicate summary tokens.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py tools\build_full_scene_shader_v3_matrix_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607 --signal-output build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607\v3_signal.json --stability-output build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607\v3_stability.json --require-lighting-split-ready --require-lighting-split-draw-count 1 --require-lighting-signal-metrics
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607 --output-json build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607\promotion_decision.json --output-md build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607\promotion_decision.md --allow-subset-review
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607 --required-families stress_rt_showcase_reflection_closeup --required-motion-modes static --output-json build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607\v3_matrix_single_packet_decision.json --output-md build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607\v3_matrix_single_packet_decision.md
```

Evidence:

- Regenerated `promotion_decision.json` reports:
  - `candidate_beauty_requested_report_count=6`
  - `candidate_beauty_ready_report_count=6`
  - `predicate_count=6`
  - `min_ready_predicate_count=1`
  - `max_ready_predicate_count=6`
  - per-predicate ready counts:
    `composite_ready=6`, `cinematic_post_ready=6`,
    `ldr_output_ready=6`, `reads_candidate_hdr=6`,
    `legacy_bridge_rejected=54`, `default_beauty_unchanged=54`
  - blocker counts for non-candidate views:
    `candidate_beauty_not_requested=48`,
    `composite_v3_not_ready=48`,
    `cinematic_post_v3_not_ready=48`,
    `candidate_ldr_output_missing=48`,
    `candidate_hdr_input_missing=48`
  - requested-report blocker counts `{}`.
- Regenerated single-packet matrix reports:
  - full matrix ready for the intentionally narrow
    `stress_rt_showcase_reflection_closeup/static` requirement.
  - packet row shows candidate ready `6/6`.
  - matrix-level candidate blocker counts match the promotion summary.

Current next work:

1. Run a bounded cross-family candidate-beauty matrix using existing packet
   controls, keeping known model-scene crash/device-removal debt separate from
   report-evidence readiness.
2. If cross-family packets show requested candidate blockers, use the new
   predicate summary to target the failing layer rather than tuning visuals.
3. Continue CompositeV3 legacy-rescue reduction and CinematicPostV3 polish only
   after candidate predicates stay stable across more than the stress gallery
   scene.

## 2026-06-07 Candidate Beauty Predicate Debt Checkpoint

Latest pushed work before this section:

- Commit `7df9521` closed the `local_reflection_radiance` RenderGraphV3
  missing-producer debt.

Implemented after that:

- `FullSceneShaderPipelineV3FrameContext` now reports candidate-beauty
  readiness as explicit predicates instead of an all-or-nothing missing-channel
  count:
  - `candidate_beauty_composite_ready`
  - `candidate_beauty_cinematic_post_ready`
  - `candidate_beauty_ldr_output_ready`
  - `candidate_beauty_reads_candidate_hdr`
  - `candidate_beauty_legacy_bridge_rejected`
  - `candidate_beauty_default_beauty_unchanged`
  - `candidate_beauty_predicate_count`
  - `candidate_beauty_ready_predicate_count`
  - `candidate_beauty_blockers`
- `candidate_path_debt` now mirrors the predicate count, ready predicate count,
  and blocker list so debt reports identify the actual failed candidate-beauty
  predicate.
- The candidate gate remains strict. Candidate beauty is still ready only when
  requested, CompositeV3 is ready, CinematicPostV3 is ready, candidate LDR is
  written from candidate HDR, the legacy HDR bridge is absent, and default
  beauty remains unchanged.
- `tools\analyze_full_scene_shader_v3_placeholders.py` now validates predicate
  ranges, blocker consistency, top-level/debt agreement, and no remaining
  blockers when `candidate_beauty_ready=true`.
- `assets\final_art\full_scene_shader_pipeline_v3_contract.json` and
  `tools\validate_full_scene_shader_pipeline_v3_plan.py` require these fields.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607
```

Evidence:

- Packet:
  `build\captures\v3_candidate_beauty_predicate_debt_smoke_20260607` passed
  end to end.
- In the ordinary `beauty` report:
  - `candidate_beauty_requested=false`
  - `candidate_beauty_ready_predicate_count=1`
  - blockers:
    `candidate_beauty_not_requested`, `composite_v3_not_ready`,
    `cinematic_post_v3_not_ready`, `candidate_ldr_output_missing`,
    `candidate_hdr_input_missing`
  - `candidate_beauty_missing_required_channels=5`
- In the `candidate_beauty_v3` report:
  - `candidate_beauty_requested=true`
  - `candidate_beauty_ready=true`
  - `candidate_beauty_predicate_count=6`
  - `candidate_beauty_ready_predicate_count=6`
  - `candidate_beauty_blockers=[]`
  - `candidate_beauty_missing_required_channels=0`
  - `total_missing_required_channels=0`
  - `render_graph_missing_producer_count=0`

Current next work:

1. Use the predicate debt fields in the promotion/matrix summaries so packet
   failures identify the exact predicate and view scope.
2. Run a bounded cross-family candidate-beauty matrix after choosing a small
   family set and keeping model-scene crash debt separate from report evidence.
3. Continue reducing CompositeV3 legacy rescue usage and improving
   CinematicPostV3 only after cross-family candidate predicate evidence is
   stable.

## 2026-06-07 Current AAA Full-Scene Shader Resume Point

User direction:

- Move from local renderer fixes toward full-scene shaders that can reach
  high-end realtime/Unreal-style visuals.
- Plan and execute the architecture before calling any goal feature complete.
- Do not use IBL blur, disabled features, post strength, or scene swaps as
  correctness proof.

Latest planning checkpoint:

- Added `2026-06-07 Whole-Renderer Refactor Blueprint` near the top of
  `docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`.
- Treat that section as the highest-level plan before continuing feature work.
  It reframes the goal as a whole candidate renderer, not a single reflection,
  IBL, material, or post-processing fix.
- The target visual path is still opt-in `FullSceneShaderV3`, with default
  beauty unchanged until promotion evidence and user review pass.
- Required refactor layers:
  `SceneProfileV3`, `RenderGraphV3`, `VisibilityV3`,
  `MaterialPayloadV3`, `SceneLocalEnvironmentV3`, `LightingShadowV3`,
  `ReflectionV3`, `TransparencyMediaV3`, `CompositeV3`,
  `CinematicPostV3`, and `PromotionV3`.
- The new blueprint defines ownership rules:
  scene profile owns environment permissions, material payload owns PBR channel
  synthesis, lighting owns shadow/source attribution, reflection owns provider
  selection, composite owns HDR contribution balance, and post owns presentation
  only.
- The recommended next coding slice is foundation work, not visual polishing:
  finish the current `SceneLocalEnvironmentV3` runtime filtered-proxy reporting
  slice, add `RenderGraphV3` pass/resource inventory to frame reports, add a
  static ownership validator for V3 shader/debug/report/analyzer links, add
  candidate-path debt fields, then run one focused packet and one cross-family
  report-only packet.
- Completion boundary remains strict: no completion until `FullSceneShaderV3`
  can own candidate HDR, explain all major visible terms through debug/report
  evidence, pass cross-family and motion packets, and satisfy user beauty
  review.

Latest runtime proxy contract checkpoint:

- Continued the first implementation slice from the renderer blueprint:
  finish `SceneLocalEnvironmentV3` runtime reporting for filtered generated
  proxy resources.
- `tools\generate_scene_local_environment_proxies.py` now writes
  `proxyResourceShape`, `filteredOutputCount`, and `minFilterVariance` into
  `src\Graphics\Generated\SceneLocalProxyContracts.generated.h`.
- Runtime propagation now covers:
  `Generated::SceneLocalProxyContractRecord ->
  Renderer::SceneLocalEnvironmentV3PayloadBindingInfo ->
  FrameContract::EnvironmentInfo ->
  FullSceneShaderPipelineV3FrameContext -> FrameContractJson`.
- New JSON fields:
  - top-level V3:
    `scene_local_environment_proxy_resource_shape`,
    `scene_local_environment_proxy_filtered_output_count`,
    `scene_local_environment_proxy_min_filter_variance`
  - nested environment:
    `scene_local_proxy_resource_shape`,
    `scene_local_proxy_filtered_output_count`,
    `scene_local_proxy_min_filter_variance`
- `tools\analyze_full_scene_shader_v3_environment_payload.py` now fails
  payload-ready reports if runtime, V3, and manifest proxy shape/filter stats do
  not agree. Its `filtered_proxy_report_count` is runtime-based now.
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` and
  `assets\final_art\full_scene_shader_pipeline_v3_contract.json` now require
  these proxy filter fields.

Validation commands for this checkpoint:

```powershell
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
```

Evidence:

- Static validation passed:
  - Python compile for proxy generator, environment payload analyzer, and V3
    plan validator
  - `python tools\validate_full_scene_shader_pipeline_v3_plan.py`
  - `git diff --check` on focused files
- Native `CortexEngine` target built successfully with
  `CORTEX_SKIP_ASSET_SYNC=1`; the known trailing `vswhere.exe` warning printed
  after link.
- Focused packet:
  `build\captures\v3_scene_local_runtime_filter_proxy_fresh_smoke_20260607`
  - reports `54`
  - payload ready `54`
  - runtime filtered proxy reports `54`
  - explicit proxy binding reports `54`
  - scene-contract proxy reports `54`
  - failures `0`
  - runtime shape `filtered_directional_bc1_v1`
  - minimum runtime filter variance `0.014379`
- Cross-profile packet:
  `build\captures\v3_scene_local_runtime_filter_proxy_cross_profile_20260607`
  - families `gallery,office,concert,stadium`
  - reports `4`
  - payload ready `4`
  - runtime filtered proxy reports `4`
  - explicit proxy binding reports `4`
  - scene-contract proxy reports `4`
  - failures `0`
  - runtime shape `filtered_directional_bc1_v1`
  - minimum runtime filter variance `0.014379`

Latest RenderGraphV3 inventory checkpoint:

- Continued the second foundation slice from the renderer blueprint:
  expose a V3 pass/resource inventory in runtime reports.
- `FullSceneShaderPipelineV3FrameContext` now derives inventory from existing
  `FrameContract::PassRecord` entries instead of maintaining a parallel pass
  list.
- V3 pass/resource classification includes named V3 passes and resources such
  as scene-local environment, lighting split outputs, reflection resources,
  candidate HDR/LDR, composite contribution/debt, and relevant legacy bridge
  resources.
- New top-level V3 JSON fields:
  - `render_graph_v3_inventory_ready`
  - `render_graph_v3_pass_count`
  - `render_graph_v3_executed_pass_count`
  - `render_graph_v3_read_resource_count`
  - `render_graph_v3_write_resource_count`
  - `render_graph_v3_missing_producer_count`
  - `render_graph_v3_pass_names`
  - `render_graph_v3_read_resources`
  - `render_graph_v3_written_resources`
  - `render_graph_v3_missing_producer_resources`
- The V3 `render_graph` domain now reports producer
  `RenderGraphV3Inventory`, output `v3_resource_inventory`, debug view
  `v3_resource_ownership`, and backing resources from the runtime written
  resource set.
- `tools\analyze_full_scene_shader_v3_placeholders.py` now allows
  `render_graph` to be ready and fails reports where the V3 inventory is
  missing or empty.
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` and
  `assets\final_art\full_scene_shader_pipeline_v3_contract.json` require the
  new inventory fields.
- Fresh focused packet:
  `build\captures\v3_render_graph_inventory_fresh_smoke_20260607`
  passed end to end.
  - sampled report:
    `stress_rt_showcase_reflection_closeup\beauty\frame_report_shutdown.json`
  - `render_graph_v3_inventory_ready=true`
  - V3 inventory pass count `20`
  - V3 executed pass count `20`
  - V3 read resource count `11`
  - V3 written resource count `28`
  - V3 missing producer count `1`
  - missing producer resource: `local_reflection_radiance`
  - V3 `render_graph` domain is now ready/instrumented with producer
    `RenderGraphV3Inventory`, output `v3_resource_inventory`, and debug view
    `v3_resource_ownership`
- Interpretation:
  - The renderer now has a real V3 pass/resource inventory in runtime JSON.
  - `local_reflection_radiance` is visible remaining provider debt for the next
    ReflectionV3/local-probe slice, not a hidden failure.

Latest candidate-path debt checkpoint:

- Added explicit `candidate_path_debt` to
  `frame_contract.full_scene_shader_pipeline_v3`.
- Debt is derived from existing V3 domain evidence and RenderGraphV3 inventory,
  not maintained as independent state.
- Debt fields:
  - `render_graph_missing_producer_count`
  - `render_graph_missing_producer_resources`
  - `material_missing_required_channels`
  - `lighting_missing_required_channels`
  - `environment_missing_required_channels`
  - `reflection_missing_required_channels`
  - `composite_missing_required_channels`
  - `cinematic_post_missing_required_channels`
  - `candidate_beauty_missing_required_channels`
  - `total_missing_required_channels`
  - `not_ready_domain_count`
  - `legacy_rescue_resource_ready`
- `tools\analyze_full_scene_shader_v3_placeholders.py` now fails if
  `candidate_path_debt` or required debt fields are missing.
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` and
  `assets\final_art\full_scene_shader_pipeline_v3_contract.json` now require
  the debt field list.
- Fresh focused packet:
  `build\captures\v3_candidate_path_debt_fresh_smoke_20260607`
  passed end to end.
- Sampled debt object from
  `stress_rt_showcase_reflection_closeup\beauty\frame_report_shutdown.json`:
  - `total_missing_required_channels=8`
  - `not_ready_domain_count=4`
  - `render_graph_missing_producer_count=1`
  - `render_graph_missing_producer_resources=["local_reflection_radiance"]`
  - `candidate_beauty_missing_required_channels=6`
  - `composite_missing_required_channels=2`
  - material, lighting, environment, reflection, and cinematic post missing
    required channels are all `0`
  - `legacy_rescue_resource_ready=false`

Latest local reflection radiance producer checkpoint:

- Closed the explicit RenderGraphV3 missing-producer debt for
  `local_reflection_radiance`.
- `LocalReflectionRadiancePass::GraphStatus` now has a `ran` flag, set only
  after the compute dispatch succeeds.
- `Renderer_RenderGraphEndFrame.cpp` now records a real frame pass:
  - pass: `LocalReflectionRadiance`
  - reads: `depth`, `vb_gbuffer_normal_roughness`,
    `vb_gbuffer_emissive_metallic`, `vb_gbuffer_material_ext1`,
    `vb_gbuffer_material_ext2`, `hdr_color`,
    `environment_specular_prefilter`
  - writes: `local_reflection_radiance`
- `local_reflection_radiance` is now a required V3 render-graph output in
  `assets\final_art\full_scene_shader_pipeline_v3_contract.json`, runtime
  `required_outputs`, static validator, and placeholder analyzer.
- `tools\analyze_full_scene_shader_v3_placeholders.py` now fails if
  `FullSceneReflectionV3` is ready but the `LocalReflectionRadiance` producer
  pass is missing, not executed, or does not write `local_reflection_radiance`.
- Focused packet:
  `build\captures\v3_local_reflection_radiance_producer_fresh_smoke_20260607_rerun`
  passed end to end.
  - `LocalReflectionRadiance.executed=true`
  - `LocalReflectionRadiance.writes=["local_reflection_radiance"]`
  - `render_graph_v3_missing_producer_count=0`
  - `render_graph_v3_missing_producer_resources=[]`
  - `local_reflection_radiance` appears in
    `render_graph_v3_written_resources`
  - candidate path debt now has
    `render_graph_missing_producer_count=0`
- A later packet attempt,
  `build\captures\v3_local_reflection_radiance_producer_final_smoke_20260607`,
  timed out before producing a manifest and left a wrapper process behind.
  Treat it as no evidence. The stale wrapper was cleaned up; validation after
  the final resolution-class patch is static/build validated, while runtime
  packet evidence remains the successful `..._rerun` packet above.
- Remaining candidate-path debt after this slice:
  - `total_missing_required_channels=8`
  - `candidate_beauty_missing_required_channels=6`
  - `composite_missing_required_channels=2`
  - `not_ready_domain_count=4`

Current planning checkpoint:

- Latest implementation moved `SceneLocalEnvironmentV3` proxy generation from
  filename inventory into decoded material-color sampling plus room-shell and
  light-rig influence. The generator now uses Pillow DDS decoding for
  albedo/diffuse payloads and writes `profile_payload_material_room_light_v1`
  in the proxy manifest. The analyzer now fails payload-ready reports unless
  decoded material samples and scene-contract influence are present.
- Added the expanded
  `2026-06-07 Full Scene Shader Goal Feature Execution Architecture`
  section near the top of
  `docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`.
- Treat that new execution architecture as the current runway before further
  goal-feature implementation. It defines the data model refactor, render-graph
  refactor, shader refactor, validation refactor, execution phases, near-term
  work order, and explicit non-goals.
- The main architectural move is to make `FullSceneShaderV3` a typed producer
  stack:
  `SceneProfileV3 -> VisibilityV3 -> MaterialPayloadV3 ->
  SceneLocalEnvironmentV3 -> LightingShadowV3 -> ReflectionV3 ->
  TransparencyMediaV3 -> CompositeV3 -> CinematicPostV3 -> PromotionV3`.
- Each domain must prove:
  `policy input -> producer/resource -> shader contribution -> debug view ->
  frame report fields -> analyzer -> packet evidence`.
- Near-term implementation should start upstream:
  decoded material-color sampling and room/light influence for
  `SceneLocalEnvironmentV3`, then report/capture separation, bounded
  `ReflectionV3` family evidence, and continued `LightingShadowV3` close-surface
  stress. `TransparencyMediaV3` and heavy `CinematicPostV3` stay deferred until
  opaque HDR ownership is credible.
- Explicit non-goal: do not count IBL blur, disabled reflections, reduced
  sharpness, scene swaps, or post masking as fixes.
- Added the short authoritative
  `2026-06-07 Goal Feature Refactor Plan - Full Scene Shader Renderer`
  section near the top of
  `docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`.
- Treat that section as the current goal-feature plan. It defines the product
  goal as an opt-in `FullSceneShaderV3` candidate renderer, not a single visual
  preset or post-process pass.
- The required renderer stack is:
  `SceneProfileV3`, `VisibilityV3`, `MaterialPayloadV3`,
  `SceneLocalEnvironmentV3`, `LightingShadowV3`, `ReflectionV3`,
  `TransparencyMediaV3`, `CompositeV3`, `CinematicPostV3`, and `PromotionV3`.
- Every visible term must prove the chain:
  `SceneProfileV3 policy -> typed producer/resource -> shader contribution ->
  debug view -> frame-report fields -> analyzer gate -> packet/promotion
  evidence`.
- Immediate implementation priority after planning:
  1. harden `ReflectionV3` provider resolver because smooth/metallic jitter is
     still a user-visible defect class
  2. expand `SceneLocalEnvironmentV3` from payload aliases toward local
     irradiance/specular/background proxy resources
  3. separate cross-family frame-report diagnostics from visual capture success
     so model-scene crashes do not block evidence
  4. add `CompositeV3` legacy-rescue accounting
  5. defer strong `CinematicPostV3` polish until upstream ownership is real
- Completion remains blocked until candidate beauty passes matrix packets and
  user review. One good screenshot or one fixed stress scene is not completion.

Latest ReflectionV3 source resolver checkpoint:

- Implemented bounded auto-source hysteresis in
  `assets\shaders\FullSceneReflectionResolverV3.hlsl`.
  - It only affects auto policy; forced `local`, `ssr`, `rt`, `environment`,
    and `none` overrides remain explicit diagnostics.
  - It holds the previous source only when previous history is reusable, the
    same provider is still available at the pixel, and the current winner is
    not decisively better.
  - The hold strength is reported through the existing rejected-source alpha
    lane together with history/material suppression.
- Added `tools\analyze_reflection_v3_source_resolver.py`.
  - It treats `reflection_source_id` as categorical evidence, not generic
    luma.
  - Red channel is measured as provider class, green as confidence, blue as
    override policy.
  - It reports mean source delta, max source-switch ratio, max active
    source-switch ratio, dominant source, and warnings.
- Wired the analyzer into
  `tools\run_reflection_v3_motion_focus_packet.ps1`, emitting:
  - `v3_reflection_source_resolver.json`
  - `v3_reflection_source_resolver.md`
- The focused runner now defaults to `-SourceOverride auto` because production
  resolver evidence should test the actual auto policy. Forced SSR remains
  available with `-SourceOverride ssr`.
- Updated `tools\validate_full_scene_shader_pipeline_v3_plan.py` so the
  reflection resolver shader, focused runner, and new source analyzer are part
  of the checked V3 runtime surface.

Validation for latest ReflectionV3 slice:

```powershell
python -m py_compile tools\analyze_reflection_v3_source_resolver.py tools\analyze_reflection_v3_material_stress.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$files=@('tools\run_reflection_v3_motion_focus_packet.ps1','tools\run_reflection_v3_material_stress_packet.ps1'); foreach($file in $files){$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file), [ref]$tokens, [ref]$errors) | Out-Null; if($errors.Count -gt 0){Write-Host $file; $errors | Format-List; exit 1}}
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_reflection_source_hysteresis_focus_20260607 -SourceOverride auto -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -MotionFrames 72 -MotionLookAmplitude 0.025 -MotionLookCycles 6.0
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607 -SourceOverride ssr -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -MotionFrames 72 -MotionLookAmplitude 0.025 -MotionLookCycles 6.0
```

Evidence:

- Native `CortexEngine` target remained buildable; the known trailing
  `vswhere.exe` warning printed after Ninja success.
- Auto resolver packet:
  `build\captures\v3_reflection_source_hysteresis_focus_20260607`
  passed end to end.
  - Generic reflection motion analyzer: `13` view sequences, `0` warnings,
    `0` failures.
  - Source resolver analyzer: `1` family, `0` warnings, `0` failures.
  - Source row: dominant source `local`, active source `1.00000`, mean source
    delta `0.000151`, max source switch `0.000442`, max active source switch
    `0.000442`, mean confidence delta `0.002472`.
- Forced SSR packet:
  `build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607`
  passed the wrapper but intentionally produced source-resolver warnings:
  max switch `0.101157`, max active switch `0.261804`.
  Treat this as useful remaining stress evidence for SSR holes/source churn, not
  as production auto-policy failure.

Current next work:

1. Use the forced-SSR warning packet to diagnose whether the churn is SSR holes,
   history rejection, or source fallback around specific pixels.
2. Extend the source analyzer to consume resolver/rejection channels beyond RGB
   if a raw buffer export exists or can be added.
3. Promote auto source-resolver evidence from one stress family into a bounded
   cross-family reflection packet after model-scene capture/report separation.

Latest ReflectionV3 forced-source diagnosis checkpoint:

- Extended `tools\analyze_reflection_v3_source_resolver.py` beyond categorical
  source-id churn.
- The analyzer now also summarizes diagnostic debug views when present:
  - `reflection_ssr_source_signal`
  - `reflection_rt_source_signal`
  - `reflection_rejected_source_mask`
  - `reflection_temporal_delta`
  - `reflection_source_suppression`
  - `reflection_history_v3_validity`
  - `reflection_history_v3_rejection`
- The markdown now includes a per-family diagnosis list and a diagnostic-channel
  table with mean RGB, motion RGB delta, and active motion RGB ratio.
- Updated the V3 static validator so this richer source-resolver diagnostic
  surface is checked.

Validation for forced-source diagnosis:

```powershell
python -m py_compile tools\analyze_reflection_v3_source_resolver.py
python tools\analyze_reflection_v3_source_resolver.py --manifest build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607\manifest.json --output-json build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607\v3_reflection_source_resolver_diagnostic.json --output-md build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607\v3_reflection_source_resolver_diagnostic.md
python tools\analyze_reflection_v3_source_resolver.py --manifest build\captures\v3_reflection_source_hysteresis_focus_20260607\manifest.json --output-json build\captures\v3_reflection_source_hysteresis_focus_20260607\v3_reflection_source_resolver_diagnostic.json --output-md build\captures\v3_reflection_source_hysteresis_focus_20260607\v3_reflection_source_resolver_diagnostic.md
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_reflection_source_diagnostic_auto_focus_20260607 -SourceOverride auto -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -MotionFrames 72 -MotionLookAmplitude 0.025 -MotionLookCycles 6.0
```

Evidence:

- Fresh integrated auto packet:
  `build\captures\v3_reflection_source_diagnostic_auto_focus_20260607`
  - runner passed end to end
  - generic reflection motion analyzer: `13` view sequences, `0` warnings,
    `0` failures
  - source resolver analyzer: `1` family, `0` warnings, `0` failures
  - dominant source `local`, active source `1.00000`, mean source delta
    `0.000151`, max source switch `0.000442`, max active source switch
    `0.000442`, mean confidence delta `0.002472`
- Auto-source packet remained clean:
  `build\captures\v3_reflection_source_hysteresis_focus_20260607`
  - warnings `0`
  - max source switch `0.000442`
  - max active source switch `0.000442`
- Forced-SSR packet remained intentionally noisy:
  `build\captures\v3_reflection_source_hysteresis_forced_ssr_focus_20260607`
  - warnings `2`
  - max source switch `0.101157`
  - max active source switch `0.261804`
  - diagnosis:
    `ssr_signal_changes_under_motion`,
    `ssr_rejection_mask_high`,
    `ssr_rejection_changes_under_motion`,
    `forced_or_history_debt_present`,
    `temporal_delta_tracks_source_churn`,
    `material_suppression_contributes`,
    `history_validity_changes_under_motion`
- Interpretation:
  - The forced-SSR instability is not an unexplained material flicker anymore.
  - It is SSR-provider churn/holes plus rejection and temporal/material debt.
  - Do not make forced SSR the production path for this view.
  - Auto resolver/hysteresis is the correct production gate until better SSR
    continuity or probe/RT provider evidence exists.

Current next work after this checkpoint:

1. Add an explicit SSR continuity/coverage debug lane if we need pixel-level
   hole attribution beyond RGB packet summaries.
2. Add local-probe or RT fallback confidence proof so auto mode has richer
   alternatives than local/environment fallback on glossy surfaces.
3. Promote `ReflectionV3` auto resolver packet evidence across more families.

Latest CompositeV3 promotion gate checkpoint:

- `tools\build_full_scene_shader_v3_promotion_decision.py` now requires and
  consumes `v3_composite_diagnostics.json`.
- The promotion decision evidence map now includes `composite_diagnostics`.
- Composite diagnostic failures and warnings are folded into promotion
  failures/warnings.
- Promotion decisions now expose:
  - `mean_explicit_legacy_rescue`
  - `mean_legacy_rescue`
  - `mean_clamp_mask`
  - `mean_clamp_ratio`
  - `mean_direct_contribution`
  - `mean_reflection_contribution`
- Promotion now blocks when:
  - explicit legacy rescue mean exceeds `0.05`
  - overbright legacy rescue mean exceeds `0.05`
  - clamp mask or clamp ratio exceeds `0.10`
  - requested candidate beauty has no meaningful direct/reflection
    contribution
- Updated the V3 static validator to require the CompositeV3 promotion metrics.

Validation for CompositeV3 promotion gate:

```powershell
python -m py_compile tools\build_full_scene_shader_v3_promotion_decision.py tools\analyze_full_scene_shader_v3_composite_diagnostics.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_environment_payload_resource_binding_gallery_20260607 --output-json build\captures\v3_environment_payload_resource_binding_gallery_20260607\promotion_decision_composite_gate_probe.json --output-md build\captures\v3_environment_payload_resource_binding_gallery_20260607\promotion_decision_composite_gate_probe.md --allow-subset-review
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_promotion_gate_fresh_smoke_20260607
```

Evidence:

- Probe promotion decision passed with status `review_packet_passed`.
- The promotion markdown now includes a `CompositeV3 Diagnostics` section.
- Probe metrics from
  `build\captures\v3_environment_payload_resource_binding_gallery_20260607`:
  - mean explicit legacy rescue `0.000000`
  - mean legacy rescue `0.000000`
  - mean clamp mask `0.000110`
  - mean clamp ratio `0.000031`
  - mean direct contribution `0.642235`
  - mean reflection contribution `0.011335`
- Fresh V3 smoke packet:
  `build\captures\v3_composite_promotion_gate_fresh_smoke_20260607`.
  - scene-local packet passed
  - V2 evidence passed
  - V3 placeholder packet passed with `54` reports
  - scene profile, environment payload, material payload, CompositeV3
    diagnostics, and promotion decision passed
  - promotion status `review_packet_passed`
  - default beauty remained non-promotable because the packet intentionally
    covered only `stress_rt_showcase_reflection_closeup` and static motion
  - fresh CompositeV3 metrics:
    - mean explicit legacy rescue `0.000000`
    - mean legacy rescue `0.000000`
    - mean clamp mask `0.000110`
    - mean clamp ratio `0.000031`
    - mean direct contribution `0.643191`
    - mean reflection contribution `0.011720`

Interpretation:

- Candidate beauty promotion can no longer silently ignore CompositeV3
  diagnostic artifacts.
- Clean packets with zero legacy rescue continue to pass review.
- Packets leaning on legacy HDR rescue will now be visible in the promotion
  decision and can block promotion before user review.

Current next work after this checkpoint:

1. Add a negative synthetic/fixture test for promotion blocking when
   `v3_composite_diagnostics.json` is missing or has high legacy rescue.
2. Add a broader CompositeV3 matrix row with at least two motion modes and more
   than the single stress family.
3. Continue toward `SceneLocalEnvironmentV3` proxy-resource expansion and
   broader ReflectionV3 family coverage.

Latest ReflectionV3 bounded family packet checkpoint:

- `tools\run_reflection_v3_motion_focus_packet.ps1` now supports
  `-FamilyFilter`.
- When `-FamilyFilter` is omitted, the wrapper keeps the old focused stress
  behavior and passes `-StressSceneOnly` plus `-StressSceneFilter`.
- When `-FamilyFilter` is present, the wrapper runs the scene-local packet
  runner in family mode instead of stress-only mode. This lets the same
  reflection motion analyzer, source resolver analyzer, and review sheet run on
  model/gallery families.
- Updated the static V3 validator to require the family-mode wrapper surface.

Validation for family-mode ReflectionV3 evidence:

```powershell
$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_reflection_v3_motion_focus_packet.ps1), [ref]$tokens, [ref]$errors) | Out-Null; if($errors.Count -gt 0){$errors | Format-List; exit 1}
python -m py_compile tools\analyze_reflection_v3_source_resolver.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_reflection_source_family_probe_gallery_office_20260607 -FamilyFilter gallery,office -SourceOverride auto -SmokeFrames 12 -CaptureFrame 6 -CaptureSequenceCount 2 -MotionFrames 48 -MotionLookAmplitude 0.02 -MotionLookCycles 4.0
```

Evidence:

- Family packet:
  `build\captures\v3_reflection_source_family_probe_gallery_office_20260607`
  passed end to end.
- Generic reflection motion analyzer:
  `26` view sequences across `2` families, `0` warnings, `0` failures.
- Source resolver analyzer:
  `2` families, `0` warnings, `0` failures.
- Gallery row:
  dominant source `local`, active source `1.00000`, mean source delta
  `0.000047`, max source switch `0.000152`, max active source switch
  `0.000152`, mean confidence delta `0.004118`.
- Office row:
  dominant source `local`, active source `1.00000`, mean source delta
  `0.000000`, max source switch `0.000000`, max active source switch
  `0.000000`, mean confidence delta `0.005103`.

Interpretation:

- This is the first bounded cross-family ReflectionV3 auto-resolver packet.
- It is useful evidence that auto source resolution is stable beyond the single
  `rt_showcase:reflection_closeup` stress scene.
- It is not promotion-grade coverage yet; remaining families and motion modes
  still need broader runs, and model-scene crash/report separation remains
  separate debt.

- Added the authoritative
  `2026-06-07 Full Scene Shader Refactor Blueprint` section to
  `docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`.
- The blueprint reframes the work as a candidate V3 renderer where every
  visible term follows this chain:
  `scene profile -> typed resource producer -> shader contribution -> debug
  view -> frame-report field -> analyzer gate -> promotion packet`.
- The refactor order is:
  1. contract/promotion reconciliation
  2. material payload and missing-channel debt
  3. consumed `SceneProfileV3` policy
  4. resource-backed `SceneLocalEnvironmentV3`
  5. multi-profile environment payloads
  6. high-contrast `LightingShadowV3` attribution
  7. `ReflectionV3` provider resolver and history hardening
  8. `TransparencyMediaV3`
  9. V3-only `CompositeV3` HDR assembly
  10. `CinematicPostV3`
  11. cross-family promotion matrix and human review
- The next implementation slice remains true resource binding for
  `SceneLocalEnvironmentV3`: bind local payload/proxy SRVs, report table
  binding/resource count/source/fallback reason, then prove it with the
  old-office/gallery stress case with IBL enabled and sharp enough to expose
  bad reflections.

Latest implementation checkpoint:

- Implemented the first true resource-backed `SceneLocalEnvironmentV3` slice.
- `SceneLocalEnvironmentV3` now allocates a two-slot transient SRV table and
  binds it at graphics root parameter `3` before drawing:
  - `t0`: scene-local payload albedo
  - `t1`: scene-local payload normal/detail
  - null SRVs are still written when payload textures are not resident
- `SceneLocalEnvironmentV3.hlsl` now samples the payload albedo/normal table
  and gates the contribution by actual texture signal so null descriptors
  remain a reported fallback rather than a black-resource artifact.
- Added `Renderer::BuildSceneLocalEnvironmentV3PayloadBindingInfo()` to choose
  a representative payload albedo/normal pair, prefer cached GPU textures, and
  queue missing uploads outside render-graph execution.
- Frame reports now expose both environment and V3 aliases for:
  - `scene_local_payload_resource_table_required`
  - `scene_local_payload_resource_table_bindable`
  - `scene_local_payload_bound_resource_count`
  - `scene_local_payload_binding_source`
  - `scene_local_payload_fallback_reason`
- Environment readiness now requires `18` channels. Payload-missing scenes can
  remain environment-ready, but payload-ready scenes must prove bindable
  shader resources or report binding debt.
- Contract, placeholder analyzer, environment-payload analyzer, and static V3
  validator now gate the new resource-binding fields.

Validation for latest slice:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\Renderer.h src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\FrameContract.h src\Graphics\FrameContractJson.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\Renderer_RenderGraphEndFrame.cpp assets\shaders\SceneLocalEnvironmentV3.hlsl assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 18 -CaptureFrame 10 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_environment_payload_resource_binding_gallery_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
ctest --test-dir build --output-on-failure -C Release
```

Evidence:

- Native `CortexEngine` build passed. The existing trailing `vswhere.exe`
  warning still printed after successful link.
- `ctest` exited successfully but this build has no registered tests.
- Focused packet:
  `build\captures\v3_environment_payload_resource_binding_gallery_20260607`
  passed V2 evidence, V3 placeholder checks, scene-profile analysis,
  environment-payload analysis, material-payload analysis, CompositeV3
  diagnostics, and promotion decision.
- Environment-payload proof:
  - reports: `54`
  - payload-ready reports: `54`
  - resource-bindable reports: `54`
  - bound-resource reports: `54`
  - first row: texture set `rt_showcase_gallery`, `12` DDS textures,
    `5` albedo, `6` normal, `2` bound resources,
    binding source `cached_scene_local_payload_pair`, fallback reason `none`

Current next work:

1. Add or alias non-gallery payload sets for enclosed room, stage/red room,
   exterior water, and stadium profiles.
2. Promote the environment payload resource-binding proof from gallery-only to
   a cross-profile packet.
3. Continue with `LightingShadowV3` high-contrast source attribution and
   `ReflectionV3` provider resolver hardening before any strong
   `CinematicPostV3` tuning.

Latest cross-profile payload checkpoint:

- Added explicit scene-local payload source aliases without duplicating DDS
  assets:
  - `home_kitchen_lantern` -> `assets/textures/rtshowcase`
  - `home_office_evening` -> `assets/textures/rtshowcase`
  - `school_classroom_day` -> `assets/textures/rtshowcase`
  - `basketball_gym_day` -> `assets/textures/scene_local/basketball_gym_day`
  - `neon_streamer_concert` -> `assets/textures/rtshowcase`
  - `red_light_room` -> `assets/textures/rtshowcase`
  - `stadium_night_match` -> `assets/textures/rtshowcase`
- The alias helper is used by both shader-constant/binding discovery and
  frame-contract payload scanning.
- The V3 static validator now requires the alias helper token so this path is
  part of the checked runtime surface.

Validation for cross-profile checkpoint:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_environment_profiles.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\Renderer_FrameContractSnapshot.cpp tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_environment_payload_resource_binding_cross_profile_20260607 -FamilyFilter kitchen,gym,concert,red_room,stadium -ViewFilter scene_local_environment -SmokeFrames 18 -CaptureFrame 10 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_environment_payload_resource_binding_cross_profile_20260607\manifest.json --output-json build\captures\v3_environment_payload_resource_binding_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_environment_payload_resource_binding_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_environment_payload_resource_binding_office_probe_20260607 -FamilyFilter office -ViewFilter scene_local_environment -SmokeFrames 18 -CaptureFrame 10 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_environment_payload_resource_binding_office_probe_20260607\manifest.json --output-json build\captures\v3_environment_payload_resource_binding_office_probe_20260607\v3_environment_payload_office.json --output-md build\captures\v3_environment_payload_resource_binding_office_probe_20260607\v3_environment_payload_office.md --min-payload-ready 1
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_environment_payload_resource_binding_gallery_20260607\manifest.json --manifest build\captures\v3_environment_payload_resource_binding_office_probe_20260607\manifest.json --manifest build\captures\v3_environment_payload_resource_binding_cross_profile_20260607\manifest.json --output-json build\captures\v3_environment_payload_resource_binding_cross_profile_20260607\v3_environment_profiles_gallery_office_stage_exterior.json --output-md build\captures\v3_environment_payload_resource_binding_cross_profile_20260607\v3_environment_profiles_gallery_office_stage_exterior.md --allow-missing-reports --min-ready-reports 4 --min-distinct-modes 4 --min-distinct-profiles 4 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

Evidence:

- Native build passed after the alias change.
- Cross-profile payload packet:
  `build\captures\v3_environment_payload_resource_binding_cross_profile_20260607`.
  The packet wrapper returned nonzero because kitchen, gym, and red room still
  hit the known model-scene capture/device instability before the environment
  pass executed.
- Despite those exits, frame reports were written and payload analysis passed:
  `5` reports, `5` texture-set-present, `5` payload-ready,
  `5` resource-bindable, and `0` payload-analysis failures.
- Clean environment-ready profile proof uses:
  - gallery from
    `build\captures\v3_environment_payload_resource_binding_gallery_20260607`
  - office from
    `build\captures\v3_environment_payload_resource_binding_office_probe_20260607`
  - concert/stadium rows from
    `build\captures\v3_environment_payload_resource_binding_cross_profile_20260607`
- Cross-profile analyzer passed:
  `57` environment-ready reports, distinct shader profiles
  `enclosed_room`, `gallery_neutral`, `open_exterior`, and `stage`; modes
  `1.0`, `2.0`, `3.0`, and `4.0`.
- Office payload proof passed with one report:
  family `home_office_evening`, shader profile `enclosed_room`, payload-ready
  true, resource-bindable true, `2` bound resources, binding source
  `cached_scene_local_payload_pair`.

Current next work after this checkpoint:

1. Separate model-scene report evidence from visual capture success so
   kitchen/gym/red-room crashes do not block diagnostics.
2. Continue `LightingShadowV3` high-contrast source attribution and
   `ReflectionV3` provider resolver hardening.
3. Later replace alias payload packs with proper per-family texture/proxy
   generation for true scene-local irradiance/specular/background resources.

Latest SceneLocalEnvironmentV3 proxy-resource binding checkpoint:

- Supersedes the older two-slot payload-only environment binding.
- `SceneLocalEnvironmentV3` now binds a five-slot transient SRV table at root
  parameter `3`:
  - `t0`: scene-local payload albedo
  - `t1`: scene-local payload normal/detail
  - `t2`: scene-local irradiance proxy
  - `t3`: scene-local specular proxy
  - `t4`: scene-local visible-background proxy
- `Renderer::BuildSceneLocalEnvironmentV3PayloadBindingInfo()` now chooses
  explicit proxy paths from the scene-local payload set and reports separate
  payload-resource and proxy-resource binding state.
- `SceneLocalEnvironmentV3.hlsl` now samples the proxy textures separately:
  ambient/radiance uses the irradiance proxy, reflection background uses the
  specular proxy, and visible background uses the visible-background proxy
  where the scene profile authorizes that signal.
- Frame reports now expose environment and V3 alias fields for:
  - `scene_local_proxy_resource_table_required`
  - `scene_local_proxy_resource_table_bindable`
  - `scene_local_proxy_bound_resource_count`
  - `scene_local_proxy_binding_source`
  - `scene_local_proxy_fallback_reason`
  - `scene_local_environment_proxy_resource_table_required`
  - `scene_local_environment_proxy_resource_table_bindable`
  - `scene_local_environment_proxy_bound_resource_count`
  - `scene_local_environment_proxy_binding_source`
  - `scene_local_environment_proxy_fallback_reason`
- Environment readiness now requires `21` channels. Payload-ready scenes must
  prove both payload table binding and proxy table binding.
- The V3 contract, placeholder analyzer, environment-payload analyzer, and
  static validator now gate the proxy-resource fields.

Validation for proxy-resource binding:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\Renderer.h src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\Renderer_RenderGraphEndFrame.cpp src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\FrameContract.h src\Graphics\FrameContractJson.cpp src\Graphics\FullSceneShaderFrameContext.h assets\shaders\SceneLocalEnvironmentV3.hlsl assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\SceneLocalEnvironmentV3.hlsl -Destination build\bin\assets\shaders\SceneLocalEnvironmentV3.hlsl -Force
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_proxy_binding_fresh_smoke_20260607
```

Evidence:

- Native `CortexEngine` target was already built by the timed-out Ninja run;
  rerunning Ninja reported `no work to do`. The known trailing `vswhere.exe`
  warning still printed after success.
- Fresh packet:
  `build\captures\v3_scene_local_proxy_binding_fresh_smoke_20260607` passed
  scene-local packet, V2 evidence, V3 placeholder checks, scene profile,
  environment payload, material payload, CompositeV3 diagnostics, and promotion
  decision.
- Environment-payload proof:
  - reports: `54`
  - payload-ready reports: `54`
  - resource-bindable reports: `54`
  - bound-resource reports: `54`
  - proxy-resource-bindable reports: `54`
  - bound-proxy-resource reports: `54`
  - first row: texture set `rt_showcase_gallery`, `12` DDS textures,
    `5` albedo, `6` normal, payload binding source
    `cached_scene_local_payload_pair`, `2` bound payload resources, proxy
    binding source `cached_scene_local_proxy_triple`, `3` bound proxy
    resources, proxy fallback reason `none`
- Promotion status stayed `review_packet_passed`, not default promotion,
  because the packet intentionally covered one stress family and static motion.
- Cross-profile proxy-resource packet:
  `build\captures\v3_scene_local_proxy_binding_cross_profile_20260607` passed
  the scene-local packet runner and both follow-up analyzers.
  - payload reports: `4`
  - payload-ready reports: `4`
  - payload-resource-bindable reports: `4`
  - proxy-resource-bindable reports: `4`
  - bound-proxy-resource reports: `4`
  - covered families:
    `rt_showcase_gallery`, `home_office_evening`,
    `neon_streamer_concert`, and `stadium_night_match`
  - shader profiles/modes:
    `gallery_neutral=1.0`, `enclosed_room=2.0`, `stage=3.0`,
    and `open_exterior=4.0`
  - every row bound `2` payload resources and `3` proxy resources with
    source `cached_scene_local_proxy_triple`

Current next work after this checkpoint:

1. Replace reused albedo/normal proxy aliases with generated or authored
   irradiance, specular prefilter, and visible-background proxy resources.
2. Expand proxy-resource proof to the unstable kitchen/gym/red-room model
   scenes after report/capture separation is clean enough not to hide proxy
   binding behind device-removal exits.
3. Continue `LightingShadowV3` attribution and broader `ReflectionV3`
   auto-resolver coverage before strong `CinematicPostV3` tuning.

Latest SceneLocalEnvironmentV3 explicit proxy asset checkpoint:

- Added `tools\generate_scene_local_environment_proxies.py`.
  - Generates deterministic 32x32 BC1 DDS proxy assets.
  - Emits and mirrors runtime copies for:
    `scene_local_irradiance_proxy.dds`,
    `scene_local_specular_proxy.dds`, and
    `scene_local_visible_background_proxy.dds`.
  - Current generated sets:
    `basketball_gym_day`, `home_kitchen_lantern`,
    `home_office_evening`, `neon_streamer_concert`,
    `red_light_room`, `rt_showcase_gallery`, `school_classroom_day`,
    and `stadium_night_match`.
- `Renderer::BuildSceneLocalEnvironmentV3PayloadBindingInfo()` now searches
  `assets\textures\scene_local_proxy\<set_id>\` for explicit proxy assets
  before falling back to payload-derived proxy textures.
- Explicit triple binding reports source
  `cached_explicit_scene_local_proxy_triple`.
- Payload-derived proxy binding now reports
  `cached_payload_derived_scene_local_proxy_triple`, which is treated as debt.
- `tools\analyze_full_scene_shader_v3_environment_payload.py` and
  `tools\analyze_full_scene_shader_v3_placeholders.py` now fail payload-ready
  packets unless the proxy binding source is
  `cached_explicit_scene_local_proxy_triple`.
- The V3 static validator now includes the proxy generator and explicit/fallback
  source tokens.

Validation for explicit proxy assets:

```powershell
python tools\generate_scene_local_environment_proxies.py --overwrite --out build\captures\scene_local_environment_proxy_generation_20260607\proxy_generation_report.json
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- tools\generate_scene_local_environment_proxies.py src\Graphics\Renderer_FramePostConstants.cpp tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\SceneLocalEnvironmentV3.hlsl -Destination build\bin\assets\shaders\SceneLocalEnvironmentV3.hlsl -Force
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_explicit_proxy_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

Evidence:

- Proxy generation report:
  `build\captures\scene_local_environment_proxy_generation_20260607\proxy_generation_report.json`.
- Native `CortexEngine` build passed. The known trailing `vswhere.exe` warning
  still printed after successful link.
- Fresh focused V3 packet:
  `build\captures\v3_scene_local_explicit_proxy_fresh_smoke_20260607`.
  - full V3 packet passed end to end
  - `54` environment payload reports
  - `54` payload-ready reports
  - `54` explicit proxy binding reports
  - first proxy source `cached_explicit_scene_local_proxy_triple`
  - first proxy fallback reason `none`
- Cross-profile packet:
  `build\captures\v3_scene_local_explicit_proxy_cross_profile_20260607`.
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` explicit proxy binding
  - covered families:
    `rt_showcase_gallery`, `home_office_evening`,
    `neon_streamer_concert`, `stadium_night_match`
  - all rows reported source `cached_explicit_scene_local_proxy_triple`
  - shader profiles/modes covered:
    `gallery_neutral=1.0`, `enclosed_room=2.0`, `stage=3.0`,
    `open_exterior=4.0`

Current next work after this checkpoint:

1. Replace solid BC1 proxy maps with texture-aware/radiance-filtered proxy
   bakes derived from decoded material color, room shell, lights, and camera
   policy.
2. Expand explicit proxy proof to kitchen/gym/red-room after report/capture
   separation is clean enough for those unstable model-scene paths.
3. Continue `LightingShadowV3` and `ReflectionV3` matrix coverage before
   tuning `CinematicPostV3`.

Latest SceneLocalEnvironmentV3 derived proxy checkpoint:

- `tools\generate_scene_local_environment_proxies.py` now derives proxy colors
  using `profile_payload_inventory_v1` instead of only static per-family color
  constants.
- The derivation reads scene-local payload inventory:
  texture count, albedo count, normal count, and filename role signals for
  floor, wall, cube, cylinder, and metal/brushed surfaces.
- The generator writes
  `assets\textures\scene_local_proxy\proxy_manifest.json` with:
  - derivation method
  - source payload path
  - base RGB
  - derived RGB
  - payload inventory
  - role weights
- Runtime mirrors are still written under `build\bin`.
- `tools\analyze_full_scene_shader_v3_environment_payload.py` now reads the
  proxy manifest and fails payload-ready scenes unless:
  - proxy source is `cached_explicit_scene_local_proxy_triple`
  - derivation method is `profile_payload_inventory_v1`
  - the current texture set has a proxy manifest entry

Validation for derived proxy assets:

```powershell
python tools\generate_scene_local_environment_proxies.py --overwrite --out build\captures\scene_local_environment_proxy_generation_20260607\derived_proxy_generation_report.json
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py assets\textures\scene_local_proxy
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_derived_proxy_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_derived_proxy_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_derived_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_derived_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_derived_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_derived_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_derived_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_derived_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

Evidence:

- Derived proxy generation report:
  `build\captures\scene_local_environment_proxy_generation_20260607\derived_proxy_generation_report.json`.
- Focused V3 packet:
  `build\captures\v3_scene_local_derived_proxy_fresh_smoke_20260607`.
  - full V3 packet passed end to end
  - `54` reports
  - `54` payload-ready reports
  - `54` explicit proxy binding reports
  - `54` derived proxy reports
  - only source: `cached_explicit_scene_local_proxy_triple`
  - only derivation: `profile_payload_inventory_v1`
- Cross-profile packet:
  `build\captures\v3_scene_local_derived_proxy_cross_profile_20260607`.
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` explicit proxy binding,
    `4` derived proxy
  - only source: `cached_explicit_scene_local_proxy_triple`
  - only derivation: `profile_payload_inventory_v1`

Current next work after this checkpoint:

1. Add real material-color sampling for BC7 albedo payloads or an offline
   decompression path so proxy bakes use decoded texture colors, not filename
   role inventory.
2. Add light-rig and room-shell influence to the proxy bake manifest and
   analyzer.
3. Expand derived proxy proof to kitchen/gym/red-room after model-scene
   report/capture separation is reliable.

Latest SceneLocalEnvironmentV3 material-sampled proxy checkpoint:

- `tools\generate_scene_local_environment_proxies.py` now uses Pillow's DDS
  loader to decode scene-local albedo/diffuse payloads, including current BC7
  DX10 payload textures.
- Derivation method is now `profile_payload_material_sample_v1`.
- The generator records material sample evidence in
  `assets\textures\scene_local_proxy\proxy_manifest.json`:
  - color payload count
  - sampled color payload count
  - failed color payload count
  - average sampled RGB
  - role-average RGB
  - sampled texture paths/roles/RGB values
- `tools\analyze_full_scene_shader_v3_environment_payload.py` now requires
  payload-ready packets to have:
  - explicit generated proxy binding
  - manifest derivation `profile_payload_material_sample_v1`
  - decoded material-color sample count greater than zero
  - decoder `pillow_dds`
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` now checks for the
  new derivation token in the V3 runtime surface.
- Generated proxy report:
  `build\captures\scene_local_environment_proxy_generation_20260607\material_sample_proxy_generation_report.json`.
- Generation evidence:
  - `basketball_gym_day`: `5` sampled color payloads, `0` failed
  - all other tracked sets: `6` sampled color payloads, `0` failed
- Validation commands:

```powershell
python tools\generate_scene_local_environment_proxies.py --overwrite --out build\captures\scene_local_environment_proxy_generation_20260607\material_sample_proxy_generation_report.json
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py docs\AAA_ASSET_QUALITY_HANDOFF.md docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md assets\textures\scene_local_proxy
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_material_sample_proxy_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

- Focused V3 packet:
  `build\captures\v3_scene_local_material_sample_proxy_fresh_smoke_20260607`.
  - full V3 packet passed end to end
  - `54` reports
  - `54` payload-ready reports
  - `54` explicit proxy binding reports
  - `54` material-sampled proxy reports
  - only source: `cached_explicit_scene_local_proxy_triple`
  - only derivation: `profile_payload_material_sample_v1`
  - sample counts: `6`
- Cross-profile packet:
  `build\captures\v3_scene_local_material_sample_proxy_cross_profile_20260607`.
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` explicit proxy binding,
    `4` material-sampled proxy reports
  - only source: `cached_explicit_scene_local_proxy_triple`
  - only derivation: `profile_payload_material_sample_v1`
  - sample counts: `6`
- This is a real improvement over filename-only inventory, but still not final
  radiance baking. Remaining debt: room-shell influence, light-rig influence,
  filtered irradiance/specular prefilter, and broader clean cross-family proof.

Current next work after this checkpoint:

1. Run fresh focused and cross-profile environment packets against the
   material-sampled proxies.
2. Add room-shell and light-rig influence to the proxy derivation manifest.
3. Separate frame-report diagnostics from visual capture success for unstable
   model-scene paths.

Latest SceneLocalEnvironmentV3 room/light proxy checkpoint:

- `tools\generate_scene_local_environment_proxies.py` now adds explicit
  `ROOM_LIGHT_CONTRACTS` for every tracked scene-local proxy set.
- Derivation method is now `profile_payload_material_room_light_v1`.
- The manifest records `scene_contract_influence` with:
  - room enclosure
  - wall reflectance
  - ceiling reflectance
  - local background occlusion
  - diffuse reflectance
  - ambient strength
  - light-rig mode
  - key/fill/accent RGB
  - accent strength
- The generator applies that contract to the material-sampled proxy colors:
  room reflectance and occlusion affect irradiance/visible background, while
  light key/fill/accent colors affect irradiance/specular/background.
- `tools\analyze_full_scene_shader_v3_environment_payload.py` now requires
  payload-ready packets to prove room-shell and light-rig influence, not just
  material samples.
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` now checks
  `profile_payload_material_room_light_v1` in the V3 runtime surface.
- Generated proxy report:
  `build\captures\scene_local_environment_proxy_generation_20260607\room_light_proxy_generation_report.json`.
- Manifest evidence:
  - `basketball_gym_day`: `tall_gym_volume`, `high_bay_day_fill`
  - `home_kitchen_lantern`: `warm_enclosed_room`,
    `warm_practical_plus_fill`
  - `home_office_evening`: `evening_enclosed_room`,
    `soft_warm_desk_fill`
  - `neon_streamer_concert`: `dark_stage_volume`, `cyan_magenta_stage`
  - `red_light_room`: `dark_red_room`, `red_practical_accent`
  - `rt_showcase_gallery`: `gallery_partial`, `neutral_gallery_key`
  - `school_classroom_day`: `bright_enclosed_room`,
    `cool_daylight_windows`
  - `stadium_night_match`: `open_exterior_bowl`, `cool_floodlights`
- Validation commands:

```powershell
python tools\generate_scene_local_environment_proxies.py --overwrite --out build\captures\scene_local_environment_proxy_generation_20260607\room_light_proxy_generation_report.json
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py assets\textures\scene_local_proxy
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_room_light_proxy_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

- Focused packet:
  `build\captures\v3_scene_local_room_light_proxy_fresh_smoke_20260607`.
  - full V3 packet passed end to end
  - `54` reports
  - `54` payload-ready reports
  - `54` derived proxy reports
  - `54` material-sampled proxy reports
  - `54` scene-contract proxy reports
  - derivation: `profile_payload_material_room_light_v1`
  - room: `gallery_partial`
  - light: `neutral_gallery_key`
- Cross-profile packet:
  `build\captures\v3_scene_local_room_light_proxy_cross_profile_20260607`.
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` derived proxy,
    `4` material-sampled proxy, `4` scene-contract proxy
  - derivation: `profile_payload_material_room_light_v1`
  - rooms covered:
    `dark_stage_volume`, `evening_enclosed_room`, `gallery_partial`,
    `open_exterior_bowl`
  - light rigs covered:
    `cool_floodlights`, `cyan_magenta_stage`, `neutral_gallery_key`,
    `soft_warm_desk_fill`

Current next work after this checkpoint:

1. Convert these room/light/material proxies from flat BC1 colors into filtered
   irradiance/specular maps or probe-like resources.
2. Separate report diagnostics from visual capture success for kitchen/gym/red
   room model-scene instability.
3. Feed the room/light contract into renderer frame reports directly, not only
   the offline proxy manifest.

Latest SceneLocalEnvironmentV3 runtime proxy-contract checkpoint:

- Runtime frame reports now carry the proxy contract fields that were
  previously only present in the offline manifest.
- Added environment/frame-contract fields:
  - `scene_local_proxy_derivation_method`
  - `scene_local_proxy_room_shell`
  - `scene_local_proxy_room_occlusion`
  - `scene_local_proxy_light_rig`
  - `scene_local_proxy_light_accent_strength`
- Added matching V3 report fields:
  - `scene_local_environment_proxy_derivation_method`
  - `scene_local_environment_proxy_room_shell`
  - `scene_local_environment_proxy_room_occlusion`
  - `scene_local_environment_proxy_light_rig`
  - `scene_local_environment_proxy_light_accent_strength`
- Runtime mapping lives in `Renderer_FramePostConstants.cpp` as
  `SceneLocalProxyContractForSetId()`. It currently mirrors the generator's
  eight tracked room/light contracts.
- `tools\analyze_full_scene_shader_v3_environment_payload.py` now checks that
  runtime derivation, room shell, room occlusion, light rig, and light accent
  strength match the proxy manifest.
- Updated
  `assets\final_art\full_scene_shader_pipeline_v3_contract.json` and
  `tools\validate_full_scene_shader_pipeline_v3_plan.py` so these fields are
  part of the checked V3 payload channel surface.

Validation commands:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\FrameContract.h src\Graphics\Renderer.h src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\FrameContractJson.cpp src\Graphics\FullSceneShaderFrameContext.h assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_runtime_proxy_contract_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

Evidence:

- Native `CortexEngine` target built successfully. The known trailing
  `vswhere.exe` warning printed after link.
- Focused packet:
  `build\captures\v3_scene_local_runtime_proxy_contract_fresh_smoke_20260607`
  - full V3 packet passed end to end
  - `54` reports, `54` payload-ready, `54` derived proxy,
    `54` material-sampled proxy, `54` scene-contract proxy
  - runtime derivation: `profile_payload_material_room_light_v1`
  - runtime room: `gallery_partial`
  - runtime light: `neutral_gallery_key`
- Cross-profile packet:
  `build\captures\v3_scene_local_runtime_proxy_contract_cross_profile_20260607`
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` derived proxy,
    `4` material-sampled proxy, `4` scene-contract proxy
  - runtime rooms:
    `dark_stage_volume`, `evening_enclosed_room`, `gallery_partial`,
    `open_exterior_bowl`
  - runtime light rigs:
    `cool_floodlights`, `cyan_magenta_stage`, `neutral_gallery_key`,
    `soft_warm_desk_fill`

Current next work after this checkpoint:

1. Replace the duplicated C++ room/light mapping with manifest loading or a
   generated header so the generator and runtime share one source of truth.
2. Convert flat BC1 proxy colors into filtered irradiance/specular/probe-like
   resources.
3. Separate report diagnostics from visual capture success for kitchen/gym/red
   room model-scene instability.

Latest SceneLocalEnvironmentV3 generated proxy-contract header checkpoint:

- `tools\generate_scene_local_environment_proxies.py` now writes
  `src\Graphics\Generated\SceneLocalProxyContracts.generated.h`.
- The generated header contains:
  - `kSceneLocalProxyDerivationMethod`
  - `kSceneLocalProxyContracts`
  - `FindSceneLocalProxyContract()`
- `Renderer_FramePostConstants.cpp` now includes the generated header and uses
  `Generated::FindSceneLocalProxyContract()` instead of a handwritten duplicate
  contract map.
- The generator report now includes `generated_contract_header`.
- `tools\validate_full_scene_shader_pipeline_v3_plan.py` now requires the
  generated header and its lookup tokens.

Validation commands:

```powershell
python tools\generate_scene_local_environment_proxies.py --overwrite --out build\captures\scene_local_environment_proxy_generation_20260607\generated_contract_proxy_report.json
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- tools\generate_scene_local_environment_proxies.py tools\validate_full_scene_shader_pipeline_v3_plan.py src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\Generated\SceneLocalProxyContracts.generated.h
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_generated_proxy_contract_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

Evidence:

- Native `CortexEngine` target built successfully. The known trailing
  `vswhere.exe` warning printed after link.
- Focused generated-header packet:
  `build\captures\v3_scene_local_generated_proxy_contract_fresh_smoke_20260607`
  - full V3 packet passed end to end
  - `54` reports, `54` payload-ready, `54` scene-contract proxy
  - runtime derivation: `profile_payload_material_room_light_v1`
  - runtime room: `gallery_partial`
  - runtime light: `neutral_gallery_key`
- Cross-profile generated-header packet:
  `build\captures\v3_scene_local_generated_proxy_contract_cross_profile_20260607`
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` scene-contract proxy
  - runtime rooms:
    `dark_stage_volume`, `evening_enclosed_room`, `gallery_partial`,
    `open_exterior_bowl`
  - runtime light rigs:
    `cool_floodlights`, `cyan_magenta_stage`, `neutral_gallery_key`,
    `soft_warm_desk_fill`

Current next work after this checkpoint:

1. Convert flat BC1 proxy colors into filtered irradiance/specular/probe-like
   resources.
2. Consider replacing the generated header with direct manifest loading only if
   runtime asset parsing becomes reliable enough.
3. Separate report diagnostics from visual capture success for kitchen/gym/red
   room model-scene instability.

Latest SceneLocalEnvironmentV3 filtered directional proxy checkpoint:

- `tools\generate_scene_local_environment_proxies.py` no longer writes flat
  solid-color BC1 proxy textures.
- Proxy resource shape is now `filtered_directional_bc1_v1`.
- The generator writes per-block directional BC1 color fields for each proxy:
  - irradiance: broad fill/key/floor low-frequency gradient
  - specular: key/accent highlight lobe plus object/material tint
  - visible background: wall/floor/occlusion/key/accent shaping
- The manifest records per-output filter stats:
  - shape
  - block dimensions
  - average/min/max RGB
  - `variance_score`
- `tools\analyze_full_scene_shader_v3_environment_payload.py` now fails
  payload-ready reports unless:
  - proxy resource shape is `filtered_directional_bc1_v1`
  - all three proxy outputs have filter metadata
  - minimum output variance is greater than `0.01`
- Static validator now checks for the `filtered_directional_bc1_v1` token.
- Generated proxy report:
  `build\captures\scene_local_environment_proxy_generation_20260607\filtered_directional_proxy_report.json`.
- Manifest variance evidence:
  - all `24` proxy outputs have filtered stats
  - weakest output is `rt_showcase_gallery/visible_background` with variance
    `0.014379`

Validation commands:

```powershell
python tools\generate_scene_local_environment_proxies.py --overwrite --out build\captures\scene_local_environment_proxy_generation_20260607\filtered_directional_proxy_report.json
python -m py_compile tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- tools\generate_scene_local_environment_proxies.py tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py assets\textures\scene_local_proxy src\Graphics\Generated\SceneLocalProxyContracts.generated.h
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_filtered_proxy_fresh_smoke_20260607
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607 -FamilyFilter gallery,office,concert,stadium -ViewFilter scene_local_environment -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.json --output-md build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607\v3_environment_payload_cross_profile.md --min-payload-ready 3
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607\manifest.json --output-json build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.json --output-md build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607\v3_environment_profiles_cross_profile.md --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3 --require-profile open_exterior=4
```

Evidence:

- Focused packet:
  `build\captures\v3_scene_local_filtered_proxy_fresh_smoke_20260607`
  - full V3 packet passed end to end
  - `54` reports, `54` payload-ready, `54` filtered proxy,
    `54` scene-contract proxy
  - shape: `filtered_directional_bc1_v1`
  - min variance: `0.014379`
- Cross-profile packet:
  `build\captures\v3_scene_local_filtered_proxy_cross_profile_20260607`
  - packet runner passed
  - environment payload analyzer passed
  - environment profile analyzer passed
  - `4` reports, `4` payload-ready, `4` filtered proxy,
    `4` scene-contract proxy
  - shape: `filtered_directional_bc1_v1`
  - min variance: `0.014379`

Current next work after this checkpoint:

1. Convert the filtered 2D proxies into stronger probe-like resources: higher
   resolution, mip/prefilter levels, or explicit diffuse/specular sampling
   contracts.
2. Feed filtered proxy shape/variance into runtime frame reports, not only the
   offline manifest/analyzer.
3. Separate report diagnostics from visual capture success for kitchen/gym/red
   room model-scene instability.

Latest LightingShadowV3 source-attribution checkpoint:

- `FullSceneLightingV3` now splits `shadow_source_attribution` by source:
  - red: directional/sun shadow-loss ratio
  - green: local fixture shadow-loss ratio
  - blue: shadow-map path enabled
  - alpha: PCSS/filter path enabled
- Added `tools\analyze_full_scene_shader_v3_shadow_attribution.py`.
  It validates that the attribution view is active and consistent with
  `v3_shadow_loss`, `v3_shadow_visibility`, and
  `v3_lighting_energy_budget`.
- The focused shadow-motion runner now calls the attribution analyzer and
  writes:
  - `v3_shadow_attribution.json`
  - `v3_shadow_attribution.md`
- The focused shadow harness no longer requires/captures
  `v3_indirect_lighting`; that view is not part of shadow-source attribution
  and was a flaky unrelated failure path.
- The V3 static validator now includes the shadow-attribution analyzer,
  focused runner, deferred lighting shader, and split source tokens.

Validation for LightingShadowV3 source split:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_shadow_attribution.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_lighting_v3_shadow_motion_focus_packet.ps1), [ref]$tokens, [ref]$errors) | Out-Null; if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\DeferredLighting.hlsl -Destination build\bin\assets\shaders\DeferredLighting.hlsl -Force
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_lighting_v3_shadow_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_lighting_shadow_source_split_focus_pass2_20260607 -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 2 -MotionFrames 64 -MotionLookAmplitude 0.025 -MotionLookCycles 5.0
```

Evidence:

- Fresh focused packet:
  `build\captures\v3_lighting_shadow_source_split_focus_pass2_20260607`.
- Packet wrapper passed end to end.
- Motion analyzer:
  `11` view sequences, `0` warnings, `0` failures.
- Attribution analyzer:
  `1` family, `0` warnings, `0` failures.
- Attribution row:
  - family `stress_rt_showcase_reflection_closeup`
  - sun loss `0.339516`
  - local loss `0.007993`
  - source active `0.464322`
  - shadow-loss active `0.839763`
  - visibility occlusion `1.000000`
  - shadow-map enabled `1.000000`
  - energy active `1.000000`

Current next work after this checkpoint:

1. Add optional deeper attribution channels if instability remains:
   directional cascade index, local shadow slice index, RT shadow mask, and
   filter/PCSS radius.
2. Continue `ReflectionV3` provider resolver hardening after this
   LightingShadowV3 source split is saved.

Latest LightingShadowV3 light-sweep stress checkpoint:

- Added a capture-only automation light sweep, off by default:
  - `CORTEX_LIGHT_SWEEP`
  - `CORTEX_LIGHT_SWEEP_FRAMES`
  - `CORTEX_LIGHT_SWEEP_CYCLES`
  - `CORTEX_LIGHT_SWEEP_YAW_AMPLITUDE_DEGREES`
  - `CORTEX_LIGHT_SWEEP_ELEVATION_AMPLITUDE`
  - `CORTEX_LIGHT_SWEEP_INTENSITY_AMPLITUDE`
- The sweep runs in `Engine::Update()` and drives real renderer state through
  `SetSunDirection()` and `SetSunIntensity()`. It is not a post-process or
  debug-view-only trick.
- `tools\run_scene_local_cinematic_renderer_v1_packets.ps1` now exposes
  `-StabilityMotionMode light_sweep` and restores the sweep environment after
  each packet.
- `tools\run_lighting_v3_shadow_motion_focus_packet.ps1` accepts
  `light_sweep`.
- `tools\run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1` accepts
  `light_sweep` so the stress can later become cross-family matrix evidence.
- `tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1` and the V3
  static validator now check the light-sweep runtime surface.

Validation for LightingShadowV3 light sweep:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_shadow_attribution.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$files=@('tools\run_scene_local_cinematic_renderer_v1_packets.ps1','tools\run_lighting_v3_shadow_motion_focus_packet.ps1','tools\run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1','tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1'); foreach($file in $files){$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file), [ref]$tokens, [ref]$errors) | Out-Null; if($errors.Count -gt 0){$errors | Format-List; exit 1}}
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_lighting_v3_shadow_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_lighting_shadow_light_sweep_focus_20260607 -StabilityMotionMode light_sweep -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -MotionFrames 72 -MotionLookAmplitude 0.035 -MotionForwardAmplitude 0.45 -MotionLiftAmplitude 0.28 -MotionLookCycles 2.0
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1
```

Evidence:

- Focused light-sweep packet:
  `build\captures\v3_lighting_shadow_light_sweep_focus_20260607`.
- Packet wrapper passed end to end.
- Motion analyzer:
  `11` view sequences, `0` warnings, `0` failures.
- Attribution analyzer:
  `1` family, `0` warnings, `0` failures.
- Key motion rows:
  - `v3_shadow_visibility.delta=0.01310343`, `1.000x` legacy,
    `31.476x` beauty.
  - `v3_shadow_loss.delta=0.00762285`, `0.957x` legacy,
    `18.311x` beauty.
  - `v3_shadow_source_attribution.delta=0.00207217`, `4.978x` beauty.
- Attribution row:
  - family `stress_rt_showcase_reflection_closeup`
  - sun loss `0.621078`
  - local loss `0.007351`
  - source active `0.777158`
  - shadow-loss active `0.998522`
  - visibility occlusion `1.000000`
  - shadow-map enabled `1.000000`
  - energy active `1.000000`

Current next work after this checkpoint:

1. Promote light-sweep from focused gallery evidence into a bounded
   cross-family matrix row once model-scene capture/report separation is less
   flaky.
2. Add cascade/slice/RT-mask attribution only if source-specific instability
   remains visible in the new light-sweep packet family.
3. Continue `ReflectionV3` provider resolver hardening.

Authoritative plan:

- `docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`
- Use its `2026-06-07 Master Refactor Before Goal Feature Completion` section
  as the current architecture and acceptance standard.
- Use its `2026-06-07 Authoritative Execution Queue` section as the current
  near-term ordering when older "next work" notes conflict.

Current product boundary:

- Stable public beauty remains the fallback/regression baseline.
- Candidate beauty V3 is the opt-in Unreal-style/full-scene shader line.
- Candidate beauty may use aggressive shader work, but every visible term must
  have a named producer, typed resource, debug view, frame-report field,
  analyzer, and packet before promotion.
- Default beauty must not change until cross-family packet evidence and user
  review accept the candidate path.

Current candidate-only state:

- `FullSceneCompositeV3` has contribution and legacy rescue diagnostics.
- `FullSceneLightingV3` owns direct, unshadowed direct, shadow visibility,
  shadow loss, indirect, lighting-energy budget, and shadow-source
  attribution resources.
- Focused reflection motion packets exist, and forced-SSR reflection-history
  warnings were resolved in the latest focused/full stress evidence.
- Standard V3 packets cover material base color, normal, roughness, metallic,
  material class, and `material_missing_channel_mask`.
- `material_missing_channel_mask` is now required by contract, included in
  frame evidence as `VB_MaterialMissingChannelMask`, and quantified by the V3
  material analyzer.
- Focused shadow-motion and high-contrast light-sweep packets exist.

Current refactor queue:

1. Contract reconciliation and promotion gate hardening.
2. `MaterialPayloadV3` missing-channel ownership. Current aggregate path done.
3. `SceneProfileV3` policy owner. Current policy-contract slice done.
4. `SceneLocalEnvironmentV3` texture/resource ownership. Current
   profile-policy consumption gate done.
5. `LightingShadowV3` high-contrast stress and source split.
6. `ReflectionV3` provider expansion and resolver hardening.
7. `TransparencyMediaV3`.
8. `CompositeV3` V3-only HDR assembly.
9. `CinematicPostV3`.
10. Cross-family promotion matrix.

Latest material ownership slice:

```text
MaterialPayloadV3 missing-channel ownership
  -> contract channel
  -> VB_MaterialMissingChannelMask frame evidence
  -> packet alias/debug view
  -> analyzer coverage and debt summary
  -> focused material packet evidence
```

Implemented:

- `assets\final_art\full_scene_shader_pipeline_v3_contract.json` now includes
  `missing_channel_mask` as a required material channel.
- `src\Graphics\FullSceneShaderFrameContext.h` includes
  `missing_channel_mask` in material channel inventory and
  `VB_MaterialMissingChannelMask` in material-domain debug views.
- `tools\analyze_full_scene_shader_v3_material_payload.py` now requires the
  mask to be captured but treats a black mask as valid zero debt; it also
  reports max mask luma and nonblack ratio.
- Added `tools\run_material_payload_v3_focus_packet.ps1`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_material_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_material_payload_v3_focus_packet.ps1), [ref]$tokens, [ref]$errors) | Out-Null; if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\FullSceneShaderFrameContext.h tools\analyze_full_scene_shader_v3_material_payload.py tools\run_material_payload_v3_focus_packet.ps1 assets\final_art\full_scene_shader_pipeline_v3_contract.json
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_material_payload_v3_focus_packet.ps1 -NoBuild -OutputRoot build\captures\material_payload_missing_channel_focus_20260607_rerun -SmokeFrames 14 -CaptureFrame 7 -CaptureSequenceCount 1
```

Evidence:

- packet: `build\captures\material_payload_missing_channel_focus_20260607_rerun`.
- `ready=true`.
- failures: `0`.
- warnings: `0`.
- contract debug-view debt: `0`.
- `material_missing_channel_mask` coverage: `covered`.
- missing-channel mask max mean luma: `0.815266`.
- missing-channel mask max nonblack: `1.000000`.
- frame report shows `material_attributes_channel_count=18`,
  `missing_required_channel_count=0`, and `VB_MaterialMissingChannelMask` in
  material-domain debug views.

Next concrete implementation slice:

```text
SceneLocalEnvironmentV3 resource owner
  -> consume SceneProfileV3 policy_contract fields
  -> produce local visible background, diffuse irradiance, specular prefilter,
     atmosphere parameters, and ownership mask
  -> keep old-office IBL/sharp reflections as stress evidence, not an
     avoidance target
  -> prove enclosed families no longer rely on unauthorized visible/reflected
     external IBL
```

Goal status:

- Do not mark complete. The plan is now clearer, but the full AAA candidate
  renderer and cross-family promotion proof are not done.
- Keep default beauty unchanged until the candidate path passes promotion.

## 2026-06-07 SceneProfileV3 Policy Contract Slice

Implemented:

- `SceneProfileV3` is now reported as the V3 scene-profile producer instead
  of `SceneCinematicProfileV1Adapter`.
- The old `scene_visual_contract` remains as the backing/adapter input.
- The V3 frame report now emits:
  - `scene_profile_policy_contract_ready`
  - `scene_profile_policy_contract.owner`
  - `scene_profile_policy_contract.contract_id`
  - `family`, `enclosure_mode`, `environment_policy`, `lighting_policy`,
    `reflection_policy`, `exposure_policy`, `material_policy`,
    `temporal_policy`, `post_policy`, and `motion_stability_policy`
- The scene-profile domain output is now `scene_profile_policy_contract`.
- The V3 contract requires the policy-contract fields.
- `tools\analyze_full_scene_shader_v3_scene_profile.py` now fails if the
  policy contract is missing, not owned by `SceneProfileV3`, or inconsistent
  with `scene_visual_contract`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_scene_profile.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_profile_v3_focus_packet.ps1 -NoBuild -OutputRoot build\captures\scene_profile_v3_policy_contract_focus_20260607 -FamilyFilter gallery,kitchen,concert -ViewFilter beauty,reflection_owner,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity -SmokeFrames 14 -CaptureFrame 7 -CaptureSequenceCount 1 -StabilityMotionMode static -MinFamilyCount 3
python tools\analyze_full_scene_shader_v3_scene_profile.py --manifest build\captures\scene_profile_v3_policy_contract_focus_20260607\manifest.json --output-json build\captures\scene_profile_v3_policy_contract_focus_20260607\v3_scene_profile_manual.json --output-md build\captures\scene_profile_v3_policy_contract_focus_20260607\v3_scene_profile_manual.md --min-family-count 3
```

Evidence:

- Plan validator passed.
- Native target rebuilt successfully; the known trailing `vswhere.exe` warning
  printed after link.
- Focus packet output:
  `build\captures\scene_profile_v3_policy_contract_focus_20260607`.
- The packet runner returned nonzero because the known model-authored kitchen
  path hit DX12 `DXGI_ERROR_DEVICE_HUNG` on some views.
- Shutdown reports were still written and usable.
- Manual SceneProfileV3 analyzer passed:
  - reports: `21`
  - families: `3`
  - profiles: `3`
  - policy contracts: `3`
  - failures: `0`
  - warnings: `0`
- Families covered:
  - `rt_showcase_gallery` -> `gallery_public_cinematic_v1:policy_v3`
  - `home_kitchen_lantern` -> `kitchen_morning_warm_scene_local_v1:policy_v3`
  - `neon_streamer_concert` -> `neon_concert_auditorium_scene_local_v1:policy_v3`

Current interpretation:

- SceneProfileV3 now has a real machine-checkable policy contract for
  downstream domains to consume.
- Environment now consumes the SceneProfileV3 environment/enclosure/reflection
  policy fields in readiness evidence. Lighting, reflection, composite, and
  post still need explicit consumption.
- The kitchen device hang remains a renderer stability issue for broad packet
  runs and should not be confused with SceneProfileV3 policy-contract failure.

## 2026-06-07 SceneLocalEnvironmentV3 Profile Policy Consumption

Implemented:

- `SceneLocalEnvironmentV3` readiness now requires consuming the
  `SceneProfileV3` policy contract.
- Environment domain ready-channel count increased from `10` to `13`:
  five source/provenance channels, five owned environment channels, and three
  SceneProfileV3 policy-consumption channels.
- Frame reports now emit:
  - `scene_local_environment_consumes_scene_profile_policy`
  - `scene_local_environment_profile_contract_id`
  - `scene_local_environment_profile_enclosure_mode`
  - `scene_local_environment_profile_policy`
  - `scene_local_environment_profile_reflection_policy`
- The environment contract requires profile policy contract,
  profile enclosure mode, and profile reflection policy channels.
- V3 placeholder and environment-payload analyzers now fail if environment
  readiness is true while policy parity with `scene_profile_policy_contract`
  is missing or mismatched.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\analyze_full_scene_shader_v3_scene_profile.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_environment_profile_policy_consumption_stress_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Full V3 stress packet:
  `build\captures\v3_environment_profile_policy_consumption_stress_20260607`.
- Packet passed end to end: V2 evidence, V3 placeholder checks,
  SceneProfileV3 analyzer, environment payload analyzer, material payload
  analyzer, CompositeV3 diagnostics, and review-packet promotion decision.
- Environment payload result:
  - reports: `54`
  - profile-policy-consumed reports: `54`
  - failures: `0`
  - payload-ready reports: `0`, expected because this stress gallery has no
    texture set yet.

Next:

- Convert policy consumption from evidence only into actual
  `SceneLocalEnvironmentV3` resource selection: local visible background,
  diffuse irradiance, specular prefilter, atmosphere parameters, and ownership
  mask.

## 2026-06-07 LightingV3 Shadow Motion Focus Harness

Purpose:

- Add a small, repeatable harness for shadow/lighting motion instead of using
  broad V3 promotion packets for every shadow-flicker question.
- Measure `lighting_energy_budget` and `shadow_source_attribution` under
  mouse jitter alongside legacy lighting/shadow views and beauty.

Implemented:

- Added `tools\run_lighting_v3_shadow_motion_focus_packet.ps1`.
- Added `--focus shadow` support to
  `tools\analyze_full_scene_shader_v3_lighting_motion.py`.
- Shadow focus measures:
  - `beauty`.
  - legacy `direct_light`, `direct_light_unshadowed`,
    `direct_light_shadow_loss`, `shadow_factor`, and `ambient_ibl`.
  - V3 `v3_direct_lighting`, `v3_direct_lighting_unshadowed`,
    `v3_shadow_visibility`, `v3_shadow_loss`, `v3_indirect_lighting`,
    `v3_lighting_energy_budget`, and `v3_shadow_source_attribution`.
- The analyzer now handles V3 diagnostic-only lighting views without requiring
  a nonexistent legacy counterpart. Those are compared against beauty motion.
- The runner writes:
  - `v3_lighting_shadow_motion_focus.json`.
  - `v3_lighting_shadow_motion_focus.md`.
  - `v3_lighting_shadow_motion_focus_sheet.png`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_lighting_motion.py
$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_lighting_v3_shadow_motion_focus_packet.ps1), [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
git -c submodule.recurse=false diff --check -- tools\run_lighting_v3_shadow_motion_focus_packet.ps1 tools\analyze_full_scene_shader_v3_lighting_motion.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_lighting_v3_shadow_motion_focus_packet.ps1 -NoBuild -OutputRoot build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607 -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -MotionFrames 72 -MotionLookAmplitude 0.025 -MotionLookCycles 6.0
python tools\analyze_full_scene_shader_v3_lighting_motion.py --manifest build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\manifest.json --output-json build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\v3_lighting_shadow_motion_focus.json --output-md build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\v3_lighting_shadow_motion_focus.md --min-sequence-count 2 --focus shadow
python tools\build_full_scene_shader_v2_review_sheet.py --manifest build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\manifest.json --output build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\v3_lighting_shadow_motion_focus_sheet.png --summary-json build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\v3_lighting_shadow_motion_focus_sheet.json --summary-md build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607\v3_lighting_shadow_motion_focus_sheet.md --views beauty,v3_shadow_visibility,v3_shadow_loss,v3_lighting_energy_budget,v3_shadow_source_attribution,direct_light_shadow_loss,shadow_factor --thumb-width 300 --thumb-height 174
```

Evidence:

- focused packet:
  `build\captures\v3_lighting_shadow_motion_focus_mouse_jitter_20260607`.
- motion mode: `mouse_jitter`.
- capture sequence count: `2`.
- view rows: `13`.
- failures: `0`.
- warnings: `0`.
- key rows:
  - `v3_shadow_visibility.delta=0.00761387`, `1.002x` legacy,
    `0.378x` beauty.
  - `v3_shadow_loss.delta=0.01245458`, `0.820x` legacy,
    `0.618x` beauty.
  - `v3_lighting_energy_budget.delta=0.00535823`, `0.266x` beauty.
  - `v3_shadow_source_attribution.delta=0.01232620`, `0.612x` beauty.

Current next work:

1. Add a high-contrast light-sweep row to this harness or a sibling harness.
2. If a user-visible shadow flicker remains, split
   `shadow_source_attribution` into separate directional shadow map, local
   shadow map, RT shadow, and PCSS/filter-radius attribution views.
3. Keep using focused packets first; reserve full V3 promotion packets for
   cross-family acceptance.

## 2026-06-07 LightingV3 Energy and Shadow Attribution Slice

Purpose:

- Move LightingV3 from a five-buffer split to a seven-buffer ownership
  contract.
- Make lighting instability explainable from actual LightingV3 producer terms,
  not inferred from final color or legacy debug views.
- Keep this candidate/debug infrastructure only; default beauty remains
  unchanged.

Implemented:

- `FullSceneLightingV3` now writes seven MRT resources:
  - `direct_lighting`.
  - `direct_lighting_unshadowed`.
  - `shadow_visibility`.
  - `shadow_loss`.
  - `indirect_lighting`.
  - `lighting_energy_budget`.
  - `shadow_source_attribution`.
- `lighting_energy_budget` encodes:
  - red: unshadowed direct-light luma budget.
  - green: shadowed direct-light luma budget.
  - blue: indirect/ambient luma budget.
  - alpha: shadow-loss ratio against total direct+indirect luma.
- `shadow_source_attribution` encodes:
  - red: primary sun-shadow occlusion.
  - green: total direct-light shadow-loss ratio.
  - blue: shadow map enabled.
  - alpha: PCSS enabled.
- Added persistent resources, RTV/SRV descriptors, render-graph imports,
  writes, frame-report resource inventory, pass memory accounting, and strict
  LightingV3 readiness for the two new outputs.
- Added debug views:
  - `90` `FullSceneLightingV3EnergyBudget`.
  - `91` `FullSceneLightingV3ShadowSourceAttribution`.
- Added packet aliases:
  - `v3_lighting_energy_budget`.
  - `v3_shadow_source_attribution`.
- Updated V3 contract JSON, plan validator, placeholder analyzer, lighting
  motion view list, packet defaults, and debug-mode max to `91`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_lighting_motion.py tools\analyze_full_scene_shader_v3_placeholders.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$files = @('tools\run_scene_local_cinematic_renderer_v1_packets.ps1','tools\run_full_scene_shader_pipeline_v3_packet.ps1','tools\run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1','tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1')
foreach ($file in $files) {
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file), [ref]$tokens, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
}
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1
git -c submodule.recurse=false diff --check -- <focused LightingV3 files>
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\DeferredLighting.hlsl -Destination build\bin\assets\shaders\DeferredLighting.hlsl -Force
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution -SmokeFrames 14 -CaptureFrame 7 -CaptureSequenceCount 1 -OutputRoot build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun\manifest.json --output-json build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun\debug_view_metrics.json --output-md build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun\debug_view_metrics.md
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun --signal-output build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun\v3_signal.json --stability-output build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun\v3_stability.json --require-lighting-split-ready --require-lighting-split-draw-count 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter direct_light,direct_light_unshadowed,direct_light_shadow_loss,shadow_factor,ambient_ibl,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,v3_lighting_energy_budget,v3_shadow_source_attribution,candidate_hdr_scene_color,reflection_radiance,reflection_confidence,reflection_source_id -SmokeFrames 14 -CaptureFrame 7 -CaptureSequenceCount 1 -OutputRoot build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607\manifest.json --output-json build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607\debug_view_metrics.json --output-md build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607\debug_view_metrics.md
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607 --signal-output build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607\v3_signal.json --stability-output build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607\v3_stability.json --require-lighting-signal-metrics
```

Evidence:

- strict V3-only packet:
  `build\captures\v3_lighting_energy_shadow_attribution_v3_only_20260607_rerun`.
  - reports: `7`.
  - `lighting_split_resources_ready=true`.
  - `lighting_split_resource_count=7`.
  - `FullSceneLightingV3.executed=true`.
  - `FullSceneLightingV3.draw_count=1`.
  - failures: `0`.
  - warnings: `0`.
- mixed signal packet:
  `build\captures\v3_lighting_energy_shadow_attribution_signal_fullgate_20260607`.
  - captured views: `16`.
  - measured views: `16`.
  - failures: `0`.
  - lighting-signal metrics ready: `true`.
  - `v3_lighting_energy_budget.mean_luma=0.101735`,
    `nonblack_ratio=1.000000`.
  - `v3_shadow_source_attribution.mean_luma=0.358283`,
    `nonblack_ratio=1.000000`.
  - `v3_shadow_loss.mean_luma=0.175342`.
  - `v3_direct_lighting.mean_luma=0.431118`.
  - `v3_direct_lighting_unshadowed.mean_luma=0.470973`.

Notes:

- The first mixed strict analyzer attempt failed because the runtime asset copy
  still held the stale V3 contract JSON and because strict producer-readiness
  was applied across legacy debug views where the split producer is not the
  selected debug path. The corrected proof uses:
  - V3-only packet for strict split readiness.
  - mixed packet for signal sanity.
- Native build succeeded. The known trailing `vswhere.exe` warning printed
  after linking.

Current next work:

1. Add shadow-motion focused packets using `v3_lighting_energy_budget` and
   `v3_shadow_source_attribution` under mouse jitter and light sweep.
2. Start splitting shadow sources further if attribution shows instability:
   directional shadow map, local spot shadow map, RT shadow mask, and PCSS
   radius/softening should become separate named views before visual tuning.
3. Do not promote default beauty from this slice.

## 2026-06-07 Full Scene Shader Master Refactor Planning Update

User direction:

- Move toward full-scene shaders for breathtaking Unreal-style visuals.
- Plan the entire refactor before completing the goal feature.
- Do not treat stronger post, IBL blur, scene changes, or local screenshot
  tuning as the root solution.

Plan location:

- `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`
- New section:
  `2026-06-07 Full Scene Shader Refactor Execution Blueprint`
- New section:
  `2026-06-07 Full Scene Shader Master Refactor Plan`

Current architectural decision:

- Keep default beauty unchanged.
- Build an opt-in `FullSceneCandidateBeautyV3` path.
- The candidate path must be assembled from named, inspectable V3 resources:
  `SceneProfileV3`, `VisibilityV3`, `MaterialPayloadV3`,
  `SceneLocalEnvironmentV3`, `LightingShadowV3`, `ReflectionV3`,
  `TransparencyMediaV3`, `CompositeV3`, and `CinematicPostV3`.
- Legacy `hdr_color`, old IBLs, and fallback material defaults may remain only
  as named rescue/reference lanes and must count as promotion debt.

Refactor principles:

- Candidate-only until proven.
- No anonymous fallback.
- Source-aware visual effects.
- Stability before strength.
- Scene-local environment ownership by default.
- Focused packets for iteration, full packets for promotion.

Planned implementation phases:

1. Contract freeze and promotion gates.
2. `SceneProfileV3` policy object.
3. `MaterialPayloadV3` hardening.
4. `SceneLocalEnvironmentV3` texture-backed split.
5. `LightingShadowV3`.
6. `ReflectionV3` source fusion.
7. `TransparencyMediaV3`.
8. `CompositeV3`.
9. `CinematicPostV3`.
10. Cross-family promotion matrix.

Near-term work order:

1. Finish the current uncommitted `SceneLocalEnvironmentV3` producer/resource
   slice and commit it as infrastructure, not visual promotion.
2. Add `SceneProfileV3` as the policy input that drives environment, lighting,
   reflection, and post choices.
3. Add environment-focused packets for old-office IBL, enclosed kitchen,
   concert stage, and exterior water/vegetation.
4. Convert `FullSceneCompositeV3` legacy HDR rescue into a measured
   contribution/debug lane.
5. Add candidate HDR contribution views for material, direct, indirect,
   reflection, environment, transparency, emissive, and rescue terms.
6. Start stronger cinematic post only after the resource spine and stability
   gates are in place.

Execution blueprint added:

- Treat `FullSceneCandidateBeautyV3` as a separate candidate renderer product
  line, not a prettier default-path patch.
- Target graph:
  `SceneProfileV3 -> VisibilityV3 -> MaterialPayloadV3 ->
  SceneLocalEnvironmentV3 -> LightingShadowV3 -> ReflectionV3 ->
  TransparencyMediaV3 -> CompositeV3 -> CinematicPostV3`.
- Every stage must have named resources, render-graph edges, frame-report
  ownership, debug views, packet aliases, and analyzer gates before it can
  influence candidate beauty.
- Legacy `hdr_color`, old IBL paths, and fallback material defaults are allowed
  only as named reference/rescue lanes, and their usage counts as promotion
  debt.
- Cross-family promotion requires focused subsystem packets first, then gallery,
  kitchen, office, gym, classroom, concert, red room, stadium, bathroom,
  bedroom, workshop, store, street, and exterior water/vegetation with static,
  mouse-jitter, camera-sweep, close-surface orbit, reflective-object orbit, and
  high-contrast light-sweep rows.

Next concrete feature boundary:

- `FullSceneCandidateBeautyV3` scaffolding and diagnostics.
- Include contract freeze, `CompositeV3` contribution outputs, legacy rescue
  usage output, debug views, packet/analyzer gates, and candidate-only review
  capture.
- Exclude default-beauty promotion, heavy cinematic post, and broad visual
  tuning.

Do not mark the goal complete from this plan. The next concrete checkpoint is
a pushed `SceneLocalEnvironmentV3` infrastructure commit plus packet evidence.

## 2026-06-07 CompositeV3 Contribution Diagnostics Slice

Purpose:

- Make `FullSceneCompositeV3` explain where candidate HDR comes from.
- Promote legacy `hdr_color` usage from a hidden rescue branch into an explicit
  measured diagnostic lane.
- Keep this as candidate-only infrastructure; this is not default-beauty
  promotion and not a post/blur visual tuning pass.

Implemented:

- Expanded `FullSceneCompositeV3.hlsl` from 3 MRTs to 5 MRTs:
  - `candidate_hdr_scene_color`.
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.
  - `composite_contribution_map`.
  - `legacy_rescue_usage`.
- `composite_contribution_map` RGB currently encodes:
  - direct contribution ratio.
  - indirect/environment/material-fill contribution ratio.
  - reflection contribution ratio.
  - alpha stores legacy rescue used for resource inspection.
- `legacy_rescue_usage` RGB currently encodes:
  - explicit legacy rescue used.
  - fallback HDR luma.
  - rescue weight.
  - alpha stores pre-rescue split luma.
- Added persistent resources, RTV/SRV descriptors, render-graph imports,
  writes, state handoff, frame-report resources, pass write ownership, and
  debug view routing for:
  - `composite_contribution_map`.
  - `legacy_rescue_usage`.
- Expanded `FullSceneCompositeV3` PSO MRT count from `3` to `5`.
- Added debug views:
  - `88` `FullSceneCompositeV3ContributionMap`.
  - `89` `FullSceneCompositeV3LegacyRescueUsage`.
- Raised renderer debug view max mode from `87` to `89`.
- Updated V3 frame context so CompositeV3 readiness requires six channels:
  HDR, inputs, energy policy, overbright diagnostics, contribution map, and
  legacy rescue usage.
- Updated frame-report JSON with:
  - `composite_contribution_map_ready`.
  - `composite_legacy_rescue_usage_ready`.
- Updated packet aliases, plan validator, placeholder analyzer, composite
  diagnostics analyzer, and contract JSON.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_composite_diagnostics.py tools\analyze_full_scene_shader_v3_placeholders.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$files = @('tools\run_scene_local_cinematic_renderer_v1_packets.ps1','tools\run_full_scene_shader_pipeline_v3_packet.ps1','tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1')
foreach ($file in $files) {
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file), [ref]$tokens, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
}
git -c submodule.recurse=false diff --check -- <focused CompositeV3 files>
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item assets\shaders\FullSceneCompositeV3.hlsl build\bin\assets\shaders\FullSceneCompositeV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter candidate_hdr_scene_color,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage -SmokeFrames 14 -CaptureFrame 7 -CaptureSequenceCount 1 -OutputRoot build\captures\v3_composite_contribution_diagnostics_20260607
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\v3_composite_contribution_diagnostics_20260607\manifest.json --output-json build\captures\v3_composite_contribution_diagnostics_20260607\debug_view_metrics.json --output-md build\captures\v3_composite_contribution_diagnostics_20260607\debug_view_metrics.md
python tools\analyze_full_scene_shader_v3_composite_diagnostics.py --manifest build\captures\v3_composite_contribution_diagnostics_20260607\manifest.json --output-json build\captures\v3_composite_contribution_diagnostics_20260607\v3_composite_diagnostics.json --output-md build\captures\v3_composite_contribution_diagnostics_20260607\v3_composite_diagnostics.md
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_composite_contribution_diagnostics_20260607 --signal-output build\captures\v3_composite_contribution_diagnostics_20260607\v3_signal.json --stability-output build\captures\v3_composite_contribution_diagnostics_20260607\v3_stability.json
```

Evidence:

- focused packet:
  `build\captures\v3_composite_contribution_diagnostics_20260607`.
- debug-view metrics:
  - captured views: `5`.
  - measured views: `5`.
  - failures: `0`.
- CompositeV3 diagnostics:
  - ready: `true`.
  - failures: `0`.
  - warnings: `1`.
  - mean clamp mask: `0.000000`.
  - mean clamp ratio: `0.000000`.
  - mean legacy rescue: `0.000000`.
  - mean explicit legacy rescue: `0.000000`.
  - mean reflection contribution: `0.065433`.
  - warning: underlit mean `0.647383` is elevated in the focused stress view.
- V3 placeholder analyzer:
  - passed.
  - reports: `5`.

Current limitation:

- The focused packet proves the new CompositeV3 diagnostic resources render and
  analyze, but the V3 report still shows `composite_v3_ready=false` in this
  stress slice because upstream lighting split readiness is not fully promoted
  in that packet.
- Do not treat this as final candidate beauty readiness.

Next:

1. Commit and push this CompositeV3 diagnostic slice.
2. Continue with `LightingShadowV3` split-resource ownership/readiness so
   CompositeV3 can become ready without relying on adapter-era lighting debt.
3. Then move to `ReflectionV3` source resolver stability before increasing
   reflection strength or cinematic post.

## 2026-06-07 Candidate Beauty Requests LightingV3 Split Producer

Problem found after the CompositeV3 diagnostic slice:

- The focused packet proved `composite_contribution_map` and
  `legacy_rescue_usage` rendered and analyzed.
- However, the V3 frame report still showed:
  - `lighting_split_resources_ready=false`.
  - `composite_v3_ready=false`.
  - `candidate_beauty_ready=false`.
- Root cause:
  `FullSceneLightingV3` only ran for LightingV3 debug views or the explicit
  `CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT` env var. Candidate-beauty
  packets used CompositeV3, but did not automatically request the split-lighting
  producer. That left valid allocated split targets without pass ownership.

Implemented:

- In `Renderer_RenderGraphVisibilityBuffer.cpp`, candidate-beauty frames now
  request `FullSceneLightingV3`.
- Candidate request sources:
  - `m_postProcessState.fullSceneCandidateBeautyV3Enabled`.
  - `CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3`.
  - `CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3`.
- The existing explicit split-lighting env var still works:
  `CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT`.

Validation:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item assets\shaders\FullSceneCompositeV3.hlsl build\bin\assets\shaders\FullSceneCompositeV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter candidate_hdr_scene_color,energy_clamp_policy,overbright_diagnostics,composite_contribution_map,legacy_rescue_usage -SmokeFrames 14 -CaptureFrame 7 -CaptureSequenceCount 1 -OutputRoot build\captures\v3_lighting_split_candidate_beauty_readiness_20260607
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\manifest.json --output-json build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\debug_view_metrics.json --output-md build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\debug_view_metrics.md
python tools\analyze_full_scene_shader_v3_composite_diagnostics.py --manifest build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\manifest.json --output-json build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\v3_composite_diagnostics.json --output-md build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\v3_composite_diagnostics.md
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_lighting_split_candidate_beauty_readiness_20260607 --signal-output build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\v3_signal.json --stability-output build\captures\v3_lighting_split_candidate_beauty_readiness_20260607\v3_stability.json
```

Evidence:

- focused packet:
  `build\captures\v3_lighting_split_candidate_beauty_readiness_20260607`.
- debug-view metrics:
  - captured views: `5`.
  - measured views: `5`.
  - failures: `0`.
- CompositeV3 diagnostics:
  - ready: `true`.
  - failures: `0`.
  - warnings: `0`.
  - mean clamp mask: `0.000110`.
  - mean clamp ratio: `0.000031`.
  - mean legacy rescue: `0.000000`.
  - mean explicit legacy rescue: `0.000000`.
  - mean direct contribution: `0.643191`.
  - mean reflection contribution: `0.011720`.
- V3 frame report evidence from the candidate HDR capture:
  - `lighting_split_resources_ready=true`.
  - `composite_v3_ready=true`.
  - `candidate_beauty_ready=true`.
  - `lighting_split_resource_count=5`.
  - `composite_v3_channel_count=6`.
  - `FullSceneLightingV3` executed and wrote:
    `direct_lighting`, `direct_lighting_unshadowed`, `shadow_visibility`,
    `shadow_loss`, and `indirect_lighting`.
  - `FullSceneCompositeV3` executed and wrote:
    `candidate_hdr_scene_color`, `energy_clamp_policy`,
    `overbright_diagnostics`, `composite_contribution_map`, and
    `legacy_rescue_usage`.
  - `CinematicPostV3` executed and wrote:
    `candidate_ldr_cinematic_output`.

Current limitation:

- This proves the focused candidate-beauty path requests its required
  split-lighting producer.
- It is not a cross-family promotion and not a visual-quality acceptance pass.

Next:

1. Commit and push this readiness fix.
2. Start the real `LightingShadowV3` split-quality pass:
   source IDs, shadow-loss attribution, locked-exposure motion packets, and
   family-specific lighting rigs.
3. Then continue into `ReflectionV3` source stability.

## 2026-06-07 SceneLocalEnvironmentV3 Producer Slice

Implemented:

- Added `assets/shaders/SceneLocalEnvironmentV3.hlsl`.
- Added persistent target resources and descriptors for:
  - `scene_local_environment`.
  - `ambient_lighting`.
  - `visible_background`.
  - `reflection_background`.
  - `atmosphere`.
- Added `SceneLocalEnvironmentV3` shader compilation and PSO setup.
- Added a render-graph pass that reads:
  `frame_constants`, `scene_visual_contract`, and `environment_state`.
- Added frame-report resources and pass ownership for the five environment
  outputs.
- Added debug views:
  - `83` `SceneLocalEnvironmentV3Aggregate`.
  - `84` `SceneLocalEnvironmentV3AmbientLighting`.
  - `85` `SceneLocalEnvironmentV3VisibleBackground`.
  - `86` `SceneLocalEnvironmentV3ReflectionBackground`.
  - `87` `SceneLocalEnvironmentV3Atmosphere`.
- Added packet aliases:
  `scene_local_environment`, `ambient_lighting`, `visible_background`,
  `reflection_background`, and `atmosphere`.
- Updated V3 analyzers and contract JSON so environment readiness requires the
  five real resources and the `SceneLocalEnvironmentV3` pass.
- `FullSceneCompositeV3` now reads `scene_local_environment` as an owned
  candidate-composite input.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
$files = @('tools\run_scene_local_cinematic_renderer_v1_packets.ps1','tools\run_full_scene_shader_pipeline_v3_packet.ps1')
foreach ($file in $files) {
  $tokens = $null
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file), [ref]$tokens, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
}
git -c submodule.recurse=false diff --check -- <focused SceneLocalEnvironmentV3 files>
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\v3_scene_local_environment_v3_env_only_20260607\manifest.json --output-json build\captures\v3_scene_local_environment_v3_env_only_20260607\debug_view_metrics.json --output-md build\captures\v3_scene_local_environment_v3_env_only_20260607\debug_view_metrics.md
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_scene_local_environment_v3_env_only_20260607 --signal-output build\captures\v3_scene_local_environment_v3_env_only_20260607\v3_signal.json --stability-output build\captures\v3_scene_local_environment_v3_env_only_20260607\v3_stability.json
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_scene_local_environment_v3_full_stress_20260607_rerun --signal-output build\captures\v3_scene_local_environment_v3_full_stress_20260607_rerun\v3_signal.json --stability-output build\captures\v3_scene_local_environment_v3_full_stress_20260607_rerun\v3_stability.json
```

Evidence:

- environment-only packet:
  `build\captures\v3_scene_local_environment_v3_env_only_20260607`.
- full short stress packet:
  `build\captures\v3_scene_local_environment_v3_full_stress_20260607_rerun`.
- environment debug metrics:
  - 5 captured views.
  - 5 measured views.
  - 0 failures.
  - all five environment views had `nonblack=1.0000`.
- environment-only placeholder analyzer:
  - passed.
  - reports: `5`.
- full short stress placeholder analyzer:
  - passed.
  - reports: `50`.
- promotion decision for the full short stress packet:
  - `review_packet_passed`.
  - `default_beauty_promotable=false`.
  - environment ready reports: `37`.
  - composite ready reports: `37`.

Frame-report ownership proof:

- `SceneLocalEnvironmentV3` executed and wrote:
  `scene_local_environment`, `ambient_lighting`, `visible_background`,
  `reflection_background`, and `atmosphere`.
- `FullSceneCompositeV3` executed and read:
  `direct_lighting`, `indirect_lighting`, `shadow_visibility`, `hdr_color`,
  `reflection_radiance`, `reflection_confidence`, `vb_gbuffer_albedo`, and
  `scene_local_environment`.

Current limitation:

- This is infrastructure and ownership, not visual promotion.
- The full packet is a short static stress subset only. Missing promotion
  families remain:
  `concert`, `gallery`, `gym`, `kitchen`, `office`, `red_room`, and
  `stadium`.
- Missing motion rows remain:
  `camera_sweep` and `mouse_jitter`.

Next:

1. Commit and push this slice.
2. Add `SceneProfileV3` so environment, lighting, reflection, and post policy
   are selected from scene intent instead of ad hoc global toggles.
3. Add environment-focused packets for old-office IBL, enclosed kitchen,
   concert stage, and exterior water/vegetation.

## 2026-06-07 SceneProfileV3 Policy Slice

Purpose:

- Move the renderer toward a reusable scene-authored policy layer instead of
  scattered global toggles.
- Let scene family/intent choose environment, lighting, reflection, temporal,
  post, material palette, water, fixture lights, and local probe policy.
- Keep this as infrastructure; it is not candidate-beauty promotion.

Implemented:

- Added `RendererSceneProfile.h/.cpp`.
- Added profile structs:
  `SceneEnvironmentProfile`, `SceneLightingProfile`,
  `SceneLightingBalanceProfile`, `SceneReflectionProfile`,
  `SceneReflectionProbeProfile`, `SceneLightFixtureProfile`,
  `SceneTemporalProfile`, `ScenePostProfile`, `SceneMaterialProfile`,
  `SceneWaterProfile`, and `SceneCinematicProfile`.
- Added profile builders:
  - `BuildSceneLocalCinematicProfile(sceneFamily)`.
  - `BuildGalleryCinematicProfile(conservativeMode)`.
- Added profile application:
  `ApplySceneCinematicProfile(Renderer&, const SceneCinematicProfile&)`.
- RT Showcase now uses the gallery cinematic profile instead of manually
  applying one-off controls.
- Model-authored runtime scenes build/apply a scene-local cinematic profile
  from the seed `scene_family`.
- Added model-authored scene preset support and camera preservation so authored
  scene seeds can be evaluated in runtime.
- Added profile-driven fixture lights and local reflection probe placement for
  scene families such as kitchen, office, classroom, gym, concert, red room,
  stadium, and gallery.
- Added local reflection probe radiance control and procedural/no-IBL
  environment handling so enclosed profiles can use scene-local lighting
  without sharp unrelated panorama leakage.
- Added descriptor-table stability support required by the profile/environment
  path:
  - contiguous persistent CBV/SRV/UAV range allocation.
  - safe shader-visible descriptor overwrite synchronization.
  - shadow/environment descriptor table signatures to avoid redundant
    overwrites.
  - bindless resources now publish into the renderer's global shader-visible
    heap instead of a separate heap.
- Added diagnostic/env override switches for disabling shadows, RT, RT
  reflections, RT GI, fog, particles, SSR, SSAO, TAA, FXAA, IBL, startup
  environment loads, and RT-showcase tuning values.
- Added camera automation controls for mouse jitter and richer camera sweeps.
- Added shadow-cascade stabilization by anchoring cascades to camera position
  instead of camera forward, reducing mouse-look shadow shimmer.

Validation:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1
git -c submodule.recurse=false diff --check -- <focused SceneProfileV3 files>
```

Evidence:

- Native target build returned `ninja: no work to do` after the previous
  successful local compile; the generated Ninja graph includes
  `src/Graphics/RendererSceneProfile.cpp`.
- The existing trailing `vswhere.exe` warning still prints after success.
- Scene-local cinematic renderer V1 contract test passed.
- Focused diff check passed with only line-ending warnings.
- Widened diff check for the coupled descriptor/profile files also passed with
  only line-ending warnings.

Important fix:

- Previous push had `CMakeLists.txt` already referencing
  `src/Graphics/RendererSceneProfile.cpp` while the source/header were still
  untracked locally. This slice must be pushed to keep `main` self-consistent.

Current limitation:

- This is a contract/policy checkpoint, not a visual-quality claim.
- Full environment/profile render packets still need to be run across
  old-office IBL, enclosed kitchen, concert stage, stadium, and exterior
  water/vegetation before promotion.

Next:

1. Commit and push this slice immediately to repair the source-set mismatch.
2. Add focused environment/profile packets.
3. Start `CompositeV3` legacy-rescue measurement and contribution debug views.

## 2026-06-07 Material Descriptor Reliability Slice

Purpose:

- Strengthen material payload readiness before deeper PBR/composite work.
- Stop relying on a separate prewarm pass as the only path that makes material
  texture descriptors valid.
- Reduce material/shader popping risk when generated or streamed material
  textures appear after a renderable has already been seen.

Implemented:

- `MaterialGPUState` now stores a per-slot bound resource signature.
- Fallback and material descriptor tables allocate contiguous persistent
  CBV/SRV/UAV ranges.
- Material descriptor refresh compares resolved source/fallback resource
  signatures and rewrites only when the actual bound resources changed.
- Descriptor overwrites synchronize through the descriptor manager before
  refreshing a shader-visible material table.
- Added `Renderer::PrepareMaterialResources()`:
  - ensures material textures.
  - refreshes descriptors.
  - records readiness counters in the frame contract diagnostics.
- Forward, depth alpha-test, indirect, overlay, transparent, and water passes
  now call `PrepareMaterialResources()` before binding material descriptors.
- Transparent and water passes gained explicit diagnostic disable flags:
  `CORTEX_DISABLE_TRANSPARENT_PASS` and `CORTEX_DISABLE_WATER_PASS`.
- Transparent sorting now uses view-space far extent for more stable ordering
  of large glass/transparent architectural surfaces.
- GPU HZB occlusion default is conservative again; relaxed previous-frame HZB
  use requires `CORTEX_GPUCULL_HZB_RELAXED`.

Validation:

- Focused diff check passed with only line-ending warnings.
- Native target had already compiled this source state locally and reported
  `ninja: no work to do`; rerun native build before/after deeper material
  payload edits.

Current limitation:

- This is descriptor/material readiness infrastructure, not final material
  artistry.
- It does not add new texture channels or material provider provenance yet.

Next:

1. Commit and push this material reliability slice.
2. Run a material-focused V3 packet after the next concrete material payload
   edit.
3. Continue toward material payload provenance/range gates and composite
   contribution views.

## Goal

Move beyond stable blockout scenes into a reusable asset-quality architecture
that can support AAA-style final art:

- explicit quality gates instead of vague screenshot taste calls.
- high-fidelity, editable, separated mesh assets.
- PBR texture/provenance/LOD/collision readiness.
- renderer V1 scene-local contracts preserved.
- frequent GitHub checkpoints for focused work only.

## Current Baseline

- Renderer V1 is complete and documented in
  `docs/SCENE_LOCAL_CINEMATIC_RENDERER_V1.md`.
- The current visual ceiling is asset/geometry fidelity:
  scene shells are stable and scene-local, but many objects still read as
  blockout, low-poly, or proxy geometry.
- Existing final-art pipeline files:
  - `docs/PRETRAINED_FINAL_ART_PIPELINE.md`
  - `docs/HUMAN_FINAL_ART_LEDGER.md`
  - `tools/FinalArtPipeline.ps1`
  - `assets/final_art/final_art_pretrained_asset_plan.json`
  - `assets/final_art/pretrained_asset_import.schema.json`

## 2026-06-05 Full Scene Shader V3 Refactor Direction

Current renderer direction:

- Continue `docs/FULL_SCENE_SHADER_PIPELINE_V3.md` as the live renderer plan
  and ledger.
- Do not chase more screenshot tweaks or IBL hiding as a quality strategy.
- Target an Unreal-style full-scene shader stack through owned, inspectable
  render domains:
  material resolve, scene-local environment, direct/indirect lighting,
  shadows, reflections, HDR composite, and cinematic post.

Current proven state:

- V3 contract, validators, runtime report visibility, and packet skeleton
  exist.
- Material Resolve V3 is complete as a review/debug domain.
- Lighting V3 now writes concrete split MRT resources:
  `direct_lighting`, `direct_lighting_unshadowed`, `shadow_visibility`,
  `shadow_loss`, and `indirect_lighting`.
- The static gallery concrete split packet passed and is pushed.
- Default beauty remains unchanged; V3 is not promoted.

Next renderer slice:

1. Finish Lighting V3 motion stability.
   - Add static, camera-sweep, and mouse-jiggle packet evidence for the five
     concrete split buffers.
   - Compare V3 split resources against legacy deferred lighting terms.
   - Run at least gallery, kitchen, gym, and concert before claiming stability.
2. Build SceneLocalEnvironmentV3 after Lighting V3 motion evidence.
   - Separate visible background, lighting environment, reflection background,
     and atmosphere.
   - Make enclosed-room/stage modes stop reflecting unrelated IBL imagery.
3. Build ReflectionV3 only after environment ownership is explicit.
   - Source-aware reflection resolver with radiance, source ID, confidence,
     temporal delta, and rejected-source debug views.
4. Add CompositeV3 and CinematicPostV3 after lighting/reflection inputs are
   stable.
   - HDR composition first, filmic post second.
   - Post is not allowed to hide unstable upstream inputs.

Do not promote V3 domains into default beauty until the packet/promotion gate
has motion and cross-family evidence.

## 2026-06-06 Full Scene Shader Refactor Resume Position

The current direction is full scene shaders for Unreal-like visuals, but the
work must stay architectural:

- Do not return to individual scene polishing.
- Do not hide artifacts by changing IBL blur, disabling reflection paths, or
  switching scenes.
- Do not treat a nice screenshot as renderer readiness.
- Build candidate beauty as a separate opt-in path until user review approves
  promotion.

Current V3 candidate state:

- `FullSceneCompositeV3` exists and writes `candidate_hdr_scene_color`.
- `CinematicPostV3` exists and writes `candidate_ldr_cinematic_output`.
- `candidate_hdr_scene_color` has debug mode `67` and packet coverage.
- Default beauty remains unchanged.
- The composite is still incomplete: it consumes V3 direct, indirect, shadow
  visibility, and legacy `hdr_color` fallback, but not yet first-class
  reflection/environment/material/media/post resources.

Immediate next implementation slice:

1. Feed `local_reflection_radiance` into `FullSceneCompositeV3`.
   - Add render-graph read ownership.
   - Add SRV binding and HLSL sample.
   - Add frame-report/analyzer evidence that the composite pass reads the
     reflection input.
   - Run static and mouse-jitter V3 packets.
   - Keep the blend conservative; this is an ownership and diagnostic slice,
     not a visual cheat.
2. After that, build `ReflectionResolverV3` as a real producer:
   `reflection_radiance`, `reflection_confidence`, `reflection_source_id`, and
   `reflection_rejected_source_mask`.
3. Then build real scene-local environment textures, material payload resources,
   emissive/GI/media, composite diagnostics, and real cinematic post in that
   order.

The detailed plan and gates are in
`docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` under
`2026-06-06 Full Scene Shader Refactor Blueprint` and
`2026-06-06 Refactor Plan Before Goal Feature Completion`.

### Full Scene Shader Blueprint Checkpoint - 2026-06-06

Current planning decision:

- The renderer target is an opt-in candidate beauty path, not default-beauty
  mutation.
- Final pixels must be assembled from named V3 resources:
  material payload, scene-local environment, direct/indirect lighting, shadows,
  reflection resolver, transparency/media, HDR composite, and cinematic post.
- Every stage needs a producer resource, debug view, frame-report ownership,
  analyzer/packet gate, and promotion evidence before it can be trusted.
- Do not use IBL blur, disabled reflections, scene switching, or post effects
  as fixes for root renderer instability.

Refactor tracks:

1. Renderer resource ownership:
   turn V3 adapter domains into real render-graph producers.
2. Physically useful shading inputs:
   complete PBR material payloads, light splits, source-aware reflections, and
   scene-local environment textures.
3. Cinematic composition:
   real HDR composition followed by owned exposure, bloom, tone map, color
   grade, and LDR output.
4. Verification and promotion:
   static, mouse-jitter, camera-sweep, close-surface, reflective-object, and
   cross-family packets before any default promotion.

Next feature boundary:

- Continue the current `FullSceneReflectionV3` source work by finishing the
  RT/ray-query input wiring and packet evidence.
- Do not jump to stronger post or prettier lighting until the reflection
  domain can explain local, SSR, RT, and environment source choices under
  motion.
- Current uncommitted RT slice may already contain code changes. Before
  modifying more code, inspect `git diff` for:
  `FullSceneReflectionResolverV3.hlsl`,
  `Renderer_RenderGraphEndFrame.cpp`,
  `Renderer_FramePostConstants.cpp`,
  `ShaderTypes.h`,
  `FullSceneShaderFrameContext.h`, and
  `tools/analyze_full_scene_shader_v3_placeholders.py`.

### ReflectionV3 RT Source Input Slice - 2026-06-06

Implementation state:

- `FullSceneReflectionResolverV3.hlsl` now samples
  `g_RTReflection : t2`.
- Reflection source override supports `3`, `rt`, `ray_query`,
  `raytraced`, and `ray_traced`.
- Resolver source IDs now encode RT as `0.75` in
  `reflection_source_id.r`.
- Forced RT can be requested without changing default beauty:
  `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE=rt`.
- The render graph imports `RTReflection`, reads it in
  `FullSceneReflectionV3`, binds it as the third SRV, and records
  `rt_reflection` in the pass read list.
- The V3 frame context and placeholder analyzer now require
  `FullSceneReflectionV3` to read `rt_reflection` before the reflection domain
  is considered ready.
- `forced_ray_query_reflection` is now an accepted source contract for
  analyzer evidence.

Validation still required:

- copy the updated reflection resolver shader into `build/bin/assets/shaders`
  before packet runs if asset sync is skipped.
- run a forced RT static packet and inspect whether RT radiance is present or
  cleanly rejected as unavailable.
- run an auto static packet and a mouse-jitter packet to verify the local/SSR/RT
  resolver policy remains stable.
- do not claim RT visual quality unless the metrics prove nonblank RT source
  signal; a passing graph/read contract only proves source ownership.

Validation completed in this slice:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- CortexEngine/assets/shaders/FullSceneReflectionResolverV3.hlsl CortexEngine/src/Graphics/Renderer_RenderGraphEndFrame.cpp CortexEngine/src/Graphics/Renderer_FramePostConstants.cpp CortexEngine/src/Graphics/ShaderTypes.h CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py CortexEngine/docs/AAA_ASSET_QUALITY_HANDOFF.md CortexEngine/docs/FULL_SCENE_SHADER_PIPELINE_V3.md CortexEngine/docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item assets\shaders\FullSceneReflectionResolverV3.hlsl build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
```

Packet evidence:

- forced RT static:
  `build/captures/v3_reflection_rt_input_forced_static_smoke1_20260606`.
- auto static:
  `build/captures/v3_reflection_rt_input_auto_static_smoke1_20260606`.
- auto mouse-jitter:
  `build/captures/v3_reflection_rt_input_auto_motion_smoke1_20260606`.

Packet results:

- all three packets passed with promotion status `review_packet_passed`.
- forced RT source contract:
  `forced_ray_query_reflection`.
- auto source contract:
  `local_probe`.
- `FullSceneReflectionV3` reads:
  `local_reflection_radiance`, `ssr_color`, and `rt_reflection`.
- `FullSceneReflectionV3` writes:
  `reflection_radiance`, `reflection_confidence`,
  `reflection_source_id`, `reflection_rejected_source_mask`,
  `reflection_temporal_delta`, and `reflection_ssr_source_signal`.

Forced RT static signal:

- `reflection_radiance.mean_luma=0.0547562`,
  `nonblack_ratio=0.3815104`.
- `reflection_confidence.mean_luma=0.3718455`,
  `nonblack_ratio=0.3947656`.

Auto static signal:

- `reflection_radiance.mean_luma=0.0977650`,
  `nonblack_ratio=0.9992958`.
- `reflection_temporal_delta.mean_luma=0.0`,
  `nonblack_ratio=0.0`.

Auto mouse-jitter signal:

- `reflection_radiance.mean_luma=0.0967341`,
  `nonblack_ratio=0.9992339`.
- `reflection_temporal_delta.mean_luma=0.0`,
  `nonblack_ratio=0.0`.
- motion deltas:
  - `candidate_hdr_scene_color.delta=0.0091199`, active `0.0633203`.
  - `reflection_radiance.delta=0.0048307`, active `0.0309180`.
  - `reflection_confidence.delta=0.0048070`, active `0.0129601`.
  - `reflection_source_id.delta=0.0039639`, active `0.0126432`.
  - `reflection_rejected_source_mask.delta=0.0005260`, active `0.0075857`.
  - `reflection_temporal_delta.delta=0.0`, active `0.0`.
  - `reflection_ssr_source_signal.delta=0.0058466`, active `0.0516949`.

Interpretation:

- RT/ray-query is now a real resolver source input with nonblank forced signal
  in the gallery packet.
- Auto mode correctly remains on stable scene-local probe in this row, so the
  RT input is diagnostic/available without destabilizing normal candidate
  reflection.
- The next reflection-quality work is not stronger composite blending. It is
  richer source diagnostics and source-quality improvement for smooth/metallic
  surfaces across more scenes.

### ReflectionV3 RT Source Signal Slice - 2026-06-06

Implementation state:

- Added `reflection_rt_source_signal` as a seventh `FullSceneReflectionV3`
  render target.
- `FullSceneReflectionResolverV3.hlsl` now writes:
  - R: raw RT luma.
  - G: raw RT alpha/confidence.
  - B: resolver-shaped RT confidence.
  - A: forced-RT rejected flag.
- Added persistent target resource, RTV, SRV, render-graph handle, MRT binding,
  frame-resource entry, frame-pass write entry, and pass-size accounting.
- Added debug mode `74`:
  `FullSceneReflectionV3RTSourceSignal`.
- Added packet view name:
  `reflection_rt_source_signal`.
- V3 runtime context now exposes
  `reflection_rt_source_signal_ready`.
- Reflection V3 readiness now requires seven channels:
  radiance, confidence, source ID, rejected-source mask, temporal delta, SSR
  source signal, and RT source signal.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_rt_source_signal_forced_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_rt_source_signal_auto_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_rt_source_signal_auto_motion_smoke1_20260606
```

Results:

- V3 plan validator passed with `Required outputs: 16`.
- Native build passed; the first wrapper timed out while compile/link continued,
  and the follow-up Ninja run reported `no work to do`.
- Forced RT static packet passed:
  `build/captures/v3_reflection_rt_source_signal_forced_static_smoke1_20260606`.
- Auto static packet passed:
  `build/captures/v3_reflection_rt_source_signal_auto_static_smoke1_20260606`.
- Auto mouse-jitter packet passed:
  `build/captures/v3_reflection_rt_source_signal_auto_motion_smoke1_20260606`.
- Motion analyzer measured `19` view sequences after adding
  `reflection_rt_source_signal`.

Frame-report proof:

- `reflection_v3_channel_count=7`.
- `reflection_rt_source_signal_ready=true`.
- `FullSceneReflectionV3.reads=local_reflection_radiance,ssr_color,rt_reflection`.
- `FullSceneReflectionV3.writes` includes `reflection_rt_source_signal`.
- Forced RT source contract reports `forced_ray_query_reflection`.
- Auto source contract remains `local_probe`.

Source signal metrics:

- forced RT static:
  - `reflection_rt_source_signal.mean_luma=0.2907308`.
  - `nonblack_ratio=0.3947667`.
  - `reflection_radiance.mean_luma=0.0547562`.
- auto static:
  - `reflection_rt_source_signal.mean_luma=0.2907241`.
  - `nonblack_ratio=0.3947233`.
  - auto still chooses `local_probe`.
- auto mouse-jitter:
  - `reflection_rt_source_signal.mean_abs_luma_delta=0.0046884`.
  - `reflection_rt_source_signal.mean_active_delta_ratio=0.0248025`.
  - `reflection_ssr_source_signal.mean_abs_luma_delta=0.0058466`.
  - `reflection_radiance.mean_abs_luma_delta=0.0048307`.

Interpretation:

- RT source plumbing is real and measurable; it is not blank.
- Auto policy correctly does not promote RT yet.
- The next quality slice should add source-quality stabilization and admission
  policy for SSR/RT/local-probe blending, not another final-composite boost.

Rejected experiment:

- Tried a screen-space derivative stability gate in
  `FullSceneReflectionResolverV3.hlsl` after this checkpoint.
- Packet path passed, but the metrics were mixed:
  - `reflection_radiance` motion delta improved from `0.0048307` to
    `0.0042962`.
  - `reflection_confidence` motion delta worsened from `0.0048070` to about
    `0.0078880`.
  - `reflection_source_id` motion delta worsened from `0.0039639` to about
    `0.0065916`.
- Decision: do not keep derivative-only admission shaping. It changes source
  ownership in a way that makes the debug contract less stable. The proper next
  slice is a real ReflectionV3 history/stability resource with explicit
  source-ID hysteresis, reprojection/validity, and candidate rejection metrics.

Next planned architecture:

- `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` now defines
  `ReflectionHistoryV3`.
- The intended contract adds:
  - `reflection_history_v3_curr`.
  - `reflection_history_v3_prev`.
  - `reflection_history_v3_validity`.
- The pass should read velocity, depth, normal/roughness, previous reflection
  history, SSR, RT, and local probe radiance.
- The policy should use reprojection, surface validity, source-ID hysteresis,
  and disocclusion rejection before allowing SSR/RT to beat the local probe.
- Success must be measured by source-ID active delta and reflection radiance
  delta under mouse-jitter and smooth/metallic stress packets.

### ReflectionHistoryV3 Seed Slice - 2026-06-06

Implemented:

- Added `assets/shaders/FullSceneReflectionHistoryV3.hlsl`.
- Added `FullSceneReflectionHistoryV3` as a separate fullscreen pass instead
  of expanding `FullSceneReflectionV3` beyond the D3D12 8-MRT limit.
- Corrected `FullSceneReflectionV3` PSO target count from `5` to `7`, matching
  its seven actual render targets.
- Added persistent resources and debug views:
  - `reflection_history_v3_curr`, debug mode `75`.
  - `reflection_history_v3_validity`, debug mode `76`.
- Added frame-report flags:
  - `reflection_history_v3_ready`.
  - `reflection_history_v3_validity_ready`.
- Reflection V3 readiness now requires `9` channels.
- Packet default view filter captures the two history views.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_history_seed_auto_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_history_seed_auto_motion_smoke1_20260606
```

Results:

- V3 plan validator passed with `Required outputs: 18`.
- Native build passed.
- Static gallery packet passed:
  `build/captures/v3_reflection_history_seed_auto_static_smoke1_20260606`.
- Mouse-jitter gallery packet passed:
  `build/captures/v3_reflection_history_seed_auto_motion_smoke1_20260606`.
- Motion analyzer measured `21` view sequences after adding the two history
  debug views.

Frame-report proof:

- `reflection_v3_channel_count=9`.
- `reflection_history_v3_ready=true`.
- `reflection_history_v3_validity_ready=true`.
- `FullSceneReflectionHistoryV3.reads=reflection_radiance,reflection_source_id,reflection_temporal_delta`.
- `FullSceneReflectionHistoryV3.writes=reflection_history_v3_curr,reflection_history_v3_validity`.

Metrics:

- static:
  - `reflection_history_v3_curr.mean_luma=0.0977650`,
    `nonblack_ratio=0.9992958`.
  - `reflection_history_v3_validity.mean_luma=0.5495951`,
    `nonblack_ratio=1.0`.
- mouse-jitter:
  - `reflection_history_v3_curr.mean_abs_luma_delta=0.0048307`,
    matching current resolved radiance by design.
  - `reflection_history_v3_validity.mean_abs_luma_delta=0.0018637`,
    `mean_active_delta_ratio=0.0094575`.

Current limitation:

- This is a seed/history-contract pass only.
- It does not yet sample `reflection_history_v3_prev`, velocity, depth, or
  normal/roughness for source-ID hysteresis.
- The next slice should add ping-pong previous-history ownership and only then
  allow history to affect SSR/RT/local-probe source admission.

### ReflectionHistoryV3 Previous-History Ownership - 2026-06-06

Implemented:

- Added persistent `reflection_history_v3_prev` resource, SRV, and debug mode
  `77`.
- Added per-resource D3D12 state tracking for all `ReflectionV3` targets so
  current history, previous history, resolver outputs, and validity are no
  longer forced through one shared state.
- `FullSceneReflectionHistoryV3` now reads:
  `reflection_radiance`, `reflection_source_id`, `reflection_temporal_delta`,
  and `reflection_history_v3_prev`.
- Added `FullSceneReflectionHistoryV3Copy`, a render-graph copy pass that
  copies `reflection_history_v3_curr` into `reflection_history_v3_prev` after
  the history pass.
- Added frame-report field `reflection_history_v3_prev_ready`.
- Reflection V3 readiness now requires `10` channels.
- Packet view filters include `reflection_history_v3_prev`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_history_prev_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_history_prev_motion_smoke1_20260606
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_reflection_history_prev_static_smoke1_20260606 --signal-output build\captures\v3_reflection_history_prev_static_smoke1_20260606\v3_signal.json --stability-output build\captures\v3_reflection_history_prev_static_smoke1_20260606\v3_stability.json --require-lighting-split-ready --require-lighting-signal-metrics
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_reflection_history_prev_motion_smoke1_20260606 --signal-output build\captures\v3_reflection_history_prev_motion_smoke1_20260606\v3_signal.json --stability-output build\captures\v3_reflection_history_prev_motion_smoke1_20260606\v3_stability.json --require-lighting-split-ready --require-lighting-signal-metrics
```

Results:

- V3 plan validator passed with `Required outputs: 19`.
- Native build passed.
- Static gallery packet passed:
  `build/captures/v3_reflection_history_prev_static_smoke1_20260606`.
- Mouse-jitter gallery packet passed:
  `build/captures/v3_reflection_history_prev_motion_smoke1_20260606`.
- Motion analyzer measured `22` view sequences after adding previous history.

Frame-report proof:

- `reflection_v3_channel_count=10`.
- `reflection_history_v3_ready=true`.
- `reflection_history_v3_prev_ready=true`.
- `reflection_history_v3_validity_ready=true`.
- `FullSceneReflectionHistoryV3.reads=reflection_radiance,reflection_source_id,reflection_temporal_delta,reflection_history_v3_prev`.
- `FullSceneReflectionHistoryV3.writes=reflection_history_v3_curr,reflection_history_v3_validity`.
- `FullSceneReflectionHistoryV3Copy.reads=reflection_history_v3_curr`.
- `FullSceneReflectionHistoryV3Copy.writes=reflection_history_v3_prev`.

Metrics:

- static:
  - `reflection_history_v3_curr.mean_luma=0.0977650`,
    `nonblack_ratio=0.9992958`.
  - `reflection_history_v3_prev.mean_luma=0.0977650`,
    `nonblack_ratio=0.9992958`.
  - `reflection_history_v3_validity.mean_luma=0.6217951`,
    `nonblack_ratio=1.0`.
- mouse-jitter:
  - `reflection_history_v3_curr.mean_abs_luma_delta=0.0048307`,
    `mean_active_delta_ratio=0.0309180`.
  - `reflection_history_v3_prev.mean_abs_luma_delta=0.0048307`,
    `mean_active_delta_ratio=0.0309180`.
  - `reflection_history_v3_validity.mean_abs_luma_delta=0.0018637`,
    `mean_active_delta_ratio=0.0094575`.

Current limitation:

- Previous history is owned and sampled, but it is not yet reprojected through
  velocity/depth/normal/roughness.
- Source-ID hysteresis is still not admitted into reflection source selection.
- Next slice should add reprojection validity and source-switch counters before
  allowing history to alter SSR/RT/local-probe source admission.

### Local Reflection Into Composite V3 - 2026-06-06

Implemented:

- `FullSceneCompositeV3` now accepts the render-graph
  `localReflectionRadiance` handle.
- The composite graph pass reads `local_reflection_radiance` when the
  `LocalReflectionRadiance` pass produced it.
- The pass allocates a fifth transient SRV slot and binds either the graph
  reflection radiance texture or a null `R16G16B16A16_FLOAT` SRV.
- `assets/shaders/FullSceneCompositeV3.hlsl` now samples
  `g_LocalReflectionRadiance : t4` and blends it conservatively into
  `candidate_hdr_scene_color`.
- The V3 frame context no longer calls `FullSceneCompositeV3` a real producer
  unless its frame pass reads `local_reflection_radiance`.
- The placeholder analyzer now requires the real composite pass to read
  `local_reflection_radiance`.
- The static V3 validator token was updated from `v3_lighting_inputs_read` to
  `v3_lighting_and_reflection_inputs_read`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- CortexEngine/src/Graphics/Renderer_RenderGraphEndFrame.cpp CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py CortexEngine/tools/validate_full_scene_shader_pipeline_v3_plan.py CortexEngine/assets/shaders/FullSceneCompositeV3.hlsl
& 'C:\Program Files\Ninja\ninja.exe' -C build -t recompact
& 'C:\Program Files\Ninja\ninja.exe' -C build -n CortexEngine
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_reflection_input_static_smoke1_20260606
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_composite_reflection_input_motion_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed.
- Focused `git diff --check` passed with only existing CRLF warnings.
- Native build passed after `ninja -t recompact` fixed slow graph evaluation.
  The dry-run then reported no work after the build completed.
- Static packet passed:
  `build/captures/v3_composite_reflection_input_static_smoke1_20260606`.
  - reports: `18`.
  - promotion status: `review_packet_passed`.
- Mouse-jitter packet passed:
  `build/captures/v3_composite_reflection_input_motion_smoke1_20260606`.
  - reports: `18`.
  - V3 lighting motion measured `12` view sequences.
  - promotion status: `review_packet_passed`.

Direct frame-report proof from the motion packet:

- `composite_v3_producer=FullSceneCompositeV3`.
- `composite_v3_ready=true`.
- `candidate_beauty_ready=true`.
- `default_beauty_affects=false`.
- `FullSceneCompositeV3` executed.
- `FullSceneCompositeV3` reads:
  `direct_lighting`, `indirect_lighting`, `shadow_visibility`, `hdr_color`,
  and `local_reflection_radiance`.
- `FullSceneCompositeV3` writes `candidate_hdr_scene_color`.

Candidate HDR motion row:

- `mean_abs_luma_delta=0.0089636365`.
- `mean_active_delta_ratio=0.0618576389`.

Remaining limitation:

- The reflection input is now consumed by the composite, but this is not yet a
  full `ReflectionResolverV3`. Source ID, confidence, rejected-source masks,
  and temporal-delta debug resources are still contract/evidence work rather
  than a concrete resolver shader.

Next renderer slice:

1. Turn `FullSceneReflectionV3` from evidence-domain into a real producer:
   `reflection_radiance`, `reflection_confidence`,
   `reflection_source_id`, and `reflection_rejected_source_mask`.
2. Add debug views and packets for those reflection resources.
3. Use metallic/smooth object motion packets to find the remaining jitter at
   the reflection-source level instead of in final beauty.

Latest V3 motion-harness checkpoint:

- Added `tools/analyze_full_scene_shader_v3_lighting_motion.py`.
  - Reads packet manifests and capture sequences.
  - Measures per-view frame-to-frame luma deltas for the five concrete V3
    lighting buffers.
  - Compares each V3 buffer against its legacy deferred debug counterpart.
- Added `tools/run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1`.
  - Runs V3 packets across selected motion modes/families.
  - Aggregates `v3_lighting_motion_matrix.json/md`.
- Added `-NoStressScene` support to V3/V2 packet runners so clean family-only
  matrix rows can be captured without the default stress target.
- Verified narrow smoke:
  `build/captures/v3_lighting_motion_matrix_gallery_smoke3_20260605`.
  - mode: `mouse_jitter`.
  - family: `gallery`.
  - rows: `5`.
  - failures: `0`.
  - stability: `report_count=11`,
    `lighting_split_ready_report_count=11`,
    `full_scene_lighting_v3_executed_report_count=11`,
    `lighting_signal_metrics_ready=true`.
- Important limitation:
  - the gallery smoke was a harness proof, not a Lighting V3 promotion.

Latest cross-family probe:

- Ran:
  `tools/run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1`.
- Artifact:
  `build/captures/v3_lighting_motion_matrix_cross_family_probe2_20260605`.
- Scope:
  - families: `gallery,kitchen,gym,concert`.
  - modes: `mouse_jitter,camera_sweep`.
  - views: `beauty`, five legacy lighting terms, five concrete V3 lighting
    buffers.
  - sequence count: `2`.
- Result:
  - aggregate rows: `40`.
  - hard failures: `0`.
  - warnings: `1`.
  - mouse-jiggle V3 stability: `report_count=44`,
    `lighting_split_ready_report_count=44`,
    `full_scene_lighting_v3_executed_report_count=44`,
    `lighting_signal_metrics_ready=true`.
  - camera-sweep V3 stability: `report_count=44`,
    `lighting_split_ready_report_count=44`,
    `full_scene_lighting_v3_executed_report_count=44`,
    `lighting_signal_metrics_ready=true`.
- Current blocker to diagnose before promotion:
  `concert/v3_indirect_lighting` under mouse-jiggle has V3 motion delta
  `0.00395094` vs legacy ambient delta `0.00115572`, ratio `3.419`.

V3 indirect parity fix:

- Root cause:
  `PSMainV3LightingSplit` used a simplified indirect path instead of the
  legacy scene-local `ambient_ibl` contract.
  - It applied local probe diffuse/specular everywhere when local probes were
    enabled instead of gating by probe weight.
  - It skipped box-projected probe direction, reflection-footprint mip
    filtering, specular ceiling, split AO, local fill, and sheen parity.
  - It included emissive in `indirect_lighting`, which made concert neon/stage
    pixels move differently from the legacy ambient debug term.
- Fix:
  `assets/shaders/DeferredLighting.hlsl` V3 split path now uses the same
  scene-local ambient/probe contract as the legacy path and leaves emissive for
  the future composite stage.
- Targeted verification:
  `build/captures/v3_lighting_concert_indirect_parity_probe2_20260605`.
  - `v3_indirect_lighting.delta=0.00115572`.
  - legacy `ambient_ibl.delta=0.00115572`.
  - V3/legacy ratio `1.000`.
  - failures `0`, warnings `0`.
- Post-fix cross-family verification:
  `build/captures/v3_lighting_motion_matrix_cross_family_after_indirect_fix1_20260605`.
  - families: `gallery,kitchen,gym,concert`.
  - modes: `mouse_jitter,camera_sweep`.
  - rows: `40`.
  - failures: `0`.
  - warnings: `0`.
- Packet family expansion:
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1` now resolves
    `red_room` and `stadium` seeds.
  - `tools/run_full_scene_shader_pipeline_v3_lighting_motion_matrix.ps1`
    defaults to the required V3 family set:
    `gallery,kitchen,office,gym,concert,red_room,stadium`.
  - the matrix runner now has `-SummarizeExisting` so interrupted capture
    runs can be aggregated from existing manifests instead of rerendered.
- New-family lighting motion smoke:
  `build/captures/v3_lighting_motion_new_families_smoke1_20260605`.
  - families: `red_room,stadium`.
  - mode: `mouse_jitter`.
  - rows: `10`.
  - debug views captured/measured: `22/22`.
  - failures: `0`.
  - warnings: `0`.
  - red room V3/legacy ratios:
    direct `1.062`, unshadowed `1.062`, shadow visibility `0.715`,
    shadow loss `0.193`, indirect `0.568`.
  - stadium V3/legacy ratios:
    direct `1.031`, unshadowed `1.030`, shadow visibility `1.091`,
    shadow loss `0.540`, indirect `1.000`.
- Next renderer step:
  repeat the V3 lighting motion matrix with promotion-grade frame counts and
  the full required family set before moving to SceneLocalEnvironmentV3 or
  ReflectionV3.

SceneLocalEnvironmentV3 evidence-domain slice:

- Runtime V3 frame reports now expose:
  - `scene_local_environment_ready`.
  - `scene_local_environment_mode`.
  - `scene_local_environment_channel_count`.
- `SceneLocalEnvironmentV3` is now a real domain entry derived from the scene
  visual/environment contract instead of a planned-only placeholder.
- Current logical output: `scene_local_environment`.
- Current debug view: `environment_mode`.
- Current readiness channels:
  `environment_mode`, `ambient_lighting`, `visible_background`,
  `reflection_background`, and `atmosphere`.
- Current mode compiler derives:
  `enclosed_room`, `stage`, `neutral_lab`, or `open_exterior`.
- The V3 placeholder analyzer now permits and validates `environment` as a
  ready domain when all five channels are owned.
- This is not a new shader/pass and does not change default beauty. It is the
  ownership contract needed before ReflectionV3 and CompositeV3 can consume a
  safe scene-local environment.
- Runtime smoke:
  `build/captures/v3_scene_local_environment_contract_smoke3_20260605`.
  - reports: `16`.
  - `scene_local_environment_ready_report_count=16`.
  - `lighting_split_ready_report_count=16`.
  - `full_scene_lighting_v3_executed_report_count=16`.
  - failures: `0`.
  - warnings: `0`.
  - gallery mode: `neutral_lab`.
  - gallery environment channel count: `5`.

ReflectionV3 evidence-domain slice:

- Runtime V3 frame reports now expose:
  - `reflection_v3_ready`.
  - `reflection_radiance_ready`.
  - `reflection_confidence_ready`.
  - `reflection_source_id_ready`.
  - `reflection_temporal_delta_ready`.
  - `reflection_v3_source_contract`.
  - `reflection_v3_channel_count`.
  - `reflection_v3_source_count`.
- `FullSceneReflectionV3` is now a real V3 domain entry derived from:
  `scene_local_environment`, `scene_visual_reflection_owner`,
  `material_reflection_policy`, `local_reflection_radiance`, and
  `rt_reflection_signal_history`.
- Current logical output: `reflection_radiance`.
- Current debug view: `reflection_confidence`.
- Current readiness channels:
  `reflection_radiance`, `reflection_confidence`, `reflection_source_id`, and
  `reflection_temporal_delta`.
- Current source contract chooses the first ready source among:
  `local_probe`, `ray_query_reflection`, `screen_space_reflection`, and
  `scene_local_environment`.
- The V3 placeholder analyzer now permits and validates `reflection` as a
  ready domain only after `SceneLocalEnvironmentV3` is ready.
- This is not a new resolver shader/pass and does not change default beauty.
  It gives ReflectionV3 explicit source/confidence/temporal ownership before
  composite or beauty promotion work.
- Runtime smoke:
  `build/captures/v3_reflection_contract_smoke1_20260605`.
  - reports: `16`.
  - `reflection_v3_ready_report_count=16`.
  - `scene_local_environment_ready_report_count=16`.
  - `lighting_split_ready_report_count=16`.
  - failures: `0`.
  - warnings: `0`.
  - gallery source contract: `local_probe`.
  - gallery reflection channel count: `4`.
  - gallery reflection source count: `4`.

CompositeV3 / CinematicPostV3 evidence-domain slice:

- Runtime V3 frame reports now expose:
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
- `FullSceneCompositeV3Adapter` is now a real V3 domain entry around the
  current `hdr_color` resource.
- Current composite readiness channels:
  `hdr_scene_color`, `composite_inputs`, `energy_clamp_policy`, and
  `overbright_diagnostics`.
- `CinematicPostV3Adapter` is now a real V3 domain entry around the current
  `PostProcess -> back_buffer` path.
- Current post readiness channels:
  `ldr_cinematic_output`, `exposure_meter`, `bloom_extract`,
  `color_grade_delta`, and `tone_map`.
- The V3 contract JSON now includes a required `composite` domain.
- This is not a default-beauty promotion. It names and validates current
  HDR/LDR ownership so the eventual candidate beauty path has a measurable
  gate.
- Runtime smoke:
  `build/captures/v3_composite_post_contract_smoke1_20260605`.
  - reports: `16`.
  - `composite_v3_ready_report_count=16`.
  - `cinematic_post_v3_ready_report_count=16`.
  - `reflection_v3_ready_report_count=16`.
  - failures: `0`.
  - warnings: `0`.
  - gallery composite producer: `FullSceneCompositeV3Adapter`.
  - gallery composite channel count: `4`.
  - gallery post producer: `CinematicPostV3Adapter`.
  - gallery post channel count: `5`.

V3 promotion decision gate:

- Added `tools/build_full_scene_shader_v3_promotion_decision.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now emits:
  - `promotion_decision.json`.
  - `promotion_decision.md`.
- Promotion decision schema:
  `cortex.full_scene_shader_pipeline_v3.promotion_decision.v1`.
- Current statuses:
  - `blocked`: packet artifacts, frame reports, domains, or analyzer gates
    failed.
  - `review_packet_passed`: packet is internally coherent but does not yet
    cover all required families/motion modes.
  - `candidate_ready_not_promoted`: full required family and motion evidence is
    present without failures or warnings.
- Required ready domains:
  `material`, `lighting`, `environment`, `reflection`, `composite`, and
  `cinematic_post`.
- The decision keeps `default_beauty_promotable=false`. This is deliberate:
  default beauty still requires a separate explicit promotion step and user
  review, not just a green packet.

## 2026-06-05 AAA Gate Refactor

Implemented:

- `assets/final_art/aaa_asset_quality_contract.json`
  - defines target families, renderer-family mapping, required hero roles,
    blockout allowlists, hard blockers, weighted metrics, and minimums.
- `tools/analyze_aaa_asset_quality.py`
  - audits admitted scene seeds, imported/generated asset manifests, final-art
    catalog requirements, and optional renderer V1 manifests.
  - emits JSON and Markdown reports.
  - marks current scenes as `BLOCKED` when they are stable but still below AAA
    asset readiness.
- `tools/FinalArtPipeline.ps1`
  - adds action `AAAAssetQuality`.
- `assets/final_art/asset_registry_v2.schema.json`
  - defines the registry fields and readiness flags.
- `tools/build_asset_registry_v2.py`
  - scans target admitted scene seeds and builds
    `assets/final_art/asset_registry_v2.json`.
- `tools/FinalArtPipeline.ps1`
  - adds action `AssetRegistryV2`.

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AssetRegistryV2
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AAAAssetQuality
python -m py_compile tools\build_asset_registry_v2.py tools\analyze_aaa_asset_quality.py
```

Generated reports:

- Asset registry:
  `assets/final_art/asset_registry_v2.json`
- JSON:
  `docs/media/final_art/generated/aaa_asset_quality/aaa_asset_quality_report.json`
- Markdown:
  `docs/media/final_art/generated/aaa_asset_quality/aaa_asset_quality_report.md`

Asset Registry V2 baseline:

- Runtime mesh assets referenced by target admitted scenes: `33`.
- Source scene count: `4`.
- AAA-ready assets: `0`.
- Source classes:
  - `artist_authored_pbr`: `1`.
  - `cc0_curated_library`: `31`.
  - `engine_generated_fidelity_mesh`: `1`.
- Interpretation:
  - the scene seeds use many real runtime meshes now, but the assets do not yet
    carry the required AAA readiness evidence: complete PBR texture sets, LOD
    chains, collision proxies, support anchors/previews for every class, and
    full provenance metadata.

Registry-backed AAA baseline result:

- Status: `BLOCKED`.
- Scenes: `5`.
- Passed: `0`.
- Blocked: `5`.
- `home_kitchen_lantern`
  - score `0.6014`.
  - blockers: primitive/blockout hero roles, required role coverage, missing
    PBR texture readiness, missing LOD readiness, missing collision readiness.
  - primitive hero roles: `cabinet`, `countertop`.
  - missing required roles: `kitchen_wall`, `tile_floor`.
- `home_office_evening`
  - score `0.5774`.
  - blockers: primitive/blockout hero roles, missing PBR texture readiness,
    missing LOD readiness, missing collision readiness.
  - primitive hero roles: `book`, `keyboard`, `monitor`, `shelf`.
- `basketball_gym_day`
  - score `0.4827`.
  - blockers: primitive/blockout hero roles, required role coverage, too few
    unique runtime assets, missing PBR texture readiness, missing LOD readiness,
    missing collision readiness.
  - primitive hero roles: `backboard`, `ball`, `bleacher`, `hoop`,
    `scoreboard`.
  - missing required roles: `ceiling_light`, `stadium_seat`.
- `neon_streamer_concert`
  - score `0.5658`.
  - blockers: primitive/blockout hero roles, required role coverage, too few
    unique runtime assets, missing PBR texture readiness, missing LOD readiness,
    missing collision readiness.
  - primitive hero roles: `hero_screen`, `stage`, `stage_light`.
  - missing required roles: `audience_riser`, `ceiling_plane`, `desk`,
    `overhead_light`, `venue_floor`, `venue_wall`.
- `rt_showcase_gallery`
  - score `0.1000`.
  - blocker class: no scene-seed asset inventory, no runtime mesh readiness,
    no PBR/LOD/collision/provenance evidence.

Why this matters:

- It changes the work from endless manual scene polishing into a measurable
  promotion gate.
- The gate can drive bulk asset replacement: any role that is still primitive,
  untextured, unprovenanced, or missing LOD/collision readiness becomes an
  explicit work order.
- The renderer V1 packet remains a prerequisite instead of a substitute for
  asset quality.

## Required Next Refactors

1. Asset Registry V2
   - central manifest for all admitted runtime assets.
   - fields: provenance, license, source provider/library, triangle count,
     texture memory, PBR map completeness, LOD chain, collision proxy, semantic
     roles, support anchors, scale bounds, preview images, and visual score.
   - this is the immediate next implementation target.

2. Scene Seed Asset Binding
   - replace direct `runtime_asset` strings with asset IDs from Asset Registry
     V2.
   - scene objects should reference semantic roles and admitted asset IDs.
   - primitive fallback must be tagged as blockout, not final art.
   - implemented as an overlay in the 2026-06-05 binding pass below; engine
     runtime consumption is still pending.

3. AAA Replacement Planner
   - reads the AAA report.
   - emits role-level work orders such as:
     `kitchen.sink needs PBR mesh with collision`, `gym.hoop hero role is
     still proxy`, `concert seating repeats need higher-fidelity instance set`.

4. Provider/Library Intake Expansion
   - prioritize CC0/high-quality model libraries and remote high-quality
     generators before more procedural proxy meshes.
   - Hunyuan/TRELLIS/Shap-E remain provider targets, but Shap-E is prototype
     only for AAA scoring unless a human review overrides it.

5. Runtime Asset Streaming Contract
   - renderer must expose whether each visible asset came from registry V2,
     fallback primitives, generated prototype assets, or missing/placeholder
     paths.
   - validation packets should fail if hero pixels are dominated by blockout
     sources.

## 2026-06-05 AAA Replacement Planner

Implemented:

- `tools/plan_aaa_asset_replacements.py`
  - reads the registry-backed AAA asset-quality report.
  - emits concrete replacement and enrichment work orders.
- `tools/FinalArtPipeline.ps1`
  - adds action `AAAReplacementPlan`.
  - action runs:
    1. `AssetRegistryV2`
    2. `AAAAssetQuality`
    3. `plan_aaa_asset_replacements.py`

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AAAReplacementPlan
python -m py_compile tools\build_asset_registry_v2.py tools\analyze_aaa_asset_quality.py tools\plan_aaa_asset_replacements.py
```

Generated work-order artifacts:

- JSON:
  `docs/media/final_art/generated/aaa_asset_quality/aaa_asset_replacement_work_orders.json`
- Markdown:
  `docs/media/final_art/generated/aaa_asset_quality/aaa_asset_replacement_work_orders.md`

Work-order baseline:

- Status: `READY`.
- Work orders: `49`.
- P0 orders: `29`.
  - primitive hero-role replacements.
  - missing required-role additions.
- P1 orders: `20`.
  - existing registry asset upgrades for PBR textures, LOD chains, collision
    proxies, previews, and provenance/readiness.

High-priority examples:

- `basketball_gym_day__replace_primitive_hero__hoop`
- `basketball_gym_day__replace_primitive_hero__backboard`
- `home_kitchen_lantern__replace_primitive_hero__cabinet`
- `home_kitchen_lantern__replace_primitive_hero__countertop`
- `home_office_evening__replace_primitive_hero__monitor`
- `neon_streamer_concert__replace_primitive_hero__stage`
- `rt_showcase_gallery__add_missing_required_role__hero_liquid_pair`

Current interpretation:

- We now have an actionable queue instead of a vague "make it better" target.
- The next implementation slice should choose a P0-heavy scene family and
  replace primitive hero roles through registry-backed assets, then rerun:
  `AAAReplacementPlan`, renderer V1 packet, and visual review sheet.

## 2026-06-05 AAA Provider Request Export

Implemented:

- `tools/export_aaa_provider_requests.py`
  - converts replacement work orders into provider/library request packs.
  - P0 role orders become `new_or_replacement_asset` requests.
  - P1 registry orders become `upgrade_existing_asset` requests.
  - every request includes accepted formats, forbidden whole-scene output
    modes, PBR/LOD/collision/preview/support-anchor requirements, and
    admission gates.
- `tools/FinalArtPipeline.ps1`
  - adds action `AAAProviderRequests`.
  - action runs:
    1. `AssetRegistryV2`
    2. `AAAAssetQuality`
    3. `AAAReplacementPlan`
    4. `export_aaa_provider_requests.py`

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AAAProviderRequests
python -m py_compile tools\build_asset_registry_v2.py tools\analyze_aaa_asset_quality.py tools\plan_aaa_asset_replacements.py tools\export_aaa_provider_requests.py
```

Generated request artifacts:

- Manifest JSON:
  `docs/media/final_art/generated/aaa_asset_quality/provider_requests/manifest.json`
- Manifest Markdown:
  `docs/media/final_art/generated/aaa_asset_quality/provider_requests/manifest.md`
- Request packs:
  `docs/media/final_art/generated/aaa_asset_quality/provider_requests/p0/*.json`
  and
  `docs/media/final_art/generated/aaa_asset_quality/provider_requests/p1/*.json`

Provider request baseline:

- Request count: `49`.
- P0 request count: `29`.
- P1 request count: `20`.
- Request files on disk including manifests: `51`.

Current interpretation:

- The asset-quality pipeline is now executable up to provider/library handoff:
  gate -> registry -> work orders -> request packs.
- No AAA assets have been fulfilled yet. The next major implementation step is
  a fulfillment/import loop:
  - consume request packs from a high-quality provider or curated CC0 source.
  - write/import assets into registry V2 with PBR/LOD/collision readiness.
  - update scene seeds to use registry-backed assets.
  - rerun AAA gate and renderer packet.

## 2026-06-05 Scene Asset Binding Overlay

Implemented:

- `assets/final_art/scene_asset_bindings_v1.schema.json`
  - documents the scene-object binding overlay schema.
- `tools/build_scene_asset_bindings_v1.py`
  - scans target admitted scene seeds.
  - maps every `runtime_asset` object to Asset Registry V2 where possible.
  - classifies primitives as:
    - `primitive_blockout_allowed`
    - `primitive_hero_blocker`
    - `primitive_scene_detail`
  - records unresolved runtime asset paths.
- `tools/analyze_aaa_asset_quality.py`
  - now reads `assets/final_art/scene_asset_bindings_v1.json`.
  - report table includes registry-bound object counts and primitive hero
    blocker counts.
- `tools/FinalArtPipeline.ps1`
  - adds action `SceneAssetBindings`.
  - `AAAReplacementPlan` now depends on `SceneAssetBindings`.

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AAAProviderRequests
python -m py_compile tools\build_asset_registry_v2.py tools\build_scene_asset_bindings_v1.py tools\analyze_aaa_asset_quality.py tools\plan_aaa_asset_replacements.py tools\export_aaa_provider_requests.py
```

Generated artifact:

- `assets/final_art/scene_asset_bindings_v1.json`

Binding baseline:

- Object count: `545`.
- Registry-bound object instances: `120`.
- Allowlisted primitive/blockout shell objects: `33`.
- Primitive hero blockers: `24`.
- Unresolved runtime asset paths: `0`.
- AAA-ready bound object instances: `0`.

Per-scene binding summary:

- `home_kitchen_lantern`
  - objects `135`.
  - registry-bound `27`.
  - allowlisted blockout primitives `6`.
  - primitive hero blockers `2`.
- `home_office_evening`
  - objects `131`.
  - registry-bound `24`.
  - allowlisted blockout primitives `5`.
  - primitive hero blockers `8`.
- `basketball_gym_day`
  - objects `125`.
  - registry-bound `30`.
  - allowlisted blockout primitives `17`.
  - primitive hero blockers `7`.
- `neon_streamer_concert`
  - objects `154`.
  - registry-bound `39`.
  - allowlisted blockout primitives `5`.
  - primitive hero blockers `7`.
- `rt_showcase_gallery`
  - missing scene seed inventory for this overlay.

Current interpretation:

- This is the missing bridge between semantic scene seeds and Asset Registry V2.
- The runtime still uses direct `runtime_asset` strings, but every target seed
  now has an external asset-ID overlay suitable for engine integration.
- Next structural refactor should make the runtime/frame report expose asset
  source class and registry readiness for visible/loaded objects, so validation
  can fail hero pixels dominated by primitive/proxy sources.

## 2026-06-05 Full Scene Shader Pipeline V2 Planning Slice

Implemented:

- `docs/FULL_SCENE_SHADER_PIPELINE_V2.md`
  - living plan and completion ledger for the next renderer architecture step.
  - preserves Renderer V1 as the stability/ownership baseline.
  - defines ten V2 phases:
    - `FSSP-V2-001` contract and plan.
    - `FSSP-V2-002` material model upgrade.
    - `FSSP-V2-003` GBuffer/debug channel expansion.
    - `FSSP-V2-004` scene-local semantic light rig system.
    - `FSSP-V2-005` local reflection probe system.
    - `FSSP-V2-006` shadow/contact stability.
    - `FSSP-V2-007` material-aware temporal pipeline.
    - `FSSP-V2-008` HDR cinematic post V2.
    - `FSSP-V2-009` render graph ownership refactor.
    - `FSSP-V2-010` cross-family V2 gate.
- `assets/final_art/full_scene_shader_pipeline_v2_contract.json`
  - machine-readable required-domain contract for the shader pipeline.
  - requires material, GBuffer, lighting, reflection, shadow, temporal, post,
    render graph, asset-registry evidence, and cross-family packet domains.
  - names the Renderer V1 gate each V2 domain must preserve.
- `tools/validate_full_scene_shader_pipeline_v2_plan.py`
  - validates that the Markdown plan and JSON contract stay coherent.
  - checks required phases, domain ids, target family order, hard rules, and
    minimum required outputs.
- `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
  - external frame-report target for the V2 runtime integration.
  - maps each required shader domain to a future
    `full_scene_shader_pipeline_v2` frame-report section.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`
  - validates the external frame-report contract against the main V2 contract.
  - can optionally inspect a runtime frame report and fail when V2 sections are
    missing.
- `assets/final_art/full_scene_shader_material_evidence_v2.schema.json`
  - schema summary for shader-facing material evidence.
- `tools/build_full_scene_shader_material_evidence_v2.py`
  - derives V2 material-family, shader-feature, PBR readiness, hero-surface,
    and primitive material blocker evidence from Asset Registry V2 and scene
    bindings.
- `assets/final_art/full_scene_shader_material_evidence_v2.json`
  - generated baseline evidence report.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_evidence_report.md`
  - human-readable material evidence summary.
- `assets/final_art/full_scene_shader_material_upgrade_plan_v2.schema.json`
  - schema summary for shader material upgrade work orders.
- `tools/plan_full_scene_shader_material_upgrades_v2.py`
  - converts blocked V2 material evidence into P0/P1 work orders.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.json`
  - generated shader material upgrade queue.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.md`
  - human-readable shader material upgrade queue.
- `assets/final_art/full_scene_shader_material_provider_requests_v2.schema.json`
  - schema summary for shader material provider request packs.
- `tools/export_full_scene_shader_material_provider_requests_v2.py`
  - exports V2 material upgrade work orders into provider/library request packs.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.json`
  - generated V2 material provider request manifest.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.md`
  - human-readable V2 material provider request manifest.
- `assets/final_art/full_scene_shader_material_fulfillment_v2.schema.json`
  - schema summary for V2 material fulfillment/admission records.
- `tools/build_full_scene_shader_material_fulfillment_v2.py`
  - creates a pending fulfillment manifest from provider requests.
- `tools/validate_full_scene_shader_material_fulfillment_v2.py`
  - validates request coverage and strict admitted-package evidence.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.json`
  - generated pending fulfillment manifest.
- `docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.md`
  - human-readable pending fulfillment manifest.

Validation:

```powershell
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\validate_full_scene_shader_pipeline_v2_plan.py
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialEvidence
python -m py_compile tools\build_full_scene_shader_material_evidence_v2.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialUpgradePlan
python -m py_compile tools\plan_full_scene_shader_material_upgrades_v2.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialProviderRequests
python -m py_compile tools\export_full_scene_shader_material_provider_requests_v2.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialFulfillmentBaseline
python -m py_compile tools\build_full_scene_shader_material_fulfillment_v2.py tools\validate_full_scene_shader_material_fulfillment_v2.py
```

Current interpretation:

- This shifts the next work from profile/post tweaks into a full renderer
  architecture refactor.
- The plan explicitly forbids hiding problems by disabling IBL, shadows,
  reflections, or temporal history.
- The refactor blueprint is now explicit:
  - keep V1 as the playable fallback while adding V2 contracts beside it.
  - add runtime facades for material, lighting, reflection, temporal, and post
    ownership before replacing internals.
  - migrate one shader domain at a time: material, GBuffer, lighting,
    reflections/shadows, temporal/post, then render graph.
  - promote domains only by packet evidence, not screenshots.
  - failed V2 domains must report their failure and fall back to V1 beauty
    output until cross-family gates pass.
- Runtime frame-report placeholders now emit `full_scene_shader_pipeline_v2`
  from `FrameContractJson.cpp` without changing beauty output:
  - status `runtime_placeholder_v1_fallback`.
  - beauty output `v1_fallback`.
  - all required material, GBuffer, lighting, reflection, shadow, temporal,
    post, render-graph, asset-evidence, and packet-gate readiness fields are
    present.
  - values are deliberately derived from current V1 ownership/diagnostic data
    and remain conservative until V2 domains are promoted.
- Validation for this checkpoint:
  - `python tools\validate_full_scene_shader_pipeline_v2_plan.py` passed.
  - `python tools\check_full_scene_shader_pipeline_v2_frame_report.py` passed
    and now checks that `FrameContractJson.cpp` emits the required runtime
    sections/fields.
  - `python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py`
    passed.
  - `git -c core.autocrlf=false diff --check` passed for the focused
    frame-report files.
  - `.\build.ps1 -Config Release` and direct `ninja -C build CortexEngine -v`
    both timed out in CMake/Ninja regeneration without compiler output in this
    environment; stopped the spawned build processes and did not leave them
    running.
- The next implementation slice should upgrade material model and registry
  material evidence before changing visual output.
- Material evidence has been upgraded with a runtime-policy bridge:
  - every registry asset now emits scene material class, reflection preference,
    temporal policy, post sensitivity, required texture slots, and missing
    texture slots.
  - `runtime_policy_bridge_asset_count=33`.
  - provider request packs carry `runtime_policy`, `runtime_policy_candidates`,
    `required_pbr_maps`, and `missing_texture_slots` in `material_contract`.
  - material evidence still correctly remains `BLOCKED`; this is contract
    precision, not an asset-quality claim.
- The first frame-report contract is external because the current renderer C++
  worktree already has broad uncommitted frame-contract changes. Runtime C++
  integration should use this external contract after those changes are
  reconciled.
- V2 material evidence baseline:
  - status `BLOCKED`.
  - assets `33`.
  - V2 material-ready assets `1`.
  - PBR texture-ready assets `1`.
  - missing hero texture evidence `10`.
  - primitive hero material blockers `24`.
  - unknown material-family assets `0`.
- V2 material upgrade work-order baseline:
  - status `READY`.
  - work orders `56`.
  - P0 orders `34`.
  - P1 orders `22`.
  - primitive hero material orders `24`.
  - hero asset material orders `10`.
  - registry asset material orders `22`.
- V2 material provider request baseline:
  - requests `56`.
  - P0 requests `34`.
  - P1 requests `22`.
  - request files including manifests `58`.
- V2 material fulfillment baseline:
  - status `PENDING`.
  - requests `56`.
  - pending `56`.
  - admitted `0`.
  - rejected `0`.
- Renderer V1 remains the baseline. V2 work must preserve the final seq8 packet
  gates or provide stronger replacement evidence.

### Full Scene Shader Pipeline V2 Runtime Material Policy Slice

Purpose:

- Move from broad surface classes toward scene-wide shader semantics that later
  lighting, reflection, temporal, and post passes can trust per pixel.
- Do not claim beauty-output promotion yet; V2 still reports
  `runtime_placeholder_v1_fallback`.

Implementation state:

- `FrameContractJson.cpp` now reports
  `full_scene_shader_pipeline_v2.gbuffer.material_policy_channel_ready`.
- The field is true only when:
  - `vb_gbuffer_material_ext2` exists and matches the frame contract.
  - scene material family counts cover every sampled material.
  - reflection preference counts cover every sampled material.
  - temporal policy counts cover every sampled material.
  - post sensitivity counts cover every sampled material.
- The V2 frame-report contract requires that readiness field.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py` now statically
  checks the runtime material-policy bridge:
  - `VisibilityBuffer.h` has `VBMaterialConstants.policyParams`.
  - `MaterialModel.h/.cpp` define/apply material policy evidence.
  - `MaterialResolve.hlsl` reads `mat.policyParams.x`.
  - `MaterialResolve.hlsl` writes encoded scene material class to
    `MaterialExt2.w`.
  - `SurfaceClassification.hlsli` owns the scene material vocabulary and
    encoders.
  - `DeferredLighting.hlsl` decodes the same channel and derives subsurface,
    direct/indirect BRDF shaping, local probe shaping, shadow softness, and
    material-policy debug color from the named scene material class.
  - `PostProcess.hlsl` decodes the same channel for reflection grading,
    contact AO, temporal/reflection stability shaping, and material-policy
    debug views.
  - CPU constant upload paths expose the matching cinematic stability and
    local ambient/probe parameters.

Current caveat:

- This is a contract and data-path slice. It hardens the substrate for AAA
  shaders but does not by itself prove final visual quality.
- Full native build has previously timed out in CMake/Ninja regeneration in
  this environment; use the focused Python validators first and only run the
  native build with a bounded timeout.

### Full Scene Shader Pipeline V2 Temporal Reprojection Slice

Purpose:

- Fix a real temporal stability substrate issue for AAA material/reflection
  quality: the temporal rejection mask must test the same jitter-aware history
  coordinate used by the TAA resolve path.
- This targets smooth/metallic/reflection popping under mouse rotation and
  camera sweeps without disabling TAA, reflections, shadows, or IBL.

Implementation state:

- `TemporalRejectionMask.hlsl` now binds `FrameConstants` and uses
  `g_TAAParams.xy` in its history UV:
  `historyUv = uv + velocity + g_TAAParams.xy`.
- The temporal rejection mask now uses a gentler high-motion taper so camera
  rotation does not reject otherwise valid static surfaces before depth/normal
  disocclusion tests can own the decision.
- `FrameContractJson.cpp` reports
  `full_scene_shader_pipeline_v2.temporal.jitter_reprojection_ready`.
- The field is true only when:
  - TAA is enabled.
  - motion vectors are planned/executed and the velocity resource is valid.
  - temporal rejection mask was built.
  - `temporal_rejection_mask` exists and matches the frame contract.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py` now statically
  checks:
  - temporal rejection shader has frame constants at `b1`.
  - temporal rejection reads `g_TAAParams`.
  - temporal rejection uses `uv + velocity + g_TAAParams.xy`.
  - post-process TAA resolve also uses jitter-aware history UVs.
  - the temporal pass binds frame constants at the expected root.

Current caveat:

- This is a data-path/stability contract slice. It still needs a runtime packet
  with mouse-jiggle/camera-sweep evidence before V2 temporal gates can be
  promoted beyond placeholder/fallback status.

### Full Scene Shader Pipeline V2 Reflection Miss Ownership Slice

Purpose:

- Make ray-traced reflection misses respect scene-local environment ownership
  instead of leaking visible HDRI/background energy into enclosed authored
  scenes.
- This is a root shader policy for glossy/metal/glass stability; it is not an
  IBL-off workaround.

Implementation state:

- `RaytracedReflections.hlsl` now treats zero background exposure as an authored
  enclosed-scene signal: when IBL is disabled and background exposure is zero,
  ray misses return black instead of synthesizing an external sky/ambient lobe.
- RT reflection environment sampling now uses `g_AmbientColor.w`
  (`backgroundBlur`) as a minimum specular mip floor, so reflection-safe local
  backgrounds can damp high-frequency HDRI detail without disabling IBL.
- Interior hit-surface radiance no longer adds a horizon-weighted sky ambient
  lobe when the authored scene declares no external environment.
- `FrameContractJson.cpp` reports
  `full_scene_shader_pipeline_v2.reflections.rt_miss_environment_policy_ready`.
- The field is true only when:
  - no invalid external HDRI is reported.
  - outdoor/non-enclosed scenes are allowed, or enclosed scenes have local
    reflection probes, zero background exposure, or IBL disabled.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py` now statically
  checks the RT reflection miss policy, background exposure upload, and
  background-blur mip-floor contract.

Current caveat:

- This hardens reflection ownership. It still needs rendered packet evidence
  on enclosed scenes with IBL enabled/background controls before V2 reflection
  gates can be promoted.

### Full Scene Shader Pipeline V2 Scene-Local Environment Shader Slice

Purpose:

- Move forward shading, procedural sky, and water away from generic standalone
  shader behavior and into the same scene-local environment contract used by
  reflections and post.
- This is the shader-side substrate for more Unreal-like scene coherence:
  stable IBL filtering, authored local sky color, and water/liquid reflections
  that read as part of the scene.

Implementation state:

- `Basic.hlsl` now has forward-path fixture shaping and stable environment
  reflection mip selection:
  - lat-long derivative seam handling through `EnvReflectionFootprintMip`.
  - background blur as a minimum specular IBL mip floor.
  - material-aware roughness floors for mirror/glass/water/brushed metal.
- `ProceduralSky.hlsl` now has a scene-local atmospheric profile instead of a
  generic HDRI replacement:
  - wet horizon haze.
  - local cloud/noise shaping.
  - below-horizon darkening suitable for water/shore captures.
- `Water.hlsl` now uses a local liquid reflection palette and glint model:
  - ambient-owned sky tint.
  - local silt/bank/film/caustic detail for water.
  - liquid-specific reflection/glint handling for water/lava/honey/molasses.
- `FrameContractJson.cpp` reports
  `full_scene_shader_pipeline_v2.lighting.scene_local_environment_shader_ready`.
- The field is true only when:
  - the scene visual contract is active.
  - the environment owner is known.
  - no invalid external HDRI is reported.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py` now statically
  checks the forward/sky/water scene-local shader surface.

Current caveat:

- This slice is shader/source-contract evidence. It still needs visual packets
  before V2 scene-local environment output can be promoted as the default look.

### Checkpoint - 2026-06-05 Early AM

Pushed commits:

- `d81dad4 Add scene material policy shader bridge`
- `7f5d57c Add jitter-aware temporal reprojection contract`
- `5e0b9e6 Add RT reflection miss ownership contract`

Latest focused validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python tools\validate_full_scene_shader_material_fulfillment_v2.py
```

Result:

- all three passed.
- fulfillment remains correctly `PENDING`: `56` requests, `56` pending,
  `0` admitted.

Native build attempt:

```powershell
.\build.ps1 -Config Release
```

Result:

- timed out after about `124s`.
- leftover `cmake`/`ninja` processes were found and stopped.
- do not treat this as a passing native build.

Next recommended slice:

- Either run a longer/cleaner native build outside the CMake regeneration hang,
  or continue focused V2 domain slices with static validators until the build
  path is made reliable.
- Strong next code target: finish scene-local environment/background ownership
  across forward/basic, sky, water, and UI/debug controls, then add a packet
  command that captures reflection-owner/material-policy/temporal debug views
  on one enclosed scene.

## Resume Commands

```powershell
git -c submodule.recurse=false status --short --ignore-submodules=all
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AssetRegistryV2
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action SceneAssetBindings
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AAAAssetQuality
python tools\plan_aaa_asset_replacements.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action AAAProviderRequests
python tools\analyze_aaa_asset_quality.py --renderer-manifest build\captures\scene_local_cinematic_renderer_v1_final_gate_20260605\warm_micro_jitter_full_seq8\manifest.json
Get-Content docs\media\final_art\generated\aaa_asset_quality\aaa_asset_quality_report.md
Get-Content docs\media\final_art\generated\aaa_asset_quality\aaa_asset_replacement_work_orders.md
Get-Content docs\media\final_art\generated\aaa_asset_quality\provider_requests\manifest.md
Get-Content docs\FULL_SCENE_SHADER_PIPELINE_V2.md
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialEvidence
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialUpgradePlan
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialProviderRequests
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderMaterialFulfillmentBaseline
```

## Git Policy

- Commit and push focused work often.
- Do not blanket-stage the dirty worktree.
- Stage only files touched for the current AAA asset-quality slice.
- If GitHub push fails due credentials/network, keep the local commit and
  record the failure here.

## Full Scene Shader Pipeline V2 Refactor Planning Checkpoint - 2026-06-05

Latest user direction:

- Move from isolated renderer fixes toward full-scene shaders capable of
  Unreal-like visual quality.
- Plan the whole refactor before landing the next goal feature.
- Keep the focus on scene-owned renderer architecture, not another
  one-screenshot visual tweak.

Current planning state:

- `docs/FULL_SCENE_SHADER_PIPELINE_V2.md` now contains a whole-renderer
  refactor strategy.
- The plan defines six ownership layers:
  - scene visual contract
  - material and asset evidence
  - frame data and GBuffer
  - scene illumination
  - stability and composition
  - presentation and evidence
- The plan adds explicit refactor boundaries so lighting, reflections,
  temporal, post, and debug packets consume shared scene/material truth instead
  of inventing local per-pass interpretations.
- The target data flow is:
  `scene preset / scene graph / asset registry -> SceneVisualContract ->
  FullSceneShaderFrameContext -> material/light/probe/post policies ->
  Visibility/GBuffer -> Lighting/Reflections/Shadows -> Material-aware
  Temporal -> HDR Post -> Debug Atlases + Frame Report + Beauty Output`.
- The implementation tracks are now:
  - Track A: contracts and facades
  - Track B: material truth
  - Track C: GBuffer and debug surfaces
  - Track D: lighting, probes, and reflections
  - Track E: shadows and temporal stability
  - Track F: post, render graph, and promotion
- The promotion ladder is now explicit:
  `planned -> instrumented -> shadow_output -> candidate -> packet_passed ->
  cross_family_passed -> default_ready`.

Next implementation slice when coding resumes:

1. Add `FullSceneShaderFrameContext` as the runtime facade.
2. Populate it from current V1 scene profile state.
3. Add per-domain promotion state for material, GBuffer, lighting, reflection,
   shadow, temporal, post, and render graph.
4. Emit owner, fallback owner, readiness, and failure reason into frame-report
   JSON.
5. Add a packet command that captures beauty plus debug atlases.
6. Keep beauty output on V1 until this instrumentation proves the renderer can
   explain its own pixels.

Important constraint:

- Do not begin with a shader beauty tweak. The next real feature should be the
  runtime facade plus evidence packet skeleton, because that creates the brain
  and harness for later AAA shader work.

## Full Scene Shader Pipeline V2 Runtime Facade Slice - 2026-06-05

Purpose:

- Start the real V2 implementation by creating a shared runtime facade for
  full-scene shader evidence.
- Keep beauty output on V1 while the renderer learns to explain per-domain
  ownership, readiness, fallback, and promotion state.

Implemented:

- Added `src/Graphics/FullSceneShaderFrameContext.h`.
- Added `FullSceneShaderPromotionState` with the promotion ladder:
  `planned`, `instrumented`, `shadow_output`, `candidate`, `packet_passed`,
  `cross_family_passed`, and `default_ready`.
- Added `FullSceneShaderDomainEvidence` with:
  - `enabled`
  - `ready`
  - `promotionState`
  - `owner`
  - `fallbackOwner`
  - `failureReason`
- Added `BuildFullSceneShaderFrameContext(const FrameContract&)`.
- The facade derives shared V2 evidence from the existing V1 `FrameContract`:
  - material family count coverage
  - material reflection policy coverage
  - material temporal policy coverage
  - material post sensitivity coverage
  - velocity readiness
  - material policy channel readiness
  - jitter-aware temporal readiness
  - reflection owner report availability
  - RT reflection miss environment policy readiness
  - scene-local environment shader readiness
  - named post-stage readiness
  - explicit render-graph readiness
- `FrameContractJson.cpp` now builds `FullSceneShaderFrameContext` and emits a
  common `evidence` object for each V2 section.
- The V2 frame-report contract now declares common evidence fields:
  `promotion_state`, `domain_ready`, `facade_owner`, `fallback_owner`, and
  `failure_reason`.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py` now verifies:
  - the runtime source includes `FullSceneShaderFrameContext`.
  - the JSON builder calls `BuildFullSceneShaderFrameContext(contract)`.
  - every common evidence field is emitted.
  - optional supplied frame reports include an `evidence` object for each V2
    section.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Result:

- all passed.

Native build attempt:

```powershell
cmake --build build --config Release --target CortexEngine
```

Result:

- timed out after about `184s`.
- leftover `cmake`/`ninja` helper processes were found and stopped.
- do not treat native compilation as verified for this slice.

Current caveat:

- This is an instrumentation/facade slice, not a beauty-output promotion.
- `full_scene_shader_pipeline_v2.status` remains
  `runtime_placeholder_v1_fallback`.
- `beauty_output` remains `v1_fallback`.

Next recommended implementation:

- Add the packet command/smoke that captures one scene with beauty plus V2
  debug atlases and the frame-report JSON.
- After that, start Track B material truth by moving richer material evidence
  into the runtime material model.

## Full Scene Shader Pipeline V2 Packet Harness Slice - 2026-06-05

Purpose:

- Make the runtime facade verifiable through a repeatable packet command,
  rather than only through static source checks.

Implemented:

- Added `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
- The script wraps `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`
  instead of duplicating launch/capture logic.
- Default packet scope is intentionally narrow:
  - family: `gallery`
  - views:
    `beauty`, `surface_policy`, `reflection_owner`, `shadow_factor`,
    `direct_light`, `ambient_ibl`, `taa_blend`
- The packet validates every emitted report with:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py --frame-report <report> --strict-frame-report
```

- The packet writes:
  - `v2_frame_report_evidence_summary.json`
  - `v2_frame_report_evidence_summary.md`
  - `v2_frame_report_checker_stdout.txt`
- The checker now accepts the real engine report shape where V2 data lives
  under `frame_contract.full_scene_shader_pipeline_v2`.
- The checker now verifies the packet runner exists and includes required V2
  debug views/evidence fields.
- `tools/FinalArtPipeline.ps1` now exposes:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\FinalArtPipeline.ps1 -Action FullSceneShaderV2Packet
```

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
powershell -NoProfile -Command "`$null = [scriptblock]::Create((Get-Content 'tools\run_full_scene_shader_pipeline_v2_packet.ps1' -Raw)); `$null = [scriptblock]::Create((Get-Content 'tools\FinalArtPipeline.ps1' -Raw)); Write-Output 'scripts parse ok'"
```

Result:

- all passed.

Current caveat:

- The packet was not run against a freshly built executable because the native
  build timed out in the previous slice.
- Running the packet before a successful build may validate an older
  executable that does not contain `FullSceneShaderFrameContext`.

Next recommended implementation:

- Fix or work around the native build timeout enough to produce a fresh
  executable.
- Run `FullSceneShaderV2Packet` and admit the generated evidence summary.
- Then move into Track B material truth.

## Full Scene Shader Pipeline V2 Fresh Build And Packet Admission - 2026-06-05

Build diagnosis:

- Direct `cmake --build build --config Release --target CortexEngine` failed
  because the current shell had empty `INCLUDE` and `LIB`; MSVC could not find
  the standard library header `string`.
- `build.ps1` imports the Visual Studio developer environment via
  `VsDevCmd.bat`, so that is the correct build entry point.

Fresh build:

```powershell
.\build.ps1 -Config Release
```

Result:

- passed.
- build time: about `112.4s`.
- executable: `build\bin\CortexEngine.exe`.
- warnings remain in existing code/vendor paths; no V2 facade compile failure.

Runtime V2 packet:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_facade_packet_20260605
```

Result:

- passed.
- manifest:
  `build/captures/full_scene_shader_pipeline_v2_facade_packet_20260605/manifest.json`
- summary:
  `build/captures/full_scene_shader_pipeline_v2_facade_packet_20260605/v2_frame_report_evidence_summary.json`
- captured views: `7`
- evidence rows: `70`
- failures: `0`

Observed V2 packet state:

- `gbuffer`, `lighting`, `reflections`, `shadows`, `temporal`, and `post`
  reported `instrumented` evidence with `domain_ready=true` for the gallery
  packet views.
- `material` remains `instrumented` but not ready because
  `FullSceneMaterialModel` is not promoted.
- `render_graph` remains `instrumented` but not ready because explicit
  producer/debug ownership is not promoted.
- `asset_evidence` and `packet_gate` remain `planned`.
- beauty output remains intentionally `v1_fallback`.

Next recommended implementation:

- Start Track B material truth:
  - move richer material evidence into runtime `MaterialModel`.
  - replace the current material-domain `domain_ready=false` with actual
    `FullSceneMaterialModel` readiness once the runtime model owns family,
    texture evidence, feature bits, reflection policy, temporal policy, and
    post sensitivity.

## Full Scene Shader Refactor Planning Checkpoint - 2026-06-05

User direction:

- Move from individual scene polish and flicker-specific fixes into full-scene
  shaders capable of breathtaking Unreal-like visuals.
- Plan the entire refactor before completing/promoting the goal feature.
- Keep this as a renderer architecture migration, not a beauty tweak.

Plan update:

- `docs/FULL_SCENE_SHADER_PIPELINE_V2.md` now has a `Master Refactor Plan`.
- The central rule is that final beauty pixels must be assembled from
  scene-owned facts, not pass-local guesses.
- Target runtime dataflow:

```text
ScenePreset / SceneGraph / AssetRegistry
  -> SceneVisualContract
  -> FullSceneMaterialTable
  -> FullSceneLightRig
  -> FullSceneProbeSet
  -> FullSceneRenderGraph
  -> FullSceneFrameData / GBuffer
  -> Lighting + Reflections + Shadows
  -> Material-aware Temporal
  -> HDR Presentation
  -> Evidence Packet + Beauty
```

Promotion ladder:

- `Instrumented`: report ownership/readiness/fallback; V1 beauty remains.
- `Shadow Output`: V2 runs beside V1 and emits compare/debug views.
- `Candidate`: selected scenes can render V2 beauty under packet gates.
- `Default Ready`: cross-family evidence passes and user accepts visual
  direction.

Implementation order:

1. Material truth: `FullSceneMaterialModel` becomes real runtime evidence.
2. Frame data/GBuffer: carry material/object/policy facts to shaders.
3. Scene-local lighting: semantic rigs for scene families.
4. Local reflection ownership: room, hero, planar, SSR, RT, neutral, external.
5. Shadow/contact stability: bias/filter/contact policy by scene/material.
6. Material-aware temporal: different history policies for glass, metal, water,
   emissive, tile, fabric, paint, and matte surfaces.
7. Named HDR post: exposure, rolloff, bloom, grade, clarity, sharpening.
8. Render graph ownership: pass/resource/debug producer validation.
9. Cross-family promotion: gallery, kitchen, office, gym, concert, and at
   least one wet/glass-heavy scene.

Current state remains:

- V2 facade and packet harness are implemented and pushed.
- Fresh V2 gallery facade packet passed with `7` views and `70` evidence rows.
- Beauty output remains `v1_fallback`.
- Material remains the first real blocker: `material` is instrumented but not
  ready because `FullSceneMaterialModel` is not promoted.

Next safe implementation checkpoint:

- Do Track B material truth first.
- Do not start with bloom, IBL blur, one-off reflection tweaks, or screenshot
  styling.
- The first implementation should make material readiness evidence real and
  keep beauty output on V1 until material packets prove ownership.

## Full Scene Shader Pipeline V2 Material Truth Slice - 2026-06-05

Purpose:

- Convert the V2 material domain from a hardcoded not-ready placeholder into a
  runtime evidence gate owned by the material system.
- Keep beauty output on V1 while making material readiness measurable.

Implemented:

- Added `FullSceneMaterialModelEvidence` to `src/Graphics/MaterialModel.h`.
- Added `BuildFullSceneMaterialModelEvidence(const FrameContract::MaterialStats&)`
  to `src/Graphics/MaterialModel.cpp`.
- The evidence builder now reports:
  - sampled material count
  - policy-applied count
  - family coverage
  - reflection-policy coverage
  - temporal-policy coverage
  - post-policy coverage
  - descriptor/texture evidence
  - shader feature flag evidence
  - unknown/default family count
  - descriptor missing/failure counts
  - material validation errors
- `FullSceneShaderFrameContext` now consumes this material-owned evidence
  instead of hardcoding `material.enabled=false` and
  `full_scene_material_model_ready=false`.
- `FrameContractJson.cpp` now emits the material readiness sub-gates into
  `full_scene_shader_pipeline_v2.material`.
- The frame-report contract now requires the new material readiness fields.
- The V2 checker now requires the material evidence builder and its policy /
  descriptor gates.
- The resolver now classifies named canonical presets that previously fell
  through as default:
  - `matte` -> ceramic tile
  - `backdrop` -> painted wall
  - `velvet`, `skin`, `foliage` -> fabric-like semantic material
  - `sand` -> concrete/granular masonry

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- src\Graphics\MaterialModel.h src\Graphics\MaterialModel.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp tools\check_full_scene_shader_pipeline_v2_frame_report.py assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json
.\build.ps1 -Config Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_material_truth_ready_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- diff whitespace check: passed.
- native Release build: passed.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- manifest:
  `build/captures/full_scene_shader_pipeline_v2_material_truth_ready_packet_20260605/manifest.json`
- summary:
  `build/captures/full_scene_shader_pipeline_v2_material_truth_ready_packet_20260605/v2_frame_report_evidence_summary.json`
- captured views: `7`
- evidence rows: `70`
- failures: `0`

Material evidence from the packet:

- `enabled=true`
- `domain_ready=true`
- `full_scene_material_model_ready=true`
- `runtime_policy_bridge_ready=true`
- `sampled_material_count=60`
- `policy_applied_count=60`
- `texture_evidence_available=true`
- `descriptor_missing_count=0`
- `descriptor_refresh_failure_count=0`
- `unknown_material_family_count=0`
- `validation_error_count=0`
- facade owner: `MaterialResolver/FullSceneMaterialModelEvidence`
- fallback owner remains `v1_fallback`

Current caveats:

- Beauty output remains intentionally `v1_fallback`.
- This proves runtime material-policy ownership for the gallery packet. It does
  not yet prove imported asset registry PBR texture quality, hero-surface
  readiness, or cross-family material readiness.
- `render_graph` remains not ready.
- `asset_evidence` and `packet_gate` remain planned.

Next recommended implementation:

- Track C: make frame data/GBuffer ownership stronger.
- Add/validate material id/object id/debug producer ownership rather than only
  broad material policy channels.
- Keep cross-family promotion blocked until material readiness is shown beyond
  the gallery packet.

## Full Scene Shader Pipeline V2 GBuffer Ownership Evidence Slice - 2026-06-05

Purpose:

- Make Track C honest and actionable.
- Stop reporting the V2 GBuffer domain as ready merely because broad material
  policy channels and velocity exist.
- Separate "required GBuffer resources exist" from "full frame data ownership
  exists".

Implemented:

- Added `FullSceneGBufferEvidence` inside
  `src/Graphics/FullSceneShaderFrameContext.h`.
- The evidence now reports:
  - channel inventory availability
  - albedo channel readiness
  - normal/roughness channel readiness
  - emissive/metallic channel readiness
  - extended material channel readiness
  - semantic material-policy channel readiness
  - velocity channel readiness
  - producer ownership availability from pass records
  - material-id channel readiness
  - object-id channel readiness
  - debug-view source ownership readiness
  - missing required channel count
  - missing ownership channel count
- `FrameContractJson.cpp` now emits these sub-gates under
  `full_scene_shader_pipeline_v2.gbuffer`.
- The V2 frame-report contract now requires the new GBuffer readiness fields.
- The V2 checker now requires the GBuffer evidence struct and the explicit
  not-promoted failure reasons for material id, object id, and debug producer
  ownership.

Important behavior change:

- `gbuffer.domain_ready` is now `false` until stable per-pixel material ids,
  object ids, and debug-view source ownership are promoted.
- This is intentional. The previous green GBuffer row was too broad for the
  full-scene shader plan.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp tools\check_full_scene_shader_pipeline_v2_frame_report.py assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json
.\build.ps1 -Config Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_gbuffer_ownership_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static V2 frame-report checker: passed.
- V2 plan validator: passed.
- Python compile: passed.
- diff whitespace check: passed.
- native Release build produced a fresh executable at `2026-06-05 03:00:40`.
  The shell wrapper timed out while waiting, but the underlying CMake/Ninja
  processes completed and the executable timestamp updated.
- V2 runtime packet: passed.
- `ctest`: completed, but this build directory reported `No tests were found`.

Packet evidence:

- manifest:
  `build/captures/full_scene_shader_pipeline_v2_gbuffer_ownership_packet_20260605/manifest.json`
- summary:
  `build/captures/full_scene_shader_pipeline_v2_gbuffer_ownership_packet_20260605/v2_frame_report_evidence_summary.json`
- captured views: `7`
- evidence rows: `70`
- failures: `0`

GBuffer evidence from the packet:

- `enabled=true`
- `domain_ready=false`
- `channel_inventory_available=true`
- `albedo_channel_ready=true`
- `normal_roughness_channel_ready=true`
- `emissive_metallic_channel_ready=true`
- `extended_material_channels_ready=true`
- `semantic_material_policy_channel_ready=true`
- `velocity_channel_ready=true`
- `producer_ownership_available=true`
- `missing_required_channel_count=0`
- `material_id_channel_ready=false`
- `object_id_channel_ready=false`
- `debug_view_source_report_available=false`
- `missing_ownership_channel_count=3`
- failure reason: `Stable per-pixel material-id channel is not promoted`
- facade owner: `VisibilityBufferRenderer/FullSceneGBufferEvidence`

Current state:

- Material domain is ready for the gallery packet.
- Required GBuffer resources and producers are present for the gallery packet.
- V2 frame-data ownership is still blocked by missing stable material-id,
  object-id, and debug producer-source reporting.
- Beauty output remains intentionally `v1_fallback`.

Next recommended implementation:

- Continue Track C by adding a stable per-pixel material-id/object-id ownership
  contract.
- Prefer a shadow/debug path first:
  1. inventory what `visibility_buffer` currently encodes,
  2. decide whether it can serve as object/instance id evidence or whether a
     new `vb_gbuffer_object_id`/`vb_gbuffer_material_id` target is required,
  3. expose producer/debug source reporting in frame reports,
  4. only then allow `gbuffer.domain_ready=true`.

## Full Scene Shader Refactor Master Plan - 2026-06-05

User direction:

- Move to full-scene shaders for Unreal-like visual quality.
- Plan the whole refactor before completing or promoting the goal feature.
- Treat this as a renderer architecture migration, not an IBL, bloom, contrast,
  or one-scene beauty tweak.

Implemented:

- Added `docs/FULL_SCENE_SHADER_REFACTOR_MASTER_PLAN.md`.
- Linked it from `docs/FULL_SCENE_SHADER_PIPELINE_V2.md`.

Master-plan decision:

- V2 beauty must be assembled from scene-owned facts:
  `SceneVisualContract -> FullSceneMaterialTable -> FullSceneFrameData/GBuffer
  -> FullSceneLightRig -> FullSceneProbeSet -> Shadows/Reflections/Indirect
  -> Material-aware Temporal -> HDR Post -> Beauty/Debug/Frame Report`.
- V1 remains the playable fallback until V2 packets prove stronger evidence.
- No V2 domain can promote without debug views, frame-report fields, and packet
  evidence.

Planned refactor phases:

1. Freeze the V1 baseline and keep V2 evidence-only where needed.
2. Add frame identity and GBuffer ownership.
3. Promote a full runtime material table.
4. Build scene-local semantic light rigs.
5. Build local reflection/probe ownership.
6. Centralize shadow/contact stability.
7. Add material-aware temporal resolve.
8. Split HDR post into named measured stages.
9. Enforce render graph resource/pass/debug ownership.
10. Run cross-family V2 promotion packets.

Next concrete feature:

- `FSSP-V2-003A Identity Ownership`.
- Expose `visibility_buffer` as a frame-report resource.
- Report visibility instance count, material table count, and invalid stable id
  count.
- Add V2 GBuffer evidence for visibility payload readiness, producer readiness,
  instance identity table readiness, material lookup readiness, and stable
  instance id readiness.
- Keep `gbuffer.domain_ready=false` until material id/object id/debug-source
  ownership is genuinely promoted.

## Full Scene Shader V2 Identity Ownership Slice - 2026-06-05

Implemented:

- `visibility_buffer` is now reported as a frame-contract resource.
- draw counts now include:
  - `visibility_buffer_materials`.
  - `visibility_buffer_invalid_stable_ids`.
- V2 GBuffer evidence now includes:
  - `visibility_payload_channel_ready`.
  - `visibility_payload_producer_ready`.
  - `instance_identity_table_ready`.
  - `instance_material_lookup_ready`.
  - `stable_instance_id_available`.
  - `visibility_buffer_instance_count`.
  - `visibility_buffer_material_count`.
  - `invalid_stable_instance_id_count`.
- `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
  and `tools/check_full_scene_shader_pipeline_v2_frame_report.py` require the
  new identity evidence.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- src\Graphics\FrameContract.h src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\Renderer_VisibilityBufferCollection.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json tools\check_full_scene_shader_pipeline_v2_frame_report.py
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_identity_ownership_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static checker: passed.
- plan validator: passed.
- Python compile: passed.
- diff check: passed.
- executable target build: passed.
- V2 packet: passed.
- `ctest`: ran but reported `No tests were found`.

Build caveat:

- `.\build.ps1 -Config Release` hung in `tools/sync_assets.cmake`.
- The stuck `cmake`/`ninja` processes were stopped.
- The direct `CortexEngine` target build with the VS environment passed.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_identity_ownership_packet_20260605`.
- captured views: `7`.
- evidence rows: `70`.
- failures: `0`.
- gallery beauty GBuffer identity:
  - `visibility_payload_channel_ready=true`.
  - `visibility_payload_producer_ready=true`.
  - `instance_identity_table_ready=true`.
  - `instance_material_lookup_ready=true`.
  - `stable_instance_id_available=true`.
  - `visibility_buffer_instance_count=55`.
  - `visibility_buffer_material_count=36`.
  - `invalid_stable_instance_id_count=0`.
  - `missing_required_channel_count=0`.
  - `missing_ownership_channel_count=3`.
  - `gbuffer.domain_ready=false`.
  - failure reason:
    `Stable per-pixel material-id channel is not promoted`.

Next:

- Continue with `FSSP-V2-003B Per-Pixel Identity Debug`.
- Add or expose material-id/object-id debug views from the visibility payload
  and instance/material tables.
- Do not promote V2 beauty yet.

## Full Scene Shader V2 Per-Pixel Identity Debug Slice - 2026-06-05

Implemented:

- Added visibility debug modes for per-pixel material id and stable object id.
- `DebugBlitVisibility.hlsl` now uses the visibility payload plus the
  visibility instance table to visualize:
  - payload/instance id.
  - material id.
  - stable object id.
- Expanded the debug-blit root signature to carry mode constants and the
  instance-table root SRV.
- Wired identity debug modes through the immediate visibility path and the
  render-graph visibility path.
- Added debug menu modes:
  - `48 = VB_MaterialId`.
  - `49 = VB_StableObjectId`.
- V2 packet defaults now include `material_id` and `object_id`.
- `FullSceneGBufferEvidence` now marks ownership ready when the packet-proved
  visibility payload, producer, instance/material lookup, and stable id
  evidence are all present.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- assets\shaders\DebugBlitVisibility.hlsl src\Graphics\VisibilityBuffer.h src\Graphics\VisibilityBuffer_DebugBlit.cpp src\Graphics\VisibilityBuffer_DebugBlitPipelines.cpp src\Graphics\Renderer_VisibilityBufferStages.cpp src\Graphics\Renderer_VisibilityBufferCulling.cpp src\Graphics\Renderer_VisibilityBufferOrchestration.cpp src\Graphics\Renderer_RenderGraphVisibilityBufferHelpers.h src\Graphics\Passes\VisibilityBufferGraphPass.h src\Graphics\Passes\VisibilityBufferGraphPass.cpp src\Graphics\Renderer_RenderGraphVisibilityBuffer.cpp src\Graphics\Renderer_DebugSettings.cpp tools\run_full_scene_shader_pipeline_v2_packet.ps1 tools\check_full_scene_shader_pipeline_v2_frame_report.py src\Graphics\FullSceneShaderFrameContext.h tools\FinalArtPipeline.ps1
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_per_pixel_identity_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static checker: passed.
- plan validator: passed.
- Python compile: passed.
- diff check: passed.
- focused changed-object build: passed.
- full `CortexEngine` target build: passed.
- V2 packet: passed.
- `ctest`: ran but reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_per_pixel_identity_packet_20260605`.
- captured views: `9`.
- evidence rows: `90`.
- failures: `0`.
- new views:
  - `material_id`.
  - `object_id`.

Gallery beauty GBuffer evidence:

- `visibility_payload_channel_ready=true`.
- `visibility_payload_producer_ready=true`.
- `instance_identity_table_ready=true`.
- `instance_material_lookup_ready=true`.
- `stable_instance_id_available=true`.
- `material_id_channel_ready=true`.
- `object_id_channel_ready=true`.
- `debug_view_source_report_available=true`.
- `visibility_buffer_instance_count=55`.
- `visibility_buffer_material_count=36`.
- `invalid_stable_instance_id_count=0`.
- `missing_required_channel_count=0`.
- `missing_ownership_channel_count=0`.
- `gbuffer.domain_ready=true`.
- failure reason:
  `FullSceneFrameData GBuffer ownership is ready`.

Build caveat:

- The first direct build attempt timed out in `tools/sync_assets.cmake`.
- Stale CortexEngine build workers were stopped.
- After refreshing only the local build stamp under `build/`, focused object
  builds and the full `CortexEngine` target build passed.

Current state:

- `FSSP-V2-003B` is packet-proved for the gallery target.
- V2 now has the per-pixel identity substrate needed by material-aware
  lighting, reflections, shadows, temporal resolve, and HDR post.
- V2 beauty is still intentionally `v1_fallback`.
- The next architecture step is full runtime material-table promotion, not
  visual tuning.

## Full Scene Shader V2 Runtime Material Table Slice - 2026-06-05

Implemented:

- `FullSceneMaterialModelEvidence` now distinguishes sampled-material evidence
  from shader-facing material-table readiness.
- Added packet-visible evidence fields:
  - `shader_material_table_ready`.
  - `shader_material_policy_rows_ready`.
  - `gbuffer_policy_channel_backed_by_material_table`.
  - `shader_material_table_row_count`.
  - `shader_material_policy_column_count`.
- `BuildFullSceneMaterialModelEvidence` now receives the visibility-buffer
  material table row count and GBuffer policy-channel readiness.
- `fullSceneMaterialModelReady` now requires the shader material table and
  GBuffer policy channel, not only policy counts and descriptor evidence.
- Updated the V2 frame-report contract and checker to require the new fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- src\Graphics\MaterialModel.h src\Graphics\MaterialModel.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json tools\check_full_scene_shader_pipeline_v2_frame_report.py
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_material_table_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static checker: passed.
- plan validator: passed.
- Python compile: passed.
- diff check: passed.
- focused object build: passed.
- full `CortexEngine` target build: passed.
- V2 packet: passed.
- `ctest`: ran but reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_material_table_packet_20260605`.
- captured views: `9`.
- evidence rows: `90`.
- failures: `0`.

Gallery beauty material evidence:

- `sampled_material_count=60`.
- `shader_material_table_row_count=36`.
- `shader_material_policy_column_count=4`.
- `runtime_policy_bridge_ready=true`.
- `shader_material_policy_rows_ready=true`.
- `shader_material_table_ready=true`.
- `gbuffer_policy_channel_backed_by_material_table=true`.
- `texture_evidence_available=true`.
- `unknown_material_family_count=0`.
- `validation_error_count=0`.
- `material.domain_ready=true`.

Current state:

- V2 has both per-pixel identity and a packet-visible shader material table.
- This gives later lighting, reflections, shadows, temporal, and post a common
  material truth layer.
- V2 beauty is still intentionally `v1_fallback`.
- Next slice should add material policy debug views before promoting any
  lighting/reflection beauty behavior.

## Full Scene Shader V2 Material Policy Debug Views - 2026-06-05

Implemented:

- Added visibility-buffer material-policy debug modes:
  - `50`: `VB_MaterialFamilyPolicy`.
  - `51`: `VB_ReflectionPolicy`.
  - `52`: `VB_TemporalPolicy`.
  - `53`: `VB_PostSensitivity`.
- `DebugBlitVisibility.hlsl` now reads the shader-facing material table at
  `t2` and colors pixels from `VBMaterialConstants.policyParams.x/y/z/w`.
- The debug blit root signature now exposes the material-table SRV, and the
  runtime blit path fails clearly if a material-policy view is requested before
  the material table exists.
- The packet runner recognizes:
  - `material_family`.
  - `reflection_policy`.
  - `temporal_policy`.
  - `post_sensitivity`.
- The default V2 packet now captures `13` views:
  `beauty`, `surface_policy`, the four material-policy views,
  `material_id`, `object_id`, `reflection_owner`, `shadow_factor`,
  `direct_light`, `ambient_ibl`, and `taa_blend`.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
git diff --check -- assets\shaders\DebugBlitVisibility.hlsl src\Graphics\VisibilityBuffer.h src\Graphics\VisibilityBuffer_DebugBlit.cpp src\Graphics\VisibilityBuffer_DebugBlitPipelines.cpp src\Graphics\Renderer_DebugSettings.cpp src\Graphics\Renderer_VisibilityBufferCulling.cpp src\Graphics\Renderer_VisibilityBufferOrchestration.cpp src\Graphics\Renderer_VisibilityBufferStages.cpp src\Graphics\Renderer_RenderGraphVisibilityBufferHelpers.h tools\run_full_scene_shader_pipeline_v2_packet.ps1 tools\FinalArtPipeline.ps1 tools\check_full_scene_shader_pipeline_v2_frame_report.py assets\final_art\full_scene_shader_pipeline_v2_frame_report_contract.json tools\run_scene_local_cinematic_renderer_v1_packets.ps1
cmake --build build --config Release --target CortexEngine --parallel
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_material_policy_debug_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- static checker: passed.
- plan validator: passed.
- Python compile: passed.
- diff check: passed.
- full `CortexEngine` target build: passed and linked after restoring the
  Visual Studio developer environment.
- V2 packet: passed.
- `ctest`: ran but reported `No tests were found`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_material_policy_debug_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.

Current state:

- `FSSP-V2-002C` is packet-proved for the gallery target.
- Material-family, reflection-policy, temporal-policy, and post-sensitivity
  are now per-pixel inspectable from the same shader material table used by the
  visibility-buffer path.
- V2 beauty is still intentionally `v1_fallback`.
- Next architecture slice should start scene-local semantic light-rig ownership
  and then local reflection/probe ownership.

## Full Scene Shader V2 Semantic Light Rig Evidence - 2026-06-05

Implemented:

- Added `FullSceneLightingRigEvidence` as the V2 lighting-domain readiness
  source.
- V2 lighting now reports semantic rig/source readiness, scene-local
  environment readiness, semantic light roles, policy-id consistency, lighting
  balance policy, local fixture contract readiness, shadowed-light ownership,
  exposure-policy readiness, intensity bounds, and missing contract count.
- The V2 checker requires the lighting evidence builder and the new JSON fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8 --verbose
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_semantic_light_rig_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_semantic_light_rig_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.
- gallery V2 lighting:
  - `missing_lighting_contract_count=0`.
  - `semantic_fixture_light_count=4`.
  - `stage_fixture_light_count=2`.
  - `rect_area_light_count=2`.
  - `shadow_casting_light_count=1`.
  - `lighting.domain_ready=true`.

Current state:

- `FSSP-V2-004A` is packet-proved for the gallery target.
- V2 lighting is now contract-owned enough to begin the actual shader-side
  semantic lighting pass work.
- Next architecture slice should build local reflection/probe ownership and RT
  miss fallback evidence.

## Full Scene Shader V2 Reflection Ownership Evidence - 2026-06-05

Implemented:

- Added `FullSceneReflectionOwnershipEvidence` as the V2 reflection-domain
  readiness source.
- V2 reflections now report reflection-owner readiness, material reflection
  policy coverage, external IBL authorization, local probe table/radiance/
  intensity readiness, RT miss safety, enclosed miss fallback safety,
  reflection source contract readiness, and missing reflection contract count.
- The V2 checker requires the reflection ownership evidence builder and the new
  JSON fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8 --verbose
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_ownership_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_ownership_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.
- gallery V2 reflections:
  - `missing_reflection_contract_count=0`.
  - `room_probe_count=2`.
  - `local_probe_contract_ready=true`.
  - `external_ibl_visibility_authorized=true`.
  - `rt_miss_environment_policy_ready=true`.
  - `reflections.domain_ready=true`.

Current state:

- `FSSP-V2-005A` is packet-proved for the gallery target.
- V2 reflection/probe ownership is now contract-owned enough to begin the
  actual local reflection/probe shader-side work.
- Next architecture slice should formalize shadow/contact stability evidence.

## Full Scene Shader V2 Shadow Contact Evidence - 2026-06-05

Implemented:

- Added `FullSceneShadowContactEvidence` as the V2 shadow-domain readiness
  source.
- V2 shadows now report shadow policy, shadow-map resource readiness,
  `ShadowPass` producer ownership, caster ownership, cascade/bias/filter policy
  bounds, RT shadow mask/history readiness, contact shadow readiness, stability
  gate status, and missing shadow contract count.
- The V2 checker requires the shadow/contact evidence builder and the new JSON
  fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8 --verbose
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_shadow_contact_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_shadow_contact_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.
- gallery V2 shadows:
  - `missing_shadow_contract_count=0`.
  - `shadow_map_ready=true`.
  - `shadow_map_producer_ready=true`.
  - `shadow_caster_ownership_ready=true`.
  - `rt_shadow_mask_ready=true`.
  - `rt_shadow_history_ready=true`.
  - `contact_shadow_ready=true`.
  - `shadow_stability_gate_passed=true`.
  - `shadows.domain_ready=true`.

Current state:

- `FSSP-V2-006A` is packet-proved for the gallery target.
- V2 shadow/contact stability is now contract-owned enough to begin shader-side
  shadow/contact promotion work.
- Next architecture slice should formalize material-aware temporal promotion
  evidence.

## Full Scene Shader V2 Beauty Candidate Refactor Plan - 2026-06-05

User direction:

- Move toward full-scene shader quality and Unreal-like visuals.
- Plan the whole refactor before completing the next goal feature.
- Do not repeat cosmetic fixes such as IBL blur, disabling reflections, or
  per-scene tuning that hides instability.

Plan update:

- `docs/FULL_SCENE_SHADER_REFACTOR_MASTER_PLAN.md` now defines the next goal
  feature as `FullSceneShaderV2BeautyCandidate`.
- The feature is a switchable V2 path, not a single shader toggle:

```text
V1 playable path
  -> unchanged default fallback

V2 candidate path
  -> FullSceneMaterialResolveV2
  -> FullSceneLightingV2
  -> FullSceneReflectionResolveV2
  -> FullSceneShadowCompositeV2
  -> FullSceneTemporalResolveV2
  -> FullScenePostV2
  -> Beauty candidate + debug atlas + frame report
```

Core rule:

- The final V2 beauty pixel must be explainable from scene-owned facts:
  scene visual contract, material table row, semantic lights, reflection
  source, shadow policy, temporal policy, HDR post profile, and render graph
  producer ownership.

Workstreams added to the master plan:

1. Shared `FullSceneFrameData`.
   - one shader-facing source for material/object/policy identity plus normal,
     depth, roughness, metallic, AO, emissive, and velocity.
2. `FullSceneLightingV2`.
   - semantic light buffers and direct lighting for key/fill/practical/display/
     stage/high-bay/sun/skylight/accent roles.
3. `FullSceneReflectionResolveV2`.
   - one resolver for SSR, RT, room probes, hero probes, planar probes, neutral
     fallback, and authorized external IBL.
4. `FullSceneShadowCompositeV2`.
   - centralized cascade/local/RT/contact shadow policy and debug ownership.
5. `FullSceneTemporalResolveV2`.
   - material/object-aware history confidence, clamp widths, jitter-consistent
     reprojection, and high-FPS mouse-jiggle validation.
6. `FullScenePostV2`.
   - named HDR stages for exposure, bloom, highlight rolloff, tone map, color
     grade, clarity/sharpen, and output encode.
7. `FullSceneRenderGraphContract`.
   - explicit pass/resource/debug ownership for the V2 candidate path.

Milestone order:

1. `FSSP-V2-007A`: material-aware temporal evidence.
2. `FSSP-V2-004B`: semantic light buffers and direct-light shadow output.
3. `FSSP-V2-005B`: reflection source resolver shadow output.
4. `FSSP-V2-006B`: shadow composite policy centralization.
5. `FSSP-V2-007B`: material-aware temporal candidate.
6. `FSSP-V2-008A`: HDR post stage contract.
7. `FSSP-V2-009A`: render graph contract.
8. `FSSP-V2-010A`: cross-family V2 candidate packet.
9. `FSSP-V2-010B`: user review packet.

Current implementation state after planning:

- No shader behavior was changed in this planning slice.
- V1 remains the playable fallback.
- Existing packet-proved V2 domains remain:
  - GBuffer identity.
  - material table and material policy debug views.
  - semantic light-rig evidence.
  - reflection/probe ownership evidence.
  - shadow/contact evidence.
- Next concrete implementation should be `FSSP-V2-007A`, because the reported
  remaining issue is motion-dependent material/reflection/shadow instability.

## Full Scene Shader V2 Material-Aware Temporal Evidence - 2026-06-05

Implemented:

- Added `FullSceneTemporalEvidence` to
  `src/Graphics/FullSceneShaderFrameContext.h`.
- Added `BuildFullSceneTemporalEvidence`.
- V2 temporal evidence now reports:
  - motion-vector readiness.
  - visibility-buffer motion readiness.
  - previous-transform history readiness.
  - temporal rejection-mask readiness/stat readiness/latency readiness.
  - jitter reprojection readiness.
  - material-aware rejection readiness.
  - TAA history readiness.
  - TAA history velocity reprojection and disocclusion rejection readiness.
  - smooth-surface motion gate status.
  - camera-sweep gate status.
  - temporal-mask ratios and readback latency.
  - TAA history age and accumulation alpha.
  - missing temporal-contract count.
- Updated `src/Graphics/FrameContractJson.cpp` to emit those fields under
  `full_scene_shader_pipeline_v2.temporal`.
- Updated
  `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
  to require the new temporal fields.
- Updated `tools/check_full_scene_shader_pipeline_v2_frame_report.py` to
  require the temporal evidence builder and JSON fields.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_temporal_evidence_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_temporal_evidence_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.
- gallery beauty temporal evidence:
  - `missing_temporal_contract_count=0`.
  - `motion_vectors_ready=true`.
  - `visibility_buffer_motion_ready=true`.
  - `previous_transform_history_ready=true`.
  - `temporal_mask_ready=true`.
  - `temporal_mask_stats_ready=true`.
  - `temporal_mask_latency_ready=true`.
  - `temporal_mask_readback_latency_frames=3`.
  - `temporal_mask_accepted_ratio=1.0`.
  - `temporal_mask_high_motion_ratio=0.0`.
  - `temporal_mask_out_of_bounds_ratio=0.0`.
  - `taa_history_ready=true`.
  - `taa_history_velocity_reprojection_ready=true`.
  - `taa_history_disocclusion_rejection_ready=true`.
  - `taa_history_age_frames=0`.
  - `taa_history_accumulation_alpha=0.05999999865889549`.
  - `smooth_surface_motion_gate_passed=true`.
  - `camera_sweep_gate_passed=true`.
  - `temporal.domain_ready=true`.

Current interpretation:

- `FSSP-V2-007A` is packet-proved for the gallery target.
- V2 beauty remains `v1_fallback`.
- Next architecture slice should start semantic light buffers and V2
  direct-light shadow output.

## Full Scene Shader V2 Semantic Light Buffer Evidence - 2026-06-05

Implemented:

- Extended `FullSceneLightingRigEvidence` in
  `src/Graphics/FullSceneShaderFrameContext.h`.
- Added `FullSceneShaderPassReadsResource`.
- V2 lighting now reports:
  - shader light-array readiness.
  - semantic light-payload readiness.
  - area-light payload readiness.
  - clustered light-list readiness.
  - direct-light pass readiness.
  - direct-light shadow-output readiness.
  - point, spot, rect-area, and two-sided area-light counts.
- Updated `src/Graphics/FrameContractJson.cpp` to emit those fields under
  `full_scene_shader_pipeline_v2.lighting`.
- Updated
  `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`
  and `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake --build build --config Release --target CortexEngine --parallel 8
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_light_buffer_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_light_buffer_packet_20260605`.
- captured views: `13`.
- evidence rows: `130`.
- failures: `0`.
- gallery beauty lighting evidence:
  - `missing_lighting_contract_count=0`.
  - `shader_light_array_ready=true`.
  - `semantic_light_payload_ready=true`.
  - `area_light_payload_ready=true`.
  - `clustered_light_list_ready=true`.
  - `direct_light_pass_ready=true`.
  - `direct_light_shadow_output_ready=true`.
  - `point_light_count=1`.
  - `spot_light_count=3`.
  - `rect_area_light_count=2`.
  - `semantic_fixture_light_count=4`.
  - `shadow_casting_light_count=1`.
  - `lighting.domain_ready=true`.

Current interpretation:

- `FSSP-V2-004B` is packet-proved for the gallery target at the evidence
  layer.
- V2 beauty remains `v1_fallback`.
- Next shader-side lighting work should add a V2 direct-light shadow
  output/debug comparison path.

## Full Scene Shader V2 Direct-Light Shadow Comparison - 2026-06-05

Implemented:

- Added shader-side direct-light comparison surfaces in
  `assets/shaders/DeferredLighting.hlsl`.
  - `directLightUnshadowed` is accumulated beside the existing shadowed
    `directLight`.
  - local lights accumulate `localDirectUnshadowed` before local shadow factors.
  - debug mode `54` outputs unshadowed direct light.
  - debug mode `55` outputs direct-light shadow loss.
- Updated `src/Graphics/Renderer_DebugSettings.cpp`.
  - max debug mode is now `55`.
  - labels added:
    - `VB_DeferredDirectLightUnshadowed`.
    - `VB_DeferredDirectLightShadowLoss`.
- Updated packet surfaces:
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`.
  - `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
- Updated V2 frame-report contract/checker:
  - `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`.
  - `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
$env:CORTEX_SKIP_ASSET_SYNC='1'; cmake --build build --config Release --target CortexEngine --parallel 8
cmake -E copy_if_different assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_direct_light_shadow_compare_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Build note:

- Full asset sync hung in this repo again.
- Build was verified with `CORTEX_SKIP_ASSET_SYNC=1`.
- The edited `DeferredLighting.hlsl` was then copied explicitly to
  `build/bin/assets/shaders/DeferredLighting.hlsl` for runtime packet proof.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_direct_light_shadow_compare_packet_20260605`.
- captured views: `15`.
- evidence rows: `150`.
- failures: `0`.
- captured direct-light comparison views:
  - `direct_light`, debug view `44`.
  - `direct_light_unshadowed`, debug view `54`.
  - `direct_light_shadow_loss`, debug view `55`.

Current interpretation:

- V2 lighting now has shader-side direct-light/shadow comparison output, not
  just evidence fields.
- V2 beauty remains `v1_fallback`.
- Next options:
  - add packet delta metrics for direct-light shadow comparison.
  - or move to reflection-source resolver shadow-output if lighting comparison
    is enough for this slice.

## Full Scene Shader V2 Reflection Resolver Debug - 2026-06-05

Implemented:

- Added post-composite reflection resolver debug surfaces in
  `assets/shaders/PostProcess.hlsl`.
  - `reflection_source_weights`, debug view `56`:
    - R = SSR post-composite weight.
    - G = RT post-composite weight.
    - B = IBL/prelit reflection potential.
  - `reflection_stability_policy`, debug view `57`:
    - R = material reflectance.
    - G = gloss.
    - B = scene/material reflection stability scale.
- Updated `src/Graphics/Renderer_DebugSettings.cpp`.
  - max debug mode is now `57`.
  - labels added:
    - `PostReflectionSourceWeights`.
    - `PostReflectionStabilityPolicy`.
- Updated packet surfaces:
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`.
  - `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
- Updated V2 frame-report contract/checker:
  - `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`.
  - `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set CORTEX_SKIP_ASSET_SYNC=1 && cmake --build build --config Release --target CortexEngine --parallel 8 && cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl && cmake -E copy_if_different assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Build note:

- The first build attempt failed because the plain shell did not have the VS
  C++ include environment loaded.
- Build passed after loading:
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`.
- Full asset sync remains skipped for this pass; edited shaders were copied
  explicitly into `build/bin/assets/shaders`.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605`.
- captured views: `17`.
- evidence rows: `170`.
- failures: `0`.
- captured reflection resolver views:
  - `reflection_owner`, debug view `46`.
  - `reflection_source_weights`, debug view `56`.
  - `reflection_stability_policy`, debug view `57`.

Current interpretation:

- Smooth/metallic reflection behavior now has packet-visible source weights
  and material/stability policy evidence.
- V2 beauty remains `v1_fallback`.
- Next implementation should use these surfaces for numeric deltas or an
  opt-in reflection resolver candidate path instead of guessing from beauty
  captures alone.

## Full Scene Shader V2 Debug View Metrics - 2026-06-05

Implemented:

- Added `tools/analyze_full_scene_shader_debug_view_metrics.py`.
  - reads a packet manifest.
  - parses captured BMPs without external image dependencies.
  - emits per-view width, height, mean RGB, max RGB, mean/max luma, nonblack
    ratio, and hot-pixel ratio.
- Updated `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
  - integrated metrics outputs:
    - `debug_view_metrics.json`.
    - `debug_view_metrics.md`.
    - `debug_view_metrics_stdout.txt`.
- Updated `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.
  - requires the metrics analyzer and packet output names.

Validation:

```powershell
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605\manifest.json --output-json build\captures\full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605\debug_view_metrics.json --output-md build\captures\full_scene_shader_pipeline_v2_reflection_resolver_debug_packet_20260605\debug_view_metrics.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_debug_view_metrics_packet_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_debug_view_metrics.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_debug_view_metrics_packet_20260605`.
- captured views: `17`.
- evidence rows: `170`.
- frame-report failures: `0`.
- measured debug views: `17`.
- metric failures: `0`.
- key reflection metrics:
  - `reflection_source_weights` mean RGB:
    `0.000001, 0.069131, 0.060542`.
  - `reflection_source_weights` nonblack ratio: `0.322766`.
  - `reflection_stability_policy` mean RGB:
    `0.159872, 0.110794, 0.343249`.
  - `reflection_stability_policy` nonblack ratio: `1.0`.

Current interpretation:

- Debug packet outputs are now measurable artifacts, not only images.
- This gives future V2 candidate passes a cheap regression gate for source
  ownership and visual signal before subjective review.

## Full Scene Shader V2 Reflection Resolver Candidate - 2026-06-05

Implemented:

- Added an opt-in reflection resolver candidate path in
  `assets/shaders/PostProcess.hlsl`.
  - Default beauty remains the current resolver.
  - Candidate path uses stricter SSR admission via `stableSSRConfidence`.
  - Polished, mirror, water, and conductor surfaces keep a stronger RT handoff
    so SSR and RT do not fight as aggressively.
  - debug view `58`: `reflection_resolver_candidate`.
  - debug view `59`: `reflection_resolver_candidate_delta`.
- Updated `src/Graphics/Renderer_DebugSettings.cpp`.
  - max debug mode is now `59`.
  - labels added:
    - `PostReflectionResolverV2Candidate`.
    - `PostReflectionResolverV2CandidateDelta`.
- Updated packet surfaces:
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`.
  - `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
- Updated V2 frame-report contract/checker:
  - `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`.
  - `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_debug_view_metrics.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set CORTEX_SKIP_ASSET_SYNC=1 && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
cmake -E copy_if_different assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_candidate_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Build note:

- The first `cmake --build` wrapper command timed out with stale `cmake` and
  `ninja` child processes.
- Those orphaned build processes were stopped.
- Direct `ninja -C build CortexEngine -v` under Visual Studio 18 completed and
  linked the executable.

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_candidate_packet_20260605`.
- captured views: `19`.
- evidence rows: `190`.
- frame-report failures: `0`.
- measured debug views: `19`.
- metric failures: `0`.
- candidate views:
  - `reflection_resolver_candidate`, debug view `58`.
  - `reflection_resolver_candidate_delta`, debug view `59`.
- key candidate metrics:
  - `reflection_resolver_candidate` mean RGB:
    `0.620142, 0.591729, 0.550348`.
  - `reflection_resolver_candidate` nonblack ratio: `1.0`.
  - `reflection_resolver_candidate_delta` mean RGB:
    `0.000000055, 0.000000043, 0.000000034`.
  - `reflection_resolver_candidate_delta` nonblack ratio:
    `0.00000217`.

Current interpretation:

- The V2 reflection candidate path exists as a measurable opt-in path.
- It is intentionally not promoted to default beauty.
- Static gallery delta is near zero, so the next proof must be motion and
  cross-family comparison rather than claiming visual improvement from this
  slice.

### Reflection Candidate Mouse-Jitter Packet - 2026-06-05

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 3 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_candidate_mouse_jitter_packet_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_candidate_mouse_jitter_packet_20260605`.
- stability motion mode: `mouse_jitter`.
- capture sequence count: `3`.
- captured views: `19`.
- evidence rows: `190`.
- frame-report failures: `0`.
- measured debug views: `19`.
- metric failures: `0`.
- key motion metrics:
  - `reflection_resolver_candidate` mean RGB:
    `0.619970, 0.591707, 0.550361`.
  - `reflection_resolver_candidate_delta` mean RGB:
    `0.000000055, 0.000000043, 0.000000034`.
  - `reflection_resolver_candidate_delta` nonblack ratio:
    `0.00000217`.

Current interpretation:

- Candidate and delta views are stable enough to capture under mouse jitter.
- This is not visual-improvement proof because the tested gallery frame
  produces almost no candidate/current delta.
- Next proof should target cross-family or a reflection-stress scene.

## Full Scene Shader V2 Reflection Candidate Signal Audit - 2026-06-05

Implemented:

- Added `tools/analyze_full_scene_shader_reflection_candidate_signal.py`.
  - consumes packet `debug_view_metrics.json`.
  - audits whether `reflection_source_weights` has signal.
  - audits whether `reflection_resolver_candidate_delta` is meaningful.
  - emits:
    - `reflection_candidate_signal.json`.
    - `reflection_candidate_signal.md`.
- Updated `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
  - integrated the signal report after debug-view metrics.
- Updated `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.
  - requires the signal analyzer and packet output names.

Validation:

```powershell
python tools\analyze_full_scene_shader_reflection_candidate_signal.py --metrics build\captures\full_scene_shader_pipeline_v2_reflection_candidate_cross_family_mouse_jitter_packet_20260605\debug_view_metrics.json --output-json build\captures\full_scene_shader_pipeline_v2_reflection_candidate_cross_family_mouse_jitter_packet_20260605\reflection_candidate_signal.json --output-md build\captures\full_scene_shader_pipeline_v2_reflection_candidate_cross_family_mouse_jitter_packet_20260605\reflection_candidate_signal.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_candidate_signal_integrated_packet_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_reflection_candidate_signal.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Integrated packet evidence:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_candidate_signal_integrated_packet_20260605`.
- requested families: `5`.
- captured views: `30`.
- evidence rows: `300`.
- frame-report failures: `0`.
- measured debug views: `30`.
- metric failures: `0`.
- source-signal families: `1`.
- candidate-delta families: `0`.
- signal warnings: `5`.
- signal statuses:
  - `gallery`: `wired_no_delta`, source luma `0.05381363`,
    delta luma `0.00000004`.
  - `kitchen`: `no_reflection_source_signal`.
  - `office`: `no_reflection_source_signal`.
  - `gym`: `no_reflection_source_signal`.
  - `concert`: `no_reflection_source_signal`.

Current interpretation:

- Cross-family packet success is now separate from reflection-candidate
  usefulness.
- The next renderer architecture target is scene-local reflection source
  plumbing for model-authored families, because the post resolver currently has
  no source signal to improve in kitchen/office/gym/concert.

## 2026-06-05 Full-Scene Shader Refactor Planning Checkpoint

User direction:

- Move toward full-scene shaders and Unreal-like final visuals.
- Plan the whole refactor before completing the next goal feature.
- Do not restart scene-authoring automation or hand-polish screenshots.

Plan recorded in:

- `docs/FULL_SCENE_SHADER_REFACTOR_MASTER_PLAN.md`
  - section: `2026-06-05 AAA Full-Scene Shader Refactor Checkpoint`.

Key decision:

- The next slice should be `FSSP-V2-004C`: scene-local source plumbing.
- Do not continue with another isolated reflection resolver tweak.
- Reason: the reflection resolver candidate is wired, but cross-family signal
  audit showed:
  - gallery has reflection-source signal but near-zero candidate delta.
  - kitchen, office, gym, and concert have `no_reflection_source_signal`.
- Therefore richer full-scene shading is blocked by missing scene-local source
  data reaching shader/post paths, not by the resolver formula alone.

Refactor spine:

```text
SceneVisualContract
  -> FullSceneMaterialTable
  -> FullSceneFrameData / GBuffer
  -> FullSceneLightRig
  -> FullSceneProbeSet
  -> FullSceneLightingReflectionShadowComposite
  -> FullSceneTemporalResolve
  -> FullScenePost
  -> FullSceneRenderGraphEvidence
```

Implementation order now preferred:

1. Consolidate scene visual contract.
2. Plumb scene-local light/probe/reflection sources into shader-facing data.
3. Promote full material table into upload path.
4. Promote lighting V2 as shadow output.
5. Promote reflection V2 as shadow output.
6. Centralize shadow/contact policy.
7. Build material-aware temporal candidate.
8. Build HDR post V2 candidate.
9. Enforce V2 render graph ownership.
10. Run cross-family candidate promotion packets.

Next concrete work:

- Trace current model-authored family scene profiles and post constants to find
  why `reflection_source_weights` is zero outside gallery.
- Implement a scene-local source contract that can report local room probe,
  hero probe, planar probe, SSR, RT, neutral fallback, and authorized external
  environment source signal.
- Rerun cross-family packet:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_scene_local_source_plumbing_packet_20260605
```

Success condition for next slice:

- Source-signal families should increase from `1/5` to at least `4/5`.
- Candidate delta does not need to be visually large yet.
- Default beauty must remain V1/candidate-gated until source ownership,
  stability, and visual packet evidence pass.

## 2026-06-05 Scene-Local Source Plumbing Slice

Implemented:

- Added shader-facing scene-local probe radiance to post-process frame
  constants:
  - `FrameConstants::localProbeParams` in `src/Graphics/ShaderTypes.h`.
  - populated in `src/Graphics/Renderer_FramePostConstants.cpp`.
  - consumed as `g_LocalProbeParams` in `assets/shaders/PostProcess.hlsl`.
- Updated debug view `56` so blue reports authorized scene-local or
  IBL/prelit reflection potential.
- Updated debug view `46` so local scene probe ownership can light up before
  generic fallback ownership.
- Default beauty remains unchanged.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set CORTEX_SKIP_ASSET_SYNC=1 && ninja -C build CortexEngine -v'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_scene_local_source_plumbing_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- Build passed.
- Packet passed.
- `ctest` completed with `No tests were found`.
- Packet:
  `build/captures/full_scene_shader_pipeline_v2_scene_local_source_plumbing_packet_20260605`.
- Source-signal families improved from `1/5` to `5/5`.
- Candidate-delta families remain `0/5`.

Family source signal:

| Family | Status | Source Luma | Source Nonblack | Delta Luma |
|---|---|---:|---:|---:|
| gallery | `wired_no_delta` | `0.05381363` | `0.32276584` | `0.00000004` |
| kitchen | `wired_no_delta` | `0.00072311` | `0.17523763` | `0.00000000` |
| office | `wired_no_delta` | `0.00023052` | `0.05801107` | `0.00000000` |
| gym | `wired_no_delta` | `0.00029567` | `0.10023872` | `0.00000000` |
| concert | `wired_no_delta` | `0.00089806` | `0.16962348` | `0.00000000` |

Current interpretation:

- This proves the model-authored family source gap was real and is now bridged
  at the post evidence layer.
- It does not prove visible reflection improvement yet.
- Next slice should make the V2 reflection candidate actually consume the
  scene-local source term, still behind debug/candidate views, and then rerun
  mouse-jiggle/cross-family packets.

## 2026-06-05 Reflection Source Authority Debug View

Implemented:

- Added debug view `60`, `reflection_source_authority`.
- Channel contract:
  - R = authorized external IBL/prelit source potential.
  - G = scene-local probe source potential.
  - B = SSR/RT screen/ray source potential.
- Wired through:
  - `assets/shaders/PostProcess.hlsl`.
  - `src/Graphics/Renderer_DebugSettings.cpp`.
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`.
  - `tools/run_full_scene_shader_pipeline_v2_packet.ps1`.
  - `assets/final_art/full_scene_shader_pipeline_v2_frame_report_contract.json`.
  - `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\analyze_full_scene_shader_reflection_candidate_signal.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_reflection_source_authority_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- Build passed.
- Packet passed.
- `ctest` completed with `No tests were found`.
- Packet:
  `build/captures/full_scene_shader_pipeline_v2_reflection_source_authority_packet_20260605`.
- Source-signal families: `5/5`.
- Candidate-delta families: `0/5`.

Authority metrics:

| Family | Mean RGB | Nonblack |
|---|---:|---:|
| gallery | `0.0605,0.0156,0.0691` | `0.3228` |
| kitchen | `0.0000,0.0100,0.0000` | `0.1752` |
| office | `0.0000,0.0032,0.0000` | `0.0580` |
| gym | `0.0000,0.0041,0.0000` | `0.1002` |
| concert | `0.0000,0.0124,0.0000` | `0.1696` |

Interpretation:

- Enclosed model-authored families now prove green scene-local probe authority
  with zero external red authority.
- Gallery still shows external/RT authority, which is expected for that family.
- Next slice should consume local probe authority in a bounded candidate
  reflection resolver. Do not add another view before making candidate output
  move.

Build command warning:

- Use `set "CORTEX_SKIP_ASSET_SYNC=1"` with quotes in `cmd.exe`.
- The unquoted form can include a trailing space and miss the exact CMake env
  check, causing slow full asset sync.

## 2026-06-05 Local Probe Candidate Resolver Slice

Implemented:

- Added a candidate-only local probe sheen term in `PostProcess.hlsl`.
- It is gated by scene-local reflection potential, reflection stability scale,
  material reflection ceiling, and existing SSR/RT candidate weight.
- It changes only `reflection_resolver_candidate` and
  `reflection_resolver_candidate_delta`.
- Default beauty remains unchanged.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_local_probe_candidate_weighted_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- Packet:
  `build/captures/full_scene_shader_pipeline_v2_local_probe_candidate_weighted_packet_20260605`.
- Source-signal families: `5/5`.
- Candidate-delta families: `5/5`.
- Warnings: `0`.
- `ctest` completed with `No tests were found`.

Candidate deltas:

| Family | Status | Delta Luma | Delta Nonblack |
|---|---|---:|---:|
| gallery | `meaningful_delta` | `0.00343681` | `0.08734592` |
| kitchen | `meaningful_delta` | `0.00149478` | `0.08704644` |
| office | `meaningful_delta` | `0.00032100` | `0.00128472` |
| gym | `meaningful_delta` | `0.00052648` | `0.01372613` |
| concert | `meaningful_delta` | `0.00219636` | `0.11669922` |

Current interpretation:

- The candidate resolver now consumes local scene source authority across the
  full tested family set.
- This is a real architecture step beyond evidence-only plumbing.
- It remains approximate because post still does not bind actual room/hero
  probe radiance; it derives the candidate local term from already-lit scene
  color plus ambient scene-local floor.
- Next work should pass a real local-probe/source color or resolved local
  reflection radiance into post, then test stability under motion before any
  default beauty promotion.

## 2026-06-05 Post-Owned Local Probe Source Slice

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - declares the existing `space1` environment SRVs for post:
    `g_EnvDiffuse` and `g_EnvSpecular`.
  - adds post-local direction/mip helpers and a scene-local probe specular
    fallback matching the deferred lighting enclosed-scene palette.
  - routes the V2 candidate-only local probe sheen term through
    `SamplePostSceneLocalReflectionSource`.
  - keeps default beauty unchanged.

Why it matters:

- The previous candidate color was still inferred from already-lit scene color
  plus ambient, which was not a real source contract.
- The new candidate source is owned:
  - authorized external environment radiance only when `g_EnvParams`
    explicitly allows it.
  - scene-local room radiance otherwise, so enclosed scenes do not reintroduce
    HDRI/background bleed.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\analyze_full_scene_shader_reflection_candidate_signal.py
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 90 -CaptureFrame 45 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_packet_20260605
ctest --test-dir build --output-on-failure -C Release
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_packet_20260605`.
- source-signal families: `5/5`.
- candidate-delta families: `5/5`.
- warnings: `0`.
- `ctest` completed with `No tests were found`.

Candidate deltas:

| Family | Delta Luma | Delta Nonblack |
|---|---:|---:|
| gallery | `0.01109893` | `0.08724501` |
| kitchen | `0.00207694` | `0.09141168` |
| office | `0.00062022` | `0.01469184` |
| gym | `0.00067247` | `0.01252604` |
| concert | `0.00249549` | `0.12392687` |

Current next work:

- Do not promote V2 reflection beauty yet.
- Next useful slice is motion/stability proof on glossy/metal/glass surfaces,
  then either:
  - bind actual local probe textures into post when authorized, or
  - add a resolved local reflection radiance buffer before post.

### Camera-Sweep Motion Proof - 2026-06-05

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen,office,gym,concert" -ViewFilter "beauty,reflection_owner,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 120 -CaptureFrame 60 -CaptureSequenceCount 3 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_camera_sweep_packet_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_post_owned_local_probe_source_camera_sweep_packet_20260605`.
- source-signal families: `5/5`.
- candidate-delta families: `5/5`.
- warnings: `0`.
- failures: `0`.

Camera-sweep deltas:

| Family | Delta Luma | Delta Nonblack |
|---|---:|---:|
| gallery | `0.01119955` | `0.08748481` |
| kitchen | `0.00205583` | `0.08955838` |
| office | `0.00061088` | `0.01438151` |
| gym | `0.00063635` | `0.01116862` |
| concert | `0.00243640` | `0.12174154` |

Updated next work:

- The owned local probe candidate has now passed mouse-jitter and camera-sweep
  cross-family evidence packets as a debug/candidate output.
- Do not promote default beauty yet.
- Next gate should be explicit glossy/metal/glass close-up stability
  comparison, then either actual local probe texture binding in post or a
  resolved local reflection radiance buffer.

### Sequence Stability Analyzer Integration - 2026-06-05

Implemented:

- `tools/analyze_full_scene_shader_sequence_stability.py`
  - reads V2 packet manifests and each result's `capture_sequence`.
  - measures consecutive frame-to-frame mean absolute luma/RGB deltas.
  - compares `reflection_resolver_candidate` motion delta against `beauty`
    per family.
  - emits `sequence_stability.json` and `sequence_stability.md`.
- `tools/run_full_scene_shader_pipeline_v2_packet.ps1`
  - now runs sequence stability after debug metrics and reflection signal.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`
  - now requires the analyzer and packet outputs.

Validation:

```powershell
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_sequence_stability.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -FamilyFilter "gallery,kitchen" -ViewFilter "beauty,reflection_source_weights,reflection_source_authority,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_sequence_stability_integrated_smoke_20260605
```

Integrated smoke result:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_sequence_stability_integrated_smoke_20260605`.
- source-signal families: `2/2`.
- candidate-delta families: `2/2`.
- sequence warnings/failures: `0/0`.

| Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| gallery | `0.00311596` | `0.00310690` | `0.997` |
| kitchen | `0.00385527` | `0.00383711` | `0.995` |

Current next work:

- Run an explicit glossy/metal/glass close-up stress packet through the same
  sequence analyzer before any default-beauty promotion.
- Then choose between actual local probe texture binding in post and a
  resolved local reflection radiance buffer.

### Glossy Surface Stress Packet Harness - 2026-06-05

Implemented:

- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`
  - new `-StressSceneFilter` accepts comma-separated `scene:camera_bookmark`
    entries.
  - validates targets against `assets/config/showcase_scenes.json`.
  - records stress targets as packet families such as
    `stress_rt_showcase_reflection_closeup`.
  - new `-StressSceneOnly` runs just stress targets without the normal family
    set.
- `tools/run_full_scene_shader_pipeline_v2_packet.ps1`
  - forwards `-StressSceneFilter` and `-StressSceneOnly`.

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:metal_closeup,glass_water_courtyard:water_closeup" -ViewFilter "beauty,roughness,metallic,reflection_source_weights,reflection_source_authority,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_glossy_stress_only_smoke2_20260605
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\analyze_full_scene_shader_sequence_stability.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\analyze_full_scene_shader_debug_view_metrics.py tools\analyze_full_scene_shader_reflection_candidate_signal.py
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_glossy_stress_only_smoke2_20260605`.
- captured views: `21`.
- stress families: `3`.
- V2 packet evidence: passed.
- sequence stability warnings/failures: `0/0`.
- reflection candidate warnings/failures: `2/0`.

Stress stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `0.00158056` | `0.00158056` | `1.000` |
| `stress_material_lab_metal_closeup` | `0.00246855` | `0.00246855` | `1.000` |
| `stress_rt_showcase_reflection_closeup` | `0.00803787` | `0.00798941` | `0.994` |

Stress reflection signal:

| Stress Family | Status | Source Luma | Source Nonblack | Delta Luma | Delta Nonblack |
|---|---|---:|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `wired_no_delta` | `0.00024963` | `0.03081489` | `0.00000000` | `0.00000000` |
| `stress_material_lab_metal_closeup` | `wired_no_delta` | `0.00527105` | `0.17212348` | `0.00000000` | `0.00000000` |
| `stress_rt_showcase_reflection_closeup` | `meaningful_delta` | `0.12203094` | `0.41034071` | `0.02113077` | `0.17418837` |

Current stopping position:

- The V2 stress harness exists and passes on three glossy/material-heavy
  closeups.
- It proves motion stability, but also exposes that the current reflection
  candidate is visually inactive on metal lab and water courtyard closeups.
- Next work should make local reflection radiance materially active on these
  stress surfaces, preferably with a resolved local reflection radiance buffer
  or actual local probe texture binding in post.
- Do not call the V2 reflection path default-ready.

### Authorized Reflection Source Candidate Activation - 2026-06-05

Root cause fixed:

- `reflection_source_weights` counted authorized IBL/prelit source potential.
- The V2 candidate sheen gate only used `sceneLocalReflectionPotential`.
- As a result, `material_lab:metal_closeup` and
  `glass_water_courtyard:water_closeup` were wired but inactive because their
  valid stress source was authorized external/prelit radiance.

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - V2 candidate sheen gate now uses `authorizedPrelitReflectionPotential`.
  - `SamplePostSceneLocalReflectionSource` still enforces ownership:
    authorized external IBL only when allowed by `g_EnvParams`, otherwise
    scene-local radiance.
  - default beauty remains unchanged.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:metal_closeup,glass_water_courtyard:water_closeup" -ViewFilter "beauty,roughness,metallic,reflection_source_weights,reflection_source_authority,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_authorized_source_stress_smoke_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_authorized_source_stress_smoke_20260605`.
- source-signal families: `3/3`.
- candidate-delta families: `3/3`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.

After-patch candidate signal:

| Stress Family | Status | Delta Luma | Delta Nonblack |
|---|---|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `meaningful_delta` | `0.00047534` | `0.00544705` |
| `stress_material_lab_metal_closeup` | `meaningful_delta` | `0.01294731` | `0.09842122` |
| `stress_rt_showcase_reflection_closeup` | `meaningful_delta` | `0.02150589` | `0.17567708` |

After-patch stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_glass_water_courtyard_water_closeup` | `0.00158056` | `0.00158097` | `1.000` |
| `stress_material_lab_metal_closeup` | `0.00246855` | `0.00244604` | `0.991` |
| `stress_rt_showcase_reflection_closeup` | `0.00803785` | `0.00798630` | `0.994` |

Current stopping position:

- The immediate wired-but-inactive stress reflection bug is fixed.
- Do not promote V2 reflection default yet.
- Next shader gate should run a broader glossy stress packet:
  `material_lab:glass_emissive`,
  `glass_water_courtyard:glass_canopy`,
  `dragon_over_water:floor_reflection_closeup`, and
  `rt_showcase:reflection_closeup`.

### Broader Glossy Stress Gate - 2026-06-05

Validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive,glass_water_courtyard:glass_canopy,dragon_over_water:floor_reflection_closeup" -ViewFilter "beauty,roughness,metallic,surface_class,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605`.
- captured views: `36`.
- stress families: `4`.
- source-signal families: `4/4`.
- candidate-delta families: `4/4`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.

Candidate signal:

| Stress Family | Delta Luma | Delta Nonblack |
|---|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00955596` | `0.07552409` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00991124` | `0.16202257` |
| `stress_material_lab_glass_emissive` | `0.01555773` | `0.13664931` |
| `stress_rt_showcase_reflection_closeup` | `0.02144538` | `0.17576714` |

Sequence stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00292183` | `0.00289800` | `0.992` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00121205` | `0.00118411` | `0.977` |
| `stress_material_lab_glass_emissive` | `0.00152219` | `0.00149388` | `0.981` |
| `stress_rt_showcase_reflection_closeup` | `0.00487044` | `0.00480172` | `0.986` |

Current stopping position:

- Broader glossy/glass/water stress gate passes.
- The V2 reflection candidate is measurably active on all tested stress
  bookmarks and remains at or below beauty motion instability.
- Do not promote default beauty yet.
- Next work should produce a review packet/contact sheet comparing default
  beauty and V2 candidate on these stress views, then decide whether to expose
  an interactive runtime candidate toggle for user review.

### Stress Review Sheet Export - 2026-06-05

Implemented:

- `tools/build_full_scene_shader_v2_review_sheet.py`
  - reads V2 packet manifests.
  - groups rows by stress family/bookmark.
  - exports side-by-side columns for:
    `beauty`, `reflection_resolver_candidate`,
    `reflection_resolver_candidate_delta`, `reflection_source_authority`, and
    `reflection_source_weights`.
  - writes JSON and Markdown summaries.

Validation:

```powershell
python tools\build_full_scene_shader_v2_review_sheet.py --manifest build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\manifest.json --output build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\v2_stress_review_sheet.jpg --summary-json build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\v2_stress_review_sheet.json --summary-md build\captures\full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605\v2_stress_review_sheet.md
python -m py_compile tools\build_full_scene_shader_v2_review_sheet.py
```

Generated artifacts:

- `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605/v2_stress_review_sheet.jpg`.
- `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605/v2_stress_review_sheet.json`.
- `build/captures/full_scene_shader_pipeline_v2_broader_glossy_stress_packet_20260605/v2_stress_review_sheet.md`.

Review summary:

- rows: `4`.
- missing cells: `0`.
- visual sanity: sheet is readable; candidate changes are subtle but localized
  to glossy/reflection-relevant regions and supported by delta/authority/weight
  columns.

Current stopping position:

- V2 reflection candidate has metric and visual-review packet evidence.
- Do not promote default beauty yet.
- Next work should either:
  - add a runtime/P-menu candidate toggle for interactive user review, or
  - move from post candidate sheen to a resolved local reflection radiance
    buffer if we want stronger visual impact before exposing the toggle.

### Interactive Candidate Beauty Toggle - 2026-06-05

Implemented:

- Default-off renderer state:
  `RendererPostProcessState::v2ReflectionCandidateEnabled`.
- Renderer API:
  `SetV2ReflectionCandidateEnabled` and
  `IsV2ReflectionCandidateEnabled`.
- Runtime control enum:
  `RendererFeatureToggle::V2ReflectionCandidate`.
- P-menu checkbox:
  `V2 reflection candidate (review)`.
- Post bit:
  `g_BloomParams.w` bit `24`.
- Packet/env hook:
  `CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY=1`.
- Shader behavior:
  `PostProcess.hlsl` uses the candidate as beauty only when bit `24` is set
  or debug view `58` is active.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
$env:CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive" -ViewFilter "beauty,reflection_resolver_candidate,reflection_resolver_candidate_delta,reflection_source_weights,reflection_source_authority" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/full_scene_shader_pipeline_v2_candidate_beauty_toggle_smoke_20260605; Remove-Item Env:\CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY -ErrorAction SilentlyContinue
```

Results:

- packet:
  `build/captures/full_scene_shader_pipeline_v2_candidate_beauty_toggle_smoke_20260605`.
- source-signal families: `2/2`.
- candidate-delta families: `2/2`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.
- logs confirm the env hook drove candidate beauty.

Candidate-beauty signal:

| Stress Family | Delta Luma | Delta Nonblack |
|---|---:|---:|
| `stress_material_lab_glass_emissive` | `0.01586410` | `0.13685547` |
| `stress_rt_showcase_reflection_closeup` | `0.02374713` | `0.17570312` |

Current stopping position:

- User can now review V2 reflection candidate output from the P menu without
  editing commands.
- Default beauty remains unchanged unless the checkbox or env hook is enabled.
- Next work should be interactive visual review or a stronger resolved local
  reflection radiance buffer if the candidate is too subtle.

### Structured Scene-Local Reflection Candidate - 2026-06-05

Implemented:

- `assets/shaders/PostProcess.hlsl`
  - adds `ComputePostSceneLocalReflectionStructure`.
  - adds `ResolveV2SceneLocalReflectionRadiance`.
  - routes the V2 candidate local sheen through stable scene-local radiance
    structure instead of the previous one-source sheen.
  - adds broad architectural breakup, horizon/floor bounce, and key-light
    strips from reflection direction plus low-frequency world position.
  - remains candidate/review only: debug view `58` or the P-menu
    `V2 reflection candidate (review)` checkbox.
- Default beauty remains unchanged while the review toggle is off.

Validation:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "CORTEX_SKIP_ASSET_SYNC=1" && ninja -C build CortexEngine -v'
cmake -E copy_if_different assets\shaders\PostProcess.hlsl build\bin\assets\shaders\PostProcess.hlsl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive,glass_water_courtyard:glass_canopy,dragon_over_water:floor_reflection_closeup" -ViewFilter "beauty,reflection_source_weights,reflection_source_authority,reflection_stability_policy,reflection_resolver_candidate,reflection_resolver_candidate_delta" -SmokeFrames 80 -CaptureFrame 40 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_struct_refl_20260605
python tools\build_full_scene_shader_v2_review_sheet.py --manifest build\captures\v2_struct_refl_20260605\manifest.json --output build\captures\v2_struct_refl_20260605\v2_structured_reflection_review_sheet.jpg --summary-json build\captures\v2_struct_refl_20260605\v2_structured_reflection_review_sheet.json --summary-md build\captures\v2_struct_refl_20260605\v2_structured_reflection_review_sheet.md
$env:CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v2_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -FamilyFilter "gallery" -StressSceneFilter "rt_showcase:reflection_closeup,material_lab:glass_emissive" -ViewFilter "beauty,reflection_resolver_candidate,reflection_resolver_candidate_delta,reflection_source_weights,reflection_source_authority" -SmokeFrames 70 -CaptureFrame 35 -CaptureSequenceCount 2 -StabilityMotionMode camera_sweep -OutputRoot build/captures/v2_struct_refl_toggle_20260605; Remove-Item Env:\CORTEX_V2_REFLECTION_CANDIDATE_BEAUTY -ErrorAction SilentlyContinue
```

Results:

- packet:
  `build/captures/v2_struct_refl_20260605`.
- captured views: `24`.
- source-signal families: `4/4`.
- candidate-delta families: `4/4`.
- reflection candidate warnings/failures: `0/0`.
- sequence stability warnings/failures: `0/0`.
- review sheet:
  `build/captures/v2_struct_refl_20260605/v2_structured_reflection_review_sheet.jpg`.
- env/P-menu beauty-toggle path packet:
  `build/captures/v2_struct_refl_toggle_20260605`.
- beauty-toggle source-signal families: `2/2`.
- beauty-toggle candidate-delta families: `2/2`.
- beauty-toggle reflection candidate warnings/failures: `0/0`.
- beauty-toggle sequence stability warnings/failures: `0/0`.

Structured candidate improved delta on all four glossy stress bookmarks:

| Stress Family | Previous Delta Luma | Structured Delta Luma | Previous Delta Nonblack | Structured Delta Nonblack |
|---|---:|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00955596` | `0.01253093` | `0.07552409` | `0.07989692` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00991124` | `0.01315398` | `0.16202257` | `0.16984158` |
| `stress_material_lab_glass_emissive` | `0.01555773` | `0.01941374` | `0.13664931` | `0.13757161` |
| `stress_rt_showcase_reflection_closeup` | `0.02144538` | `0.02852098` | `0.17576714` | `0.18127604` |

Structured candidate stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00291844` | `0.00289316` | `0.991` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00121205` | `0.00117808` | `0.972` |
| `stress_material_lab_glass_emissive` | `0.00152219` | `0.00148692` | `0.977` |
| `stress_rt_showcase_reflection_closeup` | `0.00487044` | `0.00478890` | `0.983` |

Operational note:

- The same packet failed once under the longer output root
  `full_scene_shader_pipeline_v2_structured_reflection_candidate_20260605`
  because one dragon delta BMP path was too long to open. Shorter root passed.

Current stopping position:

- V2 reflection review is measurably stronger but still stable in the stress
  packet.
- Default beauty is still not changed.
- Next work should be either interactive user review through the checkbox or
  the first render-graph/local-radiance-buffer step that moves this beyond a
  post-only approximation.

### Local Reflection Radiance Buffer Kernel - 2026-06-05

Implemented:

- `assets/shaders/LocalReflectionRadiance.hlsl`
  - compute shader for the first render-graph-ready local reflection radiance
    buffer.
  - input SRVs:
    depth `t0`, normal/roughness `t1`, emissive/metallic `t2`,
    material ext1 `t3`, material ext2 `t4`, scene color `t5`, env specular `t6`.
  - output UAV:
    `g_OutputRadiance` `u0`, with `rgb = resolved local reflection radiance`
    and `a = confidence/admission weight`.
  - uses `SurfaceClassification.hlsli` so material-class ownership matches the
    post-process candidate.
  - reconstructs world position/reflection direction from `FrameConstants`.
  - resolves local architectural reflection structure, floor/horizon bounce,
    key-light strips, material-class boosts, local probe confidence, and
    authorized IBL contribution.
  - rejects sky/background and zero-confidence pixels.
  - soft-limits extreme radiance using the RT reflection firefly clamp.

Validation:

```powershell
build\vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe -T cs_6_3 -E CSMain -O3 -Qstrip_debug -I assets\shaders -Fo build\bin\assets\shaders\LocalReflectionRadiance.dxil assets\shaders\LocalReflectionRadiance.hlsl
```

Result:

- DXC compile passed.
- Generated:
  `build/bin/assets/shaders/LocalReflectionRadiance.dxil`.

Current stopping position:

- The local reflection radiance producer shader exists and compiles.
- It is not yet bound into default rendering or the V2 review toggle.
- Next pass should add the C++ resource/descriptor/render-graph dispatch
  plumbing and feed the produced radiance texture into the V2 post resolver
  behind the existing review toggle.

### Full Scene Shader Goal Feature Contract - 2026-06-05

User asked to plan the whole refactor before completing the goal feature.

Decision:

- The goal feature is not a prettier post shader or one stronger screenshot.
- The goal feature is a default-off `FullSceneShaderV2BeautyCandidate` assembled
  from owned runtime facts:
  `SceneVisualContract -> FullSceneMaterialTable -> FullSceneSourceBuffers ->
  LightingV2 -> LocalReflectionRadiance -> ShadowContactComposite ->
  MaterialAwareTemporal -> FullSceneHdrPost -> RenderGraphEvidence`.
- Do not promote default beauty until cross-family packets prove ownership,
  stability, and visual improvement.

Immediate next slice:

- `FSSP-V2-004C scene-local source plumbing`.
- Reserve/bind the local reflection radiance resource.
- Add shader-facing local source readiness.
- Expose source debug views/analyzer gates.
- Prove nonzero source signal across gallery plus at least three
  model-authored scenes.

Important guardrails:

- Do not start by tuning bloom, blur, IBL sharpness, or color grading.
- Do not turn off IBL, shadows, reflections, or temporal history to hide
  artifacts.
- Do not claim completion until V2 beauty can explain material, light,
  reflection, shadow, temporal, post, and render-graph ownership per packet.

### Reserved Post Local Reflection Radiance Slot - 2026-06-05

Implemented and validated the first C++ binding surface for the future
render-graph local reflection radiance producer:

- post-process SRV table widened to `14` slots.
- graphics root signature widened from `t0-t12` to `t0-t13`.
- `PostProcess.hlsl` declares `g_LocalReflectionRadiance : register(t13)`.
- debug view `61` is now reachable and labeled `LocalReflectionRadiance`.
- `local_reflection_radiance` is part of the V2 frame-report/debug-view
  contract and packet default view set.
- graph and non-graph post descriptor update paths bind slot `13` as a null
  `R16G16B16A16_FLOAT` SRV until the producer exists.

Important diagnosis:

- The first packet failed because the shader declared `t13` but the graphics
  root signature still exposed only `13` descriptors.
- After that fix, `local_reflection_radiance` initially matched
  `reflection_source_authority`; this was not descriptor aliasing. The renderer
  debug mode clamp was still capped at `60`, so requested view `61` was silently
  clamped to `60`.
- Raising `kMaxDebugViewMode` to `61` proved the actual slot: debug view `61`
  is black/nonblack `0.0` while view `60` still shows authority signal.

Validation evidence:

- build passed:
  `ninja -C build CortexEngine -v`.
- shader prep passed:
  post shader copied; `LocalReflectionRadiance.hlsl` DXC compile passed.
- validators passed:
  `python tools\check_full_scene_shader_pipeline_v2_frame_report.py`
  and `python tools\validate_full_scene_shader_pipeline_v2_plan.py`.
- packet passed:
  `build/captures/v2_local_radiance_slot_smoke4_20260605`.
- packet captured `6` views, emitted `60` V2 evidence rows, and had `0`
  failures.
- debug metrics:
  `local_reflection_radiance` luma `0.0000`, nonblack `0.0000`;
  `reflection_source_authority` luma `0.0661`, nonblack `0.4109`.
- `ctest --test-dir build --output-on-failure -C Release` found no registered
  tests in this build.

Next implementation pass:

- Allocate the local reflection radiance texture.
- Add SRV/UAV descriptors and resource state.
- Dispatch `LocalReflectionRadiance.hlsl` in the render graph.
- Bind the produced SRV into post slot `13`.
- Require debug view `61` to show nonzero producer-owned signal before feeding
  it into the V2 reflection candidate.

### Render-Graph Local Reflection Radiance Producer - 2026-06-05

Implemented the producer pass promised by the previous slot checkpoint:

- new `LocalReflectionRadiancePass` compute render-graph pass.
- transient `R16G16B16A16_FLOAT` local radiance target.
- per-frame local radiance SRV/UAV descriptor tables.
- local radiance compute pipeline compiled from
  `assets/shaders/LocalReflectionRadiance.hlsl`.
- render-graph end-frame inserts `LocalReflectionRadiance` before post when
  depth, GBuffer material channels, scene color, and compute descriptors are
  available.
- post resolves the graph-produced radiance resource at execution time and
  binds it to `t13`.

Validation:

- build:
  `ninja -C build CortexEngine -v` passed; a first build invocation timed out
  after tool timeout but the rerun reported `ninja: no work to do`.
- shader prep:
  post shader copy passed; `LocalReflectionRadiance.hlsl` DXC compile passed.
- packet:
  `build/captures/v2_local_radiance_producer_smoke1_20260605`.
- V2 packet evidence passed with `6` captured views.
- debug metrics:
  `local_reflection_radiance` luma `0.0950`, nonblack `1.0000`.
- this is distinct from source-authority:
  `reflection_source_authority` luma `0.0661`, nonblack `0.4109`.
- candidate signal remained valid:
  source luma `0.12212419`, delta luma `0.02840133`.
- validators passed:
  `check_full_scene_shader_pipeline_v2_frame_report.py`,
  `validate_full_scene_shader_pipeline_v2_plan.py`, and checker py_compile.
- `ctest` found no registered tests in this build.

Current stopping position:

- Local reflection radiance is now a real render-graph-owned producer signal,
  not a post-only approximation and not a null-slot proof.
- Default beauty is unchanged.
- Next slice should consume `g_LocalReflectionRadiance` in the V2 reflection
  candidate behind the existing P-menu review toggle, then rerun broad glossy
  stress packets before considering any promotion.

### Local Reflection Radiance Candidate Consumption - 2026-06-05

Implemented:

- `assets/shaders/PostProcess.hlsl` now blends the V2 reflection candidate
  toward `g_LocalReflectionRadiance` when the produced buffer alpha confidence
  is nonzero.
- Alpha `0` preserves the previous structured resolver fallback, so null or
  missing producer behavior remains compatible with the old candidate path.
- Default beauty remains unchanged unless debug view `58` or the P-menu
  `V2 reflection candidate (review)` toggle is active.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- post shader DXC compile passed; existing depth-load truncation warnings remain
  unchanged.
- local radiance compute shader DXC compile passed.
- focused packet passed:
  `build/captures/v2_local_radiance_candidate_consume_smoke1_20260605`.
  - `local_reflection_radiance` luma `0.0950`, nonblack `0.99999`.
  - `reflection_resolver_candidate_delta` luma `0.0281`, nonblack `0.1805`.
- broader glossy packet passed:
  `build/captures/v2_local_radiance_candidate_broader_glossy_20260605`.
  - source-signal families: `4/4`.
  - candidate-delta families: `4/4`.
  - warnings/failures: `0/0`.

Broader glossy candidate signal:

| Stress Family | Local Radiance Luma | Candidate Delta Luma |
|---|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.06236662` | `0.01245237` |
| `stress_glass_water_courtyard_glass_canopy` | `0.08687313` | `0.01305811` |
| `stress_material_lab_glass_emissive` | `0.09037495` | `0.01930018` |
| `stress_rt_showcase_reflection_closeup` | `0.09496863` | `0.02824227` |

Broader glossy sequence stability:

| Stress Family | Beauty Luma Delta | Candidate Luma Delta | Candidate/Beauty |
|---|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.00291844` | `0.00289495` | `0.992` |
| `stress_glass_water_courtyard_glass_canopy` | `0.00121205` | `0.00118124` | `0.975` |
| `stress_material_lab_glass_emissive` | `0.00152219` | `0.00149033` | `0.979` |
| `stress_rt_showcase_reflection_closeup` | `0.00487044` | `0.00478940` | `0.983` |

Current stopping position:

- V2 reflection candidate now consumes a render-graph-owned local radiance
  buffer.
- This is still candidate/review-only, not default beauty.
- Next safe work is a visual review/contact sheet for the produced-radiance
  candidate or the first semantic light-buffer/direct-light V2 shadow output.

### Direct-Light V2 Signal Gate - 2026-06-05

Implemented:

- `tools/analyze_full_scene_shader_lighting_signal.py`
  - audits `direct_light`, `direct_light_unshadowed`, and
    `direct_light_shadow_loss` from packet debug metrics.
  - writes `lighting_signal.json` and `lighting_signal.md`.
  - tolerates packets with no lighting views and fails incomplete lighting view
    sets.
- `tools/run_full_scene_shader_pipeline_v2_packet.ps1`
  - now runs the lighting signal analyzer after debug-view metrics.
- `tools/analyze_full_scene_shader_reflection_candidate_signal.py`
  - now tolerates lighting-only packets by skipping families with no reflection
    views.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`
  - now requires the lighting analyzer and packet artifacts as part of the V2
    packet harness surface.

Validation:

- py_compile passed for the new/edited analyzer and checker tools.
- V2 frame-report contract checker passed.
- focused lighting packet passed:
  `build/captures/v2_lighting_signal_gallery_smoke2_20260605`.
- broader gallery lighting packet passed:
  `build/captures/v2_lighting_signal_broader_gallery_20260605`.

Broader gallery lighting result:

- direct-signal families: `4/4`.
- shadow-loss families: `4/4`.
- warnings/failures: `0/0`.

| Stress Family | Direct Luma | Unshadowed Luma | Shadow Loss Luma |
|---|---:|---:|---:|
| `stress_dragon_over_water_floor_reflection_closeup` | `0.46242101` | `0.49161639` | `0.24304297` |
| `stress_glass_water_courtyard_glass_canopy` | `0.22994216` | `0.25624099` | `0.15694929` |
| `stress_material_lab_glass_emissive` | `0.36311143` | `0.39868422` | `0.13314867` |
| `stress_rt_showcase_reflection_closeup` | `0.42693366` | `0.45802755` | `0.22293851` |

Current stopping position:

- Direct-light and shadow-loss debug views are now packet-gated by a named V2
  analyzer.
- This creates the measured bridge for the next semantic light-buffer/direct
  light V2 shadow-output pass.
- Default beauty remains unchanged.

### Direct-Light Debug Ownership Contract - 2026-06-05

Implemented:

- `FullSceneLightingRigEvidence` now has explicit readiness fields for the
  direct-light debug outputs:
  - `directLightDebugViewReady`.
  - `directLightUnshadowedDebugViewReady`.
  - `directLightShadowLossDebugViewReady`.
- `FrameContractJson.cpp` serializes:
  - `direct_light_debug_view_ready`.
  - `direct_light_unshadowed_debug_view_ready`.
  - `direct_light_shadow_loss_debug_view_ready`.
- `full_scene_shader_pipeline_v2_frame_report_contract.json` and
  `check_full_scene_shader_pipeline_v2_frame_report.py` require the new fields.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V2 frame-report checker passed.
- V2 plan checker passed.
- focused packet passed:
  `build/captures/v2_lighting_debug_contract_smoke1_20260605`.

Focused packet evidence:

- direct-signal families: `1/1`.
- shadow-loss families: `1/1`.
- `direct_light` luma `0.42691390`.
- `direct_light_unshadowed` luma `0.45792174`.
- `direct_light_shadow_loss` luma `0.22298621`.
- generated frame report fields:
  - `direct_light_pass_ready=true`.
  - `direct_light_shadow_output_ready=true`.
  - `direct_light_debug_view_ready=true`.
  - `direct_light_unshadowed_debug_view_ready=true`.
  - `direct_light_shadow_loss_debug_view_ready=true`.
  - `missing_lighting_contract_count=0`.

Current stopping position:

- Direct-light V2 debug outputs are now owned in runtime evidence, required by
  the frame-report contract, and packet-gated by `lighting_signal.json`.
- Default beauty remains unchanged.
- Next safe slice: semantic light-buffer payloads or a named
  `FullSceneLightingV2` shadow-output resource.

### FullSceneLightingV2 Output Owner Contract - 2026-06-05

Implemented:

- `FullSceneLightingRigEvidence` now names the current V2 lighting output edge:
  - `lightingV2ShadowOutputReady`.
  - `lightingV2PassOwner`.
  - `lightingV2OutputResource`.
- Runtime JSON exposes:
  - `lighting_v2_shadow_output_ready`.
  - `lighting_v2_pass_owner`.
  - `lighting_v2_output_resource`.
- The current owner/resource is deliberately:
  `VBDeferredLighting -> hdr_color`.
- This gives the lighting domain a named output contract without claiming a
  separate lighting texture exists yet.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V2 frame-report checker passed.
- V2 plan checker passed.
- focused packet passed:
  `build/captures/v2_lighting_output_owner_smoke1_20260605`.
- strict frame-report validation passed on:
  `build/captures/v2_lighting_output_owner_smoke1_20260605/stress_rt_showcase_reflection_closeup/direct_light/frame_report_shutdown.json`.

Focused packet evidence:

- `lighting_v2_shadow_output_ready=true`.
- `lighting_v2_pass_owner=VBDeferredLighting`.
- `lighting_v2_output_resource=hdr_color`.
- direct-light/shadow-loss debug readiness fields all `true`.
- `missing_lighting_contract_count=0`.
- direct-signal families: `1/1`.
- shadow-loss families: `1/1`.
- `direct_light` luma `0.42686641`.
- `direct_light_unshadowed` luma `0.45795546`.
- `direct_light_shadow_loss` luma `0.22291435`.

Current stopping position:

- V2 lighting now has a named, frame-report-visible output owner contract.
- Default beauty remains unchanged.
- Next safe slice: split into a real `FullSceneLightingV2` resource or add
  semantic light-buffer payloads while preserving this contract.

### Semantic Light Payload Ownership Contract - 2026-06-05

Implemented:

- `FullSceneLightingRigEvidence` now exposes semantic light shader payload
  readiness/count/owner/channel fields.
- Runtime report names the current semantic payload owner as
  `FrameConstants.lights`.
- Runtime report names the current semantic payload lanes as
  `direction_cosInner.w_or_params.z`.
- The frame-report checker now verifies the new JSON fields and the
  shader-facing upload tokens in
  `Renderer_VisibilityBufferDeferredLighting.cpp`.
- Default beauty remains unchanged.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V2 frame-report checker passed.
- V2 plan checker passed.
- focused packet passed:
  `build/captures/v2_semantic_light_payload_smoke1_20260605`.
- strict frame-report validation passed on:
  `build/captures/v2_semantic_light_payload_smoke1_20260605/stress_rt_showcase_reflection_closeup/direct_light/frame_report_shutdown.json`.

Focused packet evidence:

- `semantic_light_payload_ready=true`.
- `semantic_light_shader_payload_ready=true`.
- `semantic_light_payload_count=4`.
- `semantic_light_payload_owner=FrameConstants.lights`.
- `semantic_light_payload_channels=direction_cosInner.w_or_params.z`.
- `shader_light_array_ready=true`.
- `semantic_fixture_light_count=4`.
- `lighting_v2_shadow_output_ready=true`.
- `missing_lighting_contract_count=0`.
- direct-signal families: `1/1`.
- shadow-loss families: `1/1`.
- `direct_light` luma `0.42689531`.
- `direct_light_unshadowed` luma `0.45797355`.
- `direct_light_shadow_loss` luma `0.22300819`.

Current stopping position:

- Scene-local semantic light intent is now connected to named shader payload
  lanes and packet-visible contract evidence.
- The next larger architectural move is the `FullSceneShaderPipelineV3`
  refactor plan: split material, lighting, reflection, environment, and
  cinematic post into explicit render-graph resources with validation gates.

### FullSceneShaderPipeline V3 Refactor Plan - 2026-06-05

Implemented:

- Added the V3 plan/ledger:
  `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.
- Added the machine-checkable V3 contract:
  `assets/final_art/full_scene_shader_pipeline_v3_contract.json`.
- Added the V3 plan validator:
  `tools/validate_full_scene_shader_pipeline_v3_plan.py`.

V3 target:

- move from isolated V2 signal slices to explicit render-graph resources for
  material, lighting, reflection, scene-local environment, HDR composite, and
  cinematic post.
- keep default beauty unchanged until each domain has a producer resource,
  consumer path, debug view, frame-report field, analyzer, packet evidence, and
  promotion decision.
- required cross-family proof remains gallery, kitchen, office, gym, concert,
  red_room, and stadium.

Current stopping position:

- V3 is planned and contract-grounded, not implemented or promoted.
- Next safe implementation slice is P0: add runtime frame-report placeholders
  for `full_scene_shader_pipeline_v3` and packet skeletons while preserving the
  current default beauty path.

### FullSceneShaderPipeline V3 Runtime Placeholder Contract - 2026-06-05

Implemented:

- Added `FullSceneShaderPipelineV3FrameContext` to
  `src/Graphics/FullSceneShaderFrameContext.h`.
- Added `FullSceneShaderPipelineV3ToJson` to
  `src/Graphics/FrameContractJson.cpp`.
- Runtime frame reports now expose `full_scene_shader_pipeline_v3`.
- V3 reports are explicitly `status=planned_not_promoted`.
- V3 reports expose `default_beauty_affects=false`.
- V3 domains are visible but not ready:
  render graph, material, lighting, reflection, environment, cinematic post,
  and validation.
- The V3 plan validator now checks both the plan/contract and the runtime
  placeholder surface.

Current stopping position:

- V3 has a frame-report-visible placeholder contract without touching beauty.
- Next safe slice is the V3 packet harness skeleton for required debug views and
  placeholder `v3_signal.json`/`v3_stability.json` artifacts.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V3 validator passed:
  `python tools\validate_full_scene_shader_pipeline_v3_plan.py`.
- V2 frame-report checker still passed:
  `python tools\check_full_scene_shader_pipeline_v2_frame_report.py`.
- focused V3 placeholder packet passed after rerunning with the complete
  reflection view set:
  `build/captures/v3_runtime_placeholder_smoke2_20260605`.
- strict V2 frame-report validation still passed on:
  `build/captures/v3_runtime_placeholder_smoke2_20260605/stress_rt_showcase_reflection_closeup/beauty/frame_report_shutdown.json`.

Extracted V3 frame-report evidence:

- `schema=cortex.full_scene_shader_pipeline_v3.runtime_report.v1`.
- `status=planned_not_promoted`.
- `beauty_output=full_scene_shader_pipeline_v2`.
- `default_beauty_affects=false`.
- `runtime_placeholders_ready=true`.
- `contract_grounded=true`.
- `packet_gate_ready=false`.
- `required_outputs=9`.
- domains are present and not ready:
  render_graph, material, lighting, reflection, environment, cinematic_post,
  and validation.

### FullSceneShaderPipeline V3 Packet Skeleton - 2026-06-05

Implemented:

- Added `tools/analyze_full_scene_shader_v3_placeholders.py`.
- Added `tools/run_full_scene_shader_pipeline_v3_packet.ps1`.
- The V3 packet skeleton reuses the V2 packet renderer for now, then scans all
  generated `frame_report_shutdown.json` files for V3 placeholder correctness.
- The skeleton emits:
  - `v3_signal.json`.
  - `v3_stability.json`.
- The analyzer requires:
  - V3 report schema is `cortex.full_scene_shader_pipeline_v3.runtime_report.v1`.
  - `status=planned_not_promoted`.
  - `default_beauty_affects=false`.
  - `runtime_placeholders_ready=true`.
  - `contract_grounded=true`.
  - `packet_gate_ready=false`.
  - all required V3 outputs and domains are present.

Current stopping position:

- V3 P0 now has plan, contract, runtime frame-report placeholders, and packet
  skeleton artifacts.
- Next implementation slice should turn the first domain from placeholder into
  real render-graph signal. The best first domain is material resolve:
  `FullSceneMaterialResolveV3 -> material_attributes`.

Validation:

- V3 validator passed:
  `python tools\validate_full_scene_shader_pipeline_v3_plan.py`.
- Python compile passed for:
  `tools\validate_full_scene_shader_pipeline_v3_plan.py` and
  `tools\analyze_full_scene_shader_v3_placeholders.py`.
- direct analyzer pass succeeded on:
  `build/captures/v3_runtime_placeholder_smoke2_20260605`.
  - reports scanned: `6`.
  - emitted `v3_signal.json`.
  - emitted `v3_stability.json`.
- wrapper packet passed:
  `build/captures/v3_packet_skeleton_smoke1_20260605`.
  - V2 packet evidence passed.
  - V3 placeholder analyzer passed.
  - reports scanned: `6`.
  - emitted `v3_signal.json`.
  - emitted `v3_stability.json`.

### FullSceneShaderPipeline V3 Material Attributes - 2026-06-05

Implemented:

- V3 material domain now reports a real aggregate output:
  `FullSceneMaterialResolveV3 -> material_attributes`.
- `material_attributes` is backed by the visibility-buffer material resolve
  resources:
  - `vb_gbuffer_albedo`.
  - `vb_gbuffer_normal_roughness`.
  - `vb_gbuffer_emissive_metallic`.
  - `vb_gbuffer_material_ext0`.
  - `vb_gbuffer_material_ext1`.
  - `vb_gbuffer_material_ext2`.
- V3 frame reports now expose:
  - `material_attributes_ready`.
  - `material_attributes_resource_count`.
  - `material_attributes_channel_count`.
  - per-domain `backing_resources`.
  - per-domain `debug_views`.
  - per-domain `channels`.
  - per-domain channel counts.
- The V3 analyzer permits `material` as the first ready domain while keeping
  all other domains placeholder-gated.
- Default beauty remains unchanged:
  `default_beauty_affects=false`.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V3 validator passed.
- V2 frame-report checker still passed.
- V3 wrapper packet passed:
  `build/captures/v3_material_attributes_smoke1_20260605`.
- extracted frame-report evidence:
  - `status=planned_not_promoted`.
  - `beauty_output=full_scene_shader_pipeline_v2`.
  - `default_beauty_affects=false`.
  - `material_attributes_ready=true`.
  - `material_attributes_resource_count=6`.
  - `material_attributes_channel_count=17`.
  - `material.ready=true`.
  - `material.producer=FullSceneMaterialResolveV3`.
  - `material.output_resource=material_attributes`.
  - `material.ready_channel_count=17`.
  - `material.missing_required_channel_count=0`.
- `v3_stability.json`:
  - `report_count=6`.
  - `default_beauty_affects_any=false`.
  - `promoted_report_count=0`.
  - `material_ready_report_count=6`.
  - failures `0`, warnings `0`.

Current stopping position:

- V3 material attributes are instrumented as the first real V3 domain.
- The next major refactor slice should start `FullSceneLightingV3`: split
  current deferred direct-light ownership into concrete V3 resources for
  direct lighting, unshadowed direct lighting, shadow visibility, and shadow
  loss.

### FullSceneShaderPipeline V3 Lighting Adapter - 2026-06-05

Implemented:

- Added honest V3 lighting adapter evidence for the current deferred lighting
  path.
- Current producer is `FullSceneLightingV3Adapter`.
- Current adapter owner/output is `VBDeferredLighting -> hdr_color`.
- V3 frame reports now expose:
  - `lighting_adapter_ready`.
  - `lighting_split_resources_ready`.
  - `lighting_adapter_signal_count`.
  - `lighting_split_resource_count`.
- Lighting domain reports adapter debug views:
  - `VB_DeferredDirectLight`.
  - `VB_DeferredDirectLightUnshadowed`.
  - `VB_DeferredDirectLightShadowLoss`.
  - `VB_DeferredShadowFactor`.
  - `VB_DeferredAmbientIBL`.
- Lighting domain intentionally remains `ready=false` until split V3 resources
  exist.
- Default beauty remains unchanged:
  `default_beauty_affects=false`.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V3 validator passed.
- V2 frame-report checker still passed.
- V3 lighting adapter packet passed:
  `build/captures/v3_lighting_adapter_smoke1_20260605`.
- extracted frame-report evidence:
  - `status=planned_not_promoted`.
  - `beauty_output=full_scene_shader_pipeline_v2`.
  - `default_beauty_affects=false`.
  - `lighting_adapter_ready=true`.
  - `lighting_split_resources_ready=false`.
  - `lighting_adapter_signal_count=4`.
  - `lighting_split_resource_count=0`.
  - `lighting.enabled=true`.
  - `lighting.ready=false`.
  - `lighting.producer=FullSceneLightingV3Adapter`.
  - `lighting.output_resource=hdr_color`.
  - `lighting.promotion_state=adapter`.
  - `lighting.ready_channel_count=5`.
  - `lighting.missing_required_channel_count=0`.
- `v3_stability.json`:
  - `report_count=6`.
  - `default_beauty_affects_any=false`.
  - `promoted_report_count=0`.
  - `lighting_adapter_ready_report_count=6`.
  - `lighting_split_ready_report_count=0`.
  - failures `0`, warnings `0`.
- `lighting_signal.json`:
  - direct-signal families `1/1`.
  - shadow-loss families `1/1`.
  - `direct_light` luma `0.42679371`.
  - `direct_light_unshadowed` luma `0.45791158`.
  - `direct_light_shadow_loss` luma `0.22303063`.

Current stopping position:

- V3 lighting has an honest adapter contract over the current deferred path.
- The next major refactor must allocate/split real V3 lighting resources:
  `direct_lighting`, `direct_lighting_unshadowed`, `shadow_visibility`,
  `shadow_loss`, and `indirect_lighting`.

### FullSceneShaderPipeline V3 Lighting Split Resource Scaffold - 2026-06-05

Implemented:

- Allocated five concrete V3 lighting split render targets:
  - `direct_lighting`.
  - `direct_lighting_unshadowed`.
  - `shadow_visibility`.
  - `shadow_loss`.
  - `indirect_lighting`.
- Added RTV and staging SRV descriptors for each split target.
- Added each split target to the frame-contract resource snapshot.
- Added V3 runtime report field:
  `lighting_split_resources_allocated`.
- Updated the V3 analyzer to require allocated split resources when the
  lighting adapter is ready.
- Kept the lighting domain honest:
  `lighting_split_resources_allocated=true` but
  `lighting_split_resources_ready=false` until a real `FullSceneLightingV3`
  pass writes the split targets.
- Default beauty remains unchanged:
  `default_beauty_affects=false`.

Touched files:

- `src/Graphics/RendererMainTargetState.h`.
- `src/Graphics/Renderer_HDRTargets.cpp`.
- `src/Graphics/Renderer_FrameContractSnapshot.cpp`.
- `src/Graphics/Renderer_Shutdown.cpp`.
- `src/Graphics/FullSceneShaderFrameContext.h`.
- `src/Graphics/FrameContractJson.cpp`.
- `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py`.
- `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V3 lighting split scaffold packet passed:
  `build/captures/v3_lighting_split_scaffold_smoke1_20260605`.
- extracted frame-report evidence:
  - `status=planned_not_promoted`.
  - `beauty_output=full_scene_shader_pipeline_v2`.
  - `default_beauty_affects=false`.
  - `lighting_adapter_ready=true`.
  - `lighting_split_resources_allocated=true`.
  - `lighting_split_resources_ready=false`.
  - `lighting_adapter_signal_count=4`.
  - `lighting_split_resource_count=5`.
  - `lighting.ready=false`.
  - `lighting.producer=FullSceneLightingV3Adapter`.
  - `lighting.output_resource=hdr_color`.
  - `lighting.promotion_state=adapter`.
  - `lighting.backing_resource_count=6`.
  - `lighting.backing_resources=hdr_color,direct_lighting,direct_lighting_unshadowed,shadow_visibility,shadow_loss,indirect_lighting`.
- `v3_stability.json`:
  - `report_count=6`.
  - `default_beauty_affects_any=false`.
  - `promoted_report_count=0`.
  - `lighting_adapter_ready_report_count=6`.
  - `lighting_split_allocated_report_count=6`.
  - `lighting_split_ready_report_count=0`.
  - failures `0`, warnings `0`.

Current stopping position:

- V3 material attributes are instrumented as the first real V3 domain.
- V3 lighting split resources are now allocated and contract-visible.
- V3 lighting is not producer-ready; the next major slice is a real
  `FullSceneLightingV3` pass that writes the five split resources, then proves
  ownership through frame-contract pass/resource evidence.

### FullSceneShaderPipeline V3 Lighting Split Producer - 2026-06-05

Implemented:

- Added an opt-in `FullSceneLightingV3` render-graph pass enabled by:
  `CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT=1`.
- The pass writes all five V3 lighting split resources:
  - `direct_lighting`.
  - `direct_lighting_unshadowed`.
  - `shadow_visibility`.
  - `shadow_loss`.
  - `indirect_lighting`.
- The first producer implementation reuses the existing deferred lighting term
  debug paths and writes the five targets with five full-screen draws.
- This is producer proof and resource ownership scaffolding, not the final
  single-pass HDR split-lighting architecture.
- Normal/default gameplay does not run the extra split pass unless the opt-in
  env flag is present.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now enables the flag
  around packet capture and restores the caller's environment afterward.
- V3 runtime report now promotes lighting from adapter evidence to producer
  evidence when frame-contract pass writes prove all five split resources:
  - producer: `FullSceneLightingV3`.
  - output resource: `lighting_split`.
  - promotion state: `producer`.
  - ready: `true`.
- Default beauty remains unchanged:
  `default_beauty_affects=false`.

Touched files:

- `src/Graphics/VisibilityBuffer.h`.
- `src/Graphics/VisibilityBuffer_DeferredLighting.cpp`.
- `src/Graphics/Passes/VisibilityBufferGraphPass.h`.
- `src/Graphics/Passes/VisibilityBufferGraphPass.cpp`.
- `src/Graphics/Renderer_RenderGraphVisibilityBufferHelpers.h`.
- `src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp`.
- `src/Graphics/Renderer_FrameContractPasses.cpp`.
- `src/Graphics/FullSceneShaderFrameContext.h`.
- `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1`.
- `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.

Validation:

- build passed:
  `ninja -C build CortexEngine -v`.
- V3 lighting split producer packet passed:
  `build/captures/v3_lighting_split_producer_smoke1_20260605`.
- extracted frame-report evidence:
  - `lighting_split_resources_allocated=true`.
  - `lighting_split_resources_ready=true`.
  - `lighting_split_resource_count=5`.
  - `lighting.ready=true`.
  - `lighting.producer=FullSceneLightingV3`.
  - `lighting.output_resource=lighting_split`.
  - `lighting.promotion_state=producer`.
  - `FullSceneLightingV3.pass_executed=true`.
  - `FullSceneLightingV3.pass_draw_count=5`.
  - `FullSceneLightingV3.pass_writes=direct_lighting,direct_lighting_unshadowed,shadow_visibility,shadow_loss,indirect_lighting`.
- `v3_stability.json`:
  - `report_count=6`.
  - `default_beauty_affects_any=false`.
  - `promoted_report_count=0`.
  - `lighting_adapter_ready_report_count=6`.
  - `lighting_split_allocated_report_count=6`.
  - `lighting_split_ready_report_count=6`.
  - failures `0`, warnings `0`.

Current stopping position:

- V3 material is producer-ready through `FullSceneMaterialResolveV3`.
- V3 lighting is producer-ready under the opt-in split flag through
  `FullSceneLightingV3`.
- The next major renderer slice should replace the five debug-term redraws with
  a direct split-output lighting shader/pass, then add lighting signal gates
  for direct, indirect, shadow visibility, and shadow loss.

### FullSceneShaderPipeline V3 Direct MRT Lighting Split - 2026-06-05

Implemented:

- Replaced the first `FullSceneLightingV3` producer's five deferred debug-term
  redraws with a direct split-output MRT shader path.
- Added `PSMainV3LightingSplit` in `assets/shaders/DeferredLighting.hlsl`.
- Added `m_fullSceneLightingV3Pipeline` and a five-RTV fullscreen graphics PSO.
- `ApplyFullSceneLightingV3` now binds all five split render targets and draws
  one fullscreen triangle.
- Frame-contract pass evidence now reports `FullSceneLightingV3.draw_count=1`.
- Diagnosed an asset-sync/runtime-copy trap: the source shader contained the new
  entry point, but the runtime copy under `build/bin/assets/shaders` was stale.
  The runtime shader was explicitly synced before the successful packet.
- Tightened the V3 packet gate so split packets now require
  `lighting_split_resources_ready=true`, `FullSceneLightingV3` pass evidence,
  all five split writes, and `FullSceneLightingV3.draw_count=1`.

Touched files:

- `assets/shaders/DeferredLighting.hlsl`.
- `src/Graphics/VisibilityBuffer.h`.
- `src/Graphics/VisibilityBuffer_DeferredLighting.cpp`.
- `src/Graphics/VisibilityBuffer_DeferredLightingPipeline.cpp`.
- `src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp`.
- `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1`.
- `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.
- `docs/AAA_ASSET_QUALITY_HANDOFF.md`.

Important diagnosis:

- First packet attempt:
  `build/captures/v3_lighting_split_mrt_smoke1_20260605`.
- It passed the loose wrapper, but runtime evidence was not acceptable:
  `lighting_split_resources_ready=false`.
- Runtime log root cause:
  `Failed to compile FullSceneLightingV3 PS: DXC shader compilation failed: error: missing entry point definition`.
- The source shader had `PSMainV3LightingSplit`, but
  `build/bin/assets/shaders/DeferredLighting.hlsl` was stale because a previous
  build used `CORTEX_SKIP_ASSET_SYNC=1` and still touched the asset stamp.
- After syncing the runtime shader, the same packet succeeded with real producer
  evidence.
- Strict analyzer replay now rejects the stale packet with:
  `V3 split packet requires lighting_split_resources_ready=true` and
  `V3 split packet requires FullSceneLightingV3 pass evidence`.

Validation:

- clean target rebuild passed after removing corrupt generated `.obj` files left
  by the interrupted build:
  `ninja -C build CortexEngine -j 4 -v`.
- successful V3 MRT packet:
  `build/captures/v3_lighting_split_mrt_smoke3_strict_20260605`.
- extracted frame-report evidence:
  - `lighting_split_resources_allocated=true`.
  - `lighting_split_resources_ready=true`.
  - `lighting_adapter_ready=true`.
  - `lighting_adapter_signal_count=4`.
  - `lighting_split_resource_count=5`.
  - ready domains include `lighting` and `material`.
  - `FullSceneLightingV3.executed=true`.
  - `FullSceneLightingV3.draw_count=1`.
  - `FullSceneLightingV3.writes=direct_lighting,direct_lighting_unshadowed,shadow_visibility,shadow_loss,indirect_lighting`.
- `v3_stability.json`:
  - `report_count=6`.
  - `default_beauty_affects_any=false`.
  - `promoted_report_count=0`.
  - `lighting_adapter_ready_report_count=6`.
  - `lighting_split_allocated_report_count=6`.
  - `lighting_split_ready_report_count=6`.
  - `full_scene_lighting_v3_executed_report_count=6`.
  - failures `0`, warnings `0`.
- validators:
  - `python tools/validate_full_scene_shader_pipeline_v3_plan.py`: passed.
  - `python tools/check_full_scene_shader_pipeline_v2_frame_report.py`: passed.
  - `python -m py_compile tools/validate_full_scene_shader_pipeline_v3_plan.py tools/analyze_full_scene_shader_v3_placeholders.py`: passed.
  - `ctest --test-dir build --output-on-failure -C Release`: no tests found.

Current stopping position:

- V3 lighting split now has a real direct MRT producer under the opt-in split
  flag.
- Default beauty remains V2 and unchanged.
- Next safe slice:
  - add direct signal gates over the five split resources.
  - close visual parity gaps between `PSMainV3LightingSplit` and the default
    deferred beauty shader before any promotion attempt.

### FullSceneShaderPipeline V3 Lighting Signal Gate - 2026-06-05

Implemented:

- Added lighting signal metric gates to
  `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now requires:
  - `lighting_split_resources_ready=true`.
  - `FullSceneLightingV3.draw_count=1`.
  - all five split resource writes.
  - nonblank/coherent lighting debug-view metrics for:
    `direct_light`, `direct_light_unshadowed`,
    `direct_light_shadow_loss`, `shadow_factor`, and `ambient_ibl`.
- The signal gates are intentionally conservative. They catch blank/dead terms
  and basic incoherence, but do not yet prove full visual parity with default
  V2 beauty.

Touched files:

- `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1`.
- `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.
- `docs/AAA_ASSET_QUALITY_HANDOFF.md`.

Validation:

- fresh packet:
  `build/captures/v3_lighting_split_signal_gate_smoke1_20260605`.
- `v3_stability.json`:
  - `report_count=6`.
  - `default_beauty_affects_any=false`.
  - `promoted_report_count=0`.
  - `lighting_adapter_ready_report_count=6`.
  - `lighting_split_allocated_report_count=6`.
  - `lighting_split_ready_report_count=6`.
  - `full_scene_lighting_v3_executed_report_count=6`.
  - `lighting_signal_metrics_ready=true`.
  - failures `0`, warnings `0`.
- signal metrics:
  - `direct_light.mean_luma=0.426794`, `nonblack_ratio=1.0`.
  - `direct_light_unshadowed.mean_luma=0.457842`, `nonblack_ratio=1.0`.
  - `direct_light_shadow_loss.mean_luma=0.223022`, `nonblack_ratio=1.0`.
  - `shadow_factor.mean_luma=0.350937`, `nonblack_ratio=1.0`.
  - `ambient_ibl.mean_luma=0.196339`, `nonblack_ratio=1.0`.
- validators:
  - `python tools/validate_full_scene_shader_pipeline_v3_plan.py`: passed.
  - `python tools/check_full_scene_shader_pipeline_v2_frame_report.py`: passed.

Current stopping position:

- V3 lighting now has ownership, one-draw producer, and nonblank signal gates.
- The gate still measures the existing lighting debug-view capture surface.
- Next safe slice:
  - add explicit debug views that sample the actual V3 MRT split resources.
  - then compare those concrete split outputs against the legacy debug terms and
    close parity gaps in `PSMainV3LightingSplit`.

### FullSceneShaderPipeline V3 Concrete Split Debug Views - 2026-06-05

Implemented in the current working tree:

- Added a generic `VisibilityBufferRenderer::DebugBlitTextureSRVToHDR` helper.
  - It copies a staging SRV descriptor into a transient shader-visible SRV and
    uses the existing fullscreen blit pipeline.
- Added render-graph support for a post-V3 external-SRV debug blit.
  - Early `VBDebugBlit` remains for visibility/depth/GBuffer debug modes.
  - V3 lighting debug modes now run material resolve, deferred lighting, and
    `FullSceneLightingV3`, then blit the selected concrete split resource to
    HDR through `FullSceneLightingV3DebugBlit`.
- Added debug modes:
  - `62`: `VB_V3DirectLighting`.
  - `63`: `VB_V3DirectLightingUnshadowed`.
  - `64`: `VB_V3ShadowVisibility`.
  - `65`: `VB_V3ShadowLoss`.
  - `66`: `VB_V3IndirectLighting`.
- Added packet view names:
  - `v3_direct_lighting`.
  - `v3_direct_lighting_unshadowed`.
  - `v3_shadow_visibility`.
  - `v3_shadow_loss`.
  - `v3_indirect_lighting`.
- Tightened the V3 analyzer's lighting signal metrics to require concrete V3
  split-buffer metrics in addition to legacy deferred lighting metrics.
- Updated V2/V3 validators for the expanded debug mode range.
- Updated `docs/FULL_SCENE_SHADER_PIPELINE_V3.md` L005 with the concrete split
  debug-view contract.

Focused files touched:

- `src/Graphics/VisibilityBuffer.h`.
- `src/Graphics/VisibilityBuffer_DebugBlit.cpp`.
- `src/Graphics/Passes/VisibilityBufferGraphPass.h`.
- `src/Graphics/Passes/VisibilityBufferGraphPass.cpp`.
- `src/Graphics/Renderer_RenderGraphVisibilityBufferHelpers.h`.
- `src/Graphics/Renderer_RenderGraphVisibilityBuffer.cpp`.
- `src/Graphics/Renderer_VisibilityBufferCulling.cpp`.
- `src/Graphics/Renderer_DebugSettings.cpp`.
- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1`.
- `tools/analyze_full_scene_shader_v3_placeholders.py`.
- `tools/check_full_scene_shader_pipeline_v2_frame_report.py`.
- `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.
- `docs/AAA_ASSET_QUALITY_HANDOFF.md`.

Validation completed:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\check_full_scene_shader_pipeline_v2_frame_report.py
git -c submodule.recurse=false diff --check -- src\Graphics\VisibilityBuffer.h src\Graphics\VisibilityBuffer_DebugBlit.cpp src\Graphics\Passes\VisibilityBufferGraphPass.h src\Graphics\Passes\VisibilityBufferGraphPass.cpp src\Graphics\Renderer_RenderGraphVisibilityBuffer.cpp src\Graphics\Renderer_RenderGraphVisibilityBufferHelpers.h src\Graphics\Renderer_VisibilityBufferCulling.cpp src\Graphics\Renderer_DebugSettings.cpp tools\run_scene_local_cinematic_renderer_v1_packets.ps1 tools\run_full_scene_shader_pipeline_v3_packet.ps1 tools\analyze_full_scene_shader_v3_placeholders.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
cl /Zs src\Graphics\Passes\VisibilityBufferGraphPass.cpp
cl /Zs src\Graphics\Renderer_RenderGraphVisibilityBuffer.cpp
cl /Zs src\Graphics\Renderer_VisibilityBufferCulling.cpp
cl /Zs src\Graphics\Renderer_DebugSettings.cpp
cl /Zs src\Graphics\VisibilityBuffer_DebugBlit.cpp
```

Validation result:

- Python compile passed.
- V3 plan validator passed.
- V2 frame-report validator passed.
- `git diff --check` passed.
- Direct MSVC syntax checks passed for the touched C++ translation units above.

Native build status:

- Attempted full `ninja -C build CortexEngine` and focused touched-object
  builds after `VsDevCmd`.
- Both attempts timed out and left a zero-CPU stale `ninja` wrapper with no
  active `cl`/`link` process.
- Stale wrappers were killed.
- Treat full native link/build and V3 packet capture as pending, not passed.

Next safe pass:

1. Restore/diagnose the local Ninja build wrapper enough to complete a native
   build.
2. Run:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -OutputRoot build/captures/v3_lighting_split_concrete_debug_smoke1_20260605 -SmokeFrames 30 -CaptureFrame 15 -CaptureSequenceCount 1`.
3. Inspect `debug_view_metrics.json` and `v3_signal.json` for the five concrete
   V3 split views.
4. If the V3 split views pass, commit and push this focused checkpoint.
5. If they fail, fix `PSMainV3LightingSplit` or the debug-blit/resource-state
   path before moving to Reflection V3.

### FullSceneShaderPipeline V3 Concrete Split Packet Verified - 2026-06-05

Build blocker diagnosis:

- The earlier Ninja "hang" was not a C++ compiler failure.
- Root causes found:
  - multiple stale `cmake -P tools/sync_assets.cmake` processes survived from
    timed-out asset-sync attempts.
  - `CortexAssets` was dirty because `.ninja_log` had no command-line entry for
    `cortex_assets.stamp`.
  - launching Ninja outside `VsDevCmd` produced the misleading MSVC standard
    library error: `fatal error C1083: Cannot open include file: 'string'`.
- Cleanup/fix:
  - killed only the stale CortexEngine asset-sync `cmake` processes.
  - left unrelated T6 capture PowerShell processes untouched.
  - ran `CortexAssets` once with `CORTEX_SKIP_ASSET_SYNC=1` to populate the
    Ninja command log without copying the large generated asset tree.
  - rebuilt inside the Visual Studio environment.

Successful build:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set CORTEX_SKIP_ASSET_SYNC=1 && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
```

Result:

- `build/bin/CortexEngine.exe` linked successfully.
- Existing warnings remain, mostly unused parameters/internal-linkage warnings
  in unrelated files and third-party headers.

Concrete split packet:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -OutputRoot build/captures/v3_lighting_split_concrete_debug_smoke1_20260605 -SmokeFrames 30 -CaptureFrame 15 -CaptureSequenceCount 1
```

Packet result:

- Scene-local cinematic packet passed.
- V2 packet evidence passed.
- V3 placeholder/concrete split evidence passed.
- `reports=16`.
- `v3_signal.json`:
  `build/captures/v3_lighting_split_concrete_debug_smoke1_20260605/v3_signal.json`.
- `v3_stability.json`:
  `build/captures/v3_lighting_split_concrete_debug_smoke1_20260605/v3_stability.json`.

`v3_stability.json` evidence:

- `report_count=16`.
- `default_beauty_affects_any=false`.
- `promoted_report_count=0`.
- `material_ready_report_count=16`.
- `lighting_adapter_ready_report_count=16`.
- `lighting_split_allocated_report_count=16`.
- `lighting_split_ready_report_count=16`.
- `full_scene_lighting_v3_executed_report_count=16`.
- `lighting_signal_metrics_ready=true`.
- failures `0`, warnings `0`.

Concrete V3 split metrics from `v3_signal.json`:

- `v3_direct_lighting.mean_luma=0.431061`, `nonblack_ratio=1.0`.
- `v3_direct_lighting_unshadowed.mean_luma=0.470903`,
  `nonblack_ratio=1.0`.
- `v3_shadow_visibility.mean_luma=0.350934`, `nonblack_ratio=1.0`.
- `v3_shadow_loss.mean_luma=0.175254`, `nonblack_ratio=1.0`.
- `v3_indirect_lighting.mean_luma=0.193502`, `nonblack_ratio=1.0`.

Legacy comparison metrics from the same packet:

- `direct_light.mean_luma=0.426833`.
- `direct_light_unshadowed.mean_luma=0.457904`.
- `direct_light_shadow_loss.mean_luma=0.222995`.
- `shadow_factor.mean_luma=0.350900`.
- `ambient_ibl.mean_luma=0.196344`.

Current stopping position:

- The concrete V3 split debug-view slice is now built and packet-verified.
- V3 direct and unshadowed lighting are close to legacy luma in the static
  gallery packet.
- V3 shadow visibility is close to legacy shadow factor in the static gallery
  packet.
- V3 shadow loss is lower than the legacy shadow-loss debug view but passes the
  current conservative nonblank/coherence gate.
- V3 indirect is close to legacy ambient IBL mean luma but has a higher hot
  pixel ratio; this should be tracked during reflection/environment V3 work.

Next safe pass:

1. Add motion stability gates for the five concrete V3 split buffers.
2. Add cross-family packets for at least kitchen, office, gym, and concert V3
   split debug views.
3. Begin `FullSceneReflectionV3` scaffolding only after split lighting remains
   stable under motion.

### FullSceneShaderPipeline V3 Promotion Gate / Reflection Temporal Contract - 2026-06-05

Current slice:

- finishing the V3 promotion-decision gate and making it useful across
  non-gallery scene families.
- target artifact:
  `tools/build_full_scene_shader_v3_promotion_decision.py`.
- focused packet under diagnosis:
  `build/captures/v3_promotion_decision_gate_smoke1_20260605`.

Diagnosis from the kitchen packet:

- kitchen has `scene_local_environment_ready=true`.
- kitchen has a ready local reflection probe:
  `local_reflection_probe_count=1`,
  `local_reflection_probe_table_valid=true`,
  `local_reflection_probe_radiance_enabled=true`.
- kitchen reflection source contract chooses `local_probe`.
- previous `FullSceneReflectionV3` still failed because
  `reflection_temporal_delta_ready=false`.
- that blocked `reflection_v3_ready`, then `composite_v3_ready`, then
  `cinematic_post_v3_ready`.

Root contract fix:

- `src/Graphics/FullSceneShaderFrameContext.h` now treats reflection temporal
  delta ownership as source-aware.
- scene-local probe/environment reflection sources can own
  `reflection_temporal_delta_scene_local_bound` without RT reflection history.
- RT/SSR or other history-sensitive paths still require
  `reflection_temporal_delta_history_bound` through reflection history or TAA
  history evidence.
- this is not a scene workaround and does not change default beauty.

Validation still pending for this slice:

- V3 plan validator passed after updating the runtime-surface token check for
  `reflection_temporal_delta_scene_local_bound` and
  `reflection_temporal_delta_history_bound`.
- focused object compile passed for
  `CMakeFiles\CortexEngine.dir\src\Graphics\FrameContractJson.cpp.obj`.
- full `CortexEngine` build passed after using the correctly quoted skip:
  `set "CORTEX_SKIP_ASSET_SYNC=1"`.
- packet attempts:
  - `build/captures/v3_promotion_decision_gate_smoke2_20260605`
  - `build/captures/v3_promotion_decision_gate_smoke3_20260605`
- both packet attempts hit a DX12 device removal in one kitchen row, so the
  promotion decision could not be treated as a clean packet pass.
- however, the new V3 frame-report summaries prove the contract fix:
  - smoke2: `report_count=32`, reflection `32/32`, composite `32/32`,
    cinematic post `31/32`, temporal channel
    `reflection_temporal_delta_scene_local_bound=32`.
  - smoke3: `report_count=30`, reflection `30/30`, composite `30/30`,
    cinematic post `29/30`, temporal channel
    `reflection_temporal_delta_scene_local_bound=30`.
- remaining blocker is now renderer/GPU stability in the kitchen packet path,
  not the old `ReflectionV3` temporal-delta contract.

Next safe pass:

1. Diagnose the kitchen DX12 device removal:
   `lastGpuMarker='MotionVectors'`, DRED page fault `GPU VA=0x0`, failing rows
   under `v3_promotion_decision_gate_smoke2_20260605/kitchen/shadow_factor`
   and `v3_promotion_decision_gate_smoke3_20260605/kitchen/beauty`.
2. Once the kitchen packet stops device-hanging, rerun the V3 promotion packet
   and require the decision to move from `blocked` to at least
   `review_packet_passed` with subset warnings.
3. Do not weaken the promotion gate to ignore failed packet rows; use the new
   frame-report summarizer only as diagnosis evidence during GPU faults.

### Full Scene Shader AAA Refactor Direction - 2026-06-05

Planning document:

- `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`

Current direction:

- stop treating shader quality as isolated fixes.
- keep V3 as the proof harness and bridge.
- build an opt-in `FullSceneCandidateBeautyV3` path before any default beauty
  promotion.
- every major visual feature must have a named producer resource, debug view,
  frame-report field, and packet gate.
- renderer stability remains phase zero; the kitchen device-removal issue must
  be fixed before any visual packet can be trusted as promotion evidence.

Refactor phases:

1. stabilize motion-vector/kitchen packet and failure reporting.
2. add candidate beauty switch and side-by-side capture support.
3. convert material attributes from adapter evidence to real shader resources.
4. convert lighting split into direct, shadow, and indirect resources.
5. convert scene-local environment into real irradiance/reflection resources.
6. implement source-aware reflection resolver shader.
7. implement full-scene composite shader.
8. implement cinematic post stack.
9. run cross-family packet ladder and promotion decision.

Do not claim completion from a screenshot. Completion requires opt-in candidate
beauty, real V3 shader resources, cross-family packet evidence, no device
removal, explanatory debug views, and user acceptance before default promotion.

### Phase 0 Stability: VB Motion Vector Guard - 2026-06-05

Root fix:

- `assets/shaders/VBMotionVectors.hlsl` now matches the safer material-resolve
  pattern and bounds-checks visibility-buffer instance IDs before reading
  `g_Instances`.
- `src/Graphics/VisibilityBuffer_Resolve.cpp` now passes `m_instanceCount` in
  the existing 8-dword motion-vector root constants.
- motion-vector shader now rejects:
  - `g_InstanceCount == 0`.
  - `instanceID >= g_InstanceCount`.
  - empty triangle ranges.
  - malformed mesh-table entries with invalid bindless indices, bad stride, or
    invalid index format before touching `ResourceDescriptorHeap`.

Why this was root-aligned:

- failing packets reported `lastGpuMarker='MotionVectors'`.
- DRED reported page fault at `GPU VA=0x0000000000000000`.
- `VBMotionVectors.hlsl` previously read `g_Instances[instanceID]` without an
  instance-count guard, unlike `MaterialResolve.hlsl`.
- a stale/corrupt visibility-buffer pixel could therefore fault before any
  existing mesh-index guard ran.

Validation:

```powershell
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis -FamilyFilter kitchen -ViewFilter beauty -SmokeFrames 28 -CaptureFrame 14 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\kitchen_vb_motion_guard_smoke1_20260605
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery,kitchen -ViewFilter beauty,shadow_factor,direct_light,direct_light_unshadowed,direct_light_shadow_loss,ambient_ibl,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,local_reflection_radiance,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta -SmokeFrames 28 -CaptureFrame 14 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_promotion_decision_gate_smoke6_20260605
```

Results:

- build passed.
- focused kitchen beauty mouse-jitter packet passed.
- broad gallery+kitchen V3 packet passed.
- `v3_promotion_decision_gate_smoke6_20260605`:
  - `reports=32`.
  - material, lighting, environment, reflection, composite, cinematic post all
    ready in `32/32` reports.
  - `v3_lighting_motion`: 22 view sequences across 2 families, failures `0`,
    warnings `0`.
  - promotion status: `review_packet_passed`.
  - default beauty promotable: `false`.
  - expected remaining warnings: missing families
    `concert,gym,office,red_room,stadium`; missing motion modes
    `camera_sweep,static`.

Important verification note:

- The first post-build packet still used the stale copied runtime shader under
  `build/bin/assets/shaders/VBMotionVectors.hlsl` because the build used
  `CORTEX_SKIP_ASSET_SYNC=1`.
- For shader-only fixes, either run asset sync intentionally or copy the changed
  shader into `build/bin/assets/shaders/` before packet verification.

Next safe pass:

1. Keep Phase 0 open for one more stress row: rerun the same guard under a
   reflective/metallic stress scene and a longer kitchen jitter/camera-sweep
   packet.
2. After that passes, implement the opt-in `FullSceneCandidateBeautyV3` switch
   and side-by-side capture support.
3. Do not promote default beauty from the two-family smoke. The smoke only
   proves the previous kitchen device-removal blocker is cleared for the tested
   packet shape.

Additional reflective stress validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter beauty,shadow_factor,direct_light,direct_light_unshadowed,direct_light_shadow_loss,ambient_ibl,v3_direct_lighting,v3_direct_lighting_unshadowed,v3_shadow_visibility,v3_shadow_loss,v3_indirect_lighting,local_reflection_radiance,reflection_source_authority,reflection_source_weights,reflection_resolver_candidate,reflection_resolver_candidate_delta -SmokeFrames 32 -CaptureFrame 16 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflective_stress_vb_motion_guard_smoke1_20260605
```

Result:

- scene-local packet passed.
- V2 evidence passed.
- V3 placeholder artifacts coherent.
- `reports=16`.
- V3 lighting motion measured 11 view sequences across 1 stress family.
- promotion status: `review_packet_passed`.
- no device removal in the reflective/metallic stress row.

Updated next safe pass:

1. Run a longer kitchen `camera_sweep` if device-removal risk reappears while
   implementing candidate beauty.
2. Begin the opt-in `FullSceneCandidateBeautyV3` switch and side-by-side
   capture support.

### FullSceneCandidateBeautyV3 Adapter Switch - 2026-06-05

Current slice:

- added an opt-in `candidate_beauty_v3` packet view.
- `candidate_beauty_v3` sets
  `CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3=1` only for that capture row.
- normal `beauty` rows explicitly clear the candidate env var.
- frame reports now expose:
  - `candidate_beauty_requested`.
  - `candidate_beauty_ready`.
  - `candidate_beauty_producer`.
  - `candidate_beauty_output`.
- `FullSceneShaderPipelineV3` now emits an optional `candidate_beauty` domain.
- current producer is intentionally an adapter:
  `FullSceneCandidateBeautyV3Adapter`.
- current candidate output is:
  `candidate_ldr_cinematic_output`.
- default beauty still reports `default_beauty_affects=false`.

Validation:

```powershell
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_beauty_adapter_smoke1_20260605
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_candidate_beauty_adapter_smoke1_20260605 --output-json build\captures\v3_candidate_beauty_adapter_smoke1_20260605\promotion_decision.json --output-md build\captures\v3_candidate_beauty_adapter_smoke1_20260605\promotion_decision.md --allow-subset-review
```

Result:

- V3 plan validator passed.
- Python compile passed.
- native build passed.
- gallery packet passed with `reports=17`.
- promotion status: `review_packet_passed`.
- default beauty promotable: `false`.
- candidate beauty requested reports: `1`.
- candidate beauty ready reports: `1`.
- direct report check:
  - `gallery/beauty`: `candidate_beauty_requested=false`,
    `candidate_beauty_ready=false`, `default_beauty_affects=false`.
  - `gallery/candidate_beauty_v3`: `candidate_beauty_requested=true`,
    `candidate_beauty_ready=true`,
    `candidate_beauty_producer=FullSceneCandidateBeautyV3Adapter`,
    `candidate_beauty_output=candidate_ldr_cinematic_output`,
    `default_beauty_affects=false`.

Important limitation:

- this is not the final Unreal-style shader composite.
- it is the bridge that lets packets capture and rank an opt-in candidate path
  separately from public default beauty.
- next implementation should replace the adapter with a real
  `FullSceneCandidateBeautyV3` composite/post resource path.

### Full Scene Shader Refactor Blueprint - 2026-06-05

Planning checkpoint:

- Expanded `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` with the concrete
  full-scene shader refactor blueprint.
- The plan now treats AAA visual quality as a staged resource architecture, not
  a single beauty-shader tweak.
- The target frame is split into:
  1. foundation resources: visibility, depth, velocity, material attributes,
     masks, and scene profile constants.
  2. lighting resources: shadows, direct light, indirect light, environment,
     reflections, emissive, atmosphere, and confidence/validity buffers.
  3. presentation resources: HDR composite, exposure, bloom, tonemap, color
     grade, final LDR, and side-by-side candidate output.

Immediate next implementation target:

- Replace `FullSceneCandidateBeautyV3Adapter` with a real opt-in render graph
  path:
  - offscreen resource: `candidate_ldr_cinematic_output`.
  - named pass: `FullSceneCandidateBeautyV3`.
  - frame-report producer:
    `candidate_beauty_producer=FullSceneCandidateBeautyV3`.
  - default `beauty` rows still report `default_beauty_affects=false`.

Refactor guardrails:

- Do not promote a feature unless it has a named producer resource, debug view,
  frame-report field, and packet gate.
- Do not use blur, hidden IBL, disabled reflections, or post-processing to make
  an upstream artifact disappear.
- Candidate beauty must stay opt-in until cross-family evidence and user review
  accept it.
- The next code slice should first create the real candidate output resource,
  then move material, reflection, environment, composite, and post domains from
  adapters into real producers.

### FullSceneCandidateBeautyV3 Real Resource Path - 2026-06-06

Implemented:

- `FullSceneCandidateBeautyV3` is now a real opt-in render graph pass.
- New renderer-owned target:
  `candidate_ldr_cinematic_output`.
- New target state:
  `FullSceneCandidateBeautyV3TargetState` in
  `src/Graphics/RendererMainTargetState.h`.
- The target is created with HDR-sized render targets and is tracked in:
  - frame-contract resources.
  - render-target memory accounting.
  - pass write memory/resolution classification.
- `PostProcessGraphPass` now accepts a custom pass name so the same post draw
  can render to either:
  - `PostProcess -> back_buffer`.
  - `FullSceneCandidateBeautyV3 -> candidate_ldr_cinematic_output`.
- The candidate pass only schedules when
  `CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3` is set.
- Default `beauty` rows still do not schedule the candidate pass.
- V3 readiness now requires:
  - valid `candidate_ldr_cinematic_output` resource.
  - executed `FullSceneCandidateBeautyV3` pass.
  - pass reads `hdr_color`.
  - pass writes `candidate_ldr_cinematic_output`.
  - composite/post V3 evidence remains ready.
- `tools/analyze_full_scene_shader_v3_placeholders.py` now rejects candidate
  reports that lack the real pass/resource path.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- src\Graphics\RendererMainTargetState.h src\Graphics\Renderer_HDRTargets.cpp src\Graphics\Renderer_Shutdown.cpp src\Graphics\Renderer_FrameContractMemory.cpp src\Graphics\Renderer_FrameContractPasses.cpp src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\Passes\PostProcessGraphPass.h src\Graphics\Passes\PostProcessGraphPass.cpp src\Graphics\Renderer.h src\Graphics\Renderer_RenderGraphEndFrame.cpp src\Graphics\FullSceneShaderFrameContext.h tools\analyze_full_scene_shader_v3_placeholders.py docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_beauty_resource_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed and linked `build/bin/CortexEngine.exe`.
- packet passed:
  - artifact root:
    `build/captures/v3_candidate_beauty_resource_smoke1_20260606`.
  - reports: `17`.
  - promotion status: `review_packet_passed`.
  - candidate requested reports: `1`.
  - candidate ready reports: `1`.
- Direct frame-report proof:
  - `gallery/beauty`:
    `candidate_beauty_requested=false`,
    `candidate_beauty_ready=false`,
    `candidate_beauty_producer=none`,
    no `FullSceneCandidateBeautyV3` pass,
    `PostProcess` writes only `back_buffer`,
    `default_beauty_affects=false`.
  - `gallery/candidate_beauty_v3`:
    `candidate_beauty_requested=true`,
    `candidate_beauty_ready=true`,
    `candidate_beauty_producer=FullSceneCandidateBeautyV3`,
    `candidate_beauty_output=candidate_ldr_cinematic_output`,
    valid resource size `1088x612`,
    `FullSceneCandidateBeautyV3` executed,
    pass reads `hdr_color`,
    pass writes `candidate_ldr_cinematic_output`,
    `PostProcess` still writes only `back_buffer`,
    `default_beauty_affects=false`.

Important limitation:

- The real candidate path currently reuses the existing post-process shader.
- This proves separate resource ownership and packet gating, not final
  Unreal-style composite quality.

Next safe pass:

1. Add a debug-menu toggle and optional split-screen/display path for
   candidate beauty so the user can compare it without command-line flags.
2. Begin replacing the composite/post adapters with real
   `FullSceneCompositeV3` and `CinematicPostV3` producers over V3 resources.
3. Keep default beauty unchanged until cross-family evidence and user review
   approve candidate promotion.

### FullSceneCandidateBeautyV3 Debug Toggle - 2026-06-06

Implemented:

- Added renderer state:
  `RendererPostProcessState::fullSceneCandidateBeautyV3Enabled`.
- Added renderer API:
  - `SetFullSceneCandidateBeautyV3Enabled`.
  - `IsFullSceneCandidateBeautyV3Enabled`.
- Added feature-state reporting:
  `RendererFeatureState::fullSceneCandidateBeautyV3Enabled`.
- Added control-applier support:
  `RendererFeatureToggle::FullSceneCandidateBeautyV3`.
- `ExecuteEndFrameInRenderGraph` now schedules
  `FullSceneCandidateBeautyV3` when either:
  - `CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3` is set, or
  - the renderer UI toggle is enabled.
- V3 frame context treats an executed candidate pass as a candidate request, so
  UI-driven runs report `candidate_beauty_requested=true` without requiring an
  environment variable.
- Added Win32 debug-menu checkbox:
  `FullSceneCandidateBeautyV3`.
- Added on-screen settings overlay row:
  `[Advanced] Candidate Beauty V3`, row `15`.
- Keyboard overlay controls now toggle row `15` with left/right or
  space/enter.
- Debug-menu reset disables candidate beauty again.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- src\Graphics\RendererPostProcessState.h src\Graphics\Renderer_DiagnosticsTypes.h src\Graphics\Renderer_Diagnostics.cpp src\Graphics\Renderer.h src\Graphics\Renderer_FeatureSettings.cpp src\Graphics\RendererControlTypes.h src\Graphics\RendererControlApplier.h src\Graphics\RendererControlApplier_Debug.cpp src\Graphics\RendererControlApplier_Runtime.cpp src\Graphics\Renderer_RenderGraphEndFrame.cpp src\Graphics\FullSceneShaderFrameContext.h src\UI\DebugMenu.h src\UI\DebugMenu.cpp src\Core\Engine.cpp src\Core\Engine_UI.cpp src\Core\Engine_Input.cpp
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_beauty_ui_toggle_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed; first run timed out after link, immediate rerun reported
  `ninja: no work to do`.
- packet passed:
  - artifact root:
    `build/captures/v3_candidate_beauty_ui_toggle_smoke1_20260606`.
  - reports: `17`.
  - promotion status: `review_packet_passed`.
- Direct report proof for `gallery/candidate_beauty_v3`:
  - `candidate_beauty_requested=true`.
  - `candidate_beauty_ready=true`.
  - `candidate_beauty_producer=FullSceneCandidateBeautyV3`.
  - `candidate_beauty_output=candidate_ldr_cinematic_output`.
  - `default_beauty_affects=false`.
  - valid candidate resource size `1088x612`.
  - `FullSceneCandidateBeautyV3` reads `hdr_color`.
  - `FullSceneCandidateBeautyV3` writes
    `candidate_ldr_cinematic_output`.

Remaining limitation:

- The UI toggle requests/captures the offscreen candidate path, but the app
  still presents normal default beauty to the swapchain.
- A true visual compare mode still needs a blit/split-screen display path from
  `candidate_ldr_cinematic_output` to the backbuffer.

### FullSceneCandidateBeautyV3 Display Bridge - 2026-06-06

Implemented:

- Added `assets/shaders/CandidateBeautyDisplay.hlsl`, a fullscreen display
  shader for the opt-in candidate LDR output.
- Added compiled shader/pipeline state:
  - `RendererCompiledShaders::candidateBeautyDisplayPS`.
  - `RendererPipelineState::candidateBeautyDisplay`.
- Added render-graph display pass:
  `FullSceneCandidateBeautyV3Display`.
- The display pass:
  - reads `candidate_ldr_cinematic_output`.
  - writes `back_buffer`.
  - runs only when candidate beauty is requested and either:
    - the UI/debug toggle is enabled, or
    - `CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3=1`.
- `FullSceneCandidateBeautyV3` now renders with the internal render target
  dimensions when writing its offscreen candidate target.
- Frame reports now expose:
  `candidate_beauty_displayed`.
- The V3 analyzer now verifies claimed display evidence:
  - `FullSceneCandidateBeautyV3Display` pass exists.
  - the pass executed.
  - it reads `candidate_ldr_cinematic_output`.
  - it writes `back_buffer`.
  - display is not claimed before candidate beauty is ready.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- CortexEngine/src/Graphics/Renderer_PipelineSetupTypes.h CortexEngine/src/Graphics/RendererPipelineState.h CortexEngine/src/Graphics/Renderer_ShaderCompilation.cpp CortexEngine/src/Graphics/Renderer_ScreenComputePipelineSetup.cpp CortexEngine/src/Graphics/Renderer.h CortexEngine/src/Graphics/Renderer_RenderGraphEndFrame.cpp CortexEngine/src/Graphics/Renderer_FrameContractPasses.cpp CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/src/Graphics/FrameContractJson.cpp CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py CortexEngine/assets/shaders/CandidateBeautyDisplay.hlsl
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
$env:CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_beauty_display_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed and linked `build/bin/CortexEngine.exe`.
- packet passed:
  - artifact root:
    `build/captures/v3_candidate_beauty_display_smoke1_20260606`.
  - reports: `17`.
  - promotion status: `review_packet_passed`.
- Direct report proof:
  - `gallery/beauty`:
    `candidate_beauty_requested=false`,
    `candidate_beauty_ready=false`,
    `candidate_beauty_displayed=false`,
    `default_beauty_affects=false`.
  - `gallery/candidate_beauty_v3`:
    `candidate_beauty_requested=true`,
    `candidate_beauty_ready=true`,
    `candidate_beauty_displayed=true`,
    `candidate_beauty_producer=FullSceneCandidateBeautyV3`,
    `candidate_beauty_output=candidate_ldr_cinematic_output`,
    `default_beauty_affects=false`.
  - `FullSceneCandidateBeautyV3` pass executed, read `hdr_color`, and wrote
    `candidate_ldr_cinematic_output`.
  - `FullSceneCandidateBeautyV3Display` pass executed, read
    `candidate_ldr_cinematic_output`, and wrote `back_buffer`.

Remaining limitation:

- This is still a display bridge over the current post-process composite.
- The next real quality move is replacing the composite/post adapters with
  real `FullSceneCompositeV3` and `CinematicPostV3` producers that consume V3
  lighting, environment, reflection, and material resources directly.

### FullSceneCompositeV3 / CinematicPostV3 Producer Slice - 2026-06-06

Implemented:

- Added `assets/shaders/FullSceneCompositeV3.hlsl`.
- Added compiled shader/pipeline state:
  - `RendererCompiledShaders::fullSceneCompositeV3PS`.
  - `RendererPipelineState::fullSceneCompositeV3`.
- Added `candidate_hdr_scene_color` as an HDR render target with RTV/SRV
  descriptors and frame-contract resource accounting.
- Added render-graph pass `FullSceneCompositeV3`:
  - reads `direct_lighting`, `indirect_lighting`, `shadow_visibility`, and
    legacy `hdr_color` fallback.
  - writes `candidate_hdr_scene_color`.
- The opt-in candidate path now routes:
  `FullSceneCompositeV3 -> CinematicPostV3 -> candidate_ldr_cinematic_output`.
- `CinematicPostV3` reuses the existing post shader implementation for now,
  but it is a distinct graph pass and consumes `candidate_hdr_scene_color`.
- Default beauty remains unchanged. Normal non-candidate rows still report
  adapter producers.
- Static V3 contract and validators now include:
  - `candidate_hdr_scene_color`.
  - `candidate_ldr_cinematic_output`.
  - real producer acceptance for `FullSceneCompositeV3` and `CinematicPostV3`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- src\Graphics\Renderer_PipelineSetupTypes.h src\Graphics\RendererPipelineState.h src\Graphics\Renderer_ShaderCompilation.cpp src\Graphics\Renderer_ScreenComputePipelineSetup.cpp src\Graphics\Renderer.h src\Graphics\RendererMainTargetState.h src\Graphics\Renderer_HDRTargets.cpp src\Graphics\Renderer_FrameContractMemory.cpp src\Graphics\Renderer_FrameContractPasses.cpp src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\Renderer_Shutdown.cpp src\Graphics\Renderer_RenderGraphEndFrame.cpp src\Graphics\FullSceneShaderFrameContext.h tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py assets\final_art\full_scene_shader_pipeline_v3_contract.json assets\shaders\FullSceneCompositeV3.hlsl
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; $env:CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_post_producer_smoke2_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed with `Required outputs: 11`.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed and linked `build/bin/CortexEngine.exe`; the existing
  trailing `vswhere.exe` warning still appears after success.
- first packet attempt used only `CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3`
  and did not request candidate beauty; treat
  `v3_composite_post_producer_smoke1_20260606` as a negative control.
- corrected packet passed:
  - artifact root:
    `build/captures/v3_composite_post_producer_smoke2_20260606`.
  - reports: `17`.
  - promotion status: `review_packet_passed`.

Direct report proof from
`gallery/candidate_beauty_v3/frame_report_shutdown.json`:

- `candidate_beauty_requested=true`.
- `candidate_beauty_ready=true`.
- `candidate_beauty_displayed=true`.
- `candidate_beauty_producer=CinematicPostV3`.
- `candidate_beauty_output=candidate_ldr_cinematic_output`.
- `default_beauty_affects=false`.
- `composite_v3_producer=FullSceneCompositeV3`.
- `cinematic_post_v3_producer=CinematicPostV3`.
- `FullSceneCompositeV3` executed, read
  `direct_lighting`, `indirect_lighting`, `shadow_visibility`, and `hdr_color`,
  and wrote `candidate_hdr_scene_color`.
- `CinematicPostV3` executed, read `candidate_hdr_scene_color`, and wrote
  `candidate_ldr_cinematic_output`.
- `FullSceneCandidateBeautyV3Display` executed, read
  `candidate_ldr_cinematic_output`, and wrote `back_buffer`.
- `candidate_hdr_scene_color` and `candidate_ldr_cinematic_output` were both
  valid `1088x612` resources with size matching the render contract.

Remaining limitation:

- `CinematicPostV3` is a real graph producer but still uses the existing
  post-process shader implementation.
- `FullSceneCompositeV3` currently combines V3 direct/indirect lighting and
  shadow visibility with a bounded HDR fallback. Reflection/environment/media
  are not yet split into the final V3 composite inputs.
- Next slice should add raw debug views and motion-stability packets for
  `candidate_hdr_scene_color`, then start moving reflection/environment inputs
  from adapter evidence into concrete producer resources.

### Candidate HDR Debug View and Motion Gate - 2026-06-06

Implemented:

- Added debug mode `67`: `FullSceneCompositeV3CandidateHDR`.
- Added packet view `candidate_hdr_scene_color`.
- The view enables candidate V3, runs `FullSceneCompositeV3`, and displays the
  raw `candidate_hdr_scene_color` target through `FullSceneCompositeV3DebugView`.
- The V3 packet default view list now includes `candidate_hdr_scene_color`.
- The V3 metrics gate now requires `candidate_hdr_scene_color` to be present and
  nonblank.
- The V3 motion analyzer now includes `candidate_hdr_scene_color` as a candidate
  composite row. It is compared against beauty motion, not against a legacy
  lighting split row.
- The strict V2 checker now accepts debug mode range `67`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_lighting_motion.py tools\analyze_full_scene_shader_v3_placeholders.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- src\Graphics\Renderer.h src\Graphics\Renderer_DebugSettings.cpp src\Graphics\Renderer_RenderGraphEndFrame.cpp src\Graphics\Renderer_FrameContractPasses.cpp src\Graphics\FullSceneShaderFrameContext.h tools\run_scene_local_cinematic_renderer_v1_packets.ps1 tools\run_full_scene_shader_pipeline_v3_packet.ps1 tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\check_full_scene_shader_pipeline_v2_frame_report.py tools\validate_full_scene_shader_pipeline_v3_plan.py docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md docs\AAA_ASSET_QUALITY_HANDOFF.md
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"" -C build CortexEngine -j 8"
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_hdr_debug_static_smoke2_20260606
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_candidate_hdr_debug_motion_smoke1_20260606
python tools\analyze_full_scene_shader_v3_lighting_motion.py --manifest build\captures\v3_candidate_hdr_debug_motion_smoke1_20260606\manifest.json --output-json build\captures\v3_candidate_hdr_debug_motion_smoke1_20260606\v3_lighting_motion.json --output-md build\captures\v3_candidate_hdr_debug_motion_smoke1_20260606\v3_lighting_motion.md --min-sequence-count 2
```

Results:

- Python compile passed.
- V3 plan validator passed.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build completed; first command timed out while Ninja continued in the
  background, then a follow-up Ninja run reported `ninja: no work to do`.
- static packet passed:
  - artifact root:
    `build/captures/v3_candidate_hdr_debug_static_smoke2_20260606`.
  - reports: `18`.
  - promotion status: `review_packet_passed`.
- mouse-jitter motion packet passed:
  - artifact root:
    `build/captures/v3_candidate_hdr_debug_motion_smoke1_20260606`.
  - reports: `18`.
  - V3 lighting motion measured `12` view sequences.
  - promotion status: `review_packet_passed`.

Direct evidence:

- `candidate_hdr_scene_color` metrics from the motion packet:
  - debug view: `67`.
  - mean luma: `0.440450`.
  - max luma: `1.000000`.
  - nonblack ratio: `0.961260`.
  - hot-pixel ratio: `0.154786`.
- `candidate_hdr_scene_color` motion row:
  - status: `ok`.
  - mean abs luma delta: `0.008965`.
  - beauty mean abs luma delta: `0.006547`.
  - candidate/beauty ratio: `1.369420`.
  - active delta ratio: `0.061689`.
- `gallery/candidate_hdr_scene_color/frame_report_shutdown.json`:
  - `FullSceneCompositeV3` executed and wrote `candidate_hdr_scene_color`.
  - `CinematicPostV3` executed and read `candidate_hdr_scene_color`.
  - `FullSceneCompositeV3DebugView` executed, read
    `candidate_hdr_scene_color`, and wrote `back_buffer`.

Remaining limitation:

- This proves visibility and short mouse-jitter stability for the raw candidate
  HDR target, not final AAA quality.
- Next producer gap is still reflection/environment/media ownership inside
  `FullSceneCompositeV3`. The current composite uses V3 lighting plus a bounded
  HDR fallback; it does not yet consume concrete V3 reflection/environment
  radiance resources as first-class inputs.

### Concrete ReflectionResolverV3 Producer - 2026-06-06

Implemented:

- Added `assets/shaders/FullSceneReflectionResolverV3.hlsl`.
- Added persistent ReflectionV3 targets and descriptors:
  `reflection_radiance`, `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, and `reflection_temporal_delta`.
- Added render-graph pass `FullSceneReflectionV3`.
  - reads `local_reflection_radiance`.
  - writes all five concrete ReflectionV3 resources.
- `FullSceneCompositeV3` now consumes `reflection_radiance` from the resolver
  when available.
- Added ReflectionV3 debug modes:
  - `68`: reflection radiance.
  - `69`: reflection confidence.
  - `70`: reflection source ID.
  - `71`: rejected source mask.
  - `72`: temporal delta.
- Updated packet runners, V3 analyzers, the static contract, and runtime
  required-output reporting so all five ReflectionV3 outputs are required and
  visible.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/src/Graphics/Renderer_RenderGraphEndFrame.cpp CortexEngine/src/Graphics/Renderer_FrameContractSnapshot.cpp CortexEngine/src/Graphics/RendererMainTargetState.h CortexEngine/assets/final_art/full_scene_shader_pipeline_v3_contract.json CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py
& 'C:\Program Files\Ninja\ninja.exe' -C build -t recompact
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_resolver_static_smoke2_20260606
$env:CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3='1'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_resolver_motion_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed with `Required outputs: 14`.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed and linked `build/bin/CortexEngine.exe`.
- static packet passed:
  `build/captures/v3_reflection_resolver_static_smoke2_20260606`.
  - reports: `23`.
  - promotion status: `review_packet_passed`.
- mouse-jitter packet passed:
  `build/captures/v3_reflection_resolver_motion_smoke1_20260606`.
  - reports: `23`.
  - V3 lighting/reflection motion measured `17` view sequences.
  - promotion status: `review_packet_passed`.

Direct frame-report proof:

- required outputs: `14`.
- `reflection_v3_ready=true`.
- `reflection_v3_channel_count=5`.
- `reflection_v3_source_contract=local_probe`.
- `FullSceneReflectionV3` executed, read `local_reflection_radiance`, and wrote
  `reflection_radiance`, `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, and `reflection_temporal_delta`.
- `FullSceneCompositeV3` executed, read `direct_lighting`,
  `indirect_lighting`, `shadow_visibility`, `hdr_color`, and
  `reflection_radiance`, then wrote `candidate_hdr_scene_color`.

Mouse-jitter reflection evidence:

- `reflection_radiance`: mean abs luma delta `0.0024393373`, active delta
  ratio `0.0140386285`.
- `reflection_confidence`: mean abs luma delta `0.0003614430`, active delta
  ratio `0.0051226128`.
- `reflection_source_id`: mean abs luma delta `0.0004442693`, active delta
  ratio `0.0057389323`.
- `reflection_rejected_source_mask`: mean abs luma delta `0.0001864565`,
  active delta ratio `0.0017708333`.
- `reflection_temporal_delta`: mean abs luma delta `0.0001864565`, active
  delta ratio `0.0017708333`.

Remaining limitation:

- The resolver is now a concrete resource producer, but it still wraps the
  current local reflection radiance source. Source arbitration across local
  probe, SSR, RT/ray query, and scene-local environment remains the next
  ReflectionV3 quality step.
- This is still candidate-path infrastructure. Default beauty remains
  unchanged.

### ReflectionResolverV3 Source Policy Admission - 2026-06-06

Implemented:

- `FullSceneReflectionResolverV3.hlsl` now performs explicit source admission
  instead of blindly copying local reflection radiance.
- Current source candidates:
  - scene-local reflection radiance.
  - scene-local environment fallback from frame ambient/environment/probe
    constants.
- Added source override lane through `FrameConstants.localProbeParams.w`.
- Added debug environment variable:
  `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE`.
  - `auto` or unset: prefer scene-local radiance, then environment fallback.
  - `local`: force scene-local radiance if available.
  - `environment`: force scene-local environment fallback.
  - `none`: force no reflection source for rejection-path debugging.
- Frame reports now expose forced review contracts:
  `forced_scene_local_radiance`, `forced_scene_local_environment`,
  `forced_none`, or `forced_unknown`.
- The analyzer accepts forced local/environment contracts as deliberate review
  modes while still rejecting unknown source ownership.
- Rejection channels now encode:
  - local radiance rejected/missing.
  - environment fallback rejected/missing.
  - dynamic SSR/RT source not admitted yet.
- Temporal delta now reports stable zero for scene-local sources and exposes
  forced-but-unavailable policies separately.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- CortexEngine/assets/shaders/FullSceneReflectionResolverV3.hlsl CortexEngine/src/Graphics/Renderer_FramePostConstants.cpp CortexEngine/src/Graphics/ShaderTypes.h CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py
& 'C:\Program Files\Ninja\ninja.exe' -C build -t recompact
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_source_policy_auto_static_smoke1_20260606
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='environment'; powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_source_policy_environment_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_source_policy_auto_motion_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed with `Required outputs: 14`.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed and linked `build/bin/CortexEngine.exe`.
- auto static packet passed:
  `build/captures/v3_reflection_source_policy_auto_static_smoke1_20260606`.
  - reports: `23`.
  - `reflection_v3_source_contract=local_probe`.
  - promotion status: `review_packet_passed`.
- forced environment static packet passed:
  `build/captures/v3_reflection_source_policy_environment_static_smoke1_20260606`.
  - reports: `23`.
  - `reflection_v3_source_contract=forced_scene_local_environment`.
  - promotion status: `review_packet_passed`.
- auto mouse-jitter packet passed:
  `build/captures/v3_reflection_source_policy_auto_motion_smoke1_20260606`.
  - reports: `23`.
  - V3 lighting/reflection motion measured `17` view sequences.
  - promotion status: `review_packet_passed`.

Signal evidence:

- auto static:
  - `reflection_radiance.mean_luma=0.0719490`,
    `nonblack_ratio=0.9995692`, `hot_pixel_ratio=0.0042914`.
  - `reflection_confidence.mean_luma=0.0334686`,
    `nonblack_ratio=0.1463411`.
  - `reflection_source_id.mean_luma=0.0843016`,
    `nonblack_ratio=1.0`.
- forced environment static:
  - `reflection_radiance.mean_luma=0.0551851`,
    `nonblack_ratio=1.0`, `hot_pixel_ratio=0.0`.
  - `reflection_confidence.mean_luma=0.4980392`,
    `nonblack_ratio=1.0`.
  - `reflection_source_id.mean_luma=0.6409976`,
    `nonblack_ratio=1.0`.

Mouse-jitter reflection evidence:

- `reflection_radiance`: mean abs luma delta `0.0024278814`, active delta
  ratio `0.0138726128`.
- `reflection_confidence`: mean abs luma delta `0.0007756502`, active delta
  ratio `0.0068261719`.
- `reflection_source_id`: mean abs luma delta `0.0006937146`, active delta
  ratio `0.0058745660`.
- `reflection_rejected_source_mask`: mean abs luma delta `0.0001864565`,
  active delta ratio `0.0017708333`.
- `reflection_temporal_delta`: mean abs luma delta `0.0`, active delta
  ratio `0.0`.

Remaining limitation:

- SSR and RT/ray-query reflection are still not real inputs to the resolver.
  They remain visible as not-admitted dynamic source debt in the rejection
  channel until the next source-fusion slice.

### ReflectionResolverV3 SSR Input Wiring - 2026-06-06

Implemented:

- `FullSceneReflectionV3` now receives `ssr_color` as a second resolver input.
- The render graph passes `SSRColor` into the resolver when the resource exists.
- The resolver shader samples `g_SSRReflection : t1`.
- Auto source policy now supports SSR:
  - SSR can win only when its confidence is high enough above scene-local
    radiance.
  - local radiance remains the stable fallback.
  - environment remains the final scene-local fallback.
- `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE` now accepts `ssr` or numeric `2`.
- Frame reports now expose `forced_screen_space_reflection`.
- V3 readiness now requires `FullSceneReflectionV3` to read both
  `local_reflection_radiance` and `ssr_color`.
- The analyzer now fails if `FullSceneReflectionV3` stops reading `ssr_color`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git diff --check -- CortexEngine/assets/shaders/FullSceneReflectionResolverV3.hlsl CortexEngine/src/Graphics/Renderer_RenderGraphEndFrame.cpp CortexEngine/src/Graphics/Renderer_FramePostConstants.cpp CortexEngine/src/Graphics/ShaderTypes.h CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py
& 'C:\Program Files\Ninja\ninja.exe' -C build -t recompact
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_ssr_input_auto_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_ssr_input_auto_motion_smoke1_20260606
```

Results:

- Python compile passed.
- V3 plan validator passed with `Required outputs: 14`.
- focused `git diff --check` passed, with only existing CRLF warnings.
- native build passed and linked `build/bin/CortexEngine.exe`.
- auto static packet passed:
  `build/captures/v3_reflection_ssr_input_auto_static_smoke1_20260606`.
  - reports: `23`.
  - promotion status: `review_packet_passed`.
- auto mouse-jitter packet passed:
  `build/captures/v3_reflection_ssr_input_auto_motion_smoke1_20260606`.
  - reports: `23`.
  - V3 lighting/reflection motion measured `17` view sequences.
  - promotion status: `review_packet_passed`.

Direct frame-report proof:

- `SSR.executed=true`.
- `SSR.reads=hdr_color`, `depth`, and `vb_gbuffer_normal_roughness`.
- `SSR.writes=ssr_color`.
- `FullSceneReflectionV3.executed=true`.
- `FullSceneReflectionV3.reads=local_reflection_radiance` and `ssr_color`.
- `FullSceneReflectionV3.writes=reflection_radiance`,
  `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, and `reflection_temporal_delta`.
- `FullSceneCompositeV3.reads=reflection_radiance`.

Auto mouse-jitter reflection evidence:

- `reflection_radiance`: mean abs luma delta `0.0024278814`, active delta
  ratio `0.0138726128`.
- `reflection_confidence`: mean abs luma delta `0.0007756502`, active delta
  ratio `0.0068261719`.
- `reflection_source_id`: mean abs luma delta `0.0006937146`, active delta
  ratio `0.0058745660`.
- `reflection_rejected_source_mask`: mean abs luma delta `0.0001864565`,
  active delta ratio `0.0017708333`.
- `reflection_temporal_delta`: mean abs luma delta `0.0`, active delta
  ratio `0.0`.

Forced-SSR diagnostic:

- Attempted:
  `build/captures/v3_reflection_source_policy_ssr_stress_static_smoke1_20260606`.
- The report proved `SSR.executed=true`, `SSR.writes=ssr_color`, and
  `FullSceneReflectionV3.reads=ssr_color`.
- The packet failed signal gates because forced SSR produced blank
  `reflection_radiance` and `reflection_confidence` in that stress capture.
- Treat this as real SSR source-quality debt, not a failure of the V3 resolver
  wiring.

Remaining limitation:

- SSR is now a real resolver input, but it is not yet artistically/admission
  reliable enough to force as the selected source in the current stress row.
- Next SSR work should improve SSR confidence/radiance coverage or add a
  source-specific diagnostic packet before allowing SSR to win more often in
  auto policy.

### Full Scene Shader Renderer Refactor Direction - 2026-06-06

Current target:

- Build `FullSceneCandidateBeautyV3` as the opt-in high-end renderer path.
- Keep default beauty unchanged until candidate evidence is good enough and the
  user explicitly accepts promotion.
- Treat high-end visual quality as a system refactor, not a screenshot polish
  exercise.

Renderer domains to own:

- Material payload V3:
  stable PBR payload for base color, normal, roughness, metallic, specular,
  emissive, opacity, AO, height/parallax, clearcoat, sheen, anisotropy, and
  material class.
- Lighting V3:
  direct light, shadow visibility, indirect light, emissive light, volumetric
  contribution, and exposure-pre-tonemap energy as separate resources.
- Reflection V3:
  local radiance, SSR, RT/ray query, planar/hero probes, and scene-local
  environment fused by source ID, confidence, rejection mask, and temporal debt.
- Scene-local environment V3:
  visible background is separate from lighting/reflection background, so closed
  rooms do not inherit inappropriate IBL imagery.
- Composite V3:
  candidate HDR image built from V3 terms rather than an adapter over legacy
  `hdr_color`.
- Cinematic post V3:
  controlled exposure, bloom, tone map, color grade, glare, DOF, and history
  diagnostics with locked-exposure stability packets.

Execution order:

1. SSR source-quality pass.
   - Capture raw `ssr_color`, SSR confidence, normal/roughness/depth inputs,
     rejection reasons, and resolver output.
   - Fix the blank forced-SSR stress packet before allowing SSR to dominate
     auto policy.
2. Material payload pass.
   - Normalize material channels and add range/debug gates so surfaces stop
     reading as flat, plastic, mirror-like, or chalky by accident.
3. Shadow and lighting stability pass.
   - Lock exposure and reject moving shadows/light on static floor and wall
     pixels under mouse jitter and camera sweep.
4. Scene-local environment pass.
   - Keep IBL lighting/reflection useful while preventing visible/reflection
     content leaks in enclosed scenes.
5. Candidate composite pass.
   - Replace adapter ownership with a true V3 candidate composite and
     split-screen/default comparison packet.
6. Cross-family art packet.
   - Validate gallery, kitchen, office, gym, classroom, concert, red room,
     stadium, bathroom, bedroom, workshop, store, and street with static,
     mouse-jitter, camera-sweep, close-surface, and reflective-object rows.

Non-negotiable gates:

- every feature must have named resources, producer pass, debug view,
  frame-report fields, analyzer checks, and packet evidence.
- no fix can rely only on IBL blur, disabled reflections, hidden backgrounds,
  or changing the tested scene.
- do not claim completion from a single attractive screenshot.

### ReflectionV3 SSR Source Signal Diagnostic - 2026-06-06

Implemented:

- Added `reflection_ssr_source_signal` as a real ReflectionV3 render target.
- `FullSceneReflectionV3` now writes six outputs:
  `reflection_radiance`, `reflection_confidence`, `reflection_source_id`,
  `reflection_rejected_source_mask`, `reflection_temporal_delta`, and
  `reflection_ssr_source_signal`.
- Debug view `73` displays `reflection_ssr_source_signal`.
- Diagnostic channel meaning:
  - R = raw SSR radiance luma.
  - G = raw SSR alpha/weight.
  - B = admitted SSR confidence.
  - A = forced-SSR unavailable/rejected.
- V3 required outputs increased from `14` to `15`.
- `reflection_v3_channel_count` increased from `5` to `6`.
- `run_full_scene_shader_pipeline_v3_packet.ps1` now captures the new view.
- The motion analyzer now tracks `reflection_ssr_source_signal`.

Root diagnosis and fix:

- Forced SSR was blank because the resolver used the auto confidence gate for
  forced/debug mode.
- The raw SSR source had nonzero coverage, but raw alpha peaked around `0.455`,
  below the old `smoothstep(0.55, 0.86)` admission floor.
- Auto SSR remains strict.
- Forced SSR now admits raw nonzero SSR signal for diagnostics instead of
  silently returning black. This does not promote SSR dominance in auto policy.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_ssr_source_signal_auto_static_smoke3_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_ssr_source_signal_forced_ssr_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_ssr_source_signal_auto_motion_smoke1_20260606
python tools\analyze_full_scene_shader_v3_lighting_motion.py --manifest build\captures\v3_reflection_ssr_source_signal_auto_motion_smoke1_20260606\manifest.json --output-json build\captures\v3_reflection_ssr_source_signal_auto_motion_smoke1_20260606\v3_lighting_motion.json --output-md build\captures\v3_reflection_ssr_source_signal_auto_motion_smoke1_20260606\v3_lighting_motion.md
```

Results:

- V3 plan validator passed with `Required outputs: 15`.
- Native build passed.
- Auto static packet passed:
  `build/captures/v3_reflection_ssr_source_signal_auto_static_smoke3_20260606`.
- Forced SSR static packet passed:
  `build/captures/v3_reflection_ssr_source_signal_forced_ssr_static_smoke1_20260606`.
- Auto mouse-jitter packet passed:
  `build/captures/v3_reflection_ssr_source_signal_auto_motion_smoke1_20260606`.
- Motion analyzer measured `18` view sequences after adding
  `reflection_ssr_source_signal`.

Frame-report proof:

- `required_outputs=15`.
- `reflection_v3_channel_count=6`.
- `reflection_ssr_source_signal_ready=true`.
- `FullSceneReflectionV3.reads=local_reflection_radiance,ssr_color`.
- `FullSceneReflectionV3.writes` includes `reflection_ssr_source_signal`.
- `reflection_ssr_source_signal` resource valid at render resolution.

Source signal metrics:

- auto static:
  - `reflection_ssr_source_signal.mean_luma=0.0064881`.
  - `nonblack_ratio=0.0849750`.
  - source contract remains `local_probe`.
- forced SSR static:
  - `reflection_radiance.mean_luma=0.0196873`,
    `nonblack_ratio=0.0820681`.
  - `reflection_confidence.mean_luma=0.0033247`,
    `nonblack_ratio=0.0337229`.
  - `reflection_ssr_source_signal.mean_luma=0.0067306`,
    `nonblack_ratio=0.0849750`.
  - source contract is `forced_screen_space_reflection`.
- auto mouse-jitter:
  - `reflection_ssr_source_signal.mean_abs_luma_delta=0.0012916`.
  - `reflection_ssr_source_signal.mean_active_delta_ratio=0.0157010`.

Remaining limitation:

- SSR source coverage is still sparse and low-confidence in the gallery row.
- The next rendering-quality pass should improve the SSR producer itself:
  depth/normal intersection tolerance, roughness/material masks, HZB or depth
  hierarchy use, edge fade, and confidence calibration.
  Do not let SSR win more often in auto mode until source quality improves.

### SSR Producer Refinement Pass - 2026-06-06

Implemented:

- Updated `assets/shaders/SSR.hlsl`.
- Increased SSR march budget from `64` to `96` steps.
- Reduced near-origin skip distance and minimum hit separation so valid
  near-field glossy hits are not skipped as aggressively.
- Added view-space projection helper and crossing refinement:
  - if a ray step crosses from in front of to behind scene depth, refine the
    interval with 5 binary-search steps.
  - this catches hits that the previous fixed-step `dz < thickness` test could
    step past.
- Recalibrated SSR alpha as source confidence:
  - previous alpha was mostly raw material reflection weight.
  - new alpha uses `sqrt(reflectionWeight) * distanceFactor * edgeFade *
    ssrStrength`.
  - radiance remains multiplied by SSR strength.
- Kept resolver auto policy strict. This pass improves the SSR producer signal;
  it does not make SSR dominate auto reflection selection.

Validation notes:

- The broad asset-sync build path timed out once while generating assets.
  The stale runtime copy was then updated explicitly:
  `Copy-Item assets\shaders\SSR.hlsl build\bin\assets\shaders\SSR.hlsl -Force`.
- Runtime packet validation compiled/exercised the updated shader through the
  engine.
- Static checks:
  - `python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py`
  - `python tools\validate_full_scene_shader_pipeline_v3_plan.py`
  - V3 validator passed with `Required outputs: 15`.
- Packets:
  - forced SSR static:
    `build/captures/v3_ssr_producer_refined_forced_static_smoke1_20260606`.
  - auto static:
    `build/captures/v3_ssr_producer_refined_auto_static_smoke1_20260606`.
  - auto mouse-jitter:
    `build/captures/v3_ssr_producer_refined_auto_motion_smoke1_20260606`.
  - forced SSR diagnostic-only motion probe:
    `build/captures/v3_ssr_producer_refined_forced_motion_probe1_20260606`.

Before/after signal:

- previous forced SSR static:
  - `reflection_radiance.mean_luma=0.0196873`,
    `nonblack_ratio=0.0820681`.
  - `reflection_confidence.mean_luma=0.0033247`,
    `nonblack_ratio=0.0337229`.
  - `reflection_ssr_source_signal.mean_luma=0.0067306`,
    `nonblack_ratio=0.0849750`.
- refined forced SSR static:
  - `reflection_radiance.mean_luma=0.1127585`,
    `nonblack_ratio=0.3997233`.
  - `reflection_confidence.mean_luma=0.0575320`,
    `nonblack_ratio=0.3876128`.
  - `reflection_ssr_source_signal.mean_luma=0.0697975`,
    `nonblack_ratio=0.4163715`.
- refined auto static:
  - `reflection_ssr_source_signal.mean_luma=0.0656002`,
    `nonblack_ratio=0.4163715`.
  - source contract remains `local_probe`.

Motion evidence:

- refined auto mouse-jitter passed the standard V3 packet.
- admitted auto reflection rows remained essentially unchanged:
  - `reflection_radiance.delta=0.0024279`, active `0.0138726`.
  - `reflection_confidence.delta=0.0007757`, active `0.0068262`.
  - `reflection_temporal_delta.delta=0.0`, active `0.0`.
- `reflection_ssr_source_signal` is more active after producer refinement:
  - previous auto signal delta `0.0012916`, active `0.0157010`.
  - refined auto signal delta `0.0058466`, active `0.0516949`.
- forced SSR diagnostic-only motion probe:
  - `reflection_radiance.delta=0.0110627`, active `0.0645812`.
  - `reflection_confidence.delta=0.0051552`, active `0.0412988`.
  - `reflection_ssr_source_signal.delta=0.0062148`, active `0.0525141`.

Interpretation:

- SSR producer coverage/confidence improved substantially.
- Forced SSR is now useful for inspection, but it remains visibly more
  motion-sensitive than the scene-local auto path.
- Auto policy should remain local-probe-first until SSR gets temporal
  stabilization/history-aware confidence or an RT/ray-query fallback blend.

### Full AAA Scene Shader Refactor Execution Plan - 2026-06-06

User pivot:

- Move from isolated reflection/flicker patches to a full scene shader plan for
  high-end, Unreal-style visuals.
- Plan the entire refactor before completing the goal feature.
- Keep default beauty unchanged until candidate evidence and user review pass.

Durable plan update:

- Updated `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` with
  `2026-06-06 Full AAA Scene Shader Refactor Execution Plan`.
- The plan defines the V3 north star:
  visibility/depth/motion -> material payload -> scene-local environment ->
  lighting/shadow -> reflection/transparency/media -> candidate HDR composite
  -> cinematic post -> candidate LDR beauty.
- The plan locks the ownership ladder for every feature:
  contract name, render-graph resource, producer, consumer, debug view,
  frame-report proof, packet metrics, contact sheet, and promotion gate.

Current architecture boundary:

- Build on:
  - real `FullSceneReflectionV3` source-aware outputs.
  - `reflection_ssr_source_signal`.
  - refined `SSR.hlsl` source coverage.
  - `FullSceneReflectionHistoryV3` current/previous/validity resources.
  - existing V3 packet tooling.
- Still incomplete:
  - Composite V3 and Cinematic Post V3 are still adapter-like.
  - Material payload is not yet a complete concrete PBR resource set.
  - Lighting is not yet split into stable direct/shadow/indirect/emissive
    resources.
  - Scene-local environment is not yet a complete texture-backed
    irradiance/specular/background system.
  - SSR remains too motion-sensitive to dominate auto source policy.

Implementation order chosen:

1. finish ReflectionHistoryV3 reprojection validity.
2. add RT/ray-query reflection source signal and source-fusion diagnostics.
3. convert Composite V3 from adapter to real `candidate_hdr_scene_color`.
4. promote material payload from aggregate contract to concrete PBR resources.
5. make SceneLocalEnvironmentV3 texture-backed for enclosed rooms.
6. split Lighting V3 into direct/shadow/indirect/emissive resources.
7. build CinematicPostV3 on top of candidate HDR.
8. run the cross-family matrix and iterate only on failing gates.

Do not drift:

- Do not chase a single prettier screenshot.
- Do not hide reflection or shadow issues with IBL blur, source disabling, or
  scene/camera changes.
- Do not promote default beauty before candidate resources, debug views,
  frame-report fields, packet metrics, contact sheets, and user review pass.

### ReflectionHistoryV3 Reprojection Validity - 2026-06-06

Implemented:

- `FullSceneReflectionHistoryV3` now reads geometry/motion inputs:
  `depth`, normal/roughness, and `velocity`.
- The render-graph pass contract fails if those inputs are missing.
- Descriptor table expanded from `4` SRVs to `7` SRVs:
  reflection radiance, source ID, temporal delta, previous history, depth,
  normal/roughness, and velocity.
- `FullSceneReflectionHistoryV3.hlsl` now samples previous history at
  `uv + velocity + g_TAAParams.xy`.
- Reprojection validity uses the same acceptance family as the temporal
  rejection mask:
  - bounds acceptance.
  - depth agreement.
  - normal agreement.
  - motion-speed taper.
- `reflection_history_v3_validity` now packs:
  - R: current reflection active.
  - G: source class.
  - B: reusable reprojected previous history.
  - A: rejection/debt strength.
- `FullSceneShaderFrameContext.h` and
  `tools/analyze_full_scene_shader_v3_placeholders.py` now require
  `FullSceneReflectionHistoryV3` to read `depth`, `velocity`, and a
  normal/roughness resource.
- `assets/final_art/full_scene_shader_pipeline_v3_contract.json` now records
  `required_history_inputs` for the reflection domain.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` validates those
  history inputs.
- Documentation updated in:
  - `docs/FULL_SCENE_SHADER_PIPELINE_V3.md`.
  - `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_history_reprojection_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_history_reprojection_motion_smoke1_20260606
```

Results:

- Static packet passed:
  `build/captures/v3_reflection_history_reprojection_static_smoke1_20260606`.
- Mouse-jitter packet passed:
  `build/captures/v3_reflection_history_reprojection_motion_smoke1_20260606`.
- V3 promotion decision status remains `review_packet_passed`;
  `default_beauty_promotable=false`.
- Frame-report proof from the history-validity row:
  - `reflection_history_v3_ready=true`.
  - `reflection_history_v3_prev_ready=true`.
  - `reflection_history_v3_validity_ready=true`.
  - `reflection_v3_channel_count=10`.
  - `FullSceneReflectionHistoryV3.executed=true`.
  - `FullSceneReflectionHistoryV3.reads=reflection_radiance,
    reflection_source_id, reflection_temporal_delta,
    reflection_history_v3_prev, depth, vb_gbuffer_normal_roughness,
    velocity`.
  - `FullSceneReflectionHistoryV3.writes=reflection_history_v3_curr,
    reflection_history_v3_validity`.
- Mouse-jitter history metrics:
  - `reflection_history_v3_curr.mean_abs_luma_delta=0.0048307`,
    active `0.0309180`.
  - `reflection_history_v3_prev.mean_abs_luma_delta=0.0048307`,
    active `0.0309180`.
  - `reflection_history_v3_validity.mean_abs_luma_delta=0.0046699`,
    active `0.0435406`.

Current limitation:

- This pass produces advisory validity only.
- Reflection source admission is still conservative; do not use this slice to
  make SSR or RT win more often yet.
- Next aligned work is source-switch/disocclusion counters, then using
  history validity to bound SSR/RT/local-probe source fusion.

### ReflectionHistoryV3 Source Carryover and Rejection Diagnostics - 2026-06-06

Implemented:

- `FullSceneReflectionHistoryV3` now carries previous-frame
  `reflection_source_id` through `reflection_history_v3_prev_source_id`.
- The history shader now writes a third MRT:
  `reflection_history_v3_rejection`.
- `reflection_history_v3_rejection` packs:
  - R: source switch.
  - G: disocclusion/depth/normal/bounds rejection.
  - B: high-motion rejection.
  - A: out-of-bounds, forced-unavailable, or missing-history debt.
- Debug view `78` is `FullSceneReflectionHistoryV3Rejection`.
- Frame-report contract now requires:
  - `reflection_history_v3_prev_source_id_ready`.
  - `reflection_history_v3_rejection_ready`.
  - `reflection_v3_channel_count=12`.
- Packet default views now include `reflection_history_v3_rejection`.
- The analyzer now fails if:
  - history does not read `reflection_history_v3_prev_source_id`.
  - history does not write `reflection_history_v3_rejection`.
  - history copy does not read `reflection_source_id`.
  - history copy does not write `reflection_history_v3_prev_source_id`.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -C z:\328\CMPUT328-A2\codexworks\301\graphics diff --check -- <explicit V3 reflection-history files>
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_history_rejection_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_history_rejection_motion_smoke1_20260606
```

Results:

- Static packet passed:
  `build/captures/v3_reflection_history_rejection_static_smoke1_20260606`.
- Mouse-jitter packet passed:
  `build/captures/v3_reflection_history_rejection_motion_smoke1_20260606`.
- Motion packet frame-report proof from
  `gallery/reflection_history_v3_rejection/frame_report_shutdown.json`:
  - `reflection_v3_ready=true`.
  - `reflection_v3_channel_count=12`.
  - `reflection_history_v3_prev_source_id_ready=true`.
  - `reflection_history_v3_rejection_ready=true`.
  - `default_beauty_affects=false`.
  - status remains `planned_not_promoted`.
- `FullSceneReflectionHistoryV3.executed=true`.
- `FullSceneReflectionHistoryV3.reads` include:
  `reflection_radiance`, `reflection_source_id`,
  `reflection_temporal_delta`, `reflection_history_v3_prev`,
  `reflection_history_v3_prev_source_id`, `depth`,
  `vb_gbuffer_normal_roughness`, and `velocity`.
- `FullSceneReflectionHistoryV3.writes` include:
  `reflection_history_v3_curr`, `reflection_history_v3_validity`, and
  `reflection_history_v3_rejection`.
- `FullSceneReflectionHistoryV3Copy.reads` include:
  `reflection_history_v3_curr` and `reflection_source_id`.
- `FullSceneReflectionHistoryV3Copy.writes` include:
  `reflection_history_v3_prev` and
  `reflection_history_v3_prev_source_id`.
- `reflection_history_v3_prev_source_id` and
  `reflection_history_v3_rejection` are valid render-resolution resources.
- Mouse-jitter motion metrics:
  - `reflection_history_v3_curr.delta=0.00483072`, active `0.03091797`.
  - `reflection_history_v3_validity.delta=0.00466985`, active
    `0.04354058`.
  - `reflection_history_v3_rejection.delta=0.02864249`, active
    `0.10860352`.

Current limitation:

- This is still diagnostic/advisory. It does not yet change reflection source
  selection or default beauty.
- Next aligned pass is source-fusion admission: use validity plus rejection
  lanes to bound when SSR/RT/local-probe sources may win on smooth/metallic
  surfaces.

### ReflectionV3 History-Aware Source Admission - 2026-06-06

Implemented:

- `FullSceneReflectionV3` now reads prior-frame history signals before source
  admission:
  - `reflection_history_v3_prev_source_id`.
  - `reflection_history_v3_validity`.
  - `reflection_history_v3_rejection`.
- `FullSceneReflectionResolverV3.hlsl` samples those resources at `t3-t5`.
- Auto SSR/RT admission now applies a bounded source-switch penalty when prior
  history reports low reusable history, rejection debt, or recent source
  switching.
- Forced SSR/RT/local/environment debug source modes still bypass the
  hysteresis gate so source packets remain inspectable.
- `reflection_rejected_source_mask.a` now reports auto SSR/RT suppression by
  source-history hysteresis.
- Runtime readiness now requires `FullSceneReflectionV3` to read the three
  history inputs.
- The V3 JSON contract has `reflection.required_resolver_inputs` for:
  `local_reflection_radiance`, `ssr_color`, `rt_reflection`,
  `reflection_history_v3_prev_source_id`,
  `reflection_history_v3_validity`, and
  `reflection_history_v3_rejection`.
- The V3 analyzer and plan validator enforce those resolver inputs.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_source_fusion_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 36 -CaptureFrame 18 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_source_fusion_motion_smoke1_20260606
python tools\analyze_full_scene_shader_v3_lighting_motion.py --manifest build\captures\v3_reflection_source_fusion_motion_smoke1_20260606\manifest.json --output-json build\captures\v3_reflection_source_fusion_motion_smoke1_20260606\v3_lighting_motion.json --output-md build\captures\v3_reflection_source_fusion_motion_smoke1_20260606\v3_lighting_motion.md --min-sequence-count 2
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_reflection_source_fusion_motion_smoke1_20260606 --output-json build\captures\v3_reflection_source_fusion_motion_smoke1_20260606\promotion_decision.json --output-md build\captures\v3_reflection_source_fusion_motion_smoke1_20260606\promotion_decision.md --allow-subset-review
```

Results:

- Static packet passed:
  `build/captures/v3_reflection_source_fusion_static_smoke1_20260606`.
- Mouse-jitter render/analyzer packet produced complete frame reports and
  `v3_signal`/`v3_stability`, then hit the outer command timeout before final
  motion/promotion analyzers.
- Final motion analyzer and promotion analyzer were run directly on the
  generated motion packet artifacts and passed.
- Motion packet promotion result:
  `review_packet_passed`, `default beauty promotable=false`.
- Motion packet frame-report proof from
  `gallery/reflection_history_v3_rejection/frame_report_shutdown.json`:
  - `reflection_v3_ready=true`.
  - `reflection_v3_channel_count=12`.
  - `default_beauty_affects=false`.
  - status remains `planned_not_promoted`.
- `FullSceneReflectionV3.reads`:
  `local_reflection_radiance`, `ssr_color`, `rt_reflection`,
  `reflection_history_v3_prev_source_id`,
  `reflection_history_v3_validity`, and
  `reflection_history_v3_rejection`.
- `FullSceneReflectionV3.writes`:
  `reflection_radiance`, `reflection_confidence`,
  `reflection_source_id`, `reflection_rejected_source_mask`,
  `reflection_temporal_delta`, `reflection_ssr_source_signal`, and
  `reflection_rt_source_signal`.
- Mouse-jitter metrics from
  `build/captures/v3_reflection_source_fusion_motion_smoke1_20260606/v3_lighting_motion.md`:
  - `reflection_radiance.delta=0.00506397`, active `0.03371419`.
  - `reflection_source_id.delta=0.00790327`, active `0.03199653`.
  - `reflection_rejected_source_mask.delta=0.00052600`, active
    `0.00758572`.
  - `reflection_ssr_source_signal.delta=0.00584656`, active
    `0.05169488`.
  - `reflection_rt_source_signal.delta=0.00482639`, active `0.02688911`.
  - `reflection_history_v3_validity.delta=0.00639759`, active
    `0.05067166`.
  - `reflection_history_v3_rejection.delta=0.02947639`, active
    `0.11044271`.

Current limitation:

- This is the first admission-control slice, not final source fusion.
- It only penalizes auto SSR/RT on history/source-switch debt. It does not yet
  blend sources, add per-material roughness weighting, or promote candidate
  beauty.
- Next aligned pass: add material/roughness-aware reflection source weighting
  and verify on glossy/metal/glass closeups, then run broader family packets.

### ReflectionV3 Material-Aware Source Weighting - 2026-06-06

Implemented:

- `FullSceneReflectionV3` now reads material payload resources:
  - `vb_gbuffer_normal_roughness`.
  - `vb_gbuffer_emissive_metallic`.
- `FullSceneReflectionResolverV3.hlsl` samples:
  - `g_NormalRoughness : t6`.
  - `g_EmissiveMetallic : t7`.
- Auto SSR/RT confidence is weighted by roughness/metallic:
  - rough surfaces damp sharp SSR/RT source eligibility.
  - smooth/metallic surfaces keep stronger SSR/RT eligibility when history
    allows it.
  - rough nonmetallic surfaces get a small local/environment confidence boost.
- `reflection_rejected_source_mask.a` now reports either history suppression or
  material suppression of an otherwise active SSR/RT source.
- Runtime readiness now requires `FullSceneReflectionV3` to read a
  normal/roughness resource and `vb_gbuffer_emissive_metallic`.
- The V3 JSON contract and plan validator require those resolver inputs.
- The V3 analyzer fails if the resolver stops reading material roughness or
  emissive/metallic.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_material_weighting_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 30 -CaptureFrame 15 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_material_weighting_motion_smoke1_20260606
```

Results:

- Static packet passed:
  `build/captures/v3_reflection_material_weighting_static_smoke1_20260606`.
- Mouse-jitter packet passed:
  `build/captures/v3_reflection_material_weighting_motion_smoke1_20260606`.
- Motion packet promotion result:
  `review_packet_passed`, `default beauty promotable=false`.
- Motion packet frame-report proof from
  `gallery/reflection_rejected_source_mask/frame_report_shutdown.json`:
  - `reflection_v3_ready=true`.
  - `reflection_v3_channel_count=12`.
  - `default_beauty_affects=false`.
  - status remains `planned_not_promoted`.
- `FullSceneReflectionV3.reads`:
  `local_reflection_radiance`, `ssr_color`, `rt_reflection`,
  `reflection_history_v3_prev_source_id`,
  `reflection_history_v3_validity`,
  `reflection_history_v3_rejection`,
  `vb_gbuffer_normal_roughness`, and
  `vb_gbuffer_emissive_metallic`.
- Mouse-jitter metrics:
  - `reflection_radiance.delta=0.01129032`, active `0.06415907`.
  - `reflection_source_id.delta=0.00977111`, active `0.05311198`.
  - `reflection_rejected_source_mask.delta=0.00299466`, active
    `0.04688477`.
  - `reflection_ssr_source_signal.delta=0.02210732`, active `0.16835503`.
  - `reflection_rt_source_signal.delta=0.03265910`, active `0.09600803`.
  - `reflection_history_v3_validity.delta=0.00757453`, active
    `0.08257053`.
  - `reflection_history_v3_rejection.delta=0.04521344`, active
    `0.12818034`.

Current limitation:

- This is material-aware weighting, not a full BRDF reflection model.
- It does not yet add material-class-specific source policy for glass, water,
  clearcoat, anisotropy, or transmissive surfaces.
- Next aligned pass: add closeup stress packets for glossy metal/glass/rough
  dielectric surfaces and split material suppression from history suppression
  into its own debug channel or resource before candidate beauty promotion.

### ReflectionV3 Source Suppression Diagnostics - 2026-06-06

Implemented:

- `FullSceneReflectionV3` now writes `reflection_source_suppression` as an
  eighth resolver output.
- Debug mode `79` exposes `FullSceneReflectionV3SourceSuppression`.
- Channel contract:
  - `R`: history/source-switch suppression.
  - `G`: material/roughness suppression.
  - `B`: roughness.
  - `A`: metallic.
- The frame contract, resource snapshot, pass write list, memory accounting,
  V3 runtime context, JSON report, V3 contract JSON, analyzers, and packet
  runners now require and capture the resource.
- Reflection V3 readiness now requires `13` channels.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_lighting_motion.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -C z:\328\CMPUT328-A2\codexworks\301\graphics diff --check -- CortexEngine/assets/final_art/full_scene_shader_pipeline_v3_contract.json CortexEngine/assets/shaders/FullSceneReflectionResolverV3.hlsl CortexEngine/docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md CortexEngine/docs/FULL_SCENE_SHADER_PIPELINE_V3.md CortexEngine/src/Graphics/FrameContractJson.cpp CortexEngine/src/Graphics/FullSceneShaderFrameContext.h CortexEngine/src/Graphics/RendererMainTargetState.h CortexEngine/src/Graphics/Renderer_DebugSettings.cpp CortexEngine/src/Graphics/Renderer_FrameContractMemory.cpp CortexEngine/src/Graphics/Renderer_FrameContractPasses.cpp CortexEngine/src/Graphics/Renderer_FrameContractSnapshot.cpp CortexEngine/src/Graphics/Renderer_RenderGraphEndFrame.cpp CortexEngine/src/Graphics/Renderer_ScreenComputePipelineSetup.cpp CortexEngine/tools/analyze_full_scene_shader_v3_lighting_motion.py CortexEngine/tools/analyze_full_scene_shader_v3_placeholders.py CortexEngine/tools/check_full_scene_shader_pipeline_v2_frame_report.py CortexEngine/tools/run_full_scene_shader_pipeline_v3_packet.ps1 CortexEngine/tools/run_scene_local_cinematic_renderer_v1_packets.ps1 CortexEngine/tools/validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_reflection_source_suppression_static_smoke1_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 30 -CaptureFrame 15 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_source_suppression_motion_smoke1_20260606
```

Results:

- Static packet passed:
  `build/captures/v3_reflection_source_suppression_static_smoke1_20260606`.
- Mouse-jitter packet passed:
  `build/captures/v3_reflection_source_suppression_motion_smoke1_20260606`.
- Motion packet promotion result:
  `review_packet_passed`, `default beauty promotable=false`.
- Motion packet frame-report proof from
  `gallery/reflection_source_suppression/frame_report_shutdown.json`:
  - `reflection_v3_ready=true`.
  - `reflection_v3_channel_count=13`.
  - `reflection_source_suppression_ready=true`.
  - `default_beauty_affects=false`.
  - status remains `planned_not_promoted`.
- `reflection_source_suppression` resource:
  - `valid=true`.
  - `size_matches_contract=true`.
  - `width=1088`.
  - `height=612`.
- `FullSceneReflectionV3.reads`:
  `local_reflection_radiance`, `ssr_color`, `rt_reflection`,
  `reflection_history_v3_prev_source_id`,
  `reflection_history_v3_validity`, `reflection_history_v3_rejection`,
  `vb_gbuffer_normal_roughness`, and `vb_gbuffer_emissive_metallic`.
- `FullSceneReflectionV3.writes`:
  `reflection_radiance`, `reflection_confidence`,
  `reflection_source_id`, `reflection_rejected_source_mask`,
  `reflection_temporal_delta`, `reflection_ssr_source_signal`,
  `reflection_rt_source_signal`, and `reflection_source_suppression`.
- Mouse-jitter metrics:
  - `reflection_radiance.delta=0.01129032`, active `0.06415907`.
  - `reflection_source_id.delta=0.00977111`, active `0.05311198`.
  - `reflection_rejected_source_mask.delta=0.00299466`, active
    `0.04688477`.
  - `reflection_source_suppression.delta=0.07844539`, active
    `0.17913628`.

Current limitation:

- This is a diagnostic split, not a beauty promotion or final BRDF model.
- It proves why SSR/RT sources are suppressed, but it does not yet tune the
  suppression policy for glass, water, clearcoat, anisotropy, or transmission.
- Next aligned pass: run closeup stress packets for glossy metal, glass, rough
  dielectric, and water; then use `reflection_source_suppression` to tune
  material-class-specific reflection policy before candidate beauty promotion.

### ReflectionV3 Semantic Material Input and Pixel-Exact Reads - 2026-06-06

Implemented:

- Added `tools/analyze_reflection_v3_material_stress.py`.
- Added `tools/run_reflection_v3_material_stress_packet.ps1`.
- `FullSceneReflectionV3` now reads `vb_gbuffer_material_ext2`.
- `FullSceneReflectionResolverV3.hlsl` decodes surface class and named scene
  material class, then applies source floors for water, glass, mirror,
  conductor, and wet surfaces.
- `FullSceneReflectionResolverV3.hlsl` now loads normal/roughness,
  emissive/metallic, and material ext2 by exact pixel coordinate.
- `LocalReflectionRadiance.hlsl` now loads normal/roughness, emissive/metallic,
  material ext1, and material ext2 by exact pixel coordinate.
- The V3 contract JSON, runtime readiness, placeholder analyzer, and plan
  validator now require `FullSceneReflectionV3` to read
  `vb_gbuffer_material_ext2`.
- The material stress wrapper captures `surface_class` and `material_family` by
  default.
- The material stress analyzer reports smooth-class coverage from frame-report
  material policy counts.

Validation:

```powershell
python -m py_compile tools\analyze_reflection_v3_material_stress.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
Copy-Item -LiteralPath assets\shaders\LocalReflectionRadiance.hlsl -Destination build\bin\assets\shaders\LocalReflectionRadiance.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_material_stress_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneFilter "glass_water_courtyard:water_closeup" -ViewFilter "beauty,roughness,metallic,surface_class,material_family,reflection_source_suppression,reflection_ssr_source_signal,reflection_rt_source_signal,reflection_history_v3_rejection" -OutputRoot build\captures\reflection_v3_material_policy_water_after_pixel_loads_20260606 -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_reflection_v3_material_stress_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneFilter "material_lab:metal_closeup,material_lab:glass_emissive" -ViewFilter "beauty,roughness,metallic,surface_class,material_family,reflection_source_suppression,reflection_ssr_source_signal,reflection_rt_source_signal,reflection_history_v3_rejection" -OutputRoot build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606 -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter
python tools\analyze_reflection_v3_material_stress.py --manifest build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606\manifest.json --output-json build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606\reflection_v3_material_stress.json --output-md build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606\reflection_v3_material_stress.md
```

Results:

- Water packet passed the wrapper and material stress analyzer:
  `build/captures/reflection_v3_material_policy_water_after_pixel_loads_20260606`.
- Water still reports `smooth_target_has_high_roughness_signal`:
  material suppression `0.27838`, roughness mean `0.72708`, smooth-class
  coverage `31/66`.
- Metal/glass packet captured successfully; the V3 placeholder gate failed on
  material-lab lighting split readiness, but the material stress analyzer
  passed with no warnings:
  `build/captures/reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606`.
- Treat the pixel-exact material reads as a real root stability fix for
  smooth/metal edge jitter. Treat the remaining water warning as a BRDF/source
  policy issue, not missing `materialExt2` wiring.

Next aligned work:

- Add water/glass roughness policy diagnostics that distinguish authored
  material roughness from screen-view closeup composition.
- Tune water/glass source admission and local reflection radiance confidence
  using `surface_class`, `scene_material_class`, clearcoat/transmission, and
  Fresnel.
- Fix the material-lab diagnostic wrapper so it can run material stress without
  requiring full lighting-split promotion readiness.

### ReflectionV3 Material Stress Water Ownership Correction - 2026-06-06

Implemented:

- `tools/analyze_reflection_v3_material_stress.py` now reports center-ROI
  metrics for debug-view captures.
- Water targets now use `frame_contract.water.roughness` when
  `frame_contract.water.surface_count > 0`.
- The analyzer still reports opaque G-buffer roughness, but it no longer fails
  a water-pass target because the opaque G-buffer center is rough.

Reason:

- `glass_water_courtyard:water_closeup` showed opaque roughness center
  `0.75008`, but the frame contract proved the water pass owned the target:
  `water.surface_count=1`, `water.roughness=0.03`.
- The previous warning was therefore a harness ownership bug, not a water BRDF
  failure.

Validation:

```powershell
python -m py_compile tools\analyze_reflection_v3_material_stress.py
python tools\analyze_reflection_v3_material_stress.py --manifest build\captures\reflection_v3_material_policy_water_after_pixel_loads_20260606\manifest.json --output-json build\captures\reflection_v3_material_policy_water_after_pixel_loads_20260606\reflection_v3_material_stress.json --output-md build\captures\reflection_v3_material_policy_water_after_pixel_loads_20260606\reflection_v3_material_stress.md
python tools\analyze_reflection_v3_material_stress.py --manifest build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606\manifest.json --output-json build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606\reflection_v3_material_stress.json --output-md build\captures\reflection_v3_material_policy_metal_glass_after_pixel_loads_20260606\reflection_v3_material_stress.md
```

Results:

- water packet now passes with `warnings=0`; row reports
  `Rough Center=0.75008` and `Target Rough=0.03000`.
- metal/glass packet still passes with `warnings=0`.
- Next actual renderer work remains source/BRDF quality, but this harness gate
  now respects pass ownership.

### CompositeV3 Reflection Confidence Input - 2026-06-06

Implemented:

- `FullSceneCompositeV3.hlsl` now reads `reflection_confidence` alongside
  `reflection_radiance`.
- Candidate HDR reflection contribution is weighted by the actual
  ReflectionV3 resolver confidence instead of re-deriving confidence from
  reflection luma.
- Render-graph CompositeV3 descriptor table expanded from 5 to 6 SRVs.
- CompositeV3 frame pass records now include `reflection_confidence` when the
  ReflectionV3 resolver path is active.
- The V3 JSON contract, runtime readiness, and placeholder analyzer now require
  `reflection_confidence` as a real CompositeV3 input.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -C z:\328\CMPUT328-A2\codexworks\301\graphics diff --check -- CortexEngine\assets\final_art\full_scene_shader_pipeline_v3_contract.json CortexEngine\assets\shaders\FullSceneCompositeV3.hlsl CortexEngine\src\Graphics\Renderer_RenderGraphEndFrame.cpp CortexEngine\src\Graphics\FullSceneShaderFrameContext.h CortexEngine\tools\analyze_full_scene_shader_v3_placeholders.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneCompositeV3.hlsl -Destination build\bin\assets\shaders\FullSceneCompositeV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_reflection_confidence_static_fullviews_20260606
```

Results:

- native build completed after a long link.
- full V3 static gallery packet passed:
  `build/captures/v3_composite_reflection_confidence_static_fullviews_20260606`.
- `promotion_decision.md` status: `review_packet_passed`,
  `default beauty promotable=false`.
- frame report evidence from
  `gallery/candidate_hdr_scene_color/frame_report_shutdown.json`:
  - `composite_v3_ready=true`.
  - `composite_v3_producer=FullSceneCompositeV3`.
  - `reflection_confidence_ready=true`.
  - `candidate_hdr_scene_color_owned_by_full_scene_composite_v3`.
  - `FullSceneCompositeV3` pass/resource sections include
    `reflection_confidence`.

Current limitation:

- CompositeV3 still keeps `hdr_color` as a bounded rescue/reference input.
- Next CompositeV3 work should add explicit candidate energy diagnostics and
  reduce legacy HDR fallback use, rather than claiming final beauty promotion.

### CompositeV3 Energy Diagnostics Output - 2026-06-06

Implemented:

- `FullSceneCompositeV3` now writes concrete diagnostic MRTs in addition to
  candidate HDR:
  - `candidate_hdr_scene_color`.
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.
- `energy_clamp_policy` lanes:
  - `R`: pre-clamp luma normalized to the 16.0 HDR clamp ceiling.
  - `G`: clamp mask.
  - `B`: clamp ratio.
  - `A`: legacy `hdr_color` rescue usage.
- `overbright_diagnostics` lanes:
  - `R`: overbright mask.
  - `G`: underlit mask.
  - `B`: legacy `hdr_color` rescue usage.
  - `A`: ReflectionV3 confidence.
- CompositeV3 resource allocation, render-graph import/write tracking,
  frame-contract resources, memory accounting, runtime readiness, and analyzer
  gates now require `energy_clamp_policy` and `overbright_diagnostics` when the
  real `FullSceneCompositeV3` producer is active.
- Debug modes:
  - `80`: `FullSceneCompositeV3EnergyClampPolicy`.
  - `81`: `FullSceneCompositeV3OverbrightDiagnostics`.
- Packet views:
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.
- Updated the legacy V2 frame-report checker debug-range token to `81u` so the
  expanded debug registry remains compatible with V2 evidence packets.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\check_full_scene_shader_pipeline_v2_frame_report.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneCompositeV3.hlsl -Destination build\bin\assets\shaders\FullSceneCompositeV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_energy_diagnostics_static_fullviews_20260606
```

Evidence:

- packet root:
  `build/captures/v3_composite_energy_diagnostics_static_fullviews_20260606`.
- packet status: `review_packet_passed`.
- `report_count=32`.
- `composite_v3_producer=FullSceneCompositeV3`.
- `FullSceneCompositeV3` pass writes:
  `candidate_hdr_scene_color`, `energy_clamp_policy`, and
  `overbright_diagnostics`.
- all three resources are valid and size-matched at `1088x612`.
- debug metrics:
  - `candidate_hdr_scene_color.mean_luma=0.4412127`,
    `nonblack_ratio=0.9693370`.
  - `energy_clamp_policy.mean_luma=0.0063769`,
    `nonblack_ratio=0.8010699`.
  - `overbright_diagnostics.mean_luma=0.0654600`,
    `nonblack_ratio=0.2378244`.

Current limitation:

- This is still gallery/static evidence only.
- Default beauty remains unchanged and not promotable.
- Next CompositeV3 work should use the new diagnostics to reduce measured
  legacy HDR rescue usage and then run mouse-jitter/camera-sweep packets.

### CompositeV3 Diagnostic Gate - 2026-06-06

Implemented:

- Added `tools/analyze_full_scene_shader_v3_composite_diagnostics.py`.
- The analyzer reads the packet manifest and measures the CompositeV3
  diagnostic debug captures:
  - `energy_clamp_policy`.
  - `overbright_diagnostics`.
- It emits:
  - `v3_composite_diagnostics.json`.
  - `v3_composite_diagnostics.md`.
- It fails missing diagnostic captures and severe clamp/legacy-rescue debt.
- It reports softer visual debt as warnings so quality work has numbers to
  improve instead of relying on screenshots.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now runs this analyzer
  automatically before the promotion decision.

Lane interpretation:

- `energy_clamp_policy.R`: pre-clamp luma.
- `energy_clamp_policy.G`: clamp mask.
- `energy_clamp_policy.B`: clamp ratio.
- `overbright_diagnostics.R`: overbright.
- `overbright_diagnostics.G`: underlit.
- `overbright_diagnostics.B`: legacy HDR rescue usage.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_composite_diagnostics.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\analyze_full_scene_shader_v3_composite_diagnostics.py --manifest build\captures\v3_composite_energy_diagnostics_static_fullviews_20260606\manifest.json --output-json build\captures\v3_composite_energy_diagnostics_static_fullviews_20260606\v3_composite_diagnostics.json --output-md build\captures\v3_composite_energy_diagnostics_static_fullviews_20260606\v3_composite_diagnostics.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_diagnostics_gate_static_gallery_20260606
```

Evidence:

- direct analyzer pass on
  `build/captures/v3_composite_energy_diagnostics_static_fullviews_20260606`:
  - `ready=true`.
  - failures `0`.
  - warnings `0`.
  - `mean_clamp_mask=0.000045`.
  - `mean_clamp_ratio=0.000011`.
  - `mean_legacy_rescue=0.048630`.
  - `mean_underlit=0.083867`.
  - `mean_overbright=0.009254`.
- integrated packet:
  `build/captures/v3_composite_diagnostics_gate_static_gallery_20260606`.
  - `Scene-local cinematic renderer packet run passed`.
  - `Full Scene Shader Pipeline V2 packet evidence passed`.
  - `PASS: Full Scene Shader Pipeline V3 placeholder packet artifacts are coherent`.
  - `PASS: CompositeV3 diagnostics are measurable`.
  - `PASS: V3 promotion decision status=review_packet_passed`.

Current limitation:

- This gate measures gallery/static only in the latest integrated packet.
- It does not reduce legacy rescue usage yet; it establishes the baseline
  `mean_legacy_rescue=0.048630` to beat.
- Next renderer slice should reduce this fallback dependency in
  `FullSceneCompositeV3` and rerun static plus mouse-jitter/camera-sweep
  packets.

### Full Scene Shader AAA Refactor Planning Decision - 2026-06-06

User direction:

- move from local renderer patches toward full-scene shaders capable of
  high-end, Unreal-style visuals.
- plan the whole refactor before completing the next goal feature.
- do not treat blur, disabled IBL, hidden reflections, or scene changes as
  renderer fixes.

Planning update:

- `docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` now has a top-level
  `2026-06-06 Goal Feature Refactor Decision`.
- The next feature is defined as an opt-in `FullSceneCandidateBeautyV3`
  renderer path, not a single post effect or local shader tweak.
- The candidate path must assemble final pixels from owned V3 resources:
  material payload, scene-local environment, lighting/shadows, reflections,
  transparency/media, HDR composite, and cinematic post.
- Default beauty remains unchanged until promotion packets pass and the user
  accepts the result.

First implementation slice after planning:

```text
FullSceneCandidateBeautyV3 contract
  -> explicit target resource names
  -> candidate-only render graph ownership
  -> promotion gate rejects missing/legacy-owned resources
  -> packet output contains candidate HDR, candidate LDR, and domain evidence
```

Do next:

1. Finish the uncommitted CompositeV3 material-albedo / scene-local-floor
   rescue-reduction slice, document packet metrics, commit, and push.
2. Start the candidate-path scaffolding slice from the refactor plan.
3. Avoid jumping directly to bloom, tone mapping, or cinematic grade until
   the candidate path can honestly report which upstream V3 resources are real
   and which are still debt.

### CompositeV3 Material Albedo / Scene-Local Floor Slice - 2026-06-06

Implemented:

- `FullSceneCompositeV3` now reads `vb_gbuffer_albedo` through a concrete
  `MaterialAlbedo` render-graph import.
- The composite shader uses material albedo plus a small neutral scene-local
  floor before falling back to legacy `hdr_color`.
- CompositeV3 readiness, frame-context backing resources, analyzer tokens, and
  the V3 contract now require the material-albedo read edge.
- The legacy HDR fallback path remains present and measured, but it is no
  longer used in the tested gallery static/mouse-jitter packets.

Why:

- The previous diagnostic gate showed `mean_legacy_rescue=0.048630`, proving
  `FullSceneCompositeV3` still depended on legacy beauty for dark/no-term
  pixels.
- Material albedo and a bounded local floor make those pixels candidate-owned
  enough to inspect without hiding the remaining V3 renderer debt.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\analyze_full_scene_shader_v3_composite_diagnostics.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\FullSceneCompositeV3.hlsl -Destination build\bin\assets\shaders\FullSceneCompositeV3.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_composite_scene_floor_static_gallery_20260606
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 18 -CaptureFrame 9 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_composite_scene_floor_mouse_jitter_gallery_20260606
```

Evidence:

- baseline packet:
  `build/captures/v3_composite_diagnostics_gate_static_gallery_20260606`.
  - failures `0`, warnings `0`.
  - `mean_clamp_mask=0.000045`.
  - `mean_clamp_ratio=0.000011`.
  - `mean_legacy_rescue=0.048630`.
  - `mean_underlit=0.083867`.
  - `mean_overbright=0.009254`.
- static scene-floor packet:
  `build/captures/v3_composite_scene_floor_static_gallery_20260606`.
  - failures `0`, warnings `0`.
  - `mean_clamp_mask=0.000045`.
  - `mean_clamp_ratio=0.000011`.
  - `mean_legacy_rescue=0.000000`.
  - `mean_underlit=0.080436`.
  - `mean_overbright=0.009254`.
- mouse-jitter scene-floor packet:
  `build/captures/v3_composite_scene_floor_mouse_jitter_gallery_20260606`.
  - failures `0`, warnings `0`.
  - `mean_clamp_mask=0.000029`.
  - `mean_clamp_ratio=0.000009`.
  - `mean_legacy_rescue=0.000000`.
  - `mean_underlit=0.082492`.
  - `mean_overbright=0.009322`.

Frame-report proof:

- `FullSceneCompositeV3` reads now include:
  `direct_lighting`, `indirect_lighting`, `shadow_visibility`, `hdr_color`,
  `reflection_radiance`, `reflection_confidence`, and `vb_gbuffer_albedo`.
- V3 composite channels include `material_albedo_input_read`.

Current limitation:

- This is a CompositeV3 candidate-rescue reduction, not final AAA beauty.
- The scene-local floor is a bounded neutral fallback, not the final
  texture-backed `SceneLocalEnvironmentV3`.
- Evidence is gallery static/mouse-jitter only. Cross-family and camera-sweep
  packets are still required before candidate/default promotion.

### Candidate Beauty Strict Gate Scaffold - 2026-06-06

Implemented:

- `candidate_beauty` is now a required V3 contract domain in the plan
  validator.
- The machine-readable V3 contract now marks `hdr_color` and
  `ldr_cinematic_output` as `rejected_ready_inputs` for candidate beauty.
- Runtime candidate readiness now requires:
  - `CinematicPostV3` writes `candidate_ldr_cinematic_output`.
  - `CinematicPostV3` reads `candidate_hdr_scene_color`.
  - no legacy `hdr_color` bridge participates in the ready path.
- The candidate-beauty domain reports:
  - `candidate_reads_candidate_hdr_scene_color`.
  - `legacy_hdr_bridge_rejected` or `legacy_hdr_bridge_present`.
  - `default_beauty_unchanged`.
- The V3 placeholder analyzer and promotion decision builder now fail
  candidate-ready evidence that is not produced by `CinematicPostV3`.

Why:

- This is the first implementation slice of the AAA candidate renderer plan.
- It prevents the next goal feature from accidentally calling the old
  `hdr_color` bridge a real candidate beauty path.
- It keeps default beauty unchanged while giving later material, lighting,
  environment, reflection, composite, and post work a stricter admission gate.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\build_full_scene_shader_v3_promotion_decision.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 12 -CaptureFrame 6 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_candidate_beauty_strict_gate_static_gallery_20260606
```

Evidence:

- packet:
  `build/captures/v3_candidate_beauty_strict_gate_static_gallery_20260606`.
- `v3_signal.json`:
  - failures `0`, warnings `0`, rows `32`.
  - candidate requested reports `4`.
  - candidate ready reports `4`.
  - candidate producers: `CinematicPostV3`, `none`.
  - candidate outputs: `candidate_ldr_cinematic_output`, `none`.
- `v3_stability.json`:
  - report count `32`.
  - `default_beauty_affects_any=false`.
  - composite ready reports `32`.
  - cinematic post ready reports `32`.
- `promotion_decision.json`:
  - status `review_packet_passed`.
  - `default_beauty_promotable=false`.
  - failures `0`.
  - warnings `3`, all expected subset warnings: missing non-gallery families,
    missing mouse-jitter/camera-sweep, and sequence count below promotion
    evidence.
- `v3_composite_diagnostics.json`:
  - failures `0`, warnings `0`.
  - `mean_legacy_rescue=0.000000`.
  - `mean_underlit=0.080436`.
  - `mean_overbright=0.009254`.

Current limitation:

- This is a gate/scaffold slice, not visual-quality promotion.
- It proves the gallery/static candidate path uses the real
  `CinematicPostV3(candidate_hdr_scene_color -> candidate_ldr_cinematic_output)`
  contract.
- Cross-family and motion rows remain required before candidate/default
  promotion.

### V3 Material Payload Diagnostic Gate - 2026-06-06

Implemented:

- Added `tools/analyze_full_scene_shader_v3_material_payload.py`.
- The V3 packet default view set now captures material payload views:
  `roughness`, `metallic`, `surface_class`, `surface_policy`,
  `material_family`, `reflection_policy`, `temporal_policy`,
  `post_sensitivity`, `material_id`, and `object_id`.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` now emits:
  - `v3_material_payload.json`.
  - `v3_material_payload.md`.
- `tools/build_full_scene_shader_v3_promotion_decision.py` now requires
  `v3_material_payload.json` before a packet can pass review.
- `tools/analyze_full_scene_shader_v3_placeholders.py` now separates
  `material_payload` diagnostic reports from `full_pipeline` reports. Material
  debug views no longer have to prove lighting/reflection/post readiness, but
  full-pipeline reports still do.
- The V3 contract now requires material packet debug views and validation
  gates:
  - `material_payload_debug_views_present`.
  - `material_payload_ranges_valid`.

Why:

- Material V3 was previously too weak: the domain became ready when VB
  material resources existed, but packet evidence did not prove that material
  identity, policy, roughness, metallic, and object/material ID views were
  visible and in usable ranges.
- This gate makes material payload debt measurable before stronger lighting,
  reflection, composite, and post work depend on it.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_material_payload.py tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_material_payload_gate_static_gallery_retry_20260606
python tools\analyze_full_scene_shader_v3_material_payload.py --manifest build\captures\v3_material_payload_gate_static_gallery_retry_20260606\manifest.json --output-json build\captures\v3_material_payload_gate_static_gallery_retry_20260606\v3_material_payload.json --output-md build\captures\v3_material_payload_gate_static_gallery_retry_20260606\v3_material_payload.md
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_material_payload_gate_static_gallery_retry_20260606 --output-json build\captures\v3_material_payload_gate_static_gallery_retry_20260606\promotion_decision.json --output-md build\captures\v3_material_payload_gate_static_gallery_retry_20260606\promotion_decision.md --allow-subset-review
```

Evidence:

- packet:
  `build/captures/v3_material_payload_gate_static_gallery_retry_20260606`.
- `v3_signal.json`:
  - report count `42`.
  - full-pipeline reports `32`.
  - material-payload reports `10`.
  - ok reports `42`.
  - failures `0`, warnings `0`.
- `v3_stability.json`:
  - material ready reports `42`.
  - lighting ready reports `32`.
  - cinematic post ready reports `32`.
- `v3_material_payload.json`:
  - ready `true`.
  - failures `0`.
  - warnings `2`.
  - required debug views `9`, optional debug views `1`.
  - material report count `42`.
  - sampled materials total across reports `2520`.
  - named materials total across reports `2520`.
  - advanced feature materials total across reports `1344`.
  - reflection eligible total across reports `756`.
  - representative material stats per report:
    sampled `60`, named `60`, average roughness `0.5013`, average metallic
    `0.2167`, average albedo luminance `0.4559`.
  - material debug-view signals:
    roughness nonblack `1.00000`, surface class `1.00000`, material family
    `1.00000`, material ID `1.00000`, object ID `1.00000`, metallic nonblack
    `0.06651`.
- `promotion_decision.json`:
  - status `review_packet_passed`.
  - `default_beauty_promotable=false`.
  - failures `0`.
  - warnings `5`: two material fallback warnings plus expected subset
    warnings for missing non-gallery families, missing motion modes, and
    capture sequence count below promotion evidence.

Current limitation:

- This is gallery/static material payload evidence only.
- The gate proves material visibility and range sanity; it does not yet create
  new PBR material resources beyond the current VB material resolve outputs.
- Remaining material debt is explicit:
  `preset_default_roughness fallback count 8` and
  `preset_default_transmission fallback count 5`.
  The next material-quality slice should reduce those fallback counts or attach
  authored/provider-backed values.

### Material Class-Authored Defaults vs Unresolved Fallback - 2026-06-06

Implemented:

- Split material default counters into explicit semantics:
  - `preset_class_authored_default_roughness`.
  - `preset_class_authored_default_transmission`.
  - `unresolved_default_roughness_fallback`.
  - `unresolved_default_transmission_fallback`.
- `RendererSceneSnapshot` now records named material preset roughness and
  transmission overrides as class-authored material semantics.
- `FrameContractJson` exports the new counters into every frame report.
- `tools/analyze_full_scene_shader_v3_material_payload.py` now fails only on
  unresolved roughness/transmission fallback, while reporting class-authored
  defaults as material evidence.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` now requires the new
  frame-contract fields so future refactors cannot silently collapse the two
  meanings again.

Why:

- The previous `preset_default_roughness` and `preset_default_transmission`
  warnings were ambiguous. In the gallery packet, those counts came from named
  material presets intentionally overriding raw component defaults, not from
  missing material data.
- Treating authored preset values as fallback debt made the material gate noisy
  and hid the real future blocker: unresolved, provider-missing material scalar
  values.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_material_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\build_full_scene_shader_v3_promotion_decision.py
python -m json.tool assets\final_art\full_scene_shader_pipeline_v3_contract.json
python tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -NoStressScene -FamilyFilter gallery -SmokeFrames 10 -CaptureFrame 5 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_material_class_authored_defaults_static_gallery_20260606
```

Evidence:

- packet:
  `build/captures/v3_material_class_authored_defaults_static_gallery_20260606`.
- `v3_material_payload.json`:
  - ready `true`.
  - failures `0`.
  - warnings `0`.
  - sampled materials total `2520`.
  - named materials total `2520`.
  - advanced feature materials total `1344`.
  - reflection eligible total `756`.
  - class-authored roughness defaults `336`.
  - class-authored transmission defaults `210`.
  - unresolved roughness fallback `0`.
  - unresolved transmission fallback `0`.
- `promotion_decision.json`:
  - status `review_packet_passed`.
  - `default_beauty_promotable=false`.
  - failures `0`.
  - warnings `3`, limited to expected subset coverage: missing non-gallery
    families, missing motion modes, and sequence count below promotion
    evidence.

Current limitation:

- This is still gallery/static material-contract evidence only.
- Unresolved fallback attribution is now explicit, but the current gallery
  packet does not exercise a true unresolved provider-missing material case.
- The next AAA material slice should attach richer material resources and
  texture-backed scalar maps, then run the same unresolved fallback gate across
  the full family and motion matrix.

### ReflectionV3 Pixel-Exact Source Loads and Motion Warnings - 2026-06-06

Implemented:

- `FullSceneReflectionResolverV3.hlsl` now reads pixel-aligned source buffers
  with `Load()` instead of linear-filtered `Sample()`:
  - `local_reflection_radiance`.
  - `ssr_color`.
  - `rt_reflection`.
  - reflection history source-id, validity, and rejection masks.
- The resolver now emits continuous rejection/suppression/inactive debt for
  source diagnostics instead of fully binary source masks.
- `tools/analyze_full_scene_shader_v3_lighting_motion.py` now warns when
  reflection diagnostic masks move more than `1.75x` beauty with delta above
  `0.02`.
- `CMakeLists.txt` now explicitly appends the V3 runtime shader files to
  `CORTEX_ASSET_FILES` so future CMake regeneration tracks them for runtime
  asset sync.

Why:

- Smooth/metal reflection stability cannot depend on linearly filtered source
  IDs or binary rejection masks. That creates hard changes when camera motion
  shifts a source by a fraction of a pixel.
- The packet previously passed while reflection rejection/history masks moved
  much more than beauty. The analyzer now exposes that debt directly.

Validation:

```powershell
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606 --output-json build\captures\v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606\promotion_decision.json --output-md build\captures\v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606\promotion_decision.md --allow-subset-review
```

Evidence:

- before packet:
  `build/captures/v3_forced_ssr_reflection_pixel_loads_mouse_jitter_20260606`.
- after packet:
  `build/captures/v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606`.
- after packet status:
  - V3 placeholder packet passed.
  - material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision `review_packet_passed`.
- motion comparison:
  - `reflection_ssr_source_signal`: stable, delta `0.021202`, `0.797x`
    beauty.
  - `reflection_source_suppression`: improved from delta `0.060532`
    (`2.276x` beauty, warning) to `0.014887` (`0.560x` beauty, ok).
  - `reflection_temporal_delta`: improved from `0.076251` to `0.068744`,
    but still warning.
  - remaining warnings:
    `reflection_rejected_source_mask`, `reflection_temporal_delta`,
    `reflection_history_v3_validity`, and
    `reflection_history_v3_rejection`.

Current limitation:

- Reflection source suppression is now stable under the forced-SSR mouse-jitter
  probe, but the history/rejection validity path is still too motion-sensitive.
- Next reflection slice should stabilize `FullSceneReflectionHistoryV3`:
  source-class transitions, depth/normal rejection, and history validity should
  be bounded before ReflectionV3 source fusion is promoted further.
- The asset sync stamp did not copy shader-only edits in this session because
  earlier builds used `CORTEX_SKIP_ASSET_SYNC=1` and the generated Ninja file
  did not list this V3 shader. Verification manually copied the shader into
  `build/bin/assets/shaders`.
- A CMake source fix was added for future regeneration, but the full
  `CortexAssets` regeneration timed out on the large asset graph in this
  session. Treat the manual shader copy as part of this packet's reproduction
  until a normal configure/build refresh completes.

### ReflectionV3 History Confidence/Reprojection Stability - 2026-06-07

Implemented:

- `FullSceneReflectionHistoryV3.hlsl` now uses pixel-exact `Load()` for
  current-frame ReflectionV3 resources:
  - `reflection_radiance`.
  - `reflection_source_id`.
  - `reflection_temporal_delta`.
- The history pass now samples depth/normal at the reprojected UV instead of
  comparing against a nearest-neighbor current-frame point sample.
- Reprojection acceptance is less brittle:
  - depth falloff changed from `160.0` to `96.0`.
  - normal acceptance widened from `0.78/0.22` to `0.62/0.38`.
- History activity and reusable-history availability are now confidence-driven
  instead of radiance-luma-driven.
- Source-switch detection is now continuous with `smoothstep(0.04, 0.16, ...)`
  instead of a hard `step(0.08, ...)`.

Why:

- The previous pass treated the current frame as if it were previous-frame
  geometry and used point-sampled depth/normal at the reprojected UV. Under
  mouse jitter this made history validity and rejection masks move more than
  beauty.
- Confidence is a better history trust signal than radiance luma because bright
  reflection content can move across a stable reflective surface.

Validation:

```powershell
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionHistoryV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionHistoryV3.hlsl -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_history_confidence_validity_mouse_jitter_20260607
```

Evidence:

- baseline packet:
  `build/captures/v3_forced_ssr_reflection_continuous_masks_synced_mouse_jitter_20260606`.
- after packet:
  `build/captures/v3_reflection_history_confidence_validity_mouse_jitter_20260607`.
- after packet status:
  - V3 placeholder packet passed.
  - material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision `review_packet_passed`.
- motion comparison:
  - `reflection_history_v3_validity`: `0.053437 -> 0.052630`;
    active delta `0.197129 -> 0.185659`.
  - `reflection_history_v3_rejection`: `0.070312 -> 0.061797`;
    active delta `0.343471 -> 0.278238`.
  - `reflection_ssr_source_signal` remains stable: delta `0.021202`,
    `0.797x` beauty.
  - `reflection_source_suppression` remains stable: delta `0.014887`,
    `0.560x` beauty.
- after packet per-channel deltas:
  - `reflection_history_v3_validity`: RGB
    `[0.080439, 0.040978, 0.096091]`.
  - `reflection_history_v3_rejection`: RGB
    `[0.059277, 0.068931, 0.021108]`.

Current limitation:

- The history/rejection rows still warn:
  `reflection_rejected_source_mask`, `reflection_temporal_delta`,
  `reflection_history_v3_validity`, and
  `reflection_history_v3_rejection`.
- The remaining `reflection_rejected_source_mask` and
  `reflection_temporal_delta` motion is upstream in the resolver's forced-SSR
  availability/rejection channels, not fixed by the history pass alone.
- Disk was full during this pass; older reproducible V3 capture folders under
  `build/captures` were removed after verifying their paths were inside the
  repo capture directory.

### ReflectionV3 Resolver Continuous Forced-Availability Diagnostics - 2026-06-07

Implemented:

- `FullSceneReflectionResolverV3.hlsl` now reports forced-source
  unavailability as continuous availability debt instead of a hard binary
  `rawActive <= 0` flip.
- Forced SSR no longer zeroes `reflection_rejected_source_mask.g` just because
  SSR was selected; the diagnostic now carries continuous SSR availability
  debt in forced-SSR mode.

Why:

- The remaining resolver warnings came from the same hard forced-SSR
  availability channel:
  - `reflection_rejected_source_mask.g`.
  - `reflection_temporal_delta.g`.
- Both channels had RGB-G motion delta `0.082185` under mouse jitter. That was
  not a source-quality signal; it was a binary diagnostic threshold popping as
  SSR availability crossed a tiny epsilon.

Validation:

```powershell
Copy-Item -LiteralPath assets\shaders\FullSceneReflectionResolverV3.hlsl -Destination build\bin\assets\shaders\FullSceneReflectionResolverV3.hlsl -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 24 -CaptureFrame 12 -CaptureSequenceCount 2 -StabilityMotionMode mouse_jitter -OutputRoot build\captures\v3_reflection_resolver_continuous_forced_availability_mouse_jitter_20260607
```

Evidence:

- baseline packet:
  `build/captures/v3_reflection_history_confidence_validity_mouse_jitter_20260607`.
- after packet:
  `build/captures/v3_reflection_resolver_continuous_forced_availability_mouse_jitter_20260607`.
- after packet status:
  - V3 placeholder packet passed.
  - material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision `review_packet_passed`.
- motion comparison:
  - `reflection_rejected_source_mask`: `0.060360 -> 0.014533`;
    status warning -> ok.
  - `reflection_temporal_delta`: `0.068744 -> 0.022837`;
    status warning -> ok.
  - `reflection_ssr_source_signal`: remains ok at `0.021202`.
  - `reflection_source_suppression`: remains ok at `0.014887`.
  - remaining reflection warnings are now only
    `reflection_history_v3_validity` and `reflection_history_v3_rejection`.
- per-channel resolver diagnostic deltas:
  - `reflection_rejected_source_mask`: RGB
    `[0.010351, 0.082185, 0.004616] -> [0.010351, 0.018025, 0.004616]`.
  - `reflection_temporal_delta`: RGB
    `[0.047651, 0.082185, 0.0] -> [0.047651, 0.018025, 0.0]`.

Current limitation:

- Resolver-side rejected-source and temporal-delta warnings are cleared for
  forced-SSR mouse jitter, but history validity/rejection still warn.
- The remaining instability is in history/reprojection confidence, not forced
  source availability.

## 2026-06-07 Full Scene Shader Refactor Direction

The current user direction is to move toward full scene shaders for
Unreal-like visuals, but to plan the whole refactor before completing the goal
feature. The plan was updated in
`docs/FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md` under
`2026-06-07 Full Scene Shader Refactor Before Goal Feature Completion`.

Current decision:

- Build an opt-in `FullSceneCandidateBeautyV3` path, not a default-beauty
  shortcut.
- Treat the final candidate pixel as owned material payload, scene-local
  environment, lighting, shadows, indirect/emissive, source-aware reflections,
  transparent/media terms, HDR composite, and cinematic post.
- Do not hide problems by IBL blur, scene changes, post effects, or disabling
  reflection/shadow features.
- Legacy `hdr_color` may remain only as a named, measured rescue/comparison
  lane and must count as promotion debt.

Immediate implementation direction:

1. Use the new focused reflection motion packet runner before more shader
   tweaking.
   - Script:
     `tools/run_reflection_v3_motion_focus_packet.ps1`.
   - It reproduces the forced-SSR / smooth-metal mouse jitter case with only
     the reflection and beauty debug views needed.
   - It sets `CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT=1` and defaults
     `CORTEX_V3_REFLECTION_SOURCE_OVERRIDE=ssr`.
   - It writes `v3_reflection_motion_focus.json`,
     `v3_reflection_motion_focus.md`, and
     `v3_reflection_motion_focus_sheet.png`.
   - This avoids the current disk pressure from full 300 MB V3 packets.
2. Use that focused packet to attack the actual remaining blocker:
   `reflection_history_v3_validity` and `reflection_history_v3_rejection`.
3. After reflection history is stable, continue the planned layers:
   material payload hardening, scene-local environment split, lighting/shadow
   rebuild, reflection provider expansion, transparency/media integration,
   real CompositeV3, CinematicPostV3, then cross-family promotion.

Current constraints:

- `Z:` free space is critically low; last observed free space was about
  `0.53 GB`.
- Do not run another full V3 packet until old captures are safely pruned or a
  focused packet runner is in place.
- Focused files from the latest reflection commits are clean, but many
  unrelated dirty files remain in the worktree. Do not stage or revert them.

Latest harness status:

- `tools/analyze_full_scene_shader_v3_lighting_motion.py` supports
  `--focus reflection`.
- The focused runner has been exercised with real captures.
- Old generated capture folders pruned from `build/captures` after resolving
  paths inside the capture root:
  - `scene_local_cinematic_renderer_v1_targeted_micro_jitter_20260604`.
  - `scene_local_cinematic_renderer_v1_final_broad_audit_20260605`.
  - `scene_local_cinematic_renderer_v1_local_probe_procedural_radiance_20260604`.
- Free space after full validation was about `7.56 GB`.
- Lightweight validation to run after edits:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_lighting_motion.py tools\build_full_scene_shader_v2_review_sheet.py
$tokens = $errors = $null
[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_reflection_v3_motion_focus_packet.ps1), [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
git -c submodule.recurse=false diff --check -- tools\run_reflection_v3_motion_focus_packet.ps1 tools\analyze_full_scene_shader_v3_lighting_motion.py docs\FULL_SCENE_SHADER_AAA_REFACTOR_PLAN.md docs\AAA_ASSET_QUALITY_HANDOFF.md
```

## 2026-06-07 ReflectionV3 History Stability Resume Point

Implemented after the focused harness:

- `assets/shaders/FullSceneReflectionHistoryV3.hlsl` now emits
  confidence-weighted continuous history diagnostics.
- Current active history uses reflection confidence directly.
- History reusable uses previous history confidence times reprojection
  acceptance.
- Source-switch, disocclusion, high-motion, and out-of-bounds rejection are
  gated by current/previous reflection support, so non-reflective or
  low-history pixels stop becoming reflection-history debt.

Validation evidence:

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
  - V2 evidence passed.
  - V3 placeholder packet passed.
  - V3 lighting motion passed with `24` view sequences, `0` warnings,
    `0` failures.
  - V3 material payload passed.
  - CompositeV3 diagnostics passed.
  - promotion decision: `review_packet_passed`.

Current next work:

1. Do not promote default beauty; the full packet was stress-only and still
   reports missing families and motion modes.
2. Next renderer layer should be material payload hardening or the
   scene-local environment split, unless a new user-visible smooth/metal
   regression appears.

## 2026-06-07 Material Payload Contract Coverage Resume Point

Implemented:

- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1` exposes
  `material_base_color` and `material_normal` packet aliases.
- `tools/run_full_scene_shader_pipeline_v3_packet.ps1` includes those aliases
  in the default V3 packet view set.
- `tools/analyze_full_scene_shader_v3_material_payload.py` requires those
  views and reports contract-required material debug-view coverage.
- `tools/analyze_full_scene_shader_v3_placeholders.py` classifies debug modes
  `35` and `36` as material-payload scope, so material-only VB G-buffer debug
  captures do not falsely require lighting split execution.
- `assets/final_art/full_scene_shader_pipeline_v3_contract.json` lists
  `material_base_color` and `material_normal` in material packet views.

Validation evidence:

- packet:
  `build/captures/v3_material_payload_contract_views_stress_20260607`.
- V3 placeholder analyzer passed after the material debug-scope fix.
- V3 lighting motion passed with `24` view sequences.
- material payload passed with:
  - `sampled materials`: `2640`.
  - `named materials`: `2640`.
  - `advanced feature materials`: `1408`.
  - `unresolved roughness fallback`: `0`.
  - `unresolved transmission fallback`: `0`.
  - `contract required debug views`: `6`.
  - `contract debug view debt`: `1`.
  - covered: `material_base_color`, `material_roughness`,
    `material_metallic`, `material_normal`, `material_class`.
  - debt: `material_missing_channel_mask`.
- CompositeV3 diagnostics passed.
- promotion decision: `review_packet_passed`.
- default beauty remains not promotable because the packet is still a
  stress-only subset and lacks the required families/motion modes.

Current next work:

1. Continue material hardening by creating a real missing-channel-mask
   resource/debug view or a stricter equivalent frame-contract gate.
2. After material payload debt is explicit and shrinking, move to
   `SceneLocalEnvironmentV3` visible/background/reflection-environment split.

### Material Missing Channel Mask Debug Slice - 2026-06-07

Implemented:

- Added visibility debug mode `MaterialMissingChannelMask`.
- External debug view `82` maps to the new mode.
- Packet alias:
  `material_missing_channel_mask`.
- The shader computes the mask from the material table per visible pixel:
  - red: missing core texture evidence ratio
    (albedo, normal, metallic, roughness).
  - green: requested advanced feature missing texture ratio
    (occlusion, emissive, transmission, clearcoat, clearcoat roughness,
    specular, specular color).
  - blue: max fallback debt.
- This does not steal `MaterialExt2`; anisotropy, sheen, surface class, and
  named scene material class remain intact for lighting/reflection consumers.
- `tools/analyze_full_scene_shader_v3_material_payload.py` now treats
  `material_missing_channel_mask` as a required captured view instead of a
  missing packet alias.
- `tools/analyze_full_scene_shader_v3_placeholders.py` classifies debug view
  `82` as material-payload scope.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_material_payload.py tools\analyze_full_scene_shader_v3_placeholders.py
$tokens = $errors = $null
[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_scene_local_cinematic_renderer_v1_packets.ps1), [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
$tokens = $errors = $null
[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_full_scene_shader_pipeline_v3_packet.ps1), [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\shaders\DebugBlitVisibility.hlsl -Destination build\bin\assets\shaders\DebugBlitVisibility.hlsl -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipOwnerAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -ViewFilter material_base_color,material_normal,material_missing_channel_mask,roughness,metallic,surface_class,surface_policy,material_family,reflection_policy,temporal_policy,post_sensitivity,material_id,object_id -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -OutputRoot build\captures\v3_material_missing_channel_mask_material_only_20260607
python tools\analyze_full_scene_shader_debug_view_metrics.py --manifest build\captures\v3_material_missing_channel_mask_material_only_20260607\manifest.json --output-json build\captures\v3_material_missing_channel_mask_material_only_20260607\debug_view_metrics.json --output-md build\captures\v3_material_missing_channel_mask_material_only_20260607\debug_view_metrics.md
python tools\analyze_full_scene_shader_v3_material_payload.py --manifest build\captures\v3_material_missing_channel_mask_material_only_20260607\manifest.json --output-json build\captures\v3_material_missing_channel_mask_material_only_20260607\v3_material_payload.json --output-md build\captures\v3_material_missing_channel_mask_material_only_20260607\v3_material_payload.md
python tools\analyze_full_scene_shader_v3_placeholders.py --input build\captures\v3_material_missing_channel_mask_material_only_20260607 --signal-output build\captures\v3_material_missing_channel_mask_material_only_20260607\v3_signal.json --stability-output build\captures\v3_material_missing_channel_mask_material_only_20260607\v3_stability.json
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_material_missing_channel_mask_full_stress_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Native target follow-up: `ninja -C build CortexEngine -j 8` reported
  `no work to do`; the existing `vswhere.exe` warning printed after success.
- Material-only packet:
  `build\captures\v3_material_missing_channel_mask_material_only_20260607`.
  - debug-view metrics passed with `13` measured captures.
  - placeholder analyzer passed with `13` reports.
  - material payload passed with `0` failures and `0` warnings.
  - contract debug view debt: `0`.
- Full short stress packet:
  `build\captures\v3_material_missing_channel_mask_full_stress_20260607`.
  - V2 evidence passed.
  - V3 placeholder packet passed with `45` reports.
  - material payload passed:
    sampled materials `2700`, named materials `2700`, advanced feature
    materials `1440`, reflection eligible `810`.
  - `material_missing_channel_mask` captured with mean luma `0.56898` and
    nonblack ratio `1.00000`.
  - contract debug view debt: `0`.
  - CompositeV3 diagnostics passed.
  - promotion decision: `review_packet_passed`,
    `default_beauty_promotable=false` because this was a stress-only static
    subset.

Current next work:

1. Move to `SceneLocalEnvironmentV3`.
   - Split visible background, lighting environment, reflection environment,
     and atmosphere.
   - Keep IBL blur/post/scene swaps out of the root-cause strategy.
2. Continue material payload enrichment after environment split:
   texture-backed scalar maps, provider provenance, and broader family/motion
   packet coverage.

### SceneProfileV3 Adapter / Policy Evidence Slice - 2026-06-07

Implemented:

- Added the `scene_profile` V3 domain to
  `FullSceneShaderPipelineV3FrameContext`.
- The domain adapts the existing `SceneCinematicProfile` /
  `scene_visual_contract`; it does not introduce a parallel profile stack.
- Frame reports now expose:
  - `scene_profile_ready`
  - `scene_profile_policy_count`
  - `scene_profile_producer`
  - `scene_profile_output`
- The V3 contract now requires `v3_scene_profile.json` and the
  `scene_profile_policy_ready` /
  `scene_profile_family_differences_present` gates.
- Added `tools/analyze_full_scene_shader_v3_scene_profile.py`.
- Added `tools/run_scene_profile_v3_focus_packet.ps1`.
- Updated the placeholder analyzer, V3 packet runner, plan validator, and
  promotion-decision builder to account for the new domain.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_scene_profile.py tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
$files=@('tools\run_scene_profile_v3_focus_packet.ps1','tools\run_full_scene_shader_pipeline_v3_packet.ps1'); foreach ($file in $files) { $tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file), [ref]$tokens, [ref]$errors) | Out-Null; if ($errors.Count -gt 0) { Write-Host $file; $errors | Format-List; exit 1 } }
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_scene_profile.py tools\run_scene_profile_v3_focus_packet.ps1 tools\run_full_scene_shader_pipeline_v3_packet.ps1 tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_profile_v3_focus_packet.ps1 -NoBuild -OutputRoot build\captures\scene_profile_v3_focus_2fam_beauty_20260607 -FamilyFilter gallery,kitchen -ViewFilter beauty -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -MinFamilyCount 2
python tools\analyze_full_scene_shader_v3_scene_profile.py --manifest build\captures\scene_profile_v3_focus_2fam_beauty_20260607\manifest.json --output-json build\captures\scene_profile_v3_focus_2fam_beauty_20260607\v3_scene_profile_manual.json --output-md build\captures\scene_profile_v3_focus_2fam_beauty_20260607\v3_scene_profile_manual.md --min-family-count 2
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_profile_full_stress_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Plan validator passed with `10` V3 domains and `29` required outputs.
- Native target rebuilt successfully; the existing trailing `vswhere.exe`
  warning printed after success.
- Two-family scene-profile proof:
  `build\captures\scene_profile_v3_focus_2fam_beauty_20260607`.
  - The wrapper reported kitchen `exit_code=1` due to a DX12 device-hung
    renderer failure at frame 0 after HZB setup.
  - The run still wrote both frame reports and a manifest.
  - Manual scene-profile analysis passed:
    `families=2`, `profiles=2`, `light rigs=2`,
    `material palettes=2`, `failures=0`, `warnings=0`.
- Integrated full V3 stress packet:
  `build\captures\v3_scene_profile_full_stress_20260607`.
  - Scene-local packet run passed.
  - V2 evidence passed.
  - V3 placeholder artifacts passed with `54` reports.
  - Scene-profile analyzer passed with `54` reports, `0` failures, and
    `0` warnings.
  - Material payload and CompositeV3 diagnostics passed.
  - Promotion decision remains `blocked`: `scene_profile`, material,
    lighting, environment, and reflection are ready for the full-pipeline
    reports, but `composite` and `cinematic_post` are ready only on `6`
    reports while full-pipeline reports are `41`.

Current next work:

1. Do not treat `SceneProfileV3` as the blocker. The policy domain is wired,
   serialized, analyzer-covered, and packet-measurable.
2. Fix CompositeV3 / CinematicPostV3 readiness coverage so all full-pipeline
   debug views either carry the candidate composite/post domain correctly or
   are excluded by an explicit contract rule.
3. Keep the kitchen DX12 device-hung failure as a separate renderer stability
   issue. It is not a scene-profile contract failure, but it blocks reliable
   cross-family packets.
4. After composite/post coverage is coherent, continue the full scene shader
   refactor into texture-backed `SceneLocalEnvironmentV3` and richer
   material/environment payloads.

### Composite/Post Candidate-Scope Gate - 2026-06-07

Implemented:

- `assets/final_art/full_scene_shader_pipeline_v3_contract.json` now declares
  `readiness_scope: candidate_beauty_requested` for `composite` and
  `cinematic_post`.
- `tools/build_full_scene_shader_v3_promotion_decision.py` now treats
  `composite` and `cinematic_post` as candidate-only domains:
  - base domains (`scene_profile`, material, lighting, environment, reflection)
    are still required for every full-pipeline report.
  - candidate domains are required only for rows where
    `candidate_beauty_requested=true`.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` now validates that
  the contract and promotion script both preserve the candidate-only domain
  scope.

Validation:

```powershell
python -m py_compile tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\build_full_scene_shader_v3_promotion_decision.py --packet-root build\captures\v3_scene_profile_full_stress_20260607 --output-json build\captures\v3_scene_profile_full_stress_20260607\promotion_decision_candidate_scope.json --output-md build\captures\v3_scene_profile_full_stress_20260607\promotion_decision_candidate_scope.md --allow-subset-review
git -c submodule.recurse=false diff --check -- assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py
```

Evidence:

- Plan validator still passes with `10` V3 domains and `29` required outputs.
- Re-running the promotion decision on the existing full stress packet passed:
  `status=review_packet_passed`, `review_packet_passed=true`.
- Counts after the scope fix:
  - `scene_profile=41/41`
  - material `54/54`
  - lighting/environment/reflection `41/41`
  - composite/cinematic-post `6/6` for candidate-requested rows
  - candidate beauty ready `6/6`
- Default beauty remains non-promotable because the packet is stress-only,
  single-family, and static-only.

Fresh integrated packet:

- Deleted 55 generated `20260604` capture directories under `build\captures`
  after verifying each resolved path was under the capture root.
- Free space increased from about `108 MB` to about `33 GB`.
- Reran the integrated V3 packet:
  `build\captures\v3_candidate_scope_full_stress_20260607`.
- The packet passed end to end:
  - scene-local packet run passed.
  - V2 evidence passed.
  - V3 placeholder packet passed with `54` reports.
  - `v3_scene_profile.json`, `v3_material_payload.json`, and
    `v3_composite_diagnostics.json` passed.
  - promotion decision passed as `review_packet_passed`.

Current next work:

1. Use the candidate-scope gate as the correct promotion contract: do not force
   candidate composite/post resources onto upstream debug views.
2. Resume the full shader refactor with texture-backed
   `SceneLocalEnvironmentV3`, richer material payloads, and cross-family /
   motion packets.
3. Keep monitoring capture size; generated packet output can fill `Z:` quickly.

### SceneLocalEnvironmentV3 Provenance Contract - 2026-06-07

Implemented:

- `FullSceneShaderPipelineV3FrameContext` now carries explicit environment
  provenance fields:
  - `scene_local_environment_policy`
  - `scene_local_visible_background_source`
  - `scene_local_reflection_background_source`
  - `scene_local_ambient_source`
  - `scene_local_atmosphere_source`
  - `scene_local_environment_source_count`
- The `environment` domain now requires `10` ready channels instead of the
  previous `5`: mode, policy, ownership bits, and source provenance for
  ambient, visible background, reflection background, and atmosphere.
- `FrameContractJson.cpp` serializes the provenance fields into frame reports.
- `assets/final_art/full_scene_shader_pipeline_v3_contract.json` now lists the
  required environment policy channels.
- `tools/analyze_full_scene_shader_v3_placeholders.py` now fails environment
  readiness if these source/provenance fields are missing or unknown.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` validates the new
  contract/runtime surface.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\FullSceneShaderFrameContext.h src\Graphics\FrameContractJson.cpp assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_environment_provenance_full_stress_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Plan validator passed with `10` V3 domains and `29` required outputs.
- Native target rebuilt successfully; the known trailing `vswhere.exe` warning
  printed after success.
- Full V3 stress packet:
  `build\captures\v3_scene_local_environment_provenance_full_stress_20260607`.
  - scene-local packet run passed.
  - V2 evidence passed.
  - V3 placeholder packet passed with `54` reports.
  - scene profile, material payload, CompositeV3 diagnostics, and promotion
    decision passed.
  - promotion status: `review_packet_passed`, default beauty still not
    promotable because this is stress-only/static-only.
- Environment counts:
  - full-pipeline reports: `41`
  - `scene_local_environment_ready_report_count=41`
  - environment domain ready channels: `10/10`
  - source count in beauty report: `4`
- Sample beauty provenance:
  - mode: `neutral_lab`
  - policy: `authorized_external_visible_background`
  - visible background: `authorized_visible_hdri`
  - reflection background: `local_reflection_probe_radiance`
  - ambient: `scene_profile_lighting_balance`
  - atmosphere: `environment_matched_fog`

Current next work:

1. Move from provenance to texture-backed environment payloads: local
   irradiance/specular proxies, room-visible background color fields, and
   authored atmosphere parameters per scene profile.
2. Add cross-family/motion evidence once packet size is controlled.
3. Keep default beauty unpromoted until cross-family and motion evidence pass.

### V3 Promotion Matrix Harness - 2026-06-07

Implemented:

- Added `tools/build_full_scene_shader_v3_matrix_decision.py`.
  - Aggregates multiple V3 packet roots.
  - Reads each packet's `manifest.json` and `promotion_decision.json`.
  - Tracks observed families and motion modes only from packets whose
    promotion decision passed review.
  - Emits `v3_matrix_decision.json` and `v3_matrix_decision.md`.
  - Keeps `default_beauty_promotable=false`; this is evidence aggregation,
    not automatic promotion.
- Added `tools/run_full_scene_shader_pipeline_v3_matrix.ps1`.
  - Safe default: aggregates existing `-PacketRoots`.
  - Rendering a matrix requires explicit `-RunPackets`.
  - Can run a bounded matrix over selected `-FamilyFilter`,
    `-MotionModes`, and packet settings.

Validation:

```powershell
python -m py_compile tools\build_full_scene_shader_v3_matrix_decision.py
$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path tools\run_full_scene_shader_pipeline_v3_matrix.ps1), [ref]$tokens, [ref]$errors) | Out-Null; if ($errors.Count -gt 0) { $errors | Format-List; exit 1 }
python tools\build_full_scene_shader_v3_matrix_decision.py --packet-root build\captures\v3_scene_local_environment_provenance_full_stress_20260607 --required-families gallery,kitchen --required-motion-modes static,mouse_jitter --output-json build\captures\v3_matrix_smoke_existing_20260607\v3_matrix_decision.json --output-md build\captures\v3_matrix_smoke_existing_20260607\v3_matrix_decision.md
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_matrix.ps1 -OutputRoot build\captures\v3_matrix_wrapper_existing_20260607 -PacketRoots build\captures\v3_scene_local_environment_provenance_full_stress_20260607 -RequiredFamilies gallery,kitchen -RequiredMotionModes static,mouse_jitter
```

Evidence:

- The direct analyzer and wrapper both produced matrix reports from the
  existing passing V3 packet.
- Expected smoke result:
  - packet count `1`
  - passed packet count `1`
  - observed family `stress_rt_showcase_reflection_closeup`
  - observed motion `static`
  - full matrix ready `false`
  - missing required families `gallery,kitchen`
  - missing required motion `mouse_jitter`
- This proves the matrix harness catches incomplete coverage instead of
  allowing a single stress/static packet to masquerade as full promotion
  evidence.

Current next work:

1. Use the matrix harness for bounded cross-family/motion runs after choosing
   the next packet budget.
2. Continue texture-backed `SceneLocalEnvironmentV3` payload work.
3. Keep default beauty unpromoted until matrix coverage is complete and visual
   review accepts the generated scene quality.

### SceneLocalEnvironmentV3 Texture Payload Contract - 2026-06-07

Implemented:

- `FrameContract::EnvironmentInfo` now exposes scene-local texture payload
  state:
  - `scene_local_texture_set_id`
  - `scene_local_texture_set_path`
  - `scene_local_texture_set_present`
  - `scene_local_texture_count`
  - `scene_local_albedo_texture_count`
  - `scene_local_normal_texture_count`
  - `scene_local_payload_ready`
  - `scene_local_irradiance_proxy_ready`
  - `scene_local_specular_proxy_ready`
  - `scene_local_visible_background_proxy_ready`
- `Renderer_FrameContractSnapshot.cpp` derives the texture set id from the
  active scene family and scans `assets/textures/scene_local/<family>`.
  Runtime launched from `build/bin` also checks the repo-relative
  `../../assets/textures/scene_local/<family>` path.
- `FullSceneShaderPipelineV3FrameContext` now serializes:
  - `scene_local_texture_payload_ready`
  - `scene_local_texture_payload_count`
  - `scene_local_texture_set_id`
- Added `tools/analyze_full_scene_shader_v3_environment_payload.py`.
  It gates texture-set counts, albedo/normal presence, proxy readiness, and
  V3/environment contract consistency.
- Standard V3 packets now emit `v3_environment_payload.json/md`.
- `build_full_scene_shader_v3_promotion_decision.py` now requires the
  environment-payload artifact and includes its failures in promotion review.
- The V3 contract and plan validator now require the environment-payload
  artifact and payload-channel declarations.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\build_full_scene_shader_v3_matrix_decision.py
$files=@('tools\run_full_scene_shader_pipeline_v3_packet.ps1','tools\run_full_scene_shader_pipeline_v3_matrix.ps1'); foreach($file in $files){$tokens=$null;$errors=$null;[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $file),[ref]$tokens,[ref]$errors)|Out-Null; if($errors.Count -gt 0){Write-Host $file; $errors|Format-List; exit 1}}
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\FrameContract.h src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\FrameContractJson.cpp src\Graphics\FullSceneShaderFrameContext.h assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\build_full_scene_shader_v3_promotion_decision.py tools\run_full_scene_shader_pipeline_v3_packet.ps1 tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_environment_payload_gym_focus_20260607 -FamilyFilter gym -ViewFilter beauty,scene_local_environment,ambient_lighting,visible_background,reflection_background,atmosphere -SmokeFrames 8 -CaptureFrame 4 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_payload.py --manifest build\captures\v3_environment_payload_gym_focus_20260607\manifest.json --output-json build\captures\v3_environment_payload_gym_focus_20260607\v3_environment_payload_manual.json --output-md build\captures\v3_environment_payload_gym_focus_20260607\v3_environment_payload_manual.md --min-payload-ready 1
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_environment_payload_full_stress_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Native target rebuilt successfully; the known trailing `vswhere.exe` warning
  printed after success.
- Gym focused packet:
  `build\captures\v3_environment_payload_gym_focus_20260607`.
  - The renderer returned `exit_code=1` for all six views due to DX12
    `device_removed` / `DXGI_ERROR_DEVICE_HUNG` at frame 0 after HZB setup.
  - Shutdown frame reports were still written.
  - Manual environment-payload analysis passed with `0` failures:
    `report_count=6`, `texture_set_present_report_count=6`,
    `payload_ready_report_count=6`.
  - Detected family `basketball_gym_day`, texture set `basketball_gym_day`,
    `10` DDS textures, `5` albedo, `5` normal, and
    irradiance/specular/visible proxies ready.
- Full V3 stress packet:
  `build\captures\v3_environment_payload_full_stress_20260607`.
  - scene-local packet run passed.
  - V2 evidence passed.
  - V3 placeholder packet passed with `54` reports.
  - scene profile, environment payload, material payload, CompositeV3
    diagnostics, and promotion decision passed.
  - environment payload result for `rt_showcase_gallery`:
    `texture_set_present_report_count=0`, `payload_ready_report_count=0`,
    `0` failures. This is expected because no `rt_showcase_gallery` texture
    set exists yet.

Current next work:

1. Add/import scene-local texture sets for more scene families, starting with
   `rt_showcase_gallery` or the model-authored families used by the promotion
   matrix.
2. Investigate the repeatable model-authored scene DX12 device hang; gym and
   kitchen both write useful reports but fail capture runs.
3. Feed payload readiness into actual `SceneLocalEnvironmentV3.hlsl`
   color/radiance selection once enough families have texture payloads.

### SceneLocalEnvironmentV3 Shader Profile Resource Selection - 2026-06-07

Implemented:

- Added a compact SceneLocalEnvironmentV3 shader-profile lane produced by
  `Renderer::BuildSceneLocalEnvironmentV3ProfileParams()`.
  - mode `0`: neutral/lab
  - mode `1`: gallery
  - mode `2`: enclosed room
  - mode `3`: stage/concert/red room
  - mode `4`: open exterior
- Reused `FrameConstants::cinematicDofParams.zw` as owned environment lanes:
  `.z` carries profile mode and `.w` carries local-background ownership
  strength. `.x/.y` remain DOF focus/aperture.
- `SceneLocalEnvironmentV3.hlsl` now consumes those lanes to select local
  visible-background, ambient, reflection-background, and atmosphere palettes.
  Enclosed/stage profiles gain stronger local ownership, while authorized
  gallery/exterior profiles can still admit external HDRI influence.
- Frame reports now expose:
  - `scene_local_shader_profile`
  - `scene_local_shader_profile_mode`
  - `scene_local_background_strength`
  - V3 aliases
    `scene_local_environment_shader_profile`,
    `scene_local_environment_shader_profile_mode`, and
    `scene_local_environment_local_background_strength`
- Environment readiness now requires `15` channels, adding shader-profile and
  local-background-strength readiness on top of policy consumption and texture
  payload reporting.
- The V3 contract, placeholder analyzer, environment-payload analyzer, and plan
  validator now require and verify the shader-profile lanes. The validator also
  includes `Renderer_FramePostConstants.cpp` in the runtime surface so it checks
  the C++ producer, not only JSON/HLSL consumers.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\analyze_full_scene_shader_v3_scene_profile.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\Renderer.h src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\FrameContract.h src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\FrameContractJson.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\ShaderTypes.h assets\shaders\SceneLocalEnvironmentV3.hlsl assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_environment_payload.py tools\analyze_full_scene_shader_v3_placeholders.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_scene_local_environment_shader_profile_stress_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Plan validator passed with `10` domains and `29` required outputs.
- Native target rebuilt successfully. The known trailing `vswhere.exe` warning
  printed after successful link.
- Focused packet:
  `build\captures\v3_scene_local_environment_shader_profile_stress_20260607`.
  - scene-local packet passed.
  - V2 evidence passed.
  - V3 placeholder packet passed with `54` reports.
  - scene profile, environment payload, material payload, CompositeV3
    diagnostics, and promotion decision passed.
  - promotion status: `review_packet_passed`; default beauty remains
    non-promotable because matrix coverage is incomplete.
- Environment payload proof:
  - `report_count=54`
  - `profile_policy_consumed_report_count=54`
  - `failures=0`
  - first row: shader profile `gallery_neutral`, mode `1.0`,
    local-background strength `0.35`
  - payload-ready count remained `0`, expected until an
    `rt_showcase_gallery` scene-local texture set exists.

Current next work:

1. Add/import scene-local texture sets for `rt_showcase_gallery` and the first
   promotion families, then drive richer irradiance/specular/background
   selection from actual payload readiness instead of profile palettes alone.
2. Add a small cross-profile packet proving profile modes differ across at
   least gallery, enclosed room, stage/red room, and exterior.
3. Continue toward LightingShadowV3 source split and ReflectionV3 resolver
   hardening before any strong CinematicPostV3 tuning.

### SceneLocalEnvironmentV3 Cross-Profile Analyzer - 2026-06-07

Implemented:

- Added `tools/analyze_full_scene_shader_v3_environment_profiles.py`.
  - Reads one or more packet manifests and frame reports.
  - Summarizes selected shader profile, mode, local-background strength,
    profile id, enclosure mode, and environment policy.
  - Fails when ready environment reports lack known shader profiles or valid
    mode/strength ranges.
  - Can require distinct profile/mode counts and named profile/mode pairs.
  - Strict by default for missing reports, with explicit
    `--allow-missing-reports` for diagnostic manifests from known renderer
    crash paths.
- Wired the analyzer into `tools/validate_full_scene_shader_pipeline_v3_plan.py`
  so it is part of the V3 runtime/validation surface.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_profiles.py tools\validate_full_scene_shader_pipeline_v3_plan.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build\captures\v3_environment_profiles_model_family_probe_20260607 -FamilyFilter kitchen,gym,concert,red_room,stadium -ViewFilter scene_local_environment -SmokeFrames 4 -CaptureFrame 2 -CaptureSequenceCount 1 -SkipOwnerAnalysis -SkipMaterialAnalysis -SkipStabilityAnalysis -SkipVisualQualityAnalysis
python tools\analyze_full_scene_shader_v3_environment_profiles.py --manifest build\captures\v3_scene_local_environment_shader_profile_stress_20260607\manifest.json --manifest build\captures\v3_environment_profiles_model_family_probe_20260607\manifest.json --output-json build\captures\v3_environment_profiles_model_family_probe_20260607\v3_environment_profiles_cross_probe.json --output-md build\captures\v3_environment_profiles_model_family_probe_20260607\v3_environment_profiles_cross_probe.md --allow-missing-reports --min-ready-reports 3 --min-distinct-modes 3 --min-distinct-profiles 3 --require-profile gallery_neutral=1 --require-profile enclosed_room=2 --require-profile stage=3
```

Evidence:

- The model-family diagnostic packet returned nonzero due known model-scene
  crash/hang behavior:
  - kitchen exit `2173`, no report
  - gym exit `1`, shutdown report present
  - concert exit `0`
  - red room exit `1`, shutdown report present
  - stadium exit `0`
- Cross-profile analyzer output:
  `build\captures\v3_environment_profiles_model_family_probe_20260607\v3_environment_profiles_cross_probe.json/md`.
  - reports: `58`
  - environment-ready reports: `57`
  - distinct shader profiles:
    `enclosed_room`, `gallery_neutral`, `open_exterior`, `stage`
  - distinct shader modes: `1.0`, `2.0`, `3.0`, `4.0`
  - failures: `0`
- This proves the SceneLocalEnvironmentV3 profile selector is not limited to
  the gallery stress case. It also preserves the unresolved model-authored
  kitchen crash as separate stability debt instead of hiding it.

Current next work:

1. Import/add texture payload sets for gallery, enclosed-room, stage, and
   exterior families, then make shader selection use payload-backed radiance
   rather than profile palette placeholders.
2. Make the model-scene diagnostic path cleaner by separating "report evidence"
   packets from "visual capture success" packets where DX12 device removal is
   already known.
3. Start LightingShadowV3 high-contrast source-split work after the environment
   payload/resource path has at least one texture-backed non-gallery proof.

### SceneLocalEnvironmentV3 Payload-Backed Shader Influence - 2026-06-07

Implemented:

- Added `Renderer::BuildSceneLocalEnvironmentV3PayloadParams()`.
  It scans scene-local payload textures and packs:
  - payload ready
  - texture richness
  - proxy score
  - shader influence
- `FrameConstants::fogExtraParams.yzw` now carry SceneLocalEnvironmentV3
  payload lanes:
  - `.y`: payload ready
  - `.z`: texture richness
  - `.w`: payload shader influence
  `.x` remains fog start distance.
- `SceneLocalEnvironmentV3.hlsl` now consumes those payload lanes. When a
  payload is ready, visible background, ambient, reflection background, and
  output confidence are biased toward payload-owned local radiance instead of
  only profile palette constants.
- Added explicit `rt_showcase_gallery -> assets/textures/rtshowcase` payload
  alias in both the frame-report scanner and shader-constant scanner. This
  avoids duplicating about 39 MB of DDS textures while still making the gallery
  use real tracked DDS assets as a scene-local payload.
- Frame reports now expose:
  - `scene_local_payload_texture_richness`
  - `scene_local_payload_proxy_score`
  - `scene_local_payload_shader_influence`
  - V3 aliases
    `scene_local_texture_payload_richness`,
    `scene_local_texture_payload_proxy_score`, and
    `scene_local_texture_payload_shader_influence`
- The V3 contract, environment-payload analyzer, and plan validator now require
  and check these fields. If payload-ready is true, richness/proxy/influence
  must be valid and V3 values must match environment values.

Validation:

```powershell
python -m py_compile tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py tools\analyze_full_scene_shader_v3_placeholders.py tools\analyze_full_scene_shader_v3_environment_profiles.py
python tools\validate_full_scene_shader_pipeline_v3_plan.py
git -c submodule.recurse=false diff --check -- src\Graphics\Renderer.h src\Graphics\Renderer_FramePostConstants.cpp src\Graphics\FrameContract.h src\Graphics\Renderer_FrameContractSnapshot.cpp src\Graphics\FrameContractJson.cpp src\Graphics\FullSceneShaderFrameContext.h src\Graphics\ShaderTypes.h assets\shaders\SceneLocalEnvironmentV3.hlsl assets\final_art\full_scene_shader_pipeline_v3_contract.json tools\analyze_full_scene_shader_v3_environment_payload.py tools\validate_full_scene_shader_pipeline_v3_plan.py
cmd.exe /d /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && set ""CORTEX_SKIP_ASSET_SYNC=1"" && ""C:\Program Files\Ninja\ninja.exe"" -C build CortexEngine -j 8"
Copy-Item -LiteralPath assets\final_art\full_scene_shader_pipeline_v3_contract.json -Destination build\bin\assets\final_art\full_scene_shader_pipeline_v3_contract.json -Force
$env:CORTEX_V3_REFLECTION_SOURCE_OVERRIDE='ssr'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_packet.ps1 -NoBuild -SkipSceneAnalyzers -StressSceneOnly -StressSceneFilter rt_showcase:reflection_closeup -SmokeFrames 16 -CaptureFrame 8 -CaptureSequenceCount 1 -StabilityMotionMode static -OutputRoot build\captures\v3_environment_payload_shader_influence_gallery_20260607
Remove-Item Env:\CORTEX_V3_REFLECTION_SOURCE_OVERRIDE -ErrorAction SilentlyContinue
```

Evidence:

- Plan validator passed with `10` domains and `29` required outputs.
- Native target rebuilt successfully. The known trailing `vswhere.exe` warning
  printed after successful link.
- Packet:
  `build\captures\v3_environment_payload_shader_influence_gallery_20260607`.
  - scene-local packet passed.
  - V2 evidence passed.
  - V3 placeholder packet passed with `54` reports.
  - scene profile, environment payload, material payload, CompositeV3
    diagnostics, and promotion decision passed.
  - promotion status: `review_packet_passed`; default beauty remains
    non-promotable because matrix family/motion coverage is incomplete.
- Environment payload proof:
  - reports: `54`
  - payload-ready reports: `54`
  - shader-influence reports: `54`
  - failures: `0`
  - first row: texture set `rt_showcase_gallery`, `12` DDS textures,
    `5` albedo, `6` normal, payload ready `true`, richness `1.0`,
    proxy score about `0.67`, shader influence about `0.87`

Rejected/known limitation:

- A direct gym/model-authored payload smoke still exited `2173` after writing a
  visual validation BMP but before writing a frame report. Do not use that as
  payload evidence yet. Keep the model-scene crash/device-removal path as
  separate stability debt.
- This slice still does not bind/sample scene-local DDS textures in HLSL. It
  converts real payload availability into owned shader influence and contract
  evidence. The next resource step is actual local irradiance/specular texture
  binding or generated proxy resources.

Current next work:

1. Add a true SceneLocalEnvironmentV3 payload resource binding path:
   local irradiance/proxy texture, local specular/prefilter proxy, and visible
   background proxy.
2. Add non-gallery payload sets or aliases for enclosed room, stage, and
   exterior families.
3. Continue separating model-scene report-evidence capture from visual-capture
   success so family packets can produce diagnostics even when renderer
   stability debt remains.

### RT Showcase Wall/Floor IBL Flicker Diagnosis - 2026-06-09

Context:

- User repro remains the default `rt_showcase` old-office/studio IBL path,
  camera bookmark `reported_wall_floor_flicker`, with mouse yaw jitter.
- Do not treat fixes that hide/disable/blur IBL as root fixes.
- The relevant command shape is:

```powershell
Copy-Item assets\shaders\Basic.hlsl build\bin\assets\shaders\Basic.hlsl -Force
Copy-Item assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl -Force
Copy-Item assets\shaders\MaterialResolve.hlsl build\bin\assets\shaders\MaterialResolve.hlsl -Force
$env:CORTEX_DISABLE_SHADER_CACHE='1'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_rt_showcase_wall_floor_flicker_stability_smoke.ps1 `
  -NoBuild `
  -LogDir Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_wall_floor_<name> `
  -CaptureStartFrame 70 -CaptureCount 48 `
  -MotionFrames 180 -MotionLookAmplitude 0.025 -MotionLookCycles 12.0 `
  -FixedDeltaTime 0.008333333
Remove-Item Env:\CORTEX_DISABLE_SHADER_CACHE -ErrorAction SilentlyContinue
```

Important harness note:

- Pass an absolute `-LogDir`. A relative `build\captures\...` path is passed
  to the engine through `CORTEX_LOG_DIR` after the wrapper changes directory to
  `build\bin`, so captures land under `build\bin\build\captures\...` while the
  wrapper checks `build\captures\...`.

Implemented / tested this pass:

- Removed the speculative roughness/specular blur policy from
  `SurfaceClassification.hlsli` and `DeferredLighting.hlsl`; earlier evidence
  showed it did not move the reported ROI.
- Kept the VB material-gradient correction in `MaterialResolve.hlsl` as a
  technically correct derivative fix, but it is not proven as the dominant
  flicker root.
- Changed the RT Showcase courtyard pool from one full white `8x8` plane plus
  one full transparent/water `8x8` plane separated by only `0.01` units into
  explicit rim strips plus a smaller inset water plane.
  - This removes real coextensive-plane ownership debt.
  - It did not materially move the reported flicker metrics.
- Added `tools/analyze_rt_showcase_wall_floor_roi_stability.py`.
  - It parses captured BMPs directly.
  - It reports fixed ROIs for `white_platform`, `front_dark_floor`,
    `left_wall_panel`, `pool_water_rim`, `background_office`, and whole frame.
  - Use it alongside the full-frame smoke because the visible office HDRI
    background contributes legitimate parallax under mouse yaw.
- Added a VB deferred scene-local payload constant lane and tried local-payload
  ownership gating for rough non-reflective global HDRI specular.
  - Also tried the same concept in `Basic.hlsl` forward IBL using
    `g_FogExtraParams`.
  - These patches compiled, but did not materially improve the reported beauty
    or ROI metrics. Treat them as unproven until the next pass decides whether
    to keep, revise, or revert.

Current evidence:

```powershell
python tools\analyze_rt_showcase_wall_floor_roi_stability.py `
  --capture-dir build\captures\rt_showcase_wall_floor_exact_ablation_after_pool_20260609\baseline `
  --output-json build\captures\rt_showcase_wall_floor_exact_ablation_after_pool_20260609\baseline\wall_floor_roi_stability.json `
  --output-md build\captures\rt_showcase_wall_floor_exact_ablation_after_pool_20260609\baseline\wall_floor_roi_stability.md

python tools\analyze_rt_showcase_wall_floor_roi_stability.py `
  --capture-dir build\captures\rt_showcase_wall_floor_exact_ablation_after_pool_20260609\no_ibl `
  --output-json build\captures\rt_showcase_wall_floor_exact_ablation_after_pool_20260609\no_ibl\wall_floor_roi_stability.json `
  --output-md build\captures\rt_showcase_wall_floor_exact_ablation_after_pool_20260609\no_ibl\wall_floor_roi_stability.md
```

Baseline exact-path ROI maxima after the pool ownership geometry change:

| ROI | Max mean luma | Max changed | Max large changed |
|---|---:|---:|---:|
| white_platform | `11.4857` | `0.2754` | `0.0970` |
| front_dark_floor | `5.9437` | `0.1741` | `0.0282` |
| left_wall_panel | `4.8461` | `0.0819` | `0.0241` |
| pool_water_rim | `22.0276` | `0.4414` | `0.2239` |
| background_office | `8.0819` | `0.1726` | `0.0597` |
| whole_frame | `6.9590` | `0.1457` | `0.0489` |

No-IBL exact-path ROI maxima:

| ROI | Max mean luma | Max changed | Max large changed |
|---|---:|---:|---:|
| white_platform | `4.4074` | `0.0756` | `0.0483` |
| front_dark_floor | `1.5494` | `0.0159` | `0.0137` |
| left_wall_panel | `3.3334` | `0.0388` | `0.0182` |
| pool_water_rim | `12.6724` | `0.2608` | `0.1699` |
| background_office | `2.6431` | `0.0657` | `0.0237` |
| whole_frame | `2.7549` | `0.0497` | `0.0242` |

Exact beauty ablation summary:

- baseline: max mean `6.9219`, changed `0.14446`, large `0.04881`
- `CORTEX_ENABLE_VB_MOTION_VECTORS=1`: no meaningful change
- `CORTEX_DISABLE_SHADOWS=1`: no meaningful improvement
- `CORTEX_DISABLE_SSAO=1`: no meaningful change
- `CORTEX_DISABLE_SSR=1`: no meaningful change
- `CORTEX_DISABLE_IBL=1`: max mean drops to `2.7444`, changed `0.04939`
- IBL component split:
  - diffuse-only: max mean `4.0988`, changed `0.06138`
  - specular-only: max mean `6.6682`, changed `0.14154`
  - low specular (`0.25`): max mean `5.0931`, changed `0.08828`

Current interpretation:

- The major remaining owner is IBL, especially specular/environment contribution.
- The first local-probe ownership attempt failed because debug view `42`
  showed the reported white platform/wall area has essentially zero local probe
  coverage.
- Adding frame-level scene-local payload influence to VB deferred lighting and
  forward `Basic.hlsl` did not move the reported ROI enough. This means the
  remaining leak is probably in transparent/water compositing, sky/background
  contribution through the view, or another post/forward path, not just the
  opaque VB deferred BRDF.
- Do not repeat shadow/SSAO/SSR/VB-motion-vector ablations as first moves; they
  have already been ruled out for this repro.

Recommended next pass:

1. Decide whether to keep or revert the unproven scene-local specular ownership
   edits in `DeferredLighting.hlsl`, `Basic.hlsl`, `VisibilityBuffer.h`, and
   `Renderer_VisibilityBufferDeferredLighting.cpp`.
2. Run exact-path ablations for transparent/water/skybox/background composition
   ownership, not just opaque deferred lighting.
   - Search for existing env disables for auxiliary geometry, water, skybox,
     transparency, and background presentation.
   - If none exist, add default-off diagnostics so these lanes can be isolated.
3. Add an owner/debug view for transparent/water IBL contribution or composite
   contribution over the reported platform ROI.
4. Only after the owner is proven, implement the root contract:
   - transparent/water materials sample scene-local proxy radiance or a stable
     local reflection lane where appropriate;
   - visible HDRI background remains visible when required, but it must not
     inject unstable high-frequency specular bands into rough interior/room
     surfaces.

### RT Showcase Wall/Floor IBL Flicker Follow-Up - 2026-06-09

Current status:

- Not fixed yet. Do not claim the wall/floor flicker is solved.
- Added `tools/analyze_rt_showcase_wall_floor_roi_stability.py` tight ROIs:
  - `white_platform_clean_right`
  - `white_platform_clean_left`
  - `front_dark_floor_clean`
  - `left_wall_panel_clean`
- Added debug view `92` as `VB_DeferredGlobalIBLOwnership`.
  - Previous debug view `48` was invalid for this purpose because it is routed
    to `VB_MaterialId`.
- Added diagnostic env override:
  - `CORTEX_RT_SHOWCASE_BACKGROUND_BLUR=<0..1>`
  - This only overrides the RT Showcase gallery profile background blur for
    reproducible testing.
- Added derivative-based visible-background mip selection in:
  - `assets/shaders/DeferredLighting.hlsl` depth-miss background path
  - `assets/shaders/Basic.hlsl` forward skybox path

Important negative result:

- Hard-suppressing deferred global specular ownership on rough payload-owned
  pixels did not materially improve the broad beauty ROI.
- The hard branch did trigger in debug view `92`; on the clean-right platform
  ownership dropped near `0.056`.
- Therefore, the no-op was not stale shader bytecode and not the wrong pass.

Tighter ROI re-analysis:

| Capture | white_platform_clean_right | front_dark_floor_clean | left_wall_panel_clean | background_office | whole_frame |
|---|---:|---:|---:|---:|---:|
| baseline | `13.4528 / 0.4078 / 0.0949` | `5.9750 / 0.1745 / 0.0252` | `4.7129 / 0.0776 / 0.0198` | `8.0819 / 0.1726 / 0.0597` | `6.9590 / 0.1457 / 0.0489` |
| no IBL | `0.1989 / 0.0000 / 0.0000` | `0.6294 / 0.0071 / 0.0052` | `3.0693 / 0.0327 / 0.0119` | `2.6431 / 0.0657 / 0.0237` | `2.7549 / 0.0497 / 0.0242` |
| diffuse-only IBL | `0.0000 / 0.0000 / 0.0000` | `0.1586 / 0.0058 / 0.0000` | `4.3872 / 0.0594 / 0.0133` | `4.9542 / 0.0815 / 0.0402` | `4.1147 / 0.0623 / 0.0298` |
| specular-only IBL | `13.4528 / 0.4078 / 0.0949` | `5.9652 / 0.1739 / 0.0252` | `4.4160 / 0.0673 / 0.0191` | `8.0281 / 0.1716 / 0.0582` | `6.7046 / 0.1430 / 0.0480` |
| low specular IBL | `4.6343 / 0.1154 / 0.0091` | `1.9603 / 0.0363 / 0.0016` | `4.6026 / 0.0640 / 0.0148` | `6.0774 / 0.1134 / 0.0424` | `5.1147 / 0.0893 / 0.0317` |

Interpretation:

- The clean-right patch is controlled by the specular/background side of IBL,
  not diffuse IBL.
- The specular debug screenshot showed that the broad/tight screen ROIs still
  include depth-miss visible HDRI and silhouette motion. This means the current
  ROI metric can be dominated by expected background parallax, not only by
  material flicker.
- Background blur `0.0` with the old mip behavior was not worse under this
  metric, which further suggests the current metric is not isolating the
  visible user artifact cleanly enough.

Next required pass:

1. Add a depth/material-aware ROI analyzer or capture an explicit ID/depth mask
   for the reported floor/wall receiver before changing more shader policy.
2. Separate three owners in the evidence packet:
   - depth-miss visible HDRI background
   - opaque VB deferred receiver
   - forward transparent/water/aux receiver
3. Only then choose the final fix:
   - if depth-miss dominates, improve visible-background antialiasing/temporal
     stability and exclude expected parallax from the floor/wall gate;
   - if opaque receiver dominates, continue scene-local reflection ownership;
   - if transparent/water dominates, fix the forward/aux reflection path.

### RT Showcase Masked Owner Packet - 2026-06-09

Implemented:

- Quarantined the unproven IBL ownership policy:
  - removed the forward `Basic.hlsl` scene-local global specular multiplier;
  - restored deferred global HDRI specular contribution to beauty output;
  - kept debug view `92` as diagnostic-only.
- Extended `tools/analyze_rt_showcase_wall_floor_roi_stability.py` with aligned
  debug-view masks:
  - `--mask-dir`
  - `--mask-mode luma|not-reference-color`
  - `--invert-mask`
  - `--mask-threshold`
  - `--mask-reference-x/y`
- Used debug view `36` (`VB_GBuffer_NormalRoughness`) as a stable foreground
  mask via `--mask-mode not-reference-color`, rejecting the top-right
  depth-miss lavender background color.

Captured packet:

```powershell
build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609
build\captures\rt_showcase_wall_floor_masked_owner_packet_mask_normal_roughness36_20260609
build\captures\rt_showcase_wall_floor_masked_owner_packet_specular9_20260609
build\captures\rt_showcase_wall_floor_masked_owner_packet_ownership92_20260609
```

Analysis commands:

```powershell
$mask='build\captures\rt_showcase_wall_floor_masked_owner_packet_mask_normal_roughness36_20260609'
python tools\analyze_rt_showcase_wall_floor_roi_stability.py `
  --capture-dir build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609 `
  --mask-dir $mask `
  --mask-mode not-reference-color `
  --mask-threshold 12 `
  --mask-reference-x 1270 `
  --mask-reference-y 10 `
  --output-json build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609\beauty_fg_color.json `
  --output-md build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609\beauty_fg_color.md

python tools\analyze_rt_showcase_wall_floor_roi_stability.py `
  --capture-dir build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609 `
  --mask-dir $mask `
  --mask-mode not-reference-color `
  --mask-threshold 12 `
  --mask-reference-x 1270 `
  --mask-reference-y 10 `
  --invert-mask `
  --output-json build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609\beauty_bg_color.json `
  --output-md build\captures\rt_showcase_wall_floor_masked_owner_packet_beauty_20260609\beauty_bg_color.md
```

Masked evidence:

| Report | white_platform_clean_right | front_dark_floor_clean | left_wall_panel_clean | background_office | whole_frame |
|---|---:|---:|---:|---:|---:|
| beauty foreground | `0.0000 / 0.0000 / 0.0000 / coverage 0.0000` | `1.4247 / 0.0497 / 0.0017 / coverage 0.0330` | `4.6196 / 0.0721 / 0.0183 / coverage 0.9987` | `5.1814 / 0.1052 / 0.0413 / coverage 0.5981` | `5.2061 / 0.0936 / 0.0353 / coverage 0.5166` |
| beauty background | `12.3975 / 0.3739 / 0.0850 / coverage 1.0000` | `5.7482 / 0.1680 / 0.0217 / coverage 0.9685` | `25.0356 / 0.7121 / 0.2319 / coverage 0.0016` | `11.8989 / 0.2554 / 0.0908 / coverage 0.4560` | `8.1430 / 0.1846 / 0.0578 / coverage 0.5099` |
| specular foreground | `0.0000 / 0.0000 / 0.0000 / coverage 0.0000` | `1.8670 / 0.0521 / 0.0119 / coverage 0.0330` | `2.5180 / 0.0559 / 0.0156 / coverage 0.9987` | `2.0071 / 0.0398 / 0.0017 / coverage 0.5981` | `2.8751 / 0.0616 / 0.0167 / coverage 0.5166` |
| specular background | `12.3975 / 0.3739 / 0.0850 / coverage 1.0000` | `5.7750 / 0.1685 / 0.0221 / coverage 0.9685` | `22.5263 / 0.7424 / 0.2319 / coverage 0.0016` | `8.8245 / 0.2259 / 0.0536 / coverage 0.4560` | `7.2196 / 0.1767 / 0.0476 / coverage 0.5099` |

Interpretation:

- The old `white_platform_clean_right` ROI was not a reliable floor/platform
  receiver measurement in this camera. It was `100%` masked background.
- The old `front_dark_floor_clean` ROI was also mostly background
  (`~96.85%` background, `~3.30%` foreground).
- The `left_wall_panel_clean` ROI is the best current opaque receiver sample:
  `~99.87%` foreground.
- The remaining large whole-frame/broad-ROI instability is heavily influenced
  by depth-miss visible HDRI and silhouette parallax. Do not use those broad
  metrics as proof of an opaque material/shader flicker.

Next pass:

1. Promote the masked owner packet into a reusable repro gate:
   - require an aligned mask capture;
   - report foreground/background splits by default;
   - gate opaque receiver stability on foreground-only ROIs.
2. Add more foreground-only wall/floor ROIs or camera bookmarks where the
   platform/floor is actually visible in the foreground mask.
3. Only then re-enter shader policy:
   - if foreground wall ROI still flickers, inspect material ID/policy and
     specular ownership for that receiver;
   - if foreground is stable but background is noisy, work on visible-HDRI
     temporal/background presentation rather than material BRDF.

### RT Showcase Masked Owner Packet Wrapper - 2026-06-09

Implemented:

- Added `tools/run_rt_showcase_wall_floor_masked_owner_packet.ps1`.
- The wrapper captures aligned deterministic passes:
  - `beauty`
  - `mask_normal_roughness36`
  - `specular9`
  - `ownership92`
- It then emits:
  - `beauty_foreground/background`
  - `specular_foreground/background`
  - `ownership_foreground/background`
  - `masked_owner_packet_summary.json`
  - `masked_owner_packet_summary.md`
- It now gates foreground-owned opaque receiver metrics independently from the
  underlying child smoke exit code.
  - Default ROI: `left_wall_panel_clean`
  - Default thresholds:
    - max foreground mean luma delta: `8.0`
    - max foreground changed ratio: `0.12`
    - max foreground large-changed ratio: `0.04`
    - min foreground mask coverage: `0.90`

Smoke command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_wall_floor_masked_owner_packet_wrapper_smoke2_20260609 `
  -CaptureCount 2 `
  -MotionFrames 75 `
  -MotionLookCycles 4.0
```

Smoke result:

- Passed wrapper execution and wrote summary:
  `build\captures\rt_showcase_wall_floor_masked_owner_packet_wrapper_smoke2_20260609\masked_owner_packet_summary.md`
- Each child capture still exits `1` because the existing underlying smoke
  treats known frame-contract warnings as failures:
  - `visibility_buffer_rendered_without_visibility_motion_vectors`
  - `rtv_descriptor_heap_high_water`
  - debug view `36` also reports the existing local probe table warning
- The wrapper records those child exits but still succeeds if captures and
  analyses are produced.

Gate verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_wall_floor_masked_owner_packet_gate_smoke_20260609 `
  -CaptureCount 2 `
  -MotionFrames 75 `
  -MotionLookCycles 4.0
```

- Exit code: `0`
- Summary:
  `build\captures\rt_showcase_wall_floor_masked_owner_packet_gate_smoke_20260609\masked_owner_packet_summary.md`
- Gate rows:
  - `beauty_foreground / left_wall_panel_clean`: mean `1.6108`,
    changed `0.0113`, large `0.0015`, coverage `0.9987`, passed
  - `specular_foreground / left_wall_panel_clean`: mean `0.5839`,
    changed `0.0106`, large `0.0018`, coverage `0.9987`, passed

Intentional fail verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_wall_floor_masked_owner_packet_gate_fail_smoke_20260609 `
  -CaptureCount 2 `
  -MotionFrames 75 `
  -MotionLookCycles 4.0 `
  -MaxForegroundMeanLumaDelta 0.01
```

- Expected wrapper exit: `1`
- The verification wrapper observed that expected failure and returned `0` to
  the shell command.
- Failure rows:
  - `beauty_foreground/left_wall_panel_clean`
  - `specular_foreground/left_wall_panel_clean`

Next pass:

- Add another foreground-only floor/platform camera bookmark or ROI. The
  current `white_platform_clean_right` and `front_dark_floor_clean` ROIs are
  mostly or entirely background/depth-miss in the reported camera.
- Use the foreground gate to drive the next real shader fix:
  - if foreground wall/floor metrics fail, fix material/lighting ownership;
  - if only background metrics fail, work on visible HDRI/background temporal
    stability instead of opaque BRDF.

### RT Showcase Foreground Floor Gate - 2026-06-09

Implemented:

- `tools/run_rt_showcase_wall_floor_flicker_stability_smoke.ps1` now accepts
  `-CameraBookmark` instead of hardcoding `reported_wall_floor_flicker`.
- `tools/run_rt_showcase_wall_floor_masked_owner_packet.ps1` now accepts:
  - `-CameraBookmark`
  - `-CustomRois` as `name:x0,y0,x1,y1`
- `tools/analyze_rt_showcase_wall_floor_roi_stability.py` now accepts repeated
  `--roi name:x0,y0,x1,y1` entries, which add or override packet ROIs.

Foreground floor probe:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -CameraBookmark hero `
  -CustomRois "hero_floor_foreground:210,510,1120,650" `
  -ForegroundGateRois hero_floor_foreground `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_foreground_floor_gate_hero_20260609 `
  -CaptureCount 2 `
  -MotionFrames 75 `
  -MotionLookCycles 4.0
```

Result:

- Wrapper exit: `0`
- Summary:
  `build\captures\rt_showcase_foreground_floor_gate_hero_20260609\masked_owner_packet_summary.md`
- Gate rows:
  - `beauty_foreground / hero_floor_foreground`: mean `0.8038`,
    changed `0.0062`, large `0.0002`, coverage `1.0000`, passed
  - `specular_foreground / hero_floor_foreground`: mean `1.5430`,
    changed `0.0328`, large `0.0127`, coverage `1.0000`, passed

Interpretation:

- The RT Showcase `hero` bookmark has a valid foreground-owned gallery floor
  ROI. The mask confirms `hero_floor_foreground` is `100%` foreground under
  debug view `36`.
- This does not prove the original user-reported view is solved. It proves the
  opaque VB floor receiver can be stable when measured as owned geometry.
- The original `reported_wall_floor_flicker` view still needs separate
  treatment because the old floor/platform ROIs are dominated by depth-miss
  visible HDRI/background and silhouette parallax.

Next pass:

1. Add a second foreground-owned ROI on the user-reported bookmark if possible,
   or create a dedicated `reported_wall_floor_foreground_probe` bookmark that
   frames the same material receiver without depth-miss contamination.
2. If foreground-owned floor/wall remains stable, stop chasing opaque BRDF
   policy for that repro and move to visible-HDRI/background temporal
   presentation or transparent/aux compositing.
3. Keep the old broad/background metrics as diagnostic context only; do not use
   them as the final opaque material flicker gate.

### RT Showcase Reported-Camera Owner Classification - 2026-06-09

Implemented:

- `tools/analyze_rt_showcase_wall_floor_roi_stability.py`
  - added `--replace-default-rois` so long packets can analyze only the
    explicitly requested ROIs instead of timing out on every default ROI.
- `tools/run_rt_showcase_wall_floor_masked_owner_packet.ps1`
  - added `-CustomRoiList` with semicolon-delimited ROI definitions. This is
    safer than PowerShell array syntax for values that themselves contain
    commas.
  - added `-CustomRoisOnly`, which passes `--replace-default-rois`.
  - added an `Owner Classification` table to the packet summary.

Classification meanings:

- `foreground_receiver_pass`: foreground mask coverage is high and both beauty
  plus foreground specular are under the foreground gate.
- `mixed_foreground_background`: the ROI contains real foreground plus
  substantial depth-miss/background; it is not a valid opaque material gate.
- `background_or_depth_miss_dominant` / `depth_miss_background`: instability
  should be investigated in visible HDRI/background presentation or composite
  ownership, not opaque BRDF policy.

Validated short wrapper smoke:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -CameraBookmark reported_wall_floor_flicker `
  -CustomRoiList "reported_left_wall_foreground:25,185,360,440;reported_platform_mixed:610,405,1190,555;reported_lower_floor_mixed:430,545,1015,705" `
  -CustomRoisOnly `
  -ForegroundGateRois reported_left_wall_foreground `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_reported_owner_classification_smoke_20260609 `
  -CaptureCount 2 `
  -MotionFrames 75 `
  -MotionLookCycles 4.0
```

Smoke result:

- Wrapper exit: `0`
- Summary:
  `build\captures\rt_showcase_reported_owner_classification_smoke_20260609\masked_owner_packet_summary.md`
- Owner classification:
  - `reported_left_wall_foreground`: `foreground_receiver_pass`, foreground
    coverage `0.9979`
  - `reported_platform_mixed`: `mixed_foreground_background`, foreground
    coverage `0.2265`
  - `reported_lower_floor_mixed`: `mixed_foreground_background`, foreground
    coverage `0.1987`

Longer reported-camera evidence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -CameraBookmark reported_wall_floor_flicker `
  -CustomRoiList "reported_left_wall_foreground:25,185,360,440;reported_platform_mixed:610,405,1190,555;reported_lower_floor_mixed:430,545,1015,705" `
  -CustomRoisOnly `
  -ForegroundGateRois reported_left_wall_foreground `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_reported_foreground_wall_gate_wrapper_20260609 `
  -CaptureCount 16 `
  -MotionFrames 100 `
  -MotionLookCycles 6.0
```

Longer result:

- Wrapper exit: `0`
- Summary:
  `build\captures\rt_showcase_reported_foreground_wall_gate_wrapper_20260609\masked_owner_packet_summary.md`
- Foreground gate passed:
  - `beauty_foreground / reported_left_wall_foreground`: mean `5.6329`,
    changed `0.1006`, large `0.0287`, coverage `0.9979`
  - `specular_foreground / reported_left_wall_foreground`: mean `4.5307`,
    changed `0.1010`, large `0.0322`, coverage `0.9979`
- Background/mixed evidence:
  - `beauty_background / reported_left_wall_foreground`: mean `19.6978`,
    changed `0.4918`, large `0.1803`, coverage `0.0024`
  - `beauty_background / reported_platform_mixed`: mean `15.9385`,
    changed `0.4152`, large `0.1379`, coverage `0.8337`
  - `beauty_background / reported_lower_floor_mixed`: mean `6.8258`,
    changed `0.1981`, large `0.0294`, coverage `0.8101`

Current interpretation:

- The exact reported camera now has a foreground-owned wall receiver that stays
  inside the current gate over 16 frames.
- The platform/lower-floor regions are not valid opaque receiver gates because
  they are mostly depth-miss/background under the aligned mask.
- The next root pass should focus on visible HDRI/background temporal
  presentation and mixed transparent/aux compositing. Do not return to broad
  opaque BRDF or shadow tweaks unless a high-coverage foreground ROI fails.

### RT Showcase Background-TAA Negative Result - 2026-06-09

Implemented:

- `tools/run_rt_showcase_wall_floor_masked_owner_packet.ps1` now also copies
  `assets/shaders/PostProcess.hlsl` into `build/bin/assets/shaders` before
  capture. This makes post-process/TAA shader experiments actually participate
  in masked owner packets when `CORTEX_DISABLE_SHADER_CACHE=1` is set.

Tested but reverted:

- A narrow TAA shader experiment treated depth-miss pixels as a background
  surface class:
  - accepted depth-miss neighbours for the clamp window;
  - allowed background-background history instead of rejecting all far-plane
    history;
  - capped background history below opaque-surface history.
- The experiment was not kept because it did not materially move the reported
  16-frame owner packet.

Evidence packet with the experiment active:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\run_rt_showcase_wall_floor_masked_owner_packet.ps1 `
  -NoBuild `
  -CameraBookmark reported_wall_floor_flicker `
  -CustomRoiList "reported_left_wall_foreground:25,185,360,440;reported_platform_mixed:610,405,1190,555;reported_lower_floor_mixed:430,545,1015,705" `
  -CustomRoisOnly `
  -ForegroundGateRois reported_left_wall_foreground `
  -OutputRoot Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\captures\rt_showcase_reported_background_taa_20260609 `
  -CaptureCount 16 `
  -MotionFrames 100 `
  -MotionLookCycles 6.0
```

Comparison against
`build\captures\rt_showcase_reported_foreground_wall_gate_wrapper_20260609`:

- foreground wall gate stayed essentially unchanged and passed
- background/mixed rows also stayed essentially unchanged:
  - `reported_platform_mixed` background mean `15.9385 -> 15.9064`
  - `reported_lower_floor_mixed` background mean `6.8258 -> 6.8058`

Decision:

- Do not keep the TAA shader experiment.
- Treat the remaining reported-view instability as entering before or outside
  the tested TAA branch, or as expected background/depth-miss camera motion
  that needs a different presentation/ownership strategy.
- Next pass should isolate:
  1. whether `CORTEX_DISABLE_SKYBOX` / visible background removal collapses the
     mixed ROI signal;
  2. whether `CORTEX_DISABLE_AUX_GEOMETRY` or transparent/water removal
     collapses the mixed ROI signal;
  3. whether a scene-local visible-background proxy should replace old-office
     HDRI visibility in this public RT Showcase angle while keeping IBL as a
     lighting/reflection source.

### RT Showcase Scene-Local Depth-Miss Background Fix - 2026-06-09

Root cause:

- `CORTEX_DISABLE_SKYBOX` did not move the reported mixed/background ROI
  metrics because the pre-geometry skybox pass was not the effective owner of
  depth-miss pixels.
- `CORTEX_DISABLE_AUX_GEOMETRY` also did not move the signal materially, so
  water/transparent/overlay compositing was not the dominant source.
- The deferred-lighting depth-miss path sampled the external HDRI whenever
  `g_EnvParams.w` was positive. Before this fix, `g_EnvParams.w` ignored
  `m_environmentState.backgroundVisible`, so disabling visible background only
  skipped the skybox pass while deferred lighting could still redraw the old
  office/studio HDRI behind the enclosed RT Showcase geometry.

Implemented:

- `Renderer_FramePostConstants.cpp` and
  `Renderer_VisibilityBufferDeferredLighting.cpp` now set background exposure
  to `0` when either `CORTEX_DISABLE_VISIBLE_BACKGROUND` is active or
  `m_environmentState.backgroundVisible` is false. IBL diffuse/specular and
  texture binding remain independent.
- `Renderer_FrameContractSnapshot.cpp` now reports
  `background_visible=false`, `background_exposure=0`, and
  `external_hdri_visible=false` when the visible background is disabled by
  policy.
- `ApplyRTShowcaseSceneControls` now defaults RT Showcase to
  `scene_local_gallery_background_hidden_external_ibl`: hidden visible HDRI,
  live IBL lighting/reflections. For diagnostics only,
  `CORTEX_RT_SHOWCASE_VISIBLE_EXTERNAL_BACKGROUND=1` restores the old visible
  HDRI background.
- `DeferredLighting.hlsl` now returns a stable scene-local neutral/gallery
  depth-miss background when visible external background exposure is zero,
  instead of returning black or sampling the HDRI.
- `tools/run_rt_showcase_wall_floor_flicker_stability_smoke.ps1` now enforces
  the new default contract: IBL enabled/bound/positive, shadows and TAA on,
  visible external HDRI off. The wrapper tolerates the existing named
  warning-only child failures while still checking scene, camera, frame
  contract, and captured metrics.
- `tools/run_rt_showcase_wall_floor_masked_owner_packet.ps1` gained
  `-DisableSkybox`, `-DisableAuxForAll`, and `-DisableVisibleBackground`
  packet controls and records those switches in the summary.

Key evidence:

- Visible-background diagnostic:
  `build\captures\rt_showcase_reported_disable_visible_background_20260609`
  proved the isolation without disabling IBL:
  `background_visible=false`, `background_exposure=0`,
  `image_based_lighting_textures_bound=true`.
- Final default packet:
  `build\captures\rt_showcase_scene_local_depthmiss_background_final_20260609`
  with no diagnostic disable switches:
  - all four packet captures exited `0` and wrote `16` BMPs each;
  - foreground wall gate passed:
    `beauty_foreground` mean `5.6323`, changed `0.1006`, large `0.0287`,
    coverage `0.9979`;
  - frame contract proved IBL remained live:
    `ibl_enabled=true`, `image_based_lighting_textures_bound=true`,
    diffuse `0.85`, specular `1.25`;
  - frame contract proved visible HDRI was not used as scene geometry:
    `background_visible=false`, `background_exposure=0`,
    `external_hdri_visible=false`,
    `environment_owner=scene_local_gallery_background_hidden_external_ibl`,
    `scene_local_visible_background_proxy_ready=true`,
    `scene_local_environment_policy=scene_local_neutral_background`,
    `scene_local_visible_background_source=authored_enclosed_room`.

Metric comparison against
`build\captures\rt_showcase_reported_foreground_wall_gate_wrapper_20260609`:

- `reported_platform_mixed` background mean `15.9385 -> 2.9643`,
  changed `0.4152 -> 0.0577`, large `0.1379 -> 0.0330`.
- `reported_lower_floor_mixed` background mean `6.8258 -> 0.2993`,
  changed `0.1981 -> 0.0098`, large `0.0294 -> 0.0016`.
- `reported_left_wall_foreground` foreground-owned gate stayed effectively
  unchanged and passed. The tiny background sliver in that ROI still reports
  high background deltas because its background coverage is only `0.0024`; keep
  using foreground coverage when interpreting that ROI.

Remaining known warnings:

- `visibility_buffer_rendered_without_visibility_motion_vectors`
- `rtv_descriptor_heap_high_water`
- debug-mask packets can also report
  `scene_visual_local_probe_table_missing:profile=gallery_public_cinematic_v1:probe_rig=gallery_visible_ibl_panels`

These warnings are still real renderer debt, but they are no longer evidence
that the reported visible-HDRI/depth-miss flicker is unfixed.

### V3 Matrix Report Preservation - 2026-06-09

Problem:

- The cross-family V3 matrix runner previously exited on the first packet
  runner failure. That protected the shell exit code, but it lost the durable
  matrix report that should say which families, motion modes, packets, and
  promotion predicates were missing.
- This made cross-scene AAA promotion work fragile during long runs: one bad
  packet could erase the exact coverage/blocker state needed for the next
  slice.

Implemented:

- `tools/run_full_scene_shader_pipeline_v3_matrix.ps1` now supports
  `-ContinueOnPacketFailure`.
- Matrix runs now always write packet-run status artifacts:
  `packet_run_status.json` and `packet_run_status.md`.
- Packet status records include `packet_root`, `motion_mode`, `ran_packet`,
  `exit_code`, and `continued_after_failure`.
- `tools/build_full_scene_shader_v3_matrix_decision.py` now consumes
  `--packet-list-json` and carries packet runner failures into each matrix
  row as `packet_exit_code`, `packet_runner_failed`, and
  `continued_after_failure`.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` now requires the
  continuation/status contract tokens so this reporting path stays wired in.

Evidence:

- Existing-packet plus missing-packet probe:
  `build\captures\v3_matrix_report_preservation_probe_20260609`
  wrote `v3_matrix_decision.json`, `v3_matrix_decision.md`,
  `packet_run_status.json`, and `packet_run_status.md`.
  The matrix explicitly reported one passed packet, one missing packet root,
  missing `kitchen`, and missing `mouse_jitter`.
- Intentional invalid-view packet-failure probe:
  `build\captures\v3_matrix_continue_on_packet_failure_probe_20260609`
  used `-RunPackets -ContinueOnPacketFailure` with
  `-ViewFilter definitely_missing_view`.
  The child packet exited `1`, the matrix wrapper still exited `0`, and the
  matrix row recorded `packet_exit_code=1`, `packet_runner_failed=true`, and
  `continued_after_failure=true`.

Interpretation:

- This does not promote the V3 AAA renderer or claim cross-family coverage is
  good enough.
- It makes failed and incomplete promotion runs inspectable, so the next
  cross-scene rendering slices can rank real blockers instead of stopping at
  the first child-process failure.

### V3 Material Quality Promotion Gate - 2026-06-09

Problem:

- `v3_material_payload.json` already checked material debug views and frame
  contract material stats, but promotion and matrix reports did not expose a
  compact material-quality decision.
- A packet could be reviewed without a single field saying whether material
  evidence was AAA-relevant: named material coverage, advanced material
  richness, reflection eligibility, unresolved fallback debt, and required
  material debug-view coverage were spread across lower-level artifacts.

Implemented:

- `tools/build_full_scene_shader_v3_promotion_decision.py` now emits
  `material_quality_gate` with thresholds, predicates, score, blockers,
  warnings, and material summary ratios.
- The gate fails promotion when:
  - named material coverage is below `0.95`;
  - advanced-feature material ratio is below `0.20`;
  - reflection-eligible material ratio is below `0.05`;
  - contract material debug-view debt is nonzero;
  - unresolved roughness/transmission fallback debt is nonzero;
  - the material missing-channel-mask debug view is absent/nonactive.
- Class-authored default roughness/transmission remains a warning, not a
  blocker, because those are authored material-class defaults rather than
  unresolved fallback.
- `tools/build_full_scene_shader_v3_matrix_decision.py` now aggregates
  `material_quality_min_score` and `material_quality_blocker_counts`, and the
  matrix markdown shows per-packet material score.
- `tools/validate_full_scene_shader_pipeline_v3_plan.py` now requires the
  material-quality gate and matrix aggregation tokens.

Evidence:

- Synthetic weak-material probe:
  direct `material_quality_gate(...)` invocation produced blockers
  `named_material_ratio_ok`, `advanced_feature_ratio_ok`,
  `reflection_eligible_ratio_ok`, `contract_debug_views_complete`, and
  `missing_channel_mask_debug_present`.
- Real packet promotion probe:
  `build\captures\v3_material_quality_gate_probe_20260609`
  reported:
  - `review_packet_passed=true`;
  - `material_quality_gate.ready=true`;
  - score `1.0000`;
  - named material ratio `1.0000`;
  - advanced feature ratio `0.5333`;
  - reflection eligible ratio `0.3000`;
  - contract debug-view debt `0`;
  - unresolved roughness/transmission fallback `0/0`.
- Matrix probe:
  `build\captures\v3_material_quality_matrix_probe_20260609`
  over the stress reflection closeup/static packet reported
  `full_matrix_ready=true`, `material_quality_min_score=1.0`, and empty
  `material_quality_blocker_counts`.

Validation:

- `python -m py_compile tools\build_full_scene_shader_v3_promotion_decision.py tools\build_full_scene_shader_v3_matrix_decision.py tools\validate_full_scene_shader_pipeline_v3_plan.py`
- `python tools\validate_full_scene_shader_pipeline_v3_plan.py`
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_full_scene_shader_pipeline_v3_matrix.ps1 -OutputRoot build\captures\v3_material_quality_matrix_probe_20260609 -PacketRoots build\captures\v3_runtime_scene_local_resource_contract_smoke_20260609 -RequiredFamilies stress_rt_showcase_reflection_closeup -RequiredMotionModes static`

Interpretation:

- This is a promotion/harness hardening slice. It does not change default
  beauty and does not claim the final AAA look is solved.
- It gives future enclosed/heavy-lighting scene packets a concrete material
  blocker vocabulary instead of making material weakness a manual visual
  judgment after the fact.
